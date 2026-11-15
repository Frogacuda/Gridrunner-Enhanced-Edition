#include "grpp/native_motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace grpp {
void EntityPool::move(Entity &e, int32_t dx, int32_t dy, int32_t dz) {
    if (nativeMotion)
        nativeMotion->move(*this, e, dx, dy, dz);
    else {
        e.x = add32(e.x, dx);
        e.y = add32(e.y, dy);
        e.depth = add32(e.depth, dz);
    }
}
void NativeMotion::reset() {
    for (size_t i = 0; i < tracks_.size(); ++i)
        invalidate(i);
    playerInitialized_ = false;
}
void NativeMotion::beginInterval() {
    for (auto &t : tracks_) {
        t.velocity = {};
        t.moving = false;
        t.particleCount = 0;
    }
}
void NativeMotion::integrate(int32_t &value, int64_t delta, Axis &a) {
    if (!a.initialized || value != a.expected) {
        a.remainder = 0;
        a.initialized = true;
    }
    const int64_t total = int64_t(delta) + a.remainder;
    value = add32(value, signedBits(uint32_t(total / steps_)));
    a.remainder = total % steps_;
    a.expected = value;
}
void NativeMotion::move(EntityPool &p, Entity &e, int32_t dx, int32_t dy, int32_t dz) {
    if (!e.behavior) {
        e.x = add32(e.x, dx);
        e.y = add32(e.y, dy);
        e.depth = add32(e.depth, dz);
        return;
    }
    auto &t = tracks_.at(size_t(&e - &p.at(0)));
    if (t.behavior != e.behavior || t.drawing != e.drawing) {
        t.axes = {};
        t.velocity = {};
    }
    t.behavior = e.behavior;
    t.drawing = e.drawing;
    t.moving = true;
    const std::array<int32_t, 3> delta{dx, dy, dz};
    for (size_t i = 0; i < 3; ++i)
        t.velocity[i] += delta[i];
    integrate(e.x, dx, t.axes[0]);
    integrate(e.y, dy, t.axes[1]);
    integrate(e.depth, dz, t.axes[2]);
}
void NativeMotion::continueMove(EntityPool &p, size_t slot) {
    auto &t = tracks_.at(slot);
    auto &e = p.at(slot);
    if (!e.behavior || !t.moving || e.behavior != t.behavior || e.drawing != t.drawing)
        return;
    integrate(e.x, t.velocity[0], t.axes[0]);
    integrate(e.y, t.velocity[1], t.axes[1]);
    integrate(e.depth, t.velocity[2], t.axes[2]);
    if (t.particleCount && t.particlePhase < steps_ - 1) {
        ++t.particlePhase;
        emitParticles(e, t);
    }
}
void NativeMotion::emitParticles(Entity &e, Track &t) {
    const int begin = t.particleCount * t.particlePhase / steps_,
              end = t.particleCount * (t.particlePhase + 1) / steps_;
    for (int i = begin; i < end; ++i) {
        auto point = t.particles[size_t(i)];
        point.x = add32(t.particleX, mul32(i, t.particleDx));
        point.y = add32(t.particleY, mul32(i, t.particleDy));
        e.history[(t.particleFirst + uint32_t(i)) & 63u] = point;
    }
}
void NativeMotion::scheduleParticles(EntityPool &p, Entity &e, uint32_t first, int count, int32_t x,
                                     int32_t y) {
    if (count < 0 || count > 64)
        throw std::runtime_error("Native shot particle batch outside source history capacity");
    auto &t = tracks_.at(size_t(&e - &p.at(0)));
    t.particleFirst = first;
    t.particleCount = count;
    t.particlePhase = 0;
    t.particleX = x;
    t.particleY = y;
    t.particleDx = e.vx;
    t.particleDy = e.vy;
    for (int i = 0; i < count; ++i) {
        auto &point = e.history[(first + uint32_t(i)) & 63u];
        t.particles[size_t(i)] = point;
        point.tag = -1;
    }
    emitParticles(e, t);
}
void NativeMotion::record(EntityPool &p, size_t slot) {
    auto &e = p.at(slot);
    auto &t = tracks_.at(slot);
    if (t.historyBehavior != e.behavior) {
        t.cursor = 0;
        t.historyBehavior = e.behavior;
    }
    t.history[size_t(t.cursor++ % t.history.size())] = {e.x,       e.y,  e.depth, 1,
                                                        e.heading, e.vx, e.vy,    0};
}
bool NativeMotion::follow(EntityPool &p, Entity &e) {
    if (e.previous < 0 || e.previous >= int(EntityPool::Capacity))
        return false;
    auto &t = tracks_.at(size_t(e.previous));
    const auto &previous = p.at(size_t(e.previous));
    const auto slot = size_t(&e - &p.at(0));
    // Ascending slot order is observable: a later predecessor has not yet
    // written this step. Preserve its extra source-interval delay.
    const uint64_t lag = uint64_t(std::max(0, e.historySpacing - 1)) * steps_ +
                         (size_t(e.previous) > slot ? steps_ - 1 : 0);
    if (t.historyBehavior != previous.behavior || t.cursor <= lag)
        return false;
    const auto &point = t.history[size_t((t.cursor - 1 - lag) % t.history.size())];
    e.x = point.x;
    e.y = point.y;
    e.depth = point.scale;
    e.heading = point.heading;
    e.vx = point.vx;
    e.vy = point.vy;
    return true;
}
void NativeMotion::player(Entity &e, const PlayerState &s, const LogicalInput &input) {
    if (!playerInitialized_ || e.x != playerExpectedX_ || e.y != playerExpectedY_) {
        playerX_ = e.x;
        playerY_ = e.y;
        playerInitialized_ = true;
    }
    const double gain = 1 - std::pow(7.0 / 8.0, 1.0 / steps_);
    playerX_ += (double(fixed16(input.x)) - playerX_) * gain;
    if (s.transitionTicks < 1)
        playerY_ += (double(fixed16(input.y)) - playerY_) * gain;
    auto clamp = [](double v, int last) {
        if (v < 0)
            return 0.0;
        if (v >= double(fixed16(last + 1)))
            return double(fixed16(last));
        return v;
    };
    playerX_ = clamp(playerX_, s.width() - 1);
    // The source death draw owns vertical fall, including offscreen positions.
    if (s.transitionTicks < 1)
        playerY_ = clamp(playerY_, s.height() - 1);
    e.x = int32_t(playerX_);
    if (s.transitionTicks < 1)
        e.y = int32_t(playerY_);
    e.targetX = fixed16(input.x);
    if (s.transitionTicks < 1)
        e.targetY = fixed16(input.y);
    playerExpectedX_ = e.x;
    playerExpectedY_ = e.y;
}
} // namespace grpp
