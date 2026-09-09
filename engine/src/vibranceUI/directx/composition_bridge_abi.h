#pragma once

#include <cstdint>

#if defined(_WIN32)
#define VIBRANCE_COMPOSITION_CALL __cdecl
#else
// The engine's portable presenter stub includes this POD ABI header too.
#define VIBRANCE_COMPOSITION_CALL
#endif

constexpr std::uint32_t VIBRANCE_COMPOSITION_ABI_VERSION = 16u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_MAX_BUFFERS = 3u;

constexpr std::uint32_t VIBRANCE_COMPOSITION_CREATE_TRANSPARENT_FRAMEBUFFER =
    1u << 0u;
// Uses ordinary D3D11 source textures populated by the host instead of
// exporting keyed-mutex textures to Vulkan. This is the architecture-neutral
// fallback for native Vulkan drivers that cannot import/synchronise D3D11
// resources (notably current Qualcomm Windows ARM64 ICDs).
constexpr std::uint32_t VIBRANCE_COMPOSITION_CREATE_CPU_UPLOAD = 1u << 1u;

// A keyed D3D11 texture starts available at key 0. Vulkan acquires 0 and
// releases 1 after rendering; D3D11 then acquires 1 and releases 0 after its
// copy. These values are shared by both sides of the bridge ABI so ownership
// cannot silently drift.
constexpr std::uint64_t VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY = 0u;
constexpr std::uint64_t VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY = 1u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_TIMEOUT_MS = 1000u;

constexpr std::uint32_t VIBRANCE_COMPOSITION_PRESENT_SYNCHRONIZE = 1u << 0u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_PRESENT_FAILED = 0u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_PRESENTED = 1u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_PRESENT_DEFERRED = 2u;

#pragma pack(push, 8)
struct VibranceCompositionCreateInfo
{
    std::uint32_t structSize = sizeof(VibranceCompositionCreateInfo);
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t bufferCount = 0u;
    std::uint32_t adapterLuidLow = 0u;
    std::int32_t adapterLuidHigh = 0;
    std::uint32_t flags = 0u;
    void* window = nullptr;
};

struct VibranceCompositionBuffer
{
    std::uint32_t structSize = sizeof(VibranceCompositionBuffer);
    std::uint32_t index = 0u;
    void* sharedHandle = nullptr;
};

struct VibranceCompositionPresentInfo
{
    std::uint32_t structSize = sizeof(VibranceCompositionPresentInfo);
    std::uint32_t flags = 0u;
    std::uint32_t contentX = 0u;
    std::uint32_t contentY = 0u;
    std::uint32_t contentWidth = 0u;
    std::uint32_t contentHeight = 0u;
    std::uint32_t damageX = 0u;
    std::uint32_t damageY = 0u;
    std::uint32_t damageWidth = 0u;
    std::uint32_t damageHeight = 0u;
};

struct VibranceCompositionUploadInfo
{
    std::uint32_t structSize = sizeof(VibranceCompositionUploadInfo);
    const void* pixels = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t rowPitch = 0u;
};

struct VibranceCompositionRegion
{
    std::uint32_t structSize = sizeof(VibranceCompositionRegion);
    std::uint32_t material = 0u;
    std::uint32_t provider = 0u;
    std::uint32_t shape = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float cornerRadius = 0.0f;
    float topLeftRadius = 0.0f;
    float topRightRadius = 0.0f;
    float bottomRightRadius = 0.0f;
    float bottomLeftRadius = 0.0f;
    float squircleAmount = 1.0f;
    float squirclePower = 4.0f;
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    float verticalStart = 0.0f;
    float opacity = 1.0f;
    float blurRadius = 0.0f;
    float saturation = 1.0f;
    float tintRed = 1.0f;
    float tintGreen = 1.0f;
    float tintBlue = 1.0f;
    float tintAlpha = 0.0f;
};
#pragma pack(pop)

using VibranceCompositionHandle = void*;
using VibranceCompositionAbiVersionFn =
    std::uint32_t(VIBRANCE_COMPOSITION_CALL*)();
using VibranceCompositionLastErrorFn =
    const char*(VIBRANCE_COMPOSITION_CALL*)();
using VibranceCompositionCreateFn =
    VibranceCompositionHandle(VIBRANCE_COMPOSITION_CALL*)(
    const VibranceCompositionCreateInfo*,
    VibranceCompositionBuffer*,
    std::uint32_t);
using VibranceCompositionDestroyFn =
    void(VIBRANCE_COMPOSITION_CALL*)(VibranceCompositionHandle);
using VibranceCompositionAcquireFn =
    std::uint32_t(VIBRANCE_COMPOSITION_CALL*)(
        VibranceCompositionHandle,
        std::uint32_t);
using VibranceCompositionPresentFn =
    std::uint32_t(VIBRANCE_COMPOSITION_CALL*)(
    VibranceCompositionHandle,
    std::uint32_t,
    const VibranceCompositionPresentInfo*);
using VibranceCompositionUploadFn =
    std::uint32_t(VIBRANCE_COMPOSITION_CALL*)(
    VibranceCompositionHandle,
    std::uint32_t,
    const VibranceCompositionUploadInfo*);
using VibranceCompositionSetRegionsFn =
    std::uint32_t(VIBRANCE_COMPOSITION_CALL*)(
    VibranceCompositionHandle,
    const VibranceCompositionRegion*,
    std::uint32_t);

constexpr const char* VIBRANCE_COMPOSITION_ABI_VERSION_SYMBOL =
    "vibrance_composition_abi_version";
constexpr const char* VIBRANCE_COMPOSITION_LAST_ERROR_SYMBOL =
    "vibrance_composition_last_error";
constexpr const char* VIBRANCE_COMPOSITION_CREATE_SYMBOL =
    "vibrance_composition_create";
constexpr const char* VIBRANCE_COMPOSITION_DESTROY_SYMBOL =
    "vibrance_composition_destroy";
constexpr const char* VIBRANCE_COMPOSITION_ACQUIRE_SYMBOL =
    "vibrance_composition_acquire";
constexpr const char* VIBRANCE_COMPOSITION_PRESENT_SYMBOL =
    "vibrance_composition_present";
constexpr const char* VIBRANCE_COMPOSITION_UPLOAD_SYMBOL =
    "vibrance_composition_upload";
constexpr const char* VIBRANCE_COMPOSITION_SET_REGIONS_SYMBOL =
    "vibrance_composition_set_regions";

#undef VIBRANCE_COMPOSITION_CALL
