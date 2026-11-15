#include "grpp/chains.hpp"
#include "grpp/native_motion.hpp"
#include "original_tables.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grpp {
namespace {
int32_t magnitude(int32_t value) { return value < 0 ? sub32(0, value) : value; }
} // namespace
const std::array<int32_t, 1024> &headingSines() {
    static const auto table = [] {
        std::array<int32_t, 1024> values{};
        float angle = 0;
        // 0041a986..0041a9db: increment BEFORE the first sample. FST stores
        // the float accumulator but leaves the unrounded sum for sin(double).
        constexpr float step = 0x1.921fb6p-8f; // source bits 3bc90fdb
        for (auto &value : values) {
            const double argument = double(angle) + double(step);
            angle = float(argument);
            value = int32_t(std::sin(argument) * 65536.0);
        }
        return values;
    }();
    return table;
}
void ChainMotion::writeTrail(Entity &e, float phase, bool head) {
    auto &p = e.history[e.historyCursor++ & 63u];
    p.x = e.x;
    p.y = e.y;
    if (head)
        p.scale = e.depth;
    else
        p.tag = 1; // Body does NOT copy depth into its output history.
    p.heading = e.heading;
    p.vx = e.vx;
    p.vy = e.vy;
    // Original writes a WORD, retaining the previous high half of this word.
    p.color = (p.color & 0xffff0000u) | packOriginalColor(cycleColor(original::cycles[0], phase));
}
void ChainMotion::createAttack(const Entity &head) {
    // The chance draw occurs even with a full projectile pool or type zero.
    if (random_.below(256) > asr32(head.attackParameters, 16))
        return;
    const int slot = pool_.firstFree(EntityPool::EnemyShotFirst, EntityPool::EnemyShotCount);
    if (slot < 0)
        return;
    auto &shot = pool_.at(size_t(slot));
    const bool reverse = (head.flags & 2) != 0;
    shot.x = head.x;
    shot.y = head.y;
    shot.vx = 0;
    shot.vy = reverse ? -0x60000 : 0x60000;
    shot.sprite = shot.animationBase = reverse ? 172 : 64;
    shot.hitFlash = 0;
    shot.animation = -1;
    shot.drawing = 10;
    shot.flags = 1;
    shot.commandOrHealth = 0xffffffffu;
    shot.behavior = 7;
    const auto &p = pool_.at(EntityPool::Player);
    const int32_t dx = sub32(add32(p.x, player_.worldX), head.x);
    const int32_t dy = sub32(add32(p.y, player_.worldY), head.y);
    const unsigned type = uint32_t(head.attackParameters) & 255u;
    if (type == 2 || type == 3) {
        shot.vy = dy > 0 ? 0x60000 : -0x60000;
        shot.sprite = shot.animationBase = dy > 0 ? 64 : 172;
        if (type == 3 && magnitude(dx) >= magnitude(dy)) {
            shot.sprite = shot.animationBase = dx > 0 ? 190 : 182;
            shot.vx = dx > 0 ? 0x90000 : -0x90000;
            shot.vy = 0;
        }
    }
}
void ChainMotion::advanceHead(size_t slot, const ChainFrame &frame, EntityCensus &census) {
    auto &e = pool_.at(slot);
    ++census.activeEnemies;
    createAttack(e);
    const auto &sprite = assets_.sprite(e.sprite);
    const int x = pixel(e.x), y = pixel(e.y);
    const bool visibleX = x < frame.minimumX ? magnitude(sub32(x, frame.minimumX)) <= sprite.width
                                             : sub32(x, frame.maximumX) <= sprite.width;
    const bool visibleY = y < frame.minimumY ? magnitude(sub32(y, frame.minimumY)) <= sprite.height
                                             : sub32(y, frame.maximumY) <= sprite.height;
    e.outsideTicks = visibleX && visibleY ? 0 : add32(e.outsideTicks, 1);
    if (e.outsideTicks > 250) {
        int next = int(slot);
        // Valid source chains terminate at -1. Reject corrupt links instead of
        // reading outside the native arena; never silently truncate a chain.
        size_t visited = 0;
        while (next >= 0) {
            if (++visited > EntityPool::Capacity)
                throw std::runtime_error("Cyclic source chain during retirement");
            auto &part = pool_.at(size_t(next));
            part.behavior = 0;
            next = part.next;
        }
    }
    const auto &sine = headingSines();
    pool_.move(e, asr32(mul32(sine[uint32_t(e.heading) & 1023u], e.vx), 8),
               asr32(mul32(sine[(uint32_t(e.heading) + 256u) & 1023u], e.vy), 8));
    const auto &companion = pool_.at(EntityPool::Companion);
    const int dx = magnitude(sub32(e.x, companion.x)), dy = magnitude(sub32(e.y, companion.y));
    if (fixed16(frame.minimumX) < e.x && e.x < fixed16(frame.maximumX) &&
        fixed16(frame.minimumY) < e.y && e.y < fixed16(frame.maximumY) && dx < census.nearestX &&
        dy < census.nearestY) {
        census.nearestX = dx;
        census.nearestY = dy;
        census.nearestHead = int(slot);
    }
    // Retirement above does not suppress this final movement/history write.
    writeTrail(e, frame.trailColorPhase, true);
}
void ChainMotion::followBody(Entity &body, float phase) {
    // Case 4 continues after scatter conversion has cleared previous to -1.
    // In the original layout that read aliases the old platform font map:
    // predecessor cursor is untouched BSS zero, and all reachable spacings
    // (2,3,4,8) select unmapped high character entries initialized to glyph 44
    // by 0040392d..00403995. Preserve this observable defect explicitly; do
    // not emulate an address space or silently omit the final follow step.
    if (body.previous == -1) {
        if (body.historySpacing != 2 && body.historySpacing != 3 && body.historySpacing != 4 &&
            body.historySpacing != 8)
            throw std::runtime_error("Detached-body font alias outside recovered source spacings");
        body.x = body.y = body.depth = body.vx = body.vy = body.heading = 44;
        writeTrail(body, phase, false);
        return;
    }
    const auto &previous = pool_.at(size_t(body.previous));
    const auto &point =
        previous.history[(previous.historyCursor - uint32_t(body.historySpacing)) & 63u];
    body.x = point.x;
    body.y = point.y;
    body.depth = point.scale;
    body.vx = point.vx;
    body.vy = point.vy;
    body.heading = point.heading;
    if (pool_.nativeMotion)
        pool_.nativeMotion->follow(pool_, body);
    writeTrail(body, phase, false);
}
int ChainMotion::retargetTurn(const Entity &head) const {
    const auto &player = pool_.at(EntityPool::Player);
    const int dx = sub32(add32(player.x, player_.worldX), head.x);
    const int dy = sub32(add32(player.y, player_.worldY), head.y);
    int quadrant = dx < 0 ? (dy < 0 ? 5 : 7) : (dy < 0 ? 3 : 1);
    const int difference = quadrant - int((uint32_t(head.heading) & 1023u) >> 7);
    int turn = tuning_.retargetTurnStep;
    if (magnitude(difference) > 3)
        turn = sub32(0, turn);
    if (difference < 1)
        turn = sub32(0, turn);
    return turn;
}
void ChainMotion::randomTurn(Entity &head) {
    if (random_.below(512) >= tuning_.specialTurnChance)
        return;
    head.turnRate = random_.below(8) < 4 ? -64 : 64;
    head.phase = head.heading;
    head.heading = add32(head.heading, head.turnRate);
    head.mode = signedBits(uint32_t(head.mode) << 16 | 1u);
}
void ChainMotion::steerHead(Entity &e, const ChainFrame &f, const WorldGrid &grid) {
    e.heading = int(uint32_t(e.heading) & 1023u);
    const int gx = std::clamp(asr32(e.x, 20), 0, player_.columns() - 1),
              gy = std::clamp(asr32(e.y, 20), 0, player_.rows() - 1);
    const bool pod = (grid[size_t(gx + gy * player_.columns())] & 255u) == 1;
    const bool reverse = (e.flags & 2) != 0;
    const auto &player = pool_.at(EntityPool::Player);
    const int px = add32(player.x, player_.worldX), py = add32(player.y, player_.worldY);
    switch (uint32_t(e.mode) & 255u) {
    case 0:
        if (pod && (e.heading == 768 || e.heading == 256)) {
            e.mode = 1;
            e.phase = e.heading == 768 ? 256 : 768;
            e.turnRate = e.heading == 768 ? 64 : -64;
            if (reverse)
                e.turnRate = sub32(0, e.turnRate);
        }
        if (e.x < fixed16(f.minimumX) && e.heading == 768) {
            e.mode = 1;
            e.phase = 256;
            e.turnRate = reverse ? -64 : 64;
        }
        if (fixed16(f.maximumX) < e.x && e.heading == 256) {
            e.mode = 1;
            e.phase = 768;
            e.turnRate = reverse ? 64 : -64;
        }
        if ((!reverse && py < e.y) || (reverse && e.y < py)) {
            e.turnRate = e.heading < 512 ? 64 : -64;
            e.phase = e.heading < 512 ? 384 : 640;
            e.mode = 0x30001;
        }
        randomTurn(e);
        break;
    case 1:
        e.heading = add32(e.heading, e.turnRate);
        if (magnitude(sub32(int(uint32_t(e.heading) & 1023u), e.phase)) < magnitude(e.turnRate)) {
            e.heading = e.phase;
            e.mode = asr32(e.mode, 16);
        }
        break;
    case 2:
        if ((e.heading == 512 && e.y < e.turnTargetY()) ||
            (e.heading == 0 && e.turnTargetY() < e.y)) {
            e.mode = 1;
            // Source compares the local player X here, without world offset.
            e.phase = player.x < e.x ? 256 : 768;
            e.turnRate = player.x < e.x ? 64 : -64;
        }
        break;
    case 3:
        // Independent source if-blocks: a corner can overwrite the first turn.
        if (e.heading == 128) {
            if (fixed16(f.maximumY) < e.y) {
                e.phase = 384;
                e.mode = 0x30001;
            }
            if (fixed16(f.maximumX) < e.x) {
                e.phase = 896;
                e.mode = 0x30001;
            }
        }
        if (e.heading == 384) {
            if (e.y < fixed16(f.minimumY)) {
                e.phase = 128;
                e.mode = 0x30001;
            }
            if (fixed16(f.maximumX) < e.x) {
                e.phase = 640;
                e.mode = 0x30001;
            }
        }
        if (e.heading == 640) {
            if (e.y < fixed16(f.minimumY)) {
                e.phase = 896;
                e.mode = 0x30001;
            }
            if (e.x < fixed16(f.minimumX)) {
                e.phase = 384;
                e.mode = 0x30001;
            }
        }
        if (e.heading == 896) {
            if (fixed16(f.maximumY) < e.y) {
                e.phase = 640;
                e.mode = 0x30001;
            }
            if (e.x < fixed16(f.minimumX)) {
                e.phase = 128;
                e.mode = 0x30001;
            }
        }
        randomTurn(e);
        break;
    case 4: {
        const int old = e.retargetCountdown();
        e.retargetCountdown() = sub32(old, 1);
        if (old < 0) {
            e.retargetCountdown() = tuning_.retargetInterval;
            e.heading = int(uint32_t(add32(e.heading, retargetTurn(e))) & 1023u);
        }
        break;
    }
    case 5:
        if (pod && (e.heading == 0 || e.heading == 512)) {
            e.mode = 0x50001;
            e.phase = e.heading == 512 ? 0 : 512;
            e.turnRate = e.heading == 512 ? 64 : -64;
            if (reverse)
                e.turnRate = sub32(0, e.turnRate);
        }
        if (e.y < fixed16(f.minimumY) && e.heading == 512) {
            e.mode = 0x50001;
            e.phase = 0;
            e.turnRate = reverse ? -64 : 64;
        }
        if (fixed16(f.maximumY) < e.y && e.heading == 0) {
            e.mode = 0x50001;
            e.phase = 512;
            e.turnRate = reverse ? 64 : -64;
        }
        if ((!reverse && e.x < px) || (reverse && px < e.x)) {
            e.turnRate = e.heading < 512 ? 64 : -64;
            e.phase = e.heading < 512 ? 384 : 640;
            e.mode = 0x50002;
        }
        randomTurn(e);
        break;
    default:
        break;
    }
}
} // namespace grpp
