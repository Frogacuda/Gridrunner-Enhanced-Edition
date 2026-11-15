#include "grpp/original_overscan.hpp"
#include "grpp/numeric.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace grpp {
OriginalOverscan originalOverscan(const Canvas &source, const Assets &assets, int width, int height,
                                  Aspect aspect, double seconds, bool boundaries) {
    if (width < 1 || height < 1 || source.width() < 1 || source.height() < 1 ||
        !std::isfinite(seconds))
        throw std::invalid_argument("Invalid Original overscan dimensions/time");
    OriginalOverscan result;
    const auto view = makeViewport(width, height, aspect, source.width(), source.height());
    const double sx = double(view.width) / source.width(),
                 sy = double(view.height) / source.height();
    result.padX = std::max(0, int(std::ceil(std::max(view.x, width - view.x - view.width) / sx)));
    result.padY = std::max(0, int(std::ceil(std::max(view.y, height - view.y - view.height) / sy)));
    const int w = source.width() + 2 * result.padX, h = source.height() + 2 * result.padY;
    result.actors.dimensions(w, h);
    result.actors.clear(source.clearColor());
    result.background.assign(size_t(w) * h, source.clearColor());
    auto pixel = [&](int x, int y, uint16_t ink) {
        x += result.padX;
        y += result.padY;
        if (x >= 0 && x < w && y >= 0 && y < h)
            result.background[size_t(y) * w + x] = ink;
    };
    // Match source horizontal inclusive-end and vertical length+1 conventions.
    auto horizontal = [&](int x, int y, int length, uint16_t ink) {
        if (y + result.padY < 0 || y + result.padY >= h)
            return;
        for (int p = std::max(x, -result.padX); p <= std::min(x + length, w - result.padX - 1); ++p)
            pixel(p, y, ink);
    };
    auto vertical = [&](int x, int y, int length, uint16_t ink) {
        if (x + result.padX < 0 || x + result.padX >= w)
            return;
        if (length < 1)
            ++length;
        for (int p = std::max(y, -result.padY); p < std::min(y + length + 1, h - result.padY); ++p)
            pixel(x, p, ink);
    };
    const auto &grid = source.decorativeGrid();
    if (grid.valid) {
        auto pattern = [&](uint32_t c, int ox, int oy, int spacing, int rx, int ry, int shades,
                           int mode) {
            std::array<uint16_t, 33> colors{};
            uint32_t r = 0, g = 0, b = 0;
            const uint32_t dr = ((c >> 8) & 0xff0000u) / shades, dg = (c & 0xff0000u) / shades,
                           db = ((c & 0xff00u) << 8) / shades;
            for (int i = 0; i < shades; ++i) {
                r += dr;
                g += dg;
                b += db;
                colors[size_t(i)] = packOriginalColor(((r & 0xff0000u) << 8) | (g & 0xff0000u) |
                                                      ((b & 0xff0000u) >> 8));
            }
            const int unit = mode == 0 ? 1 : 65536;
            auto first = [&](int origin, int pad, int radius) {
                return origin +
                       int(std::floor(double(-pad * unit - radius - origin) / spacing)) * spacing;
            };
            const int beginX = first(ox, result.padX, rx), beginY = first(oy, result.padY, ry);
            for (int x = beginX; x < (w - result.padX) * unit + rx; x += spacing)
                for (int y = beginY; y < (h - result.padY) * unit + ry; y += spacing) {
                    if (mode == 0) {
                        vertical(x, y - ry, ry * 2, packOriginalColor(c));
                        horizontal(x - rx, y, rx * 2, packOriginalColor(c));
                        continue;
                    }
                    const int stepX = rx / shades, stepY = ry / shades;
                    int left = x - rx, right = x + rx - (mode == 1 ? 0 : stepX);
                    for (int i = 0; i < shades; ++i) {
                        const auto ink = colors[size_t(i)];
                        if (mode == 1) {
                            pixel(grpp::pixel(left), grpp::pixel(y), ink);
                            pixel(grpp::pixel(right), grpp::pixel(y), ink);
                        } else {
                            horizontal(grpp::pixel(left), grpp::pixel(mode == 3 ? y - ry : y),
                                       grpp::pixel(stepX), ink);
                            horizontal(grpp::pixel(right), grpp::pixel(mode == 3 ? y - ry : y),
                                       grpp::pixel(stepX), ink);
                            if (mode == 3) {
                                horizontal(grpp::pixel(left), grpp::pixel(y + ry),
                                           grpp::pixel(stepX), ink);
                                horizontal(grpp::pixel(right), grpp::pixel(y + ry),
                                           grpp::pixel(stepX), ink);
                            }
                        }
                        left += stepX;
                        right -= stepX;
                    }
                    int top = y - ry, bottom = y + ry - (mode == 1 ? 0 : stepY);
                    for (int i = 0; i < shades; ++i) {
                        const auto ink = colors[size_t(i)];
                        if (mode == 1) {
                            pixel(grpp::pixel(x), grpp::pixel(top), ink);
                            pixel(grpp::pixel(x), grpp::pixel(bottom), ink);
                        } else {
                            vertical(grpp::pixel(mode == 3 ? x - rx : x), grpp::pixel(top),
                                     grpp::pixel(stepY), ink);
                            vertical(grpp::pixel(mode == 3 ? x - rx : x), grpp::pixel(bottom),
                                     grpp::pixel(stepY), ink);
                            if (mode == 3) {
                                vertical(grpp::pixel(x + rx), grpp::pixel(top), grpp::pixel(stepY),
                                         ink);
                                vertical(grpp::pixel(x + rx), grpp::pixel(bottom),
                                         grpp::pixel(stepY), ink);
                            }
                        }
                        top += mode == 1 ? stepY : stepX;
                        bottom -= mode == 1 ? stepY : stepX;
                    }
                }
        };
        // Original's cached pixel commands use actual pan, not the Enhanced
        // decorative-camera interpolation that the host may apply to metadata.
        const auto &field = source.playfield();
        const int32_t worldX = field.valid ? int32_t(field.originX * 65536.) : int32_t(grid.worldX);
        const int32_t worldY = field.valid ? int32_t(field.originY * 65536.) : int32_t(grid.worldY);
        const int dx = add32(asr32(sub32(fixed16(source.width() / 2), worldX), 5), 0x20000);
        const int dy = add32(asr32(sub32(fixed16(source.width()), worldY), 5), 0x20000);
        const int frame = int(std::floor(grid.frame));
        const int advance =
            int(std::lround(grid.advance - (grid.frame - frame) * grid.advancePerFrame));
        auto layer = [&](uint32_t c, int time, int count, uint32_t flags) {
            uint32_t r = 0, g = 0, b = 0;
            for (int depth = 4096 - time % 512 + count * 512; depth > 512; depth -= 512) {
                r = std::min(r + ((c >> 8) & 0xff0000u) / uint32_t(count), 0xff0000u);
                g = std::min(g + (c & 0xff0000u) / uint32_t(count), 0xff0000u);
                b = std::min(b + ((c & 0xff00u) << 8) / uint32_t(count), 0xff0000u);
                const uint32_t ink = (r & 0xff0000u) << 8 | (g & 0xff0000u) | (b & 0xff0000u) >> 8;
                const int spacing = grid.projection / depth,
                          x = (dx / depth + source.width() / 2) % spacing,
                          y = (dy / depth + source.height() / 2) % spacing,
                          mode = int(flags & 255u);
                if (mode == 0)
                    pattern(ink, x, y, spacing, spacing >> 3, spacing >> 3, 1, 0);
                else
                    pattern(ink, fixed16(x), fixed16(y), fixed16(spacing),
                            asr32(fixed16(spacing), (flags >> 16) & 31u),
                            asr32(fixed16(spacing), (flags >> 8) & 31u), int(flags >> 24), mode);
            }
        };
        const auto &p = grid.pattern;
        layer(grid.colors[0], add32(mul32(frame, 64), advance), 4,
              uint32_t(p[0] << 16) | 0x0f000000u | uint32_t(p[1] << 8) | uint32_t(p[4]));
        layer(grid.colors[1], add32(mul32(frame, 32), advance), 2,
              uint32_t(p[2] << 16) | 0x04000000u | uint32_t(p[3] << 8) | uint32_t(p[5]));
        layer(grid.colors[2], advance, 4, 0x04040402u);
        // Preserve the source viewport's exact raster/clipping, including edge
        // quirks. The extended lattice supplies only the new exterior area.
        Canvas center;
        center.dimensions(source.width(), source.height());
        center.clear(source.clearColor());
        for (size_t i = 0; i < grid.first; ++i)
            center.appendDraw(source.draws().at(i));
        Raster565 raster;
        raster.render(center, assets);
        for (int y = 0; y < source.height(); ++y)
            std::copy_n(raster.pixels().data() + size_t(y) * source.width(), source.width(),
                        result.background.data() + size_t(y + result.padY) * w + result.padX);
    }
    if (source.starField().valid) {
        const auto &field = source.starField();
        const int travel = mul32(field.frame, -0x40000);
        for (const auto &star : field.stars) {
            const int depth = int((uint32_t(add32(travel, star[2])) & 0xffffffu) + 1);
            const int x = add32(star[0] / depth, 120), y = add32(star[1] / depth, 160);
            if (x < 0 || x >= source.width() || y < 0 || y >= source.height())
                pixel(x, y, 0xffff);
        }
    }
    for (size_t i = grid.valid ? grid.first : 0; i < source.draws().size(); ++i) {
        auto d = source.draws()[i];
        if (d.sheet == -3)
            continue;
        d.x += result.padX;
        d.y += result.padY;
        if (d.hudAnchor == HudAnchor::BottomLeft) {
            d.x -= float(view.x / sx);
            d.y += float((height - view.height - view.y) / sy);
        } else if (d.hudAnchor == HudAnchor::BottomRight) {
            d.x += float((width - view.width - view.x) / sx);
            d.y += float((height - view.height - view.y) / sy);
        } else if (d.hudAnchor == HudAnchor::TopRight) {
            d.x += float((width - view.width - view.x) / sx);
            d.y -= float(view.y / sy);
        } else if (d.hudAnchor == HudAnchor::BottomCenter)
            d.y += float((height - view.height - view.y) / sy);
        // Keep the framebuffer's native pixel grid for Original HUD glyphs.
        if (d.hudAnchor != HudAnchor::None) {
            d.x = std::round(d.x);
            d.y = std::round(d.y);
        }
        result.actors.appendDraw(d);
    }
    if (boundaries && source.playfield().valid) {
        const auto &f = source.playfield();
        const double pulse = (1 - std::cos(seconds * 3.141592653589793)) * .5,
                     energy = .4 + .3 * pulse;
        const uint16_t ink =
            uint16_t((int(std::lround(63 * energy)) << 5) | int(std::lround(31 * pulse * energy)));
        auto line = [&](double x, double y, int length, bool vert) {
            for (int a = 0; a < length; a += 8) {
                Draw d;
                d.direct = true;
                d.color = ink;
                d.x = float(result.padX + x + (vert ? 0 : a));
                d.y = float(result.padY + y + (vert ? a : 0));
                d.x = std::floor(d.x);
                d.y = std::floor(d.y);
                d.width = float(vert ? 1 : std::min(4, length - a));
                d.height = float(vert ? std::min(4, length - a) : 1);
                result.actors.appendDraw(d);
            }
        };
        line(-f.originX, -f.originY, f.width, false);
        line(-f.originX, f.height - f.originY, f.width, false);
        line(-f.originX, -f.originY, f.height, true);
        line(f.width - f.originX, -f.originY, f.height, true);
    }
    return result;
}
} // namespace grpp
