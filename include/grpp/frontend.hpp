#pragma once
#include "grpp/frontend_data.hpp"
#include "grpp/game.hpp"
#include <filesystem>
namespace grpp {
// Menu rectangles use source left/top/right/bottom coordinates, strict edges.
std::vector<std::array<int, 4>> layoutMenu(TileMap &, const MenuSpec &, int padding = 8,
                                           const std::vector<std::string> &labels = {});
// Original hover drawing clips the hit rectangle in place as a side effect.
int menuHit(std::vector<std::array<int, 4>> &, int x, int y);
void drawMenuStars(Canvas &, const std::array<std::array<int32_t, 3>, 1000> &, int frame);
enum class MenuPopulation { Directional, Drifting, Profile };
void seedMenuParticles(EntityPool &, GameRandom &, int count, MenuPopulation);
std::vector<TileMap> paginateInformation(const std::vector<std::string> &);
enum class FrontendView {
    Splash,
    Main,
    About,
    Reader,
    Players,
    Slot,
    Clear,
    Name,
    Level,
    Playing,
    HighScores,
    Farewell,
    Campaign
};
struct FrontendInput {
    LogicalInput pointer;
    bool pointerActive = false, pointerReleased = false, confirm = false, back = false;
    int vertical = 0, horizontal = 0;
    char letter = 0;
    bool erase = false;
};
// Owns the outer 0041a93c flow and its modal source loops. Shares the actual
// gameplay clock, entity pool, RNG, color palette and profile owner with Game.
class Frontend {
  public:
    Frontend(Assets &, std::filesystem::path saveFile);
    void step(const FrontendInput &);
    void editionMenu(bool enabled);
    bool arseEdition() const { return aofActive_; }
    void setNativeRate(bool enabled, int substeps = 3) { game_.setNativeRate(enabled, substeps); }
    void deferPresentation(bool enabled) { game_.deferPresentation(enabled); }
    void preparePresentation(bool referenceBackground = true, bool actors = true) {
        if (view_ == FrontendView::Playing)
            game_.renderNative(referenceBackground, actors);
    }
    const Canvas &canvas() const { return game_.canvas_; }
    const Game &game() const { return game_; }
    Canvas expandedCanvas() { return game_.expandedCanvas(); }
    FrontendView view() const { return view_; }
    const Scores &scores() const { return scores_; }
    const std::vector<std::array<int, 4>> &menuBounds() const { return bounds_; }
    const std::vector<int> &sounds() const { return game_.sounds_; }
    bool audioResetRequested() const { return audioReset_; }
    unsigned assetRevision() const { return game_.assetRevision_; }
    bool quitRequested() const { return quit_; }
    const std::string &saveError() const { return saveError_; }
    void flush();
    void close();
    void cancelInput() {
        game_.cancelPendingInput();
        pressed_ = -1;
        previousPointerHeld_ = false;
    }

  private:
    void enter(FrontendView, bool initialize = true);
    void activate(int);
    void rebuild();
    void seedParticles(int count, MenuPopulation);
    void drawMenu(const FrontendInput &);
    void startGameplay(bool resume, bool endurance = false);
    void switchEdition();
    void finishGameplay();
    void levelChange(int);
    void editName(int);
    const std::string &playerName() const;
    SaveSlot &playerSlot();
    Assets &assets_;
    Game game_;
    Scores scores_;
    Scores originalScores_, aofScores_;
    std::array<int, 2> crafts_{{0, 1}};
    bool editionMenu_ = false, aofActive_ = false;
    bool freshProfile_ = true;
    std::filesystem::path aofSaveFile_;
    std::filesystem::path saveFile_;
    FrontendView view_ = FrontendView::Splash;
    TileMap map_{240, 320};
    std::vector<std::array<int, 4>> bounds_;
    std::vector<TileMap> pages_;
    std::array<std::array<int32_t, 3>, 1000> stars_{};
    int selected_ = 0, pressed_ = -1, level_ = 0, document_ = 0, page_ = 0, slogan_ = 0,
        nameLetter_ = 0, nameCursor_ = 0, rank_ = 10;
    bool previousPointerHeld_ = false, quit_ = false, dirty_ = false, audioReset_ = false;
    std::string saveError_;
};
} // namespace grpp
