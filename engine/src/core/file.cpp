#include <vibranceUI/core/file.h>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::vector<unsigned char> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        return {};
    }
    return bytes;
}

std::string read_text_file(const std::filesystem::path& path)
{
    const std::vector<unsigned char> bytes = read_binary_file(path);
    return std::string(bytes.begin(), bytes.end());
}

bool write_file_atomically(const std::filesystem::path& path, std::string_view bytes)
{
    if (path.empty() || path.filename().empty() ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return false;

    std::error_code error;
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(parent, error);
    if (error) return false;

    // An exclusively created sibling directory prevents concurrent writes
    // from truncating one another's staging file, on every supported platform.
    static std::atomic<std::uint64_t> sequence { 0 };
    std::filesystem::path temporaryDirectory;
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        auto candidate = parent / path.filename();
        candidate += ".tmp-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        if (std::filesystem::create_directory(candidate, error))
        {
            temporaryDirectory = std::move(candidate);
            break;
        }
        if (error) return false;
    }
    if (temporaryDirectory.empty()) return false;
    const auto temporary = temporaryDirectory / "document";
    struct Cleanup
    {
        const std::filesystem::path& file;
        const std::filesystem::path& directory;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup { temporary, temporaryDirectory };

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!bytes.empty()) output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) return false;
#if defined(_WIN32)
    // MoveFileEx can deny simultaneous replacements of one destination even
    // with independent staging files. Serialise the short replacement step.
    static std::mutex replacementMutex;
    const std::lock_guard lock(replacementMutex);
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}
