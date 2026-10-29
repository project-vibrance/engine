#include <vibranceUI/core/runtime_log_storage.h>
#include <fstream>
#include <stdexcept>
#include <iostream>

int main()
{
    namespace storage = runtime_log_storage;
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("vibrance-retention-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(directory)) return 1;
    struct Cleanup
    {
        fs::path directory;
        ~Cleanup()
        {
            // Every file belongs to this exclusively created fixture directory.
            std::error_code ignored;
            for (const auto& entry : fs::directory_iterator(directory)) fs::remove(entry.path(), ignored);
            fs::remove(directory, ignored);
        }
    } cleanup { directory };
    const auto make = [&](std::string name, std::uintmax_t size, unsigned ageHours = 0) {
        const auto path = directory / name;
        std::ofstream(path).put('x');
        fs::resize_file(path, size);
        fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(ageHours));
        return path;
    };
    const auto unrelated = make("notes.txt", 80u * 1024u * 1024u);
    const auto similarlyNamed = make("vibrance-app-user-notes.txt", 10);
    const auto datedNotes = make("vibrance-app_2026-report.txt", 10, 24 * 8);
    const auto expired = make("vibrance-app_2000-01-01_00-00-00_0_0_0.txt", 10, 24 * 8);
    const auto active = make("vibrance-app_2026-01-01_00-00-00_0_0_99.txt", storage::fileLimit);
#if defined(_WIN32)
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    const auto otherLiveSession = make("vibrance-installer-bootstrap-20260101-000000-" +
        std::to_string(process) + ".log", storage::fileLimit, 24 * 8);
    for (unsigned index = 1; index <= 14; ++index)
        make("vibrance-installer_2026-01-01_00-00-00_0_0_" + std::to_string(index) + ".txt", storage::fileLimit, index);
    for (unsigned index = 1; index <= 6; ++index)
        make("vibrance-updater-20260101-00000" + std::to_string(index) + "-0.log.dmp", 100);
    storage::prune(directory, active);
    if (!fs::exists(active) || !fs::exists(otherLiveSession) || !fs::exists(unrelated) ||
        !fs::exists(similarlyNamed) || !fs::exists(datedNotes) || fs::exists(expired)) return 2;
    std::uintmax_t bytes = 0;
    unsigned dumps = 0;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        const auto kind = storage::identify(entry.path().filename().string());
        if (kind.owned) { bytes += entry.file_size(); dumps += kind.dump; }
    }
    if (bytes > storage::directoryLimit - storage::fileLimit || dumps > storage::retainedDumps) return 3;
    for (unsigned index = 0; index < 8; ++index)
    {
        if (!storage::rotate(active)) return 4;
        std::ofstream(active) << "part " << index;
    }
    if (fs::exists(storage::archive_path(active, storage::archives + 1))) return 5;
    std::ifstream current(active);
    std::string newest;
    std::getline(current, newest);
    if (newest != "part 7") return 6;
    // Legacy names have no process identifier; seconds must not become a PID.
    if (storage::identify("vibrance-app_2026-01-01_00-00-04.txt").process != 0) return 7;
    for (const auto prefix : { "vibrance-app-bootstrap", "vibrance-app-entry", "vibrance-installer-runtime" })
    {
        const auto kind = storage::identify(std::string(prefix) + "-20261029-010203-1234.log");
        if (!kind.owned || kind.process != 1234) return 8;
    }
    std::cout << "Retention budget, rotation, expiry, dump count and unrelated-file protection passed.\n";
    return 0;
}
