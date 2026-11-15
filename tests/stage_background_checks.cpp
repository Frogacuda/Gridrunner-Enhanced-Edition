#include "aof_frontend.hpp"
#include "grpp/background.hpp"
#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static const auto &recipe(const Game &g) { return g.background_; }
    static void retry(Game &g) { g.beginAttempt(); }
    static void clear(Game &g) {
        g.phase_ = GamePhase::StageClear;
        g.clearTicks_ = 1;
        g.player_.scoreStep = 16;
    }
};
} // namespace grpp
void confirm(Frontend &f) {
    FrontendInput input;
    input.confirm = true;
    f.step(input);
}
void back(Frontend &f) {
    FrontendInput input;
    input.back = true;
    f.step(input);
}
void click(Frontend &f, size_t index) {
    const auto r = f.menuBounds().at(index);
    FrontendInput i;
    i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
    i.pointerActive = i.pointerReleased = true;
    f.step(i);
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS SCRATCH required");
        Assets assets;
        assets.load(argv[1]);
        std::set<std::array<int, 6>> unique;
        for (unsigned stage = 0; stage < 50; ++stage)
            check(unique.insert(stageBackground(stage)).second, "Two levels share a grid recipe");
        check(stageBackground(0) == stageBackground(50), "Campaign wrap has wrong grid");
        const auto root = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(root);
        int previews = 0, states = 0;
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (bool deferred : {false, true}) {
                    const auto directory =
                        root / (std::to_string(aof) + "-" + std::to_string(steps) + "-" +
                                std::to_string(deferred));
                    std::filesystem::create_directories(directory);
                    auto standard = original::defaultScores(), alternate = aof::defaultScores();
                    for (auto *scores : {&standard, &alternate}) {
                        scores->selectedPlayer = 0;
                        scores->players[0].bestLevel = 49;
                        scores->players[0].level = 17;
                        scores->players[0].lives = 3;
                    }
                    standard.saveAtomic(directory / "hst.txt");
                    alternate.saveAtomic(directory / "aof-hst.txt");
                    auto owner = std::make_unique<Frontend>(assets, directory / "hst.txt");
                    auto &f = *owner;
                    f.editionMenu(true);
                    f.setNativeRate(true, steps);
                    f.deferPresentation(deferred);
                    confirm(f);
                    if (aof)
                        click(f, 1);
                    click(f, 0);
                    check(f.view() == FrontendView::Level, "No level-select view");
                    for (unsigned stage = 0; stage < 50; ++stage) {
                        f.step({});
                        check(f.canvas().decorativeGrid().pattern == stageBackground(stage),
                              "Level-select preview did not change grid");
                        ++previews;
                        FrontendInput next;
                        next.horizontal = 1;
                        f.step(next);
                    }
                    check(f.canvas().decorativeGrid().pattern == stageBackground(0),
                          "Level-select wrap did not restore first grid");
                    for (int n = 0; n < 17; ++n) {
                        FrontendInput next;
                        next.horizontal = 1;
                        f.step(next);
                    }
                    click(f, 2);
                    f.preparePresentation();
                    check(f.view() == FrontendView::Playing && f.game().stage() == 17,
                          "Selected level did not start");
                    check(f.canvas().decorativeGrid().pattern == stageBackground(17),
                          "First game frame ignored selected grid");
                    for (int n = 0; n < 12; ++n)
                        f.step({});
                    f.preparePresentation();
                    check(f.canvas().decorativeGrid().pattern == stageBackground(17),
                          "First source updates replaced the selected grid");
                    back(f);
                    f.step({});
                    check(NativeRateTests::recipe(f.game()) == stageBackground(0),
                          "Exit did not reset the stored menu grid");
                    back(f);
                    confirm(f);
                    check(f.view() == FrontendView::Main, "Exit route did not return to main");
                    check(f.canvas().decorativeGrid().pattern == stageBackground(0),
                          "Title did not render its reset grid");
                    click(f, 2);
                    f.preparePresentation();
                    check(f.game().stage() == 17 &&
                              f.canvas().decorativeGrid().pattern == stageBackground(17),
                          "Continue ignored checkpoint grid");
                    back(f);
                    f.step({});
                    back(f);
                    confirm(f);
                    click(f, 0);
                    click(f, 2);
                    f.preparePresentation();
                    check(f.game().stage() == 0 &&
                              f.canvas().decorativeGrid().pattern == stageBackground(0),
                          "New game retained previous session grid");
                }
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (unsigned stage : {0u, 17u, 49u}) {
                    auto faithful = std::make_unique<Game>(assets),
                         mainline = std::make_unique<Game>(assets);
                    mainline->stageBackgrounds(true);
                    for (auto *g : {faithful.get(), mainline.get()}) {
                        g->arse(aof);
                        g->setNativeRate(steps > 1, steps == 1 ? 3 : steps);
                        g->start(stage);
                    }
                    auto compare = [&]() {
                        check(faithful->randomState() == mainline->randomState() &&
                                  faithful->frame() == mainline->frame() &&
                                  faithful->combatState().score == mainline->combatState().score,
                              "Grid policy changed simulation/RNG");
                        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot)
                            check(std::memcmp(&faithful->entities().at(slot),
                                              &mainline->entities().at(slot), sizeof(Entity)) == 0,
                                  "Grid policy changed an enemy/player/history");
                        ++states;
                    };
                    for (int tick = 0; tick < 120; ++tick) {
                        faithful->step({120, 270, 0, 0});
                        mainline->step({120, 270, 0, 0});
                        compare();
                    }
                    NativeRateTests::retry(*faithful);
                    NativeRateTests::retry(*mainline);
                    compare();
                    mainline->renderNative();
                    if (steps > 1)
                        check(mainline->canvas().decorativeGrid().pattern == stageBackground(stage),
                              "Life retry changed the stage recipe");
                    NativeRateTests::clear(*faithful);
                    NativeRateTests::clear(*mainline);
                    for (int tick = 0; tick < steps * 2; ++tick) {
                        faithful->step({120, 270, 0, 0});
                        mainline->step({120, 270, 0, 0});
                        compare();
                    }
                    mainline->renderNative();
                    check(mainline->canvas().decorativeGrid().pattern ==
                              stageBackground((stage + 1) % 50),
                          "Level progression chose a different recipe than direct selection");
                }
        std::cout << previews << " real menu previews; start/continue/exit/restart routes; "
                  << states
                  << " policy-independent simulation states; retries/progression/wrap PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
