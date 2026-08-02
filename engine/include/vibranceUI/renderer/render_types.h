#pragma once
#include <glm/glm.hpp>

enum class PipelineType
{
    // Stable keys for pipeline maps built during renderer initialisation
    eClear,
    eRasteriseSmall,
    eShape2D,
    eShadow2D,
    eBlur2D,
    eTextMSDF,
    eMedia2D,
    eCompositeHosted3D,
    eComposite2D,
    eModel3D
};

enum class DescriptorScope
{
    // Descriptor scopes separate frame globals, cache targets, media, models and post passes
    eFrame,
    eModelFrame,
    eDrawCall,
    eModel3DTexture,
    eMediaTexture,
    ePost,
    eUICache,
    eUICachePost
};

struct RasterPushConstants
{
    alignas(16) glm::mat4 worldToClip { 1.0f };
    alignas(4) uint32_t firstTriangle = 0;
    alignas(4) uint32_t triangleCount = 0;
    alignas(4) uint32_t pass = 0;
    alignas(4) uint32_t flags = 0;
    alignas(16) glm::vec4 viewportRect { 0.0f };
    alignas(16) glm::vec4 clipRect { 0.0f };
    alignas(16) glm::vec4 materialColor { 1.0f };
};

static_assert(sizeof(RasterPushConstants) <= 128, "Raster push constants must fit the Vulkan minimum.");

struct Model3DPushConstants
{
    alignas(16) glm::mat4 worldToClip { 1.0f };
    alignas(16) glm::vec4 normalToWorld0 { 1.0f, 0.0f, 0.0f, -0.35f };
    alignas(16) glm::vec4 normalToWorld1 { 0.0f, 1.0f, 0.0f, 0.65f };
    alignas(16) glm::vec4 normalToWorld2 { 0.0f, 0.0f, 1.0f, 0.68f };
    alignas(16) glm::vec4 materialColor { 1.0f };
};

static_assert(sizeof(Model3DPushConstants) <= 128, "Model3D push constants must fit the Vulkan minimum.");

struct Renderer2DPushConstants
{
    // Shape, media and text compute shaders share this compact push constant layout
    alignas(16) glm::vec4 rect { 0.0f };
    alignas(16) glm::vec4 uvRect { 0.0f };
    alignas(16) glm::vec4 color0 { 1.0f };
    alignas(16) glm::vec4 color1 { 1.0f };
    alignas(16) glm::vec4 color2 { 0.0f };
    alignas(16) glm::vec4 effect0 { 0.0f };
    alignas(16) glm::vec4 effect1 { 0.0f };
    alignas(16) glm::uvec4 data { 0u };
};

static_assert(sizeof(Renderer2DPushConstants) <= 128, "Renderer2D push constants must fit the Vulkan minimum.");
