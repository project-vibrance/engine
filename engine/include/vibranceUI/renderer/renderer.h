#pragma once
#include "vibranceUI/export.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/renderer/present_mode.h>
#include <vibranceUI/graphics/backend.h>
#include <vibranceUI/graphics/backdrop.h>
#include <vibranceUI/audio/audio.h>
#include <vibranceUI/localisation/localisation.h>

struct EngineCreateInfo
{
    const char* applicationName = "vibranceUI";
    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;
    // Optional offscreen render target cap; leave at 0 to render at native framebuffer resolution
    uint32_t maxRenderPixels = 0;
    // Requested MSAA samples for hosted 3D rendering; falls back to the nearest supported count
    uint32_t msaaSamples = 4;
    RendererPresentMode presentMode = RendererPresentMode::eAuto;
    RenderBackend renderBackend = RenderBackend::eVulkan;
    PresentationBackend presentationBackend = PresentationBackend::eNative;
    // 0 means uncapped; otherwise draw waits to stay near the requested frame rate
    uint32_t targetFrameRate = 0;
    uint32_t instanceExtensionCount = 0;
    const char* const* instanceExtensions = nullptr;
    void* surfaceUserData = nullptr;
    // Native platform handle used only by an explicitly selected presenter.
    void* nativeWindowHandle = nullptr;
    int (*createSurface)(void* instance, void* userData, void* surfaceOut) = nullptr;
    bool transparentFramebuffer = false;
    bool enableAudio = true;
    std::filesystem::path defaultRenderer2DFontPath {};
    Renderer2DFontAtlasLoadOptions defaultRenderer2DFontOptions {};
};

class VIBRANCE_ENGINE_API Engine
{
    public:
    Engine(const EngineCreateInfo& createInfo);

    ~Engine();

    // Draws the current renderer scene once
    void draw();

    // Updates frame timing and returns a measured FPS sample once per second
    int update_timing(double currentTimeSeconds);

    bool set_present_mode(RendererPresentMode mode);

    RendererPresentMode present_mode_preference() const;

    RendererPresentMode active_present_mode() const;

    std::vector<RendererPresentMode> available_present_modes() const;

    void set_target_frame_rate(uint32_t frameRate);

    uint32_t target_frame_rate() const;

    // Suggested cadence for CPU-side input, layout, and animation updates.
    // draw() retains the requested/uncapped render-loop semantics; callers can
    // use this value to avoid updating scene state more often than it can be
    // displayed.
    uint32_t recommended_ui_update_rate() const;

    // Resizes swapchain-dependent renderer resources
    void resize(uint32_t framebufferWidth, uint32_t framebufferHeight);

    // Loads renderer assets through the engine-owned caches
    Model3DHandle load_model_3d(const std::filesystem::path& path);

    Media2DHandle load_media_2d(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options = {}
    );

    AudioClipHandle load_audio_clip(const std::filesystem::path& path);

    // Audio is optional in EngineCreateInfo for lightweight UI-only windows
    AudioEngine& audio();

    const AudioEngine& audio() const;

    // Replaces the shared 2D font atlas and refreshes existing frame bindings
    // and text layouts. New text can immediately use renderer2d_font_atlas().
    bool load_renderer2d_font(const std::filesystem::path& path);

    bool load_renderer2d_font(
        const std::filesystem::path& path,
        const Renderer2DFontAtlasLoadOptions& options
    );

    bool load_localisation_directory(const std::filesystem::path& directory);

    // Replaces current dictionaries with packaged-to-user layers. Valid user
    // keys override packaged keys; invalid/incomplete files retain defaults.
    bool load_localisation_directories(
        const std::vector<std::filesystem::path>& directories);

    // Updates this engine instance and refreshes localised text entities
    bool set_locale(const std::string& locale);

    // Shared locale keeps multiple windows in step
    static bool set_shared_locale(const std::string& locale);

    static std::string shared_locale();

    std::string locale() const;

    std::string resolve_text(const Text& text) const;

    RenderBackend render_backend() const;

    PresentationBackend presentation_backend() const;

    bool system_backdrop_available() const;

    // True only after the Vulkan device, swapchain, pipelines, and frame
    // resources are ready. Window hosts use this to fail visibly instead of
    // leaving a transparent but non-rendering native window on screen.
    bool ready() const;

    // Vulkan API level exposed by the selected native physical device, such
    // as "1.4.303". Empty means no physical device was selected.
    std::string vulkan_api_version() const;

    // Re-resolves every LocalisedTextComponent in the active 2D scene
    void refresh_localised_texts();

    Localisation& localisation();

    const Localisation& localisation() const;

    bool set_external_backdrop_rgba(
        uint32_t width,
        uint32_t height,
        const unsigned char* rgba,
        std::size_t byteCount
    );

    void clear_external_backdrop();

    uint32_t render_width() const;

    uint32_t render_height() const;

    // Direct access to the 2D scene is used by reusable UI builders
    Renderer2DScene& renderer2d_scene();

    const Renderer2DScene& renderer2d_scene() const;

    Renderer2DFontAtlas& renderer2d_font_atlas();

    const Renderer2DFontAtlas& renderer2d_font_atlas() const;

    private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
