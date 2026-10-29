#include <vibranceUI/core/windows_diagnostics.h>
#include <fstream>
#include <iostream>

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("vibrance-bootstrap-diagnostics-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto blocker = directory / L"not-a-directory";
    { std::ofstream file(blocker); file << "blocked log directory fixture"; }
    const auto originalLocal = windows_diagnostic_paths::environment_path(L"LOCALAPPDATA");
    SetEnvironmentVariableW(L"LOCALAPPDATA", directory.c_str());
    windows_diagnostics::initialise(L"installer-bootstrap", blocker / L"logs");
    const auto path = windows_diagnostics::logPath;
    windows_diagnostics::log(L"bootstrap fallback fixture");
    bool passed = windows_diagnostics::logFile != INVALID_HANDLE_VALUE &&
        path.parent_path() == directory / L"Vibrance/logs";
    {
        std::ifstream file(path);
        const std::string text { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        passed &= text.find("SESSION START: installer-bootstrap") != std::string::npos &&
            text.find("bootstrap fallback fixture") != std::string::npos;
    }
    // Reinitialisation must close the previous writer and refresh its state.
    windows_diagnostics::initialise(L"app-bootstrap", directory / L"explicit");
    passed &= windows_diagnostics::logPath.parent_path() == directory / L"explicit";
    CloseHandle(windows_diagnostics::logFile);
    windows_diagnostics::logFile = INVALID_HANDLE_VALUE;
    if (crash_diagnostics::emergencyFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(crash_diagnostics::emergencyFile);
        crash_diagnostics::emergencyFile = INVALID_HANDLE_VALUE;
    }
    SetEnvironmentVariableW(L"LOCALAPPDATA", originalLocal.empty() ? nullptr : originalLocal.c_str());
    std::error_code ignored;
    for (auto file : { path, windows_diagnostics::logPath })
    {
        std::filesystem::remove(file, ignored);
        file += ".crash.txt";
        std::filesystem::remove(file, ignored);
    }
    std::filesystem::remove(blocker, ignored);
    for (const auto& folder : { directory / L"Vibrance/logs", directory / L"Vibrance",
            directory / L"explicit", directory }) std::filesystem::remove(folder, ignored);
    if (!passed) std::cerr << "Bootstrap diagnostics lost a writable fallback or the session-start marker.\n";
    return passed ? 0 : 1;
}
