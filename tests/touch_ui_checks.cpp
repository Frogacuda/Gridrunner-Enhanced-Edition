#include "grpp/options_menu.hpp"
#include <iostream>
#include <memory>
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
        RelativeTouch t;
        t.begin(-100, 100, 80.25f, 150.5f);
        check(t.target(-100, 100) == std::array<float, 2>{80.25f, 150.5f}, "Non-neutral touch");
        auto moved = t.target(-60, 120);
        check(moved == std::array<float, 2>{120.25f, 170.5f}, "Non-proportional touch");
        for (int n = 0; n < 100; n++)
            check(t.target(-60, 120) == moved, "Held touch accelerates");
        t.begin(200, 200, 12, 25);
        check(t.target(200, 200) == std::array<float, 2>{12, 25}, "Origin not reset");
        PauseMenu menu;
        PlayerOptions o;
        menu.open();
        menu.rebuild(o, false);
        check(menu.bounds().size() == 8, "Touch row visible on desktop");
        FrontendInput i;
        i.vertical = -1;
        menu.input(i);
        i = {};
        i.confirm = true;
        check(menu.input(i) == 6, "Hidden row changes Exit");
        menu.rebuild(o, true);
        check(menu.bounds().size() == 9, "Missing touch row");
        check(menu.actions() == std::vector<int>({0, 1, 2, 3, 4, 7, 5, 8, 6}),
              "View is not directly beneath Orientation");
        menu.rebuild(o, true, false);
        check(menu.actions() == std::vector<int>({0, 1, 2, 4, 7, 5, 8, 6}) &&
                  menu.bounds().size() == 8,
              "Android Display row was not removed and compacted");
        check(menu.actions()[size_t(menu.selected())] == 6,
              "Hiding Display changes selected action");
        menu.rebuild(o, false, false);
        check(menu.actions() == std::vector<int>({0, 1, 2, 4, 7, 8, 6}),
              "Hidden capability rows leave menu gaps");
        for (auto screen : {std::array<int, 2>{720, 1280}, std::array<int, 2>{1280, 720}})
            for (int rotation = 0; rotation < 3; ++rotation) {
                OriginStick stick;
                const float radius = .25f * std::min(screen[0], screen[1]);
                stick.begin(100, 100, screen[0], screen[1], rotation);
                check(stick.tick(1) == std::array<float, 2>{0, 0}, "Stick press not neutral");
                stick.move(100 + radius, 100);
                auto full = stick.tick(1);
                const auto d = orientDelta(250, 0, rotation);
                check(std::abs(full[0] - d[0]) < .001 && std::abs(full[1] - d[1]) < .001,
                      "Quarter-screen stick radius/rotation wrong");
                stick.move(100 + radius * 4, 100);
                check(stick.tick(1) == full, "Stick does not clamp");
                check(std::abs(
                          std::hypot(stick.tick(1, 240, 320, 3)[0], stick.tick(1, 240, 320, 3)[1]) -
                          750) < .001,
                      "Stick sensitivity wrong");
                stick.move(100 + radius * .59f, 100);
                check(std::abs(std::hypot(stick.tick(1)[0], stick.tick(1)[1]) - 62.5f) < .001,
                      "Stick curve wrong or origin reset");
                stick.move(100 - radius, 100);
                check(std::abs(stick.tick(1)[0] + full[0]) < .001 &&
                          std::abs(stick.tick(1)[1] + full[1]) < .001,
                      "Stick reversal reset origin");
                stick.cancel();
                check(stick.tick(1) == std::array<float, 2>{0, 0}, "Stick release moves ship");
            }
        t.begin(0, 0, 230, 300);
        check(t.target(100, 100, 3) == std::array<float, 2>{239, 319}, "Relative clamp wrong");
        check(t.target(99, 99, 3) == std::array<float, 2>{236, 316},
              "Relative boundary stores overshoot");
        for (int mask = 0; mask < 32; ++mask)
            check(gameplayMouseCapture(mask & 1, mask & 2, mask & 4, mask & 8, mask & 16) ==
                      (mask == 23),
                  "Mouse capture escaped gameplay policy");
        menu.open();
        menu.rebuild(o, false);
        auto row =
            std::find(menu.actions().begin(), menu.actions().end(), 8) - menu.actions().begin();
        for (int dir : {-1, 1}) {
            auto r = menu.carets()[size_t(dir > 0)];
            FrontendInput click;
            click.pointerActive = true;
            click.pointer.x = (r[0] + r[2]) / 2;
            click.pointer.y = (r[1] + r[3]) / 2;
            click.pointer.held = 1;
            check(menu.input(click) == -1, "Sensitivity changes before release");
            click.pointer.held = 0;
            click.pointerReleased = true;
            check(menu.input(click) == 8 && menu.direction() == dir,
                  "Sensitivity caret direction wrong");
        }
        FrontendInput key;
        key.horizontal = -1;
        check(menu.input(key) == 8 && menu.direction() == -1, "Sensitivity left navigation wrong");
        Canvas c;
        c.clear(0x07e0);
        c.alphaFill({0, 0, 240, 320}, 0, .5f);
        auto raster = std::make_unique<Raster565>();
        raster->render(c, assets);
        check(raster->pixels()[120 + 160 * 240] == 0x0400, "Panel opacity wrong");
        Canvas border;
        border.selection({0, 0, 240, 320}, 0xffff);
        int beams = 0;
        for (auto &d : border.draws())
            if (d.sheet == -3) {
                ++beams;
                check(d.x > 0 && d.y > 0 && d.endX < 239 && d.endY < 319, "Border clipped");
            }
        check(beams == 4, "Missing vector border");
        std::cout << "Relative touch, capability menu, alpha panel and inset vector border pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
