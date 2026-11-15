#include "grpp/background.hpp"
#include "grpp/clock.hpp"
#include "grpp/game.hpp"
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
namespace grpp {
struct NativeRateTests {
    static void falling(Game &g) {
        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot)
            g.pool_.at(slot).behavior = 0;
        auto &p = g.pool_.at(89);
        p.behavior = 1;
        p.drawing = 0;
        p.flags = 1;
        p.x = fixed16(120);
        p.y = fixed16(240);
        p.vy = 0;
        for (auto &point : p.history) {
            point.x = p.x;
            point.y = p.y;
        }
        g.player_.transitionTicks = 0;
        g.player_.reverseTransition = false;
        g.player_.worldX = p.x;
        g.player_.worldY = p.y;
        g.player_.shotCountdown = 100000;
        g.native_->reset();
        g.nativePhase_ = 0;
        g.grid_ = {};
        g.worldState_ = {};
        g.phase_ = GamePhase::Playing;
        g.beginPending_ = false;
        g.combat_.hazardHit(0);
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        if (argc < 3)
            throw std::runtime_error(
                "Usage: death_presentation_checks ASSETS CSV [--expect-old-jitter]");
        const bool old = argc == 4;
        std::ofstream out(argv[2]);
        if (!out)
            throw std::runtime_error("Cannot write death camera trace");
        out << "cap,presentation,time_ns,motion_tick,source_tick,phase,camera_y,display_camera_y,"
               "fall_y\n";
        for (int hz : {30, 60, 120, 144, 200, 240}) {
            Assets assets;
            assets.load(argv[1]);
            auto game = std::make_unique<Game>(assets);
            game->setNativeRate(true, 6);
            game->start();
            NativeRateTests::falling(*game);
            FixedCadence simulation(0, 5000000, 24);
            DecorativeCamera camera;
            int n = 0, reversals = 0, pulses = 0;
            double previous = 240 * 65536., peakReversal = 0;
            for (int frame = 0; frame < hz * 2; ++frame) {
                const uint64_t now = uint64_t(frame) * 1000000000 / unsigned(hz);
                for (unsigned due = simulation.pending(now); due; --due) {
                    game->step({120, 240, 0, 0});
                    camera.capture(game->canvas(), game->assetRevision());
                    ++n;
                }
                Canvas display = game->canvas();
                camera.apply(display, simulation.fraction(now));
                const double y = display.decorativeGrid().worldY;
                if (y < previous - 1) {
                    ++reversals;
                    peakReversal = std::max(peakReversal, (previous - y) / 65536.);
                }
                if (game->player().worldY == fixed16(319))
                    ++pulses;
                out << hz << ',' << frame << ',' << now << ',' << n << ',' << game->frame() << ','
                    << game->nativePhase() << ',' << game->player().worldY / 65536. << ','
                    << y / 65536. << ',' << game->entities().at(89).y / 65536. << '\n';
                previous = y;
            }
            std::cout << hz << " FPS: backward camera jumps=" << reversals
                      << ", largest=" << peakReversal
                      << " logical pixels; samples clamped to bottom=" << pulses << '\n';
            if (!old && (reversals || previous <= fixed16(319)))
                throw std::runtime_error("Presentation camera still jitters during fall");
            if (old && hz <= 60 && reversals == 0)
                throw std::runtime_error(
                    "Unsmoothed low-refresh camera did not expose the expected jitter");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
