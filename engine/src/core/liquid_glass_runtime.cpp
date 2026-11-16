// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/core/liquid_glass_runtime.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../platform/win32/liquid_glass_runtime_identity.h"
#include "../platform/win32/liquid_glass_dwm_session.h"
#include <sddl.h>
#include <shlobj.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
std::filesystem::path runtime_status_directory()
{
    PWSTR root = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &root))) return {};
    const std::filesystem::path directory(root);
    CoTaskMemFree(root);
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> bytes(size);
    LPWSTR sid = nullptr;
    if (GetTokenInformation(token, TokenUser, bytes.data(), size, &size))
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid);
    CloseHandle(token);
    if (!sid) return {};
    const auto result = directory / L"VibranceLiquidGlass" / sid;
    LocalFree(sid);
    return result;
}

class RuntimeStatusMonitor
{
public:
    RuntimeStatusMonitor() : worker_([this] { run(); }) {}
    ~RuntimeStatusMonitor()
    {
        { std::lock_guard lock(mutex_); stopped_ = true; }
        wake_.notify_one();
        worker_.join();
    }
    bool restart_required() const noexcept { return restartRequired_.load(std::memory_order_relaxed); }
private:
    void run() noexcept
    {
        try
        {
            using namespace vibrance::win32;
            const auto executable = std::filesystem::path(module_path(nullptr));
            auto bundleStamp = liquid_runtime_bundle_stamp(executable.parent_path());
            auto desired = liquid_runtime_key(executable.parent_path() / liquidRuntimeNames[0]);
            const auto directory = runtime_status_directory();
            if (directory.empty()) return;
            std::filesystem::file_time_type checkedWriteTime {};
            LiquidRuntimeStatus checkedReport;
            std::wstring verifiedStagedKey;
            for (;;)
            {
                std::error_code error;
                const auto nextStamp = liquid_runtime_bundle_stamp(executable.parent_path());
                const bool bundleChanged = nextStamp != bundleStamp || desired.empty();
                if (bundleChanged)
                {
                    desired = nextStamp.complete ? liquid_runtime_key(
                        executable.parent_path() / liquidRuntimeNames[0]) : std::wstring {};
                    bundleStamp = nextStamp;
                }
                const auto writeTime = std::filesystem::last_write_time(directory / L"runtime-status.txt", error);
                if (!error && (bundleChanged || writeTime != checkedWriteTime))
                {
                    const auto report = read_liquid_runtime_status(directory);
                    const auto stagedKey = report.requestedKey == desired ?
                        liquid_runtime_key(report.stagedExtension) : std::wstring {};
                    const auto afterRead = std::filesystem::last_write_time(directory / L"runtime-status.txt", error);
                    if (!error && afterRead == writeTime)
                    {
                        checkedReport = report;
                        verifiedStagedKey = stagedKey;
                        checkedWriteTime = writeTime;
                    }
                    else error = std::make_error_code(std::errc::resource_unavailable_try_again);
                }
                restartRequired_.store(!error && verifiedStagedKey == desired &&
                    liquid_runtime_restart_required(checkedReport, liquid_session_compositor(), desired), std::memory_order_relaxed);
                std::unique_lock lock(mutex_);
                if (wake_.wait_for(lock, std::chrono::seconds(2), [this] { return stopped_; })) break;
            }
        }
        catch (...) { restartRequired_.store(false, std::memory_order_relaxed); }
    }
    std::atomic<bool> restartRequired_ { false };
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopped_ = false;
    std::thread worker_;
};
}
#endif

bool vibrance::liquid_glass_runtime_restart_required() noexcept
{
#if defined(_WIN32)
    try { static RuntimeStatusMonitor monitor; return monitor.restart_required(); }
    catch (...) { return false; }
#else
    return false;
#endif
}
