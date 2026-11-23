// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/image_services.h>
#include <stb_image.h>

#include <fstream>
#include <memory>

#if defined(VIBRANCE_HAS_GIO)
#include <gio/gio.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#if __has_include(<stb_image_write.h>)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#define VIBRANCE_HAS_CLIPBOARD_PNG 1
#endif
#endif

namespace colour_palette
{
namespace
{
constexpr std::size_t maximumBytes = 64u * 1024u * 1024u;

#if defined(VIBRANCE_HAS_GIO)
constexpr auto portal = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto requestInterface = "org.freedesktop.portal.Request";

struct ClipboardCall
{
    std::unique_ptr<GCancellable, decltype(&g_object_unref)> cancel {g_cancellable_new(), g_object_unref};
    std::jthread deadline {[this](std::stop_token stop) {
        std::mutex mutex;
        std::unique_lock lock(mutex);
        std::condition_variable_any timer;
        timer.wait_for(lock, stop, std::chrono::milliseconds(900), [] { return false; });
        if (!stop.stop_requested()) g_cancellable_cancel(cancel.get());
    }};
};

GSubprocess* clipboard_process(bool reading)
{
    const auto flags = static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDERR_SILENCE |
        (reading ? G_SUBPROCESS_FLAGS_STDOUT_PIPE : G_SUBPROCESS_FLAGS_STDIN_PIPE));
    if (const char* display = std::getenv("WAYLAND_DISPLAY"); display && *display)
    {
        auto* process = reading ? g_subprocess_new(flags, nullptr, "wl-paste", "--no-newline", "--type", "image", nullptr) :
            g_subprocess_new(flags, nullptr, "wl-copy", "--type", "image/png", nullptr);
        if (process) return process;
    }
    if (const char* display = std::getenv("DISPLAY"); display && *display)
        return g_subprocess_new(flags, nullptr, "xclip", "-selection", "clipboard", "-t", "image/png",
            reading ? "-out" : "-in", nullptr);
    return nullptr;
}

struct PortalPicker
{
    std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context {g_main_context_new(), g_main_context_unref};
    std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus {nullptr, g_object_unref};
    std::function<void(std::optional<std::uint32_t>)> callback;
    guint subscription = 0;
    std::string path;

    ~PortalPicker() { clear(); }

    void clear()
    {
        if (subscription) g_dbus_connection_signal_unsubscribe(bus.get(), subscription);
        subscription = 0;
        callback = {};
    }

    void dispatch()
    {
        while (g_main_context_iteration(context.get(), FALSE)) {}
    }

    static void response(GDBusConnection*, const char*, const char*, const char*, const char*,
        GVariant* parameters, gpointer user)
    {
        auto& picker = *static_cast<PortalPicker*>(user);
        if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ua{sv})"))) return;
        guint32 code = 0;
        GVariant* result = nullptr;
        g_variant_get(parameters, "(u@a{sv})", &code, &result);
        double red = 0.0, green = 0.0, blue = 0.0;
        std::optional<std::uint32_t> colour;
        if (code == 0 && g_variant_lookup(result, "color", "(ddd)", &red, &green, &blue) &&
            std::isfinite(red) && std::isfinite(green) && std::isfinite(blue))
        {
            const auto channel = [](double value) {
                return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
            };
            colour = channel(red) << 16 | channel(green) << 8 | channel(blue);
        }
        g_variant_unref(result);
        auto completion = std::move(picker.callback);
        picker.clear();
        try { completion(colour); } catch (...) {}
    }

    bool begin(std::function<void(std::optional<std::uint32_t>)> completed)
    {
        dispatch();
        if (subscription)
        {
            g_dbus_connection_call(bus.get(), portal, path.c_str(), requestInterface, "Close", nullptr,
                nullptr, G_DBUS_CALL_FLAGS_NONE, 300, nullptr, nullptr, nullptr);
            clear();
        }
        if (!bus || g_dbus_connection_is_closed(bus.get()))
            bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr));
        if (!bus) return false;
        const std::unique_ptr<char, decltype(&g_free)> random(g_uuid_string_random(), g_free);
        std::string token(random.get());
        std::replace(token.begin(), token.end(), '-', '_');
        std::string sender(g_dbus_connection_get_unique_name(bus.get()) + 1);
        std::replace(sender.begin(), sender.end(), '.', '_');
        path = std::string(portalPath) + "/request/" + sender + "/" + token;
        callback = std::move(completed);
        g_main_context_push_thread_default(context.get());
        subscription = g_dbus_connection_signal_subscribe(bus.get(), portal, requestInterface,
            "Response", path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, response, this, nullptr);
        g_main_context_pop_thread_default(context.get());
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
        GVariant* reply = g_dbus_connection_call_sync(bus.get(), portal, portalPath,
            "org.freedesktop.portal.Screenshot", "PickColor", g_variant_new("(sa{sv})", "", &options),
            G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 600, nullptr, nullptr);
        if (reply)
        {
            const char* returnedPath = nullptr;
            g_variant_get(reply, "(&o)", &returnedPath);
            const bool expected = path == returnedPath;
            g_variant_unref(reply);
            if (expected) return true;
        }
        clear();
        return false;
    }
};

thread_local PortalPicker picker;
#endif
}

std::optional<ImageSource> clipboard_image(void*, std::string& error)
{
#if defined(VIBRANCE_HAS_GIO)
    const std::unique_ptr<GSubprocess, decltype(&g_object_unref)> process(clipboard_process(true), g_object_unref);
    if (process)
    {
        ClipboardCall call;
        ImageSource source;
        source.kind = ImageSourceKind::eEncoded;
        source.name = "Clipboard";
        std::array<unsigned char, 16384> buffer;
        auto* stream = g_subprocess_get_stdout_pipe(process.get());
        while (true)
        {
            const auto count = g_input_stream_read(stream, buffer.data(), buffer.size(), call.cancel.get(), nullptr);
            if (count == 0 && g_subprocess_wait_check(process.get(), call.cancel.get(), nullptr) && !source.bytes.empty())
            {
                error.clear();
                return source;
            }
            if (count <= 0 || static_cast<std::size_t>(count) > maximumBytes - source.bytes.size()) break;
            source.bytes.insert(source.bytes.end(), buffer.begin(), buffer.begin() + count);
        }
        g_subprocess_force_exit(process.get());
        error = "The clipboard did not provide a readable image.";
        return {};
    }
#endif
    error = "Image clipboard access requires wl-clipboard or xclip.";
    return {};
}
Image decode_image(const ImageSource& source, std::string& error, std::stop_token stop)
{
    error = "The palette image could not be decoded.";
    const auto valid_size = [](std::uint32_t width, std::uint32_t height) {
        return width && height && height <= maximumBytes / 4u / width;
    };
    if (stop.stop_requested()) return {};
    if (source.kind == ImageSourceKind::eRgba)
    {
        if (!valid_size(source.width, source.height) ||
            source.bytes.size() != std::size_t(source.width) * source.height * 4u) return {};
        error.clear();
        return {source.name, source.path, source.width, source.height, source.bytes};
    }
    std::vector<unsigned char> fileBytes;
    auto bytes = std::span<const unsigned char>(source.bytes);
    if (source.kind == ImageSourceKind::eFile)
    {
        std::ifstream input(source.path, std::ios::binary | std::ios::ate);
        const auto length = input.tellg();
        if (length <= 0 || length > static_cast<std::streamoff>(maximumBytes)) return {};
        fileBytes.resize(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(fileBytes.data()), length)) return {};
        bytes = fileBytes;
    }
    else if (source.kind != ImageSourceKind::eEncoded) return {};
    if (bytes.empty() || bytes.size() > maximumBytes || stop.stop_requested()) return {};

    const auto encodedLength = static_cast<int>(bytes.size());
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), encodedLength, &width, &height, &channels) ||
        width <= 0 || height <= 0 || !valid_size(width, height)) return {};

    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), encodedLength, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels || !valid_size(width, height) || stop.stop_requested()) return {};
    const auto count = std::size_t(width) * height * 4u;
    error.clear();
    return {source.name, source.path, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        {pixels.get(), pixels.get() + count}};
}
bool copy_image_to_clipboard(void*, const Image& image, std::string& error)
{
#if defined(VIBRANCE_HAS_CLIPBOARD_PNG)
    if (image.valid() && image.rgba.size() <= maximumBytes)
    {
        int length = 0;
        const std::unique_ptr<unsigned char, decltype(&std::free)> png(stbi_write_png_to_mem(
            image.rgba.data(), image.width * 4u, image.width, image.height, 4, &length), std::free);
        const std::unique_ptr<GSubprocess, decltype(&g_object_unref)> process(
            png ? clipboard_process(false) : nullptr, g_object_unref);
        if (process)
        {
            ClipboardCall call;
            auto* stream = g_subprocess_get_stdin_pipe(process.get());
            if (g_output_stream_write_all(stream, png.get(), length, nullptr, call.cancel.get(), nullptr) &&
                g_output_stream_close(stream, call.cancel.get(), nullptr) &&
                g_subprocess_wait_check(process.get(), call.cancel.get(), nullptr))
            {
                error.clear();
                return true;
            }
            g_subprocess_force_exit(process.get());
        }
    }
#endif
    error = "The image could not be copied to the desktop clipboard.";
    return false;
}
std::optional<std::uint32_t> screen_colour_at_cursor(std::string& error)
{ error = "Screen colour sampling is unavailable on this platform."; return {}; }
ScreenPickState screen_pick_state() noexcept
{
#if defined(VIBRANCE_HAS_GIO)
    picker.dispatch();
#endif
    return {false, true};
}
bool begin_native_screen_pick(std::function<void(std::optional<std::uint32_t>)> completed)
{
#if defined(VIBRANCE_HAS_GIO)
    return picker.begin(std::move(completed));
#else
    return false;
#endif
}
}
