#pragma once
#include "grpp/mechanisms.hpp"
#include <stdexcept>

namespace grpp {
// Integrates current, source-selected velocities; never evaluates a future frame.
// Forces/decisions remain on the recovered 30 ms clock. Three or six actual moves,
// with integer remainders, conserve their full source displacement exactly.
class NativeMotion {
    friend struct ReplayState;
  public:
    static constexpr int Substeps = 3;
    explicit NativeMotion(int steps = Substeps) : steps_(steps) {
        if (steps != 3 && steps != 6)
            throw std::invalid_argument("Native motion requires three or six substeps");
    }
    int substeps() const { return steps_; }
    void reset();
    void invalidate(size_t slot) {
        tracks_.at(slot) = Track{};
        ++generations_.at(slot);
    }
    uint64_t generation(size_t slot) const { return generations_.at(slot); }
    void beginInterval();
    void move(EntityPool &, Entity &, int32_t dx, int32_t dy, int32_t dz = 0);
    void continueMove(EntityPool &, size_t slot);
    void record(EntityPool &, size_t slot);
    bool follow(EntityPool &, Entity &);
    void player(Entity &, const PlayerState &, const LogicalInput &);
    void scheduleParticles(EntityPool &, Entity &, uint32_t first, int count, int32_t x, int32_t y);

  private:
    int steps_;
    struct Axis {
        int64_t remainder = 0;
        int32_t expected = 0;
        bool initialized = false;
    };
    struct Track {
        std::array<int64_t, 3> velocity{};
        std::array<Axis, 3> axes{};
        int behavior = 0, drawing = 0;
        bool moving = false;
        std::array<TrailPoint, 384> history{};
        uint64_t cursor = 0;
        int historyBehavior = 0;
        std::array<TrailPoint, 64> particles{};
        uint32_t particleFirst = 0;
        int particleCount = 0, particlePhase = 0;
        int32_t particleX = 0, particleY = 0, particleDx = 0, particleDy = 0;
    };
    void integrate(int32_t &, int64_t, Axis &);
    void emitParticles(Entity &, Track &);
    std::array<Track, EntityPool::Capacity> tracks_{};
    std::array<uint64_t, EntityPool::Capacity> generations_{};
    double playerX_ = 0, playerY_ = 0;
    int32_t playerExpectedX_ = 0, playerExpectedY_ = 0;
    bool playerInitialized_ = false;
};
} // namespace grpp
