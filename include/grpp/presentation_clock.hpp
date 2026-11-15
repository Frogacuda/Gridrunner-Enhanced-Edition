#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace grpp {
// Presentation only. Never changes the simulation deadline or source phase.
class PresentationClock {
  public:
    explicit PresentationClock(uint64_t now = 0) : next_(now) {}
    bool rate(uint64_t now, uint32_t numerator, uint32_t denominator) {
        const double hz = denominator ? double(numerator) / denominator : 0;
        if (hz < 10 || hz > 1000 || denominator > 1'000'000) {
            numerator = 60;
            denominator = 1;
        }
        if (numerator == numerator_ && denominator == denominator_)
            return false;
        const long double oldPeriod = 1'000'000'000.L * denominator_ / numerator_;
        const long double newPeriod = 1'000'000'000.L * denominator / numerator;
        const long double remaining =
            next_ > now ? std::min(static_cast<long double>(next_ - now), oldPeriod) : 0;
        next_ = now + uint64_t(std::llround(remaining * newPeriod / oldPeriod));
        numerator_ = numerator;
        denominator_ = denominator;
        remainder_ = 0;
        return true;
    }
    bool pending(uint64_t now) {
        if (now < next_)
            return false;
        unsigned skipped = 0;
        do {
            advance();
        } while (next_ <= now && ++skipped < 4);
        if (next_ <= now) {
            next_ = now;
            remainder_ = 0;
            advance();
        }
        return true;
    }
    bool due(uint64_t now) const { return now >= next_; }
    uint32_t numerator() const { return numerator_; }
    uint32_t denominator() const { return denominator_; }

  private:
    void advance() {
        const uint64_t ns = 1'000'000'000ull * denominator_;
        next_ += ns / numerator_;
        remainder_ += ns % numerator_;
        if (remainder_ >= numerator_) {
            ++next_;
            remainder_ -= numerator_;
        }
    }
    uint64_t next_, remainder_ = 0;
    uint32_t numerator_ = 60, denominator_ = 1;
};
} // namespace grpp
