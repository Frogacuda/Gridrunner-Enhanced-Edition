#include "grpp/background.hpp"
#include "grpp/original_overscan.hpp"
#include "grpp/visuals.hpp"
#include <algorithm>
#include <iostream>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        int cases = 0;
        for (bool aof : {false, true})
            for (unsigned stage = 0; stage < 50; ++stage)
                for (int sample : {0, 1, 2}) {
                    PlayerState player;
                    player.arse = aof;
                    player.worldX = fixed16(sample == 0 ? 0 : sample == 1 ? 123 : 230);
                    player.worldY = fixed16(sample == 0 ? 0 : sample == 1 ? 231 : 319);
                    GameRandom random(1);
                    EffectRandom effects(1);
                    Canvas source;
                    source.dimensions(aof ? 400 : 240, aof ? 400 : 320);
                    source.clear(0x0821);
                    SourceVisuals visual(source, assets, player, random, effects);
                    visual.frame(sample * 37, .7f);
                    visual.background(sample * 11, stageBackground(stage));
                    Raster565 expected;
                    expected.render(source, assets);
                    const auto rng = random.state();
                    for (auto size : {std::array<int, 2>{1280, 720}, {720, 1280}}) {
                        const auto result = originalOverscan(source, assets, size[0], size[1],
                                                             Aspect::Fit, 0, false);
                        const int w = result.actors.width();
                        check(result.background.size() == size_t(w) * result.actors.height(),
                              "Invalid framebuffer size");
                        for (int y = 0; y < source.height(); ++y)
                            for (int x = 0; x < source.width(); ++x)
                                check(result.background[size_t(y + result.padY) * w + x +
                                                        result.padX] ==
                                          expected.pixels()[size_t(y) * source.width() + x],
                                      "Original central background changed");
                        check(std::none_of(result.actors.draws().begin(),
                                           result.actors.draws().end(),
                                           [](const Draw &d) { return d.sheet == -3; }),
                              "Enhanced beam leaked into Original");
                        int exterior = 0;
                        for (int y = 0; y < result.actors.height(); ++y)
                            for (int x = 0; x < w; ++x)
                                if (x < result.padX || x >= result.padX + source.width() ||
                                    y < result.padY || y >= result.padY + source.height())
                                    exterior +=
                                        result.background[size_t(y) * w + x] != source.clearColor();
                        check(exterior > 0, "Original overscan grid missing");
                        check(random.state() == rng, "Overscan consumed gameplay RNG");
                        ++cases;
                        if (stage == 0 && sample == 0) {
                            auto interpolated = source;
                            auto grid = interpolated.decorativeGrid();
                            grid.worldX += 123456;
                            grid.worldY -= 654321;
                            interpolated.decorativeGrid(grid);
                            check(originalOverscan(interpolated, assets, size[0], size[1],
                                                   Aspect::Fit, 0, false)
                                          .background == result.background,
                                  "Enhanced camera interpolation leaked into Original overscan");
                        }
                    }
                }
        Canvas world;
        world.playfield({true, 480, 640, 240, 320});
        const auto green = originalOverscan(world, assets, 1280, 720, Aspect::Fit, 0, true),
                   cyan = originalOverscan(world, assets, 1280, 720, Aspect::Fit, 1, true);
        check(!green.actors.draws().empty() &&
                  green.actors.draws().size() == cyan.actors.draws().size(),
              "Boundary segments missing");
        check(green.actors.draws().front().color == uint16_t(25 << 5) &&
                  cyan.actors.draws().front().color == uint16_t((44 << 5) | 22),
              "Original boundary pulse lost green/cyan energy range");
        const auto shifted = originalOverscan(world, assets, 1280, 720, Aspect::Fit, 0, false);
        check(shifted.actors.draws().empty(), "Boundary toggle ignored");
        world.dimensions(400, 400);
        const auto cropped = originalOverscan(world, assets, 200, 300, Aspect::Integer, 0, false);
        check(cropped.padX >= 0 && cropped.padY >= 0 && cropped.actors.width() >= 400 &&
                  cropped.actors.height() >= 400,
              "Small integer viewport produced negative padding");
        Canvas stars;
        DecorativeStars field;
        field.valid = true;
        field.stars[0] = {-200, 0, 0};
        stars.starField(field);
        const auto extendedStars =
            originalOverscan(stars, assets, 1280, 720, Aspect::Fit, 0, false);
        check(std::count(extendedStars.background.begin(), extendedStars.background.end(),
                         uint16_t(0xffff)) == 1,
              "Original exterior star missing");
        Canvas hud;
        hud.fill({110, 300, 10, 10}, 0xffff);
        hud.anchorSince(0, HudAnchor::BottomCenter);
        const auto bottom = originalOverscan(hud, assets, 720, 1280, Aspect::Fit, 0, false);
        check(bottom.actors.draws()[0].x == 110 && bottom.actors.draws()[0].y == 407,
              "Original multiplier did not move to bottom center");
        std::cout << cases
                  << " Original overscan center-pixel, recipe/pan, exterior, RNG and native-raster "
                     "checks PASS; pixel boundary pulse PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
