#include "grpp/visuals.hpp"
#include "grpp/background.hpp"
#include "original_tables.hpp"
#include "original_visuals.hpp"
#include <algorithm>
#include <stdexcept>

namespace grpp {
void advanceTextPalette(std::vector<uint32_t> &palette, float phase) {
    if (palette.size() != 129)
        throw std::invalid_argument("Source text palette size");
    for (int i = 80; i >= 0; --i)
        palette[size_t(i + 1)] = palette[size_t(i)];
    palette[0] = cycleColor(original::cycles[7], phase);
    palette[64] = cycleColor(original::cycles[5], phase);
    palette[65] = cycleColor(original::cycles[0], phase);
    palette[80] = cycleColor(original::cycles[9], phase);
    palette[128] = cycleColor(original::cycles[8], phase);
}
uint16_t advanceFrameClear(int &flashTicks, uint32_t &clearColor) {
    if (flashTicks) {
        if (flashTicks < 0 || flashTicks > int(original::frameFlashColors.size()))
            throw std::out_of_range("Source flash countdown");
        clearColor = original::frameFlashColors[size_t(--flashTicks)];
    }
    // 00402dc1 passes RGB565 to memset, which repeats its low byte.
    const uint16_t byte = packOriginalColor(clearColor) & 255u;
    return uint16_t(byte | (byte << 8));
}
namespace {
int mag(int x) { return x < 0 ? sub32(0, x) : x; }
} // namespace
uint32_t SourceVisuals::color(int index, float offset) const {
    return cycleColor(original::cycles.at(size_t(index)), phase_ + offset);
}
void SourceVisuals::pixel(int x, int y, uint16_t c) {
    if (x >= 0 && x < canvas_.width() && y >= 0 && y < canvas_.height()) {
        const auto first = canvas_.draws().size();
        canvas_.rawFill({x, y, 1, 1}, c);
        canvas_.referenceOnlySince(first);
        canvas_.beam(x + .5f, y + .5f, x + .5f, y + .5f, c,
                     backgroundDrawing_ ? backgroundEnergy_ : vectorEnergy_, 1.f,
                     backgroundDrawing_);
    }
}
void SourceVisuals::cross(int x, int y, uint16_t c) {
    if (x > 0 && x < (canvas_.width() - 1) && y > 0 && y < (canvas_.height() - 2)) {
        auto first = canvas_.draws().size();
        if (frame_ & 1) {
            pixel(x, y - 1, c);
            pixel(x, y + 1, c);
        } else {
            pixel(x - 1, y, c);
            pixel(x + 1, y, c);
        }
        canvas_.referenceOnlySince(first);
        canvas_.beam(x + .5f, y + .5f, x + .5f, y + .5f, c, vectorEnergy_, 1.3f);
    }
}
void SourceVisuals::horizontal(int x, int y, int length, uint16_t c, uint32_t mask,
                               bool patterned) {
    const int begin = std::max(0, x), end = std::min((canvas_.width() - 1), add32(x, length));
    if (y < 0 || y > (canvas_.height() - 1) || begin > end)
        return;
    const auto first = canvas_.draws().size();
    if (patterned) {
        for (int i = 0; i < end - begin; ++i)
            if (mask & (1u << (unsigned(i) & 31u)))
                pixel(begin + i, y, c);
    } else if (y < (canvas_.height() - 1))
        canvas_.rawFill({begin, y, end - begin + 1, 1}, c);
    canvas_.referenceOnlySince(first);
    if (patterned) {
        for (int i = 0; i < end - begin;) {
            if (!(mask & (1u << (unsigned(i) & 31u)))) {
                ++i;
                continue;
            }
            int a = i++;
            while (i < end - begin && (mask & (1u << (unsigned(i) & 31u))))
                ++i;
            canvas_.beam(begin + a + .5f, y + .5f, begin + i - .5f, y + .5f, c,
                         backgroundDrawing_ ? backgroundEnergy_ : 1.f, i - a == 1 ? 1.f : .8f,
                         true);
        }
    } else if (y < (canvas_.height() - 1))
        canvas_.beam(float(begin), y + .5f, float(end + 1), y + .5f, c,
                     backgroundDrawing_ ? backgroundEnergy_ : 1.f);
}
void SourceVisuals::vertical(int x, int y, int length, uint16_t c, uint32_t mask, bool patterned) {
    if (length < 1)
        ++length;
    const int begin = std::max(0, y),
              end = std::min((canvas_.height() - 1), add32(y, add32(length, 1)));
    if (x < 0 || x > (canvas_.width() - 1) || begin >= end)
        return;
    const auto first = canvas_.draws().size();
    if (patterned) {
        for (int i = 0; i < end - begin; ++i)
            if (mask & (1u << (unsigned(i) & 31u)))
                pixel(x, begin + i, c);
    } else
        canvas_.rawFill({x, begin, 1, end - begin}, c);
    canvas_.referenceOnlySince(first);
    if (patterned) {
        for (int i = 0; i < end - begin;) {
            if (!(mask & (1u << (unsigned(i) & 31u)))) {
                ++i;
                continue;
            }
            int a = i++;
            while (i < end - begin && (mask & (1u << (unsigned(i) & 31u))))
                ++i;
            canvas_.beam(x + .5f, begin + a + .5f, x + .5f, begin + i - .5f, c,
                         backgroundDrawing_ ? backgroundEnergy_ : 1.f, i - a == 1 ? 1.f : .8f,
                         true);
        }
    } else
        canvas_.beam(x + .5f, float(begin), x + .5f, float(end), c,
                     backgroundDrawing_ ? backgroundEnergy_ : 1.f);
}
void SourceVisuals::sprite(int id, int x, int y, uint16_t foreground) {
    const auto &s = assets_.sprite(size_t(id));
    int left = x - (s.width >> 1) + offsetX_, top = y - (s.height >> 1) + offsetY_, sx = s.x,
        sy = s.y, w = s.width, h = s.height;
    if (left < 0) {
        sx -= left;
        w += left;
        left = 0;
    }
    if (top < 0) {
        sy -= top;
        h += top;
        top = 0;
    }
    w = std::min(w, (canvas_.width() - 1) - left);
    h = std::min(h, (canvas_.height() - 1) - top);
    if (w > 0 && h > 0) {
        canvas_.image(s.sheet, {sx, sy, w, h}, float(left), float(top), float(w), float(h),
                      {true, foreground, 0});
        canvas_.subpixelLast(spriteOffsetX_, spriteOffsetY_);
    }
}
void SourceVisuals::beamSprite(int id, int x, int y, uint16_t foreground) {
    const auto first = canvas_.draws().size();
    sprite(id, x, y, foreground);
    const auto last = canvas_.draws().size();
    canvas_.referenceOnlySince(first);
    for (size_t index = first; index < last; ++index) {
        const auto d = canvas_.draws()[index];
        const auto &image = assets_.page(size_t(d.sheet));
        for (int row = 0; row < d.source.height; ++row)
            for (int column = 0; column < d.source.width;) {
                const auto sample = image.at(d.source.x + column, d.source.y + row);
                if (!sample) {
                    ++column;
                    continue;
                }
                const uint16_t color = foreground ? foreground : sample;
                const int begin = column++;
                while (column < d.source.width) {
                    const auto next = image.at(d.source.x + column, d.source.y + row);
                    if (!next || (foreground ? foreground : next) != color)
                        break;
                    ++column;
                }
                const float py = d.y + row + .5f + d.subpixelY;
                canvas_.beam(d.x + begin + .5f + d.subpixelX, py, d.x + column - .5f + d.subpixelX,
                             py, color, .60f / .65f, .65f);
            }
    }
}
void SourceVisuals::lightning(int x1, int y1, int x2, int y2, int segments, uint16_t c) {
    const int dx = sub32(x2, x1), dy = sub32(y2, y1);
    int lastX = x1, lastY = y1;
    for (int segment = 0; segment < segments; ++segment) {
        x1 = add32(x1, dx / segments);
        y1 = add32(y1, dy / segments);
        int ax = lastX, ay = lastY, bx = add32(x1, fixed16(random_.below(16) - 8)),
            by = add32(y1, fixed16(random_.below(16) - 8));
        lastX = bx;
        lastY = by;
        if ((ax < 0 && bx < 0) || (ay < 0 && by < 0))
            continue;
        if (by < ay) {
            std::swap(ax, bx);
            std::swap(ay, by);
        }
        int count = asr32(std::max(mag(sub32(bx, ax)), mag(sub32(by, ay))), 16);
        if (!count)
            continue;
        const int stepX = sub32(bx, ax) / count, stepY = sub32(by, ay) / count;
        if (ay < 0) {
            if (stepY < 1)
                count = 0;
            else {
                int skip = 1 - ay / stepY;
                ax = add32(ax, mul32(stepX, skip));
                ay = add32(ay, mul32(stepY, skip));
                count -= skip;
            }
        }
        if (count > 0 && ax < 0) {
            if (stepX < 1)
                count = 0;
            else {
                int skip = 1 - ax / stepX;
                ax = add32(ax, mul32(stepX, skip));
                ay = add32(ay, mul32(stepY, skip));
                count -= skip;
            }
        }
        if (count > 0 && by > fixed16((canvas_.height() - 1))) {
            if (stepY < 1)
                count = 0;
            else {
                int skip = (by - fixed16((canvas_.height() - 1))) / stepY + 1;
                bx = sub32(bx, mul32(stepX, skip));
                count -= skip;
            }
        }
        if (count > 0 && bx > fixed16((canvas_.width() - 1))) {
            if (stepX < 1)
                count = 0;
            else
                count -= (bx - fixed16((canvas_.width() - 1))) / stepX + 1;
        }
        if (count > 0 && pixel(ay) > 0 && pixel(ay) < canvas_.height()) {
            const int startX = ax, startY = ay;
            const auto first = canvas_.draws().size();
            for (int i = 0; i <= count; ++i) {
                pixel(pixel(ax), pixel(ay), c);
                ax = add32(ax, stepX);
                ay = add32(ay, stepY);
            }
            canvas_.referenceOnlySince(first);
            canvas_.beam(startX / 65536.f, startY / 65536.f, sub32(ax, stepX) / 65536.f,
                         sub32(ay, stepY) / 65536.f, c, 1, 1.2f);
        }
    }
}
void SourceVisuals::transformed(Entity &e, bool world, bool fixed) {
    const auto &s = assets_.sprite(size_t(e.sprite));
    auto &t = e.transform;
    t.sheet = s.sheet;
    t.x = s.x;
    t.y = s.y;
    t.width = s.width;
    t.height = s.height;
    if (t.tileWidth < 1 || t.tileHeight < 1)
        throw std::runtime_error("Transformed sprite lacks source tile dimensions");
    EffectRandom noise(t.noiseSeed);
    const int baseX =
        (fixed ? e.x : pixel(world ? sub32(e.x, player_.worldX) : e.x)) - (s.width >> 1);
    const int baseY =
        (fixed ? e.y : pixel(world ? sub32(e.y, player_.worldY) : e.y)) - (s.height >> 1);
    const int pivotX = sub32(t.pivotX, 32768), pivotY = sub32(t.pivotY, 32768);
    int rowX =
        add32(mul32(pivotX, asr32(mul32(mul32(t.axisXX, e.scale), s.width) / t.tileWidth, 16)),
              mul32(pivotX, asr32(mul32(mul32(t.axisYX, e.scale), s.height) / t.tileHeight, 16)));
    int rowY =
        add32(mul32(pivotY, asr32(mul32(mul32(t.axisXY, e.scale), s.width) / t.tileWidth, 16)),
              mul32(pivotY, asr32(mul32(mul32(t.axisYY, e.scale), s.height) / t.tileHeight, 16)));
    int jitterX = 0, jitterY = 0;
    for (int x = 0; x < s.width; x += t.tileWidth) {
        int colX = rowX, colY = rowY;
        for (int y = 0; y < s.height; y += t.tileHeight) {
            if (t.flags & 2) {
                jitterX = mul32(asr32(mul32(asr32(signedBits(noise.next()), 16), t.jitterX), 16),
                                e.scale);
                jitterY = mul32(asr32(mul32(asr32(signedBits(noise.next()), 16), t.jitterY), 16),
                                e.scale);
            }
            if (t.flags & 4)
                (void)noise.next();
            colX = add32(colX, mul32(t.axisYX, e.scale));
            colY = add32(colY, mul32(t.axisYY, e.scale));
            int dx = baseX + x + asr32(add32(colX, jitterX), 16) + offsetX_,
                dy = baseY + y + asr32(add32(colY, jitterY), 16) + offsetY_;
            int sx = s.x + x, sy = s.y + y, w = std::min(t.tileWidth, s.width - x),
                h = std::min(t.tileHeight, s.height - y);
            if (dx < 0) {
                sx -= dx;
                w += dx;
                dx = 0;
            }
            if (dy < 0) {
                sy -= dy;
                h += dy;
                dy = 0;
            }
            w = std::min(w, (canvas_.width() - 1) - dx);
            h = std::min(h, (canvas_.height() - 1) - dy);
            if (w > 0 && h > 0) {
                canvas_.image(s.sheet, {sx, sy, w, h}, float(dx), float(dy), float(w), float(h));
                canvas_.markFragment();
                const int px = world ? sub32(e.x, player_.worldX) : e.x,
                          py = world ? sub32(e.y, player_.worldY) : e.y;
                canvas_.subpixelLast((fixed ? 0.f : float(uint32_t(px) & 65535u) / 65536.f) +
                                         float(uint32_t(add32(colX, jitterX)) & 65535u) / 65536.f,
                                     (fixed ? 0.f : float(uint32_t(py) & 65535u) / 65536.f) +
                                         float(uint32_t(add32(colY, jitterY)) & 65535u) / 65536.f);
            }
        }
        rowX = add32(rowX, mul32(t.axisXX, e.scale));
        rowY = add32(rowY, mul32(t.axisXY, e.scale));
    }
}
void SourceVisuals::glyphTrail(Entity &e, int mode) {
    const float priorEnergy = vectorEnergy_;
    const auto &g = original::glyphEffects.at(size_t(e.effectMode));
    const auto &sine = headingSines();
    e.colorPhase += .1f;
    float phase = e.colorPhase;
    uint32_t cursor = mode == 11 ? e.historyCursor : e.historyCursor - uint32_t(g.trails) - 1u;
    int turn = add32(asr32(mul32(e.history[cursor & 63u].scale, g.depthTurn), 16), 768);
    int width = fixed16(e.phase), height = width;
    for (int trail = 0; trail < g.trails; ++trail) {
        const auto &p = e.history[cursor & 63u];
        int a, b, px, py, w, h;
        if (mode == 11) {
            int angle = asr32(mul32(p.scale, g.depthTurn), 16);
            a = sine[(uint32_t(angle) + 768) & 1023u];
            b = sine[uint32_t(angle) & 1023u];
            px = sub32(p.x, player_.worldX);
            py = sub32(p.y, player_.worldY);
            w = width;
            h = height;
            --cursor;
        } else if (mode == 14) {
            int angle = asr32(mul32(p.scale, g.depthTurn), 16);
            a = sine[(uint32_t(angle) + 256) & 1023u];
            b = sine[(uint32_t(angle) + 512) & 1023u];
            px = p.x;
            py = p.y;
            w = e.sprite;
            h = e.extent;
            ++cursor;
        } else {
            a = sine[uint32_t(turn) & 1023u];
            b = sine[(uint32_t(turn) + 256) & 1023u];
            turn = add32(turn, g.turnStep);
            px = sub32(p.x, player_.worldX);
            py = sub32(p.y, player_.worldY);
            w = e.sprite;
            h = e.extent;
            ++cursor;
        }
        const int depth = mul32(p.scale, 16);
        phase += .1f;
        const uint16_t c =
            packOriginalColor(cycleColor(original::cycles.at(size_t(e.colorCycle)), phase));
        vectorEnergy_ = priorEnergy * beamDepthEnergy(p.scale);
        if (depth > 0) {
            int yy = asr32(mul32(g.height, h), 1);
            for (int row = 0; row < g.height; ++row) {
                int xx = asr32(sub32(0, mul32(g.height, w)), 1);
                const auto &text = g.rows.at(size_t(row + trail * g.stride));
                for (int column = 0; column < g.width; ++column) {
                    if (text[size_t(column)] == '#') {
                        const unsigned shift = mode == 11 ? 11u : 8u;
                        const int x = add32(sub32(px, fixed16((canvas_.width() / 2))),
                                            add32(asr32(mul32(sub32(0, xx), asr32(a, shift)), 8),
                                                  asr32(mul32(yy, asr32(b, shift)), 8)));
                        const int y = add32(sub32(py, fixed16((canvas_.height() / 2))),
                                            add32(asr32(mul32(xx, asr32(b, shift)), 8),
                                                  asr32(mul32(yy, asr32(a, shift)), 8)));
                        cross(x / depth + (canvas_.width() / 2), y / depth + (canvas_.height() / 2),
                              c);
                    }
                    xx = add32(xx, w);
                }
                yy = sub32(yy, h);
            }
        }
        width = add32(width, 0x20000);
        height = add32(height, 0x20000);
    }
    vectorEnergy_ = priorEnergy;
}
void SourceVisuals::entity(Entity &e) {
    spriteOffsetX_ = spriteOffsetY_ = 0;
    if (e.drawing == 3 || e.drawing == 6 || e.drawing == 10 || e.drawing == 15) {
        spriteOffsetX_ = float(uint32_t(sub32(e.x, player_.worldX)) & 65535u) / 65536.f;
        spriteOffsetY_ = float(uint32_t(sub32(e.y, player_.worldY)) & 65535u) / 65536.f;
    }
    const auto &sine = headingSines();
    switch (e.drawing) {
    case 0: {
        uint32_t cursor = e.historyCursor - uint32_t(player_.sheepPower * 4);
        float phase = phase_;
        for (int i = 0; i < player_.sheepPower; ++i) {
            if (i == player_.sheepPower - 1) {
                spriteOffsetX_ = float(uint32_t(e.x) & 65535u) / 65536.f;
                spriteOffsetY_ = float(uint32_t(e.y) & 65535u) / 65536.f;
                sprite(e.sprite, pixel(e.x), pixel(e.y));
            } else {
                phase += .2f;
                const auto &p = e.history[cursor & 63u];
                spriteOffsetX_ = float(uint32_t(p.x) & 65535u) / 65536.f;
                spriteOffsetY_ = float(uint32_t(p.y) & 65535u) / 65536.f;
                const auto first = canvas_.draws().size();
                sprite(e.sprite, pixel(p.x), pixel(p.y),
                       packOriginalColor(cycleColor(original::cycles[7], phase)));
                if (canvas_.draws().size() > first)
                    canvas_.markSilhouette();
                cursor += 4;
            }
        }
        break;
    }
    case 1:
        transformed(e, true, false);
        break;
    case 2:
        sprite(e.sprite, pixel(sub32(e.x, player_.worldX)), pixel(sub32(e.y, player_.worldY)));
        for (const auto &p : e.history)
            if (p.tag > 0) {
                int x = pixel(sub32(p.x, player_.worldX)), y = pixel(sub32(p.y, player_.worldY));
                if (x > 0 && x < canvas_.width() && y > 0 && y < canvas_.height())
                    pixel(x, y, uint16_t(p.color));
            }
        break;
    case 3: {
        if (e.flags & 4)
            horizontal(0, pixel(sub32(e.y, player_.worldY)), canvas_.width(),
                       packOriginalColor(color(5)), effects_.next(), true);
        uint16_t foreground = 0;
        if (e.hitFlash) {
            --e.hitFlash;
            foreground = packOriginalColor(color(5));
        }
        sprite(e.sprite, pixel(sub32(e.x, player_.worldX)), pixel(sub32(e.y, player_.worldY)),
               foreground);
        break;
    }
    case 4: {
        e.colorPhase += .1f;
        float phase = e.colorPhase;
        const int px = pixel(sub32(e.x, player_.worldX)), py = pixel(sub32(e.y, player_.worldY));
        sprite((frame_ >> 2) % 10 + 7, px, py);
        for (int ring = 1; ring < 8; ring += 2) {
            int angle =
                ring * ((uint32_t(e.heading) & 0x3fff) < 512 ? -1 : 1) * 16 - asr32(e.x, 13);
            phase += .9f;
            const auto c =
                packOriginalColor(cycleColor(original::cycles.at(size_t(e.colorCycle)), phase));
            for (int i = 0; i < 4; ++i) {
                int a = sine[uint32_t(angle) & 1023u], b = sine[(uint32_t(angle) + 256) & 1023u];
                angle += 256;
                cross(px + asr32(mul32(asr32(mul32(ring, e.vx), 10), a), 16),
                      py + asr32(mul32(asr32(mul32(ring, e.vy), 10), b), 16), c);
            }
        }
        break;
    }
    case 5: {
        if (e.trailProfile != 0)
            throw std::runtime_error("Unknown native polar trail profile");
        const auto &profile = original::rewardTrailProfile;
        e.colorPhase += .1f;
        float phase = e.colorPhase;
        uint32_t cursor = e.historyCursor - profile[17];
        int scale = 256, spin = 0;
        for (int i = int(profile[16]); i < int(profile[17]); ++i) {
            const auto &p = e.history[cursor++ & 63u];
            const int px = pixel(sub32(p.x, player_.worldX)),
                      py = pixel(sub32(p.y, player_.worldY));
            phase += .4f;
            const auto c =
                packOriginalColor(cycleColor(original::cycles.at(size_t(e.colorCycle)), phase));
            spin += int(profile[19]);
            for (int j = 0; j < 16; ++j) {
                int angle = int(profile[size_t(j)] >> 16) + spin;
                int radius = asr32(
                    mul32(asr32(mul32(int(profile[size_t(j)] & 65535u), scale), 8), e.scale), 16);
                int x = asr32(mul32(radius, sine[uint32_t(angle) & 1023u]), 16),
                    y = asr32(mul32(radius, sine[(uint32_t(angle) + 256) & 1023u]), 16);
                for (int side = 0; side < 8; ++side)
                    if (profile[20] & (1u << side)) {
                        int a = side < 4 ? x : y, b = side < 4 ? y : x;
                        cross(px + ((side & 1) ? -a : a), py + ((side & 2) ? -b : b), c);
                    }
            }
            scale = asr32(mul32(scale, int(profile[18])), 8);
        }
        break;
    }
    case 6: {
        uint16_t foreground = 0;
        if (e.hitFlash) {
            --e.hitFlash;
            foreground = packOriginalColor(color(5));
        }
        sprite(e.animationBase + int((uint32_t(e.heading) >> 7) & 7u),
               pixel(sub32(e.x, player_.worldX)), pixel(sub32(e.y, player_.worldY)), foreground);
        break;
    }
    case 7:
        throw std::runtime_error("Sphere-trail menu family has no stage constructor");
    case 8:
        glyphTrail(e, 8);
        break;
    case 9:
        if (player_.transitionTicks < 0) {
            sprite(111, pixel(e.x), pixel(e.y), packOriginalColor(color(10)));
            const int x = e.x, y = e.y;
            for (int i = 1; i < 8; ++i) {
                e.scale = mul32(mul32(player_.transitionTicks, i), -1024);
                const auto &p = e.history[(e.historyCursor - uint32_t(i * 4)) & 63u];
                e.x = p.x;
                e.y = p.y;
                auto &t = e.transform;
                t.axisXX = 32;
                t.axisXY = t.axisYX = 0;
                t.tileWidth = t.tileHeight = 2;
                t.jitterY = 64;
                t.jitterX = canvas_.width();
                t.axisYY = 32;
                t.pivotX = t.pivotY = 0;
                transformed(e, false, false);
            }
            e.x = x;
            e.y = y;
        } else {
            e.scale = mul32(player_.transitionTicks, 256);
            e.history[e.historyCursor & 63u].y = e.y;
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 1; i < 8; ++i) {
                    e.y = e.history[(e.historyCursor - uint32_t(i)) & 63u].y;
                    e.transform.axisXX = pass ? i * -32 : i * 16;
                    e.transform.axisYY = pass ? i * -16 : i * 8;
                    e.transform.axisYX = pass ? -(i * 8 - 24) : (i - 3) * 32;
                    transformed(e, false, false);
                }
            ++e.historyCursor;
            e.y = add32(e.y, e.vy);
            e.vy = add32(e.vy, 32768);
        }
        break;
    case 10: {
        int animation = frame_ & 7;
        if (frame_ & 8)
            animation = 7 - animation;
        sprite(e.animationBase + animation, pixel(sub32(e.x, player_.worldX)),
               pixel(sub32(e.y, player_.worldY)));
        break;
    }
    case 11:
        if (e.effectMode == 0) {
            const float prior = vectorEnergy_;
            vectorEnergy_ *= .60f;
            const auto c = packOriginalColor(color(e.colorCycle));
            for (const auto &p : e.history) {
                int x = sub32(sub32(p.x, player_.worldX), fixed16((canvas_.width() / 2))) /
                            0x30000 +
                        (canvas_.width() / 2),
                    y = sub32(sub32(p.y, player_.worldY), fixed16((canvas_.height() / 2))) /
                            0x30000 +
                        (canvas_.height() / 2);
                if (x > 0 && x < canvas_.width() && y > 0 && y < canvas_.height())
                    pixel(x, y, c);
            }
            vectorEnergy_ = prior;
        } else
            glyphTrail(e, 11);
        break;
    case 12: {
        e.colorPhase += .1f;
        const auto c =
            packOriginalColor(cycleColor(original::cycles.at(size_t(e.colorCycle)), e.colorPhase));
        int x = pixel(sub32(e.x, player_.worldX)), y = pixel(sub32(e.y, player_.worldY));
        for (int i = 0; i < 1024; i += 16)
            cross(x + asr32(mul32(asr32(e.scale, 8), sine[size_t(i)]), 16),
                  y + asr32(mul32(asr32(e.scale, 8), sine[size_t((i + 256) & 1023)]), 16), c);
        break;
    }
    case 13:
        if (e.scale < 1) {
            if (e.life > 0) {
                auto c = packOriginalColor(color(7));
                uint32_t mask = 0xff00ff00u >> (uint32_t(frame_) & 7u);
                horizontal(0, pixel(sub32(e.y, player_.worldY)), canvas_.width(), c, mask, true);
                vertical(pixel(sub32(e.x, player_.worldX)), 0, canvas_.height(), c, mask, true);
            }
            sprite((e.sprite > 0) + 92, pixel(sub32(e.x, player_.worldX)),
                   pixel(sub32(e.y, player_.worldY)));
            if (e.sprite)
                --e.sprite;
        } else {
            e.colorPhase += .1f;
            const auto c = packOriginalColor(cycleColor(original::cycles[7], e.colorPhase));
            int radius = asr32(e.scale, 8), angle = 0;
            for (int ring = 0; ring < 4; ++ring) {
                for (int i = 0; i < 64; ++i) {
                    cross(pixel(sub32(e.x, player_.worldX)) +
                              asr32(mul32(radius, sine[uint32_t(angle) & 1023u]), 16),
                          pixel(sub32(e.y, player_.worldY)) +
                              asr32(mul32(radius, sine[(uint32_t(angle) + 256) & 1023u]), 16),
                          c);
                    angle += 16;
                }
                radius += 8;
            }
        }
        break;
    case 14:
        glyphTrail(e, 14);
        break;
    case 15:
        beamSprite(e.sprite, pixel(sub32(e.x, player_.worldX)), pixel(sub32(e.y, player_.worldY)),
                   packOriginalColor(color(7)));
        break;
    case 16:
        e.x = (canvas_.width() / 2);
        e.y = (canvas_.height() + 20) - e.heading * 4;
        if (e.y < (canvas_.height() / 2)) {
            int distance = (canvas_.height() / 2) - e.y;
            e.y = (canvas_.height() / 2);
            for (int i = 1; i < std::min(distance, 8); ++i) {
                e.scale = distance * i * -512;
                transformed(e, false, true);
            }
        } else
            sprite(e.sprite, e.x, e.y);
        break;
    default:
        throw std::runtime_error("Unrecovered source drawing mode");
    }
    spriteOffsetX_ = spriteOffsetY_ = 0;
}
void SourceVisuals::pattern(uint32_t c, int ox, int oy, int width, int height, int rx, int ry,
                            int shades, int mode) {
    const bool prior = backgroundDrawing_;
    backgroundDrawing_ = true;
    patternReference(c, ox, oy, width, height, rx, ry, shades, mode);
    // background() marks this entire range reference-only. Deduplicating its
    // beam metadata here changes no final command and used thousands of hash
    // allocations per frame even in Original graphics.
    backgroundDrawing_ = prior;
}
void SourceVisuals::patternReference(uint32_t c, int ox, int oy, int width, int height, int rx,
                                     int ry, int shades, int mode) {
    std::array<uint16_t, 33> colors{};
    uint32_t r = 0, g = 0, b = 0;
    const uint32_t dr = ((c >> 8) & 0xff0000u) / uint32_t(shades),
                   dg = (c & 0xff0000u) / uint32_t(shades),
                   db = ((c & 0xff00u) << 8) / uint32_t(shades);
    for (int i = 0; i < shades; ++i) {
        r += dr;
        g += dg;
        b += db;
        colors[size_t(i)] =
            packOriginalColor(((r & 0xff0000u) << 8) | (g & 0xff0000u) | ((b & 0xff0000u) >> 8));
    }
    if (mode == 0) {
        for (int x = ox; x < (canvas_.width() - 1); x += width)
            for (int y = oy; y < (canvas_.height() - 1); y += height) {
                vertical(x, y - ry, ry * 2, packOriginalColor(c));
                horizontal(x - rx, y, rx * 2, packOriginalColor(c));
            }
        return;
    }
    const int stepX = rx / shades, stepY = ry / shades;
    for (int x = ox; x < fixed16((canvas_.width() - 1)); x += width)
        for (int y = oy; y < fixed16((canvas_.height() - 1)); y += height) {
            int left = x - rx, right = x + rx - (mode == 1 ? 0 : stepX);
            for (int i = 0; i < shades; ++i) {
                const auto color = colors[size_t(i)];
                if (mode == 1) {
                    for (int p : {pixel(left), pixel(right)})
                        if (p > 0 && p < canvas_.width() && pixel(y) > 0 &&
                            pixel(y) < canvas_.height())
                            pixel(p, pixel(y), color);
                } else {
                    horizontal(pixel(left), pixel(mode == 3 ? y - ry : y), pixel(stepX), color);
                    horizontal(pixel(right), pixel(mode == 3 ? y - ry : y), pixel(stepX), color);
                    if (mode == 3) {
                        horizontal(pixel(left), pixel(y + ry), pixel(stepX), color);
                        horizontal(pixel(right), pixel(y + ry), pixel(stepX), color);
                    }
                }
                left += stepX;
                right -= stepX;
            }
            int top = y - ry, bottom = y + ry - (mode == 1 ? 0 : stepY);
            for (int i = 0; i < shades; ++i) {
                const auto color = colors[size_t(i)];
                if (mode == 1) {
                    for (int p : {pixel(top), pixel(bottom)})
                        if (pixel(x) > 0 && pixel(x) < canvas_.width() && p > 0 &&
                            p < canvas_.height())
                            pixel(pixel(x), p, color);
                } else {
                    vertical(pixel(mode == 3 ? x - rx : x), pixel(top), pixel(stepY), color);
                    vertical(pixel(mode == 3 ? x - rx : x), pixel(bottom), pixel(stepY), color);
                    if (mode == 3) {
                        vertical(pixel(x + rx), pixel(top), pixel(stepY), color);
                        vertical(pixel(x + rx), pixel(bottom), pixel(stepY), color);
                    }
                }
                top += mode == 1 ? stepY : stepX;
                bottom -= mode == 1 ? stepY : stepX;
            }
        }
}
void SourceVisuals::background(int advance, const std::array<int, 6> &p, double subframe,
                               double advancePerFrame, bool referenceGeometry) {
    const auto first = canvas_.draws().size();
    if (referenceGeometry) {
        const int dx = add32(asr32(sub32(fixed16((canvas_.width() / 2)), player_.worldX), 5),
                             0x20000),
                  dy = add32(asr32(sub32(fixed16(canvas_.width()), player_.worldY), 5), 0x20000);
        auto layer = [&](uint32_t color, int time, int count, uint32_t flags) {
            uint32_t r = 0, g = 0, b = 0;
            for (int depth = 4096 - time % 512 + count * 512; depth > 512; depth -= 512) {
                r = std::min(r + ((color >> 8) & 0xff0000u) / uint32_t(count), 0xff0000u);
                g = std::min(g + (color & 0xff0000u) / uint32_t(count), 0xff0000u);
                b = std::min(b + ((color & 0xff00u) << 8) / uint32_t(count), 0xff0000u);
                uint32_t packed = (r & 0xff0000u) << 8 | (g & 0xff0000u) | (b & 0xff0000u) >> 8;
                int w = (canvas_.width() == 400 ? 0x80000 : 0x40000) / depth, h = w,
                    x = (dx / depth + (canvas_.width() / 2)) % w,
                    y = (dy / depth + (canvas_.height() / 2)) % h;
                backgroundEnergy_ = (.25f + .75f * std::clamp(512.f / float(depth), 0.f, 1.f)) *
                                    beamDepthEnergy(depth, 2048);
                int mode = int(flags & 255u);
                if (mode == 0)
                    pattern(packed, x, y, w, h, w >> 3, h >> 3, 1, 0);
                else
                    pattern(packed, fixed16(x), fixed16(y), fixed16(w), fixed16(h),
                            asr32(fixed16(w), (flags >> 16) & 31u),
                            asr32(fixed16(h), (flags >> 8) & 31u), int(flags >> 24), mode);
            }
        };
        layer(color(11), add32(frame_ * 64, advance), 4,
              uint32_t(p[0] << 16) | 0x0f000000u | uint32_t(p[1] << 8) | uint32_t(p[4]));
        layer(color(11), add32(frame_ * 32, advance), 2,
              uint32_t(p[2] << 16) | 0x04000000u | uint32_t(p[3] << 8) | uint32_t(p[5]));
        layer(color(12), advance, 4, 0x04040402u);
        canvas_.markReferenceGeometrySince(first);
        canvas_.referenceOnlySince(first);
    }
    DecorativeGrid grid;
    grid.valid = true;
    grid.frame = frame_ + subframe;
    grid.advance = advance + subframe * advancePerFrame;
    grid.advancePerFrame = advancePerFrame;
    grid.width = canvas_.width();
    grid.height = canvas_.height();
    grid.cameraY = canvas_.width();
    grid.projection = canvas_.width() == 400 ? 524288 : 262144;
    grid.worldX = player_.worldX;
    grid.worldY = player_.worldY;
    grid.pattern = p;
    grid.colors = {color(11), color(11), color(12)};
    // Geometry is evaluated at presentation time, leaving simulation-owned
    // commands and the original reference raster completely untouched.
    grid.first = grid.last = canvas_.draws().size();
    canvas_.decorativeGrid(grid);
    canvas_.playfield({true, player_.worldWidth(), player_.worldHeight(), player_.worldX / 65536.,
                       player_.worldY / 65536.});
}
} // namespace grpp
