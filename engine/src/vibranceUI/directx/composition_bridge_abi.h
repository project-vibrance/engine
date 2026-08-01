#pragma once

#include <cstdint>

constexpr std::uint32_t VIBRANCE_COMPOSITION_ABI_VERSION = 8u;
constexpr std::uint32_t VIBRANCE_COMPOSITION_MAX_BUFFERS = 3u;

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
    std::uint32_t reserved = 0u;
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
    float blurRadius = 0.0f;
    float saturation = 1.0f;
    float refraction = 0.035f;
    float tintRed = 1.0f;
    float tintGreen = 1.0f;
    float tintBlue = 1.0f;
    float tintAlpha = 0.0f;
};
#pragma pack(pop)

using VibranceCompositionHandle = void*;
using VibranceCompositionAbiVersionFn = std::uint32_t(__cdecl*)();
using VibranceCompositionLastErrorFn = const char*(__cdecl*)();
using VibranceCompositionCreateFn = VibranceCompositionHandle(__cdecl*)(
    const VibranceCompositionCreateInfo*,
    VibranceCompositionBuffer*,
    std::uint32_t);
using VibranceCompositionDestroyFn = void(__cdecl*)(VibranceCompositionHandle);
using VibranceCompositionAcquireFn = std::uint32_t(__cdecl*)(VibranceCompositionHandle, std::uint32_t);
using VibranceCompositionPresentFn = std::uint32_t(__cdecl*)(
    VibranceCompositionHandle,
    std::uint32_t,
    const VibranceCompositionPresentInfo*);
using VibranceCompositionSetRegionsFn = std::uint32_t(__cdecl*)(
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
constexpr const char* VIBRANCE_COMPOSITION_SET_REGIONS_SYMBOL =
    "vibrance_composition_set_regions";
