#include <vibranceUI/platform/native_dialog.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <unistd.h>
#endif

namespace
{
std::filesystem::path closest_existing_directory(
    std::filesystem::path path)
{
    std::error_code error;
    while (!path.empty() && !std::filesystem::is_directory(path, error))
    {
        error.clear();
        const std::filesystem::path parent = path.parent_path();
        if (parent == path)
        {
            break;
        }
        path = parent;
    }
    return path;
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    return result;
}
#else
std::string shell_quote(const std::string& value)
{
    std::string result = "'";
    for (const char character : value)
    {
        if (character == '\'')
        {
            result += "'\\''";
        }
        else
        {
            result += character;
        }
    }
    result += "'";
    return result;
}

std::optional<std::filesystem::path> run_folder_picker_command(
    const std::string& command)
{
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
    {
        return std::nullopt;
    }

    std::string output;
    std::array<char, 512> buffer {};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        output += buffer.data();
    }
    const int result = pclose(pipe);
    if (result != 0)
    {
        return std::nullopt;
    }

    while (!output.empty() &&
        (output.back() == '\r' || output.back() == '\n'))
    {
        output.pop_back();
    }
    return output.empty() ?
        std::nullopt :
        std::optional<std::filesystem::path>(output);
}
#endif
}

std::optional<std::filesystem::path> show_native_folder_dialog(
    const NativeFolderDialogOptions& options)
{
#if defined(_WIN32)
    const HRESULT initialiseResult = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialise = SUCCEEDED(initialiseResult);
    if (FAILED(initialiseResult) && initialiseResult != RPC_E_CHANGED_MODE)
    {
        return std::nullopt;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IFileOpenDialog,
        reinterpret_cast<void**>(&dialog));
    if (FAILED(result) || !dialog)
    {
        if (uninitialise)
        {
            CoUninitialize();
        }
        return std::nullopt;
    }

    DWORD dialogOptions = 0;
    if (SUCCEEDED(dialog->GetOptions(&dialogOptions)))
    {
        dialog->SetOptions(
            dialogOptions |
            FOS_PICKFOLDERS |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST);
    }

    const std::wstring title = utf8_to_wide(options.title);
    if (!title.empty())
    {
        dialog->SetTitle(title.c_str());
    }

    const std::filesystem::path initialDirectory =
        closest_existing_directory(options.initialDirectory);
    if (!initialDirectory.empty())
    {
        IShellItem* initialItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                initialDirectory.c_str(),
                nullptr,
                IID_IShellItem,
                reinterpret_cast<void**>(&initialItem))) &&
            initialItem)
        {
            dialog->SetFolder(initialItem);
            initialItem->Release();
        }
    }

    result = dialog->Show(static_cast<HWND>(options.parentWindow));
    std::optional<std::filesystem::path> selection;
    if (SUCCEEDED(result))
    {
        IShellItem* selectedItem = nullptr;
        if (SUCCEEDED(dialog->GetResult(&selectedItem)) && selectedItem)
        {
            PWSTR selectedPath = nullptr;
            if (SUCCEEDED(selectedItem->GetDisplayName(
                    SIGDN_FILESYSPATH,
                    &selectedPath)) &&
                selectedPath)
            {
                selection = std::filesystem::path(selectedPath);
                CoTaskMemFree(selectedPath);
            }
            selectedItem->Release();
        }
    }

    dialog->Release();
    if (uninitialise)
    {
        CoUninitialize();
    }
    return selection;
#elif defined(__APPLE__)
    const std::filesystem::path initialDirectory =
        closest_existing_directory(options.initialDirectory);
    std::string script =
        "POSIX path of (choose folder with prompt " +
        std::string("\"") + options.title + "\"";
    if (!initialDirectory.empty())
    {
        script += " default location POSIX file \"" +
            initialDirectory.string() + "\"";
    }
    script += ")";
    return run_folder_picker_command(
        "osascript -e " + shell_quote(script));
#else
    const std::filesystem::path initialDirectory =
        closest_existing_directory(options.initialDirectory);
    std::string command =
        "zenity --file-selection --directory --title=" +
        shell_quote(options.title);
    if (!initialDirectory.empty())
    {
        command += " --filename=" + shell_quote(
            initialDirectory.string() + "/");
    }
    return run_folder_picker_command(command);
#endif
}

std::filesystem::path native_program_files_directory()
{
#if defined(_WIN32)
    PWSTR directory = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_ProgramFiles,
            KF_FLAG_DEFAULT,
            nullptr,
            &directory)) &&
        directory)
    {
        const std::filesystem::path result(directory);
        CoTaskMemFree(directory);
        return result;
    }

    if (const wchar_t* programFiles = _wgetenv(L"ProgramFiles"))
    {
        return std::filesystem::path(programFiles);
    }
    return std::filesystem::path(L"C:\\Program Files");
#elif defined(__APPLE__)
    return std::filesystem::path("/Applications");
#else
    return std::filesystem::path("/opt");
#endif
}

bool native_process_is_elevated()
{
#if defined(_WIN32)
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation {};
    DWORD returnedSize = 0;
    const bool elevated = GetTokenInformation(
        token,
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &returnedSize) && elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
#else
    return geteuid() == 0;
#endif
}

bool native_path_requires_elevation(const std::filesystem::path& path)
{
    if (path.empty() || native_process_is_elevated())
    {
        return false;
    }

    const std::filesystem::path existingDirectory =
        closest_existing_directory(path);
    if (existingDirectory.empty())
    {
        return false;
    }

#if defined(_WIN32)
    HANDLE directory = CreateFileW(
        existingDirectory.c_str(),
        FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (directory != INVALID_HANDLE_VALUE)
    {
        CloseHandle(directory);
        return false;
    }
    return GetLastError() == ERROR_ACCESS_DENIED;
#else
    return access(existingDirectory.c_str(), W_OK) != 0;
#endif
}
