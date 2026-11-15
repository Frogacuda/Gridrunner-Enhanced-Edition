#pragma once
#include "formation_observation.hpp"
#include "grpp/chains.hpp"

namespace chain_check {
struct Input {
    int x = 120, y = 160, heading = 0, mode = 0, flags = 1;
    int targetHeading = 0, turnRate = 64, countdown = -1;
    int playerX = 120, playerY = 240, pod = 0, turnChance = 0, worldX = 0, worldY = 0;
};
struct Observation {
    uint64_t pool;
    std::array<uint32_t, 5> state;
    bool operator==(const Observation &b) const { return pool == b.pool && state == b.state; }
};
inline std::ostream &operator<<(std::ostream &out, const Observation &o) {
    out << std::hex << o.pool;
    for (auto word : o.state)
        out << ' ' << word;
    return out;
}
// Inputs only. Expected values come from the original full dispatcher under
// its real invulnerable-player state, no bomb, and no selected beam target.
template <class Backend, class Emit> void schedule(Backend &b, Emit emit) {
    for (int scenario = 0; scenario < 20; ++scenario) {
        b.construct(scenario);
        emit("construct", scenario, -1, b.observe());
        for (int tick = 0; tick < 130; ++tick) {
            b.tick(tick);
            emit("motion", scenario, tick, b.observe());
        }
    }
    // Focused source branches. Position pairs include exact and one-pixel
    // outside bounds. Head speeds are zero so steering thresholds stay exact.
    int scenario = 0;
    const auto run = [&](Input input) {
        b.focus(input);
        for (int tick = 0; tick < 12; ++tick) {
            b.tick(tick);
            emit("steer", scenario, tick, b.observe());
        }
        ++scenario;
    };
    for (int reverse : {0, 2}) {
        run({15, 160, 768, 0, 1 | reverse});
        run({16, 160, 768, 0, 1 | reverse});
        run({225, 160, 256, 0, 1 | reverse});
        run({224, 160, 256, 0, 1 | reverse});
        run({120, 160, 256, 0, 1 | reverse, 0, 64, -1, 120, 160, 1});
        run({120, 160, 768, 0, 1 | reverse, 0, 64, -1, 120, 160, 1});
        run({120, 241, 256, 0, 1 | reverse});
        run({120, 239, 768, 0, 1 | reverse});
        run({120, 15, 512, 5, 1 | reverse});
        run({120, 16, 512, 5, 1 | reverse});
        run({120, 305, 0, 5, 1 | reverse});
        run({120, 304, 0, 5, 1 | reverse});
        run({120, 160, 0, 5, 1 | reverse, 0, 64, -1, 120, 240, 1});
        run({120, 160, 512, 5, 1 | reverse, 0, 64, -1, 120, 240, 1});
        run({119, 160, 0, 5, 1 | reverse});
        run({121, 160, 512, 5, 1 | reverse});
    }
    for (int heading : {128, 384, 640, 896})
        for (int corner : {0, 1})
            run({corner ? 225 : 15, corner ? 305 : 15, heading, 3});
    for (int heading : {0, 128, 512, 896})
        for (int side : {0, 1})
            run({120, 160, heading, 4, 1, 0, 16, -1, side ? 119 : 121, side ? 159 : 161});
    for (int rate : {-64, 64})
        for (int difference : {-65, -64, -63, 0, 63, 64, 65})
            run({120, 160, 256, 0x50001, 1, 256 + difference, rate});
    for (int heading : {0, 512})
        for (int side : {0, 1})
            run({120, 160, heading, 2, 1, 0, 64, side ? 159 * 65536 : 161 * 65536,
                 side ? 119 : 121});
    run({120, 160, 256, 0, 1, 0, 64, -1, 120, 240, 0, 512});
    run({120, 160, 128, 3, 1, 0, 64, -1, 120, 240, 0, 512});
    run({120, 160, 0, 2, 1, 0, 64, 159 * 65536, 100, 240, 0, 0, 80, 0});
    for (int attack = 0; attack < 20; ++attack) {
        b.attack(attack);
        emit("attack", attack, -1, b.observe());
    }
    // Offscreen chain retirement occurs on old counter 250 -> 251 and still
    // performs head movement/history in that same source dispatch.
    b.retirement();
    for (int tick = 0; tick < 3; ++tick) {
        b.tick(tick);
        emit("retire", 0, tick, b.observe());
    }
    b.lifecycle();
    emit("lifecycle-seed", 0, -1, b.observe());
    for (int tick = 0; tick < 240; ++tick) {
        b.integratedTick(tick);
        emit("lifecycle", 0, tick, b.observe());
    }
}
} // namespace chain_check
