#include "audio.hpp"
#include "original_tables.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace grpp {
Audio::Audio(const std::filesystem::path &root, bool arse) {
    const SDL_AudioSpec destination{SDL_AUDIO_F32, 2, 48000};
    const char *extra[] = {"sw1.wav", "sw2.wav", "sw3.wav", "sw4.wav", "sw5.wav",
                           "sh1.wav", "sh2.wav", "sh3.wav", "sh4.wav"};
    for (size_t i = 0; i < samples_.size(); ++i) {
        SDL_AudioSpec source{};
        Uint8 *original = nullptr;
        Uint32 length = 0;
        if (i >= 31 && !arse)
            continue;
        const char *name = i < 31 ? original::sounds[i] : extra[i - 31];
        const auto path =
            (arse && std::filesystem::exists(root / "aof" / name) ? root / "aof" / name
                                                                  : root / name)
                .string();
        if (!SDL_LoadWAV(path.c_str(), &source, &original, &length))
            throw std::runtime_error("Cannot load " + path + ": " + SDL_GetError());
        Uint8 *converted = nullptr;
        int convertedLength = 0;
        const bool convertedOk = SDL_ConvertAudioSamples(
            &source, original, int(length), &destination, &converted, &convertedLength);
        SDL_free(original);
        if (!convertedOk)
            throw std::runtime_error(SDL_GetError());
        samples_[i].resize(size_t(convertedLength) / sizeof(float));
        std::memcpy(samples_[i].data(), converted, size_t(convertedLength));
        SDL_free(converted);
    }
    stream_ =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &destination, callback, this);
    if (!stream_)
        throw std::runtime_error(SDL_GetError());
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        throw std::runtime_error(SDL_GetError());
    }
}
Audio::~Audio() {
    if (stream_)
        SDL_DestroyAudioStream(stream_);
}
void Audio::play(size_t index, int hundredthDecibels, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= voices_.size())
        return;
    // 00407985 stops/rewinds the sample's one DirectSound buffer and plays it.
    // Repeated events restart that voice; distinct sample IDs overlap.
    voices_[index] = {0, true, loop,
                      std::pow(10.f, float(std::clamp(hundredthDecibels, -10000, 0)) / 2000.f)};
}
void Audio::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &voice : voices_)
        voice.playing = false;
}
void SDLCALL Audio::callback(void *userdata, SDL_AudioStream *stream, int additional, int) {
    static_cast<Audio *>(userdata)->feed(stream, additional);
}
void Audio::feed(SDL_AudioStream *stream, int additional) {
    // Fixed scratch space; no allocations on our audio thread.
    std::array<float, 2048> mix{};
    while (additional > 0) {
        const int count = std::min(int(mix.size()), (additional + 7) / 8 * 2);
        std::fill_n(mix.begin(), count, 0.f);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t id = 0; id < voices_.size(); ++id) {
                auto &voice = voices_[id];
                const auto &sample = samples_[id];
                for (int i = 0; i < count && voice.playing; ++i) {
                    if (voice.position == sample.size()) {
                        if (voice.loop && !sample.empty())
                            voice.position = 0;
                        else {
                            voice.playing = false;
                            break;
                        }
                    }
                    mix[size_t(i)] += sample[voice.position++] * voice.gain;
                }
            }
        }
        for (int i = 0; i < count; ++i)
            mix[size_t(i)] = std::clamp(mix[size_t(i)], -1.f, 1.f);
        if (!SDL_PutAudioStreamData(stream, mix.data(), count * int(sizeof(float))))
            return;
        additional -= count * int(sizeof(float));
    }
}
} // namespace grpp
