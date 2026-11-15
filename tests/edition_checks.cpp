#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
void confirm(Frontend &f) {
    FrontendInput i;
    i.confirm = true;
    f.step(i);
}
void click(Frontend &f, size_t n) {
    auto r = f.menuBounds().at(n);
    FrontendInput i;
    i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
    i.pointerActive = i.pointerReleased = true;
    f.step(i);
}
void back(Frontend &f) {
    FrontendInput i;
    i.back = true;
    f.step(i);
}
namespace grpp {
struct NativeRateTests {
    static Game &live(Frontend &f) { return const_cast<Game &>(f.game()); }
    static void clear(Game &g) {
        auto &p = g.pool_.at(89);
        p.x = fixed16(83);
        p.y = fixed16(151);
        g.player_.sheepPower = 4;
        g.grid_[0] = 123;
        g.stageFinished();
        if (g.endurance()) {
            check(g.phase_ == GamePhase::Playing && g.lives_ == 0,
                  "Endurance clear enters interlude/grants life");
            g.beginAttempt();
            check(p.x == fixed16(83) && p.y == fixed16(151) && g.player_.sheepPower == 4 &&
                      g.grid_[0] == 123,
                  "Endurance state not continuous");
        } else
            check(g.phase_ == GamePhase::StageClear && g.lives_ == 4,
                  "Normal stage clear lost recharge/life");
    }
    static void score(Game &g, int score, unsigned level) {
        g.combatState_.score = score;
        g.stage_ = level;
    }
    static void die(Game &g) {
        g.player_.transitionTicks = 100;
        g.player_.reverseTransition = false;
        g.sourceStep({120, 200, 0, 0});
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        check(argc == 4, "ASSETS SCRATCH ORIGINAL_AOF_SAVE required");
        const auto root =
            std::filesystem::path(argv[2]) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        Scores imported;
        imported.load(argv[3]);
        check(imported.extendedFormat && imported.selectedPlayer == 1 &&
                  imported.alternateCraftUnlocked == 0 &&
                  imported.enduranceMaster.name == "MUSK_OX" &&
                  imported.enduranceMaster.points == 0x1b85f5u && imported.enduranceLevel == 4 &&
                  imported.normal[0].points == 0x122717u,
              "Original AoF save fields shifted/lost");
        imported.saveAtomic(root / "imported.txt");
        Scores roundtrip;
        roundtrip.load(root / "imported.txt");
        check(roundtrip.enduranceMaster.points == imported.enduranceMaster.points &&
                  roundtrip.selectedPlayer == 1,
              "Original AoF save roundtrip");
        for (bool aof : {false, true})
            for (bool endurance : {false, true}) {
                const auto dir = root / (std::to_string(aof) + std::to_string(endurance));
                std::filesystem::create_directories(dir);
                auto standard = original::defaultScores();
                standard.selectedPlayer = 1;
                standard.players[1].name = "ACTIVE_EWE";
                standard.players[1].level = 2;
                standard.players[1].lives = 5;
                standard.players[1].score = 1234;
                standard.saveAtomic(dir / "hst.txt");
                auto alternate = original::defaultScores();
                alternate.extendedFormat = true;
                alternate.selectedPlayer = 2;
                alternate.players[1].level = 3;
                alternate.players[1].lives = 6;
                alternate.players[1].score = 5678;
                alternate.alternateCraftUnlocked = 0;
                alternate.saveAtomic(dir / "aof-hst.txt");
                Assets assets;
                assets.load(argv[1]);
                const auto originalBanner = assets.page(2).pixels;
                auto owner = std::make_unique<Frontend>(assets, dir / "hst.txt");
                auto &f = *owner;
                f.editionMenu(true);
                f.setNativeRate(true, 6);
                confirm(f);
                check(f.menuBounds().size() == 6, "Missing title actions");
                if (aof) {
                    click(f, 1);
                    check(f.view() == FrontendView::Main && f.arseEdition() && assets.arse(),
                          "Edition toggle starts gameplay");
                    check(f.audioResetRequested() && f.sounds() == std::vector<int>{7},
                          "Title audio restart missing");
                    check(assets.page(2).pixels != originalBanner,
                          "Title artwork not switched immediately");
                    check(!f.game().player().alternateCraftUnlocked,
                          "Edition switch forces unlock");
                }
                check(f.scores().selectedPlayer == 1 && f.scores().players[1].name == "ACTIVE_EWE",
                      "Edition switch loses active identity");
                click(f, 0);
                check(f.view() == FrontendView::Level, "Play skips level selection");
                click(f, endurance ? 3 : 2);
                check(f.view() == FrontendView::Playing && f.game().arse() == aof &&
                          f.game().endurance() == endurance,
                      "Edition/mode coupling");
                check(f.game().lives() == (endurance ? 0 : 3) && f.game().stage() == 0,
                      "Mode start state");
                check(f.scores().selectedPlayer == 1 &&
                          f.game().entities().at(89).sprite == (aof ? 364 : 359),
                      "Wrong profile-linked ship after edition switch");
                check(f.canvas().width() == (aof ? 400 : 240) &&
                          f.canvas().height() == (aof ? 400 : 320),
                      "Edition arena dimensions");
                NativeRateTests::clear(NativeRateTests::live(f));
                if (endurance) {
                    NativeRateTests::score(NativeRateTests::live(f), 999999, 7);
                    back(f);
                    check(f.scores().normal[0].points == 50000, "Endurance pollutes normal scores");
                    check(f.scores().enduranceMaster.points == 999999 &&
                              f.scores().enduranceLevel == 7,
                          "Endurance record missing");
                } else
                    back(f);
                f.flush();
                Scores loaded;
                loaded.load(dir / (aof ? "aof-hst.txt" : "hst.txt"));
                if (endurance)
                    check(loaded.enduranceLevel == 7 && loaded.enduranceMaster.points == 999999,
                          "Endurance record roundtrip");
                while (f.view() != FrontendView::Main)
                    confirm(f);
                check(f.arseEdition() == aof, "Edition lost when returning to title");
                click(f, 2);
                check(f.view() == FrontendView::Playing && !f.game().endurance() &&
                          f.game().stage() == (aof ? 3u : 2u) && f.game().lives() == (aof ? 6 : 5),
                      "Resume inherits Endurance or loses checkpoint");
                back(f);
                while (f.view() != FrontendView::Main)
                    confirm(f);
                click(f, 1);
                check(f.arseEdition() != aof && assets.arse() != aof, "Reverse edition switch");
                click(f, 1);
                check(f.scores().selectedPlayer == 1u, "Edition toggle changes active player");
            }
        // Mainline level navigation groups +/- into one keyboard/controller row.
        for (bool aof : {false, true}) {
            Assets menuAssets;
            menuAssets.load(argv[1]);
            auto f = std::make_unique<Frontend>(menuAssets, std::filesystem::path{});
            f->editionMenu(true);
            confirm(*f);
            if (aof)
                click(*f, 1);
            click(*f, 0);
            FrontendInput left;
            left.horizontal = -1;
            f->step(left); // Wrap to a nonzero unlocked level.
            FrontendInput down;
            down.vertical = 1;
            f->step(down);
            confirm(*f);
            check(f->view() == FrontendView::Playing && f->game().stage() == 4,
                  "One Down did not focus Start after grouped level row");
            back(*f);
            while (f->view() != FrontendView::Main)
                confirm(*f);
            click(*f, 0);
            FrontendInput right;
            right.horizontal = 1;
            f->step(right);
            f->step(down);
            f->step(right);
            confirm(*f);
            check(f->view() == FrontendView::Playing && f->game().stage() == 1,
                  "Horizontal input on Start changed level");
        }
        Scores records = original::defaultScores();
        check(records.recordEndurance(5, 100, "A"), "Initial record");
        check(!records.recordEndurance(4, 9999, "B"), "Score overrides higher level");
        check(!records.recordEndurance(5, 99, "B"), "Worse tied-level score");
        check(records.recordEndurance(5, 101, "B"), "Score tiebreak");
        check(records.recordEndurance(6, 1, "C"), "Higher level not preferred");
        Assets assets;
        assets.load(argv[1]);
        for (bool aof : {false, true}) {
            auto f = std::make_unique<Frontend>(assets, std::filesystem::path{});
            f->editionMenu(true);
            confirm(*f);
            if (aof)
                click(*f, 1);
            for (int profile = 0; profile < 5; ++profile) {
                click(*f, 4);
                click(*f, size_t(profile));
                if (profile < 4)
                    click(*f, 3);
                click(*f, 2);
                check(f->view() == FrontendView::Playing &&
                          f->game().player().selectedPlayer == profile &&
                          f->game().entities().at(89).sprite == 358 + profile + (aof ? 5 : 0),
                      "Player menu launches wrong ship");
                back(*f);
                while (f->view() != FrontendView::Main)
                    confirm(*f);
            }
        }
        for (bool aof : {false, true})
            for (bool endurance : {false, true}) {
                auto g = std::make_unique<Game>(assets);
                g->arse(aof);
                g->endurance(endurance);
                g->start();
                NativeRateTests::die(*g);
                check(g->deaths() == 1 &&
                          g->phase() == (endurance ? GamePhase::GameOver : GamePhase::Playing),
                      "Death budget coupled to edition");
                if (endurance)
                    check(g->sounds().back() == (aof ? 35 : 16),
                          "Wrong edition/mode game-over sample");
            }
        auto pool = std::make_unique<EntityPool>();
        PlayerState p;
        std::vector<int> sounds;
        for (int profile = 0; profile < 5; ++profile) {
            p = {};
            p.arse = true;
            p.selectedPlayer = profile;
            p.alternateCraftUnlocked = true;
            p.shotCountdown = 10000;
            auto &ship = pool->at(89);
            ship.sprite = 358 + profile;
            updatePlayer(*pool, p, {120, 200, 0, 0x40}, sounds);
            check(ship.sprite == 363 + profile, "Alternate craft variant missing");
            updatePlayer(*pool, p, {120, 200, 0, 0x40}, sounds);
            check(ship.sprite == 358 + profile, "Standard craft variant missing");
            p.alternateCraftUnlocked = false;
            updatePlayer(*pool, p, {120, 200, 0, 0x40}, sounds);
            check(ship.sprite == 358 + profile, "Locked craft toggles");
        }
        std::cout << "Four edition/mode combinations, immediate title/audio commands, resume, "
                     "records, saves and ten craft variants PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
