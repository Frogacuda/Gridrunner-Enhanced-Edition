#pragma once
#include <cstdint>
#include <set>
namespace grpp {
// Action fingers are separate from the movement finger. Times are milliseconds.
class TouchActions {
    std::set<int64_t> fingers_;
    int64_t second_ = 0;
    uint64_t down_ = 0, lastUp_ = 0;
    float x_ = 0, y_ = 0, lastX_ = 0, lastY_ = 0;
    bool candidate_ = false, previous_ = false, bomb_ = false, pulse_ = false;

  public:
    void reset() { *this = TouchActions{}; }
    void down(int64_t id, uint64_t now, float x, float y) {
        if (!fingers_.insert(id).second)
            return;
        if (fingers_.size() == 1) {
            second_ = id;
            down_ = now;
            x_ = x;
            y_ = y;
            candidate_ = true;
            const float dx = x - lastX_, dy = y - lastY_;
            bomb_ =
                previous_ && now >= lastUp_ && now - lastUp_ <= 300 && dx * dx + dy * dy <= .01f;
            pulse_ |= bomb_;
            previous_ = false;
        } else {
            bomb_ = true;
            pulse_ = true;
            candidate_ = false;
            previous_ = false;
        }
    }
    void move(int64_t id, float x, float y) {
        if (id == second_) {
            const float dx = x - x_, dy = y - y_;
            if (dx * dx + dy * dy > .01f)
                candidate_ = false;
        }
    }
    void up(int64_t id, uint64_t now, bool cancelled = false) {
        if (!fingers_.erase(id))
            return;
        if (id == second_) {
            previous_ = !cancelled && candidate_ && now >= down_ && now - down_ <= 250;
            lastUp_ = now;
            lastX_ = x_;
            lastY_ = y_;
            candidate_ = false;
        }
        if (cancelled) {
            previous_ = false;
            candidate_ = false;
            pulse_ = false;
        }
        if (fingers_.empty())
            bomb_ = false;
    }
    bool secondary() const { return !fingers_.empty(); }
    bool bomb() const { return bomb_ || pulse_; }
    void sampled() { pulse_ = false; }
};
} // namespace grpp
