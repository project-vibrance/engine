// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "vibranceUI/export.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <vibranceUI/renderer/renderer2d_components.h>
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

    uint32_t maxRenderPixels = 0;

    uint32_t msaaSamples = 4;
    RendererPresentMode presentMode = RendererPresentMode::eAuto;
    RenderBackend renderBackend = default_render_backend();
    PresentationBackend presentationBackend = PresentationBackend::eNative;

    uint32_t targetFrameRate = 0;
    uint32_t instanceExtensionCount = 0;
    const char* const* instanceExtensions = nullptr;
    void* surfaceUserData = nullptr;

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


    void draw();






    bool poll_pending_presentation();


    static void begin_externally_paced_frame(uint32_t frameRate);
    static void end_externally_paced_frame();


    int update_timing(double currentTimeSeconds);

    bool set_present_mode(RendererPresentMode mode);

    RendererPresentMode present_mode_preference() const;

    RendererPresentMode active_present_mode() const;

    std::vector<RendererPresentMode> available_present_modes() const;

    void set_target_frame_rate(uint32_t frameRate);

    uint32_t target_frame_rate() const;





    uint32_t recommended_ui_update_rate() const;


    void resize(uint32_t framebufferWidth, uint32_t framebufferHeight);


    Model3DHandle load_model_3d(const std::filesystem::path& path);

    Media2DHandle load_media_2d(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options = {}
    );



    Media2DUploadResult upload_media_2d(
        const PreparedMedia2D& prepared, uint32_t maxFrameUploads = 1);
    void cancel_media_2d_upload(const PreparedMedia2D& prepared);

    AudioClipHandle load_audio_clip(const std::filesystem::path& path);


    AudioEngine& audio();

    const AudioEngine& audio() const;



    bool load_renderer2d_font(const std::filesystem::path& path);

    bool load_renderer2d_font(
        const std::filesystem::path& path,
        const Renderer2DFontAtlasLoadOptions& options
    );

    bool load_localisation_directory(const std::filesystem::path& directory);



    bool load_localisation_directories(
        const std::vector<std::filesystem::path>& directories);


    bool set_locale(const std::string& locale);


    static bool set_shared_locale(const std::string& locale);

    static std::string shared_locale();




    static double idle_event_wait_seconds();

    std::string locale() const;

    std::string resolve_text(const Text& text) const;

    RenderBackend render_backend() const;

    PresentationBackend presentation_backend() const;

    bool system_backdrop_available() const;




    bool ready() const;



    std::string vulkan_api_version() const;


    std::string graphics_api_version() const;


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


    Renderer2DScene& renderer2d_scene();

    const Renderer2DScene& renderer2d_scene() const;

    Renderer2DFontAtlas& renderer2d_font_atlas();

    const Renderer2DFontAtlas& renderer2d_font_atlas() const;

    private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
