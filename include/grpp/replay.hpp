#pragma once
#include "grpp/game.hpp"
#include <filesystem>
#include <vector>
namespace grpp {
struct ReplayInfo {
    uint32_t ticks = 0, score = 0, stage = 0, profile = 0, maxStage = 1;
    bool aof = false, completed = false;
};
struct ReplayState {
    struct Fields;
    static std::vector<uint8_t> capture(Game &);
    static void restore(Game &, const std::vector<uint8_t> &);
};
// Consumes one recorded native tick per step; the host owns pacing and pause.
class ReplayPlayback {
  public:
    void load(Game &, const std::filesystem::path &);
    bool step(Game &);
    const ReplayInfo &info() const { return info_; }

  private:
    ReplayInfo info_;
    std::vector<uint8_t> bytes_;
    size_t position_ = 0;
    uint32_t ticks_ = 0, highestStage_ = 0;
    int x_ = 0, y_ = 0;
    uint8_t buttons_ = 0;
};
class ReplayRecording {
  public:
    static constexpr uint32_t MaximumTicks = 200 * 60 * 90;
    void begin(Game &, unsigned profile);
    void append(const LogicalInput &);
    void resetInput();
    void checkpoint(Game &);
    void finish(int score, bool completed);
    void reachedStage(unsigned stage);
    void discard();
    bool active() const { return active_; }
    bool pending() const { return pending_; }
    const ReplayInfo &info() const { return info_; }
    void save(const std::filesystem::path &) const;
    static ReplayInfo inspect(const std::filesystem::path &);
    // Runs the incremental decoder to completion for verification.
    static ReplayInfo play(Game &, const std::filesystem::path &);

  private:
    ReplayInfo info_;
    std::vector<uint8_t> initial_, inputs_;
    bool active_ = false, pending_ = false;
    int x_ = 0, y_ = 0;
    uint8_t buttons_ = 0;
};
} // namespace grpp
