#include "gles_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/frontend.hpp"
#include "grpp/game.hpp"
#include "grpp/phosphor.hpp"
#include <SDL3/SDL_main.h>
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
    static void finalClear(Game &g) {
        g.phase_ = GamePhase::StageClear;
        g.clearTicks_ = 1;
        g.player_.scoreStep = 16;
        g.player_.bombActive = 0;
    }
};
} // namespace grpp
struct Pixels {
    int w, h;
    std::vector<unsigned char> data;
    int channel(int x, int y, int c) const { return data[(size_t(y) * w + x) * 3 + c]; }
};
Pixels read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int max;
    Pixels p;
    in >> magic >> p.w >> p.h >> max;
    in.get();
    p.data.resize(size_t(p.w) * p.h * 3);
    in.read(reinterpret_cast<char *>(p.data.data()), std::streamsize(p.data.size()));
    check(bool(in), "Missing capture");
    return p;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        Assets assets;
        assets.load(argv[1]);
        std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        check(std::abs(backgroundBeamEnergy(512) * BeamIntensity - .9f) < 1.e-6,
              "Near background is not 90 percent");
        for (double depth : {2048., 4096., 8192.}) {
            const float original =
                float((.25 + .75 * 512 / depth) * (.45 + .55 * std::min(1., 2048 / depth)));
            check(backgroundBeamEnergy(depth) == original, "Far background brightness changed");
        }
        for (float energy : {1.f, 1.2f, 1.5f, 2.f, 100.f}) {
            const auto blue = resolvePhosphor({0, 0, energy}, 1);
            check(blue[0] <= .5f && blue[1] <= .5f && blue[2] <= 1,
                  "Whitening exceeded 50 percent");
        }
        const auto knee = resolvePhosphor({0, 0, 1.5f}, 1);
        check(knee[0] > .31f && knee[0] < .32f && knee[2] == 1,
              "Overdrive no longer has a smooth diminishing-return knee");
        const float first = resolvePhosphor({0, 0, 1.2f}, 1)[0],
                    second = resolvePhosphor({0, 0, 1.4f}, 1)[0],
                    third = resolvePhosphor({0, 0, 1.6f}, 1)[0];
        check(first > 0 && second > first && third > second && third - second < second - first,
              "Additional highlight bonuses do not diminish smoothly");
        int recipes = 0;
        for (int a = 3; a <= 6; ++a)
            for (int b = 3; b <= 6; ++b)
                for (int c = 3; c <= 6; ++c)
                    for (int d = 3; d <= 6; ++d)
                        for (int e = 1; e <= 3; ++e)
                            for (int f = 1; f <= 3; ++f) {
                                DecorativeGrid grid;
                                grid.valid = true;
                                grid.pattern = {a, b, c, d, e, f};
                                grid.colors = {0x00ff0000, 0x00ff0000, 0x00ff0000};
                                grid.frame = 23.125;
                                Canvas canvas;
                                drawPreciseBackground(canvas, grid);
                                check(!canvas.draws().empty(),
                                      "Legal stage recipe produced no grid");
                                if (e == 1 || f == 1)
                                    check(std::any_of(canvas.draws().begin(), canvas.draws().end(),
                                                      [](const Draw &draw) {
                                                          return draw.patternedBeam &&
                                                                 draw.x == draw.endX &&
                                                                 draw.y == draw.endY;
                                                      }),
                                          "Legal dotted recipe produced no points");
                                for (const auto &draw : canvas.draws())
                                    check(std::isfinite(draw.energy) && draw.energy > 0,
                                          "Invalid grid radiance");
                                ++recipes;
                            }
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (bool deferred : {false, true})
                    for (unsigned stage = 0; stage < 50; ++stage) {
                        auto legacy = std::make_unique<Game>(assets),
                             native = std::make_unique<Game>(assets);
                        legacy->arse(aof);
                        native->arse(aof);
                        native->setNativeRate(true, steps);
                        native->deferPresentation(deferred);
                        legacy->start(stage);
                        native->start(stage);
                        NativeRateTests::finalClear(*legacy);
                        NativeRateTests::finalClear(*native);
                        legacy->step({120, 270, 0, 0});
                        const auto recipe = legacy->canvas().decorativeGrid().pattern;
                        for (int sub = 0; sub < steps; ++sub) {
                            native->step({120, 270, 0, 0});
                            native->renderNative();
                            check(native->canvas().decorativeGrid().pattern == recipe,
                                  "Next-stage recipe leaked into previous source frame");
                        }
                        legacy->step({120, 270, 0, 0});
                        native->step({120, 270, 0, 0});
                        native->renderNative();
                        check(native->canvas().decorativeGrid().pattern ==
                                  legacy->canvas().decorativeGrid().pattern,
                              "Native next-stage recipe mismatch");
                        check(native->randomState() == legacy->randomState(),
                              "Recipe snapshot changed source RNG");
                    }
        for (int steps : {3, 6})
            for (bool deferred : {false, true}) {
                auto menu = std::make_unique<Frontend>(assets, std::filesystem::path{});
                menu->setNativeRate(true, steps);
                menu->deferPresentation(deferred);
                FrontendInput confirm;
                confirm.confirm = true;
                menu->step(confirm);
                const auto recipe = menu->canvas().decorativeGrid().pattern;
                for (int n = 0; n < 2; ++n) {
                    const auto r = menu->menuBounds().at(2);
                    FrontendInput click;
                    click.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
                    click.pointerActive = click.pointerReleased = true;
                    menu->step(click);
                }
                check(menu->view() == FrontendView::Playing,
                      "Initial recipe route did not enter game");
                menu->preparePresentation();
                check(menu->canvas().decorativeGrid().pattern == recipe,
                      "First presentation used an uninitialized recipe");
            }
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window =
            SDL_CreateWindow("Phosphor checks", 240, 320, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        {
            GlesRenderer renderer(window, assets);
            auto capture = [&](const Canvas &canvas, const char *name) {
                const auto path = output / (std::string(name) + ".ppm");
                renderer.present(canvas, assets, false, true, Aspect::Fit, path);
                return read(path);
            };
            Canvas line, point, cross, dense;
            line.beam(20.5, 160.5, 220.5, 160.5, 0x001f, .2f);
            point.beam(100.5, 160.5, 100.5, 160.5, 0x001f, .2f);
            cross = line;
            cross.beam(100.5, 20.5, 100.5, 300.5, 0x001f, .2f);
            const auto l = capture(line, "line"), p = capture(point, "point"),
                       j = capture(cross, "intersection");
            const int base = l.channel(100, 160, 2), dot = p.channel(100, 160, 2),
                      joined = j.channel(100, 160, 2);
            std::cout << "Blue center: line=" << base << " point=" << dot
                      << " intersection=" << joined << '\n';
            check(std::abs(dot - base * 1.2f) <= 3,
                  "Point is not 20 percent brighter than equivalent beam");
            check(std::abs(joined - base * 2.4f) <= 5,
                  "Crossing does not receive 20 percent intersection boost");
            int previous = joined;
            for (int n = 1; n <= 40; ++n) {
                Canvas shifted = line;
                const float x = 100.5f + n * .025f;
                shifted.beam(x, 20.5, x, 300.5, 0x001f, .2f);
                const auto name = "join-sweep-" + std::to_string(n);
                const int current = capture(shifted, name.c_str()).channel(100, 160, 2);
                check(current <= previous + 1 && previous - current <= 4,
                      "Intersection response contains a coarse brightness step");
                previous = current;
            }
            Canvas mixed, reversed;
            mixed.beam(20.5, 160.5, 220.5, 160.5, 0xf800, .2f);
            mixed.beam(100.5, 20.5, 100.5, 300.5, 0x001f, .2f);
            reversed.beam(100.5, 20.5, 100.5, 300.5, 0x001f, .2f);
            reversed.beam(20.5, 160.5, 220.5, 160.5, 0xf800, .2f);
            const auto purple = capture(mixed, "red-blue-crossing"),
                       reverse = capture(reversed, "red-blue-reversed");
            check(purple.channel(100, 160, 0) > base && purple.channel(100, 160, 2) > base &&
                      purple.channel(100, 160, 1) == 0,
                  "Mixed low-energy crossing lost its magenta hue");
            for (size_t n = 0; n < purple.data.size(); ++n)
                check(std::abs(int(purple.data[n]) - int(reverse.data[n])) <= 1,
                      "Colored crossing depends on stroke order");
            for (int n = 0; n < 1500; ++n) {
                if (n % 500 == 0)
                    dense.fill({0, 0, 1, 1}, 0);
                dense.beam(100.5, 160.5, 100.5, 160.5, 0x001f, 8);
            }
            const auto saturated = capture(dense, "dense-blue");
            check(saturated.channel(100, 160, 0) >= 126 && saturated.channel(100, 160, 0) <= 129 &&
                      saturated.channel(100, 160, 1) <= 129 &&
                      saturated.channel(100, 160, 2) == 255,
                  "Dense blue points escaped bounded whitening");
            const auto restored = capture(line, "restored-line");
            check(restored.data == l.data, "Beam accumulation leaked across frames");
            Canvas occluded = cross;
            occluded.fill({90, 150, 20, 20}, 0x07e0);
            const auto covered = capture(occluded, "occluded-crossing");
            check(covered.channel(100, 160, 0) == 0 && covered.channel(100, 160, 1) == 255 &&
                      covered.channel(100, 160, 2) == 0,
                  "Opaque primitive failed to occlude prior beam light");
            Canvas cluster, interleaved, control;
            for (int n = 0; n < 60; ++n) {
                cluster.beam(100.5, 160.5, 100.5, 160.5, 0x0040, .01, 1, true);
                cluster.markDecorativeLast();
                for (int k = 0; k < 512; ++k)
                    interleaved.beam(10.5f + (k % 8) * 4, 10.5f, 10.5f + (k % 8) * 4, 10.5f, 0x07e0,
                                     .00001f);
                interleaved.beam(100.5, 160.5, 100.5, 160.5, 0x0040, .01, 1, true);
                interleaved.markDecorativeLast();
            }
            const auto faint = capture(cluster, "faint-cluster");
            check(*std::max_element(faint.data.begin(), faint.data.end()) > 0,
                  "Faint dotted light disappeared");
            check(faint.data == capture(interleaved, "faint-interleaved").data,
                  "Faint radiance depends on stroke batching");
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << recipes
                  << " complete legal recipe combinations; frame-boundary snapshots; bounded 20 "
                     "percent phosphor response PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
