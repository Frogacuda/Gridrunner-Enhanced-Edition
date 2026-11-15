#pragma once
#include "formation_observation.hpp"
#include "grpp/combat.hpp"

namespace combat_check {
inline std::array<uint32_t, 22> extraWords(const grpp::Entity &e) {
    const auto &t = e.transform;
    return {uint32_t(e.depthAcceleration),
            uint32_t(e.extent),
            uint32_t(e.companionHealth),
            uint32_t(e.companionMaximumHealth),
            uint32_t(e.trailProfile),
            t.noiseSeed,
            uint32_t(t.jitterX),
            uint32_t(t.jitterY),
            t.flags,
            uint32_t(t.axisXX),
            uint32_t(t.axisXY),
            uint32_t(t.axisYX),
            uint32_t(t.axisYY),
            uint32_t(t.pivotX),
            uint32_t(t.pivotY),
            uint32_t(t.sheet),
            uint32_t(t.x),
            uint32_t(t.y),
            uint32_t(t.width),
            uint32_t(t.height),
            uint32_t(t.tileWidth),
            uint32_t(t.tileHeight)};
}
inline uint64_t poolDigest(const grpp::EntityPool &pool) {
    auto h = formation_check::poolDigest(pool);
    for (size_t slot = 0; slot < grpp::EntityPool::Capacity; ++slot)
        for (auto w : extraWords(pool.at(slot)))
            h = formation_check::append(h, w);
    return h;
}
struct Observation {
    uint64_t pool = 0, grid = 0, sounds = 0;
    std::array<uint32_t, 23> state{};
    bool operator==(const Observation &b) const {
        return pool == b.pool && grid == b.grid && sounds == b.sounds && state == b.state;
    }
};
inline std::ostream &operator<<(std::ostream &out, const Observation &o) {
    out << std::hex << o.pool << ' ' << o.grid << ' ' << o.sounds;
    for (auto w : o.state)
        out << ' ' << w;
    return out;
}
// A common input schedule, never an implementation of expected gameplay.
// Test-only word writes identify original controlled inputs; production code
// uses named entities/state and never interprets these field indices.
template <class B, class Emit> void schedule(B &b, Emit emit) {
    int scenario = 0;
    for (int target : {78, 70, 66, 65})
        for (int mode : {-1, 0, 1, 2, 3})
            for (int heading : {512, 513})
                for (int reverse : {0, 2}) {
                    b.begin(mode);
                    b.word(78, 37, mode == 1 ? 4 : 0);
                    for (int slot : {78, 70, 69, 68, 67, 66, 65}) {
                        b.word(slot, 40, heading);
                        b.word(slot, 42, 1 | reverse);
                    }
                    b.split(target);
                    emit("split", scenario, -1, b.observe());
                    for (int tick = 0; tick < 8; ++tick) {
                        b.tick(tick);
                        emit("split-tick", scenario, tick, b.observe());
                    }
                    ++scenario;
                }
    scenario = 0;
    for (int target : {78, 70, 66, 65})
        for (int mode : {-1, 0, 1, 2, 3})
            for (int health : {-1, 0, 1}) {
                b.begin(mode);
                b.word(target, 20, health);
                b.selectBeam(target);
                emit("beam-input", scenario, -1, b.observe());
                for (int tick = 0; tick < 4; ++tick) {
                    b.newBeam();
                    b.tick(tick);
                    emit("beam-tick", scenario, tick, b.observe());
                }
                ++scenario;
            }
    for (int condition = 0; condition < 4; ++condition) {
        b.begin(-1);
        b.selectBeam(78);
        b.beamGate(condition);
        b.tick(0);
        emit("beam-gate", condition, 0, b.observe());
    }
    for (int spacing : {2, 3, 4, 8}) {
        b.begin(0);
        b.word(70, 16, spacing);
        b.selectBeam(70);
        for (int tick = 0; tick < 3; ++tick) {
            b.tick(tick);
            emit("font-alias", spacing, tick, b.observe());
        }
    }
    scenario = 0;
    for (int countdown : {1, 0, -1})
        for (int pending : {0, 3, -2})
            for (int full : {0, 1}) {
                b.begin(-1);
                b.dropInput(countdown, pending, full);
                b.drop();
                emit("drop", scenario++, -1, b.observe());
            }
    scenario = 0;
    for (int kind : {3, 4, 7, 16, 17})
        for (int distance : {0, 15, 16}) {
            b.begin(-1);
            b.contactInput(kind, distance);
            b.tick(0);
            emit("contact", scenario, 0, b.observe());
            for (int tick = 1; tick < 5; ++tick) {
                b.tick(tick);
                emit("contact-tick", scenario, tick, b.observe());
            }
            ++scenario;
        }
    // Lifetimes and partial/stale initialization; real conversion helpers,
    // followed by the source's corresponding effect execution cases.
    for (int which = 0; which < 6; ++which) {
        b.begin(-1);
        b.effect(which);
        emit("effect", which, -1, b.observe());
        for (int tick = 0; tick < 104; ++tick) {
            b.tick(tick);
            emit("effect-tick", which, tick, b.observe());
        }
    }
    scenario = 0;
    for (int kind : {3, 4, 7, 13, 16, 17})
        for (int power : {1, 3})
            for (int health : {-1, 0}) {
                b.begin(-1);
                b.shotInput(kind, 7, power, health, 0);
                for (int tick = 0; tick < 4; ++tick) {
                    b.tick(tick);
                    emit("shot-contact", scenario, tick, b.observe());
                }
                ++scenario;
            }
    scenario = 0;
    for (int sprite : {7, 107, 109})
        for (int power : {1, 3})
            for (int pod : {1, 2, 3}) {
                b.begin(-1);
                b.shotInput(0, sprite, power, -1, pod);
                for (int tick = 0; tick < 6; ++tick) {
                    b.tick(tick);
                    emit("shot-grid", scenario, tick, b.observe());
                }
                ++scenario;
            }
    b.begin(-1);
    b.firing();
    for (int tick = 0; tick < 80; ++tick) {
        b.tick(tick);
        emit("firing", 0, tick, b.observe());
    }
    scenario = 0;
    for (int power : {1, 8, 9})
        for (int transition : {-20, -1, 0, 1, 2})
            for (int restore : {0, 1})
                for (int flags : {0, 128}) {
                    b.begin(-1);
                    b.pickupInput(power, transition, restore, flags);
                    for (int tick = 0; tick < 8; ++tick) {
                        b.tick(tick);
                        emit("pickup", scenario, tick, b.observe());
                    }
                    ++scenario;
                }
    for (int which = 0; which < 10; ++which) {
        b.begin(-1);
        b.companionInput(which);
        for (int tick = 0; tick < 48; ++tick) {
            b.tick(tick);
            emit("companion", which, tick, b.observe());
        }
    }
}
} // namespace combat_check
