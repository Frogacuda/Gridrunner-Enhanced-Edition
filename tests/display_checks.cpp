#include "grpp/background.hpp"
#include "grpp/clock.hpp"
#include "grpp/game.hpp"
#include "grpp/presentation_clock.hpp"
#include <cstring>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static uint64_t hash(const Game &g) {
        uint64_t h = 1469598103934665603ull;
        auto add = [&](const void *p, size_t n) {
            auto b = static_cast<const unsigned char *>(p);
            while (n--) {
                h ^= *b++;
                h *= 1099511628211ull;
            }
        };
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            add(&g.pool_.at(i), sizeof(Entity));
        add(&g.player_, sizeof(g.player_));
        add(&g.combatState_, sizeof(g.combatState_));
        add(&g.worldState_, sizeof(g.worldState_));
        add(&g.grid_, sizeof(g.grid_));
        add(&g.census_, sizeof(g.census_));
        for (int v : {g.frame_, g.lives_, g.deaths_, g.stagesCleared_, int(g.stage_), int(g.phase_),
                      int(g.nativePhase_), g.clearTicks_, g.introTicks_, g.backgroundAdvance_})
            add(&v, sizeof v);
        auto r = g.random_.state(), e = g.effects_.state();
        add(&r, sizeof r);
        add(&e, sizeof e);
        for (int sound : g.sounds_)
            add(&sound, sizeof sound);
        return h;
    }
    static void events(Game &g, int tick) {
        if (tick == 200) {
            auto &p = g.pool_.at(89);
            g.player_.transitionTicks = 1;
            g.player_.reverseTransition = false;
            p.drawing = 9;
            p.y = fixed16(340);
            p.vy = fixed16(3);
            for (auto &point : p.history) {
                point.x = p.x;
                point.y = p.y;
            }
        }
        if (tick == 1600) {
            g.phase_ = GamePhase::StageClear;
            g.clearTicks_ = 30;
            g.player_.bombActive = 0;
        }
        if (tick == 2200)
            g.gameOver();
    }
    static void tick(Game &g, int n) {
        events(g, n);
        g.step({80 + (n / 50) % 100, 250, (n / 6) % 100 < 3 ? 32u : 0u, n % 600 == 0 ? 32u : 0u});
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Usage: display_checks ASSETS");
        // Regression: dying vertical motion belongs to the original draw routine.
        // Active player bounds must not clamp its offscreen fall back to y=319.
        for (int steps : {3, 6}) {
            auto motion = std::make_unique<NativeMotion>(steps);
            Entity e{};
            PlayerState p{};
            e.x = fixed16(120);
            e.y = fixed16(420);
            p.transitionTicks = 20;
            for (int k = 0; k < steps; ++k) {
                motion->player(e, p, {239, 0, 0, 0});
                check(e.y == fixed16(420),
                      "Death fall was clamped back onto the screen between source ticks");
            }
        }
        constexpr int count = 2600;
        std::vector<uint64_t> expected;
        Assets baselineAssets;
        baselineAssets.load(argv[1]);
        auto baseline = std::make_unique<Game>(baselineAssets);
        baseline->setNativeRate(true, 6);
        baseline->start();
        for (int n = 0; n < count; ++n) {
            NativeRateTests::tick(*baseline, n);
            expected.push_back(NativeRateTests::hash(*baseline));
        }
        for (int hz : {30, 60, 120, 144, 200, 240, 0}) {
            Assets assets;
            assets.load(argv[1]);
            auto game = std::make_unique<Game>(assets);
            game->setNativeRate(true, 6);
            game->start();
            FixedCadence simulation(0, 5000000, 24);
            PresentationClock display(0);
            DecorativeCamera camera;
            int n = 0, draws = 0, fallSamples = 0;
            double largestFallCamera = 0;
            for (uint64_t now = 0; n < count; now += 1000000) {
                const int target = hz ? hz
                                      : (now < 3000000000   ? 30
                                         : now < 6000000000 ? 60
                                         : now < 9000000000 ? 120
                                                            : 200);
                display.rate(now, unsigned(target), 1);
                for (unsigned due = simulation.pending(now); due && n < count; --due, ++n) {
                    NativeRateTests::tick(*game, n);
                    check(NativeRateTests::hash(*game) == expected[size_t(n)],
                          "Presentation rate changed simulation state");
                    camera.capture(game->canvas(), game->assetRevision());
                    if (n >= 206 && n < 212) {
                        check(game->player().worldY > fixed16(319),
                              "Death camera snapped back to the active-player bound");
                        ++fallSamples;
                        largestFallCamera =
                            std::max(largestFallCamera, double(game->player().worldY));
                    }
                }
                if (display.pending(now)) {
                    auto before = NativeRateTests::hash(*game);
                    game->renderNative();
                    Canvas canvas = game->canvas();
                    camera.apply(canvas, simulation.fraction(now));
                    auto sampled = resampleDecoration(canvas, simulation.fraction(now) / 6);
                    // Exercise the exact original low-resolution renderer too,
                    // switching paths repeatedly during the same active run.
                    if (draws % 11 == 0) {
                        auto raster = std::make_unique<Raster565>();
                        raster->render(canvas, assets);
                    }
                    check(before == NativeRateTests::hash(*game),
                          "Rendering or graphics switching mutated gameplay");
                    ++draws;
                }
            }
            check(n == count && fallSamples == 6 && largestFallCamera > fixed16(319),
                  "Incomplete presentation isolation study");
            std::cout << (hz ? std::to_string(hz) : "30/60/120/200 live changes") << " Hz: " << n
                      << " identical state hashes, " << draws
                      << " presentations, offscreen death camera retained\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
