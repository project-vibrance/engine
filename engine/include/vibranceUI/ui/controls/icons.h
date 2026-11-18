// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiCommonIconSet
{

    Media2DHandle search {};
    Media2DHandle check {};
    Media2DHandle chevronLeft {};
    Media2DHandle chevronRight {};
    Media2DHandle chevronDown {};
    Media2DHandle chevronUp {};
};

inline Media2DLoadOptions ui_svg_icon_load_options(uint32_t rasterSize = 128u)
{

    Media2DLoadOptions options = {};
    options.rasterWidth = rasterSize;
    options.rasterHeight = rasterSize;
    options.premultiplyAlpha = true;
    return options;
}

template <typename EngineLike>
inline UiCommonIconSet ui_load_common_icon_set(
    EngineLike& engine,
    const std::filesystem::path& assetRoot = {},
    const Media2DLoadOptions& loadOptions = ui_svg_icon_load_options())
{

    if (assetRoot.empty())
    {
        return {};
    }
    const std::filesystem::path svgRoot = assetRoot / "svg" / "static";
    UiCommonIconSet icons = {};
    icons.search = engine.load_media_2d(svgRoot / "icons" / "search.svg", loadOptions);
    icons.check = engine.load_media_2d(svgRoot / "icons" / "check.svg", loadOptions);
    icons.chevronLeft = engine.load_media_2d(svgRoot / "arrows" / "chevron_left.svg", loadOptions);
    icons.chevronRight = engine.load_media_2d(svgRoot / "arrows" / "chevron_right.svg", loadOptions);
    icons.chevronDown = engine.load_media_2d(svgRoot / "arrows" / "chevron_down.svg", loadOptions);
    icons.chevronUp = engine.load_media_2d(svgRoot / "arrows" / "chevron_up.svg", loadOptions);
    return icons;
}

template <typename EngineLike>
inline UiCommonIconSet ui_load_common_icon_set(
    EngineLike& engine,
    const UiResourceDirectories& resources,
    const Media2DLoadOptions& loadOptions = ui_svg_icon_load_options())
{
    const std::filesystem::path svgRoot = std::filesystem::path("svg") / "static";
    const auto loadLayeredIcon = [&engine, &resources, &loadOptions](
        const std::filesystem::path& relativePath) {
        Media2DHandle handle = {};
        for (const std::filesystem::path& path : resources.asset_candidates(relativePath))
        {
            handle = engine.load_media_2d(path, loadOptions);
            if (handle.drawable)
            {
                return handle;
            }
        }
        return Media2DHandle {};
    };
    UiCommonIconSet icons = {};
    icons.search = loadLayeredIcon(svgRoot / "icons" / "search.svg");
    icons.check = loadLayeredIcon(svgRoot / "icons" / "check.svg");
    icons.chevronLeft = loadLayeredIcon(svgRoot / "arrows" / "chevron_left.svg");
    icons.chevronRight = loadLayeredIcon(svgRoot / "arrows" / "chevron_right.svg");
    icons.chevronDown = loadLayeredIcon(svgRoot / "arrows" / "chevron_down.svg");
    icons.chevronUp = loadLayeredIcon(svgRoot / "arrows" / "chevron_up.svg");
    return icons;
}

