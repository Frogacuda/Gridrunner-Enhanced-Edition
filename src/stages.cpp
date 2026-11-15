#include "grpp/stages.hpp"
#include <stdexcept>

namespace grpp {
std::optional<uint32_t> SpawnProgram::next() { return advance(true); }
std::optional<uint32_t> SpawnProgram::advance(bool consumeEnd) {
    if (ended_)
        return {};
    while (cursor_ < commands_.size()) {
        const uint32_t word = commands_[cursor_];
        if ((word & 0x8f000000u) == 0x8f000000u) {
            if (!consumeEnd)
                return {};
            ++cursor_;
            ended_ = true;
            // 0040c3b1..0040c3d5: finish this request by reading one ordinary
            // command even after completion is set. Only the *next* call stops.
            continue;
        }
        ++cursor_;
        if (!(word & 0x80000000u))
            return word;
        switch ((word >> 24) & 15u) {
        case 0:
            if (loops_.size() >= 8)
                throw std::runtime_error("Spawn loop stack exceeds original storage");
            loops_.push_back({cursor_ - 1, int((word >> 16) & 255)});
            break;
        case 1: {
            if (loops_.empty())
                throw std::runtime_error("Unmatched spawn loop end");
            auto &loop = loops_.back();
            if (loop.remaining > 0) {
                --loop.remaining;
                cursor_ = loop.begin + 1;
            } else
                loops_.pop_back();
            break;
        }
        default:
            break; // Original ignores the other high-bit control opcodes.
        }
    }
    throw std::runtime_error("Stage program reached the end of storage without its end command");
}
void SpawnProgram::skip(int count) {
    // 0040c125 neither consumes the terminal control nor sets the completion
    // flag. 0040c27e performs both when the next entity is requested.
    while (count > 0 && advance(false))
        --count;
}
} // namespace grpp
