#include "grpp/background.hpp"
#include "grpp/numeric.hpp"
#include "grpp/phosphor.hpp"
#include "original_visuals.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace grpp {
std::array<int, 6> stageBackground(unsigned stage) {
    stage %= 50;
    if (stage == 0)
        return original::initialBackground;
    // A permutation of the 2,304 legal post-clear recipes gives each of the
    // remaining 49 levels a distinct, repeatable recipe without rand() calls.
    unsigned recipe = stage * 401u % 2304u;
    std::array<int, 6> result{};
    for (int n = 0; n < 4; ++n) {
        result[size_t(n)] = int(recipe % 4) + 3;
        recipe /= 4;
    }
    result[4] = int(recipe % 3) + 1;
    recipe /= 3;
    result[5] = int(recipe % 3) + 1;
    return result;
}
namespace {
float depthEnergy(double depth) { return backgroundBeamEnergy(depth); }
void stroke(Canvas &c, double ax, double ay, double bx, double by, uint16_t color, float energy,
            bool dotted = false) {
    if (!color || energy <= 0)
        return;
    // Match source pixel-center convention, retaining fractional positions.
    c.beam(float(ax + .5), float(ay + .5), float(bx + .5), float(by + .5), color, energy, 1.f,
           dotted);
    c.markDecorativeLast();
}
void pattern(Canvas &out, uint32_t color, double anchorX, double anchorY, double spacing, double rx,
             double ry, int shades, int mode, float energy, int width, int height, int compact) {
    std::array<uint16_t, 33> colors{};
    uint32_t r = 0, g = 0, b = 0;
    const uint32_t dr = ((color >> 8) & 0xff0000u) / uint32_t(shades),
                   dg = (color & 0xff0000u) / uint32_t(shades),
                   db = ((color & 0xff00u) << 8) / uint32_t(shades);
    for (int i = 0; i < shades; ++i) {
        r += dr;
        g += dg;
        b += db;
        colors[size_t(i)] =
            packOriginalColor(((r & 0xff0000u) << 8) | (g & 0xff0000u) | ((b & 0xff0000u) >> 8));
    }
    const double stepX = rx / shades, stepY = ry / shades;
    // Stable lattice indices avoid modulo-origin jumps as spacing changes.
    const int firstX = int(std::ceil((-rx - 1 - anchorX) / spacing)),
              lastX = int(std::floor((width + rx + 1 - anchorX) / spacing));
    const int firstY = int(std::ceil((-ry - 1 - anchorY) / spacing)),
              lastY = int(std::floor((height + ry + 1 - anchorY) / spacing));
    for (int ix = firstX; ix <= lastX; ++ix)
        for (int iy = firstY; iy <= lastY; ++iy) {
            const double x = anchorX + ix * spacing, y = anchorY + iy * spacing;
            if (mode == 0) {
                stroke(out, x, y - ry, x, y + ry, packOriginalColor(color), energy);
                stroke(out, x - rx, y, x + rx, y, packOriginalColor(color), energy);
                continue;
            }
            if (compact && mode >= 2) {
                // Fuse only contiguous, equally spaced source samples. The recovered
                // vertical asymmetry can produce gaps/overlaps: retain those draws.
                auto ramp = [&](double ax, double ay, double bx, double by) {
                    Draw d;
                    d.sheet = -3;
                    d.x = float(ax + .5);
                    d.y = float(ay + .5);
                    d.endX = float(bx + .5);
                    d.endY = float(by + .5);
                    d.rampColor = color;
                    d.color = colors[0];
                    d.rampEndColor = colors[size_t(shades - 1)];
                    d.rampSteps = compact == 3 ? -(shades + 32) : compact == 2 ? -shades : shades;
                    d.energy = energy;
                    d.beamWidth = 1;
                    d.decorative = true;
                    out.appendDraw(d);
                };
                const double yy = mode == 3 ? y - ry : y;
                ramp(x - rx, yy, x, yy);
                ramp(x + rx, yy, x, yy);
                if (mode == 3) {
                    ramp(x - rx, y + ry, x, y + ry);
                    ramp(x + rx, y + ry, x, y + ry);
                }
                if (std::abs(stepX - stepY) < 1.e-9) {
                    const double xx = mode == 3 ? x - rx : x;
                    ramp(xx, y - ry, xx, y);
                    ramp(xx, y + ry, xx, y);
                    if (mode == 3) {
                        ramp(x + rx, y - ry, x + rx, y);
                        ramp(x + rx, y + ry, x + rx, y);
                    }
                    continue;
                }
            }
            for (int i = 0; i < shades; ++i) {
                const auto ink = colors[size_t(i)];
                const double left = x - rx + i * stepX,
                             right = x + rx - (mode == 1 ? 0 : stepX) - i * stepX;
                if (mode == 1) {
                    // Preserve integrated energy when many shaded source samples
                    // occupy less than one old logical pixel; do not stack them
                    // into a newly overbright line when removing quantization.
                    const float e = energy * float(std::min(1., stepX));
                    stroke(out, left, y, left, y, ink, e, true);
                    stroke(out, right, y, right, y, ink, e, true);
                } else if (!compact) {
                    const double yy = mode == 3 ? y - ry : y;
                    stroke(out, left, yy, left + stepX, yy, ink, energy);
                    stroke(out, right, yy, right + stepX, yy, ink, energy);
                    if (mode == 3) {
                        stroke(out, left, y + ry, left + stepX, y + ry, ink, energy);
                        stroke(out, right, y + ry, right + stepX, y + ry, ink, energy);
                    }
                }
                // Retain the recovered vertical step asymmetry in modes 2/3.
                const double progression = i * (mode == 1 ? stepY : stepX);
                const double top = y - ry + progression,
                             bottom = y + ry - (mode == 1 ? 0 : stepY) - progression;
                if (mode == 1) {
                    const float e = energy * float(std::min(1., stepY));
                    stroke(out, x, top, x, top, ink, e, true);
                    stroke(out, x, bottom, x, bottom, ink, e, true);
                } else if (!compact || std::abs(stepX - stepY) >= 1.e-9) {
                    const double xx = mode == 3 ? x - rx : x;
                    stroke(out, xx, top, xx, top + stepY, ink, energy);
                    stroke(out, xx, bottom, xx, bottom + stepY, ink, energy);
                    if (mode == 3) {
                        stroke(out, x + rx, top, x + rx, top + stepY, ink, energy);
                        stroke(out, x + rx, bottom, x + rx, bottom + stepY, ink, energy);
                    }
                }
            }
        }
}
} // namespace
std::vector<GridPlane> projectGrid(const DecorativeGrid &grid, double frameOffset) {
    std::vector<GridPlane> planes;
    planes.reserve(32);
    if (!std::isfinite(frameOffset) || frameOffset < 0 || frameOffset > 1)
        throw std::invalid_argument("Decorative offset must be within one source frame");
    if (!std::isfinite(grid.frame) || !std::isfinite(grid.advance) ||
        !std::isfinite(grid.advancePerFrame) || !std::isfinite(grid.worldX) ||
        !std::isfinite(grid.worldY) || std::abs(grid.worldX) > 1.e12 ||
        std::abs(grid.worldY) > 1.e12)
        throw std::invalid_argument("Invalid decorative projection");
    for (int i = 0; i < 4; ++i)
        if (grid.pattern[size_t(i)] < 0 || grid.pattern[size_t(i)] > 31)
            throw std::invalid_argument("Invalid decorative spacing");
    if (grid.pattern[4] < 0 || grid.pattern[4] > 3 || grid.pattern[5] < 0 || grid.pattern[5] > 3)
        throw std::invalid_argument("Invalid decorative pattern");
    const double sourceWidth = grid.sourceWidth ? grid.sourceWidth : grid.width;
    const double sourceHeight = grid.sourceHeight ? grid.sourceHeight : grid.height;
    const double dx = ((sourceWidth / 2.) * 65536 - grid.worldX) / 32. + 131072,
                 dy = (grid.cameraY * 65536. - grid.worldY) / 32. + 131072;
    auto layer = [&](uint32_t color, double time, int count, int shiftX, int shiftY, int shades,
                     int mode) {
        double phase = std::fmod(time, 512.);
        if (phase < 0)
            phase += 512.;
        for (double depth = 4096 - phase + count * 512.; depth > 512.; depth -= 512.) {
            const int index = int(std::llround((4096 - phase + count * 512. - depth) / 512.)) + 1;
            // Use the original fixed-point shade accumulation, including its
            // fractional channel increments, rather than integer RGB division.
            const uint32_t r =
                std::min(((color >> 8) & 0xff0000u) / unsigned(count) * unsigned(index), 0xff0000u);
            const uint32_t g =
                std::min((color & 0xff0000u) / unsigned(count) * unsigned(index), 0xff0000u);
            const uint32_t b =
                std::min(((color & 0xff00u) << 8) / unsigned(count) * unsigned(index), 0xff0000u);
            const uint32_t packed = (r & 0xff0000u) << 8 | (g & 0xff0000u) | (b & 0xff0000u) >> 8;
            const double spacing = double(grid.projection) / depth;
            const double x = grid.offsetX + sourceWidth / 2. + dx / depth,
                         y = grid.offsetY + sourceHeight / 2. + dy / depth;
            const double rx = spacing / std::exp2(mode == 0 ? 3 : shiftX),
                         ry = spacing / std::exp2(mode == 0 ? 3 : shiftY);
            planes.push_back({x, y, spacing, rx, ry, packed, depthEnergy(depth),
                              mode == 0 ? 1 : shades, mode, int(std::ceil((-rx - 1 - x) / spacing)),
                              int(std::floor((grid.width + rx + 1 - x) / spacing)),
                              int(std::ceil((-ry - 1 - y) / spacing)),
                              int(std::floor((grid.height + ry + 1 - y) / spacing))});
        }
    };
    const auto &p = grid.pattern;
    const double frame = grid.frame + frameOffset;
    const double advance = grid.advance + frameOffset * grid.advancePerFrame;
    layer(grid.colors[0], frame * 64 + advance, 4, p[0], p[1], 15, p[4]);
    layer(grid.colors[1], frame * 32 + advance, 2, p[2], p[3], 4, p[5]);
    layer(grid.colors[2], advance, 4, 4, 4, 4, 2);
    return planes;
}
void drawPreciseBackground(Canvas &canvas, const DecorativeGrid &grid, double frameOffset,
                           int compact) {
    if (!((compact >= 0 && compact <= 4) || compact == 10 || compact == 11 || compact == 12))
        throw std::invalid_argument("Invalid grid rendering mode");
    const auto planes = projectGrid(grid, frameOffset);
    if (compact >= 4) {
        auto metadata = grid;
        metadata.gpuMode = compact;
        canvas.decorativeGrid(metadata);
        Draw marker;
        marker.sheet = -4;
        marker.decorative = true;
        canvas.appendDraw(marker);
        return;
    }
    for (const auto &p : planes)
        pattern(canvas, p.color, p.x, p.y, p.spacing, p.rx, p.ry, p.shades, p.mode, p.energy,
                grid.width, grid.height, compact);
}
void drawPreciseStars(Canvas &canvas, const DecorativeStars &field, double frameOffset) {
    if (!std::isfinite(frameOffset) || frameOffset < 0 || frameOffset > 1)
        throw std::invalid_argument("Invalid star phase");
    constexpr double period = 16777216.;
    const double travel = mul32(field.frame, -0x40000) - frameOffset * 262144.;
    for (const auto &star : field.stars) {
        double depth = std::fmod(travel + double(star[2]), period);
        if (depth < 0)
            depth += period;
        depth += 1.;
        const double x = double(star[0]) / depth + 120., y = double(star[1]) / depth + 160.;
        if (x > 0 && x < 240 && y > 0 && y < 320)
            canvas.beam(float(x + .5), float(y + .5), float(x + .5), float(y + .5), 0xffff, 1.f,
                        1.f);
    }
}
Canvas resampleDecoration(const Canvas &source, double frameOffset, int compact) {
    Canvas result = source;
    if (source.decorativeGrid().valid) {
        Canvas replacement;
        drawPreciseBackground(replacement, source.decorativeGrid(), frameOffset, compact);
        result.replaceDecoration(replacement);
        auto grid = result.decorativeGrid();
        grid.gpuMode = compact >= 4 ? compact : 0;
        result.decorativeGrid(grid);
    }
    if (source.starField().valid) {
        Canvas replacement;
        drawPreciseStars(replacement, source.starField(), frameOffset);
        result.replaceStars(replacement);
    }
    return result;
}
void DecorativeCamera::capture(const Canvas &source, uint64_t continuity) {
    const auto &grid = source.decorativeGrid();
    if (!grid.valid) {
        reset();
        return;
    }
    if (!valid_ || continuity_ != continuity) {
        previousX_ = currentX_ = grid.worldX;
        previousY_ = currentY_ = grid.worldY;
    } else {
        previousX_ = currentX_;
        previousY_ = currentY_;
        currentX_ = grid.worldX;
        currentY_ = grid.worldY;
    }
    valid_ = true;
    continuity_ = continuity;
}
void DecorativeCamera::apply(Canvas &canvas, double fraction) const {
    if (!valid_ || !canvas.decorativeGrid().valid)
        return;
    if (!std::isfinite(fraction))
        throw std::invalid_argument("Non-finite decorative camera phase");
    fraction = std::clamp(fraction, 0., 1.);
    auto grid = canvas.decorativeGrid();
    grid.worldX = previousX_ + (currentX_ - previousX_) * fraction;
    grid.worldY = previousY_ + (currentY_ - previousY_) * fraction;
    canvas.decorativeGrid(grid);
}
} // namespace grpp
