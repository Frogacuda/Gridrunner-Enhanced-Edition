#include <algorithm>
#include "gles_renderer.hpp"
#include <SDL3/SDL_main.h>
#include <fstream>
#include <iostream>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
std::vector<unsigned char> read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::string magic;
    int w, h, max;
    f >> magic >> w >> h >> max;
    f.get();
    std::vector<unsigned char> b(size_t(w) * h * 3);
    f.read(reinterpret_cast<char *>(b.data()), b.size());
    check(bool(f), "Capture read failed");
    return b;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window =
            SDL_CreateWindow("Cursor checks", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        int cases = 0;
        {
            GlesRenderer renderer(window, assets);
            Canvas canvas;
            canvas.dimensions(240, 320);
            for (bool original : {false, true})
                for (bool edge : {false, true})
                    for (int rotation : {0, 1, 2, 3}) {
                        auto capture = [&](const std::array<float, 2> *pointer, const char *name) {
                            auto path = out / name;
                            renderer.present(canvas, assets, original, true, Aspect::Fit, path, 0,
                                             rotation, edge, false, pointer);
                            return read(path);
                        };
                        const auto base = capture(nullptr, "base.ppm");
                        for (auto point : {std::array<float, 2>{20, 30}, {320, 240}, {632, 460}}) {
                            const auto pixels = capture(&point, "cursor.ppm");
                            int white = 0, different = 0;
                            int width = 0, height = 0;
                            SDL_GetWindowSizeInPixels(window, &width, &height);
                            const float scale =
                                std::clamp(SDL_GetWindowDisplayScale(window), 1.f, 3.f);
                            for (int y = 0; y < height; ++y)
                                for (int x = 0; x < width; ++x) {
                                    const auto n = (size_t(y) * width + x) * 3;
                                    if (pixels[n] != base[n] || pixels[n + 1] != base[n + 1] ||
                                        pixels[n + 2] != base[n + 2]) {
                                        check(x >= point[0] * width / 640 - 12 * scale &&
                                                  x <= point[0] * width / 640 + 12 * scale &&
                                                  y >= point[1] * height / 480 - 12 * scale &&
                                                  y <= point[1] * height / 480 + 12 * scale,
                                              "Cursor rotated, shifted or affected other pixels");
                                        ++different;
                                        if (pixels[n] > 240 && pixels[n + 1] > 240 &&
                                            pixels[n + 2] > 240)
                                            ++white;
                                    }
                                }
                            check(different > 10 && white > 5,
                                  "Pointer missing from actual framebuffer");
                            check(capture(nullptr, "restored.ppm") == base,
                                  "Cursor remained in gameplay framebuffer");
                            ++cases;
                        }
                    }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << cases
                  << " cursor pixel checks PASS: graphics, view, rotation, margins, edge clipping "
                     "and removal\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
