#include "gles_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/frontend.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
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
    const auto r = f.menuBounds().at(n);
    FrontendInput i;
    i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
    i.pointerActive = i.pointerReleased = true;
    f.step(i);
}
std::vector<unsigned char> pixels(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int w, h, max;
    in >> magic >> w >> h >> max;
    in.get();
    std::vector<unsigned char> data(size_t(w) * h * 3);
    in.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size()));
    check(bool(in), "Incomplete capture");
    return data;
}
int main(int argc, char **argv) {
    try {
        check(argc == 4, "ASSETS REPLAY OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Grid transition diagnostics", 720, 960,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        const auto output = std::filesystem::path(argv[3]);
        std::filesystem::create_directories(output);
        {
            GlesRenderer renderer(window, assets);
            auto owner = std::make_unique<Frontend>(assets, std::filesystem::path{});
            auto &f = *owner;
            auto capture = [&](const char *name) {
                const auto &source = f.canvas();
                auto grid = source.decorativeGrid();
                check(grid.valid, "Missing grid recipe");
                Canvas background;
                background.dimensions(source.width(), source.height());
                for (size_t n = 0; n < grid.first && n < source.draws().size(); ++n)
                    background.appendDraw(source.draws()[n]);
                grid.first = grid.last = background.draws().size();
                background.decorativeGrid(grid);
                renderer.present(background, assets, false, true, Aspect::Fit,
                                 output / (std::string(name) + "-enhanced.ppm"));
                renderer.present(background, assets, true, false, Aspect::Fit,
                                 output / (std::string(name) + "-original.ppm"));
                Canvas precise;
                drawPreciseBackground(precise, grid);
                Canvas dots;
                dots.dimensions(source.width(), source.height());
                int count = 0;
                double energy = 0;
                for (const auto &d : precise.draws())
                    if (d.patternedBeam && d.x == d.endX && d.y == d.endY) {
                        dots.appendDraw(d);
                        ++count;
                        energy += d.energy;
                    }
                renderer.present(dots, assets, false, true, Aspect::Fit,
                                 output / (std::string(name) + "-dots.ppm"));
                std::cout << name << " pattern";
                for (int v : grid.pattern)
                    std::cout << ' ' << v;
                std::cout << "; dots=" << count << " energy=" << energy << '\n';
                return grid;
            };
            confirm(f);
            capture("title-fresh");
            click(f, 2);
            click(f, 2);
            std::ifstream replay(argv[2]);
            check(bool(replay), "Missing replay");
            int x, y;
            uint32_t held;
            int rows = 0;
            InputLatch latch;
            while (replay >> x >> y >> held) {
                FrontendInput i;
                i.pointer = latch.sample(x, y, held);
                f.step(i);
                if (rows++ == 0)
                    capture("level-1");
            }
            std::cout << "Replay end stage=" << f.game().stage()
                      << " phase=" << int(f.game().phase())
                      << " cleared=" << f.game().stagesCleared() << '\n';
            check(rows == 1953 && f.game().stagesCleared() == 1 && f.game().deaths() == 2 &&
                      f.game().combatState().score == 20015,
                  "Accepted replay gameplay changed");
            for (int n = 0; n < 102; ++n) {
                FrontendInput i;
                i.pointer = {120, 270, 0, 0};
                f.step(i);
            }
            check(f.game().stage() == 1, "Replay did not progress to level 2");
            renderer.reloadPages(assets);
            const auto changed = capture("level-2");
            FrontendInput back;
            back.back = true;
            f.step(back);
            confirm(f);
            confirm(f);
            check(f.view() == FrontendView::Main, "Did not return to title");
            check(capture("title-after-level-2").pattern == changed.pattern,
                  "Title lost retained grid pattern");
            click(f, 2);
            click(f, 2);
            f.step({});
            renderer.reloadPages(assets);
            check(capture("restarted-level-1").pattern == changed.pattern,
                  "Restart changed source grid pattern");
            // Separately exercise a dense dotted recipe admitted by level-clear selection.
            Canvas dense;
            auto grid = changed;
            grid.pattern = {6, 6, 6, 6, 1, 1};
            grid.first = grid.last = 0;
            dense.decorativeGrid(grid);
            renderer.present(dense, assets, false, true, Aspect::Fit,
                             output / "dense-level-2-dots.ppm");
            grid.colors = {0x00080000u, 0x00080000u, 0};
            dense.decorativeGrid(grid);
            renderer.present(dense, assets, false, true, Aspect::Fit,
                             output / "faint-dense-dots.ppm");
            Canvas cluster, ordinary, control;
            for (int i = 0; i < 60; ++i) {
                const float x = 120.5f + (i % 15) * .002f, y = 160.5f + (i / 15) * .002f;
                cluster.beam(x, y, x, y, 0x0040, .01f, 1, true);
                cluster.markDecorativeLast();
                ordinary.beam(x, y, x, y, 0x0040, .01f, 1, true);
            }
            control.beam(120.514f, 160.503f, 120.514f, 160.503f, 0x0040, .6f, 1, true);
            control.markDecorativeLast();
            renderer.present(cluster, assets, false, true, Aspect::Fit,
                             output / "faint-cluster.ppm");
            renderer.present(control, assets, false, true, Aspect::Fit,
                             output / "summed-cluster-control.ppm");
            renderer.present(ordinary, assets, false, true, Aspect::Fit,
                             output / "ordinary-faint-points.ppm");
            const auto actual = pixels(output / "faint-cluster.ppm"),
                       summed = pixels(output / "summed-cluster-control.ppm"),
                       unchanged = pixels(output / "ordinary-faint-points.ppm");
            check(*std::max_element(actual.begin(), actual.end()) > 0,
                  "Overlapping faint decorative samples vanished before accumulation");
            for (size_t n = 0; n < actual.size(); ++n)
                check(std::abs(int(actual[n]) - int(summed[n])) <= 1,
                      "Faint-dot accumulation does not preserve summed light");
            check(*std::max_element(unchanged.begin(), unchanged.end()) == 0,
                  "Nondecorative point profile changed");
            Canvas bounded;
            for (int i = 0; i < 512; ++i) {
                float x = (i % 32) * 7 + .5f, y = (i / 32) * 12 + .5f;
                bounded.beam(x, y, x, y, 0x0040, .01f, 1, true);
                bounded.markDecorativeLast();
            }
            for (int i = 0; i < 60; ++i) {
                bounded.beam(217.5f, 180.5f, 217.5f, 180.5f, 0x0040, .01f, 1, true);
                bounded.markDecorativeLast();
            }
            renderer.present(bounded, assets, false, true, Aspect::Fit,
                             output / "bounded-faint-cluster.ppm");
            const auto batch = pixels(output / "bounded-faint-cluster.ppm");
            check(*std::max_element(batch.begin(), batch.end()) > 0,
                  "Batch limit prematurely flushed faint-dot accumulation");
            std::cout << "Level transition/title/restart and faint-dot radiance/bounded-batch "
                         "regressions PASS\n";
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
