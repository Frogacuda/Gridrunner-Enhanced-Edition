#include "grpp/game.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace grpp;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Usage: stage_checks ASSETS INPUT_REPLAY");
        Assets assets;
        assets.load(argv[1]);
        auto gameOwner = std::make_unique<Game>(assets);
        auto &game = *gameOwner;
        game.start();
        InputLatch latch;
        std::ifstream replay(argv[2]);
        require(bool(replay), "Cannot open accepted movement replay");
        int x, y, frames = 0;
        uint32_t held;
        while (replay >> x >> y >> held) {
            require(x >= 0 && x < 240 && y >= 0 && y < 320, "Replay target outside logical screen");
            game.step(latch.sample(x, y, held));
            ++frames;
        }
        require(replay.eof(), "Malformed replay");
        require(frames == 1849 && game.stagesCleared() == 1 &&
                    game.phase() == GamePhase::StageClear,
                "Stage one did not clear at corrected replay boundary");
        require(game.deaths() == 2 && game.combatState().score == 22435,
                "Death/retry/scoring trace changed");
        std::cout << "Stage 1 completed: " << frames << " frames; two deaths; score "
                  << game.combatState().score << '\n';
        for (int j = 0; j < 150; ++j)
            game.step(latch.sample(120, 270, 0));
        require(game.stage() == 1 && game.phase() == GamePhase::Playing,
                "Next stage failed to start");
        auto lossOwner = std::make_unique<Game>(assets);
        auto &loss = *lossOwner;
        loss.start();
        int ticks = 0;
        while (loss.phase() != GamePhase::GameOver && ticks < 20000) {
            loss.step({120, 160, 0, 0});
            ++ticks;
        }
        std::cout << "Loss run: phase " << int(loss.phase()) << " deaths " << loss.deaths()
                  << " lives " << loss.lives() << " cleared " << loss.stagesCleared() << '\n';
        require(loss.phase() == GamePhase::GameOver && loss.deaths() >= 4 && loss.lives() == -1,
                "Lives exhaustion did not reach game over");
        for (int i = 0; i < 100; ++i)
            loss.step({120, 160, 0, 0});
        loss.start();
        loss.step({120, 240, 0, 0});
        require(loss.phase() == GamePhase::Playing && loss.lives() == 3, "Restart failed");
        std::cout << "Next stage, game over (" << ticks
                  << " frames), animated game-over screen and restart passed\n";
        auto beamOwner = std::make_unique<Game>(assets);
        auto &beam = *beamOwner;
        beam.start();
        bool activated = false, targeted = false;
        for (int i = 0; i < 240; ++i) {
            beam.step({120, 270, i >= 100 && i < 120 ? 32u : 0u, 0});
            activated |=
                std::find(beam.sounds().begin(), beam.sounds().end(), 27) != beam.sounds().end();
            targeted |= beam.player().beamMode > 0;
        }
        require(activated && targeted, "Ordinary bomb input failed to activate/target the beam");
        std::cout << "Bomb input and HUD beam targeting passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
