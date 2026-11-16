// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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


    bool enabled = false;
    std::string identifier {};
    std::string applicationName = "Application";
    bool showDuplicateError = true;
    std::string duplicateErrorTitle {};
    std::string duplicateErrorMessage {};
};



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


    void release() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};



VIBRANCE_ENGINE_API bool show_native_error_dialog(
    std::string_view title,
    std::string_view message);
