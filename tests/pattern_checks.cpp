#include "grpp/visuals.hpp"
#include <cmath>
#include <iostream>
#include <memory>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: pattern_checks ASSETS");
        Assets assets;
        assets.load(argv[1]);
        PlayerState player;
        GameRandom random(1);
        EffectRandom effects(1);
        Canvas canvas;
        SourceVisuals v(canvas, assets, player, random, effects);
        int cases = 0;
        for (bool vertical : {false, true})
            for (int start : {-13, 10})
                for (uint32_t mask :
                     {0u, ~0u, 0x55555555u, 0x01010101u, 0xff00ff00u, 0x80000001u}) {
                    canvas.clear();
                    const auto seed = random.state();
                    if (vertical)
                        v.vertical(50, start, 96, 0x07e0, mask, true);
                    else
                        v.horizontal(start, 50, 96, 0x07e0, mask, true);
                    const int begin = std::max(0, start), end = start + 96 + (vertical ? 1 : 0);
                    for (int pixel = begin; pixel < end; ++pixel) {
                        bool occupied = false;
                        float point = pixel + .5f;
                        for (const auto &d : canvas.draws())
                            if (d.sheet == -3 && !d.referenceOnly) {
                                require(d.patternedBeam,
                                        "Masked stroke lost patterned classification");
                                float a = vertical ? d.y : d.x, b = vertical ? d.endY : d.endX;
                                if (a <= point && point <= b)
                                    occupied = true;
                            }
                        require(
                            occupied == bool(mask & (1u << ((pixel - begin) & 31))),
                            "Beam geometry bridged a source mask gap or shifted its clipped phase");
                    }
                    require(seed == random.state(), "Pattern conversion consumes gameplay RNG");
                    ++cases;
                }
        canvas.clear();
        v.frame(0, 1);
        v.background(0, {1, 2, 5, 3, 1, 1});
        int dots = 0;
        for (const auto &d : canvas.draws())
            if (d.sheet == -3 && d.referenceGeometry && d.patternedBeam && d.x == d.endX &&
                d.y == d.endY)
                ++dots;
        require(dots > 100, "Dotted background mode was replaced with solid crosses");
        // Audit every lit software-reference pixel, not merely total dot count.
        // All four recovered pattern modes and changing projection depths must
        // retain a corresponding enhanced point/axis segment at that location.
        int audits = 0;
        auto raster = std::make_unique<Raster565>();
        for (int modeA = 0; modeA < 4; ++modeA)
            for (int modeB = 0; modeB < 4; ++modeB)
                for (int frame : {0, 17}) {
                    canvas.clear();
                    v.frame(frame, 1.25f);
                    v.background(frame * 123, {1, 2, 5, 3, modeA, modeB});
                    raster->render(canvas, assets);
                    std::array<bool, 240 * 320> covered{};
                    for (const auto &d : canvas.draws())
                        if (d.sheet == -3 && d.referenceGeometry && d.color && d.energy > 0) {
                            if (d.y == d.endY) {
                                int y = int(d.y);
                                for (int x = std::max(0, int(std::floor(std::min(d.x, d.endX))));
                                     x <= std::min(239, int(std::floor(std::max(d.x, d.endX))));
                                     ++x)
                                    if (y >= 0 && y < 320 && x + .5f >= std::min(d.x, d.endX) &&
                                        x + .5f <= std::max(d.x, d.endX))
                                        covered[size_t(y) * 240 + x] = true;
                            }
                            if (d.x == d.endX) {
                                int x = int(d.x);
                                for (int y = std::max(0, int(std::floor(std::min(d.y, d.endY))));
                                     y <= std::min(319, int(std::floor(std::max(d.y, d.endY))));
                                     ++y)
                                    if (x >= 0 && x < 240 && y + .5f >= std::min(d.y, d.endY) &&
                                        y + .5f <= std::max(d.y, d.endY))
                                        covered[size_t(y) * 240 + x] = true;
                            }
                        }
                    for (size_t p = 0; p < covered.size(); ++p)
                        require(!raster->pixels()[p] || covered[p],
                                "Background conversion omitted a lit source pixel");
                    ++audits;
                }
        canvas.clear();
        canvas.beam(1.5f, 2.5f, 1.5f, 2.5f, 1, 1, .65f, true);
        canvas.beam(1.5f, 2.5f, 1.5f, 2.5f, 2, 1, .65f, true);
        canvas.coalescePatternDotsSince(0);
        require(canvas.draws()[0].referenceOnly && !canvas.draws()[1].referenceOnly &&
                    canvas.draws()[1].color == 2,
                "Background shading duplicates no longer preserve last source write");
        std::cout
            << cases << " source mask/clipping cases, " << audits
            << " full background coverage audits, dotted geometry and duplicate shading pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
