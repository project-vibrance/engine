#pragma once
#include <vibranceUI/export.h>
#include <filesystem>

// Returns an absolute executable path, or an empty path on failure/unsupported hosts.
VIBRANCE_ENGINE_API std::filesystem::path current_executable_path();
// Start a replacement after shutting down application windows/resources.
// Windows preserves the native command line; Unix uses the supplied arguments.
// Returns whether process creation succeeded, not whether its start-up completed.
VIBRANCE_ENGINE_API bool relaunch_current_process(int argc, char* argv[]);
