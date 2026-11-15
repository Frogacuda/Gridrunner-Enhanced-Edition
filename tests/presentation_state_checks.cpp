#include "grpp/game.hpp"
#include <cstring>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        unsigned states = 0;
        for (bool aof : {false, true})
            for (unsigned stage : {0u, 24u, 49u})
                for (int steps : {3, 6}) {
                    auto old = std::make_unique<Game>(assets),
                         lean = std::make_unique<Game>(assets);
                    for (auto *game : {old.get(), lean.get()}) {
                        game->arse(aof);
                        game->stageBackgrounds(true);
                        game->setNativeRate(true, steps);
                        game->deferPresentation(true);
                        game->start(stage);
                    }
                    for (int tick = 0; tick < steps * 500; ++tick) {
                        const int source = tick / steps;
                        LogicalInput input{source % 80 < 40 ? 40 : (aof ? 350 : 200),
                                           aof ? 350 : 270, source % 127 < 2 ? 32u : 0u, 0};
                        old->step(input);
                        lean->step(input);
                        if (tick % 3 == 0) {
                            old->renderNative();
                            const bool edge = (tick / 31) % 2, reference = (tick / 37) % 2;
                            if (edge) {
                                auto discarded = old->expandedCanvas();
                                (void)discarded;
                            }
                            lean->renderNative(reference, !edge);
                            if (edge) {
                                auto discarded = lean->expandedCanvas();
                                (void)discarded;
                            }
                        }
                        check(old->frame() == lean->frame() &&
                                  old->randomState() == lean->randomState() &&
                                  old->phase() == lean->phase() && old->stage() == lean->stage() &&
                                  old->lives() == lean->lives() &&
                                  old->combatState().score == lean->combatState().score,
                              "Lean presentation changed future gameplay");
                        for (size_t n = 0; n < EntityPool::Capacity; ++n)
                            check(std::memcmp(&old->entities().at(n), &lean->entities().at(n),
                                              sizeof(Entity)) == 0,
                                  "Lean presentation changed future entity/history state");
                        ++states;
                    }
                }
        std::cout << states
                  << " old/lean future simulation states identical across live graphics/View "
                     "switching PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
