#include "gles_renderer.hpp"
#include <SDL3/SDL_main.h>
#include <fstream>
#include <iostream>
#include <vector>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
struct Pixels {
    int w, h;
    std::vector<unsigned char> rgb;
};
Pixels read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    Pixels p;
    int max;
    in >> magic >> p.w >> p.h >> max;
    in.get();
    p.rgb.resize(size_t(p.w) * p.h * 3);
    in.read(reinterpret_cast<char *>(p.rgb.data()), std::streamsize(p.rgb.size()));
    check(bool(in), "Incomplete capture");
    return p;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Draw to Edge checks", 1280, 720,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        const auto output = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(output);
        int cases = 0;
        {
            GlesRenderer renderer(window, assets);
            for (bool aof : {false, true})
                for (auto size : {std::array<int, 2>{1280, 720}, std::array<int, 2>{720, 1280}})
                    for (int rotation : {0, 1, 2})
                        for (bool reference : {false, true}) {
                            check(SDL_SetWindowSize(window, size[0], size[1]), SDL_GetError());
                            SDL_PumpEvents();
                            Canvas control;
                            const int w = aof ? 400 : 240, h = aof ? 400 : 320;
                            control.dimensions(w, h);
                            control.playfield({true, w * 2, h * 2, 0, 0});
                            DecorativeGrid grid;
                            grid.valid = true;
                            grid.width = w;
                            grid.height = h;
                            grid.cameraY = w;
                            grid.projection = aof ? 524288 : 262144;
                            grid.pattern = {3, 3, 3, 3, 0, 0};
                            grid.colors = {0x20408000u, 0x20408000u, 0x20408000u};
                            control.decorativeGrid(grid);
                            Canvas actors = control;
                            for (auto p : {std::array<int, 2>{-24, h / 2},
                                           {w + 24, h / 2},
                                           {w / 2, -24},
                                           {w / 2, h + 24}})
                                actors.sprite(assets, 64, p[0], p[1], {true, 0xffe0, 0});
                            const auto name = std::string(aof ? "aof" : "standard") + "-" +
                                              std::to_string(size[0]) + "-r" +
                                              std::to_string(rotation) +
                                              (reference ? "-original" : "-enhanced");
                            auto capture = [&](const Canvas &c, bool edge,
                                               const std::string &suffix) {
                                const auto path = output / (name + suffix + ".ppm");
                                renderer.present(c, assets, reference, true, Aspect::Fit, path, 0,
                                                 rotation, edge, true);
                                return read(path);
                            };
                            const auto before = capture(actors, false, "-letterbox"),
                                       base = capture(control, true, "-control"),
                                       edge = capture(actors, true, "-edge"),
                                       after = capture(actors, false, "-restored");
                            check(before.rgb == after.rgb,
                                  "Returning to Letterbox changed original pixels");
                            const auto v =
                                makeViewport(rotation ? edge.h : edge.w, rotation ? edge.w : edge.h,
                                             Aspect::Fit, w, h);
                            int differences = 0;
                            for (int y = 0; y < edge.h; ++y)
                                for (int x = 0; x < edge.w; ++x) {
                                    auto p = orientationPointer(float(x), float(y), edge.w, edge.h,
                                                                rotation);
                                    if (p[0] >= v.x - 16 && p[0] < v.x + v.width + 16 &&
                                        p[1] >= v.y - 16 && p[1] < v.y + v.height + 16)
                                        continue;
                                    const auto n = (size_t(y) * edge.w + x) * 3;
                                    if (edge.rgb[n] != base.rgb[n] ||
                                        edge.rgb[n + 1] != base.rgb[n + 1] ||
                                        edge.rgb[n + 2] != base.rgb[n + 2])
                                        ++differences;
                                }
                            check(differences > 8, "Actors were cropped at the play-area boundary");
                            ++cases;
                            Canvas world;
                            world.dimensions(w, h);
                            world.playfield({true, w * 2, h * 2, w * .9, h * .9});
                            const auto bounds = capture(world, true, "-scrolling-world");
                            const float right = float(v.x + w * 1.1 * v.width / w),
                                        bottom = float(v.y + h * 1.1 * v.height / h);
                            int edgePixels = 0;
                            for (int y = 0; y < bounds.h; ++y)
                                for (int x = 0; x < bounds.w; ++x) {
                                    const auto p = orientationPointer(float(x), float(y), bounds.w,
                                                                      bounds.h, rotation);
                                    if ((std::abs(p[0] - right) < 3 ||
                                         std::abs(p[1] - bottom) < 3) &&
                                        bounds.rgb[(size_t(y) * bounds.w + x) * 3 + 1] > 0)
                                        ++edgePixels;
                                }
                            check(edgePixels > 4, "World boundary did not follow actual pan "
                                                  "through renderer/orientation");
                        }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout
            << cases
            << " Draw to Edge cases pass: two editions, landscape/portrait, all orientations, both "
               "graphics, exterior actors and exact Letterbox restoration\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
