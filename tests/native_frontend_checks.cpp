#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include <chrono>
#include <iostream>
#include <memory>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
void confirm(Frontend &f) {
    FrontendInput i;
    i.confirm = true;
    f.step(i);
}
void back(Frontend &f) {
    FrontendInput i;
    i.back = true;
    f.step(i);
}
void click(Frontend &f, size_t n) {
    const auto r = f.menuBounds().at(n);
    FrontendInput i;
    i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
    i.pointerReleased = true;
    f.step(i);
}
int main(int argc, char **argv) {
    try {
        require(argc == 3 || argc == 4, "Usage: native_frontend_checks ASSETS SCRATCH");
        const int substeps = argc == 4 ? 6 : 3;
        Assets assets;
        assets.load(argv[1]);
        const auto dir =
            std::filesystem::path(argv[2]) /
            ("native-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        const auto file = dir / "hst.txt";
        auto scores = original::defaultScores();
        scores.selectedPlayer = 2;
        scores.players[2].level = 3;
        scores.players[2].lives = 7;
        scores.players[2].score = 123456;
        scores.players[2].difficulty = 2;
        scores.saveAtomic(file);
        auto resumed = std::make_unique<Frontend>(assets, file);
        resumed->setNativeRate(true, substeps);
        confirm(*resumed);
        click(*resumed, 3);
        require(resumed->view() == FrontendView::Playing && resumed->game().nativeRate() &&
                    resumed->game().stage() == 3 && resumed->game().combatState().score == 123456,
                "Native resume lost checkpoint or mode");
        FrontendInput in;
        in.pointer = {120, 270, 0, 0};
        resumed->step(in);
        require(resumed->game().nativePhase() == 1, "Native frontend did not use 10 ms updates");
        back(*resumed);
        resumed->flush();
        Scores saved;
        saved.load(file);
        require(saved.normal[0].points == 123456 && saved.selectedPlayer == 2,
                "Aborting a partial source interval corrupted score ownership");
        scores = original::defaultScores();
        scores.players[0].level = 50;
        scores.players[0].lives = 7;
        scores.players[0].difficulty = 2;
        scores.saveAtomic(file);
        auto campaign = std::make_unique<Frontend>(assets, file);
        campaign->setNativeRate(true, substeps);
        confirm(*campaign);
        click(*campaign, 3);
        require(campaign->view() == FrontendView::Campaign, "Native campaign notice missing");
        for (int page = 0; page < 10 && campaign->view() == FrontendView::Campaign; ++page)
            confirm(*campaign);
        for (int i = 0; i < substeps; ++i)
            campaign->step(in);
        require(campaign->view() == FrontendView::Playing && campaign->game().stage() == 0 &&
                    campaign->game().combatState().difficulty == 4 &&
                    campaign->game().player().alternateCraftUnlocked &&
                    assets.page(5).height == 512,
                "Native campaign wrap changed unlock/difficulty/atlas");
        auto loss = std::make_unique<Frontend>(assets, std::filesystem::path{});
        loss->setNativeRate(true, substeps);
        confirm(*loss);
        click(*loss, 2);
        click(*loss, 2);
        int steps = 0;
        in.pointer = {120, 160, 0, 0};
        for (; steps < 60000 && loss->game().phase() != GamePhase::GameOver; ++steps)
            loss->step(in);
        std::cout << "Loss result: phase=" << int(loss->game().phase())
                  << " lives=" << loss->game().lives() << " deaths=" << loss->game().deaths()
                  << " clears=" << loss->game().stagesCleared() << " steps=" << steps << "\n";
        require(loss->game().phase() == GamePhase::GameOver && loss->game().lives() == -1 &&
                    loss->game().deaths() == 4 + loss->game().stagesCleared(),
                "Native death/retry/lives ownership failed");
        confirm(*loss);
        require(loss->view() == FrontendView::HighScores,
                "Native game over did not finish score submission");
        confirm(*loss);
        confirm(*loss);
        click(*loss, 2);
        click(*loss, 2);
        require(loss->game().nativePhase() == 0, "New run retained a partial native interval");
        loss->step(in);
        require(loss->game().nativePhase() == 1, "New native run failed to restart");
        for (const auto &name : {"hst.txt", "hst.txt.bak", "hst.txt.tmp"})
            std::filesystem::remove(dir / name);
        std::filesystem::remove(dir);
        std::cout << "Native frontend: saved checkpoint, partial-tick abort, high scores, campaign "
                     "wrap, full loss ("
                  << steps << " motion steps), and restart pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
