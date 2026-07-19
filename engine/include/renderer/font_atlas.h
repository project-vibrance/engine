#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/renderer2d_components.h>

class StorageImage;

struct Renderer2DTextLayout
{
    // Precomputed glyph run and visible bounds for one text string
    std::vector<MSDFGlyph> glyphs;
    glm::vec2 bounds { 0.0f };
    float lineHeight = 0.0f;
};

struct Renderer2DCodepointRange
{
    uint32_t first = 0u;
    uint32_t last = 0u;
};

struct Renderer2DFontAtlasLoadOptions
{
    // Optional preload text expands the atlas for localisation before first draw
    bool includeBasicLatin = true;
    bool requireRequestedGlyphs = false;
    int fontWeight = 0;
    std::vector<std::filesystem::path> fallbackFontPaths;
    std::vector<Renderer2DCodepointRange> ranges;
    std::string preloadText;
};

struct Renderer2DFontProfile
{
    // App-provided font map used to override the built-in locale resolver
    std::filesystem::path defaultFontPath {};
    std::vector<std::filesystem::path> defaultFontCandidates {};
    std::vector<std::filesystem::path> fallbackFontPaths {};
    std::unordered_map<std::string, std::filesystem::path> localeFontPaths {};
    std::unordered_map<std::string, std::vector<std::filesystem::path>> localeFontCandidates {};
};

VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::filesystem::path& assetFontDirectory = {});

// Directory layers are ordered from packaged fallback to highest-priority
// user override. Relative font names resolve against the highest layer first.
VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_font_candidates(
    const std::vector<std::filesystem::path>& assetFontDirectories,
    const std::filesystem::path& relativeFontPath);

VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::vector<std::filesystem::path>& assetFontDirectories);

VIBRANCE_ENGINE_API bool renderer2d_load_font_profile(
    const std::filesystem::path& profilePath,
    const std::filesystem::path& assetFontDirectory,
    Renderer2DFontProfile& outProfile);

VIBRANCE_ENGINE_API bool renderer2d_load_font_profile(
    const std::filesystem::path& profilePath,
    const std::vector<std::filesystem::path>& assetFontDirectories,
    Renderer2DFontProfile& outProfile);

VIBRANCE_ENGINE_API bool renderer2d_load_font_profile_layers(
    const std::vector<std::filesystem::path>& profilePaths,
    const std::vector<std::filesystem::path>& assetFontDirectories,
    Renderer2DFontProfile& outProfile);

VIBRANCE_ENGINE_API std::filesystem::path renderer2d_primary_font_for_locale(
    const std::filesystem::path& assetFontDirectory,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath = {});

VIBRANCE_ENGINE_API std::filesystem::path renderer2d_primary_font_for_locale(
    const std::vector<std::filesystem::path>& assetFontDirectories,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath = {});

VIBRANCE_ENGINE_API std::filesystem::path renderer2d_primary_font_for_locale(
    const Renderer2DFontProfile& profile,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath = {});

class VIBRANCE_ENGINE_API Renderer2DFontAtlas
{
public:
    // MSDF atlas generator used by text entities and input caret measurements
    bool load_from_file(
        const std::filesystem::path& path,
        const Renderer2DFontAtlasLoadOptions& options,
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
    );

    bool load_from_file(
        const std::filesystem::path& path,
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
    );

    // Returns glyph positions in local text space for drawing and caret placement
    Renderer2DTextLayout layout_text(std::string_view text, float fontSize) const;

    StorageImage* image() const;
    bool loaded() const;
    uint32_t atlas_id() const;
    glm::uvec2 texture_size() const;
    float pixel_range() const;
    const std::string& name() const;

private:
    struct GlyphRecord
    {
        glm::vec2 offset { 0.0f };
        glm::vec2 size { 0.0f };
        glm::vec2 uvMin { 0.0f };
        glm::vec2 uvMax { 0.0f };
        float advance = 0.0f;
        uint32_t sourceFace = 0u;
        bool valid = false;
        bool drawable = false;
    };

    const GlyphRecord* glyph_for(uint32_t codepoint) const;
    float kerning(uint32_t left, uint32_t right) const;

    std::unordered_map<uint32_t, GlyphRecord> glyphs;
    std::unordered_map<uint64_t, float> kerningTable;
    StorageImage* atlasImage = nullptr;
    glm::uvec2 atlasSize { 1u, 1u };
    std::string fontName = "Renderer2D Font";
    uint32_t atlasId = 1;
    float bakedPixelHeight = 64.0f;
    float lineHeight = 64.0f;
    float baseline = 48.0f;
    float sdfPixelRange = 8.0f;
    bool isLoaded = false;
};
