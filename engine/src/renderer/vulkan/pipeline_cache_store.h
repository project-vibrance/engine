#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace vibrance::vulkan
{
// The opaque driver payload is never patched or truncated. The envelope detects
// partial writes and corruption before any persisted bytes reach the driver.
inline constexpr std::size_t kMaximumPipelineCacheBytes = 64u * 1024u * 1024u;
inline constexpr std::size_t kPipelineCacheFileHeaderBytes = 72u;
inline constexpr uint64_t kPipelineCacheLayoutVersion = 1u;
#if defined(_M_ARM64) || defined(__aarch64__)
inline constexpr uint32_t kPipelineCacheArchitecture = 1u;
#elif defined(_M_X64) || defined(__x86_64__)
inline constexpr uint32_t kPipelineCacheArchitecture = 2u;
#elif defined(_M_IX86) || defined(__i386__)
inline constexpr uint32_t kPipelineCacheArchitecture = 3u;
#else
inline constexpr uint32_t kPipelineCacheArchitecture = 0u;
#endif

inline uint64_t pipeline_cache_hash(std::span<const std::byte> bytes,
    uint64_t hash = 14695981039346656037ull)
{
    for (const auto byte : bytes)
    {
        hash ^= std::to_integer<uint8_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct PipelineCacheIdentity
{
    uint32_t vendor = 0u;
    uint32_t device = 0u;
    uint32_t driver = 0u;
    uint32_t pointerBytes = sizeof(void*);
    uint32_t architecture = kPipelineCacheArchitecture;
    std::array<std::byte, 16> uuid {};
    uint64_t shaderFingerprint = 0u;
    uint64_t layoutVersion = kPipelineCacheLayoutVersion;
};

inline uint64_t pipeline_cache_integer(std::span<const std::byte> bytes,
    std::size_t offset, std::size_t size)
{
    uint64_t value = 0u;
    for (std::size_t index = 0u; index < size; ++index)
        value |= uint64_t(std::to_integer<uint8_t>(bytes[offset + index])) << (index * 8u);
    return value;
}

inline void pipeline_cache_append(std::vector<std::byte>& bytes, uint64_t value,
    std::size_t size)
{
    for (std::size_t index = 0u; index < size; ++index)
        bytes.push_back(static_cast<std::byte>((value >> (index * 8u)) & 255u));
}

inline bool valid_pipeline_cache_payload(std::span<const std::byte> bytes,
    const PipelineCacheIdentity& identity)
{
    // Vulkan specifies these exact little-endian offsets independently of C
    // structure padding and host byte order (VkPipelineCacheHeaderVersionOne).
    return bytes.size() >= 32u && bytes.size() <= kMaximumPipelineCacheBytes &&
        pipeline_cache_integer(bytes, 0u, 4u) == 32u &&
        pipeline_cache_integer(bytes, 4u, 4u) == 1u &&
        pipeline_cache_integer(bytes, 8u, 4u) == identity.vendor &&
        pipeline_cache_integer(bytes, 12u, 4u) == identity.device &&
        std::equal(identity.uuid.begin(), identity.uuid.end(), bytes.begin() + 16u);
}

inline std::string pipeline_cache_key(const PipelineCacheIdentity& identity)
{
    std::ostringstream key;
    key << "v1-" << std::hex << std::setfill('0') << std::setw(8) << identity.vendor
        << '-' << std::setw(8) << identity.device << '-' << std::setw(8) << identity.driver << '-';
    for (const auto byte : identity.uuid)
        key << std::setw(2) << unsigned(std::to_integer<uint8_t>(byte));
    key << '-' << std::setw(16) << identity.shaderFingerprint << '-'
        << identity.architecture << '-' << identity.pointerBytes << '-' << identity.layoutVersion;
    return key.str();
}

inline std::vector<std::byte> encode_pipeline_cache(std::span<const std::byte> payload,
    const PipelineCacheIdentity& identity)
{
    if (!valid_pipeline_cache_payload(payload, identity)) return {};
    constexpr std::array<std::byte, 8> magic {
        std::byte{'V'}, std::byte{'I'}, std::byte{'B'}, std::byte{'V'},
        std::byte{'K'}, std::byte{'P'}, std::byte{'C'}, std::byte{'1'} };
    std::vector<std::byte> bytes;
    bytes.reserve(kPipelineCacheFileHeaderBytes + payload.size());
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    pipeline_cache_append(bytes, 1u, 4u);
    pipeline_cache_append(bytes, payload.size(), 4u);
    pipeline_cache_append(bytes, identity.vendor, 4u);
    pipeline_cache_append(bytes, identity.device, 4u);
    pipeline_cache_append(bytes, identity.driver, 4u);
    pipeline_cache_append(bytes, identity.pointerBytes | (identity.architecture << 16u), 4u);
    bytes.insert(bytes.end(), identity.uuid.begin(), identity.uuid.end());
    pipeline_cache_append(bytes, identity.shaderFingerprint, 8u);
    pipeline_cache_append(bytes, identity.layoutVersion, 8u);
    pipeline_cache_append(bytes, pipeline_cache_hash(payload), 8u);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

inline std::vector<std::byte> decode_pipeline_cache(std::span<const std::byte> bytes,
    const PipelineCacheIdentity& identity)
{
    constexpr std::array<std::byte, 8> magic {
        std::byte{'V'}, std::byte{'I'}, std::byte{'B'}, std::byte{'V'},
        std::byte{'K'}, std::byte{'P'}, std::byte{'C'}, std::byte{'1'} };
    if (bytes.size() < kPipelineCacheFileHeaderBytes + 32u ||
        bytes.size() > kPipelineCacheFileHeaderBytes + kMaximumPipelineCacheBytes ||
        !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
        pipeline_cache_integer(bytes, 8u, 4u) != 1u ||
        pipeline_cache_integer(bytes, 12u, 4u) != bytes.size() - kPipelineCacheFileHeaderBytes ||
        pipeline_cache_integer(bytes, 16u, 4u) != identity.vendor ||
        pipeline_cache_integer(bytes, 20u, 4u) != identity.device ||
        pipeline_cache_integer(bytes, 24u, 4u) != identity.driver ||
        pipeline_cache_integer(bytes, 28u, 4u) !=
            (identity.pointerBytes | (identity.architecture << 16u)) ||
        !std::equal(identity.uuid.begin(), identity.uuid.end(), bytes.begin() + 32u) ||
        pipeline_cache_integer(bytes, 48u, 8u) != identity.shaderFingerprint ||
        pipeline_cache_integer(bytes, 56u, 8u) != identity.layoutVersion)
        return {};
    const auto payload = bytes.subspan(kPipelineCacheFileHeaderBytes);
    if (!valid_pipeline_cache_payload(payload, identity) ||
        pipeline_cache_integer(bytes, 64u, 8u) != pipeline_cache_hash(payload))
        return {};
    return { payload.begin(), payload.end() };
}

inline std::filesystem::path pipeline_cache_directory()
{
    if (const char* overridePath = std::getenv("VIBRANCE_PIPELINE_CACHE_ROOT");
        overridePath && *overridePath)
        return std::filesystem::path(overridePath);
#if defined(_WIN32)
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local) / L"Vibrance" / L"Cache" / L"Vulkan";
#else
    if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache && *cache)
        return std::filesystem::path(cache) / "Vibrance" / "Vulkan";
    if (const char* userHome = std::getenv("HOME"); userHome && *userHome)
        return std::filesystem::path(userHome) / ".cache" / "Vibrance" / "Vulkan";
#endif
    return {};
}

class PipelineCacheStore
{
public:
    explicit PipelineCacheStore(PipelineCacheIdentity identity,
        std::filesystem::path root = pipeline_cache_directory()) :
        identity_(identity), path_(root.empty() ? std::filesystem::path{} :
            root / (pipeline_cache_key(identity) + ".bin")),
        seedKey_(root.generic_string() + '/' + pipeline_cache_key(identity)) {}

    const PipelineCacheIdentity& identity() const { return identity_; }
    const std::filesystem::path& path() const { return path_; }

    std::vector<std::byte> load() const
    {
        {
            std::lock_guard lock(seeds_mutex());
            if (const auto found = seeds().find(seedKey_); found != seeds().end())
                return found->second.bytes;
        }
        return load_file();
    }

    std::vector<std::byte> load_file() const
    {
        if (path_.empty()) return {};
        std::vector<std::byte> bytes;
#if defined(_WIN32)
        const HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        struct CloseReadHandle
        {
            HANDLE value;
            ~CloseReadHandle() { CloseHandle(value); }
        } closeFile { file };
        LARGE_INTEGER size {};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
            static_cast<uint64_t>(size.QuadPart) >
                kMaximumPipelineCacheBytes + kPipelineCacheFileHeaderBytes)
        {
            return {};
        }
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0u;
        const bool complete = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
            &read, nullptr) && read == bytes.size();
        if (!complete) return {};
#else
        std::ifstream file(path_, std::ios::binary | std::ios::ate);
        if (!file) return {};
        const auto size = file.tellg();
        if (size < 0 || static_cast<uint64_t>(size) >
            kMaximumPipelineCacheBytes + kPipelineCacheFileHeaderBytes) return {};
        bytes.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return {};
#endif
        return decode_pipeline_cache(bytes, identity_);
    }

    bool publish(std::span<const std::byte> payload) const
    {
        if (!valid_pipeline_cache_payload(payload, identity_)) return false;
        {
            std::lock_guard lock(seeds_mutex());
            auto& entries = seeds();
            if (!entries.contains(seedKey_) && entries.size() >= 4u)
            {
                const auto oldest = std::min_element(entries.begin(), entries.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.generation < right.second.generation;
                    });
                entries.erase(oldest);
            }
            entries.insert_or_assign(seedKey_, Seed {
                { payload.begin(), payload.end() }, ++seed_generation() });
        }
        if (path_.empty()) return false;
        const auto bytes = encode_pipeline_cache(payload, identity_);
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) return false;
        static std::atomic<uint64_t> serial { 0u };
#if defined(_WIN32)
        const auto process = GetCurrentProcessId();
#else
        const auto process = getpid();
#endif
        auto temporary = path_;
        temporary += ".tmp-" + std::to_string(process) + '-' +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            '-' + std::to_string(serial.fetch_add(1u));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            output.flush();
            if (!output)
            {
                output.close();
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        // Each writer owns its temporary file. Readers see the previous complete
        // cache or the replacement; racing writers never expose partial data.
#if defined(_WIN32)
        const bool replaced = MoveFileExW(temporary.c_str(), path_.c_str(),
            MOVEFILE_REPLACE_EXISTING) != FALSE;
#else
        std::filesystem::rename(temporary, path_, error);
        const bool replaced = !error;
#endif
        if (!replaced) std::filesystem::remove(temporary, error);
        return replaced;
    }

private:
    struct Seed { std::vector<std::byte> bytes; uint64_t generation; };
    static std::mutex& seeds_mutex() { static std::mutex value; return value; }
    static std::unordered_map<std::string, Seed>& seeds()
    {
        static std::unordered_map<std::string, Seed> value;
        return value;
    }
    static uint64_t& seed_generation() { static uint64_t value = 0u; return value; }
    PipelineCacheIdentity identity_;
    std::filesystem::path path_;
    std::string seedKey_;
};
}
