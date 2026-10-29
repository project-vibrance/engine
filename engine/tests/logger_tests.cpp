#include <vibranceUI/core/logger.h>
#include <vibranceUI/core/crash_diagnostics.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <csignal>
#include <vibranceUI/core/path_utf8.h>
#if defined(_WIN32)
#include <windows.h>
#include <vibranceUI/core/windows_diagnostic_paths.h>

namespace
{
unsigned dumpCalls = 0;

BOOL WINAPI reentrant_dump(HANDLE, DWORD, HANDLE dump, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION info, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION)
{
    if (++dumpCalls != 1) return FALSE;
    EXCEPTION_RECORD secondary {};
    secondary.ExceptionCode = 0xC00000FFu;
    secondary.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    secondary.ExceptionAddress = info->ExceptionPointers->ExceptionRecord->ExceptionAddress;
    EXCEPTION_POINTERS nested { &secondary, info->ExceptionPointers->ContextRecord };
    if (crash_diagnostics::unhandled(&nested) != EXCEPTION_CONTINUE_SEARCH) return FALSE;
    constexpr char contents[] = "first crash dump fixture";
    DWORD written = 0;
    return WriteFile(dump, contents, sizeof(contents) - 1, &written, nullptr) &&
        written == sizeof(contents) - 1;
}
}
#endif

int main(int argc, char* argv[])
{
#if defined(_WIN32) && defined(VIBRANCE_TEST_UTF8_CODE_PAGE)
    if (GetACP() != CP_UTF8) return 77;
#endif
    LoggerOutputOptions options;
    options.directory = argc > 2 ? std::filesystem::path(argv[2]) :
        std::filesystem::temp_directory_path() / ("vibrance-logger-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    options.filePrefix = "logger-test";
    if (argc == 1)
        options.directory /= std::filesystem::path(u8"\u6e2c\u8a66-\u0442\u0435\u0441\u0442-\U0001F31F");
    Logger::configure_output(options);
    auto* logger = Logger::fetch_logger();
#if defined(_WIN32)
    if (argc > 1 && std::string(argv[1]) == "--crash-worker-no-filter")
        SetUnhandledExceptionFilter(nullptr);
#endif
    crash_diagnostics::initialise(logger->output_path());
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (argc > 1)
    {
        const std::string mode = argv[1];
        if (mode == "--crash-abort") std::abort();
        if (mode == "--crash-terminate")
        {
            std::thread worker([] {
                try { throw std::runtime_error("worker crash fixture"); }
                catch (...) { std::terminate(); }
            });
            worker.join();
        }
        if (mode == "--crash-worker" || mode == "--crash-worker-reinitialise" ||
            mode == "--crash-worker-reconfigure" || mode == "--crash-worker-no-filter")
        {
            if (mode == "--crash-worker-reconfigure")
            {
                options.filePrefix = "logger-reinitialised";
                Logger::configure_output(options);
            }
            if (mode == "--crash-worker-reinitialise")
                crash_diagnostics::initialise(logger->output_path());
            if (mode == "--crash-worker-reconfigure")
                crash_diagnostics::initialise(logger->output_path());
            std::thread worker([] { throw std::runtime_error("worker crash fixture"); });
            worker.join();
        }
#if defined(_WIN32)
        if (mode == "--crash-native") RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        if (mode == "--crash-reentrant")
        {
            crash_diagnostics::writeDump = reentrant_dump;
            RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        }
#endif
        return 2;
    }
    const auto path = logger->output_path();
    bool sessionRan = false;
    if (Logger::run_session("Unicode startup fixture", [&] {
            sessionRan = true;
            return 0;
        }) != 0 || !sessionRan) return 8;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.emplace_back([logger, worker] {
            for (int line = 0; line < 100; ++line)
                logger->info("concurrent-message-" + std::to_string(worker) + "-" + std::to_string(line));
        });
    for (auto& worker : workers) worker.join();
    Logger::configure_output(options);
    if (logger->output_path() != path) return 3;
    logger->set_vulkan_renderer_logging_mode(false);
    logger->vulkan(LogLevel::eError, "release renderer failure fixture");
    try { throw std::runtime_error("caught exception fixture"); }
    catch (const std::exception&) {}
    auto caughtReport = path;
    caughtReport += ".crash.txt";
    {
        std::ifstream emergency(caughtReport);
        if (emergency.peek() != std::char_traits<char>::eof()) return 7;
    }
    if (Logger::run_session("fixture", []() -> int {
        const LogOperation operation("throwing operation");
        throw std::runtime_error("session exception fixture");
    }) != EXIT_FAILURE) return 4;
    std::ifstream input(path);
    std::string line;
    unsigned messages = 0;
    bool rendererError = false, caughtException = false, operationException = false;
    while (std::getline(input, line))
    {
        const auto marker = line.find("concurrent-message-");
        if (marker != std::string::npos)
        {
            if (line.find("concurrent-message-", marker + 1) != std::string::npos ||
                line.find("[thread ") == std::string::npos) return 5;
            ++messages;
        }
        rendererError |= line.find("release renderer failure fixture") != std::string::npos;
        caughtException |= line.find("UNHANDLED EXCEPTION: session exception fixture") != std::string::npos;
        operationException |= line.find("EXCEPTION throwing operation") != std::string::npos;
    }
    bool passed = messages == 400 && rendererError && caughtException && operationException;
    logger->info("repetition fixture");
    for (int index = 0; index < 10; ++index) logger->info("repetition fixture");
    logger->info("flush repetition summary");
    std::ifstream repeated(path);
    const std::string repeatedText { std::istreambuf_iterator<char>(repeated), std::istreambuf_iterator<char>() };
    passed &= repeatedText.find("[repeated 10 times]") != std::string::npos;
    options.directory /= "rotation";
    Logger::configure_output(options);
    const auto rotatedPath = logger->output_path();
    crash_diagnostics::initialise(rotatedPath);
    // Exercise the real logger writer across enough data to replace all parts.
    const std::string block(5u * 1024u * 1024u, 'x');
    for (int index = 0; index < 7; ++index) logger->write_file_output(block);
    logger->write_file_output("newest diagnostic\n");
    logger->flush_file_output();
    crash_diagnostics::write("emergency report after rotation\n");
    unsigned parts = 0;
    for (const auto& entry : std::filesystem::directory_iterator(options.directory))
    {
        passed &= entry.file_size() <= 5u * 1024u * 1024u;
        if (vibrance::path_utf8(entry.path().filename()).find(".crash.txt") == std::string::npos) ++parts;
    }
    passed &= parts == 5 && logger->output_path() == rotatedPath;
    std::ifstream newest(rotatedPath);
    std::string newestLine;
    std::getline(newest, newestLine);
    passed &= newestLine == "newest diagnostic";
    auto emergencyPath = rotatedPath;
    emergencyPath += ".crash.txt";
    std::ifstream emergency(emergencyPath);
    const std::string emergencyText { std::istreambuf_iterator<char>(emergency), std::istreambuf_iterator<char>() };
    passed &= emergencyText.find("emergency report after rotation") != std::string::npos;
#if defined(_WIN32)
    // A blocked/invalid preferred location must not disable all logging or
    // lose fatal-report registration. Use an ordinary file as the blocker.
    const auto requestedDirectory = options.directory;
    const auto blocker = requestedDirectory / "not-a-directory";
    { std::ofstream file(blocker); file << "blocked preferred log path fixture"; }
    options.directory = blocker / "logs";
    options.directoryName = "Vibrance/logger-fallback-fixture-" + std::to_string(GetCurrentProcessId());
    Logger::configure_output(options);
    const auto fallbackPath = logger->output_path();
    passed &= logger->is_file_open() && fallbackPath.parent_path() != options.directory;
    logger->info("fallback diagnostic fixture");
    logger->flush_file_output();
    std::ifstream fallback(fallbackPath);
    const std::string fallbackText { std::istreambuf_iterator<char>(fallback), std::istreambuf_iterator<char>() };
    passed &= fallbackText.find("fallback diagnostic fixture") != std::string::npos;
    options.directory = requestedDirectory;
    Logger::configure_output(options);
    fallback.close();
    std::error_code fallbackError;
    std::filesystem::remove(fallbackPath, fallbackError);
    auto fallbackEmergency = fallbackPath;
    fallbackEmergency += ".crash.txt";
    std::filesystem::remove(fallbackEmergency, fallbackError);
    std::filesystem::remove(fallbackPath.parent_path(), fallbackError);
    std::filesystem::remove(fallbackPath.parent_path().parent_path(), fallbackError);
    std::filesystem::remove(blocker, fallbackError);
#endif
    logger->get_file_stream().close();
    input.close();
    repeated.close();
    newest.close();
    emergency.close();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    auto firstEmergency = path;
    firstEmergency += ".crash.txt";
    std::filesystem::remove(firstEmergency, ignored);
    for (const auto& entry : std::filesystem::directory_iterator(options.directory))
        std::filesystem::remove(entry.path(), ignored);
    std::filesystem::remove(options.directory, ignored);
    std::filesystem::remove(options.directory.parent_path(), ignored);
    return passed ? 0 : 6;
}
