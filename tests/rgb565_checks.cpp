#include "gles_renderer.hpp"
#include "grpp/game.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <fstream>
#include <glad/gl.h>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
PFNGLTEXIMAGE2DPROC realImage;
PFNGLTEXSUBIMAGE2DPROC realSub;
PFNGLDRAWARRAYSPROC realDraw;
int drawCount = 0;
void GLAD_API_PTR drawHook(GLenum mode, GLint first, GLsizei count) {
    ++drawCount;
    realDraw(mode, first, count);
}
int images565 = 0, subs565 = 0;
bool failImage = false, failSub = false;
void GLAD_API_PTR imageHook(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                            GLint border, GLenum format, GLenum type, const void *pixels) {
    if (type == GL_UNSIGNED_SHORT_5_6_5) {
        ++images565;
        if (failImage) {
            failImage = false;
            format = 0;
        }
    }
    realImage(target, level, internal, w, h, border, format, type, pixels);
}
void GLAD_API_PTR subHook(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                          GLenum format, GLenum type, const void *pixels) {
    if (type == GL_UNSIGNED_SHORT_5_6_5) {
        ++subs565;
        if (failSub) {
            failSub = false;
            format = 0;
        }
    }
    realSub(target, level, x, y, w, h, format, type, pixels);
}
struct Hooks {
    Hooks() {
        realImage = glad_glTexImage2D;
        realSub = glad_glTexSubImage2D;
        realDraw = glad_glDrawArrays;
        glad_glTexImage2D = imageHook;
        glad_glTexSubImage2D = subHook;
        glad_glDrawArrays = drawHook;
    }
    ~Hooks() {
        glad_glTexImage2D = realImage;
        glad_glTexSubImage2D = realSub;
        glad_glDrawArrays = realDraw;
    }
};
std::vector<unsigned char> read(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w, h, m;
    f >> magic >> w >> h >> m;
    f.get();
    std::vector<unsigned char> p(size_t(w) * h * 3);
    f.read(reinterpret_cast<char *>(p.data()), p.size());
    check(bool(f), "Capture read failed");
    return p;
}
void closePixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    check(a.size() == b.size(), "Capture size changed");
    for (size_t i = 0; i < a.size(); ++i)
        check(std::abs(int(a[i]) - int(b[i])) <= 1,
              "RGB565 changed a pixel beyond one channel code");
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS OUTPUT required");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window =
            SDL_CreateWindow("RGB565 checks", 256, 256, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        int scenes = 0;
        Canvas palette;
        palette.dimensions(256, 256);
        for (int n = 0; n < 65536; ++n)
            palette.rawFill({n % 256, n / 256, 1, 1}, uint16_t(n));
        {
            GlesRenderer renderer(window, assets);
            Hooks hooks;
            SDL_GL_SetSwapInterval(0);
            auto capture = [&](const Canvas &c, bool enabled, bool reference = false,
                               int rotation = 0, bool edge = false) {
                renderer.rgb565Uploads(enabled);
                auto p = out / "frame.ppm";
                renderer.present(c, assets, reference, false, Aspect::Fit, p, 0, rotation, edge,
                                 false);
                return read(p);
            };
            auto p = out / "default.ppm";
            renderer.present(palette, assets, true, false, Aspect::Fit, p);
            check(images565 == 1, "Default did not allocate RGB565");
            const auto expected = capture(palette, false, true);
            closePixels(expected, read(p));
            closePixels(expected, capture(palette, true, true));
            const int allocations = images565, updates = subs565;
            closePixels(expected, capture(palette, true, true));
            check(images565 == allocations && subs565 == updates + 1, "RGB565 storage not reused");
            for (bool aof : {false, true}) {
                auto game = std::make_unique<Game>(assets);
                game->arse(aof);
                game->stageBackgrounds(true);
                game->setNativeRate(true, 6);
                game->deferPresentation(true);
                game->start(24);
                for (int tick = 0; tick < 600; ++tick)
                    game->step({aof ? 300 : 180, aof ? 350 : 270, 0, 0});
                game->renderNative();
                renderer.reloadPages(assets);
                for (auto size : {std::array<int, 2>{640, 360}, {360, 640}})
                    for (int rotation : {0, 1, 2}) {
                        SDL_SetWindowSize(window, size[0], size[1]);
                        SDL_PumpEvents();
                        const auto rgba = capture(game->canvas(), false, true, rotation);
                        closePixels(rgba, capture(game->canvas(), true, true, rotation));
                        const auto edge = capture(game->canvas(), false, true, rotation, true);
                        const int prior = images565 + subs565;
                        drawCount = 0;
                        closePixels(edge, capture(game->canvas(), true, true, rotation, true));
                        check(images565 + subs565 > prior,
                              "Opaque Original overscan did not use RGB565");
                        check(drawCount == (rotation ? 2 : 1),
                              "Original overscan used extra GPU scene/beam passes");
                        const auto enhanced = capture(game->canvas(), true, false, rotation);
                        check(enhanced == capture(game->canvas(), false, false, rotation),
                              "Enhanced rendering changed");
                        closePixels(rgba, capture(game->canvas(), true, true, rotation));
                        ++scenes;
                    }
            }
        }
        // Exercise actual GL errors in both allocation and steady-state update paths.
        for (bool updateFailure : {false, true}) {
            SDL_SetWindowSize(window, 256, 256);
            GlesRenderer renderer(window, assets);
            Hooks hooks;
            SDL_GL_SetSwapInterval(0);
            renderer.rgb565Uploads(false);
            auto p = out / "fallback-base.ppm";
            renderer.present(palette, assets, true, false, Aspect::Fit, p);
            const auto expected = read(p);
            renderer.rgb565Uploads(true);
            if (updateFailure)
                renderer.present(palette, assets, true, false, Aspect::Fit);
            failImage = !updateFailure;
            failSub = updateFailure;
            p = out / "fallback.ppm";
            renderer.present(palette, assets, true, false, Aspect::Fit, p);
            check(!renderer.rgb565Available(), "Rejected RGB565 path was not disabled");
            check(read(p) == expected, "Fallback frame differs from RGBA baseline");
            const int attempts = images565 + subs565;
            renderer.present(palette, assets, true, false, Aspect::Fit, p);
            check(images565 + subs565 == attempts, "Failed upload retried every frame");
            check(read(p) == expected, "Fallback was not persistent");
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout
            << "RGB565 PASS: 65536 colours, storage reuse, " << scenes
            << " edition/shape/orientation hot-swap scenes, allocation/update failure fallback\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
