#pragma once

#include "vibranceUI/export.h"

#include <memory>
#include <string>
#include <string_view>

enum class SingleInstanceStatus
{
    eDisabled,
    ePrimary,
    eDuplicate,
    eUnavailable
};

struct SingleInstanceOptions
{
    // Disabled by default so libraries and multi-window applications opt in at
    // the process entry point rather than once per Engine/window instance.
    bool enabled = false;
    std::string identifier {};
    std::string applicationName = "Application";
    bool showDuplicateError = true;
    std::string duplicateErrorTitle {};
    std::string duplicateErrorMessage {};
};

// Holds the process-wide operating-system lock for its complete lifetime.
// Construct this before creating windows and keep it alive until shutdown.
class VIBRANCE_ENGINE_API SingleInstanceGuard
{
public:
    explicit SingleInstanceGuard(SingleInstanceOptions options = {});
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard(SingleInstanceGuard&&) = delete;
    SingleInstanceGuard& operator=(SingleInstanceGuard&&) = delete;

    bool can_run() const noexcept;
    bool owns_instance() const noexcept;
    SingleInstanceStatus status() const noexcept;
    // Relinquishes the operating-system lock before this guard is destroyed.
    // Use immediately before launching a replacement process during restart.
    void release() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Shows a modal platform error: MessageBox on Windows, a native alert on
// macOS, and the available desktop dialog provider on Linux/BSD.
VIBRANCE_ENGINE_API bool show_native_error_dialog(
    std::string_view title,
    std::string_view message);
