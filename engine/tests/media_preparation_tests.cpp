#include <vibranceUI/core/logger.h>
#include <vibranceUI/glfw/window.h>
#include <vibranceUI/renderer/media2d.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <stop_token>
#include <string_view>
#include <thread>

namespace
{
    std::vector<unsigned char> make_gif()
    {
        return {
            'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0,
            255, 0, 0, 0, 255, 0,
            0x21, 0xf9, 4, 0, 7, 0, 0, 0,
            0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 1, 0,
            0x21, 0xf9, 4, 0, 13, 0, 0, 0,
            0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4c, 1, 0,
            0x3b
        };
    }

    void write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

int main(int argc, char** argv)
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("vibrance-media-preparation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    } cleanup { directory };
    LoggerOutputOptions logging;
    logging.directory = directory / "logs";
    Logger::configure_output(logging);
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };

    const auto path = directory / "two-frames.gif";
    const auto bytes = make_gif();
    write_bytes(path, bytes);
    Media2DLoadOptions options;
    options.maxDecodedBytes = 8u;
    options.premultiplyAlpha = true;
    const auto prepared = prepare_media_2d(path, options);
    expect(prepared.valid() && prepared.frame_count() == 2u && prepared.decoded_byte_count() == 8u,
        "The complete two-frame GIF must prepare within its exact decoded-byte budget");
    expect(prepared.path() == std::filesystem::absolute(path).lexically_normal(),
        "Prepared media must retain its normalised source path");
    auto limited = options;
    limited.maxDecodedBytes = 7u;
    expect(!prepare_media_2d(path, limited).valid(), "An animation over budget must fail safely");
    limited.maxAnimationFrames = 1u;
    expect(!prepare_media_2d(path, limited).valid(),
        "GIF preflight must bound stb's complete allocation even when the caller samples frames");
    limited.maxDecodedBytes = 8u;
    const auto sampled = prepare_media_2d(path, limited);
    expect(sampled.valid() && sampled.frame_count() == 1u && sampled.decoded_byte_count() == 4u,
        "Existing caller-requested frame sampling must remain available");
    expect(media_2d_cache_key(path, limited) != prepared.cache_key(),
        "Media with different frame sampling must have separate renderer cache entries");
    auto samePixels = options;
    samePixels.maxDecodedBytes *= 2u;
    expect(media_2d_cache_key(path, samePixels) == prepared.cache_key(),
        "A preparation safety budget must not duplicate identical renderer resources");

    const auto malformed = directory / "malformed.gif";
    auto truncated = bytes;
    truncated.resize(30u);
    write_bytes(malformed, truncated);
    expect(!prepare_media_2d(malformed, options).valid(), "Truncated GIF blocks must fail safely");
    const auto oversized = directory / "oversized.gif";
    auto oversizedBytes = bytes;
    oversizedBytes[6] = oversizedBytes[7] = oversizedBytes[8] = oversizedBytes[9] = 0xffu;
    write_bytes(oversized, oversizedBytes);
    expect(!prepare_media_2d(oversized, options).valid(),
        "A tiny encoded GIF with a huge decoded canvas must fail before allocating pixels");
    expect(!prepare_media_2d(directory / "missing.gif", options).valid(),
        "Missing files must retain the caller's placeholder");

    std::stop_source cancelled;
    cancelled.request_stop();
    expect(!prepare_media_2d(path, options, cancelled.get_token()).valid(),
        "A cancelled request must not decode a valid source");
    auto first = std::async(std::launch::async, [&] { return prepare_media_2d(path, options); });
    auto second = std::async(std::launch::async, [&] {
        return prepare_media_2d(path, options, cancelled.get_token());
    });
    expect(first.get().valid() && !second.get().valid(),
        "Concurrent preparations must not share cancellation or byte-budget state");

    // GPU checks run separately so the decoder/cancellation checks stay usable
    // on a machine without a graphics device, on both renderer backends.
    if (argc < 2 || std::string_view(argv[1]) != "--renderer") return passed ? 0 : 1;
    GlfwWindowHostOptions hostOptions;
    hostOptions.title = "Vibrance incremental media upload regression";
    hostOptions.size = { 80, 80 };
    hostOptions.focusOnShow = false;
    hostOptions.showInTaskbar = hostOptions.showInAltTab = false;
    hostOptions.enableAudio = false;
    GlfwWindowHost host(hostOptions);
    if (!host.open()) return passed ? 77 : 1;
    set_glfw_window_visible(host.window(), false);
    auto& engine = *host.engine();
    expect(!engine.upload_media_2d(prepared, 0u).finished,
        "A zero upload budget must perform no frame uploads");
    const auto partial = engine.upload_media_2d(prepared, 1u);
    expect(!partial.finished && !partial.handle.valid(),
        "One upload tick must not expose a partial two-frame animation");
    engine.cancel_media_2d_upload(prepared);
    const auto restarted = engine.upload_media_2d(prepared, 1u);
    expect(!restarted.finished && !restarted.handle.valid(),
        "Cancellation must discard unpublished frames before another load begins");
    Media2DUploadResult complete;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete.finished && std::chrono::steady_clock::now() < deadline)
    {
        complete = engine.upload_media_2d(prepared, 1u);
        if (!complete.finished) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(complete.finished && complete.handle.drawable && complete.handle.frameCount == 2u,
        "Fence polling must eventually publish the complete original animation");
    expect(complete.handle.frameDurationsSeconds.size() == 2u &&
        std::abs(complete.handle.frameDurationsSeconds[0] - 0.07) < 0.000001 &&
        std::abs(complete.handle.frameDurationsSeconds[1] - 0.13) < 0.000001 &&
        std::abs(complete.handle.durationSeconds - 0.2) < 0.000001,
        "Lazy upload must preserve authored variable frame delays and total loop duration");
    expect(engine.upload_media_2d(prepared).handle.id == complete.handle.id &&
        engine.load_media_2d(path, options).id == complete.handle.id,
        "Prepared and synchronous loads must share the existing Engine cache");
    for (uint32_t index = 0u; index < 10u; ++index)
    {
        auto mixedOptions = options;
        mixedOptions.rasterWidth = 100u + index;
        const auto mixed = prepare_media_2d(path, mixedOptions);
        expect(!engine.upload_media_2d(mixed).finished,
            "An uncached prepared twin must begin without publishing incomplete media");
        const auto synchronous = engine.load_media_2d(path, mixedOptions);
        const auto cached = engine.upload_media_2d(mixed);
        expect(cached.finished && cached.handle.id == synchronous.id && synchronous.drawable,
            "A synchronous completion must replace and retire its unpublished prepared twin");
    }
    auto finalOptions = options;
    finalOptions.rasterWidth = 200u;
    const auto finalPrepared = prepare_media_2d(path, finalOptions);
    Media2DUploadResult finalUpload;
    const auto finalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!finalUpload.finished && std::chrono::steady_clock::now() < finalDeadline)
    {
        finalUpload = engine.upload_media_2d(finalPrepared);
        if (!finalUpload.finished) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(finalUpload.finished && finalUpload.handle.drawable,
        "Retiring synchronous/prepared twins must release admission slots for later loads");
    host.close();
    return passed ? 0 : 1;
}
