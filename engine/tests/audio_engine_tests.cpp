#include <vibranceUI/audio/audio.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("vibrance-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    try
    {
        AudioEngine audio;
#if defined(__APPLE__)
        if (audio.backend_name() != "not started")
            throw std::runtime_error("An unused audio engine opened the output device");
#endif
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
#if defined(__APPLE__)
        if (audio.backend_name() != "not started")
            throw std::runtime_error("Preloading a clip opened the output device");
#endif
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
