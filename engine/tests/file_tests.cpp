#include <vibranceUI/core/file.h>
#include <vibranceUI/core/process.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("vibrance-file-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "nested" / std::filesystem::path(u8"préférences.json");
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    expect(write_file_atomically(path, "first"), "create nested document");
    expect(write_file_atomically(path, "replacement"), "replace existing document");
    expect(read_text_file(path) == "replacement", "replacement contents");
    const std::string binary("a\0b", 3);
    expect(write_file_atomically(path, binary) && read_text_file(path) == binary,
        "binary data and Unicode path survive replacement");
    expect(write_file_atomically(path, {}) && read_text_file(path).empty(), "empty document");
    expect(!write_file_atomically({}, "bad"), "reject empty destination");
    expect(!write_file_atomically(path / "child", "bad"), "reject a file as parent");
    const auto blocked = directory / "blocked";
    std::filesystem::create_directory(blocked);
    expect(write_file_atomically(blocked / "sentinel", "preserve"), "seed blocked destination");
    expect(!write_file_atomically(blocked, "bad"), "do not replace a directory");
    expect(read_text_file(blocked / "sentinel") == "preserve", "failed replacement preserves destination");

    const std::vector<std::string> documents {
        std::string(16384, 'a'), std::string(16384, 'b'), std::string(16384, 'c') };
    std::atomic<bool> writesSucceeded = true;
    std::vector<std::thread> writers;
    for (const auto& document : documents)
        writers.emplace_back([&, document] {
            for (int iteration = 0; iteration < 8; ++iteration)
                if (!write_file_atomically(path, document)) writesSucceeded = false;
        });
    for (auto& writer : writers) writer.join();
    expect(writesSucceeded, "concurrent writers use independent staging files");
    const auto saved = read_text_file(path);
    expect(saved == documents[0] || saved == documents[1] || saved == documents[2],
        "a complete concurrent document wins");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        expect(entry.path().filename().string().find(".tmp-") == std::string::npos,
            "temporary files and directories are removed");
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    const auto executable = current_executable_path();
    expect(executable.is_absolute() && std::filesystem::is_regular_file(executable),
        "resolve current executable");
#endif
    // Only this test's uniquely created directory is removed.
    std::filesystem::remove_all(directory);
    return passed ? 0 : 1;
}
