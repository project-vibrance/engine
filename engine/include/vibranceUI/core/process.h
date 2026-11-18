// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <vibranceUI/export.h>
#include <filesystem>


VIBRANCE_ENGINE_API std::filesystem::path current_executable_path();



VIBRANCE_ENGINE_API bool relaunch_current_process(int argc, char* argv[]);
