#include "grpp/game.hpp"
#include "raster_generic_oracle.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
using namespace grpp;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> double milliseconds(F &&work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        int rasterCases = 0, states = 0, images = 0;
        std::mt19937 random(0x1982);
        for (bool aof : {false, true})
            for (bool custom : {false, true})
                for (int scene = 0; scene < 24; ++scene) {
                    Canvas canvas;
                    canvas.dimensions(aof ? 400 : 240, aof ? 400 : 320);
                    canvas.clear(uint16_t(random()));
                    for (int n = 0; n < 160; ++n) {
                        Draw d;
                        d.sheet = n % 3 == 0 ? -1 : 0;
                        d.x = float(int(random() % 600) - 100);
                        d.y = float(int(random() % 600) - 100);
                        d.width = float(random() % 80 + 1);
                        d.height = float(random() % 80 + 1);
                        d.source = {int(random() % 600) - 30, int(random() % 600) - 30,
                                    int(d.width), int(d.height)};
                        d.paint = {bool(random() % 2), uint16_t(random() % 2 ? 0 : random()),
                                   uint16_t(random() % 2 ? 0 : random())};
                        d.color = uint16_t(random());
                        d.direct = random() % 2 != 0;
                        if (n % 7 == 0) {
                            d.angle = .2f;
                            d.opacity = .55f;
                        }
                        if (n % 11 == 0) {
                            d.x += .25f;
                            d.width *= 1.5f;
                        }
                        canvas.appendDraw(d);
                    }
                    Raster565 raster;
                    const Rect clip{7, 13, 197, 281};
                    if (custom)
                        raster.setClip(clip);
                    raster.render(canvas, assets);
                    check(raster.pixels() == genericRaster(canvas, assets, custom, clip),
                          "Optimized raster changed pixels/clipping/keying");
                    ++rasterCases;
                }
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (unsigned stage : {0u, 11u, 49u}) {
                    Assets other;
                    other.load(argv[1]);
                    auto eager = std::make_unique<Game>(assets),
                         lazy = std::make_unique<Game>(other);
                    for (Game *game : {eager.get(), lazy.get()}) {
                        game->arse(aof);
                        game->endurance(aof);
                        game->setNativeRate(true, steps);
                        game->start(stage);
                    }
                    lazy->deferPresentation(true);
                    for (int tick = 0; tick < 720; ++tick) {
                        LogicalInput input{(tick / 180) % 2 ? 20 : (aof ? 380 : 220),
                                           aof ? 350 : 270, tick % 400 < 10 ? 32u : 0u, 0};
                        eager->step(input);
                        lazy->step(input);
                        check(eager->randomState() == lazy->randomState() &&
                                  eager->frame() == lazy->frame() &&
                                  eager->phase() == lazy->phase() &&
                                  eager->deaths() == lazy->deaths() &&
                                  eager->combatState().score == lazy->combatState().score,
                              "Deferred presentation changed gameplay");
                        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot)
                            check(std::memcmp(&eager->entities().at(slot),
                                              &lazy->entities().at(slot), sizeof(Entity)) == 0,
                                  "Deferred presentation changed an entity/history");
                        const auto a = eager->canvas().decorativeGrid(),
                                   b = lazy->canvas().decorativeGrid();
                        check(a.frame == b.frame && a.advance == b.advance &&
                                  a.worldX == b.worldX && a.worldY == b.worldY,
                              "Deferred camera sample changed");
                        ++states;
                        if (tick % 73 == 0) {
                            lazy->renderNative();
                            Raster565 first, second;
                            first.render(eager->canvas(), assets);
                            second.render(lazy->canvas(), other);
                            check(first.pixels() == second.pixels(),
                                  "Deferred presentation changed Original raster");
                            check(eager->canvas().draws().size() == lazy->canvas().draws().size(),
                                  "Deferred presentation changed draw count");
                            ++images;
                        }
                    }
                }
        auto game = std::make_unique<Game>(assets);
        game->setNativeRate(true, 6);
        game->start();
        const auto run = [&](bool deferred) {
            auto sample = std::make_unique<Game>(assets);
            sample->setNativeRate(true, 6);
            sample->start();
            sample->deferPresentation(deferred);
            const auto elapsed = milliseconds([&] {
                for (int tick = 0; tick < 1800; ++tick) {
                    sample->step({120, 270, 0, 0});
                    if (deferred && tick % 4 == 3)
                        sample->renderNative();
                }
            });
            game = std::move(sample);
            return elapsed;
        };
        const double eagerMs = run(false), lazyMs = run(true);
        Raster565 raster;
        const auto canvas = game->canvas();
        const double genericMs = milliseconds([&] {
            for (int n = 0; n < 60; ++n) {
                auto result = genericRaster(canvas, assets);
                check(!result.empty(), "Empty baseline raster");
            }
        });
        const double optimizedMs = milliseconds([&] {
            for (int n = 0; n < 60; ++n)
                raster.render(canvas, assets);
        });
        std::cout << rasterCases << " randomized raster scenes; " << states
                  << " identical simulation/camera samples; " << images
                  << " identical presented rasters PASS\n";
        std::cout << "Host CPU only (not Android FPS): eager=" << eagerMs
                  << "ms deferred=" << lazyMs << "ms; generic raster=" << genericMs
                  << "ms optimized=" << optimizedMs << "ms\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
