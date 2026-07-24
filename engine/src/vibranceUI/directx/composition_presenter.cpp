#include "composition_presenter.h"
#include "composition_bridge_abi.h"

#include <vibranceUI/core/logger.h>

#include <algorithm>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
std::uint32_t memory_type_index(
    vk::PhysicalDevice physicalDevice,
    std::uint32_t typeBits,
    vk::MemoryPropertyFlags desired)
{
    const vk::PhysicalDeviceMemoryProperties properties =
        physicalDevice.getMemoryProperties();
    for (std::uint32_t index = 0u; index < properties.memoryTypeCount; ++index)
    {
        if ((typeBits & (1u << index)) != 0u &&
            (properties.memoryTypes[index].propertyFlags & desired) == desired)
        {
            return index;
        }
    }
    for (std::uint32_t index = 0u; index < properties.memoryTypeCount; ++index)
    {
        if ((typeBits & (1u << index)) != 0u)
        {
            return index;
        }
    }
    return UINT32_MAX;
}
}

struct WindowsCompositionPresenter::Impl
{
    struct SharedImage
    {
        vk::Image image {};
        vk::DeviceMemory memory {};
        bool firstUse = true;
    };

#if defined(_WIN32)
    HMODULE module = nullptr;
    VibranceCompositionHandle bridge = nullptr;
    VibranceCompositionDestroyFn destroy = nullptr;
    VibranceCompositionAcquireFn acquire = nullptr;
    VibranceCompositionPresentFn present = nullptr;
    VibranceCompositionSetRegionsFn setRegions = nullptr;
#endif
    std::vector<SharedImage> images;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    bool ready = false;

#if defined(_WIN32)
    static HMODULE load_module()
    {
        wchar_t enginePath[MAX_PATH] {};
        HMODULE engineModule = nullptr;
        const auto address = reinterpret_cast<LPCWSTR>(
            reinterpret_cast<const void*>(&system_backdrop_platform_supported));
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address,
                &engineModule) != FALSE &&
            GetModuleFileNameW(engineModule, enginePath, MAX_PATH) > 0u)
        {
            wchar_t* separator = std::wcsrchr(enginePath, L'\\');
            if (separator)
            {
                *(separator + 1) = L'\0';
                const wchar_t fileName[] = L"vibrance_win32_composition.dll";
                if (std::wcslen(enginePath) + std::size(fileName) < MAX_PATH)
                {
                    std::wcscat(enginePath, fileName);
                    if (HMODULE result = LoadLibraryExW(
                            enginePath,
                            nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
                    {
                        return result;
                    }
                }
            }
        }
        return LoadLibraryW(L"vibrance_win32_composition.dll");
    }

    template <typename Function>
    static Function load_function(HMODULE moduleHandle, const char* symbol)
    {
        return reinterpret_cast<Function>(GetProcAddress(moduleHandle, symbol));
    }
#endif

    void destroy_vulkan_images(vk::Device device)
    {
        if (!device)
        {
            images.clear();
            return;
        }
        for (SharedImage& shared : images)
        {
            if (shared.image)
            {
                device.destroyImage(shared.image);
            }
            if (shared.memory)
            {
                device.freeMemory(shared.memory);
            }
        }
        images.clear();
    }

#if defined(_WIN32)
    bool import_image(
        vk::PhysicalDevice physicalDevice,
        vk::Device logicalDevice,
        HANDLE sharedHandle,
        SharedImage& destination)
    {
        vk::ExternalMemoryImageCreateInfo externalInfo = {};
        externalInfo.handleTypes =
            vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture;

        vk::ImageCreateInfo imageInfo = {};
        imageInfo.pNext = &externalInfo;
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = vk::Format::eB8G8R8A8Unorm;
        imageInfo.extent = vk::Extent3D(width, height, 1u);
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eTransferSrc;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        auto imageResult = logicalDevice.createImage(imageInfo);
        if (imageResult.result != vk::Result::eSuccess)
        {
            return false;
        }
        destination.image = imageResult.value;

        const vk::MemoryRequirements requirements =
            logicalDevice.getImageMemoryRequirements(destination.image);
        const std::uint32_t typeIndex = memory_type_index(
            physicalDevice,
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        if (typeIndex == UINT32_MAX)
        {
            logicalDevice.destroyImage(destination.image);
            destination.image = nullptr;
            return false;
        }

        vk::MemoryDedicatedAllocateInfo dedicatedInfo = {};
        dedicatedInfo.image = destination.image;

        vk::ImportMemoryWin32HandleInfoKHR importInfo = {};
        importInfo.pNext = &dedicatedInfo;
        importInfo.handleType =
            vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture;
        importInfo.handle = sharedHandle;

        vk::MemoryAllocateInfo allocationInfo = {};
        allocationInfo.pNext = &importInfo;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = typeIndex;
        auto memoryResult = logicalDevice.allocateMemory(allocationInfo);
        if (memoryResult.result != vk::Result::eSuccess)
        {
            logicalDevice.destroyImage(destination.image);
            destination.image = nullptr;
            return false;
        }
        destination.memory = memoryResult.value;

        const vk::Result bindResult = logicalDevice.bindImageMemory(
            destination.image,
            destination.memory,
            0u);
        if (bindResult != vk::Result::eSuccess)
        {
            logicalDevice.destroyImage(destination.image);
            logicalDevice.freeMemory(destination.memory);
            destination = {};
            return false;
        }
        return true;
    }
#endif
};

WindowsCompositionPresenter::WindowsCompositionPresenter() :
    impl(std::make_unique<Impl>())
{
}

WindowsCompositionPresenter::~WindowsCompositionPresenter() = default;

bool WindowsCompositionPresenter::initialise(
    void* nativeWindow,
    vk::PhysicalDevice physicalDevice,
    vk::Device logicalDevice,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bufferCount,
    std::uint32_t graphicsQueueFamilyIndex)
{
#if !defined(_WIN32)
    (void)nativeWindow;
    (void)physicalDevice;
    (void)logicalDevice;
    (void)width;
    (void)height;
    (void)bufferCount;
    (void)graphicsQueueFamilyIndex;
    return false;
#else
    if (!nativeWindow || !physicalDevice || !logicalDevice ||
        width == 0u || height == 0u)
    {
        return false;
    }

    shutdown(logicalDevice);
    impl->module = Impl::load_module();
    if (!impl->module)
    {
        Logger::fetch_logger()->warning(
            "Windows Composition presenter is unavailable: "
            "vibrance_win32_composition.dll was not found.");
        return false;
    }

    const auto abiVersion = Impl::load_function<VibranceCompositionAbiVersionFn>(
        impl->module,
        VIBRANCE_COMPOSITION_ABI_VERSION_SYMBOL);
    const auto create = Impl::load_function<VibranceCompositionCreateFn>(
        impl->module,
        VIBRANCE_COMPOSITION_CREATE_SYMBOL);
    const auto lastError = Impl::load_function<VibranceCompositionLastErrorFn>(
        impl->module,
        VIBRANCE_COMPOSITION_LAST_ERROR_SYMBOL);
    impl->destroy = Impl::load_function<VibranceCompositionDestroyFn>(
        impl->module,
        VIBRANCE_COMPOSITION_DESTROY_SYMBOL);
    impl->acquire = Impl::load_function<VibranceCompositionAcquireFn>(
        impl->module,
        VIBRANCE_COMPOSITION_ACQUIRE_SYMBOL);
    impl->present = Impl::load_function<VibranceCompositionPresentFn>(
        impl->module,
        VIBRANCE_COMPOSITION_PRESENT_SYMBOL);
    impl->setRegions = Impl::load_function<VibranceCompositionSetRegionsFn>(
        impl->module,
        VIBRANCE_COMPOSITION_SET_REGIONS_SYMBOL);
    if (!abiVersion || !create || !impl->destroy || !impl->acquire ||
        !impl->present || !impl->setRegions ||
        abiVersion() != VIBRANCE_COMPOSITION_ABI_VERSION)
    {
        Logger::fetch_logger()->warning(
            "Windows Composition presenter ABI is missing or incompatible.");
        shutdown(logicalDevice);
        return false;
    }

    vk::PhysicalDeviceIDProperties idProperties = {};
    vk::PhysicalDeviceProperties2 properties = {};
    properties.pNext = &idProperties;
    physicalDevice.getProperties2(&properties);
    if (!idProperties.deviceLUIDValid)
    {
        Logger::fetch_logger()->warning(
            "Windows Composition presenter could not match the Vulkan GPU to DXGI.");
        shutdown(logicalDevice);
        return false;
    }

    LUID adapterLuid {};
    std::memcpy(&adapterLuid, idProperties.deviceLUID, VK_LUID_SIZE);
    const std::uint32_t safeBufferCount = std::clamp(
        bufferCount,
        2u,
        VIBRANCE_COMPOSITION_MAX_BUFFERS);
    VibranceCompositionCreateInfo createInfo = {};
    createInfo.width = width;
    createInfo.height = height;
    createInfo.bufferCount = safeBufferCount;
    createInfo.adapterLuidLow = adapterLuid.LowPart;
    createInfo.adapterLuidHigh = adapterLuid.HighPart;
    createInfo.window = nativeWindow;

    std::vector<VibranceCompositionBuffer> buffers(safeBufferCount);
    for (std::uint32_t index = 0u; index < safeBufferCount; ++index)
    {
        buffers[index].index = index;
    }
    impl->bridge = create(
        &createInfo,
        buffers.data(),
        static_cast<std::uint32_t>(buffers.size()));
    if (!impl->bridge)
    {
        const char* detail = lastError ? lastError() : nullptr;
        Logger::fetch_logger()->warning(
            "Windows Composition/D3D11 presenter initialisation failed; using native Vulkan presentation.");
        if (detail && detail[0] != '\0')
        {
            Logger::fetch_logger()->warning(
                std::string("Windows Composition bridge detail: ") + detail);
        }
        shutdown(logicalDevice);
        return false;
    }

    impl->width = width;
    impl->height = height;
    impl->graphicsQueueFamilyIndex = graphicsQueueFamilyIndex;
    impl->images.resize(safeBufferCount);
    for (std::uint32_t index = 0u; index < safeBufferCount; ++index)
    {
        if (!buffers[index].sharedHandle ||
            !impl->import_image(
                physicalDevice,
                logicalDevice,
                static_cast<HANDLE>(buffers[index].sharedHandle),
                impl->images[index]))
        {
            Logger::fetch_logger()->warning(
                "Vulkan could not import a D3D11 Composition buffer; using native presentation.");
            shutdown(logicalDevice);
            return false;
        }
    }

    impl->ready = true;
    Logger::fetch_logger()->info(
        "Windows Composition presenter enabled: Vulkan renderer -> D3D11 bridge -> OS compositor.");
    return true;
#endif
}

void WindowsCompositionPresenter::shutdown(vk::Device logicalDevice)
{
    if (!impl)
    {
        return;
    }
    if (logicalDevice)
    {
        (void)logicalDevice.waitIdle();
    }
    impl->ready = false;
    impl->destroy_vulkan_images(logicalDevice);
#if defined(_WIN32)
    if (impl->bridge && impl->destroy)
    {
        impl->destroy(impl->bridge);
    }
    impl->bridge = nullptr;
    impl->destroy = nullptr;
    impl->acquire = nullptr;
    impl->present = nullptr;
    impl->setRegions = nullptr;
    if (impl->module)
    {
        FreeLibrary(impl->module);
    }
    impl->module = nullptr;
#endif
}

bool WindowsCompositionPresenter::available() const
{
    return impl && impl->ready;
}

std::uint32_t WindowsCompositionPresenter::buffer_count() const
{
    return impl ? static_cast<std::uint32_t>(impl->images.size()) : 0u;
}

vk::Image WindowsCompositionPresenter::image(std::uint32_t index) const
{
    if (!available() || index >= impl->images.size())
    {
        return {};
    }
    return impl->images[index].image;
}

bool WindowsCompositionPresenter::first_use(std::uint32_t index) const
{
    return available() && index < impl->images.size() &&
        impl->images[index].firstUse;
}

void WindowsCompositionPresenter::mark_used(std::uint32_t index)
{
    if (available() && index < impl->images.size())
    {
        impl->images[index].firstUse = false;
    }
}

bool WindowsCompositionPresenter::acquire(std::uint32_t index)
{
#if defined(_WIN32)
    return available() && index < impl->images.size() &&
        impl->acquire(impl->bridge, index) != 0u;
#else
    (void)index;
    return false;
#endif
}

bool WindowsCompositionPresenter::present(std::uint32_t index)
{
#if defined(_WIN32)
    return available() && index < impl->images.size() &&
        impl->present(impl->bridge, index) != 0u;
#else
    (void)index;
    return false;
#endif
}

bool WindowsCompositionPresenter::set_regions(
    const std::vector<SystemBackdropRegion>& regions)
{
#if !defined(_WIN32)
    (void)regions;
    return false;
#else
    if (!available())
    {
        return false;
    }
    std::vector<VibranceCompositionRegion> nativeRegions;
    nativeRegions.reserve(regions.size());
    for (const SystemBackdropRegion& region : regions)
    {
        if (region.material == SystemBackdropMaterial::eOff ||
            region.width <= 0.0f || region.height <= 0.0f)
        {
            continue;
        }
        VibranceCompositionRegion native = {};
        native.material = static_cast<std::uint32_t>(region.material);
        native.provider = static_cast<std::uint32_t>(region.provider);
        native.shape = static_cast<std::uint32_t>(region.shape);
        native.x = region.x;
        native.y = region.y;
        native.width = region.width;
        native.height = region.height;
        native.cornerRadius = region.cornerRadius;
        native.topLeftRadius = region.topLeftRadius;
        native.topRightRadius = region.topRightRadius;
        native.bottomRightRadius = region.bottomRightRadius;
        native.bottomLeftRadius = region.bottomLeftRadius;
        native.squircleAmount = region.squircleAmount;
        native.squirclePower = region.squirclePower;
        native.blurRadius = region.blurRadius;
        native.saturation = region.saturation;
        native.tintRed = region.tint.red;
        native.tintGreen = region.tint.green;
        native.tintBlue = region.tint.blue;
        native.tintAlpha = region.tint.alpha;
        nativeRegions.push_back(native);
    }
    return impl->setRegions(
        impl->bridge,
        nativeRegions.empty() ? nullptr : nativeRegions.data(),
        static_cast<std::uint32_t>(nativeRegions.size())) != 0u;
#endif
}
