#include "../src/renderer/vulkan/pipeline_cache_store.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace vibrance::vulkan;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

PipelineCacheIdentity identity()
{
    PipelineCacheIdentity value;
    value.vendor = 0x12345678u;
    value.device = 0x87654321u;
    value.driver = 0x01020304u;
    value.shaderFingerprint = 0x0123456789abcdefull;
    for (std::size_t index = 0u; index < value.uuid.size(); ++index)
        value.uuid[index] = static_cast<std::byte>(index + 1u);
    return value;
}

std::vector<std::byte> payload(const PipelineCacheIdentity& value, uint8_t variant = 0u)
{
    std::vector<std::byte> bytes;
    pipeline_cache_append(bytes, 32u, 4u);
    pipeline_cache_append(bytes, 1u, 4u);
    pipeline_cache_append(bytes, value.vendor, 4u);
    pipeline_cache_append(bytes, value.device, 4u);
    bytes.insert(bytes.end(), value.uuid.begin(), value.uuid.end());
    bytes.resize(512u, static_cast<std::byte>(variant));
    return bytes;
}

void corruption_and_identity_checks()
{
    const auto id = identity();
    const auto original = payload(id);
    const auto encoded = encode_pipeline_cache(original, id);
    require(decode_pipeline_cache(encoded, id) == original,
        "A valid opaque cache payload did not survive its envelope");
    for (std::size_t size = 0u; size < encoded.size(); ++size)
        require(decode_pipeline_cache(std::span(encoded).first(size), id).empty(),
            "A truncated cache was accepted");
    for (std::size_t index = 0u; index < encoded.size(); ++index)
    {
        auto corrupt = encoded;
        corrupt[index] ^= std::byte{1u};
        require(decode_pipeline_cache(corrupt, id).empty(),
            "A corrupt identity, length, checksum or opaque payload reached the driver");
    }
    auto trailing = encoded;
    trailing.push_back(std::byte{0u});
    require(decode_pipeline_cache(trailing, id).empty(), "Trailing cache data was accepted");
    const auto isolated = [&](const auto& change) {
        auto different = id;
        change(different);
        require(pipeline_cache_key(different) != pipeline_cache_key(id) &&
            decode_pipeline_cache(encoded, different).empty(),
            "Different GPU, driver, shader or architecture identities shared a cache");
    };
    isolated([](auto& value) { ++value.vendor; });
    isolated([](auto& value) { ++value.device; });
    isolated([](auto& value) { ++value.driver; });
    isolated([](auto& value) { value.uuid[0] ^= std::byte{1u}; });
    isolated([](auto& value) { ++value.shaderFingerprint; });
    isolated([](auto& value) { ++value.pointerBytes; });
    isolated([](auto& value) { ++value.architecture; });
    isolated([](auto& value) { ++value.layoutVersion; });
    for (const auto offset : { 0u, 4u, 8u, 12u, 16u })
    {
        auto corrupt = original;
        corrupt[offset] ^= std::byte{1u};
        require(!valid_pipeline_cache_payload(corrupt, id), "An incompatible Vulkan header was accepted");
    }
    require(original[8] == std::byte{0x78u} && original[11] == std::byte{0x12u},
        "The Vulkan header was not validated in its specified little-endian order");
}

void file_and_seed_checks(const std::filesystem::path& root)
{
    const auto id = identity();
    const auto original = payload(id, 7u);
    PipelineCacheStore store(id, root / "normal");
    require(store.load().empty(), "A fresh identity unexpectedly had a seed");
    require(store.publish(original) && store.load_file() == original,
        "An atomic cache publication did not survive a disk round trip");
    PipelineCacheStore laterWindow(id, root / "normal");
    require(laterWindow.load() == original, "A later window did not receive its compatible binary seed");
    {
        std::ofstream corrupt(store.path(), std::ios::binary | std::ios::trunc);
        corrupt << "interrupted write";
    }
    require(store.load_file().empty() && laterWindow.load() == original,
        "Corrupt disk data displaced a valid in-process binary seed");
    require(store.publish(original), "A corrupt cache was not safely replaced");

    const auto blockedRoot = root / "unavailable";
    std::ofstream(blockedRoot) << "The cache directory is unavailable";
    PipelineCacheStore unavailable(id, blockedRoot / "child");
    require(!unavailable.publish(original) && unavailable.load() == original,
        "An unavailable disk cache prevented best-effort memory reuse");

    PipelineCacheStore oversized(id, root / "oversized");
    std::filesystem::create_directories(oversized.path().parent_path());
    {
        std::ofstream file(oversized.path(), std::ios::binary);
        file.seekp(kMaximumPipelineCacheBytes + kPipelineCacheFileHeaderBytes);
        file.put('x');
    }
    require(oversized.load_file().empty(), "An oversized cache file was read into memory");

    PipelineCacheStore concurrent(id, root / "concurrent");
    require(concurrent.publish(original), "The concurrent fixture could not start");
    std::atomic<unsigned> active { 4u };
    std::atomic<unsigned> saved { 0u };
    std::array<std::thread, 4> writers;
    for (unsigned index = 0u; index < writers.size(); ++index)
        writers[index] = std::thread([&, index] {
            for (unsigned iteration = 0u; iteration < 32u; ++iteration)
                saved += concurrent.publish(payload(id, static_cast<uint8_t>(index * 32u + iteration))) ? 1u : 0u;
            --active;
        });
    bool completeReads = true;
    unsigned acceptedReads = 0u;
    while (active > 0u)
    {
        const auto read = concurrent.load_file();
        // Windows may reject an open while another process replaces the file.
        // A cache miss is a safe cold start; every accepted blob must be one
        // complete publication rather than a mixture of concurrent writes.
        if (read.empty()) continue;
        ++acceptedReads;
        completeReads = read == payload(id, std::to_integer<uint8_t>(read[32])) && completeReads;
    }
    for (auto& writer : writers) writer.join();
    require(completeReads && acceptedReads > 0u && saved > 0u && !concurrent.load_file().empty(),
        "Concurrent writers exposed a partial or incompatible cache to a reader");
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Pass a private test output directory");
        const auto root = std::filesystem::path(argv[1]) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        corruption_and_identity_checks();
        file_and_seed_checks(root);
        std::cout << "Pipeline cache validation, isolation, corruption and concurrent publication passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
