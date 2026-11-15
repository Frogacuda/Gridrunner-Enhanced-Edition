#include "grpp/background.hpp"
#include "grpp/frontend.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
const Draw &star(const Canvas &c) {
    const auto &f = c.starField();
    check(f.valid && f.last - f.first == 1, "Expected one projected star");
    return c.draws().at(f.first);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        std::array<std::array<int32_t, 3>, 1000> field;
        field.fill({INT32_MAX, INT32_MAX, 1});
        field[0] = {12345678, -7654321, 8000000};
        Canvas base;
        drawMenuStars(base, field, 0);
        base.fill({30, 40, 8, 9}, 0x1234);
        const auto initial = star(base);
        check(std::abs(initial.x - (120.5 + 12345678. / 8000001.)) < .00002,
              "Star projection still quantized");
        check(std::abs(initial.x * 2 - std::round(initial.x * 2)) > .01f,
              "Star remains on old half-pixel lattice");
        Raster565 raster;
        raster.render(base, assets);
        const auto original = raster.pixels();
        float previous = initial.x;
        for (double phase : {.125, .25, .5, .75, 1.}) {
            auto sampled = resampleDecoration(base, phase);
            const auto &dot = star(sampled);
            const double depth = 8000001. - phase * 262144.;
            check(std::abs(dot.x - (120.5 + 12345678. / depth)) < .00002,
                  "Wrong continuous star equation");
            check(dot.x > previous && dot.x - previous < .1f, "Star does not move smoothly");
            previous = dot.x;
            check(dot.energy == 1 && dot.beamWidth == 1 && dot.color == 0xffff &&
                      dot.endX == dot.x && dot.endY == dot.y,
                  "Star size/brightness changed");
            raster.render(sampled, assets);
            check(raster.pixels() == original, "Star smoothing changes Original pixels");
            const auto &foreground = sampled.draws().back();
            check(foreground.sheet == -1 && foreground.x == 30 && foreground.y == 40 &&
                      foreground.color == 0x1234,
                  "Resampling changed foreground");
            auto repeated = resampleDecoration(base, phase);
            check(star(repeated).x == dot.x && star(repeated).y == dot.y,
                  "Repeated presentation drifts");
        }
        Canvas next;
        drawMenuStars(next, field, 1);
        check(std::abs(star(next).x - star(resampleDecoration(base, 1)).x) < .00002,
              "Star source-frame discontinuity");
        check(star(base).x == initial.x && base.starField().stars == field,
              "Presentation mutates source state");
        field[0] = {100, 0, 65536};
        Canvas wrap;
        drawMenuStars(wrap, field, 0);
        auto before = resampleDecoration(wrap, .249999), after = resampleDecoration(wrap, .250001);
        check(star(before).x > 190 && std::abs(star(after).x - 120.5f) < .001f,
              "Depth wrap interpolates across respawn");
        for (bool starsFirst : {false, true}) {
            Canvas combined;
            DecorativeGrid grid;
            grid.valid = true;
            grid.pattern = {3, 4, 3, 4, 1, 2};
            grid.colors = {0x00ff0000u, 0xff000000u, 0x0000ff00u};
            auto addGrid = [&]() {
                grid.first = combined.draws().size();
                drawPreciseBackground(combined, grid);
                grid.last = combined.draws().size();
                combined.decorativeGrid(grid);
            };
            if (starsFirst)
                drawMenuStars(combined, field, 0);
            addGrid();
            if (!starsFirst)
                drawMenuStars(combined, field, 0);
            combined.fill({4, 5, 6, 7}, 0xabcd);
            raster.render(combined, assets);
            const auto reference = raster.pixels();
            for (double phase : {.1, .25, .9}) {
                auto sampled = resampleDecoration(combined, phase);
                check(sampled.starField().last <= sampled.draws().size() &&
                          sampled.decorativeGrid().last <= sampled.draws().size(),
                      "Invalid resampled span");
                raster.render(sampled, assets);
                check(raster.pixels() == reference,
                      "Combined resampling modifies raster reference");
                check(sampled.draws().back().color == 0xabcd,
                      "Combined resampling changes UI order");
            }
        }
        std::cout << "Fractional stars, continuous phase, wrap, cached purity, unchanged Original "
                     "raster, grid/star span ordering PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
