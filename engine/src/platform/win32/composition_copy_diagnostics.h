#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace vibrance::win32
{
// Optional measurements of the actual recorded D3D11 UI transfers. Source
// Vulkan readback bytes can differ because each retained D3D texture has its
// own missed damage history. No I/O or counter work occurs unless requested.
class CompositionCopyDiagnostics
{
public:
    CompositionCopyDiagnostics()
    {
        wchar_t path[32768] {};
        const DWORD length = GetEnvironmentVariableW(
            L"VIBRANCE_COMPOSITION_DIAGNOSTICS_PATH", path, 32768u);
        if (length && length < 32768u)
            file_ = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ |
                FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
        started_ = GetTickCount64();
    }
    ~CompositionCopyDiagnostics()
    {
        if (file_ != INVALID_HANDLE_VALUE)
        {
            emit(GetTickCount64());
            CloseHandle(file_);
        }
    }
    void copied(std::uint64_t bytes) noexcept
    {
        if (file_ != INVALID_HANDLE_VALUE) copyBytes_ += bytes;
    }
    void uploaded(std::uint64_t bytes) noexcept
    {
        if (file_ != INVALID_HANDLE_VALUE) uploadBytes_ += bytes;
    }
    void presented(HWND window) noexcept
    {
        if (file_ == INVALID_HANDLE_VALUE) return;
        window_ = window;
        ++frames_;
        const ULONGLONG now = GetTickCount64();
        if (now - started_ >= 2000u) emit(now);
    }

private:
    void emit(ULONGLONG now) noexcept
    {
        if (!frames_ && !copyBytes_ && !uploadBytes_) return;
        char message[320] {};
        const int size = std::snprintf(message, sizeof(message),
            "{\"window\":%llu,\"elapsed_ms\":%llu,\"frames\":%llu,\"ui_copy_bytes\":%llu,\"ui_upload_bytes\":%llu}\n",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(window_)),
            static_cast<unsigned long long>(now - started_),
            static_cast<unsigned long long>(frames_),
            static_cast<unsigned long long>(copyBytes_),
            static_cast<unsigned long long>(uploadBytes_));
        if (size > 0 && size < static_cast<int>(sizeof(message)))
        {
            DWORD written = 0u;
            WriteFile(file_, message, static_cast<DWORD>(size), &written, nullptr);
        }
        started_ = now;
        frames_ = copyBytes_ = uploadBytes_ = 0u;
    }

    HANDLE file_ = INVALID_HANDLE_VALUE;
    HWND window_ = nullptr;
    ULONGLONG started_ = 0u;
    std::uint64_t frames_ = 0u, copyBytes_ = 0u, uploadBytes_ = 0u;
};
}
#endif
