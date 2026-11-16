// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/native_dialog.h>
#include <array>
#include <cerrno>
#include <thread>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

std::optional<std::filesystem::path> show_native_file_dialog(const NativeFileDialogOptions& options, std::string& error)
{
    std::string pattern;
    for (const auto& filter : options.filters)
        for (const auto& extension : filter.extensions) pattern += (pattern.empty() ? "" : " ") + std::string("*.") + extension;
    if (options.allowAllFiles || pattern.empty()) pattern += " *";
    const auto title = "--title=" + options.title;
    const auto filter = "--file-filter=Files | " + pattern;
    const auto initial = options.initialDirectory.empty() ? std::string(".") : options.initialDirectory.string();
    const std::array<std::vector<const char*>, 2> commands {{
        { "zenity", "--file-selection", title.c_str(), filter.c_str(), nullptr },
        { "kdialog", "--getopenfilename", initial.c_str(), pattern.c_str(), "--title", options.title.c_str(), nullptr }
    }};
    for (const auto& command : commands)
    {
        int output[2] {};
        if (pipe(output) != 0) break;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, output[0]);
        posix_spawn_file_actions_addclose(&actions, output[1]);
        std::vector<char*> arguments;
        for (const auto* value : command) arguments.push_back(const_cast<char*>(value));
        pid_t process = 0;
        const auto launched = posix_spawnp(&process, arguments.front(), &actions, nullptr, arguments.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(output[1]);
        if (launched != 0) { close(output[0]); continue; }
        std::string selected;
        std::array<char, 4096> block {};
        for (;;)
        {
            const auto received = read(output[0], block.data(), block.size());
            if (received < 0 && errno == EINTR) continue;
            if (received <= 0) break;
            if (selected.size() + static_cast<std::size_t>(received) <= 65536u)
                selected.append(block.data(), static_cast<std::size_t>(received));
        }
        close(output[0]);
        int result = 0;
        while (waitpid(process, &result, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(result) || WEXITSTATUS(result) != 0 || selected.empty())
        { error.clear(); return {}; }
        while (!selected.empty() && (selected.back() == '\n' || selected.back() == '\r')) selected.pop_back();
        error.clear();
        return std::filesystem::path(selected);
    }
    error = "A supported file chooser is unavailable. Install Zenity or KDialog.";
    return {};
}

bool native_open_directory(const std::filesystem::path& directory, std::string& error, bool createIfMissing)
{
    std::error_code fileError;
    if (createIfMissing && !directory.empty()) std::filesystem::create_directories(directory, fileError);
    if (directory.empty() || fileError || !std::filesystem::is_directory(directory, fileError))
    { error = "The requested directory is unavailable."; return false; }
    auto path = std::filesystem::absolute(directory).string();
    char program[] = "xdg-open";
    char* arguments[] { program, path.data(), nullptr };
    pid_t process = 0;
    if (posix_spawnp(&process, program, nullptr, nullptr, arguments, environ) != 0)
    { error = "The directory could not be opened."; return false; }
    std::thread([process] {
        int result = 0;
        while (waitpid(process, &result, 0) < 0 && errno == EINTR) {}
    }).detach();
    error.clear();
    return true;
}
