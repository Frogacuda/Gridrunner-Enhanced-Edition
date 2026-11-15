#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void confirm(Frontend &f) {
    FrontendInput i;
    i.confirm = true;
    f.step(i);
}
void down(Frontend &f, int count = 1) {
    for (int n = 0; n < count; ++n) {
        FrontendInput i;
        i.vertical = 1;
        f.step(i);
    }
}
void back(Frontend &f) {
    FrontendInput i;
    i.back = true;
    f.step(i);
}
void click(Frontend &f, size_t choice) {
    auto r = f.menuBounds().at(choice);
    FrontendInput i;
    i.pointer = {((r[0] + r[2]) / 2), ((r[1] + r[3]) / 2), 0, 0};
    i.pointerActive = true;
    i.pointerReleased = true;
    f.step(i);
}
int main(int argc, char **argv) {
    try {
        require(argc == 4, "Usage: frontend_checks ASSETS SCRATCH_DIRECTORY MENU_STAGE_REPLAY");
        Assets assets;
        assets.load(argv[1]);
        const auto directory =
            std::filesystem::path(argv[2]) /
            ("grpp-profiles-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto save = directory / "hst.txt";
        auto owner = std::make_unique<Frontend>(assets, save);
        auto &f = *owner;
        require(f.view() == FrontendView::Splash, "No splash");
        confirm(f);
        require(f.view() == FrontendView::Main, "Splash/main transition failed");
        confirm(f);
        require(f.view() == FrontendView::About, "Information option failed");
        for (int doc = 0; doc < 4; ++doc) {
            click(f, size_t(doc));
            require(f.view() == FrontendView::Reader, "Information page unavailable");
            for (int n = 0; n < 30 && f.view() == FrontendView::Reader; ++n)
                click(f, 1);
            require(f.view() == FrontendView::About, "Information reader did not finish");
        }
        back(f);
        down(f);
        confirm(f);
        require(f.view() == FrontendView::Players, "Slots missing");
        confirm(f);
        require(f.view() == FrontendView::Slot, "Slot options missing");
        confirm(f);
        require(f.view() == FrontendView::Name, "Name editor missing");
        FrontendInput letter;
        letter.letter = 'Z';
        f.step(letter);
        click(f, 2);
        letter.letter = 'A';
        f.step(letter);
        click(f, 4);
        require(f.scores().players[0].name == "ZA", "Source name cursor operations failed");
        if (!f.saveError().empty())
            throw std::runtime_error(f.saveError());
        Scores loaded;
        loaded.load(save);
        require(loaded.players[0].name == "ZA", "Name did not persist");
        confirm(f);
        down(f);
        confirm(f);
        require(f.view() == FrontendView::Clear, "Clear confirmation missing");
        click(f, 1);
        require(f.scores().players[0].name == "ZA", "Declining clear erased slot");
        confirm(f);
        down(f);
        confirm(f);
        confirm(f);
        require(f.scores().players[0].name == "MUSK_OX" && f.scores().players[0].lives == 0,
                "Confirmed clear differs from source reset");
        back(f);
        click(f, 2);
        require(f.view() == FrontendView::Level, "New-game level selector missing");
        FrontendInput left;
        left.horizontal = -1;
        f.step(left);
        click(f, 2);
        require(f.view() == FrontendView::Playing && f.game().stage() == 4 && f.game().lives() == 3,
                "Unlocked-level wrap or new-game state wrong");
        back(f);
        require(f.view() == FrontendView::HighScores, "Aborted run did not enter high scores");
        confirm(f);
        confirm(f);
        click(f, 1);
        click(f, 4);
        require(f.scores().selectedPlayer == 4 && f.view() == FrontendView::Level,
                "Guest route unavailable");
        click(f, 2);
        require(f.game().player().selectedPlayer == 4, "Guest craft not selected");
        back(f);
        f.flush();
        loaded.load(save);
        require(loaded.selectedPlayer == 4, "Guest selection cannot reload");
        // Real save/resume round trip uses the source's best-checkpoint comparison.
        auto scores = original::defaultScores();
        require(scores.recordCheckpoint(2, 7, 3, 123456, 2), "Better checkpoint rejected");
        require(!scores.recordCheckpoint(2, 6, 49, 999999, 9),
                "Fewer lives displaced best checkpoint");
        require(!scores.recordCheckpoint(2, 7, 3, 999999, 9), "Tied checkpoint displaced best");
        scores.selectedPlayer = 2;
        scores.saveAtomic(save);
        {
            std::ifstream file(save, std::ios::binary);
            const std::string before{std::istreambuf_iterator<char>(file), {}};
            auto blocked = save;
            blocked += ".tmp";
            std::filesystem::create_directory(blocked);
            bool failed = false;
            try {
                scores.saveAtomic(save);
            } catch (const std::runtime_error &) {
                failed = true;
            }
            require(failed, "Blocked temporary save unexpectedly committed");
            std::ifstream afterFile(save, std::ios::binary);
            const std::string after{std::istreambuf_iterator<char>(afterFile), {}};
            require(before == after, "Failed save altered valid profile data");
            std::filesystem::remove(blocked);
        }
        auto resumed = std::make_unique<Frontend>(assets, save);
        confirm(*resumed);
        click(*resumed, 3);
        require(resumed->game().stage() == 3 && resumed->game().lives() == 7 &&
                    resumed->game().combatState().score == 123456 &&
                    resumed->game().combatState().difficulty == 2,
                "Resume did not restore complete checkpoint");
        back(*resumed);
        resumed->flush();
        require(resumed->scores().normal[0].name == "GOATY" &&
                    resumed->scores().normal[0].points == 123456,
                "Session score/name did not enter the high-score table");
        require(std::filesystem::exists(save.string() + ".bak"), "No previous valid save retained");
        {
            std::ofstream corrupt(save);
            corrupt << "truncated";
        }
        auto recovered = std::make_unique<Frontend>(assets, save);
        require(recovered->scores().selectedPlayer == 2 && !recovered->saveError().empty(),
                "Valid backup not recovered");
        // Stage 50 is a legitimate serialized checkpoint at the campaign boundary.
        // Resume must show the original ending before wrapping and rebuilding atlas 5.
        scores = original::defaultScores();
        scores.players[0].level = 50;
        scores.players[0].lives = 7;
        scores.players[0].difficulty = 2;
        scores.saveAtomic(save);
        auto campaign = std::make_unique<Frontend>(assets, save);
        confirm(*campaign);
        click(*campaign, 3);
        require(campaign->view() == FrontendView::Campaign && assets.page(5).height == 256,
                "Campaign notice/art missing on boundary resume");
        for (int page = 0; page < 10 && campaign->view() == FrontendView::Campaign; ++page)
            confirm(*campaign);
        campaign->step({});
        require(campaign->view() == FrontendView::Playing && campaign->game().stage() == 0 &&
                    campaign->game().combatState().difficulty == 4 &&
                    campaign->game().player().alternateCraftUnlocked &&
                    assets.page(5).height == 512,
                "Campaign continuation failed to restore gameplay atlas/state");
        const auto stageSave = directory / "stage-hst.txt";
        auto stageOwner = std::make_unique<Frontend>(assets, stageSave);
        confirm(*stageOwner);
        click(*stageOwner, 2);
        click(*stageOwner, 2);
        std::ifstream stageInput(argv[3]);
        require(bool(stageInput), "Missing accepted menu-to-stage input replay");
        int x, y, count = 0;
        uint32_t held;
        while (stageInput >> x >> y >> held) {
            FrontendInput i;
            i.pointer = {x, y, held, 0};
            stageOwner->step(i);
            ++count;
        }
        require(stageInput.eof() && count == 1953 && stageOwner->game().stagesCleared() == 1 &&
                    stageOwner->game().deaths() == 2 &&
                    stageOwner->game().combatState().score == 20015,
                "Menu-to-stage replay changed");
        require(stageOwner->scores().underworld[0].name == "MUSK_OX" &&
                    stageOwner->scores().underworld[0].points > 1000,
                "Stage record not owned by selected player");
        for (int i = 0; i < 101; ++i) {
            FrontendInput input;
            input.pointer = {120, 270, 0, 0};
            stageOwner->step(input);
        }
        require(stageOwner->game().stage() == 1 && stageOwner->game().phase() == GamePhase::Playing,
                "Menu-origin session did not enter stage two");
        Scores savedStage;
        savedStage.load(stageSave);
        require(savedStage.underworld[0].points == stageOwner->scores().underworld[0].points &&
                    savedStage.players[0].lives == 3 && savedStage.players[0].level == 0,
                "Stage records or lives-first checkpoint ownership did not persist");
        // A fresh ordinary-input full loss exercises real death/game-over ownership.
        auto loss = std::make_unique<Frontend>(assets, std::filesystem::path{});
        confirm(*loss);
        click(*loss, 2);
        click(*loss, 2);
        int ticks = 0;
        for (; ticks < 20000 && loss->game().phase() != GamePhase::GameOver; ++ticks) {
            FrontendInput i;
            i.pointer = {120, 160, 0, 0};
            loss->step(i);
        }
        require(loss->game().phase() == GamePhase::GameOver && loss->game().lives() == -1,
                "Ordinary-input loss did not exhaust lives");
        confirm(*loss);
        require(loss->view() == FrontendView::HighScores, "Game over did not submit score");
        confirm(*loss);
        confirm(*loss);
        click(*loss, 4);
        require(loss->view() == FrontendView::Farewell, "Exit bypasses farewell");
        confirm(*loss);
        require(loss->quitRequested(), "Farewell cannot exit");
        std::cout << "Menu branches, four documents, name/clear/guest, durable profiles, resume, "
                     "backup recovery and ordinary-input game over passed; "
                  << ticks << " loss ticks\n";
        // Remove only the explicitly created individual test files.
        for (const auto &name : {"hst.txt", "hst.txt.tmp", "hst.txt.bak", "stage-hst.txt",
                                 "stage-hst.txt.tmp", "stage-hst.txt.bak"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
