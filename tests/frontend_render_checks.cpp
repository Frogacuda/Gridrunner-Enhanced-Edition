// Separate development executable. Production contains no scripted UI driver.
#include "gles_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/frontend.hpp"
#include "grpp/options_menu.hpp"
#include "original_frontend.hpp"
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
std::vector<char> bytes(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Missing render capture");
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
    try {
        require(argc == 3 || argc == 4,
                "Usage: frontend_render_checks ASSETS OUTPUT_DIRECTORY [--native]");
        const bool nativeMode = argc == 4;
        const int substeps = nativeMode && std::string(argv[3]) == "--native-200hz" ? 6 : 3;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        auto *window = SDL_CreateWindow("Gridrunner++ frontend renderer checks", 720, 960,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        Assets assets;
        assets.load(argv[1]);
        const std::filesystem::path output = argv[2];
        std::filesystem::create_directories(output);
        {
            GlesRenderer renderer(window, assets);
            auto owner = std::make_unique<Frontend>(assets, std::filesystem::path{});
            auto &f = *owner;
            f.editionMenu(true);
            f.setNativeRate(nativeMode, substeps);
            int captures = 0;
            auto step = [&](FrontendInput i = FrontendInput{}) {
                const auto initial = f.view();
                const int count = nativeMode && initial == FrontendView::Playing ? substeps : 1;
                for (int k = 0; k < count; ++k) {
                    auto rev = f.assetRevision();
                    f.step(i);
                    if (f.assetRevision() != rev)
                        renderer.reloadPages(assets);
                    if (f.view() != initial)
                        break;
                    i.back = i.confirm = i.pointerReleased = false;
                    i.pointer.pressed = 0;
                }
            };
            auto captureCanvas = [&](const Canvas &canvas, const char *name) {
                const auto native = output / (std::string(name) + "-native.ppm"),
                           reference = output / (std::string(name) + "-reference.ppm");
                renderer.present(canvas, assets, false, true, Aspect::Fit, native);
                renderer.present(canvas, assets, true, false, Aspect::Fit, reference);
                require(bytes(native) != bytes(reference),
                        "Enhanced rendering unexpectedly equals the reference raster");
                const auto cached = output / (std::string(name) + "-cached.ppm");
                renderer.present(canvas, assets, false, true, Aspect::Fit, cached);
                require(bytes(native) == bytes(cached),
                        "Cached presentation changes enhanced pixels");
                ++captures;
            };
            auto capture = [&](const char *name) { captureCanvas(f.canvas(), name); };
            auto click = [&](size_t item) {
                if (f.view() == FrontendView::Main) {
                    const size_t actions[] = {3, 4, 0, 2, 5, 5};
                    item = actions[item];
                }
                const auto &r = f.menuBounds().at(item);
                FrontendInput i;
                i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
                i.pointerActive = true;
                i.pointerReleased = true;
                step(i);
            };
            auto back = [&]() {
                FrontendInput i;
                i.back = true;
                step(i);
            };
            step();
            capture("splash");
            const Canvas splash = f.canvas();
            click(0);
            capture("main");
            renderer.present(f.canvas(), assets, true, true, Aspect::Fit,
                             output / "original-nearest-forced.ppm");
            require(bytes(output / "main-reference.ppm") ==
                        bytes(output / "original-nearest-forced.ppm"),
                    "Original graphics inherited bilinear filtering");
            struct RgbImage {
                int w, h;
                std::vector<unsigned char> pixels;
            };
            auto rgbImage = [&](const std::filesystem::path &p) {
                std::ifstream in(p, std::ios::binary);
                std::string magic;
                int w, h, max;
                in >> magic >> w >> h >> max;
                in.get();
                std::vector<unsigned char> data(size_t(w) * h * 3);
                in.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size()));
                require(bool(in), "Incomplete orientation image");
                return RgbImage{w, h, std::move(data)};
            };
            for (const auto size : {std::array<int, 2>{1280, 720}, {720, 1600}}) {
                require(SDL_SetWindowSize(window, size[0], size[1]), SDL_GetError());
                SDL_PumpEvents();
                const auto path = output / ("splash-edge-" + std::to_string(size[0]) + ".ppm");
                renderer.present(splash, assets, false, true, Aspect::Fit, path, 0, 0, true, false);
                const auto pixels = rgbImage(path);
                std::array<bool, 256> levels{};
                for (int y = 0; y < pixels.h / 2; ++y)
                    levels[pixels.pixels[(size_t(y) * pixels.w + 2) * 3 + 2]] = true;
                int shades = 0;
                for (bool used : levels)
                    shades += used;
                require(shades > 80, "Enhanced intro still uses quantized RGB565 gradient");
                if (size[1] > size[0])
                    require(pixels.pixels[2 * 3 + 2] > 150,
                            "Portrait gradient did not continue brightening into overscan");

                const auto originalPath =
                    output / ("splash-edge-original-" + std::to_string(size[0]) + ".ppm");
                renderer.present(splash, assets, true, false, Aspect::Fit, originalPath, 0, 0, true,
                                 false);
                const auto originalSplash = rgbImage(originalPath);
                auto blue = [&](int x, int y) {
                    return originalSplash.pixels[(size_t(y) * originalSplash.w + x) * 3 + 2];
                };
                const auto view = makeViewport(originalSplash.w, originalSplash.h, Aspect::Fit,
                                               splash.width(), splash.height());
                const auto edge = blue(view.x + 2, view.y);
                require(edge > 100 && blue(2, 0) == edge &&
                            blue(2, originalSplash.h - 1) == edge,
                        "Original splash padding does not match its edge blue");
                if (view.x > 0)
                    require(blue(2, view.height / 3) == blue(view.x + 2, view.height / 3) &&
                                blue(2, view.height / 3) < edge,
                            "Original splash gradient did not extend horizontally");
                if (view.y > 0)
                    require(blue(2, view.y - 1) == edge &&
                                blue(2, view.y + view.height) == edge,
                            "Original splash portrait padding has a visible seam");
            }
            require(SDL_SetWindowSize(window, 720, 960), SDL_GetError());
            SDL_PumpEvents();
            Canvas fullAnnouncement, clippedAnnouncement;
            fullAnnouncement.sprite(assets, 256, 120, 160);
            PlayerState clipPlayer;
            GameRandom clipRandom(1);
            EffectRandom clipEffects(1);
            SourceVisuals clipVisual(clippedAnnouncement, assets, clipPlayer, clipRandom,
                                     clipEffects);
            clipVisual.sprite(256, 20, 160);
            const auto fullPath = output / "got-you-full.ppm",
                       clippedPath = output / "got-you-clipped.ppm";
            renderer.present(fullAnnouncement, assets, false, true, Aspect::Fit, fullPath);
            renderer.present(clippedAnnouncement, assets, false, true, Aspect::Fit, clippedPath);
            const auto fullPixels = rgbImage(fullPath), clippedPixels = rgbImage(clippedPath);
            for (int y = 457; y < 503; ++y)
                for (int x = 5; x < 280; ++x)
                    for (int c = 0; c < 3; ++c)
                        require(
                            std::abs(int(clippedPixels.pixels[(size_t(y) * 720 + x) * 3 + c]) -
                                     int(fullPixels.pixels[(size_t(y) * 720 + x + 300) * 3 + c])) <=
                                2,
                            "Clipping stretches the high-resolution announcement UVs");
            auto original = rgbImage(output / "main-reference.ppm");
            for (int y = 0; y < original.h; ++y)
                for (int x = 0; x < original.w; ++x)
                    for (int c = 0; c < 3; ++c)
                        require(
                            original.pixels[(size_t(y) * original.w + x) * 3 + c] ==
                                original
                                    .pixels[(size_t(y / 3 * 3) * original.w + x / 3 * 3) * 3 + c],
                            "Original output is not made of identical 3x3 low-resolution pixels");
            for (bool originalMode : {false, true}) {
                const auto base =
                    rgbImage(output / (originalMode ? "main-reference.ppm" : "main-native.ppm"));
                require(SDL_SetWindowSize(window, 960, 720), SDL_GetError());
                SDL_PumpEvents();
                for (int rotation : {1, 2}) {
                    const auto path =
                        output / (std::string(originalMode ? "original" : "enhanced") +
                                  "-rotation-" + std::to_string(rotation) + ".ppm");
                    renderer.present(f.canvas(), assets, originalMode, true, Aspect::Fit, path, 0,
                                     rotation);
                    const auto turned = rgbImage(path);
                    require(turned.w == base.h && turned.h == base.w,
                            "Rotated physical dimensions wrong");
                    for (int y = 0; y < base.h; ++y)
                        for (int x = 0; x < base.w; ++x) {
                            const int px = rotation == 1 ? base.h - 1 - y : y,
                                      py = rotation == 1 ? x : base.w - 1 - x;
                            for (int c = 0; c < 3; ++c)
                                require(
                                    std::abs(
                                        int(base.pixels[(size_t(y) * base.w + x) * 3 + c]) -
                                        int(turned.pixels[(size_t(py) * turned.w + px) * 3 + c])) <=
                                        (originalMode ? 0 : 1),
                                    "Rotated rendering does not match upright image");
                        }
                }
                require(SDL_SetWindowSize(window, 720, 960), SDL_GetError());
                SDL_PumpEvents();
            }
            std::cout << "Original: forced nearest filtering and exact 3x3 pixels; both renderers "
                         "rotate correctly in both directions\n";
            click(0);
            capture("about");
            for (size_t doc = 0; doc < 4; ++doc) {
                click(doc);
                capture(("reader-" + std::to_string(doc)).c_str());
                for (int p = 0; p < 12 && f.view() == FrontendView::Reader; ++p)
                    click(1);
            }
            back();
            click(1);
            capture("players");
            click(0);
            capture("slot");
            click(0);
            capture("name");
            click(4);
            click(0);
            click(1);
            capture("clear");
            click(1);
            back();
            click(2);
            capture("levels");
            FrontendInput levelStep;
            levelStep.horizontal = 1;
            step(levelStep);
            capture("levels-no-endurance");
            levelStep.horizontal = -1;
            step(levelStep);
            click(2);
            for (int i = 0; i < 100; ++i) {
                FrontendInput input;
                input.pointer = {120, 270, 0, 0};
                step(input);
            }
            capture("gameplay");
            Canvas gridStudy = f.canvas();
            gridStudy.fill({80, 200, 80, 40}, 0);
            gridStudy.sprite(assets, 358, 120, 220);
            gridStudy.beam(90, 235, 150, 235, 0x07e0);
            captureCanvas(gridStudy, "subpixel-grid-study");
            renderer.present(gridStudy, assets, false, true, Aspect::Fit,
                             output / "subpixel-grid-quarter.ppm", .25);
            renderer.present(gridStudy, assets, false, true, Aspect::Fit,
                             output / "subpixel-grid-quarter-repeat.ppm", .25);
            renderer.present(gridStudy, assets, true, false, Aspect::Fit,
                             output / "subpixel-grid-quarter-reference.ppm", .25);
            require(bytes(output / "subpixel-grid-study-native.ppm") !=
                        bytes(output / "subpixel-grid-quarter.ppm"),
                    "Fractional visual time did not move the background");
            require(bytes(output / "subpixel-grid-quarter.ppm") ==
                        bytes(output / "subpixel-grid-quarter-repeat.ppm"),
                    "Repeated visual phase changed pixels");
            require(bytes(output / "subpixel-grid-study-reference.ppm") ==
                        bytes(output / "subpixel-grid-quarter-reference.ppm"),
                    "Decorative phase moved reference pixels");
            auto actorRegion = [&](const std::filesystem::path &path) {
                std::ifstream in(path, std::ios::binary);
                std::string magic;
                int w, h, max;
                in >> magic >> w >> h >> max;
                in.get();
                require(w == 720 && h == 960, "Unexpected grid study resolution");
                std::vector<char> pixels(size_t(w) * h * 3), region;
                in.read(pixels.data(), std::streamsize(pixels.size()));
                for (int y = 603; y < 717; ++y)
                    region.insert(region.end(), pixels.begin() + (size_t(y) * w + 243) * 3,
                                  pixels.begin() + (size_t(y) * w + 477) * 3);
                return region;
            };
            require(actorRegion(output / "subpixel-grid-study-native.ppm") ==
                        actorRegion(output / "subpixel-grid-quarter.ppm"),
                    "Visual phase moved foreground actor/hazard pixels");
            DecorativeCamera view;
            view.capture(gridStudy, 1);
            auto moved = gridStudy;
            auto recipe = moved.decorativeGrid();
            recipe.worldX += 65536;
            moved.decorativeGrid(recipe);
            view.capture(moved, 1);
            view.apply(moved, .5);
            renderer.present(moved, assets, false, true, Aspect::Fit,
                             output / "subpixel-grid-camera-half.ppm");
            require(bytes(output / "subpixel-grid-study-native.ppm") !=
                            bytes(output / "subpixel-grid-camera-half.ppm") &&
                        actorRegion(output / "subpixel-grid-study-native.ppm") ==
                            actorRegion(output / "subpixel-grid-camera-half.ppm"),
                    "Camera interpolation leaked outside background");
            back();
            step();
            capture("highscores");
            click(0);
            click(0);
            click(5);
            capture("farewell");
            auto scores = original::defaultScores();
            scores.players[0].level = 50;
            scores.saveAtomic(output / "campaign-hst.txt");
            auto campaign = std::make_unique<Frontend>(assets, output / "campaign-hst.txt");
            campaign->setNativeRate(nativeMode, substeps);
            FrontendInput action;
            action.confirm = true;
            campaign->step(action);
            auto r = campaign->menuBounds().at(3);
            action = {};
            action.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
            action.pointerReleased = true;
            campaign->step(action);
            renderer.reloadPages(assets);
            require(campaign->view() == FrontendView::Campaign, "Campaign screen unavailable");
            captureCanvas(campaign->canvas(), "campaign");
            action = {};
            action.confirm = true;
            for (int p = 0; p < 10 && campaign->view() == FrontendView::Campaign; ++p)
                campaign->step(action);
            campaign->step({});
            renderer.reloadPages(assets);
            captureCanvas(campaign->canvas(), "campaign-continue");
            Canvas trail;
            trail.clear(0x0841);
            for (int x = 30; x < 220; x += 20)
                trail.beam(float(x), 100, float(x), 220, 0x045f, .5f, .7f);
            PlayerState player;
            player.sheepPower = 6;
            GameRandom random(1);
            EffectRandom effect(1);
            SourceVisuals visual(trail, assets, player, random, effect);
            visual.frame(10, 2);
            Entity ship;
            ship.drawing = 0;
            ship.sprite = 358;
            ship.x = fixed16(180);
            ship.y = fixed16(160);
            ship.historyCursor = 24;
            for (int i = 0; i < 64; ++i)
                ship.history[size_t(i)] = {fixed16(55 + i * 5), fixed16(160)};
            visual.entity(ship);
            captureCanvas(trail, "rainbow-trail");
            Canvas arcs;
            arcs.clear();
            SourceVisuals arcVisual(arcs, assets, player, random, effect);
            for (int i = 0; i < 4; ++i)
                arcVisual.beamSprite(107 + i, 60 + i * 40, 160, 0x07ff);
            captureCanvas(arcs, "arc-projectiles");
            Canvas beam;
            beam.clear();
            beam.beam(50, 160, 190, 160, 0x03e0);
            renderer.present(beam, assets, false, true, Aspect::Fit, output / "beam-single.ppm");
            beam.beam(120, 100, 120, 220, 0x03e0);
            renderer.present(beam, assets, false, true, Aspect::Fit, output / "beam-overlap.ppm");
            auto green = [&](const std::filesystem::path &path) {
                std::ifstream input(path, std::ios::binary);
                std::string magic;
                int w, h, max;
                input >> magic >> w >> h >> max;
                input.get();
                require(magic == "P6" && max == 255, "Bad capture");
                std::vector<unsigned char> data(size_t(w) * h * 3);
                input.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size()));
                return int(data[(size_t(h / 2) * w + w / 2) * 3 + 1]);
            };
            const int single = green(output / "beam-single.ppm"),
                      overlap = green(output / "beam-overlap.ppm");
            require(single > 20 && single < 160 && overlap > single * 1.7,
                    "Beam headroom/additive overlap failed");
            std::cout << "Beam overlap: " << single << " -> " << overlap << " green\n";
            Canvas arcLevel;
            arcLevel.clear();
            arcLevel.beam(50, 160, 190, 160, 0x07e0, .60f / .65f);
            renderer.present(arcLevel, assets, false, true, Aspect::Fit,
                             output / "arc-energy-near.ppm");
            const int arcNear = green(output / "arc-energy-near.ppm");
            arcLevel.clear();
            arcLevel.beam(50, 160, 190, 160, 0x07e0,
                          .60f / .65f * SourceVisuals::beamDepthEnergy(8192));
            renderer.present(arcLevel, assets, false, true, Aspect::Fit,
                             output / "arc-energy-far.ppm");
            const int arcFar = green(output / "arc-energy-far.ppm");
            require(std::abs(arcNear - 149) <= 2 && arcFar < arcNear && arcFar > arcNear / 2,
                    "60 percent beam intensity/depth falloff failed");
            std::cout << "60 percent beam depth: " << arcNear << " -> " << arcFar << " green\n";
            Canvas reduced;
            reduced.clear();
            reduced.beam(50, 160, 190, 160, 0x07e0, .60f);
            renderer.present(reduced, assets, false, true, Aspect::Fit,
                             output / "crawler-energy-60pct.ppm");
            const int reducedGreen = green(output / "crawler-energy-60pct.ppm");
            require(std::abs(reducedGreen - 161 * .60f) <= 2,
                    "Crawler control did not dim by 40 percent");
            // Source RGB565 intensity must survive conversion, including black.
            int previous = -1, full = 0;
            std::vector<int> measured;
            for (int level : {0, 8, 16, 32, 63}) {
                Canvas ramp;
                ramp.clear();
                ramp.beam(50, 160, 190, 160, uint16_t(level << 5));
                auto path = output / ("brightness-" + std::to_string(level) + ".ppm");
                renderer.present(ramp, assets, false, true, Aspect::Fit, path);
                int value = green(path);
                require(value > previous, "Source brightness is not monotonic");
                previous = value;
                measured.push_back(value);
                full = value;
            }
            require(measured[0] == 0, "Black source pixels emitted light");
            int index = 0;
            for (int level : {0, 8, 16, 32, 63}) {
                int expanded = (level << 2) | (level >> 4);
                require(std::abs(measured[size_t(index++)] * 255 - full * expanded) <= 510,
                        "Source brightness was normalized or lost");
            }
            Canvas patterns;
            patterns.clear();
            SourceVisuals patternVisual(patterns, assets, player, random, effect);
            int row = 40;
            for (uint32_t mask : {0x55555555u, 0x01010101u, 0xff00ff00u, 0x80000001u}) {
                patternVisual.horizontal(20, row, 200, 0x07e0, mask, true);
                row += 40;
            }
            captureCanvas(patterns, "patterns");
            auto sampleGreen = [&](int x, int y) {
                std::ifstream input(output / "patterns-native.ppm", std::ios::binary);
                std::string magic;
                int w, h, max;
                input >> magic >> w >> h >> max;
                input.get();
                input.seekg(std::streamoff((size_t(y) * w + x) * 3 + 1), std::ios::cur);
                return input.get();
            };
            const int on = sampleGreen(61, 121), gap = sampleGreen(64, 121);
            require(on > 20 && gap < on / 2, "Glow bridged alternating source pixel gaps");
            std::cout << "Source brightness ramp: ";
            for (int v : measured)
                std::cout << v << ' ';
            std::cout << "; patterned on/gap " << on << '/' << gap << '\n';
            Canvas starStudy, pixelStudy;
            std::array<std::array<int32_t, 3>, 1000> starField{};
            starField.fill({INT32_MAX, INT32_MAX, 1});
            starField[0] = {0, 0, 1};
            drawMenuStars(starStudy, starField, 0);
            SourceVisuals pixelVisual(pixelStudy, assets, player, random, effect);
            pixelVisual.pixel(120, 160, 0xffff);
            renderer.present(starStudy, assets, false, true, Aspect::Fit, output / "star-size.ppm");
            renderer.present(pixelStudy, assets, false, true, Aspect::Fit,
                             output / "ordinary-dot-size.ppm");
            require(bytes(output / "star-size.ppm") == bytes(output / "ordinary-dot-size.ppm"),
                    "Stars differ from ordinary converted pixel dots");
            Canvas movingStars;
            starField[0] = {123456789, 34567890, 2000000};
            drawMenuStars(movingStars, starField, 0);
            movingStars.fill({8, 8, 20, 20}, 0x07e0);
            captureCanvas(movingStars, "fractional-stars");
            renderer.present(movingStars, assets, false, true, Aspect::Fit,
                             output / "stars-phase-quarter.ppm", .25);
            renderer.present(movingStars, assets, false, true, Aspect::Fit,
                             output / "stars-phase-quarter-repeat.ppm", .25);
            renderer.present(movingStars, assets, false, true, Aspect::Fit,
                             output / "stars-phase-threequarters.ppm", .75);
            renderer.present(movingStars, assets, true, false, Aspect::Fit,
                             output / "stars-phase-reference.ppm", .75);
            require(bytes(output / "stars-phase-quarter.ppm") !=
                            bytes(output / "stars-phase-threequarters.ppm") &&
                        bytes(output / "stars-phase-quarter.ppm") ==
                            bytes(output / "stars-phase-quarter-repeat.ppm"),
                    "Star presentation time is frozen or nondeterministic");
            require(bytes(output / "fractional-stars-reference.ppm") ==
                        bytes(output / "stars-phase-reference.ppm"),
                    "Star presentation phase changes Original output");
            const auto starA = rgbImage(output / "stars-phase-quarter.ppm"),
                       starB = rgbImage(output / "stars-phase-threequarters.ppm");
            for (int y = 24; y < 84; ++y)
                for (int x = 24; x < 84; ++x)
                    for (int ch = 0; ch < 3; ++ch)
                        require(starA.pixels[(size_t(y) * starA.w + x) * 3 + ch] ==
                                    starB.pixels[(size_t(y) * starB.w + x) * 3 + ch],
                                "Star presentation moves foreground pixels");
            Canvas crawlers;
            crawlers.clear();
            SourceVisuals crawlVisual(crawlers, assets, player, random, effect);
            crawlVisual.frame(0, 1.25f);
            Entity waiting;
            waiting.drawing = 11;
            waiting.effectMode = 0;
            waiting.colorCycle = 13;
            for (int i = 0; i < 64; ++i) {
                waiting.history[size_t(i)].x = fixed16(120 + (i - 32) * 3);
                waiting.history[size_t(i)].y = fixed16(160 + (i % 3) * 3);
            }
            crawlVisual.entity(waiting);
            captureCanvas(crawlers, "waiting-crawlers");
            require(SDL_SetWindowSize(window, 480, 640), SDL_GetError());
            SDL_PumpEvents();
            Canvas brightGrid, gridControl;
            for (float x : {100.5f, 108.5f}) {
                brightGrid.beam(x, 160.5f, x, 160.5f, 0x07e0, .1f, 1.f, true);
                brightGrid.markDecorativeLast();
                gridControl.beam(x, 160.5f, x, 160.5f, 0x07e0, .1f, 1.f, true);
            }
            renderer.present(brightGrid, assets, false, true, Aspect::Fit,
                             output / "grid-dot-boost.ppm");
            renderer.present(gridControl, assets, false, true, Aspect::Fit,
                             output / "grid-dot-previous-profile.ppm");
            auto dotMetrics = [&](const char *name) {
                std::ifstream in(output / name, std::ios::binary);
                std::string magic;
                int w, h, max;
                in >> magic >> w >> h >> max;
                in.get();
                int peak = 0, gap = 0;
                long long total = 0;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        in.get();
                        int g = in.get();
                        in.get();
                        peak = std::max(peak, g);
                        total += g;
                        if (x == 209)
                            gap = std::max(gap, g);
                    }
                return std::array<long long, 3>{peak, total, gap};
            };
            const auto boosted = dotMetrics("grid-dot-boost.ppm"),
                       previousDot = dotMetrics("grid-dot-previous-profile.ppm");
            require(boosted[0] > previousDot[0] * 1.8 && boosted[1] > previousDot[1] * 2.5,
                    "Decorative grid dot brightness/glow boost is too weak");
            require(boosted[2] < boosted[0] / 4, "Boosted decorative dot glow closes grid gaps");
            std::cout << "Decorative grid dots: peak " << previousDot[0] << " -> " << boosted[0]
                      << "; integrated glow " << previousDot[1] << " -> " << boosted[1] << "; gap "
                      << boosted[2] << '\n';
            Canvas faint;
            faint.clear();
            faint.beam(120.5f, 160.5f, 120.5f, 160.5f, 0x0040, .25f, 1.f, true);
            renderer.present(faint, assets, false, true, Aspect::Fit,
                             output / "faint-grid-dot-2x.ppm");
            Canvas oldWidth;
            oldWidth.clear();
            oldWidth.beam(120.5f, 160.5f, 120.5001f, 160.5f, 0x0040, .25f, .65f, true);
            renderer.present(oldWidth, assets, false, true, Aspect::Fit,
                             output / "faint-grid-old-width-control.ppm");
            auto peakGreen = [&](const std::filesystem::path &path) {
                std::ifstream input(path, std::ios::binary);
                std::string magic;
                int w, h, max;
                input >> magic >> w >> h >> max;
                input.get();
                std::vector<unsigned char> pixels(size_t(w) * h * 3);
                input.read(reinterpret_cast<char *>(pixels.data()), std::streamsize(pixels.size()));
                int peak = 0;
                for (size_t p = 1; p < pixels.size(); p += 3)
                    peak = std::max(peak, int(pixels[p]));
                return peak;
            };
            const int dotPeak = peakGreen(output / "faint-grid-dot-2x.ppm"),
                      oldPeak = peakGreen(output / "faint-grid-old-width-control.ppm");
            require(dotPeak > 0 && dotPeak > oldPeak,
                    "Faint grid dot coverage did not improve at 2x scale");
            std::cout << "Faint grid dot at 2x: " << oldPeak << " -> " << dotPeak
                      << " peak green; star/ordinary-dot pixels identical; crawler control "
                      << reducedGreen << " green\n";
            auto edition = std::make_unique<Frontend>(assets, std::filesystem::path{});
            edition->editionMenu(true);
            edition->setNativeRate(true, 6);
            FrontendInput select;
            select.confirm = true;
            edition->step(select);
            auto er = edition->menuBounds().at(1);
            select = {};
            select.pointer = {(er[0] + er[2]) / 2, (er[1] + er[3]) / 2, 0, 0};
            select.pointerActive = select.pointerReleased = true;
            edition->step(select);
            renderer.reloadPages(assets);
            captureCanvas(edition->canvas(), "aof-title");
            er = edition->menuBounds().at(0);
            select.pointer = {(er[0] + er[2]) / 2, (er[1] + er[3]) / 2, 0, 0};
            edition->step(select);
            er = edition->menuBounds().at(2);
            select.pointer = {(er[0] + er[2]) / 2, (er[1] + er[3]) / 2, 0, 0};
            edition->step(select);
            renderer.reloadPages(assets);
            for (int n = 0; n < 360; ++n)
                edition->step({});
            captureCanvas(edition->canvas(), "aof-gameplay");
            for (int rotation : {1, 2})
                renderer.present(edition->canvas(), assets, false, true, Aspect::Fit,
                                 output / ("aof-rotation-" + std::to_string(rotation) + ".ppm"), 0,
                                 rotation);
            PauseMenu pause;
            PlayerOptions preferences;
            pause.open();
            pause.rebuild(preferences, true);
            Canvas paused = edition->canvas(), overlay;
            pause.draw(overlay);
            paused.centeredUi(overlay);
            captureCanvas(paused, "aof-pause");
            auto editionClick = [&](size_t item) {
                auto r = edition->menuBounds().at(item);
                FrontendInput i;
                i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 0, 0};
                i.pointerReleased = i.pointerActive = true;
                edition->step(i);
                renderer.reloadPages(assets);
            };
            auto editionTitle = [&]() {
                FrontendInput i;
                i.back = true;
                edition->step(i);
                i = {};
                i.confirm = true;
                edition->step(i);
                edition->step(i);
            };
            editionTitle();
            editionClick(0);
            editionClick(3);
            require(edition->game().arse() && edition->game().endurance(),
                    "AoF Endurance route failed");
            for (int n = 0; n < 360; ++n)
                edition->step({});
            captureCanvas(edition->canvas(), "aof-endurance");
            editionTitle();
            editionClick(1);
            require(!edition->arseEdition(), "No-save title cannot switch back");
            captureCanvas(edition->canvas(), "standard-title-restored");
            editionClick(0);
            editionClick(3);
            require(!edition->game().arse() && edition->game().endurance(),
                    "Standard Endurance route failed");
            for (int n = 0; n < 360; ++n)
                edition->step({});
            captureCanvas(edition->canvas(), "standard-endurance");
            std::cout << captures
                      << " enhanced frontend/gameplay GLES views differ from reference and stay "
                         "stable across cached presentation; "
                      << renderer.driver() << '\n';
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
