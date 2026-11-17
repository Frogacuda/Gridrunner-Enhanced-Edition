#pragma once
#include "grpp/mechanisms.hpp"
#include "grpp/stages.hpp"
#include <optional>

namespace grpp {
// Original mutable tuning. Level and formation records overwrite selected
// members; the remaining members carry forward. These are not per-level defaults.
struct FormationTuning {
    int headHealth = -1, segmentHealth = -1, podResistance = 1;
    int allowance = 8, segmentCount = 4;
    uint32_t headAttack = 65539;
    int speedX = 1280, speedY = 1280;
    int anchorSpeedX = 524288, anchorSpeedY = 655360, anchorDepthStep = 128;
    int headSprite = 124, segmentSprite = 116, historySpacing = 4;
    uint32_t splitTransform = 528;
    int retargetInterval = -1, retargetTurnStep = 64, beamReach = 60;
    int specialTurnChance = 0, flyingPlantsGrid = 0, flyingTracking = 0, flyingAlternateSpeed = 0;
};
struct SpawnFrame {
    int minimumX, maximumX, minimumY, maximumY;
    int previousActiveEnemies;
    bool enabled = true;
};

// Owns the complete waiting -> travelling -> materialization -> requeue path.
// Enemy movement, combat and session transitions are separate consumers of its
// real entities; they are not replaced by provisional behavior here.
class FormationSystem {
    friend struct ReplayState;
  public:
    FormationSystem(EntityPool &pool, PlayerState &player, GameRandom &random)
        : pool_(pool), player_(player), random_(random) {}
    void prepareLevel(const StageSpec &stage, unsigned levelIndex, int cycleDifficulty,
                      int skipCommands = 0);
    void seedInitialSpawners();
    void updateSpawner(Entity &entity, const SpawnFrame &frame, EntityCensus &counts);
    void materialize(const Entity &spawner);
    bool createCompanion();
    void resetEmissionCount() { emitted_ = 0; }
    FormationTuning &tuning() { return tuning_; }
    const FormationTuning &tuning() const { return tuning_; }
    int baselineAdvance() const { return baselineAdvance_; }
    int activeThreshold() const { return activeThreshold_; }
    int emitted() const { return emitted_; }
    int requeueDistance() const { return requeueDistance_; }
    bool programEnded() const { return program_ && program_->ended(); }
    const SpawnProgram &program() const;

  private:
    void apply(const SettingValue &setting);
    void applyPattern(uint32_t command);
    void requeue(Entity &spawner);
    void randomIdleMotion(Entity &entity);
    void remember(Entity &entity, int32_t depth);
    void fillHistory(Entity &entity, bool includeHeading);
    int freeBodyBefore(int slot) const;
    EntityPool &pool_;
    PlayerState &player_;
    GameRandom &random_;
    const StageSpec *stage_ = nullptr;
    std::optional<SpawnProgram> program_;
    FormationTuning tuning_;
    int baselineAdvance_ = 16, activeThreshold_ = 100, emitted_ = 0, requeueDistance_ = 0;
    unsigned levelIndex_ = 0;
};
} // namespace grpp
