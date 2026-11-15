#pragma once
#include "grpp/native_motion.hpp"
#include "grpp/world.hpp"
#include <memory>

namespace grpp {
class Frontend;
enum class GamePhase { Playing, StageClear, GameOver };
class Game {
  public:
    explicit Game(Assets &assets);
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    void start(unsigned stage = 0);
    void stageBackgrounds(bool enabled);
    void arse(bool enabled) {
        arse_ = enabled;
        player_.arse = enabled;
        assets_.arse(enabled);
        player_.shotInterval = enabled ? 1 : 2;
    }
    bool arse() const { return arse_; }
    void endurance(bool enabled) {
        endurance_ = enabled;
        player_.endurance = enabled;
    }
    bool endurance() const { return endurance_; }
    void step(const LogicalInput &input);
    void deferPresentation(bool enabled) { deferPresentation_ = enabled; }
    void setNativeRate(bool enabled, int substeps = 3);
    int nativeSubsteps() const;
    bool nativeRate() const { return bool(native_); }
    unsigned nativePhase() const { return nativePhase_; }
    void cancelPendingInput() { pendingPressed_ = pendingHeld_ = 0; }
    void renderNative(bool referenceBackground = true,
                      bool actors = true); // Pure command generation from the current live state.
    Canvas expandedCanvas();               // Original pan/projection, with extended actor clipping.
    const Canvas &canvas() const { return canvas_; }
    const EntityPool &entities() const { return pool_; }
    const PlayerState &player() const { return player_; }
    const CombatState &combatState() const { return combatState_; }
    const WorldState &worldState() const { return worldState_; }
    const EntityCensus &census() const { return census_; }
    const FormationSystem &formations() const { return formations_; }
    const std::vector<int> &sounds() const { return sounds_; }
    GamePhase phase() const { return phase_; }
    unsigned stage() const { return stage_; }
    int lives() const { return lives_; }
    int frame() const { return frame_; }
    int stagesCleared() const { return stagesCleared_; }
    int deaths() const { return deaths_; }
    unsigned assetRevision() const { return assetRevision_; }
    uint32_t randomState() const { return random_.state(); }

  private:
    friend class Frontend; // The original menu/session owner shares these live globals.
    friend struct NativeRateTests;
    void sourceStep(const LogicalInput &input);
    void beginAttempt();
    void warpIn();
    void gameOver();
    void hud(SourceVisuals &visual, const ChainFrame &frame);
    void cacheHud(size_t first);
    void stageFinished();
    Assets &assets_;
    Canvas canvas_;
    EntityPool pool_;
    PlayerState player_;
    GameRandom random_{1};
    EffectRandom effects_;
    FormationSystem formations_{pool_, player_, random_};
    CombatState combatState_;
    WorldGrid grid_{};
    WorldState worldState_;
    std::vector<int> sounds_;
    CombatSystem combat_{pool_,        player_, random_, formations_,
                         combatState_, grid_,   assets_, sounds_};
    WorldSystem world_{pool_,       player_, combatState_, combat_, grid_, formations_.tuning(),
                       worldState_, sounds_};
    EntityCensus census_;
    GamePhase phase_ = GamePhase::Playing;
    unsigned stage_ = 0, assetRevision_ = 0;
    int lives_ = 3, frame_ = 0, stagesCleared_ = 0, deaths_ = 0, introTicks_ = 60, clearTicks_ = 0,
        backgroundAdvance_ = 0;
    int beamX_ = 0, beamY_ = 0;
    bool beginPending_ = false;
    bool arse_ = false, endurance_ = false, arseStarted_ = false;
    bool stageBackgrounds_ = false;
    float colorPhase_ = 0;
    uint32_t frameClearColor_ = 0;
    std::array<int, 6> background_{};
    std::array<int, 6> nativeVisualBackground_{};
    TileMap announcement_{240, 320};
    std::vector<uint32_t> textPalette_ = std::vector<uint32_t>(129, 0);
    Scores *records_ = nullptr;
    std::string playerName_;
    bool campaignNotice_ = false;
    std::unique_ptr<NativeMotion> native_;
    std::unique_ptr<EntityPool> nativeDrawBase_ = std::make_unique<EntityPool>();
    uint32_t nativeVisualRandom_ = 1, nativeVisualEffects_ = 1;
    float nativeVisualPhase_ = 0;
    int nativeVisualFrame_ = 0;
    unsigned nativePhase_ = 0;
    bool deferPresentation_ = false;
    uint32_t pendingPressed_ = 0, pendingHeld_ = 0;
    Canvas nativeHud_;
    size_t nativeHudFirst_ = 0;
};
} // namespace grpp
