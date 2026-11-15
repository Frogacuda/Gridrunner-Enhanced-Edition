// Ordinary-input deterministic smoke sweep. No gameplay overrides.
#include "grpp/game.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
int main(int argc, char **argv) {
    try {
        if (argc != 3 && argc != 6)
            throw std::runtime_error(
                "Usage: native_campaign_audit ASSETS REPORT [standard|aof 1|3|6 SOURCE_TICKS]");
        const bool aof = argc == 6 && std::string(argv[3]) == "aof";
        const int steps = argc == 6 ? std::stoi(argv[4]) : 3;
        const int ticks = argc == 6 ? std::stoi(argv[5]) : 2000;
        if ((steps != 1 && steps != 3 && steps != 6) || ticks < 1)
            throw std::runtime_error("Invalid audit timing");
        std::ofstream report(argv[2]);
        if (!report)
            throw std::runtime_error("Cannot write audit");
        report << "edition,substeps,stage,motion_steps,source_ticks,result,deaths,score,emitted,"
                  "behavior_mask,head_mode_mask,deterministic\n";
        for (unsigned stage = 0; stage < 50; ++stage) {
            Assets a, b;
            a.load(argv[1]);
            b.load(argv[1]);
            auto g = std::make_unique<Game>(a), repeat = std::make_unique<Game>(b);
            g->arse(aof);
            repeat->arse(aof);
            g->endurance(aof);
            repeat->endurance(aof);
            g->setNativeRate(steps != 1, steps == 1 ? 3 : steps);
            repeat->setNativeRate(steps != 1, steps == 1 ? 3 : steps);
            g->start(stage);
            repeat->start(stage);
            int step = 0;
            InputLatch latch;
            uint32_t behaviors = 0, modes = 0;
            for (; step < ticks * steps && g->phase() != GamePhase::GameOver && !g->stagesCleared();
                 ++step) {
                int x = g->player().width() / 2, best = 100000;
                for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
                    const auto &e = g->entities().at(slot);
                    if (e.behavior < 0 || e.behavior > 19)
                        throw std::runtime_error("Invalid entity behavior");
                    behaviors |= 1u << e.behavior;
                    if (e.behavior == 3)
                        modes |= 1u << (uint32_t(e.mode) & 15u);
                    if ((e.behavior == 3 || e.behavior == 4 || e.behavior == 16 ||
                         e.behavior == 17) &&
                        (e.flags & 1) && pixel(e.y) < best) {
                        best = pixel(e.y);
                        x = std::clamp(pixel(e.x) / 2, 20, g->player().width() - 20);
                    }
                }
                auto input = latch.sample(x, g->player().height() - 50,
                                          (step / steps) % 500 < 10 ? 32u : 0u);
                g->step(input);
                repeat->step(input);
                if (step % 61 == 0)
                    repeat->renderNative();
                if (g->randomState() != repeat->randomState() ||
                    g->combatState().score != repeat->combatState().score ||
                    g->frame() != repeat->frame() || g->phase() != repeat->phase() ||
                    g->deaths() != repeat->deaths())
                    throw std::runtime_error("Native repeat diverged at stage " +
                                             std::to_string(stage + 1));
                for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
                    const auto &first = g->entities().at(slot);
                    const auto &second = repeat->entities().at(slot);
                    if (std::memcmp(&first, &second, sizeof(Entity)))
                        throw std::runtime_error("Native entity repeat diverged at stage " +
                                                 std::to_string(stage + 1));
                }
                const auto &p = g->entities().at(EntityPool::Player);
                if (p.behavior == 1 && g->player().transitionTicks == 0 &&
                    (p.x < 0 || pixel(p.x) >= g->player().width() || p.y < 0 ||
                     pixel(p.y) >= g->player().height()))
                    throw std::runtime_error("Player bounds violated");
            }
            const char *result = g->stagesCleared()                  ? "clear"
                                 : g->phase() == GamePhase::GameOver ? "game-over"
                                                                     : "bounded-timeout";
            report << (aof ? "aof" : "standard") << ',' << steps << ',' << stage + 1 << ',' << step
                   << ',' << g->frame() << ',' << result << ',' << g->deaths() << ','
                   << g->combatState().score << ',' << g->formations().emitted() << ',' << behaviors
                   << ',' << modes << ",pass\n";
            report.flush();
            std::cout << "Stage " << stage + 1 << ": " << result << " (" << step
                      << " native steps), deterministic\n"
                      << std::flush;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
