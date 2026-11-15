// Development-only exploration through ordinary inputs. No entity writes,
// forced retirement, invulnerability or completion overrides.
#include "grpp/game.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: campaign_audit ASSETS REPORT [STAGE_ONE_INPUT]\n";
        return 1;
    }
    std::ofstream report(argv[2]);
    if (!report)
        return 1;
    report << "stage,frames,result,deaths,score,emitted\n";
    int errors = 0;
    for (unsigned stage = 0; stage < 50; ++stage) {
        try {
            Assets assets;
            assets.load(argv[1]);
            auto owner = std::make_unique<Game>(assets);
            auto &game = *owner;
            game.start(stage);
            InputLatch latch;
            int tick = 0;
            std::ofstream replay;
            if (stage == 0 && argc == 4)
                replay.open(argv[3]);
            for (; tick < 12000 && game.phase() != GamePhase::GameOver && !game.stagesCleared();
                 ++tick) {
                int x = 120, best = 100000;
                for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
                    const auto &e = game.entities().at(slot);
                    if ((e.behavior == 3 || e.behavior == 4 || e.behavior == 16 ||
                         e.behavior == 17) &&
                        (e.flags & 1) && pixel(e.y) < best) {
                        best = pixel(e.y);
                        x = std::clamp(pixel(e.x) / 2, 20, 220);
                    }
                }
                if (replay)
                    replay << x << " 270 0\n";
                game.step(latch.sample(x, 270, 0));
            }
            const char *result = game.stagesCleared()                  ? "clear"
                                 : game.phase() == GamePhase::GameOver ? "game-over"
                                                                       : "timeout";
            report << stage + 1 << ',' << tick << ',' << result << ',' << game.deaths() << ','
                   << game.combatState().score << ',' << game.formations().emitted() << '\n';
            report.flush();
            std::cout << "Stage " << stage + 1 << ": " << result << " after " << tick << " frames\n"
                      << std::flush;
        } catch (const std::exception &e) {
            ++errors;
            report << stage + 1 << ",0,error,0,0,0 # " << e.what() << '\n';
            report.flush();
            std::cerr << "Stage " << stage + 1 << ": " << e.what() << '\n';
        }
    }
    return errors ? 2 : 0;
}
