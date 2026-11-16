// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#if defined(__APPLE__)
#include <vibranceUI/renderer/metal/resources.h>
#else
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#endif
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/renderer2d_components.h>

class StorageImage;
class Renderer2DEmojiShaper;

struct Renderer2DTextLayout
{

    std::vector<MSDFGlyph> glyphs;
    glm::vec2 bounds { 0.0f };
    float lineHeight = 0.0f;
    uint32_t lineCount = 0u;
    bool rightToLeft = false;


    float baseline = 0.0f;

    float advanceWidth = 0.0f;
    glm::vec2 inkOffset { 0.0f };
};

struct Renderer2DCodepointRange
{
    uint32_t first = 0u;
    uint32_t last = 0u;
};

struct Renderer2DFontAtlasLoadOptions
{

    bool includeBasicLatin = true;
    bool requireRequestedGlyphs = false;
    int fontWeight = 0;
    std::vector<std::filesystem::path> fallbackFontPaths;
    std::vector<std::filesystem::path> emojiFontPaths;
    std::vector<Renderer2DCodepointRange> ranges;
    std::string preloadText;
};

struct Renderer2DFontProfile
{

    std::filesystem::path defaultFontPath {};
    std::vector<std::filesystem::path> defaultFontCandidates {};
    std::vector<std::filesystem::path> fallbackFontPaths {};
    std::vector<std::filesystem::path> emojiFontCandidates {};
    std::unordered_map<std::string, std::filesystem::path> localeFontPaths {};
    std::unordered_map<std::string, std::vector<std::filesystem::path>> localeFontCandidates {};
};

VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::filesystem::path& assetFontDirectory = {});



VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_font_candidates(
    const std::vector<std::filesystem::path>& assetFontDirectories,
    const std::filesystem::path& relativeFontPath);

VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::vector<std::filesystem::path>& assetFontDirectories);

VIBRANCE_ENGINE_API std::vector<std::filesystem::path> renderer2d_emoji_font_candidates(
    const Renderer2DFontProfile& profile,
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

    bool load_from_file(
        const std::filesystem::path& path,
        const Renderer2DFontAtlasLoadOptions& options,
#if defined(__APPLE__)
        const MetalTextureUpload& upload
#else
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
#endif
    );

    bool load_from_file(
        const std::filesystem::path& path,
#if defined(__APPLE__)
        const MetalTextureUpload& upload
#else
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
#endif
    );


    Renderer2DTextLayout layout_text(std::string_view text, float fontSize) const;
    Renderer2DTextLayout layout_text(
        std::string_view text,
        float fontSize,
        const TextLayout2DOptions& options) const;






    bool covers_text(std::string_view text) const;
    std::vector<uint32_t> missing_codepoints(std::string_view text) const;

    StorageImage* image() const;
    bool loaded() const;
    uint32_t atlas_id() const;
    glm::uvec2 texture_size() const;
    float pixel_range() const;
    int font_weight() const;
    void set_pixel_scale(float scale);
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
        float bakedFontWeight = 400.0f;
        bool valid = false;
        bool drawable = false;
        bool coloured = false;
        float sdfPadding = 0.0f;
        glm::vec2 sdfAtlasSize { 0.0f };
    };

    const GlyphRecord* glyph_for(uint32_t codepoint) const;
    float kerning(uint32_t left, uint32_t right) const;
    float tracking(uint32_t left, uint32_t right, float fontSize, bool tabularDigits, float pointSize = 0.0f) const;
    void update_tabular_digit_metrics();

    std::unordered_map<uint32_t, GlyphRecord> glyphs;
    std::unordered_map<uint64_t, float> kerningTable;
    std::vector<std::vector<glm::vec2>> faceTracking;
    std::vector<float> faceTrackingUnitsToBakedPixels;
    std::shared_ptr<Renderer2DEmojiShaper> emojiShaper;
    std::vector<uint32_t> visual_glyph_codepoints(std::string_view text) const;
    StorageImage* atlasImage = nullptr;
#if defined(__APPLE__)
    std::unique_ptr<StorageImage> metalAtlasImage;
#endif
    glm::uvec2 atlasSize { 1u, 1u };
    std::string fontName = "Renderer2D Font";
    uint32_t atlasId = 1;
    float bakedPixelHeight = 96.0f;
    int bakedFontWeight = 400;
    float opticalPixelScale = 1.0f;
    float lineHeight = 96.0f;
    float baseline = 72.0f;
    float sdfPixelRange = 8.0f;
    float tabularDigitAdvance = 0.0f;
    float tabularDigitLeft = 0.0f;
    float tabularDigitRight = 0.0f;
    bool isLoaded = false;
};
