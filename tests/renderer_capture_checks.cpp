#include "gles_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/game.hpp"
#include <SDL3/SDL_main.h>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
uint64_t hash(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    check(bool(f), "Missing capture");
    uint64_t h = 1469598103934665603ull;
    char c;
    while (f.get(c)) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3 || argc == 4, "ASSETS OUTPUT [--original|--phone] required");
        const bool phone = argc == 4 && std::string(argv[3]) == "--phone";
        const bool original = argc == 4 && !phone;
        const std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Renderer capture checks", phone ? 1080 : 640,
                                        phone ? 2400 : 360, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        int cases = 0;
        {
            GlesRenderer renderer(window, assets);
            SDL_GL_SetSwapInterval(0);
            std::ofstream csv(out / "hashes.csv");
            csv << "edition,stage,width,height,edge,rotation,original,hash\n";
            if (phone) {
                int w = 0, h = 0;
                SDL_GetWindowSizeInPixels(window, &w, &h);
                check(w == 1080 && h == 2400, "Incorrect phone drawable dimensions");
                Canvas c;
                DecorativeGrid g;
                g.valid = true;
                g.frame = 100;
                g.pattern = stageBackground(49);
                const uint32_t blue = 239;
                g.colors = {blue << 8, blue << 8, ((blue / 4) << 24) | ((blue / 4) << 8)};
                g.worldX = 120 * 65536;
                g.worldY = 320 * 65536;
                c.decorativeGrid(g);
                const auto capture = out / "frame.ppm";
                renderer.present(c, assets, false, true, Aspect::Fit, capture, 0, 0, true, false);
                check(hash(capture) == 7335062437998400555ull,
                      "1080x2400 Atlas differs from saved grid reference");
                cases = 1;
            } else
                for (bool aof : {false, true})
                    for (unsigned stage : {0u, 8u, 12u, 24u, 42u, 49u}) {
                        auto game = std::make_unique<Game>(assets);
                        game->arse(aof);
                        game->endurance(aof);
                        game->stageBackgrounds(true);
                        game->setNativeRate(true, 6);
                        game->deferPresentation(true);
                        game->start(stage);
                        for (int tick = 0; tick < 1800; ++tick)
                            game->step({aof ? 300 : 180, aof ? 350 : 270, 0, 0});
                        renderer.reloadPages(assets);
                        const auto frame = game->frame();
                        const auto random = game->randomState();
                        for (auto size : {std::array<int, 2>{640, 360}, {360, 640}}) {
                            SDL_SetWindowSize(window, size[0], size[1]);
                            SDL_PumpEvents();
                            for (bool edge : {false, true})
                                for (int rotation : {0, 1, 2}) {
                                    if (original && edge &&
                                        std::string(argv[3]) != "--original-all")
                                        continue;
                                    game->renderNative(original);

                                    const auto canvas =
                                        edge ? game->expandedCanvas() : game->canvas();
                                    const auto capture = out / "frame.ppm";
                                    renderer.present(canvas, assets, original, true, Aspect::Fit,
                                                     capture, .35, rotation, edge, false);
                                    csv << (aof ? "AoF" : "Standard") << ',' << stage + 1 << ','
                                        << size[0] << ',' << size[1] << ',' << edge << ','
                                        << rotation << ',' << original << ',' << hash(capture)
                                        << '\n';
                                    ++cases;
                                    check(game->frame() == frame && game->randomState() == random,
                                          "Renderer changed gameplay/RNG");
                                }
                        }
                    }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << cases << " render captures and gameplay/RNG purity checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
