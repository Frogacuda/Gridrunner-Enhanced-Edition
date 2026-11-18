#include "grpp/background.hpp"
#include "grpp/expanded_presentation.hpp"
#include "grpp/game.hpp"
#include "grpp/options_menu.hpp"
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static void exterior(Game &g) {
        const int w = g.canvas_.width(), h = g.canvas_.height();
        const std::array<std::array<int, 2>, 4> positions{
            {{-24, h / 2}, {w + 24, h / 2}, {w / 2, -24}, {w / 2, h + 24}}};
        for (size_t n = 0; n < positions.size(); ++n) {
            auto &e = g.pool_.at(n + 1);
            e = {};
            e.behavior = 3;
            e.drawing = 3;
            e.sprite = 64;
            e.x = add32(g.player_.worldX, fixed16(positions[n][0]));
            e.y = add32(g.player_.worldY, fixed16(positions[n][1]));
            g.nativeDrawBase_->at(n + 1) = e;
        }
        const auto before = hash(g);
        const auto view = g.expandedCanvas();
        const auto &sprite = g.assets_.sprite(64);
        for (const auto &p : positions) {
            bool found = false;
            for (const auto &d : view.draws())
                if (d.sheet == sprite.sheet && d.source.x == sprite.x && d.source.y == sprite.y &&
                    d.width == sprite.width && d.height == sprite.height &&
                    std::abs(d.x + d.width / 2 - p[0]) < 1 &&
                    std::abs(d.y + d.height / 2 - p[1]) < 1) {
                    found = true;
                    break;
                }
            check(found, "Actor was cropped at a horizontal or vertical play-area boundary");
        }
        check(hash(g) == before, "Exterior actor rendering changes gameplay");
    }
    static uint64_t hash(const Game &g) {
        uint64_t h = 1469598103934665603ull;
        auto add = [&](const auto &value) {
            auto p = reinterpret_cast<const unsigned char *>(&value);
            for (size_t i = 0; i < sizeof(value); ++i) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
        };
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            add(g.pool_.at(i));
        add(g.player_);
        add(g.grid_);
        add(g.worldState_);
        add(g.combatState_);
        add(g.random_.state());
        add(g.effects_.state());
        add(g.frame_);
        add(g.nativePhase_);
        return h;
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        check(argc == 3, "ASSETS SCRATCH required");
        PlayerOptions options;
        check(options.expanded && !options.original && options.cap == 200 && options.control == 1 &&
                  options.sensitivity == 10,
              "Fresh defaults changed");
        const auto file = std::filesystem::path(argv[2]) / "view-options.txt";
        options.expanded = true;
        check(options.save(file), "Options save");
        PlayerOptions loaded;
        loaded.load(file);
        check(loaded.expanded, "Draw to Edge did not persist");
        std::ofstream(file) << "4 120 0 1 0 2\n";
        loaded.load(file);
        check(!loaded.expanded && loaded.orientation == 2,
              "Old options did not default to Letterbox");
        std::ofstream(file) << "5 200 0 1 0 0 2\n";
        loaded.load(file);
        check(loaded.cap == 120 && !loaded.expanded && loaded.orientation == 2,
              "Invalid view option changed settings");
        PauseMenu menu;
        // A star outside the source viewport must appear in the matching overscan
        // without changing its projection, speed or the stars inside the viewport.
        for (const auto size : {std::array<int, 2>{1280, 720}, {720, 1600}}) {
            Canvas sky;
            DecorativeStars stars;
            stars.valid = true;
            stars.stars.fill({0, 0, 1048575});
            stars.stars[0] = size[0] > size[1] ? std::array<int32_t, 3>{-220 * 1048576, 0, 1048575}
                                               : std::array<int32_t, 3>{0, -200 * 1048576, 1048575};
            stars.first = stars.last = 0;
            sky.starField(stars);
            const auto wide = expandedPresentation(sky, size[0], size[1], Aspect::Fit, 0, 0, false);
            const auto view = makeViewport(size[0], size[1], Aspect::Fit, 240, 320);
            bool outside = false;
            for (const auto &d : wide.draws())
                outside |= d.sheet == -3 && (d.x < view.x || d.x >= view.x + view.width ||
                                             d.y < view.y || d.y >= view.y + view.height);
            check(outside, "Draw to Edge stars remain clipped to source viewport");
        }
        menu.open();
        menu.rebuild(options, false);
        FrontendInput i;
        i.vertical = -1;
        menu.input(i);
        menu.input(i);
        menu.input(i);
        i = {};
        i.confirm = true;
        check(menu.input(i) == 7, "Draw to Edge row action missing");
        Canvas source;
        source.dimensions(240, 320);
        source.fill({70, 100, 10, 10}, 0xf800);
        source.playfield({true, 480, 640, 120, 160});
        auto start = source.draws().size();
        source.fill({8, 300, 10, 10}, 0x07e0);
        source.anchorSince(start, HudAnchor::BottomLeft);
        start = source.draws().size();
        source.fill({220, 300, 10, 10}, 0x001f);
        source.anchorSince(start, HudAnchor::BottomRight);
        start = source.draws().size();
        source.fill({200, 0, 10, 10}, 0xf81f);
        source.anchorSince(start, HudAnchor::TopRight);
        DecorativeGrid grid;
        grid.valid = true;
        grid.width = 240;
        grid.height = 320;
        grid.colors = {0x80a0ff00u, 0x80a0ff00u, 0x80a0ff00u};
        grid.pattern = {3, 3, 3, 3, 0, 0};
        source.decorativeGrid(grid);
        for (auto size : {std::array<int, 2>{1280, 720}, std::array<int, 2>{720, 1280},
                          std::array<int, 2>{960, 960}}) {
            const auto view = makeViewport(size[0], size[1], Aspect::Fit, 240, 320);
            const float sx = view.width / 240.f, sy = view.height / 320.f;
            const auto a = expandedPresentation(source, size[0], size[1], Aspect::Fit, 0, 0),
                       b = expandedPresentation(source, size[0], size[1], Aspect::Fit, 0, 1);
            check(a.width() == size[0] && a.height() == size[1],
                  "Composition does not fill screen");
            int borders = 0, objects = 0;
            for (const auto &d : a.draws()) {
                if (d.sheet == -1) {
                    ++objects;
                    float x = 0, y = 0;
                    if (d.color == 0xf800) {
                        x = view.x + 70 * sx;
                        y = view.y + 100 * sy;
                    } else if (d.color == 0x07e0) {
                        x = 8 * sx;
                        y = size[1] - view.height + 300 * sy;
                    } else if (d.color == 0x001f) {
                        x = size[0] - view.width + 220 * sx;
                        y = size[1] - view.height + 300 * sy;
                    } else {
                        x = size[0] - view.width + 200 * sx;
                        y = 0;
                    }
                    check(std::abs(d.x - x) < .001f && std::abs(d.y - y) < .001f,
                          "Game viewport or HUD anchor moved incorrectly");
                }
                if (d.sheet == -3 && !d.decorative) {
                    ++borders;
                    check(!d.patternedBeam && d.beamWidth > .8f && d.energy == .4f &&
                              d.color == 0x07e0,
                          "Boundary lost regular glowing vector profile or green 40 percent pulse");
                }
            }
            check(objects == 4 && borders > 4, "Missing gameplay/HUD/boundary geometry");
            check(a.draws().size() == b.draws().size(), "Pulse changes dash geometry");
            for (size_t n = 0; n < a.draws().size(); ++n)
                if (a.draws()[n].sheet == -3 && !a.draws()[n].decorative)
                    check(b.draws()[n].color == 0x07ff &&
                              std::abs(b.draws()[n].energy - .7f) < .0001f,
                          "Boundary does not pulse to cyan at 70 percent");
            const auto mid = expandedPresentation(source, size[0], size[1], Aspect::Fit, 0, .5);
            for (const auto &d : mid.draws())
                if (d.sheet == -3 && !d.decorative)
                    check(std::abs(d.energy - .55f) < .0001f,
                          "Boundary intensity does not interpolate with hue");
            Canvas original;
            drawPreciseBackground(original, grid, 0);
            for (size_t n = 0; n < std::min(size_t(20), original.draws().size()); ++n) {
                const auto &e = original.draws()[n];
                bool found = false;
                for (const auto &d : a.draws())
                    if (d.decorative && d.color == e.color &&
                        std::abs((d.x - view.x) / sx - e.x) < .002f &&
                        std::abs((d.y - view.y) / sy - e.y) < .002f &&
                        std::abs(d.energy - e.energy) < .0001f) {
                        found = true;
                        break;
                    }
                check(found, "Expanded background changes original pan/projection");
            }
        }
        check(source.width() == 240 && source.height() == 320 && source.draws().size() == 4 &&
                  source.draws()[0].x == 70,
              "Composition mutated source");
        for (bool aof : {false, true})
            for (bool portrait : {false, true}) {
                const int w = aof ? 400 : 240, h = aof ? 400 : 320, pw = portrait ? 720 : 1280,
                          ph = portrait ? 1280 : 720;
                const auto v = makeViewport(pw, ph, Aspect::Fit, w, h);
                const float sx = v.width / float(w), sy = v.height / float(h);
                for (int pan : {0, w / 2, w - 1}) {
                    Canvas field;
                    field.dimensions(w, h);
                    field.playfield({true, w * 2, h * 2, double(pan), double(pan) * h / w});
                    // Deliberately unrelated decorative camera: bounds must use real pan.
                    auto unrelated = grid;
                    unrelated.worldX = 999999;
                    unrelated.worldY = -999999;
                    unrelated.valid = false;
                    field.decorativeGrid(unrelated);
                    const auto output = expandedPresentation(field, pw, ph, Aspect::Fit, 0, 0);
                    const auto bounds = field.playfield();
                    const float left = float(v.x - bounds.originX * sx),
                                right = float(v.x + (w * 2 - bounds.originX) * sx);
                    const float top = float(v.y - bounds.originY * sy),
                                bottom = float(v.y + (h * 2 - bounds.originY) * sy);
                    bool l = false, r = false, t = false, b = false;
                    for (const auto &d : output.draws()) {
                        if (d.x == d.endX) {
                            check(std::abs(d.x - left) < .001f || std::abs(d.x - right) < .001f,
                                  "Vertical bound is fixed to the viewport");
                            l |= std::abs(d.x - left) < .001f;
                            r |= std::abs(d.x - right) < .001f;
                        }
                        if (d.y == d.endY) {
                            check(std::abs(d.y - top) < .001f || std::abs(d.y - bottom) < .001f,
                                  "Horizontal bound is fixed to the viewport");
                            t |= std::abs(d.y - top) < .001f;
                            b |= std::abs(d.y - bottom) < .001f;
                        }
                    }
                    check(l && r && t && b, "Missing world edge");
                    if (pan == w - 1 && !portrait)
                        check(left < 0 && right > 0 && right < pw,
                              "Left bound does not scroll offscreen toward the right bound");
                }
                Canvas field;
                field.dimensions(w, h);
                field.playfield({true, w * 2, h * 2, 0., double(h - 1)});
                const auto output = expandedPresentation(field, pw, ph, Aspect::Fit, 0, 1);
                const float top = v.y - (h - 1) * sy, bottom = v.y + (h + 1) * sy;
                if (portrait)
                    check(top < 0 && bottom > 0 && bottom < ph,
                          "Top bound does not scroll offscreen toward the bottom bound");
            }
        Canvas beam;
        beam.beam(100, 100, 120, 120, 0xffff);
        const auto tall = expandedPresentation(beam, 720, 1280, Aspect::Fit, 0, 0, false);
        check(tall.draws().size() == 1 && std::abs(tall.draws()[0].beamWidth - .75f) < .0001f,
              "Vertical overscan changes original beam thickness");
        for (bool aof : {false, true})
            for (bool native : {false, true}) {
                Assets assets;
                assets.load(argv[1]);
                auto game = std::make_unique<Game>(assets);
                game->arse(aof);
                game->setNativeRate(native, 6);
                game->start();
                for (int tick = 0; tick < 240; ++tick) {
                    game->step({100 + tick % 80, 200, 0, 0});
                    if (tick % 12)
                        continue;
                    const auto hash = NativeRateTests::hash(*game);
                    for (int n = 0; n < 3; ++n) {
                        const auto canvas = game->expandedCanvas();
                        const auto &bounds = canvas.playfield();
                        check(bounds.valid && bounds.width == game->player().worldWidth() &&
                                  bounds.height == game->player().worldHeight() &&
                                  bounds.originX == game->player().worldX / 65536. &&
                                  bounds.originY == game->player().worldY / 65536.,
                              "Real gameplay world bounds or pan were not preserved");
                        expandedPresentation(canvas, 1280, 720, Aspect::Fit, 0, n * .5);
                    }
                    check(NativeRateTests::hash(*game) == hash,
                          "Hot-swapping composition changed gameplay");
                }
                NativeRateTests::exterior(*game);
            }
        std::cout << "Draw to Edge: options, HUD, original projection, scrolling world bounds, "
                     "green/cyan 40-70% pulse and two-edition render isolation pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
