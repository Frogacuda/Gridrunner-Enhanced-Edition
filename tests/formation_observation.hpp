#pragma once
#include "grpp/formations.hpp"
#include <array>
#include <cstring>
#include <ostream>

namespace formation_check {
// Observation contract only. Both backends receive the same controlled inputs;
// expected behavior is emitted by original x86 execution, not by this schedule.
inline constexpr std::array<int, 36> sourceWords{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,  12,
                                                 13, 14, 15, 16, 18, 19, 20, 21, 22, 26, 27,  29,
                                                 30, 32, 33, 36, 37, 38, 40, 41, 42, 43, 556, 558};
inline uint64_t append(uint64_t hash, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i)
        hash = (hash ^ uint8_t(word >> (i * 8))) * 1099511628211ull;
    return hash;
}
inline std::array<uint32_t, 36> entityWords(const grpp::Entity &e) {
    uint32_t phaseBits;
    std::memcpy(&phaseBits, &e.colorPhase, 4);
    return {uint32_t(e.x),
            uint32_t(e.y),
            uint32_t(e.depth),
            uint32_t(e.vx),
            uint32_t(e.vy),
            uint32_t(e.speed),
            uint32_t(e.sprite),
            uint32_t(e.targetX),
            uint32_t(e.targetY),
            uint32_t(e.ax),
            uint32_t(e.ay),
            uint32_t(e.outsideTicks),
            uint32_t(e.next),
            uint32_t(e.previous),
            uint32_t(e.chainIndex),
            uint32_t(e.historySpacing),
            uint32_t(e.behavior),
            uint32_t(e.drawing),
            e.commandOrHealth,
            uint32_t(e.hitFlash),
            uint32_t(e.life),
            phaseBits,
            uint32_t(e.colorCycle),
            uint32_t(e.animation),
            uint32_t(e.animationBase),
            uint32_t(e.animationShift),
            uint32_t(e.animationFrames),
            uint32_t(e.attackParameters),
            uint32_t(e.mode),
            uint32_t(e.phase),
            uint32_t(e.heading),
            uint32_t(e.turnRate),
            e.flags,
            e.historyCursor,
            uint32_t(e.scale),
            uint32_t(e.effectMode)};
}
inline uint64_t poolDigest(const grpp::EntityPool &pool) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < grpp::EntityPool::Capacity; ++i) {
        const auto &e = pool.at(i);
        for (uint32_t word : entityWords(e))
            hash = append(hash, word);
        for (const auto &p : e.history)
            for (uint32_t word : {uint32_t(p.x), uint32_t(p.y), uint32_t(p.scale), uint32_t(p.tag),
                                  uint32_t(p.heading), uint32_t(p.vx), uint32_t(p.vy), p.color})
                hash = append(hash, word);
    }
    return hash;
}
struct Observation {
    uint64_t pool = 0, tuning = 0;
    std::array<uint32_t, 11> globals{};
    bool operator==(const Observation &b) const {
        return pool == b.pool && tuning == b.tuning && globals == b.globals;
    }
};
inline std::ostream &operator<<(std::ostream &out, const Observation &o) {
    out << std::hex << o.pool << ' ' << o.tuning;
    for (uint32_t word : o.globals)
        out << ' ' << word;
    return out;
}
template <class Backend, class Emit> void schedule(Backend &backend, Emit emit) {
    for (int level = 0; level < 50; ++level) {
        backend.begin(level, level / 20);
        emit("level", level, -1, backend.observe());
        backend.seed();
        emit("seed", level, -1, backend.observe());
        for (int tick = 0; tick < 140; ++tick) {
            // This is a controlled creation-family environment, not a campaign:
            // retire created enemies externally and supply clock/census inputs.
            // Fields other than behavior survive retirement, exercising reuse.
            if (tick % 20 == 0)
                backend.retire();
            backend.tick(tick);
            if (tick % 10 == 0 || tick == 139)
                emit("tick", level, tick, backend.observe());
        }
    }
    for (int scenario = 0; scenario < 28; ++scenario) {
        backend.matrix(scenario);
        emit("factory", scenario, -1, backend.observe());
    }
    for (int skip : {-2, 0, 1, 5, 22, 23, 10000}) {
        backend.begin(0, 0, skip);
        emit("resume", skip, -1, backend.observe());
        backend.seed();
        emit("resume-seed", skip, -1, backend.observe());
    }
}
} // namespace formation_check
