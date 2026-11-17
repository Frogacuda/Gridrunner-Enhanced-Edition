#pragma once
#include "grpp/chains.hpp"

namespace grpp {
struct CombatState {
    int score = 0, stageScore = 0;
    int killsUntilDrop = 10, pendingRestorePower = 0, restorePower = 0;
    int difficulty = 0, frameFlashTicks = 0, travellingAtDeath = 0;
    float shotColorPhase = 0;
};

// The original enemy/combat dispatcher over real shared entities. The session
// owns gates and traversal. Player input, world hazards and spawning are
// separate components participating in that same source-ordered traversal.
class CombatSystem {
    friend struct ReplayState;
  public:
    CombatSystem(EntityPool &pool, PlayerState &player, GameRandom &random,
                 FormationSystem &formations, CombatState &state, WorldGrid &grid,
                 const Assets &assets, std::vector<int> &sounds)
        : pool_(pool), player_(player), random_(random), formations_(formations),
          tuning_(formations.tuning()), state_(state), grid_(grid), assets_(assets),
          sounds_(sounds), motion_(pool, player, random, tuning_, assets) {}
    static bool supports(int behavior);
    void update(size_t slot, const ChainFrame &frame, EntityCensus &census);
    void beginNativeInterval() { nativeHitConsumed_.fill(false); }
    void nativeContacts(const ChainFrame &frame);
    void splitChain(size_t slot, int successor, int originalMode);
    void convertHead(size_t slot, int effect);
    void convertFragment(size_t slot, int effect);
    void beamDamage(size_t slot, int minimumX);
    void award(int points);
    void considerDrop(int minimumX);
    void playerContact(size_t slot);
    void hazardHit(int flashTicks) {
        state_.frameFlashTicks = flashTicks;
        killPlayer(0);
    }

  private:
    bool contactsPlayer(const Entity &entity, int radius) const;
    bool withinWorldSpriteBounds(const Entity &entity, const ChainFrame &frame, bool broad) const;
    void killPlayer(size_t hitter);
    void deathAnnouncement();
    void plantPod(const Entity &entity);
    void updateFlying(Entity &entity, const ChainFrame &frame);
    void updateSplit(Entity &entity, const ChainFrame &frame);
    void updatePlayerShot(Entity &entity, const ChainFrame &frame, bool advance = true);
    void shotDamage(size_t slot, int minimumX);
    void updatePickup(Entity &entity, const ChainFrame &frame, bool advance = true);
    void updateCompanion(Entity &entity, const EntityCensus &census);
    void companionContacts(Entity &entity);
    bool &contactBudget(Entity &entity);
    void companionDamage(size_t slot);
    void burstEnemyShot(Entity &entity, int points);
    void revivalAnnouncement();
    void writeEffectHistory(Entity &entity, bool tag, bool conditional);
    EntityPool &pool_;
    PlayerState &player_;
    GameRandom &random_;
    FormationSystem &formations_;
    const FormationTuning &tuning_;
    CombatState &state_;
    WorldGrid &grid_;
    const Assets &assets_;
    std::vector<int> &sounds_;
    ChainMotion motion_;
    std::array<bool, EntityPool::Capacity> nativeHitConsumed_{};
    std::array<uint64_t, EntityPool::Capacity> nativeHitGeneration_{};
};
} // namespace grpp
