#include "grpp/game.hpp"
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static void isolate(Game &g) {
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            g.pool_.at(i) = {};
        auto &p = g.pool_.at(EntityPool::Player);
        p.behavior = 1;
        p.x = fixed16(120);
        p.y = fixed16(240);
        p.flags = 1;
        g.player_.transitionTicks = 0;
        g.player_.worldX = p.x;
        g.player_.worldY = p.y;
        g.player_.shotCountdown = 10000;
        g.grid_ = {};
        g.worldState_ = {};
        g.phase_ = GamePhase::Playing;
        g.beginPending_ = false;
        g.native_->reset();
        g.nativePhase_ = 0;
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        int phases = 0;
        for (int steps : {3, 6})
            for (int phase = 0; phase < steps; ++phase) {
                auto game = std::make_unique<Game>(assets);
                game->setNativeRate(true, steps);
                game->deferPresentation(true);
                game->start();
                NativeRateTests::isolate(*game);
                InputLatch latch;
                for (int n = 0; n < phase; ++n)
                    game->step(latch.sample(120, 240, 0));
                latch.press(0x20); // up already drained before sample
                for (int n = 0; n <= steps; ++n)
                    game->step(latch.sample(120, 240, 0));
                check(game->player().bombActive,
                      "Short bomb tap did not survive until source tick");
                ++phases;
            }
        InputLatch latch;
        latch.press(0x40);
        auto pulse = latch.sample(120, 240, 0);
        check(pulse.pressed == 0x40 && pulse.held == 0x40, "Alternate pulse lost");
        check(latch.sample(120, 240, 0).held == 0, "Pulse repeated");
        latch.press(0x20);
        check(latch.sample(120, 240, 0x20).pressed == 0x20, "Initial held edge lost");
        check(latch.sample(120, 240, 0x20).pressed == 0, "Held action duplicated");
        latch.press(0x20);
        latch.reset();
        check(latch.sample(120, 240, 0).held == 0, "Pause/focus cancellation retained action");
        std::cout << phases
                  << " native source phases retain short actions; "
                     "held/pulse/alternate/cancellation checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
