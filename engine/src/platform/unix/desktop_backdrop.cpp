// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "desktop_backdrop.h"

#define GLFW_INCLUDE_NONE
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
#define GLFW_EXPOSE_NATIVE_X11
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
#include <X11/Xatom.h>
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
#include "background-effect.h"
#include "vibrance-liquid-glass-v1.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using RegionRect = std::array<int, 4>;
    constexpr auto liquidProperty = "_VIBRANCE_LIQUID_GLASS_V1";
    constexpr auto liquidFrameProperty = "_VIBRANCE_LIQUID_GLASS_FRAME_V1";

    bool desktop_surface(const SystemBackdropRegion& region)
    {
        return region.provider == SystemBackdropProvider::eDesktopCompositor &&
            region.opacity > 0.01f && region.width > 0.0f && region.height > 0.0f;
    }

    std::vector<RegionRect> blur_rectangles(std::span<const SystemBackdropRegion> regions,
        glm::vec2 scale, glm::ivec2 size, bool optical, bool liquidSupported)
    {
        std::vector<RegionRect> rectangles;
        for (const auto& region : regions)
        {
            if (!desktop_surface(region)) continue;
            const bool liquid = region.material == GlassMaterial::eLiquid;
            if (optical ? !liquid :
                (liquid && (liquidSupported || region.liquidBlur <= 0.0f)))
                continue;

            const float left = region.x * scale.x;
            const float right = (region.x + region.width) * scale.x;
            const float top = region.y * scale.y;
            const float bottom = (region.y + region.height) * scale.y;
            const auto& viewport = region.viewportRect;
            const bool clipped = region.viewportFade[3] > 0.0f;
            const float clipLeft = clipped ? viewport[0] * scale.x : 0.0f;
            const float clipRight = clipped ? (viewport[0] + viewport[2]) * scale.x : size.x;
            const float clipTop = clipped ? viewport[1] * scale.y : 0.0f;
            const float clipBottom = clipped ? (viewport[1] + viewport[3]) * scale.y : size.y;
            const int first = static_cast<int>(std::ceil(std::max({ top, clipTop, 0.0f })));
            const int last = static_cast<int>(std::floor(std::min({ bottom, clipBottom, float(size.y) })));
            const auto corner_inset = [&](float radius, float distance) {
                radius = std::clamp(radius, 0.0f, std::min(region.width, region.height) * 0.5f);
                const float scaledRadius = radius * scale.y;
                if (distance >= scaledRadius || scaledRadius <= 0.0f) return 0.0f;
                const float offset = 1.0f - std::clamp(distance / scaledRadius, 0.0f, 1.0f);
                const float power = std::lerp(2.0f, std::clamp(region.squirclePower, 2.0f, 8.0f),
                    std::clamp(region.squircleAmount, 0.0f, 1.0f));
                return radius * scale.x * (1.0f - std::pow(1.0f - std::pow(offset, power), 1.0f / power));
            };
            const auto rowsStart = rectangles.size();
            for (int y = first; y < last; ++y)
            {
                const float fromTop = y + 0.5f - top;
                const float fromBottom = bottom - y - 0.5f;
                const float leftInset = std::max(corner_inset(region.topLeftRadius, fromTop),
                    corner_inset(region.bottomLeftRadius, fromBottom));
                const float rightInset = std::max(corner_inset(region.topRightRadius, fromTop),
                    corner_inset(region.bottomRightRadius, fromBottom));
                const int x = static_cast<int>(std::ceil(std::max({ left + leftInset, clipLeft, 0.0f })));
                const int end = static_cast<int>(std::floor(std::min({ right - rightInset, clipRight, float(size.x) })));
                if (end <= x) continue;
                if (rectangles.size() > rowsStart && rectangles.back()[0] == x &&
                    rectangles.back()[2] == end - x && rectangles.back()[1] + rectangles.back()[3] == y)
                    ++rectangles.back()[3];
                else
                    rectangles.push_back({ x, y, end - x, 1 });
            }
        }
        return rectangles;
    }

    std::vector<std::uint32_t> optical_material(std::span<const SystemBackdropRegion> regions,
        glm::vec2 scale, glm::ivec2 size)
    {
        const auto rectangles = blur_rectangles(regions, scale, size, true, true);
        if (rectangles.empty()) return {};
        const auto selected = std::find_if(regions.begin(), regions.end(), [](const auto& region) {
            return desktop_surface(region) && region.material == GlassMaterial::eLiquid;
        });
        const auto percent = [](float value) {
            return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
        };
        std::vector<std::uint32_t> words {
            percent(selected->liquidRefraction), percent(selected->liquidBlur),
            percent(selected->liquidDispersion),
            static_cast<std::uint32_t>(std::lround(std::clamp(selected->cornerRadius *
                std::min(scale.x, scale.y), 0.0f, 256.0f)))
        };
        constexpr std::size_t maxRectangles = 2048u;
        const auto count = std::min(rectangles.size(), maxRectangles);
        words.reserve(words.size() + 4u * count);
        for (const auto& rect : std::span(rectangles).first(count))
            words.insert(words.end(), rect.begin(), rect.end());
        return words;
    }

#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
    bool supports_liquid(Display* display, Atom liquid)
    {
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;
        const auto result = XGetWindowProperty(display, DefaultRootWindow(display), liquid, 0, 0,
            False, AnyPropertyType, &type, &format, &count, &remaining, &data);
        if (data) XFree(data);
        return result == Success && type != None;
    }
#endif
}

namespace vibrance
{
    struct DesktopBackdrop::Impl
    {
        GLFWwindow* window = nullptr;
        DesktopBackdropPaint paint;
        std::vector<RegionRect> applied;
        std::vector<std::uint32_t> opticalApplied;
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        Display* display = nullptr;
        Window x11Window = 0;
        Atom blurAtom = 0;
        Atom liquidAtom = 0;
        Atom frameAtom = 0;
        std::uint32_t frameSequence = 0;
        std::chrono::steady_clock::time_point frameDeadline;
        bool frameSyncActive = false;
        bool opticalX11 = false;
        bool composited = false;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
        wl_display* waylandDisplay = nullptr;
        wl_surface* surface = nullptr;
        wl_registry* registry = nullptr;
        wl_compositor* compositor = nullptr;
        ext_background_effect_manager_v1* manager = nullptr;
        ext_background_effect_surface_v1* effect = nullptr;
        vibrance_liquid_glass_v1* liquid = nullptr;
        std::uint32_t liquidGlobal = 0;
        bool blurAvailable = false;

        void bind_wayland()
        {
            waylandDisplay = glfwGetWaylandDisplay();
            surface = glfwGetWaylandWindow(window);
            if (!waylandDisplay || !surface) return;
            auto* queue = wl_display_create_queue(waylandDisplay);
            if (!queue) return;
            auto* wrapper = wl_proxy_create_wrapper(waylandDisplay);
            if (wrapper)
            {
                wl_proxy_set_queue(static_cast<wl_proxy*>(wrapper), queue);
                registry = wl_display_get_registry(static_cast<wl_display*>(wrapper));
                wl_proxy_wrapper_destroy(wrapper);
            }
            if (registry)
            {
                static constexpr wl_registry_listener listener {
                    [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
                        auto& native = *static_cast<Impl*>(data);
                        const std::string_view type(interface);
                        if (!native.compositor && type == wl_compositor_interface.name)
                            native.compositor = static_cast<wl_compositor*>(
                                wl_registry_bind(registry, name, &wl_compositor_interface, 1));
                        else if (!native.liquid && type == vibrance_liquid_glass_v1_interface.name)
                        {
                            native.liquid = static_cast<vibrance_liquid_glass_v1*>(
                                wl_registry_bind(registry, name, &vibrance_liquid_glass_v1_interface, 1));
                            if (native.liquid) native.liquidGlobal = name;
                        }
                        else if (!native.manager && type == ext_background_effect_manager_v1_interface.name)
                        {
                            native.manager = static_cast<ext_background_effect_manager_v1*>(
                                wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, 1));
                            static constexpr ext_background_effect_manager_v1_listener capabilities {
                                [](void* data, ext_background_effect_manager_v1*, uint32_t flags) {
                                    static_cast<Impl*>(data)->blurAvailable =
                                        (flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) != 0;
                                }
                            };
                            if (native.manager)
                                ext_background_effect_manager_v1_add_listener(native.manager, &capabilities, &native);
                        }
                    },
                    [](void* data, wl_registry*, uint32_t name) {
                        auto& native = *static_cast<Impl*>(data);
                        if (native.liquid && name == native.liquidGlobal)
                        {
                            vibrance_liquid_glass_v1_destroy(native.liquid);
                            native.liquid = nullptr;
                            native.liquidGlobal = 0;
                            native.opticalApplied.clear();
                        }
                    }
                };
                wl_registry_add_listener(registry, &listener, this);
                if (wl_display_roundtrip_queue(waylandDisplay, queue) >= 0 && manager)
                    wl_display_roundtrip_queue(waylandDisplay, queue);
                if (manager) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(manager), nullptr);
                if (liquid) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(liquid), nullptr);
                if (compositor) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(compositor), nullptr);
                wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(registry), nullptr);
            }
            wl_event_queue_destroy(queue);
        }
#endif
    };

    DesktopBackdrop::DesktopBackdrop(GLFWwindow* window, bool opaqueSurface) : impl(std::make_unique<Impl>())
    {
        impl->window = window;
        impl->paint.opaqueSurface = opaqueSurface;
#ifdef GLFW_PLATFORM_X11
        const auto platform = glfwGetPlatform();
#endif
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
#ifdef GLFW_PLATFORM_X11
        if (platform == GLFW_PLATFORM_X11)
#endif
        {
            impl->display = glfwGetX11Display();
            if (impl->display)
            {
                impl->x11Window = glfwGetX11Window(window);
                impl->blurAtom = XInternAtom(impl->display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
                impl->liquidAtom = XInternAtom(impl->display, liquidProperty, False);
                const std::string selection = "_NET_WM_CM_S" + std::to_string(DefaultScreen(impl->display));
                const Atom atom = XInternAtom(impl->display, selection.c_str(), True);
                impl->composited = atom && XGetSelectionOwner(impl->display, atom);
                impl->opticalX11 = impl->composited && supports_liquid(impl->display, impl->liquidAtom);
                if (impl->opticalX11)
                {
                    const auto atom = XInternAtom(impl->display, liquidFrameProperty, True);
                    if (atom && supports_liquid(impl->display, atom)) impl->frameAtom = atom;
                }
            }
        }
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
#ifdef GLFW_PLATFORM_WAYLAND
        if (platform == GLFW_PLATFORM_WAYLAND)
#endif
        {
            if (!opaqueSurface) impl->bind_wayland();
        }
#endif
    }

    DesktopBackdrop::~DesktopBackdrop()
    {
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        if (impl->x11Window)
        {
            if (!impl->applied.empty()) XDeleteProperty(impl->display, impl->x11Window, impl->blurAtom);
            if (!impl->opticalApplied.empty()) XDeleteProperty(impl->display, impl->x11Window, impl->liquidAtom);
        }
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
        if (impl->effect) ext_background_effect_surface_v1_destroy(impl->effect);
        if (impl->liquid) vibrance_liquid_glass_v1_destroy(impl->liquid);
        if (impl->registry) wl_registry_destroy(impl->registry);
        if (impl->manager) ext_background_effect_manager_v1_destroy(impl->manager);
        if (impl->compositor) wl_compositor_destroy(impl->compositor);
#endif
    }

    DesktopBackdropPaint DesktopBackdrop::paint() const
    {
        return impl->paint;
    }

    bool DesktopBackdrop::available() const
    {
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        if (impl->composited && !impl->paint.opaqueSurface) return true;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
        if (impl->liquid || (impl->manager && impl->compositor && impl->blurAvailable)) return true;
#endif
        return false;
    }

    bool DesktopBackdrop::frame_ready()
    {
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        if (impl->frameDeadline == std::chrono::steady_clock::time_point{}) return true;
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;
        const auto result = XGetWindowProperty(impl->display, impl->x11Window, impl->frameAtom, 0, 1,
            False, XA_CARDINAL, &type, &format, &count, &remaining, &data);
        const bool acknowledged = result == Success && type == XA_CARDINAL && format == 32 &&
            count == 1 && data && *reinterpret_cast<const unsigned long*>(data) == impl->frameSequence;
        if (data) XFree(data);
        if (!acknowledged)
        {
            if (std::chrono::steady_clock::now() < impl->frameDeadline) return false;
            if (!impl->frameSyncActive || !supports_liquid(impl->display, impl->frameAtom))
                impl->frameAtom = 0;
        }
        else
        {
            impl->frameSyncActive = true;
        }
        impl->frameDeadline = {};
#endif
        return true;
    }

    void DesktopBackdrop::presentation_failed()
    {
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        if (impl->frameDeadline != std::chrono::steady_clock::time_point{})
        {
            impl->frameDeadline = {};
            impl->opticalApplied.clear();
        }
#endif
    }

    void DesktopBackdrop::update(std::span<const SystemBackdropRegion> regions, glm::uvec2 outputSize,
        [[maybe_unused]] const std::function<bool()>& waitForFrame)
    {
        if (regions.empty() && impl->applied.empty() && impl->opticalApplied.empty()) return;
        int width = 0, height = 0;
        glfwGetWindowSize(impl->window, &width, &height);
        if (width <= 0 || height <= 0 || outputSize.x == 0 || outputSize.y == 0) return;
        const glm::ivec2 size { width, height };
        const auto scale = glm::vec2(size) / glm::vec2(outputSize);
        bool liquidSupported = false;
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        liquidSupported = impl->opticalX11;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
        liquidSupported = liquidSupported || impl->liquid;
#endif
        auto rectangles = blur_rectangles(regions, scale, size, false, liquidSupported);
        auto optics = liquidSupported ? optical_material(regions, scale, size) : std::vector<std::uint32_t>{};
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
        if (impl->x11Window && impl->composited && !impl->paint.opaqueSurface)
        {
            const bool opticsChanged = optics != impl->opticalApplied;
            const bool blurChanged = rectangles != impl->applied;
            if ((!opticsChanged && !blurChanged) || !waitForFrame()) return;
            if (opticsChanged)
            {
                if (optics.empty() && !impl->frameAtom)
                    XDeleteProperty(impl->display, impl->x11Window, impl->liquidAtom);
                else
                {
                    std::vector<unsigned long> words(optics.begin(), optics.end());
                    if (impl->frameAtom)
                    {
                        if (++impl->frameSequence == 0) ++impl->frameSequence;
                        if (words.empty()) words.resize(4);
                        words.insert(words.end(), { impl->frameSequence, 0, 0, 0 });
                        impl->frameDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
                    }
                    XChangeProperty(impl->display, impl->x11Window, impl->liquidAtom, XA_CARDINAL,
                        32, PropModeReplace, reinterpret_cast<const unsigned char*>(words.data()),
                        static_cast<int>(words.size()));
                }
                impl->opticalApplied = std::move(optics);
            }
            if (blurChanged)
            {
                if (rectangles.empty())
                    XDeleteProperty(impl->display, impl->x11Window, impl->blurAtom);
                else
                {
                    std::vector<unsigned long> words;
                    words.reserve(rectangles.size() * 4u);
                    for (const auto& rect : rectangles)
                        words.insert(words.end(), rect.begin(), rect.end());
                    XChangeProperty(impl->display, impl->x11Window, impl->blurAtom, XA_CARDINAL,
                        32, PropModeReplace, reinterpret_cast<const unsigned char*>(words.data()),
                        static_cast<int>(words.size()));
                }
                impl->applied = std::move(rectangles);
            }
            XFlush(impl->display);
            return;
        }
#endif
#ifdef VIBRANCE_HAS_WAYLAND_BACKDROP
        if (impl->liquid && optics != impl->opticalApplied)
        {
            const auto bytes = optics.size() * sizeof(std::uint32_t);
            wl_array material { bytes, bytes, optics.data() };
            vibrance_liquid_glass_v1_set_surface(impl->liquid, impl->surface, &material);
            impl->opticalApplied = std::move(optics);
        }
        if (impl->manager && rectangles != impl->applied)
        {
            if (!impl->effect)
                impl->effect = ext_background_effect_manager_v1_get_background_effect(impl->manager, impl->surface);
            if (!impl->effect) return;
            auto* region = rectangles.empty() ? nullptr : wl_compositor_create_region(impl->compositor);
            if (!region && !rectangles.empty()) return;
            for (const auto& rect : rectangles)
                wl_region_add(region, rect[0], rect[1], rect[2], rect[3]);
            ext_background_effect_surface_v1_set_blur_region(impl->effect, region);
            if (region) wl_region_destroy(region);
            impl->applied = std::move(rectangles);
        }
#endif
    }
}
