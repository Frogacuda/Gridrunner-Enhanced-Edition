#pragma once
#include "grpp/formations.hpp"

namespace grpp {
// The live 30x40 world grid, shared with hazards and combat. Motion reads the
// low-byte cell type; it does not generate a substitute obstacle field.
using WorldGrid = std::array<uint32_t, 50 * 50>;
struct ChainFrame {
    int minimumX, maximumX, minimumY, maximumY;
    float trailColorPhase;
    int frameNumber = 0;
};
const std::array<int32_t, 1024> &headingSines();

// Source case 3 has combat between advanceHead and steerHead. Source case 4
// has combat BEFORE followBody. These are real phase boundaries, not a full
// dispatcher with missing calls silently replaced by no-ops.
class ChainMotion {
  public:
    ChainMotion(EntityPool &pool, PlayerState &player, GameRandom &random,
                const FormationTuning &tuning, const Assets &assets)
        : pool_(pool), player_(player), random_(random), tuning_(tuning), assets_(assets) {}
    void advanceHead(size_t slot, const ChainFrame &frame, EntityCensus &census);
    void steerHead(Entity &head, const ChainFrame &frame, const WorldGrid &grid);
    void followBody(Entity &body, float trailColorPhase);
    void createAttack(const Entity &head);

  private:
    void randomTurn(Entity &head);
    int retargetTurn(const Entity &head) const;
    void writeTrail(Entity &entity, float phase, bool head);
    EntityPool &pool_;
    PlayerState &player_;
    GameRandom &random_;
    const FormationTuning &tuning_;
    const Assets &assets_;
};
} // namespace grpp
