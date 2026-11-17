#include "grpp/frontend.hpp"
#ifdef GRPP_REPLAY_RENDER_CHECKS
#include "gles_renderer.hpp"
#include <SDL3/SDL_main.h>
#endif
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void choose(Frontend &f, int n) {
    FrontendInput i;
    i.vertical = 1;
    for (int k = 0; k < n; ++k)
        f.step(i);
    i = {};
    i.confirm = true;
    f.step(i);
}
void start(Frontend &f) {
    choose(f, 0);
    choose(f, 0);
    choose(f, 1);
    check(f.view() == FrontendView::Playing, "Did not start");
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "Usage: replay_checks ASSETS SCRATCH");
        auto dir = std::filesystem::path(argv[2]) /
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Assets assets;
        assets.load(argv[1]);
#ifdef GRPP_REPLAY_RENDER_CHECKS
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window =
            SDL_CreateWindow("Replay menus", 720, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        auto renderer = std::make_unique<GlesRenderer>(window, assets);
        auto capture = [&](Frontend &front, const char *name) {
            front.step({});
            renderer->reloadPages(assets);
            renderer->present(front.canvas(), assets, false, true, Aspect::Fit,
                              dir / (std::string(name) + ".ppm"));
        };
#else
        auto capture = [](Frontend &, const char *) {};
#endif

        for (bool aof : {false, true})
            for (int rate : {3, 6}) {
                auto original = std::make_unique<Game>(assets),
                     restored = std::make_unique<Game>(assets);
                original->arse(aof);
                original->setNativeRate(true, rate);
                original->deferPresentation(true);
                original->stageBackgrounds(true);
                original->start(24);
                ReplayRecording r;
                r.begin(*original, 2);
                for (int t = 0; t < 1800; ++t) {
                    LogicalInput in{(t * 17) % (aof ? 400 : 240), (t * 7) % (aof ? 400 : 320),
                                    t % 90 == 0     ? 32u
                                    : t % 211 < 100 ? 1u
                                                    : 0u,
                                    t % 90 == 0 ? 32u : 0u};
                    if (t == 203 || t == 999) {
                        original->cancelPendingInput();
                        r.resetInput();
                    }
                    r.append(in);
                    original->step(in);
                    r.reachedStage(original->stage());
                    if (t == 1000)
                        r.checkpoint(*original);
                }
                r.finish(original->combatState().score, false);
                auto path = dir / (aof ? "aof.grr" : "std.grr");
                r.save(path);
                auto info = ReplayRecording::play(*restored, path);
                check(info.ticks == 1800 && info.profile == 2 && info.maxStage >= 25,
                      "Replay metadata changed");
                check(ReplayState::capture(*original) == ReplayState::capture(*restored),
                      "Restored simulation diverged");
                auto bytes = std::filesystem::file_size(path);
                std::cout << (aof ? "AoF" : "Standard") << " replay: " << bytes << " bytes\n";
                auto corrupt = dir / "corrupt.grr";
                std::filesystem::copy_file(path, corrupt,
                                           std::filesystem::copy_options::overwrite_existing);
                {
                    std::fstream f(corrupt, std::ios::in | std::ios::out | std::ios::binary);
                    f.seekp(45);
                    f.put('X');
                }
                bool rejected = false;
                try {
                    ReplayRecording::play(*restored, corrupt);
                } catch (...) {
                    rejected = true;
                }
                check(rejected, "Corrupt replay accepted");
            }
        auto f = std::make_unique<Frontend>(assets, dir / "frontend/hst.txt");
        f->editionMenu(true);
        f->setNativeRate(true, 6);
        f->deferPresentation(true);
        f->enableReplays(true);
        start(*f);
        for (int t = 0; t < 600; ++t) {
            FrontendInput in;
            in.pointer = {120 + t % 40, 240, 0, 0};
            f->step(in);
        }
        check(f->replay().active() && f->replay().info().ticks == 600,
              "Automatic recording failed");
        f->cancelInput();
        check(f->replay().info().ticks == 600, "Pause reset added time");
        FrontendInput back;
        back.back = true;
        f->step(back);
        check(f->view() == FrontendView::ReplayPrompt, "Quit did not prompt");
        check(!std::filesystem::exists(dir / "frontend/replays"), "Unsaved replay wrote to disk");
        capture(*f, "save-prompt");
        auto pendingScore = f->replay().info().score;
        choose(*f, 0);
        check(f->view() == FrontendView::ReplaySlots && f->menuBounds().size() == 11,
              "Ten slots missing");
        capture(*f, "save-slots");
        auto slot = f->replayPath(0);
        choose(*f, 0);
        check(std::filesystem::exists(slot) && f->view() == FrontendView::HighScores,
              "Slot save failed");
        check(ReplayRecording::inspect(slot).score == pendingScore,
              "High-score bookkeeping changed replay score");
        // A second run in the same process has menu/previous-game RNG and residual state.
        choose(*f, 0);
        start(*f);
        for (int t = 0; t < 600; ++t) {
            FrontendInput in;
            in.pointer = {30 + t % 190, 210, t % 89 == 0 ? 32u : 0u, 0};
            f->step(in);
        }
        auto expected = ReplayState::capture(const_cast<Game &>(f->game()));
        f->step(back);
        choose(*f, 0);
        capture(*f, "occupied-slots");
        choose(*f, 0);
        check(f->view() == FrontendView::ReplayOverwrite, "Occupied slot did not ask");
        capture(*f, "overwrite-prompt");
        choose(*f, 0);
        auto decoded = std::make_unique<Game>(assets);
        ReplayRecording::play(*decoded, slot);
        check(ReplayState::capture(*decoded) == expected,
              "Frontend starting state did not reconstruct");
        // Reach playback exclusively through the shipped profile menus.
        choose(*f, 0); // High scores -> splash.
        choose(*f, 0); // Main.
        choose(*f, 4); // Save Slots/Replays.
        choose(*f, 0); // Profile zero.
        check(f->view() == FrontendView::Slot && f->menuBounds().size() == 6,
              "Watch option missing above Exit");
        capture(*f, "profile-watch-option");
        auto saveBefore = [&]() {
            std::ifstream stream(dir / "frontend/hst.txt", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream), {});
        }();
        choose(*f, 4);
        check(f->view() == FrontendView::ReplayBrowse && f->menuBounds().size() == 11,
              "Replay browser missing");
        capture(*f, "watch-slots");
        choose(*f, 1); // Empty slot must stay in browser.
        check(f->view() == FrontendView::ReplayBrowse && !f->saveError().empty(),
              "Empty replay slot left browser");
        choose(*f, 10); // Selection was one, wrap around to zero.
        check(f->watchingReplay(), "Saved replay did not start");
        ReplayPlayback paced;
        auto reference = std::make_unique<Game>(assets);
        paced.load(*reference, slot);
        for (int t = 0; t < 599; ++t) {
            FrontendInput unwanted;
            unwanted.pointer = {1, 1, 0x61, 0x61};
            f->step(unwanted);
            check(paced.step(*reference), "Paced replay ended early");
        }
        check(ReplayState::capture(const_cast<Game &>(f->game())) ==
                  ReplayState::capture(*reference),
              "Watch playback differs from verifier or accepts live input");
        f->step({});
        check(f->view() == FrontendView::ReplayBrowse && f->saveError().empty(),
              "Playback did not finish in browser");
        check(!f->replay().active() && !f->replay().pending(), "Playback recorded a new run");
        std::ifstream savedAfter(dir / "frontend/hst.txt", std::ios::binary);
        check(std::string(std::istreambuf_iterator<char>(savedAfter), {}) == saveBefore,
              "Playback changed profile or high scores");
        choose(*f, 0);
        f->step(back);
        check(f->view() == FrontendView::ReplayBrowse, "Back did not stop playback");
        std::filesystem::copy_file(dir / "corrupt.grr", f->replayPath(2));
        choose(*f, 2);
        check(f->view() == FrontendView::ReplayBrowse && !f->saveError().empty(),
              "Damaged replay was accepted by menu");
        f->step(back);
        choose(*f, 5); // Slot Exit -> main.
        choose(*f, 0); // Level.
        choose(*f, 1); // Start.
        FrontendInput warmup;
        warmup.pointer = {120, 240, 0, 0};
        f->step(warmup);
        f->step(back);
        choose(*f, 1); // Discard -> high scores.

        // Profile namespaces are independent, with no automatic storage before a choice.
        ReplayRecording a, b;
        auto g = std::make_unique<Game>(assets);
        g->setNativeRate(true, 6);
        g->start();
        a.begin(*g, 0);
        a.reachedStage(49);
        a.reachedStage(0);
        check(a.info().maxStage == 50, "Highest stage lost after campaign wrap");
        b.begin(*g, 1);
        check(a.info().profile != b.info().profile, "Profiles conflated");
        choose(*f, 0);
        start(*f);
        FrontendInput in;
        in.pointer = {120, 240, 0, 0};
        f->step(in);
        f->step(back);
        choose(*f, 1);
        check(!f->replay().pending() && f->view() == FrontendView::HighScores,
              "No did not discard");
        // Newest run is independent of the prior unsaved run; selecting No leaves storage alone.
        choose(*f, 0);
        start(*f);
        f->step(in);
        check(f->replay().info().ticks == 1, "Newest run retained old ticks");
        check(!f->requestQuit() && f->view() == FrontendView::ReplayPrompt,
              "Window close did not prompt");
        choose(*f, 1);
        check(f->quitRequested() && !f->replay().pending(), "Close/No did not discard and exit");
        // An actual terminal run triggers the prompt without requiring another confirmation.
        auto terminal = std::make_unique<Frontend>(assets, dir / "terminal/hst.txt");
        terminal->editionMenu(true);
        terminal->setNativeRate(true, 6);
        terminal->deferPresentation(true);
        terminal->enableReplays(true);
        start(*terminal);
        for (int n = 0; n < 60000 && terminal->view() == FrontendView::Playing; ++n) {
            FrontendInput i;
            i.pointer = {120, 240, 0, 0};
            terminal->step(i);
        }
        check(terminal->view() == FrontendView::ReplayPrompt && terminal->replay().info().completed,
              "Natural Game Over did not prompt");
        auto completePath = terminal->replayPath(9);
        choose(*terminal, 0);
        choose(*terminal, 9);
        check(std::filesystem::exists(completePath), "Slot ten did not save");
        auto completed = std::make_unique<Game>(assets);
        check(ReplayRecording::play(*completed, completePath).completed, "Completed replay failed");
        auto scores = terminal->scores();
        scores.selectedPlayer = 1;
        scores.saveAtomic(dir / "terminal/hst.txt");
        auto other = std::make_unique<Frontend>(assets, dir / "terminal/hst.txt");
        other->editionMenu(true);
        other->setNativeRate(true, 6);
        other->deferPresentation(true);
        other->enableReplays(true);
        start(*other);
        other->step(in);
        other->step(back);
        auto otherPath = other->replayPath(9);
        check(otherPath != completePath && !std::filesystem::exists(otherPath),
              "Replay slots shared between profiles");
        choose(*other, 1);
        // A storage failure leaves the recording pending and existing slots intact.
        {
            std::ofstream block(dir / "terminal/replays/profile-1");
            block << "not a directory";
        }
        choose(*other, 0);
        start(*other);
        other->step(in);
        other->step(back);
        choose(*other, 0);
        choose(*other, 0);
        check(other->view() == FrontendView::ReplaySlots && other->replay().pending() &&
                  !other->saveError().empty(),
              "Save failure discarded recording");
        check(std::filesystem::exists(completePath), "Save failure removed another profile's slot");
        std::cout << "Replay checkpoint, delta inputs, reset markers, corruption, prompts, slots "
                     "and discard pass\n";
#ifdef GRPP_REPLAY_RENDER_CHECKS
        renderer.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
