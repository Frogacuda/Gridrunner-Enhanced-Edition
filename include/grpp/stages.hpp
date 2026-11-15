#pragma once
#include "grpp/numeric.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace grpp {
enum class LevelSetting {
    HeadHealth,
    SegmentHealth,
    AdvancePerTick,
    FormationAllowance,
    SegmentCount,
    HeadAttackParameters,
    HeadSpeedX,
    HeadSpeedY,
    WorldAnchorSpeedX,
    WorldAnchorSpeedY,
    WorldAnchorDepthStep,
    HistorySpacing,
    BeamReach,
    RetargetInterval,
    RetargetTurnStep,
    FlyingEnemyPlantsGrid,
    FlyingEnemyTracking,
    FlyingEnemyAlternateSpeed
};
struct SettingValue {
    LevelSetting setting;
    int32_t value;
};
struct StageSpec {
    const char *caption;
    std::vector<int> orientationSources;
    std::vector<uint32_t> spawnProgram;
    std::vector<SettingValue> settings;
    std::vector<std::vector<SettingValue>> formationPatterns;
};
// This is the game's own small data-script mechanism (0040c125/0040c27e),
// not a CPU interpreter. Addresses are replaced with indices into typed data.
class SpawnProgram {
  public:
    explicit SpawnProgram(const std::vector<uint32_t> &commands) : commands_(commands) {}
    std::optional<uint32_t> next();
    void skip(int count);
    bool ended() const { return ended_; }
    size_t cursor() const { return cursor_; }

  private:
    std::optional<uint32_t> advance(bool consumeEnd);
    struct Loop {
        size_t begin;
        int remaining;
    };
    const std::vector<uint32_t> &commands_;
    size_t cursor_ = 0;
    std::vector<Loop> loops_;
    bool ended_ = false;
};
} // namespace grpp
