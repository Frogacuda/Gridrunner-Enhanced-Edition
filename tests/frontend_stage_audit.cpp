// Ordinary input exploration starting through the actual source menu owner.
#include "grpp/frontend.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: frontend_stage_audit ASSETS OUTPUT_DIRECTORY");
        const std::filesystem::path output = argv[2];
        std::filesystem::create_directories(output);
        for (int strategy = 0; strategy < 8; ++strategy) {
            Assets assets;
            assets.load(argv[1]);
            auto f = std::make_unique<Frontend>(assets, std::filesystem::path{});
            FrontendInput in;
            in.confirm = true;
            f->step(in);
            auto click = [&](int index) {
                auto r = f->menuBounds().at(size_t(index));
                FrontendInput i;
                i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
                i.pointerReleased = true;
                f->step(i);
            };
            click(2);
            click(2);
            std::ofstream replay(output / ("strategy-" + std::to_string(strategy) + ".txt"));
            int ticks = 0;
            for (; ticks < 16000 && !f->game().stagesCleared() &&
                   f->game().phase() != GamePhase::GameOver;
                 ++ticks) {
                int x = 120, best = 100000;
                if (strategy < 4)
                    for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
                        const auto &e = f->game().entities().at(slot);
                        if ((e.behavior == 3 || e.behavior == 4 || e.behavior == 16 ||
                             e.behavior == 17) &&
                            (e.flags & 1) && pixel(e.y) < best) {
                            best = pixel(e.y);
                            x = std::clamp(pixel(e.x) / (strategy == 1 ? 1 : 2), 20, 220);
                        }
                    }
                else
                    x = 20 + (ticks * (strategy - 2) / 8) % 200;
                const uint32_t held = strategy == 2 && ticks % 400 < 20 ? 32u : 0u;
                int y = strategy == 3 ? 290 : 270;
                FrontendInput i;
                i.pointer = {x, y, held, 0};
                f->step(i);
                replay << x << ' ' << y << ' ' << held << '\n';
            }
            std::cout << strategy << ": " << (f->game().stagesCleared() ? "clear" : "loss")
                      << " ticks=" << ticks << " deaths=" << f->game().deaths()
                      << " score=" << f->game().combatState().score << '\n';
            if (f->game().stagesCleared())
                return 0;
        }
        return 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
