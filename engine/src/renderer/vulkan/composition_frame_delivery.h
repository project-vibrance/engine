// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

namespace vibrance::win32
{



template <typename Uploaded, typename Upload, typename PublishRegions, typename Present>
auto present_completed_composition_frame(Uploaded&& uploaded, Upload&& upload,
    PublishRegions&& publishRegions, Present&& present)
    -> std::optional<decltype(present())>
{
    if (!uploaded)
    {
        if (!upload()) return std::nullopt;
        uploaded = true;
    }
    publishRegions();
    return present();
}
}
