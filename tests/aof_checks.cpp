#include "aof_levels.hpp"
#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include "original_levels.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
using namespace grpp;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
namespace grpp {
struct NativeRateTests {
    static void continuity(Game &g) {
        auto &p = g.pool_.at(EntityPool::Player);
        p.x = fixed16(123);
        p.y = fixed16(222);
        p.history[7].x = 12345;
        g.player_.sheepPower = 4;
        g.grid_[2499] = 0x12345678;
        auto &anchor = g.pool_.at(0);
        anchor.x = fixed16(333);
        anchor.y = fixed16(444);
        g.stageFinished();
        check(g.phase_ == GamePhase::Playing && g.lives_ == 0, "AoF clear grants a life/interlude");
        g.beginAttempt();
        check(p.x == fixed16(123) && p.y == fixed16(222) && p.history[7].x == 12345,
              "AoF clear resets player");
        check(g.player_.sheepPower == 4 && g.grid_[2499] == 0x12345678,
              "AoF clear resets power/grid");
        check(anchor.x == fixed16(333) && anchor.y == fixed16(444), "AoF clear resets anchor");
        g.stage_ = 49;
        g.stageFinished();
        check(g.stage_ == 0 && g.combatState_.difficulty == 2 && g.lives_ == 0,
              "AoF campaign wrap");
        g.beginPending_ = false;
        g.grid_ = {};
        g.player_.transitionTicks = 100;
        g.player_.reverseTransition = false;
        g.step({123, 222, 0, 0});
        check(g.sounds_.back() == 35 && g.phase_ == GamePhase::GameOver && g.deaths_ == 1 &&
                  g.lives_ == -1,
              "AoF one-death game over");
    }
};
} // namespace grpp
std::string contents(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void confirm(Frontend &f) {
    FrontendInput i;
    i.confirm = true;
    f.step(i);
}
void click(Frontend &f, size_t n) {
    auto r = f.menuBounds().at(n);
    FrontendInput i;
    i.pointer = {((r[0] + r[2]) / 2), ((r[1] + r[3]) / 2), 0, 0};
    i.pointerActive = i.pointerReleased = true;
    f.step(i);
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS SCRATCH required");
        Assets assets;
        assets.load(argv[1]);
        const auto originalPixels = assets.page(0).pixels;
        assets.arse(true);
        assets.load(argv[1]);
        bool artChanged = originalPixels != assets.page(0).pixels;
        // Both source sheets are overrides; the first page need not contain changed pixels.
        assets.arse(false);
        assets.load(argv[1]);
        auto normalSecond = assets.page(1).pixels;
        assets.arse(true);
        assets.load(argv[1]);
        artChanged |= normalSecond != assets.page(1).pixels;
        check(artChanged, "AoF artwork not selected");
        auto g = std::make_unique<Game>(assets);
        g->arse(true);
        g->endurance(true);
        auto normal = std::make_unique<Game>(assets);
        for (unsigned stage = 0; stage < 50; ++stage) {
            const auto &a = aof::stages[stage];
            const auto &o = original::stages[stage];
            check(std::string(a.caption) == o.caption &&
                      a.orientationSources == o.orientationSources &&
                      a.spawnProgram == o.spawnProgram,
                  "Recovered AoF stage data mismatch");
            normal->start(stage);
            const auto base = normal->formations().tuning();
            const int advance = normal->formations().baselineAdvance();
            g->start(stage);
            check(g->canvas().width() == 400 && g->canvas().height() == 400 && g->lives() == 0,
                  "AoF geometry/lives");
            check(g->formations().tuning().allowance == base.allowance - 2 &&
                      g->formations().baselineAdvance() == advance * 2,
                  "AoF formation/advance changes");
            check(g->formations().tuning().beamReach == std::min(400, base.beamReach + 300),
                  "AoF beam reach change");
            check(g->player().shotInterval == 1 && g->player().alternateCraft == 1 &&
                      !g->player().alternateCraftUnlocked,
                  "AoF player defaults");
            const auto &t = g->formations().tuning();
            const auto &anchor = g->entities().at(0);
            check(anchor.vx == mul32(t.anchorSpeedX, 2) && anchor.vy == mul32(t.anchorSpeedY, 2) &&
                      anchor.speed == mul32(t.anchorDepthStep, 2),
                  "AoF anchor speed");
            check(t.beamReach <= 400, "AoF beam cap");
            check(g->entities().at(89).transform.jitterX == 240,
                  "Source shatter width changed with playfield");
            for (int frame = 0; frame < 12; ++frame)
                g->step({200, 300, 0, 0});
        }
        g->start();
        NativeRateTests::continuity(*g);
        auto pool = std::make_unique<EntityPool>();
        PlayerState p;
        p.arse = true;
        p.shotInterval = 1;
        p.transitionTicks = 0;
        p.sheepPower = 1;
        auto &ship = pool->at(89);
        ship.x = fixed16(200);
        ship.y = fixed16(300);
        ship.behavior = 1;
        std::vector<int> sounds;
        int lastSign = 0;
        for (int n = 0; n < 8; ++n) {
            p.shotCountdown = -1;
            for (size_t j = EntityPool::ShotFirst;
                 j < EntityPool::ShotFirst + EntityPool::ShotCount; ++j)
                pool->at(j).behavior = 0;
            updatePlayer(*pool, p, {200, 300, 1, 0}, sounds);
            auto &s = pool->at(EntityPool::ShotFirst);
            check(s.vy == 0 && s.ay == 0 && std::abs(s.vx) == 65536 && std::abs(s.ax) == 8192 &&
                      s.vx * lastSign <= 0,
                  "AoF alternating sidefire");
            lastSign = s.vx > 0 ? 1 : -1;
        }
        for (size_t j = EntityPool::ShotFirst; j < EntityPool::ShotFirst + EntityPool::ShotCount;
             ++j)
            pool->at(j).behavior = 0;
        p.shotCountdown = -1;
        updatePlayer(*pool, p, {200, 300, 0, 0}, sounds);
        check(pool->at(EntityPool::ShotFirst).ay == -8192, "AoF primary acceleration");
        p.shotCountdown = -1;
        p.sheepPower = 2;
        updatePlayer(*pool, p, {200, 300, 0, 0}, sounds);
        check(std::abs(pool->at(EntityPool::ShotFirst + EntityPool::ShotCount).ay) == 12288,
              "AoF arc acceleration");
        GameRandom random{0x1982};
        FormationSystem formations(*pool, p, random);
        CombatState state;
        WorldGrid grid{};
        CombatSystem combat(*pool, p, random, formations, state, grid, assets, sounds);
        std::set<int> voices;
        for (int n = 0; n < 128; ++n) {
            sounds.clear();
            p.transitionTicks = 0;
            ship.behavior = 1;
            ship.x = ship.y = 0;
            GameRandom expected = random;
            expected.below(512);
            const int voice = expected.below(8);
            const int impact = expected.below(2) + 10;
            expected.below(512);
            expected.below(8);
            expected.below(8);
            auto &enemy = pool->at(70);
            enemy.x = enemy.y = 0;
            enemy.behavior = 3;
            combat.playerContact(70);
            check(sounds.size() == 2 && (sounds[1] == 10 || sounds[1] == 11),
                  "AoF death sound trigger count");
            voices.insert(sounds[0]);
            const int ids[]{1, 2, 3, 4, 31, 32, 33, 34};
            check(sounds[0] == ids[voice] && sounds[1] == impact &&
                      random.state() == expected.state(),
                  "AoF death RNG order");
        }
        check(voices == std::set<int>{1, 2, 3, 4, 31, 32, 33, 34},
              "AoF eight-voice death selection");
        WorldState worldState;
        WorldSystem world(*pool, p, state, combat, grid, formations.tuning(), worldState, sounds);
        p.transitionTicks = 0;
        p.worldX = p.worldY = 0;
        ship.behavior = 1;
        ship.x = ship.y = fixed16(784);
        grid[2499] = 1;
        world.nativeContacts();
        check(p.transitionTicks > 0, "AoF native grid contacts omit far rows/columns");
        const auto dir = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(dir);
        auto save = dir / "hst.txt";
        original::defaultScores().saveAtomic(save);
        const auto before = contents(save);
        assets.arse(false);
        assets.load(argv[1]);
        auto f = std::make_unique<Frontend>(assets, save);
        f->editionMenu(true);
        f->setNativeRate(true, 6);
        confirm(*f);
        check(f->menuBounds().size() == 6, "Edition toggle missing");
        click(*f, 1);
        click(*f, 0);
        click(*f, 3);
        check(f->view() == FrontendView::Playing && f->arseEdition() && f->game().lives() == 0 &&
                  f->canvas().width() == 400,
              "AoF menu route");
        for (int n = 0; n < 60; ++n)
            f->step({});
        FrontendInput back;
        back.back = true;
        f->step(back);
        f->flush();
        check(contents(save) == before, "AoF changed original save");
        check(std::filesystem::exists(dir / "aof-hst.txt"), "AoF save missing");
        for (int n = 0; n < 5 && f->view() != FrontendView::Main; ++n)
            confirm(*f);
        check(f->arseEdition() && assets.arse(), "Edition lost on return");
        click(*f, 1);
        check(!assets.arse() && f->canvas().width() == 240,
              "Edition toggle did not restore Standard");
        auto native = std::make_unique<Game>(assets), source = std::make_unique<Game>(assets);
        native->arse(true);
        source->arse(true);
        native->setNativeRate(true, 6);
        native->start();
        source->start();
        for (int frame = 0; frame < 40; ++frame) {
            source->step({200, 300, 0, 0});
            for (int sub = 0; sub < 6; ++sub)
                native->step({200, 300, 0, 0});
            check(source->frame() == native->frame() &&
                      source->randomState() == native->randomState() &&
                      source->player().transitionTicks == native->player().transitionTicks,
                  "AoF 200 Hz source clock/RNG drift");
            const auto randomBefore = native->randomState();
            const auto shipBefore = native->entities().at(89);
            native->renderNative();
            native->renderNative();
            check(native->randomState() == randomBefore &&
                      native->entities().at(89).x == shipBefore.x &&
                      native->entities().at(89).y == shipBefore.y,
                  "AoF presentation mutates simulation");
        }
        std::cout << "AoF 50-stage startup, source rules, continuity, eight death voices, assets "
                     "and isolated saves PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
