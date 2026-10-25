#include <vibranceUI/audio/audio.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv)
{
    auto filename = std::filesystem::path(u8"vibrance-audio-\u97f3\u58f0-");
    filename += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav";
    const auto path = std::filesystem::temp_directory_path() / filename;
    try
    {
        AudioEngine audio;
        if (audio.backend_name() != "not started")
            throw std::runtime_error("An unused audio engine opened the output device");
        audio.set_global_volume(0.25f);
        {
            std::ofstream file(path, std::ios::binary);
            const auto little = [&](unsigned value, unsigned bytes) {
                for (unsigned i = 0; i < bytes; ++i)
                    file.put(static_cast<char>((value >> (i * 8)) & 255u));
            };
            file.write("RIFF", 4); little(36 + 882, 4); file.write("WAVEfmt ", 8);
            little(16, 4); little(1, 2); little(1, 2); little(44100, 4);
            little(88200, 4); little(2, 2); little(16, 2);
            file.write("data", 4); little(882, 4);
            for (unsigned i = 0; i < 882; ++i) file.put(0);
        }
        auto clip = audio.load_clip(path);
        if (!clip || audio.global_volume() != 0.25f ||
            audio.load_clip(path).id != clip.id)
            throw std::runtime_error("On-demand audio initialisation or clip caching failed");
        if (audio.backend_name() != "not started")
            throw std::runtime_error("Preloading a clip opened the output device");
        if (audio.find_clip(path).id != clip.id)
            throw std::runtime_error("Unicode audio cache lookup failed");
        audio.unload_clip(clip);
        if (audio.find_clip(path))
            throw std::runtime_error("Unicode audio cache removal failed");
        clip = audio.load_clip(path);
        if (!clip || audio.clip_length(clip) <= 0.0)
            throw std::runtime_error("Unicode audio preload or decoded clip retention failed");
        if (argc == 2 && std::string_view(argv[1]) == "--unicode-preload")
        {
            std::filesystem::remove(path);
            if (audio.backend_name() != "not started" || audio.clip_length(clip) <= 0.0)
                throw std::runtime_error("Unicode preload used playback or depended on an open source file");
            std::cout << "Unicode WAV decoding, cache lookup/removal and silent preload passed\n";
            return 0;
        }
        AudioPlayOptions options;
        options.volume = 0.0f;
        options.paused = true;
        const auto voice = audio.play(clip, options);
        if (!voice || !audio.available() || !audio.is_playing(voice))
            throw std::runtime_error("Audio playback failed after on-demand initialisation");
        audio.stop(voice);
        if (audio.is_playing(voice))
            throw std::runtime_error("Stopped audio voice remained active");
        audio.unload_clip(clip);
        AudioEngine disabled(false);
        if (disabled.available() || disabled.load_clip(path))
            throw std::runtime_error("Disabled audio unexpectedly initialised");
        std::filesystem::remove(path);
    }
    catch (const std::exception &error)
    {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
