#include "grpp/background.hpp"
#include "grpp/clock.hpp"
#include "grpp/game.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <tuple>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
bool same(const Draw &a, const Draw &b) {
    return std::tie(a.sheet, a.source.x, a.source.y, a.source.width, a.source.height, a.x, a.y,
                    a.width, a.height, a.angle, a.paint.keyed, a.paint.foreground,
                    a.paint.background, a.color, a.direct, a.referenceOnly, a.squareFragment,
                    a.silhouette, a.patternedBeam, a.energy, a.beamWidth, a.endX, a.endY,
                    a.subpixelX, a.subpixelY, a.decorative, a.referenceGeometry) ==
           std::tie(b.sheet, b.source.x, b.source.y, b.source.width, b.source.height, b.x, b.y,
                    b.width, b.height, b.angle, b.paint.keyed, b.paint.foreground,
                    b.paint.background, b.color, b.direct, b.referenceOnly, b.squareFragment,
                    b.silhouette, b.patternedBeam, b.energy, b.beamWidth, b.endX, b.endY,
                    b.subpixelX, b.subpixelY, b.decorative, b.referenceGeometry);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: background_checks ASSETS");
        Assets assets;
        assets.load(argv[1]);
        DecorativeGrid grid;
        grid.valid = true;
        grid.frame = 2.345;
        grid.worldX = 123.456 * 65536;
        grid.worldY = 231.125 * 65536;
        grid.pattern = {2, 3, 4, 5, 0, 0};
        grid.colors = {0xffffff00u, 0xffffff00u, 0xffffff00u};
        Canvas a, b;
        drawPreciseBackground(a, grid);
        drawPreciseBackground(b, grid, .001);
        require(!a.draws().empty() && a.draws().size() == b.draws().size(),
                "Continuous geometry fixture changed topology");
        const auto &first = a.draws()[0];
        const auto &next = b.draws()[0];
        const double depth = 6144 - std::fmod(grid.frame * 64, 512.);
        const double expected = 262144. / depth / 4.;
        require(std::abs((first.endY - first.y) - expected) < .0001 &&
                    std::abs(expected - std::round(expected)) > .001,
                "Grid spacing is still quantized to old logical pixels");
        const double movement = std::hypot(next.x - first.x, next.y - first.y);
        require(movement > 1.e-6 && movement < .1,
                "Small visual-time increments do not produce subpixel movement");
        auto cameraGrid = grid;
        cameraGrid.worldX += 64;
        Canvas cameraMoved;
        drawPreciseBackground(cameraMoved, cameraGrid);
        require(cameraMoved.draws().size() == a.draws().size() &&
                    std::abs(cameraMoved.draws()[0].x - first.x) > 0 &&
                    std::abs(cameraMoved.draws()[0].x - first.x) < .01,
                "Parallax discarded fractional camera placement");
        // Marked decoration is the only replacement span; real actors and hazards
        // must remain byte-for-byte equivalent in their actual drawing parameters.
        Canvas scene;
        scene.clear();
        scene.sprite(assets, 358, 120, 160);
        grid.first = grid.last = scene.draws().size();
        scene.decorativeGrid(grid);
        scene.beam(10, 200, 230, 200, 0x07e0, 1, 1, true);
        const auto player = scene.draws().front(), hazard = scene.draws().back();
        auto ref = std::make_unique<Raster565>();
        ref->render(scene, assets);
        const auto reference = ref->pixels();
        for (double phase : {0., .001, .25, .5, .999}) {
            auto sample = resampleDecoration(scene, phase);
            require(same(sample.draws().front(), player) && same(sample.draws().back(), hazard),
                    "Decorative sampling modified an actor or hazard");
            for (size_t i = sample.decorativeGrid().first; i < sample.decorativeGrid().last; ++i)
                require(sample.draws()[i].decorative && sample.draws()[i].sheet == -3,
                        "Unapproved drawing entered decorative span");
            ref->render(sample, assets);
            require(ref->pixels() == reference, "Decorative presentation changed reference raster");
            auto repeated = resampleDecoration(scene, phase);
            require(sample.draws().size() == repeated.draws().size(),
                    "Repeated decoration sample changed topology");
            for (size_t i = 0; i < sample.draws().size(); ++i)
                require(same(sample.draws()[i], repeated.draws()[i]),
                        "Repeated visual sample is nondeterministic");
        }
        require(scene.draws().size() == 2 && same(scene.draws()[0], player) &&
                    same(scene.draws()[1], hazard),
                "Source canvas was mutated");
        DecorativeCamera camera;
        camera.capture(scene, 1);
        auto update = scene;
        auto changed = grid;
        changed.worldX += 320;
        update.decorativeGrid(changed);
        camera.capture(update, 1);
        auto display = update;
        camera.apply(display, .5);
        require(std::abs(display.decorativeGrid().worldX - (grid.worldX + 160)) < 1.e-8 &&
                    update.decorativeGrid().worldX == changed.worldX,
                "Background-only camera interpolation leaked or used wrong endpoints");
        camera.capture(update, 2);
        display = update;
        camera.apply(display, 0);
        require(display.decorativeGrid().worldX == changed.worldX,
                "Camera interpolation crossed a continuity reset");
        Canvas menu;
        menu.fill({0, 0, 20, 20}, 0xffff);
        camera.capture(menu, 2);
        auto untouched = resampleDecoration(menu, .5);
        require(untouched.draws().size() == 1 && same(menu.draws()[0], untouched.draws()[0]),
                "Canvas without a background was resampled");
        bool rejected = false;
        auto unsafe = menu;
        DecorativeGrid bad;
        bad.valid = true;
        bad.first = 0;
        bad.last = 1;
        unsafe.decorativeGrid(bad);
        try {
            unsafe.replaceDecoration(a);
        } catch (const std::logic_error &) {
            rejected = true;
        }
        require(rejected, "Nondecorative replacement span was accepted");
        FixedCadence clock(0, 10'000'000);
        clock.pending(0);
        require(clock.fraction(5'000'000) == .5, "Fractional presentation phase is wrong");
        clock.reset(9'000'000);
        require(clock.fraction(9'000'000) == 0, "Clock reset retained a stale visual fraction");
        auto game = std::make_unique<Game>(assets);
        game->setNativeRate(true);
        game->start();
        for (int i = 0; i < 100; ++i)
            game->step({120, 270, 0, 0});
        const auto rng = game->randomState();
        const auto frame = game->frame();
        std::vector<Entity> entities;
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            entities.push_back(game->entities().at(i));
        for (int i = 0; i < 25; ++i) {
            auto sampled = resampleDecoration(game->canvas(), i / 25.);
            require(sampled.decorativeGrid().valid, "Live game has no decorative recipe");
        }
        require(game->randomState() == rng && game->frame() == frame,
                "Visual sampling advanced game time or randomness");
        for (size_t i = 0; i < entities.size(); ++i)
            require(!std::memcmp(&entities[i], &game->entities().at(i), sizeof(Entity)),
                    "Presentation sampling interpolated live entity state");
        std::cout
            << "Background: continuous spacing/time/parallax, fixed reference pixels, actor/hazard "
               "exclusion, pure resampling, camera reset and unchanged live entities/RNG pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
