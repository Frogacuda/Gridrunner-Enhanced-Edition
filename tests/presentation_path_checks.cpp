#include "gles_renderer.hpp"
#include "grpp/game.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static void phase(Game &g, GamePhase p) { g.phase_ = p; }
};
} // namespace grpp
std::vector<unsigned char> read(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w, h, m;
    f >> magic >> w >> h >> m;
    f.get();
    std::vector<unsigned char> p(size_t(w) * h * 3);
    f.read(reinterpret_cast<char *>(p.data()), p.size());
    check(bool(f), "Capture failed");
    return p;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Preparation equivalence", 640, 360,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        int cases = 0;
        {
            GlesRenderer renderer(window, assets);
            SDL_GL_SetSwapInterval(0);
            for (bool aof : {false, true})
                for (unsigned stage : {0u, 24u, 49u})
                    for (int steps : {3, 6}) {
                        auto game = std::make_unique<Game>(assets);
                        game->arse(aof);
                        game->stageBackgrounds(true);
                        game->setNativeRate(true, steps);
                        game->deferPresentation(true);
                        game->start(stage);
                        for (int tick = 0; tick < 300 * steps; ++tick)
                            game->step({aof ? 300 : 180, aof ? 350 : 270, 0, 0});
                        renderer.reloadPages(assets);
                        for (auto phase :
                             {GamePhase::Playing, GamePhase::StageClear, GamePhase::GameOver}) {
                            NativeRateTests::phase(*game, phase);
                            const auto frame = game->frame();
                            const auto rng = game->randomState();
                            std::vector<Entity> entities;
                            for (size_t n = 0; n < EntityPool::Capacity; ++n)
                                entities.push_back(game->entities().at(n));
                            for (bool reference : {false, true})
                                for (bool edge : {false, true})
                                    for (int rotation : {0, 1, 2, 3}) {
                                        game->renderNative();
                                        Raster565 oldRaster;
                                        oldRaster.render(game->canvas(), assets);
                                        Canvas before = game->canvas();
                                        if (edge)
                                            before = game->expandedCanvas();
                                        renderer.present(before, assets, reference, true,
                                                         Aspect::Fit, out / "before.ppm", 0,
                                                         rotation, edge, false);
                                        const auto baseline = read(out / "before.ppm");
                                        game->renderNative(reference, !edge);
                                        Canvas after =
                                            edge ? game->expandedCanvas() : game->canvas();
                                        renderer.present(after, assets, reference, true,
                                                         Aspect::Fit, out / "after.ppm", 0,
                                                         rotation, edge, false);
                                        check(baseline == read(out / "after.ppm"),
                                              "Optimized presentation changed pixels");
                                        game->renderNative();
                                        Raster565 restored;
                                        restored.render(game->canvas(), assets);
                                        check(restored.pixels() == oldRaster.pixels(),
                                              "Original hot-swap reconstruction changed");
                                        check(game->frame() == frame && game->randomState() == rng,
                                              "Presentation changed frame/RNG");
                                        for (size_t n = 0; n < entities.size(); ++n)
                                            check(std::memcmp(&entities[n], &game->entities().at(n),
                                                              sizeof(Entity)) == 0,
                                                  "Presentation changed entity state");
                                        ++cases;
                                    }
                        }
                    }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << cases
                  << " presentation paths pixel-identical: graphics, overscan axes, rotations, "
                     "phases, rates, editions, Original hot restore and entity/RNG purity PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
