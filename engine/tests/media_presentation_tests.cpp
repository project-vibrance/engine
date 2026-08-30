#include <vibranceUI/media/media.h>

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "media presentation test failed: " << message << '\n';
        }
        return condition;
    }

    bool near(float left, float right)
    {
        return std::abs(left - right) <= 0.001f;
    }
}

int main()
{
    bool passed = true;

    const std::string accented =
        std::string("A") + "\xC3\xA9" + "B";
    const std::string accentedPrefix =
        std::string("A") + "\xC3\xA9" + "...";
    passed &= expect(
        media_ui::ellipsize_utf8(accented, 2u) == accentedPrefix,
        "ellipsizing should preserve complete UTF-8 characters");
    passed &= expect(
        media_ui::ellipsize_utf8(accented, 3u) == accented,
        "values within the limit should remain unchanged");
    passed &= expect(
        media_ui::media_time(-1) == "0:00" &&
            media_ui::media_time(61'000) == "1:01",
        "media time should clamp and format milliseconds");

    Media2DHandle wideMedia {};
    wideMedia.pixelSize = { 1920u, 1080u };
    const glm::vec2 wideSize = media_ui::supported_media_size(wideMedia);
    passed &= expect(
        near(wideSize.x, 112.0f) && near(wideSize.y, 63.0f),
        "wide artwork should use the supported 16:9 ratio");

    Media2DHandle portraitMedia {};
    portraitMedia.pixelSize = { 800u, 1200u };
    const glm::vec2 portraitSize =
        media_ui::supported_media_size(portraitMedia);
    passed &= expect(
        near(portraitSize.x, 66.0f) && near(portraitSize.y, 66.0f),
        "portrait artwork should fall back to a square presentation");
    passed &= expect(
        near(media_ui::media_ease_out_back(0.0f), 0.0f) &&
            near(media_ui::media_ease_out_back(1.0f), 1.0f),
        "media easing should preserve its endpoints");

    media_ui::MediaArtworkPlaybackPresentation playback {};
    media_ui::MediaArtworkPlaybackAnimationState playbackState {};
    media_ui::update_media_artwork_playback_animation(
        playbackState,
        playback,
        false,
        1.0);
    passed &= expect(
        near(playbackState.scale, playback.pausedScale) &&
            near(playbackState.opacity, playback.pausedOpacity),
        "initial paused artwork should immediately use the paused style");
    media_ui::update_media_artwork_playback_animation(
        playbackState,
        playback,
        true,
        2.0);
    media_ui::update_media_artwork_playback_animation(
        playbackState,
        playback,
        true,
        3.0);
    passed &= expect(
        near(playbackState.scale, 1.0f) &&
            near(playbackState.opacity, 1.0f) &&
            !playbackState.active(),
        "playback animation should settle exactly at its target");

    media_ui::RevisionedMediaSlot slot {};
    slot.reset();
    passed &= expect(
        slot.revision() == 0u && !slot.handle().valid(),
        "a reset revisioned media slot should be empty");

    return passed ? 0 : 1;
}
