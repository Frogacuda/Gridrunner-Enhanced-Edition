#include "grpp/render.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace grpp {
void Canvas::clear(uint16_t color) {
    clear_ = color;
    draws_.clear();
    decorative_ = {};
    stars_ = {};
    playfield_ = {};
}
void Canvas::centeredUi(const Canvas &ui) {
    const float dx = (width_ - ui.width()) * .5f, dy = (height_ - ui.height()) * .5f;
    for (auto d : ui.draws()) {
        d.x += dx;
        d.y += dy;
        if (d.sheet == -3) {
            d.endX += dx;
            d.endY += dy;
        }
        draws_.push_back(d);
    }
}
void Canvas::replaceDecoration(const Canvas &replacement) {
    if (!decorative_.valid)
        return;
    if (decorative_.first > decorative_.last || decorative_.last > draws_.size())
        throw std::logic_error("Invalid decorative span");
    for (size_t i = decorative_.first; i < decorative_.last; ++i)
        if (!draws_[i].decorative)
            throw std::logic_error("Nondecorative draw in background span");
    const auto first = decorative_.first;
    const auto last = decorative_.last, removed = last - first, added = replacement.draws_.size();
    if (stars_.valid && stars_.first >= last) {
        stars_.first = stars_.first - removed + added;
        stars_.last = stars_.last - removed + added;
    }
    draws_.erase(draws_.begin() + first, draws_.begin() + decorative_.last);
    draws_.insert(draws_.begin() + first, replacement.draws_.begin(), replacement.draws_.end());
    decorative_.last = first + replacement.draws_.size();
}
void Canvas::replaceStars(const Canvas &replacement) {
    if (!stars_.valid)
        return;
    const auto first = stars_.first, last = stars_.last;
    if (first > last || last > draws_.size())
        throw std::logic_error("Invalid star span");
    for (size_t i = first; i < last; ++i)
        if (draws_[i].sheet != -3 || draws_[i].referenceOnly)
            throw std::logic_error("Non-star draw in star span");
    const auto removed = last - first, added = replacement.draws_.size();
    if (decorative_.valid && decorative_.first >= last) {
        decorative_.first = decorative_.first - removed + added;
        decorative_.last = decorative_.last - removed + added;
    }
    draws_.erase(draws_.begin() + first, draws_.begin() + last);
    draws_.insert(draws_.begin() + first, replacement.draws_.begin(), replacement.draws_.end());
    stars_.last = first + added;
}
void Canvas::beam(float ax, float ay, float bx, float by, uint16_t color, float energy, float width,
                  bool patterned) {
    Draw draw;
    draw.sheet = -3;
    draw.x = ax;
    draw.y = ay;
    draw.endX = bx;
    draw.endY = by;
    draw.color = color;
    draw.energy = energy;
    draw.beamWidth = width;
    draw.patternedBeam = patterned;
    draw.direct = true;
    draws_.push_back(draw);
}
void Canvas::referenceOnlySince(size_t first) {
    for (size_t i = first; i < draws_.size(); ++i)
        draws_[i].referenceOnly = true;
}
void Canvas::coalescePatternDotsSince(size_t first) {
    std::unordered_map<uint32_t, size_t> seen;
    for (size_t i = first; i < draws_.size(); ++i) {
        auto &d = draws_[i];
        if (d.sheet != -3 || d.referenceOnly || !d.patternedBeam || d.x != d.endX || d.y != d.endY)
            continue;
        const uint32_t key = (uint32_t(d.x * 2) << 16) | uint32_t(d.y * 2);
        auto prior = seen.find(key);
        if (prior != seen.end())
            draws_[prior->second].referenceOnly = true;
        seen[key] = i;
    }
}
void Canvas::sprite(const Assets &assets, size_t index, int cx, int cy, Paint paint) {
    const auto &r = assets.sprite(index);
    image(r.sheet, {r.x, r.y, r.width, r.height}, float(cx - (r.width >> 1)),
          float(cy - (r.height >> 1)), float(r.width), float(r.height), paint);
    identifySpriteLast(int(index));
}
void Canvas::image(int sheet, Rect source, float x, float y, float width, float height, Paint paint,
                   float angle) {
    if (width <= 0 || height <= 0 || source.width <= 0 || source.height <= 0)
        return;
    draws_.push_back({sheet, source, x, y, width, height, angle, paint, 0});
}
void Canvas::fill(Rect r, uint16_t color) {
    if (r.width <= 0 || r.height <= 0)
        return;
    draws_.push_back(
        {-1, {}, float(r.x), float(r.y), float(r.width), float(r.height), 0, {}, color});
}
void Canvas::alphaFill(Rect r, uint16_t color, float opacity) {
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw std::invalid_argument("Invalid opacity");
    if (r.width <= 0 || r.height <= 0)
        return;
    fill(r, color);
    draws_.back().opacity = opacity;
    draws_.back().direct = true;
}
void Canvas::selection(std::array<int, 4> r, uint16_t color) {
    const int left = std::clamp(r[0], 2, width_ - 4), top = std::clamp(r[1], 2, height_ - 4);
    const int right = std::clamp(r[2], left + 1, width_ - 3),
              bottom = std::clamp(r[3], top + 1, height_ - 3);
    const auto first = draws_.size();
    line(left, top, right - left, 1, color);
    line(left, bottom, right - left, 1, color);
    line(left, top, 1, bottom - top, color);
    line(right, top, 1, bottom - top, color);
    referenceOnlySince(first);
    beam(left + .5f, top + .5f, right + .5f, top + .5f, color);
    beam(left + .5f, bottom + .5f, right + .5f, bottom + .5f, color);
    beam(left + .5f, top + .5f, left + .5f, bottom + .5f, color);
    beam(right + .5f, top + .5f, right + .5f, bottom + .5f, color);
}
void Canvas::line(int x, int y, int width, int height, uint16_t color) {
    fill({x, y, width, height}, color);
}
void Canvas::rawFill(Rect r, uint16_t color) {
    if (r.width > 0 && r.height > 0)
        draws_.push_back(
            {-1, {}, float(r.x), float(r.y), float(r.width), float(r.height), 0, {}, color, true});
}
void Canvas::text(const char *text, int x, int y, uint16_t foreground, uint16_t background) {
    const int start = x;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if (*p == '\n') {
            y += 8;
            x = start;
            continue;
        }
        if (*p == ' ') {
            if (background)
                fill({x, y, 8, 8}, background);
            x += 8;
            continue;
        }
        image(0, {int(*p % 32) * 8, int(*p / 32) * 8, 8, 8}, float(x), float(y), 8, 8,
              {true, foreground, background});
        x += 8;
    }
}
void Raster565::render(const Canvas &canvas, const Assets &assets,
                       const std::vector<uint16_t> *initialPixels) {
    const int rasterWidth = canvas.width(), rasterHeight = canvas.height();
    if (initialPixels) {
        if (initialPixels->size() != size_t(rasterWidth) * rasterHeight)
            throw std::invalid_argument("Invalid initial raster size");
        pixels_ = *initialPixels;
    } else
        pixels_.assign(size_t(rasterWidth) * rasterHeight, canvas.clearColor());
    const Rect clip = customClip_ ? clip_ : Rect{0, 0, rasterWidth - 1, rasterHeight - 1};
    const int left = std::max(clip.x, 0), top = std::max(clip.y, 0);
    const int right = std::min(clip.x + clip.width, rasterWidth),
              bottom = std::min(clip.y + clip.height, rasterHeight);
    for (const Draw &draw : canvas.draws()) {
        if (draw.sheet == -3)
            continue;
        const Image *source = draw.sheet < 0 ? nullptr : &assets.page(size_t(draw.sheet));
        // Exact source-pixel blits/fills need no rotation, normalized UVs or
        // floating-point division per pixel. Fractional/scaled draws retain the
        // general path below, including its original edge sampling semantics.
        if (draw.angle == 0 && draw.opacity >= 1 && draw.width > 0 && draw.height > 0 &&
            draw.x >= -32768 && draw.x <= 32768 && draw.y >= -32768 && draw.y <= 32768 &&
            draw.width <= 32768 && draw.height <= 32768 && draw.x == std::floor(draw.x) &&
            draw.y == std::floor(draw.y) && draw.width == std::floor(draw.width) &&
            draw.height == std::floor(draw.height) &&
            (!source || (draw.width == draw.source.width && draw.height == draw.source.height &&
                         draw.source.x >= -32768 && draw.source.x <= 32768 &&
                         draw.source.y >= -32768 && draw.source.y <= 32768))) {
            const int dx = int(draw.x), dy = int(draw.y);
            int x0 = std::max(draw.direct ? 0 : left, dx),
                x1 = std::min(draw.direct ? rasterWidth : right, dx + int(draw.width));
            int y0 = std::max(draw.direct ? 0 : top, dy),
                y1 = std::min(draw.direct ? rasterHeight : bottom, dy + int(draw.height));
            if (source) {
                x0 = std::max(x0, dx - draw.source.x);
                x1 = std::min(x1, dx + source->width - draw.source.x);
                y0 = std::max(y0, dy - draw.source.y);
                y1 = std::min(y1, dy + source->height - draw.source.y);
            }
            if (x0 >= x1 || y0 >= y1)
                continue;
            for (int y = y0; y < y1; ++y) {
                auto *output = pixels_.data() + size_t(y) * rasterWidth + x0;
                if (!source) {
                    std::fill_n(output, x1 - x0, draw.color);
                    continue;
                }
                const auto *input = source->pixels.data() +
                                    size_t(draw.source.y + y - dy) * source->width + draw.source.x +
                                    x0 - dx;
                for (int x = x0; x < x1; ++x, ++input, ++output) {
                    auto color = *input;
                    if (draw.paint.keyed) {
                        if (color == 0) {
                            if (draw.paint.background == 0)
                                continue;
                            color = draw.paint.background;
                        } else if (draw.paint.foreground != 0)
                            color = draw.paint.foreground;
                    }
                    *output = color;
                }
            }
            continue;
        }
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
}
std::array<float, 2> orientationPointer(float x, float y, int width, int height, int orientation) {
    if (orientation == 1)
        return {y, x == 0 ? std::nextafter(float(width), 0.f) : float(width) - x};
    if (orientation == 2)
        return {y == 0 ? std::nextafter(float(height), 0.f) : float(height) - y, x};
    if (orientation == 3)
        return {x == 0 ? std::nextafter(float(width), 0.f) : float(width) - x,
                y == 0 ? std::nextafter(float(height), 0.f) : float(height) - y};
    return {x, y};
}
float controllerGain(float magnitude) {
    const float value = std::clamp((magnitude - .18f) / .82f, 0.f, 1.f);
    return 1.5f * value * value;
}
bool Viewport::toLogical(float px, float py, float &lx, float &ly) const {
    if (width <= 0 || height <= 0)
        return false;
    lx = (px - float(x)) * logicalWidth / float(width);
    ly = (py - float(y)) * logicalHeight / float(height);
    return px >= x && py >= y && px < x + width && py < y + height;
}
Viewport makeViewport(int width, int height, Aspect mode, int logicalWidth, int logicalHeight) {
    if (width <= 0 || height <= 0)
        return {};
    if (mode == Aspect::Stretch)
        return {0, 0, width, height, logicalWidth, logicalHeight};
    double scale = std::min(double(width) / logicalWidth, double(height) / logicalHeight);
    if (mode == Aspect::Integer && scale >= 1)
        scale = std::floor(scale);
    const int w = std::max(1, int(std::floor(logicalWidth * scale))),
              h = std::max(1, int(std::floor(logicalHeight * scale)));
    return {(width - w) / 2, (height - h) / 2, w, h, logicalWidth, logicalHeight};
}
namespace {
int tileCount(int dimension, int tile) {
    if (dimension < 1 || tile < 1)
        throw std::invalid_argument("Invalid tile map dimensions");
    return dimension / tile + 1;
}
} // namespace
TileMap::TileMap(int width, int height, int tileWidth, int tileHeight, int sheet)
    : columns_(tileCount(width, tileWidth)), rows_(tileCount(height, tileHeight)),
      tileWidth_(tileWidth), tileHeight_(tileHeight), sheet_(sheet),
      cells_(size_t(columns_) * rows_, 32) {
    if (width < 1 || height < 1 || tileWidth < 1 || tileHeight < 1)
        throw std::invalid_argument("Invalid tile map dimensions");
}
void TileMap::clear(uint32_t cell) { std::fill(cells_.begin(), cells_.end(), cell); }
void TileMap::put(int x, int y, uint32_t cell) {
    if (x < 0 || y < 0 || x >= columns_ || y >= rows_)
        return;
    cells_[size_t(y) * columns_ + x] = cell;
}
uint32_t TileMap::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= columns_ || y >= rows_)
        return 32;
    return cells_[size_t(y) * columns_ + x];
}
void TileMap::text(const char *string, int x, int y, int colorIndex) {
    const int start = x;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(string); *p; ++p) {
        if (*p == '\n') {
            ++y;
            x = start;
            continue;
        }
        put(x++, y, uint32_t(*p) | 0xff00u | (uint32_t(colorIndex & 255) << 16));
    }
}
Rect TileMap::centeredText(const char *string, int row, uint32_t attributes) {
    const std::string text(string);
    const int start = (columns_ >> 1) - (int(text.size()) >> 1);
    for (size_t i = 0; i < text.size(); ++i)
        put(start + int(i), row, uint32_t(int32_t(static_cast<signed char>(text[i]))) | attributes);
    return {start * tileWidth_, row * tileHeight_, (int(text.size()) + 1) * tileWidth_,
            tileHeight_};
}
void TileMap::scroll(int dx, int dy) {
    // 00407ee5 wraps by one tile per invocation; retain source behavior rather
    // than treating this as an arbitrary large-displacement modulo operation.
    scrollX_ += dx;
    scrollY_ += dy;
    if (scrollX_ < 0) {
        scrollX_ += tileWidth_;
        columnOffset_ = (columnOffset_ + columns_ - 1) % columns_;
    } else if (scrollX_ >= tileWidth_) {
        scrollX_ -= tileWidth_;
        columnOffset_ = (columnOffset_ + 1) % columns_;
    }
    if (scrollY_ < 0) {
        scrollY_ += tileHeight_;
        rowOffset_ = (rowOffset_ + rows_ - 1) % rows_;
    } else if (scrollY_ >= tileHeight_) {
        scrollY_ -= tileHeight_;
        rowOffset_ = (rowOffset_ + 1) % rows_;
    }
}
void TileMap::draw(Canvas &canvas, const std::vector<uint32_t> &palette) const {
    auto color = [&](unsigned index) {
        return index && index <= palette.size() ? packOriginalColor(palette[index - 1])
                                                : uint16_t(0);
    };
    for (int y = 0; y < rows_; ++y)
        for (int x = 0; x < columns_; ++x) {
            const uint32_t cell = get((x + columnOffset_) % columns_, (y + rowOffset_) % rows_);
            const unsigned glyph = cell & 255;
            if (glyph == 32)
                continue;
            canvas.image(sheet_,
                         {int(glyph % 32) * tileWidth_, int(glyph / 32) * tileHeight_, tileWidth_,
                          tileHeight_},
                         float(x * tileWidth_ - scrollX_), float(y * tileHeight_ - scrollY_),
                         float(tileWidth_), float(tileHeight_),
                         {(cell & 0xff00) != 0, color((cell >> 16) & 255), color(cell >> 24)});
        }
}
} // namespace grpp
