#pragma once
#include <cstdint>
#include <cstring>

namespace grpp {
// Original arithmetic is 32-bit two's-complement. These helpers make wrapping
// and arithmetic shifts explicit, including on ARM and under UB sanitizers.
inline int32_t signedBits(uint32_t value) noexcept {
    int32_t result;
    std::memcpy(&result, &value, sizeof result);
    return result;
}
inline int32_t add32(int32_t a, int32_t b) noexcept {
    return signedBits(uint32_t(a) + uint32_t(b));
}
inline int32_t sub32(int32_t a, int32_t b) noexcept {
    return signedBits(uint32_t(a) - uint32_t(b));
}
inline int32_t mul32(int32_t a, int32_t b) noexcept {
    return signedBits(uint32_t(a) * uint32_t(b));
}
inline int32_t asr32(int32_t a, unsigned bits) noexcept {
    bits &= 31;
    if (!bits)
        return a;
    return signedBits((uint32_t(a) >> bits) | (a < 0 ? (~uint32_t(0) << (32 - bits)) : 0));
}
inline int32_t fixed16(int value) noexcept { return signedBits(uint32_t(value) << 16); }
inline int pixel(int32_t value) noexcept { return asr32(value, 16); }

// VC6 rand(), recovered CRT 0041b132. Never share this stream with effects.
class GameRandom {
  public:
    explicit GameRandom(uint32_t seed = 1) : state_(seed) {}
    uint32_t next() noexcept {
        state_ = state_ * 214013u + 2531011u;
        return (state_ >> 16) & 0x7fffu;
    }
    int below(int bound) noexcept { return asr32(mul32(int32_t(next()), bound), 15); }
    float scaled(float bound) noexcept { return float(double(next()) * double(bound) / 32767.0); }
    uint32_t state() const noexcept { return state_; }

  private:
    uint32_t state_;
};

// 00408277: arithmetic shift, not a conventional unsigned LFSR shift.
class EffectRandom {
  public:
    explicit EffectRandom(uint32_t seed) : state_(seed) {}
    uint32_t next() noexcept {
        const bool low = (state_ & 1u) != 0;
        state_ = uint32_t(asr32(signedBits(state_), 1));
        if (low)
            state_ ^= 0xa3000000u;
        return state_;
    }
    uint32_t state() const noexcept { return state_; }

  private:
    uint32_t state_;
};
} // namespace grpp
