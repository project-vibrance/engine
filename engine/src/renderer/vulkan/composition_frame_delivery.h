#pragma once

#include <optional>

namespace vibrance::win32
{
// A completed frame retains its uploaded pixels across a deferred Present.
// Native regions are shared between pending frames, so restore this frame's
// regions on every attempt, including retries after another frame was visited.
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
