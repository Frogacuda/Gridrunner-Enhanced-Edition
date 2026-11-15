// Frozen pre-optimization raster from d8f0799, used only for exact pixel comparisons.
#pragma once
#include "grpp/render.hpp"
#include <algorithm>
#include <cmath>
namespace grpp {
inline std::vector<uint16_t> genericRaster(const Canvas &canvas, const Assets &assets,
                                           bool customClip_ = false, Rect clip_ = {}) {
    std::vector<uint16_t> pixels_;
    const int rasterWidth = canvas.width(), rasterHeight = canvas.height();
    pixels_.assign(size_t(rasterWidth) * rasterHeight, canvas.clearColor());
    const Rect clip = customClip_ ? clip_ : Rect{0, 0, rasterWidth - 1, rasterHeight - 1};
    const int left = std::max(clip.x, 0), top = std::max(clip.y, 0);
    const int right = std::min(clip.x + clip.width, rasterWidth),
              bottom = std::min(clip.y + clip.height, rasterHeight);
    for (const Draw &draw : canvas.draws()) {
        if (draw.sheet == -3)
            continue;
        const Image *source = draw.sheet < 0 ? nullptr : &assets.page(size_t(draw.sheet));
        const double c = std::cos(double(draw.angle)), s = std::sin(double(draw.angle));
        const double cx = draw.x + draw.width * .5, cy = draw.y + draw.height * .5;
        const double extentX = std::abs(c * draw.width * .5) + std::abs(s * draw.height * .5);
        const double extentY = std::abs(s * draw.width * .5) + std::abs(c * draw.height * .5);
        const int x0 = std::max(draw.direct ? 0 : left, int(std::floor(cx - extentX))),
                  x1 = std::min(draw.direct ? rasterWidth : right, int(std::ceil(cx + extentX)));
        const int y0 = std::max(draw.direct ? 0 : top, int(std::floor(cy - extentY))),
                  y1 = std::min(draw.direct ? rasterHeight : bottom, int(std::ceil(cy + extentY)));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const double dx = x + .5 - cx, dy = y + .5 - cy;
                const double u = (c * dx + s * dy) / draw.width + .5,
                             v = (-s * dx + c * dy) / draw.height + .5;
                if (u < 0 || u >= 1 || v < 0 || v >= 1)
                    continue;
                uint16_t color = draw.color;
                if (source) {
                    const int sx = draw.source.x + int(u * draw.source.width),
                              sy = draw.source.y + int(v * draw.source.height);
                    if (sx < 0 || sy < 0 || sx >= source->width || sy >= source->height)
                        continue;
                    color = source->at(sx, sy);
                    if (draw.paint.keyed) {
                        if (color == 0) {
                            if (draw.paint.background == 0)
                                continue;
                            color = draw.paint.background;
                        } else if (draw.paint.foreground != 0)
                            color = draw.paint.foreground;
                    }
                }
                if (draw.opacity < 1) {
                    const auto previous = pixels_[size_t(y) * rasterWidth + x];
                    const float a = draw.opacity;
                    const auto channel = [&](int shift, int mask) {
                        return uint16_t(std::lround(float((color >> shift) & mask) * a +
                                                    float((previous >> shift) & mask) * (1 - a)));
                    };
                    color =
                        uint16_t((channel(11, 31) << 11) | (channel(5, 63) << 5) | channel(0, 31));
                }
                pixels_[size_t(y) * rasterWidth + x] = color;
            }
    }
    return pixels_;
}
} // namespace grpp
