#include <vibranceUI/core/single_instance.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    std::mutex& active_identifiers_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_set<std::string>& active_identifiers()
    {
        static std::unordered_set<std::string> identifiers;
        return identifiers;
    }

    std::string normalized_identifier(const SingleInstanceOptions& options)
    {
        if (!options.identifier.empty())
        {
            return options.identifier;
        }
        if (!options.applicationName.empty())
        {
            return options.applicationName;
        }
        return "application";
    }

    std::string identifier_hash(std::string_view identifier)
    {
        std::uint64_t value = 14695981039346656037ull;
        for (const unsigned char byte : identifier)
        {
            value ^= static_cast<std::uint64_t>(byte);
            value *= 1099511628211ull;
        }
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << value;
        return stream.str();
    }

    void release_in_process_identifier(const std::string& identifier)
    {
        std::lock_guard<std::mutex> lock(active_identifiers_mutex());
        active_identifiers().erase(identifier);
    }

#if defined(_WIN32)
    std::wstring utf8_to_wide(std::string_view value)
    {
        if (value.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (size <= 0)
        {
            return std::wstring(value.begin(), value.end());
        }
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size);
        return result;
    }
#elif !defined(__APPLE__)
    bool run_desktop_dialog(const std::vector<std::string>& arguments)
    {
        if (arguments.empty())
        {
            return false;
        }
        const pid_t child = fork();
        if (child < 0)
        {
            return false;
        }
        if (child == 0)
        {
            std::vector<char*> argv;
            argv.reserve(arguments.size() + 1u);
            for (const std::string& argument : arguments)
            {
                argv.push_back(const_cast<char*>(argument.c_str()));
            }
            argv.push_back(nullptr);
            execvp(argv.front(), argv.data());
            _exit(127);
        }

        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = waitpid(child, &status, 0);
        }
        while (waited < 0 && errno == EINTR);
        return waited == child && WIFEXITED(status) &&
            WEXITSTATUS(status) != 127;
    }
#endif
}

struct SingleInstanceGuard::Impl
{
    SingleInstanceStatus status = SingleInstanceStatus::eDisabled;
    std::string identifier {};
    bool registeredInProcess = false;
#if defined(_WIN32)
    HANDLE mutexHandle = nullptr;
#else
    int lockFile = -1;
#endif
};

bool show_native_error_dialog(std::string_view title, std::string_view message)
{
#if defined(_WIN32)
    const std::wstring wideTitle = utf8_to_wide(title);
    const std::wstring wideMessage = utf8_to_wide(message);
    return MessageBoxW(
        nullptr,
        wideMessage.c_str(),
        wideTitle.c_str(),
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST) != 0;
#elif defined(__APPLE__)
    const CFStringRef header = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(title.data()),
        static_cast<CFIndex>(title.size()),
        kCFStringEncodingUTF8,
        false);
    const CFStringRef body = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(message.data()),
        static_cast<CFIndex>(message.size()),
        kCFStringEncodingUTF8,
        false);
    if (!header || !body)
    {
        if (header)
        {
            CFRelease(header);
        }
        if (body)
        {
            CFRelease(body);
        }
        return false;
    }
    CFOptionFlags response = 0u;
    const SInt32 result = CFUserNotificationDisplayAlert(
        0.0,
        kCFUserNotificationStopAlertLevel,
        nullptr,
        nullptr,
        nullptr,
        header,
        body,
        CFSTR("OK"),
        nullptr,
        nullptr,
        &response);
    CFRelease(header);
    CFRelease(body);
    return result == 0;
#else
    const std::string titleString(title);
    const std::string messageString(message);
    if (run_desktop_dialog({
            "zenity", "--error", "--title", titleString,
            "--text", messageString }) ||
        run_desktop_dialog({
            "kdialog", "--error", messageString,
            "--title", titleString }) ||
        run_desktop_dialog({
            "xmessage", "-center", "-title", titleString,
            messageString }))
    {
        return true;
    }
    std::fprintf(stderr, "%.*s: %.*s\n",
        static_cast<int>(title.size()), title.data(),
        static_cast<int>(message.size()), message.data());
    return false;
#endif
}

SingleInstanceGuard::SingleInstanceGuard(SingleInstanceOptions options)
    : impl(std::make_unique<Impl>())
{
    if (!options.enabled)
    {
        return;
    }

    impl->identifier = normalized_identifier(options);
    {
        std::lock_guard<std::mutex> lock(active_identifiers_mutex());
        const auto [iterator, inserted] =
            active_identifiers().insert(impl->identifier);
        static_cast<void>(iterator);
        if (!inserted)
        {
            impl->status = SingleInstanceStatus::eDuplicate;
        }
        else
        {
            impl->registeredInProcess = true;
        }
    }

    if (impl->status != SingleInstanceStatus::eDuplicate)
    {
        const std::string hash = identifier_hash(impl->identifier);
#if defined(_WIN32)
        const std::wstring mutexName =
            L"Local\\vibranceUI.single_instance." + utf8_to_wide(hash);
        impl->mutexHandle = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        if (!impl->mutexHandle)
        {
            impl->status = SingleInstanceStatus::eUnavailable;
        }
        else if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(impl->mutexHandle);
            impl->mutexHandle = nullptr;
            impl->status = SingleInstanceStatus::eDuplicate;
        }
        else
        {
            impl->status = SingleInstanceStatus::ePrimary;
        }
#else
        std::filesystem::path lockDirectory;
        if (const char* runtimeDirectory = std::getenv("XDG_RUNTIME_DIR");
            runtimeDirectory && runtimeDirectory[0] != '\0')
        {
            lockDirectory = runtimeDirectory;
        }
        if (lockDirectory.empty())
        {
            std::error_code error;
            lockDirectory = std::filesystem::temp_directory_path(error);
            if (error)
            {
                lockDirectory = "/tmp";
            }
        }
        const std::filesystem::path lockPath =
            lockDirectory / ("vibrance-single-instance-" + hash + ".lock");
        int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        impl->lockFile = open(lockPath.string().c_str(), flags, 0600);
        if (impl->lockFile < 0)
        {
            impl->status = SingleInstanceStatus::eUnavailable;
        }
        else if (flock(impl->lockFile, LOCK_EX | LOCK_NB) != 0)
        {
            close(impl->lockFile);
            impl->lockFile = -1;
            impl->status = errno == EWOULDBLOCK || errno == EAGAIN ?
                SingleInstanceStatus::eDuplicate :
                SingleInstanceStatus::eUnavailable;
        }
        else
        {
            impl->status = SingleInstanceStatus::ePrimary;
        }
#endif
    }

    if (impl->status != SingleInstanceStatus::ePrimary &&
        impl->registeredInProcess)
    {
        release_in_process_identifier(impl->identifier);
        impl->registeredInProcess = false;
    }

    if (impl->status == SingleInstanceStatus::eDuplicate &&
        options.showDuplicateError)
    {
        const std::string title = options.duplicateErrorTitle.empty() ?
            options.applicationName + " is already running" :
            options.duplicateErrorTitle;
        const std::string message = options.duplicateErrorMessage.empty() ?
            "This application can only be run once." :
            options.duplicateErrorMessage;
        show_native_error_dialog(title, message);
    }
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    release();
}

void SingleInstanceGuard::release() noexcept
{
    if (!impl)
    {
        return;
    }
#if defined(_WIN32)
    if (impl->mutexHandle)
    {
        ReleaseMutex(impl->mutexHandle);
        CloseHandle(impl->mutexHandle);
        impl->mutexHandle = nullptr;
    }
#else
    if (impl->lockFile >= 0)
    {
        flock(impl->lockFile, LOCK_UN);
        close(impl->lockFile);
        impl->lockFile = -1;
    }
#endif
    if (impl->registeredInProcess)
    {
        release_in_process_identifier(impl->identifier);
        impl->registeredInProcess = false;
    }
    impl->status = SingleInstanceStatus::eDisabled;
}

bool SingleInstanceGuard::can_run() const noexcept
{
    return impl && (
        impl->status == SingleInstanceStatus::eDisabled ||
        impl->status == SingleInstanceStatus::ePrimary);
}

bool SingleInstanceGuard::owns_instance() const noexcept
{
    return impl && impl->status == SingleInstanceStatus::ePrimary;
}

SingleInstanceStatus SingleInstanceGuard::status() const noexcept
{
    return impl ? impl->status : SingleInstanceStatus::eUnavailable;
}
