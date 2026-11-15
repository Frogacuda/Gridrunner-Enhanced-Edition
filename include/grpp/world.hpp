#pragma once
#include "grpp/visuals.hpp"

namespace grpp {
struct WorldState {
    int rippleTicks = 0, rippleDepth = 0;
    bool forecast = false;
    int laserTicks = 0, laserX = 0, laserY = 0;
};
class WorldSystem {
  public:
    WorldSystem(EntityPool &pool, PlayerState &player, CombatState &combatState,
                CombatSystem &combat, WorldGrid &grid, const FormationTuning &tuning,
                WorldState &state, std::vector<int> &sounds)
        : pool_(pool), player_(player), combatState_(combatState), combat_(combat), grid_(grid),
          tuning_(tuning), state_(state), sounds_(sounds) {}
    void anchor(Entity &entity, const ChainFrame &frame);
    void respawn(Entity &entity);
    void grid(SourceVisuals &visual, const ChainFrame &frame, int finalBackgroundMode,
              bool advance = true);
    void lasers(SourceVisuals &visual, const ChainFrame &frame, bool advance = true);
    void nativeContacts();

  private:
    bool playerNear(int x, int y, int radius, bool cross = false) const;
    uint32_t fadeMask(int overrun, int finalBackgroundMode) const;
    EntityPool &pool_;
    PlayerState &player_;
    CombatState &combatState_;
    CombatSystem &combat_;
    WorldGrid &grid_;
    const FormationTuning &tuning_;
    WorldState &state_;
    std::vector<int> &sounds_;
    bool laserVisible_ = false;
    std::array<int32_t, 6> forecastAnchor_{};
    bool forecastAnchorValid_ = false;
    WorldGrid renderedGrid_{};
};
} // namespace grpp
