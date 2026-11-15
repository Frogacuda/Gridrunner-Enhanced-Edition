#include "grpp/frontend.hpp"
#include "grpp/visuals.hpp"
#include <cmath>
#include <iostream>
#include <memory>
using namespace grpp;
void require(bool c, const char *why) {
    if (!c)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: enhancement_checks ASSETS");
        Assets assets;
        assets.load(argv[1]);
        Canvas canvas;
        PlayerState state;
        state.sheepPower = 6;
        GameRandom random(1);
        EffectRandom effects(1);
        SourceVisuals v(canvas, assets, state, random, effects);
        v.frame(1, 1);
        Entity ship;
        ship.drawing = 0;
        ship.sprite = 358;
        ship.x = fixed16(180) + 32768;
        ship.y = fixed16(160);
        ship.historyCursor = 24;
        for (int i = 0; i < 64; ++i) {
            ship.history[size_t(i)].x = fixed16(60 + i * 5);
            ship.history[size_t(i)].y = fixed16(160);
        }
        v.entity(ship);
        int silhouettes = 0, solid = 0;
        for (const auto &d : canvas.draws()) {
            if (d.silhouette)
                ++silhouettes;
            else
                ++solid;
        }
        require(silhouettes == 5 && solid == 1,
                "Player trail silhouette classification lost leading ship");
        require(canvas.draws().back().subpixelX == .5f, "Subpixel ship position lost");
        canvas.clear();
        ship.drawing = 1;
        ship.scale = 1024;
        ship.transform.tileWidth = ship.transform.tileHeight = 2;
        ship.transform.axisXX = ship.transform.axisYY = 32;
        v.entity(ship);
        require(!canvas.draws().empty(), "Fragment test emitted no geometry");
        for (const auto &d : canvas.draws())
            require(d.squareFragment && !d.silhouette && d.sheet >= 0,
                    "Square shatter entered vector/trail path");
        canvas.clear();
        auto before = random.state();
        v.cross(120, 160, 0x07e0);
        int beams = 0;
        for (const auto &d : canvas.draws())
            if (d.sheet == -3 && !d.referenceOnly)
                ++beams;
        require(beams == 1 && random.state() == before,
                "Vector dot conversion changes RNG or duplicates beams");
        Canvas stars;
        std::array<std::array<int32_t, 3>, 1000> field{};
        field.fill({INT32_MAX, INT32_MAX, 1});
        field[0] = {0, 0, 1};
        drawMenuStars(stars, field, 0);
        int starCount = 0;
        for (const auto &d : stars.draws())
            if (d.sheet == -3 && !d.referenceOnly) {
                ++starCount;
                require(d.energy == 1.f && d.beamWidth == 1.f && !d.patternedBeam,
                        "Stars do not match ordinary pixel dots");
            }
        require(starCount == 1, "Star fixture projected unexpected points");
        canvas.clear();
        Entity crawler;
        crawler.drawing = 11;
        crawler.effectMode = 0;
        crawler.colorCycle = 13;
        for (auto &p : crawler.history) {
            p.x = fixed16(120);
            p.y = fixed16(160);
        }
        auto crawlSeed = random.state();
        v.entity(crawler);
        int crawlerCount = 0;
        for (const auto &d : canvas.draws())
            if (d.sheet == -3 && !d.referenceOnly) {
                ++crawlerCount;
                require(std::abs(d.energy - .60f) < .00001f,
                        "Waiting crawler did not receive a 40 percent reduction");
            }
        require(crawlerCount == 64 && random.state() == crawlSeed,
                "Crawler rendering changed source samples or RNG");
        canvas.clear();
        v.pixel(120, 160, 0xffff);
        require(canvas.draws().back().energy == 1.f, "Crawler dimming leaked to other dots");
        for (int id : {107, 108, 109, 110})
            for (int x : {0, 120, 239})
                for (uint16_t ink : {uint16_t(0), uint16_t(0x07e0)}) {
                    Canvas original, enhanced;
                    SourceVisuals a(original, assets, state, random, effects),
                        b(enhanced, assets, state, random, effects);
                    auto seed = random.state(), noise = effects.state();
                    a.sprite(id, x, 160, ink);
                    b.beamSprite(id, x, 160, ink);
                    auto r1 = std::make_unique<Raster565>(), r2 = std::make_unique<Raster565>();
                    r1->render(original, assets);
                    r2->render(enhanced, assets);
                    require(r1->pixels() == r2->pixels(),
                            "Arc conversion changed reference pixels or clipping");
                    int active = 0;
                    for (const auto &d : enhanced.draws()) {
                        if (d.sheet >= 0)
                            require(d.referenceOnly, "Arc sprite still draws under its beam mask");
                        if (d.sheet == -3) {
                            ++active;
                            require(std::abs(d.energy * .65f - .60f) < .00001f &&
                                        d.color ==
                                            r1->pixels()[size_t(int(d.y)) * 240 + size_t(int(d.x))],
                                    "Arc beam lost 60 percent source-colored intensity");
                        }
                    }
                    require(active > 0 && seed == random.state() && noise == effects.state(),
                            "Arc beams lost geometry or consumed random draws");
                }
        require(SourceVisuals::beamDepthEnergy(4096) == 1 &&
                    SourceVisuals::beamDepthEnergy(8192) < 1 &&
                    SourceVisuals::beamDepthEnergy(16384) < SourceVisuals::beamDepthEnergy(8192) &&
                    SourceVisuals::beamDepthEnergy(100000) > .45f,
                "Projected vector depth falloff is not bounded/monotonic");
        std::cout << "Trail opacity classification, opaque leading ship, fractional placement, "
                     "square fragments and RNG-free beam dots pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
