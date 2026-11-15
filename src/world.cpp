#include "grpp/world.hpp"
#include <stdexcept>

namespace grpp {
namespace {
int mag(int x) { return x < 0 ? sub32(0, x) : x; }
} // namespace
void WorldSystem::anchor(Entity &e, const ChainFrame &f) {
    // Decisions belong to the completed 30ms source move. Native integration
    // exposes only its first fraction here; testing that fraction can repeatedly
    // reverse a cube that has not yet returned inside the depth bounds.
    const int endX = add32(e.x, e.vx), endY = add32(e.y, e.vy), endDepth = add32(e.depth, e.speed);
    pool_.move(e, e.vx, e.vy, e.speed);
    const int liveX = e.x, liveY = e.y, liveDepth = e.depth;
    e.x = endX;
    e.y = endY;
    e.depth = endDepth;
    const int minX = player_.arse ? 0 : f.minimumX, maxX = player_.arse ? 800 : f.maximumX;
    const int minY = player_.arse ? 0 : f.minimumY, maxY = player_.arse ? 800 : f.maximumY;
    if ((pixel(e.x) < minX && e.vx < 0) || (pixel(e.x) > maxX && e.vx > 0)) {
        e.vx = sub32(0, e.vx);
        state_.rippleTicks = 5;
        state_.rippleDepth = mul32(e.depth, 16);
    }
    if ((pixel(e.y) < minY && e.vy < 0) || (pixel(e.y) > maxY && e.vy > 0)) {
        e.vy = sub32(0, e.vy);
        state_.rippleTicks = 5;
        state_.rippleDepth = mul32(e.depth, 16);
    }
    if (player_.arse ? ((e.depth < 4096 && e.speed < 0) || (e.depth > 20480 && e.speed > 0))
                     : (e.depth < 4096 || e.depth > 20480))
        e.speed = sub32(0, e.speed);
    if (e.depth < 8192 && e.speed < 0) {
        e.colorCycle = 2;
        e.effectMode = 8;
    } else {
        e.colorCycle = 7;
        e.effectMode = 2;
    }
    const bool before = state_.forecast;
    state_.forecast = add32(e.depth, mul32(e.speed, 32)) < 4096;
    if (!before && state_.forecast)
        sounds_.push_back(25);
    if (e.depth < 4096 && player_.transitionTicks == 0) {
        state_.laserX = pixel(e.x);
        state_.laserY = pixel(e.y);
        state_.laserTicks = 10;
        sounds_.push_back(8);
    }
    auto &p = e.history[e.historyCursor++ & 63u];
    p.x = e.x;
    p.y = e.y;
    p.scale = e.depth;
    forecastAnchor_ = {e.x, e.y, e.depth, e.vx, e.vy, e.speed};
    forecastAnchorValid_ = true;
    e.x = liveX;
    e.y = liveY;
    e.depth = liveDepth;
}
void WorldSystem::respawn(Entity &e) {
    const auto &sine = headingSines();
    e.oscillatorX() = add32(e.oscillatorX(), e.oscillatorXStep());
    e.x = add32(e.targetX, mul32(sine[uint32_t(e.oscillatorX()) & 1023u], 64));
    e.oscillatorY() = add32(e.oscillatorY(), e.oscillatorYStep);
    e.y = add32(e.targetY, mul32(sine[uint32_t(e.oscillatorY()) & 1023u], 64));
    e.depth = add32(asr32(add32(sine[uint32_t(e.oscillatorX()) & 1023u],
                                sine[uint32_t(e.oscillatorY()) & 1023u]),
                          6),
                    4864);
    auto &p = e.history[e.historyCursor++ & 63u];
    p.x = e.x;
    p.y = e.y;
    p.scale = e.depth;
    p.tag = 1;
}
bool WorldSystem::playerNear(int x, int y, int radius, bool cross) const {
    const auto &p = pool_.at(EntityPool::Player);
    if (p.behavior == 14 || player_.transitionTicks != 0)
        return false;
    const int px =
        cross ? add32(pixel(p.x), pixel(player_.worldX)) : pixel(add32(p.x, player_.worldX));
    const int py =
        cross ? add32(pixel(p.y), pixel(player_.worldY)) : pixel(add32(p.y, player_.worldY));
    const int dx = mag(sub32(x, px)), dy = mag(sub32(y, py));
    return cross ? (dx < radius && dy < 8) || (dy < radius && dx < 8) : dx < radius && dy < radius;
}
uint32_t WorldSystem::fadeMask(int overrun, int backgroundMode) const {
    // 004184e2's negative indices alias live globals, not a fade table.
    // Radius advances by four, so precisely these eight words are reachable.
    switch (overrun) {
    case 4: {
        uint32_t bits;
        std::memcpy(&bits, &combatState_.shotColorPhase, 4);
        return bits;
    }
    case 8:
        return 50;
    case 12:
        return 5;
    case 16:
        return uint32_t(combatState_.killsUntilDrop);
    case 20:
    case 24:
        return 0xffffff00u;
    case 28:
        return 8192;
    case 32:
        return uint32_t(backgroundMode);
    default:
        throw std::runtime_error("Grid beam fade outside recovered source alignment");
    }
}
void WorldSystem::grid(SourceVisuals &v, const ChainFrame &f, int backgroundMode, bool advance) {
    for (int x = 0; x < player_.columns(); ++x)
        for (int y = 0; y < player_.rows(); ++y) {
            const auto index = size_t(x + y * player_.columns());
            auto &cell = grid_[index];
            uint32_t value = advance ? cell : renderedGrid_[index];
            if (advance)
                renderedGrid_[index] = value;
            if ((value & 255u) == 1) {
                if (advance && playerNear(x * 16, y * 16, 16))
                    combat_.hazardHit(6);
                uint32_t growth = (value >> 16) & 255u;
                v.sprite(int(growth) + 35, x * 16 - v.cameraX(), y * 16 - v.cameraY());
                if (advance && (f.frameNumber & 1)) {
                    uint32_t damage = value >> 24;
                    if (!(value & 256u)) {
                        if (growth + 1 == damage)
                            value |= 256u;
                        cell = damage << 24 | (growth + 1) << 16 | (value & 65535u);
                    } else {
                        growth -= 1;
                        if (signedBits(growth) < 1) {
                            ++damage;
                            if (damage < 8)
                                value &= 255u;
                            else {
                                value = 2;
                                growth = 0;
                                damage = 0;
                                sounds_.push_back(9);
                            }
                        }
                        cell = damage << 24 | growth << 16 | (value & 65535u);
                    }
                }
            } else if ((value & 255u) == 2) {
                int animation = f.frameNumber & 3;
                if (f.frameNumber & 4)
                    animation = 3 - animation;
                const auto color = packOriginalColor(v.color(5));
                const int sx = x * 16 - v.cameraX(), sy = y * 16 - v.cameraY();
                const int radius =
                    add32(asr32(signedBits(value), 16), advance ? (player_.arse ? 8 : 4) : 0);
                if (add32(tuning_.beamReach, 32) < radius) {
                    if (advance) {
                        cell = 0;
                        renderedGrid_[index] = 0;
                    }
                    continue;
                }
                if (advance) {
                    cell = (value & 65535u) | uint32_t(radius) * 65536u;
                    renderedGrid_[index] = cell;
                }
                const uint32_t mask = radius > tuning_.beamReach
                                          ? fadeMask(radius - tuning_.beamReach, backgroundMode)
                                          : ~0u;
                v.horizontal(sx - radius, sy, radius * 2, color, mask, radius > tuning_.beamReach);
                v.vertical(sx, sy - radius, radius * 2, color, mask, radius > tuning_.beamReach);
                v.sprite(animation + 72, sx, sy - radius);
                v.sprite(animation + 76, sx, sy + radius);
                v.sprite(animation + 80, sx + radius, sy);
                v.sprite(animation + 84, sx - radius, sy);
                if (advance && playerNear(x * 16, y * 16, radius, true)) {
                    combat_.hazardHit(12);
                    sounds_.push_back(12);
                }
            }
        }
}
void WorldSystem::lasers(SourceVisuals &v, const ChainFrame &f, bool advance) {
    const int ox = v.cameraX(), oy = v.cameraY(), animation = (f.frameNumber >> 1) & 3;
    if (advance)
        laserVisible_ = state_.laserTicks != 0;
    if (advance ? state_.laserTicks != 0 : laserVisible_) {
        if (advance)
            --state_.laserTicks;
        for (int x = 0; x < player_.worldWidth(); x += 16)
            v.sprite(animation + 48, x - ox, state_.laserY - oy);
        for (int y = 0; y < player_.worldHeight(); y += 16)
            v.sprite(animation + 52, state_.laserX - ox, y - oy);
        if (advance && playerNear(state_.laserX, state_.laserY, player_.height(), true)) {
            combat_.hazardHit(12);
            sounds_.push_back(12);
        }
    }
    if (advance && state_.rippleTicks)
        --state_.rippleTicks;
    const auto &liveAnchor = pool_.at(0);
    const auto pose = forecastAnchorValid_
                          ? forecastAnchor_
                          : std::array<int32_t, 6>{liveAnchor.x,  liveAnchor.y,  liveAnchor.depth,
                                                   liveAnchor.vx, liveAnchor.vy, liveAnchor.speed};
    Entity anchor;
    anchor.x = pose[0];
    anchor.y = pose[1];
    anchor.depth = pose[2];
    anchor.vx = pose[3];
    anchor.vy = pose[4];
    anchor.speed = pose[5];
    const int minX = player_.arse ? 0 : f.minimumX, maxX = player_.arse ? 800 : f.maximumX;
    const int minY = player_.arse ? 0 : f.minimumY, maxY = player_.arse ? 800 : f.maximumY;
    if (state_.forecast && anchor.speed < 1) {
        int ticks = 0, x = anchor.x, y = anchor.y, vx = anchor.vx, vy = anchor.vy;
        while (add32(anchor.depth, mul32(ticks, anchor.speed)) > 4096) {
            ++ticks;
            x = add32(x, vx);
            if (pixel(x) > maxX || pixel(x) < minX) {
                vx = sub32(0, vx);
                x = add32(x, vx);
            }
            y = add32(y, vy);
            if (pixel(y) > maxY || pixel(y) < minY) {
                vy = sub32(0, vy);
                y = add32(y, vy);
            }
        }
        const int px = pixel(x) - ox, py = pixel(y) - oy;
        // The caller supplies a fresh effect-stream mask for every line. This
        // stream is independent of gameplay rand(), even when all lines clip.
        for (int side : {-1, 1})
            v.vertical(px + side * ticks * 2, 0, v.viewHeight(), packOriginalColor(v.color(5)),
                       v.noiseMask(), true);
        for (int side : {-1, 1})
            v.horizontal(0, py + side * ticks * 2, v.viewWidth(), packOriginalColor(v.color(5)),
                         v.noiseMask(), true);
    }
}
void WorldSystem::nativeContacts() {
    for (int x = 0; x < player_.columns(); ++x)
        for (int y = 0; y < player_.rows(); ++y) {
            const auto value = grid_[size_t(x + y * player_.columns())];
            if ((value & 255u) == 1 && playerNear(x * 16, y * 16, 16))
                combat_.hazardHit(6);
            else if ((value & 255u) == 2 &&
                     playerNear(x * 16, y * 16, asr32(signedBits(value), 16), true)) {
                combat_.hazardHit(12);
                sounds_.push_back(12);
            }
        }
    if (laserVisible_ && playerNear(state_.laserX, state_.laserY, player_.height(), true)) {
        combat_.hazardHit(12);
        sounds_.push_back(12);
    }
}
} // namespace grpp
