// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct NativeTrayMenuItem
{
    std::uint32_t command = 0;
    std::string label {};
    bool enabled = true;
    bool separator = false;
    std::string shortcut {};
};

enum class NativeTrayEventKind { eNone, eToggleCustomMenu, eOpenNativeMenu, eCommand };

struct NativeTrayEvent
{
    NativeTrayEventKind kind = NativeTrayEventKind::eNone;
    std::uint32_t command = 0;
};

struct NativeTrayAnchor
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

struct NativeTrayOptions
{
    std::filesystem::path iconPath {};
    std::string tooltip {};
    std::string fallbackText = "?";
    std::vector<NativeTrayMenuItem> menu {};
    bool preferCustomMenu = false;

    bool registerWithShell = true;
};




class VIBRANCE_ENGINE_API NativeTray
{
public:
    explicit NativeTray(NativeTrayOptions options = {});
    ~NativeTray();
    NativeTray(const NativeTray&) = delete;
    NativeTray& operator=(const NativeTray&) = delete;
    NativeTray(NativeTray&&) = delete;
    NativeTray& operator=(NativeTray&&) = delete;

    bool initialise();
    void shutdown();
    bool available() const;
    NativeTrayEvent take_event();
    NativeTrayAnchor menu_anchor() const;
    void set_custom_menu_enabled(bool enabled);
    bool custom_menu_enabled() const;
    void show_native_menu();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
