#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include <chrono>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void confirm(Frontend &f) {
    FrontendInput i;
    i.confirm = true;
    f.step(i);
}
void click(Frontend &f, size_t n) {
    const auto r = f.menuBounds().at(n);
    FrontendInput i;
    i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
    i.pointerActive = i.pointerReleased = true;
    f.step(i);
}
void direction(Frontend &f, int x, int y = 0) {
    FrontendInput i;
    i.horizontal = x;
    i.vertical = y;
    f.step(i);
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS SCRATCH required");
        const auto root =
            std::filesystem::path(argv[2]) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        for (bool aof : {false, true}) {
            Assets assets;
            assets.load(argv[1]);
            auto f = std::make_unique<Frontend>(
                assets, root / (aof ? "aof-start" : "standard-start") / "hst.txt");
            f->editionMenu(true);
            check(f->scores().selectedPlayer == 0, "Fresh profile is not Musk Ox");
            confirm(*f);
            check(f->menuBounds().size() == 6, "Main menu misses Restart Best");
            if (aof)
                click(*f, 1);
            check(f->arseEdition() == aof, "Second title action does not switch edition");
            click(*f, 0);
            check(f->view() == FrontendView::Level,
                  "First title action does not open level selection");
            check(f->menuBounds().size() == 4, "Level 1 misses Endurance or retains restart");
            const auto start = f->menuBounds()[2], endurance = f->menuBounds()[3];
            check(endurance[1] >= start[3], "Level action layout overlaps");
            direction(*f, 1);
            check(f->menuBounds().size() == 3,
                  "Endurance visible above level 1 or restart remains");
            FrontendInput stale;
            stale.pointer = {(endurance[0] + endurance[2]) / 2, (endurance[1] + endurance[3]) / 2,
                             0, 0};
            stale.pointerReleased = stale.pointerActive = true;
            f->step(stale);
            check(f->view() == FrontendView::Level, "Hidden Endurance touch launches a game");
            direction(*f, -1);
            check(f->menuBounds().size() == 4, "Returning to level 1 does not restore Endurance");
            click(*f, 3);
            check(f->view() == FrontendView::Playing && f->game().endurance() &&
                      f->game().arse() == aof && f->game().stage() == 0 && f->game().lives() == 0,
                  "Start Endurance route incorrect");
        }
        Assets assets;
        assets.load(argv[1]);
        auto scores = original::defaultScores();
        scores.selectedPlayer = 3;
        scores.players[3].level = 3;
        scores.players[3].lives = 7;
        scores.players[3].score = 43210;
        scores.players[3].difficulty = 2;
        const auto save = root / "saved" / "hst.txt";
        scores.saveAtomic(save);
        auto f = std::make_unique<Frontend>(assets, save);
        f->editionMenu(true);
        check(f->scores().selectedPlayer == 3, "Existing selected player was overwritten");
        confirm(*f);
        click(*f, 2);
        check(f->view() == FrontendView::Playing && !f->game().endurance() &&
                  f->game().stage() == 3 && f->game().lives() == 7 &&
                  f->game().combatState().score == 43210 && f->game().combatState().difficulty == 2,
              "Restart Best fails after level navigation");
        auto g = std::make_unique<Frontend>(assets, std::filesystem::path{});
        g->editionMenu(true);
        confirm(*g);
        click(*g, 0);
        direction(*g, 0, -1);
        direction(*g, 1);
        confirm(*g);
        check(g->view() == FrontendView::Playing && g->game().endurance() && g->game().stage() == 0,
              "Horizontal input on Endurance changes level or activates another action");
        Scores fresh;
        fresh.load(root / "standard-start" / "hst.txt");
        check(fresh.selectedPlayer == 0, "Fresh Musk Ox preference did not save");
        std::cout << "Six-action title, Musk Ox default, conditional Endurance, stable Restart "
                     "Best, touch exclusion and checkpoint restore PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
