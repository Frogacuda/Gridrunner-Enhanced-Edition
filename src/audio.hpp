#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <mutex>
#include <vector>

namespace grpp {
class Audio {
  public:
    explicit Audio(const std::filesystem::path &assets, bool arse = false);
    ~Audio();
    void play(size_t index, int hundredthDecibels = -1000, bool loop = false);
    void stop();

  private:
    struct Voice {
        size_t position = 0;
        bool playing = false, loop = false;
        float gain = 1;
    };
    static void SDLCALL callback(void *userdata, SDL_AudioStream *stream, int additional,
                                 int total);
    void feed(SDL_AudioStream *stream, int additional);
    std::array<std::vector<float>, 40> samples_;
    std::array<Voice, 40> voices_{};
    std::mutex mutex_;
    SDL_AudioStream *stream_ = nullptr;
};
} // namespace grpp
