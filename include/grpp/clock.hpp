#pragma once
#include <algorithm>
#include <cstdint>
namespace grpp {
// Simulation deadlines advance from their previous deadline, not the display
// frame which happened to service them. Long stalls/focus changes pause time.
class FixedCadence {
  public:
    static constexpr uint64_t Period = 30'000'000;
    explicit FixedCadence(uint64_t now = 0, uint64_t period = Period, unsigned limit = 4)
        : next_(now), period_(period), limit_(limit) {}
    void reset(uint64_t now) {
        next_ = now;
        hasTick_ = false;
    }
    double fraction(uint64_t now) const {
        return !hasTick_ || now <= last_ ? 0. : std::min(1., double(now - last_) / double(period_));
    }
    unsigned pending(uint64_t now) {
        if (now < next_)
            return 0;
        const uint64_t elapsed = (now - next_) / period_ + 1;
        const unsigned steps = unsigned(std::min<uint64_t>(elapsed, limit_));
        last_ = elapsed > limit_ ? now : next_ + uint64_t(steps - 1) * period_;
        hasTick_ = true;
        if (elapsed > limit_)
            next_ = now + period_;
        else
            next_ += uint64_t(steps) * period_;
        return steps;
    }

  private:
    unsigned limit_;
    uint64_t next_;
    uint64_t period_;
    uint64_t last_ = 0;
    bool hasTick_ = false;
};
} // namespace grpp
