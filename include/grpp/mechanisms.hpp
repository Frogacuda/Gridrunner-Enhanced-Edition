#pragma once
#include "grpp/numeric.hpp"
#include "grpp/render.hpp"
#include <array>
#include <functional>
#include <vector>

namespace grpp {
struct LogicalInput {
    int x = 120, y = 240;
    uint32_t held = 0, pressed = 0;
};
class InputLatch {
  public:
    void press(uint32_t actions) { pending_ |= actions; }
    LogicalInput sample(int x, int y, uint32_t held) {
        LogicalInput result{x, y, held | pending_, (held & ~previous_) | pending_};
        previous_ = held;
        pending_ = 0;
        return result;
    }
    void reset() { previous_ = pending_ = 0; }

  private:
    uint32_t previous_ = 0, pending_ = 0;
};

struct TrailPoint {
    int32_t x = 0, y = 0, scale = 0, tag = 0, heading = 0, vx = 0, vy = 0;
    uint32_t color = 0;
};
// Descriptor consumed by the source's transformed-sprite renderer. Effects
// initialize selected members only; draw preparation supplies the sprite rect.
struct SpriteTransform {
    uint32_t noiseSeed = 0;
    int jitterX = 0, jitterY = 0;
    uint32_t flags = 0;
    int axisXX = 0, axisXY = 0, axisYX = 0, axisYY = 0;
    int pivotX = 0, pivotY = 0;
    int sheet = 0, x = 0, y = 0, width = 0, height = 0, tileWidth = 0, tileHeight = 0;
};
struct Entity {
    int32_t x = 0, y = 0, vx = 0, vy = 0, ax = 0, ay = 0;
    int32_t targetX = 0, targetY = 0;
    int sprite = 0, behavior = 0, drawing = 0;
    int animation = 0, animationBase = 0, animationShift = 0, animationFrames = 0;
    // The original reuses this behavior-dependent value for several roles.
    // Named accessors below distinguish callers without duplicating state.
    int32_t life = 0, phase = 0, attackParameters = 0;
    int companionHealth = 0;        // Source word 23; projectile/companion damage.
    int companionMaximumHealth = 0; // Source word 24.
    int oscillatorYStep = 0;        // Source word 25, respawn oscillator.
    int32_t scale = 0;
    uint32_t flags = 0, historyCursor = 0;
    // Shared source slots have role-dependent values. In particular the same
    // word carries a spawner command and, after construction, chain hit points.
    uint32_t commandOrHealth = 0;
    int32_t depth = 0, speed = 0, depthAcceleration = 0, outsideTicks = 0;
    int32_t extent = 0; // Shared depth limit / glyph height (source word 17).
    int next = 0, previous = 0, chainIndex = 0, historySpacing = 0;
    int mode = 0, heading = 0, turnRate = 0, hitFlash = 0, effectMode = 0;
    float colorPhase = 0;
    int colorCycle = -1;   // Source null pointer, distinct from oscillator zero.
    int trailProfile = -1; // Null or a typed profile index; reward profile is 0.
    SpriteTransform transform;
    std::array<TrailPoint, 64> history{};
    int32_t &turnTargetY() { return life; }
    int32_t &retargetCountdown() { return life; }
    int32_t &podPlantChance() { return life; }
    int32_t &impactGrowthStep() { return life; }
    int32_t &oscillatorX() { return life; }
    int &oscillatorXStep() { return companionHealth; }
    int &oscillatorY() { return companionMaximumHealth; }
    int &glyphWidth() { return sprite; }
    int32_t &glyphHeight() { return extent; }
    int32_t &depthLimit() { return extent; }
    int32_t &companionShieldTicks() { return life; }
    int32_t companionShieldTicks() const { return life; }
};

// One traversal owns this census. Travelling spawners, chains, projectiles and
// other enemy behaviors all contribute to activeEnemies in the source. The
// session supplies the previous traversal's total for spawn admission.
struct EntityCensus {
    int waiting = 0, activeEnemies = 0;
    // Source requires both distances to improve; not Euclidean nearest.
    int nearestX = 0x0fff0000, nearestY = 0x0fff0000, nearestHead = -1;
};

// The original uses slots, not a linked list or an ownership tree. Slots can
// be activated during an ascending update and later slots then run that tick.
class NativeMotion;
class EntityPool {
  public:
    static constexpr size_t Capacity = 116;
    static constexpr size_t Player = 89, Companion = 88, ShotFirst = 90, ShotCount = 10;
    static constexpr size_t SpawnerFirst = 12, SpawnerCount = 16, HeadFirst = 78, HeadCount = 8;
    static constexpr size_t EnemyShotFirst = 1, EnemyShotCount = 10;
    static constexpr size_t PickupFirst = 110, PickupCount = 5, Announcement = 115;
    static constexpr int LastBodySlot = int(HeadFirst - HeadCount); // 0040737a: 70, not 77.
    Entity &at(size_t i) { return entries_.at(i); }
    const Entity &at(size_t i) const { return entries_.at(i); }
    NativeMotion *nativeMotion = nullptr;
    void move(Entity &, int32_t dx, int32_t dy, int32_t dz = 0);
    int firstFree(size_t begin, size_t count) const;
    void reset(size_t begin, size_t count, bool menuParticles = false);
    void animate(Entity &entity, int32_t frame);
    void updateRange(size_t begin, size_t count, int32_t frame,
                     const std::function<void(Entity &, size_t)> &update);

  private:
    std::array<Entity, Capacity> entries_{};
};
bool squareCollision(const Entity &a, const Entity &b, int radius);
bool outsideX(const Entity &e, const SpriteRect &sprite);
bool outsideY(const Entity &e, const SpriteRect &sprite);
void updateMenuParticle(Entity &e, const Assets &assets);

struct PlayerState {
    bool arse = false;
    bool endurance = false;
    int fireParity = 0;
    int width() const { return arse ? 400 : 240; }
    int height() const { return arse ? 400 : 320; }
    int worldWidth() const { return width() * 2; }
    int worldHeight() const { return height() * 2; }
    int columns() const { return width() / 8; }
    int rows() const { return height() / 8; }
    int transitionTicks = 0;
    bool reverseTransition = false;
    int scoreClock = 0;
    int scoreStep = 2;
    int shotCountdown = 0, shotInterval = 2;
    int sheepPower = 1, followerShotIndex = 0;
    int worldX = 0, worldY = 0;
    bool alternateCraftUnlocked = false;
    int alternateCraft = 0, selectedPlayer = 0;
    int bombActive = 0; // Source 0=available, 1=firing, -1=spent this life.
    int bombTicks = 0, beamTarget = 0;
    int beamMode = 0;
    bool beamHitConsumed = false;
};
// Complete case 1 from 00410d5f, extracted as a mechanism. It does not claim
// to implement the owner/session or the projectile's collision dispatcher.
void updatePlayer(EntityPool &pool, PlayerState &state, const LogicalInput &input,
                  std::vector<int> &sounds);

struct Score {
    std::string name;
    uint32_t points = 0;
};
struct SaveSlot {
    std::string name;
    uint32_t bestLevel = 0, lives = 0, score = 0, level = 0, difficulty = 0;
};
struct Scores {
    std::array<Score, 10> normal;
    std::array<Score, 50> underworld;
    std::array<SaveSlot, 4> players;
    SaveSlot guest; // Fifth live profile, deliberately absent from hst.txt.
    uint32_t selectedPlayer = 0, alternateCraftUnlocked = 0;
    Score enduranceMaster{"VINDALOO", 0};
    uint32_t enduranceLevel = 0;
    bool extendedFormat = false;
    bool recordEndurance(unsigned level, uint32_t points, const std::string &name);
    void load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;
    int insertNormal(Score score);
    bool recordStage(unsigned stage, int32_t points, const std::string &name);
    bool recordCheckpoint(unsigned slot, int lives, unsigned level, int32_t score, int difficulty);
    void saveAtomic(const std::filesystem::path &path) const;
};
} // namespace grpp
