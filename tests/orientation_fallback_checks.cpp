#include "gles_renderer.hpp"
#include "grpp/game.hpp"
#include "grpp/options_menu.hpp"
#include <SDL3/SDL_main.h>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
std::vector<unsigned char> read(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w, h, max;
    f >> magic >> w >> h >> max;
    f.get();
    std::vector<unsigned char> pixels(size_t(w) * h * 3);
    f.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
    check(bool(f), "Capture read failed");
    return pixels;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Orientation fallback checks", 640, 360,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        int cases = 0, maxEnhanced = 0, maxIntermediate = 0;
        double maxMean = 0;
        {
            GlesRenderer renderer(window, assets);
            SDL_GL_SetSwapInterval(0);
            for (bool aof : {false, true})
                for (unsigned stage : {0u, 49u}) {
                    auto game = std::make_unique<Game>(assets);
                    game->arse(aof);
                    game->stageBackgrounds(true);
                    game->setNativeRate(true, 6);
                    game->deferPresentation(true);
                    game->start(stage);
                    for (int tick = 0; tick < 600; ++tick)
                        game->step({aof ? 300 : 180, aof ? 350 : 270, 0, 0});
                    renderer.reloadPages(assets);
                    const auto frame = game->frame();
                    const auto rng = game->randomState();
                    for (auto size : {std::array<int, 2>{1280, 640}, {480, 960}}) {
                        SDL_SetWindowSize(window, size[0], size[1]);
                        SDL_PumpEvents();
                        for (bool edge : {false, true})
                            for (bool original : {false, true}) {
                                game->renderNative(original);
                                const Canvas canvas =
                                    edge ? game->expandedCanvas() : game->canvas();
                                auto capture = [&](int rotation, bool direct, const char *name) {
                                    renderer.directResolve(direct);
                                    auto file = out / name;
                                    renderer.present(canvas, assets, original, true, Aspect::Fit,
                                                     file, .35, rotation, edge, false);
                                    return read(file);
                                };
                                const auto upright = capture(0, true, "upright.ppm"),
                                           flipped = capture(3, true, "flipped.ppm");
                                check(upright.size() == flipped.size(),
                                      "180 changed output dimensions");
                                int maximum = 0;
                                double total = 0;
                                for (size_t n = 0; n < upright.size() / 3; ++n)
                                    for (size_t c = 0; c < 3; ++c) {
                                        const int delta = std::abs(
                                            int(upright[n * 3 + c]) -
                                            int(flipped[(upright.size() / 3 - 1 - n) * 3 + c]));
                                        maximum = std::max(maximum, delta);
                                        total += delta;
                                    }
                                if (original)
                                    check(maximum == 0,
                                          "Original 180 output differs from exact CPU rotation");
                                else {
                                    maxEnhanced = std::max(maxEnhanced, maximum);
                                    maxMean = std::max(maxMean, total / upright.size());
                                    const auto intermediate = capture(3, false, "intermediate.ppm");
                                    int difference = 0;
                                    for (size_t n = 0; n < flipped.size(); ++n)
                                        difference =
                                            std::max(difference, std::abs(int(flipped[n]) -
                                                                          int(intermediate[n])));
                                    maxIntermediate = std::max(maxIntermediate, difference);
                                }
                                check(game->frame() == frame && game->randomState() == rng,
                                      "Rotation changed gameplay/RNG");
                                ++cases;
                            }
                    }
                }
        }
        check(maxEnhanced == 0 && maxIntermediate == 0,
              "Enhanced rotated capture is not pixel-exact");
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << cases
                  << " orientation fixtures: Original exact CPU 180 rotation; Enhanced max channel "
                     "difference "
                  << maxEnhanced << " (max mean " << maxMean << "); direct/intermediate max "
                  << maxIntermediate << "; both editions/views/shapes and RNG purity PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
