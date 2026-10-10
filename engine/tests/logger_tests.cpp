#include <vibranceUI/core/logger.h>
#include <vibranceUI/core/crash_diagnostics.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <csignal>
#if defined(_WIN32)
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    LoggerOutputOptions options;
    options.directory = argc > 2 ? std::filesystem::path(argv[2]) :
        std::filesystem::temp_directory_path() / ("vibrance-logger-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    options.filePrefix = "logger-test";
    Logger::configure_output(options);
    auto* logger = Logger::fetch_logger();
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
        if (mode == "--crash-worker")
        {
            std::thread worker([] { throw std::runtime_error("worker crash fixture"); });
            worker.join();
        }
#if defined(_WIN32)
        if (mode == "--crash-native") RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
        return 2;
    }
    const auto path = logger->output_path();
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
    const bool passed = messages == 400 && rendererError && caughtException && operationException;
    logger->get_file_stream().close();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return passed ? 0 : 6;
}
