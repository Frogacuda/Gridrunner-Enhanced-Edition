#include "grpp/combat.hpp"
#include "grpp/native_motion.hpp"
#include "original_tables.hpp"
#include <stdexcept>

namespace grpp {
namespace {
int32_t magnitude(int32_t value) { return value < 0 ? sub32(0, value) : value; }
int sign(int32_t value) { return value < 0 ? -1 : 1; } // Source zero is positive.
} // namespace
void CombatSystem::award(int points) {
    const int delta = mul32(points, player_.sheepPower);
    state_.score = add32(state_.score, delta);
    state_.stageScore = add32(state_.stageScore, delta);
}
void CombatSystem::considerDrop(int minimumX) {
    const int old = state_.killsUntilDrop;
    state_.killsUntilDrop = sub32(old, 1);
    if (old >= 1)
        return;
    state_.killsUntilDrop = 10;
    const bool restore = state_.pendingRestorePower >= 1;
    if (state_.pendingRestorePower != 0) {
        state_.restorePower = state_.pendingRestorePower;
        state_.pendingRestorePower = 0;
    }
    const int slot = pool_.firstFree(EntityPool::PickupFirst, EntityPool::PickupCount);
    // Source consumes the countdown/pending restore even when the pool is full.
    if (slot < 0)
        return;
    auto &e = pool_.at(size_t(slot));
    e.x = fixed16(add32(random_.below(player_.width()), minimumX));
    e.y = -0x100000;
    e.vx = 0;
    e.vy = 0x20000;
    e.sprite = e.animationBase = restore ? 112 : 104;
    e.drawing = 3;
    e.animation = -1;
    e.behavior = 15;
    e.flags = 0;
    sounds_.push_back(19);
}
void CombatSystem::convertFragment(size_t slot, int effect) {
    auto &e = pool_.at(slot);
    const auto &p = pool_.at(EntityPool::Player);
    e.vx = asr32(sub32(add32(p.x, player_.worldX), e.x), 8);
    e.vy = asr32(sub32(add32(p.y, player_.worldY), e.y), 7);
    e.ax = 0;
    e.ay = 0;
    award(20);
    e.flags &= ~1u;
    e.speed = -128;
    e.depthAcceleration = 0;
    e.glyphWidth() = 0x20000;
    e.glyphHeight() = 0x20000;
    e.behavior = 5;
    e.drawing = 8;
    e.scale = 0;
    e.effectMode = effect;
}
void CombatSystem::convertHead(size_t slot, int effect) {
    auto &e = pool_.at(slot);
    const auto &p = pool_.at(EntityPool::Player);
    e.behavior = 5;
    e.drawing = 8;
    e.trailProfile = 0;
    e.vx = asr32(sub32(add32(p.x, player_.worldX), e.x), 6);
    e.vy = asr32(sub32(add32(p.y, player_.worldY), e.y), 5);
    e.ax = 0;
    e.ay = 0;
    e.depthAcceleration = 0;
    award(100);
    e.glyphWidth() = 0x20000;
    e.glyphHeight() = 0x20000;
    e.speed = -128;
    e.scale = 0;
    e.flags = 0xfffffffeu;
    e.effectMode = effect;
}
void CombatSystem::splitChain(size_t slot, int successor, int originalMode) {
    auto *e = &pool_.at(slot);
    if (e->previous > 0)
        pool_.at(size_t(e->previous)).next = -1;
    if (e->next < 0)
        return;
    int tails = 0;
    for (auto *part = e; part->next >= 0; part = &pool_.at(size_t(part->next))) {
        if (++tails > int(EntityPool::Capacity))
            throw std::runtime_error("Cyclic source chain in split");
    }
    const bool scatter = random_.below(256) < int(tuning_.splitTransform & 255u) && tails > 1;
    if (scatter) {
        sounds_.push_back(26);
        for (;;) {
            e->behavior = 16;
            e->vx = mul32(mul32(e->vx, random_.below(16) + 8), 8);
            e->vy = mul32(mul32(e->vy, random_.below(16) + 8), 8);
            e->ax = 4096;
            e->ay = 4096;
            e->commandOrHealth = ((tuning_.splitTransform >> 16) & 255u) - 1u;
            e->mode = int((tuning_.splitTransform >> 8) & 255u);
            e->turnRate = (random_.below(16) - 8) * 32;
            if (random_.below(8) < 4)
                e->vx = sub32(0, e->vx);
            if (random_.below(8) < 4)
                e->vy = sub32(0, e->vy);
            if (e->mode == 3) {
                convertFragment(size_t(e - &pool_.at(0)), 3);
                e->flags = 0xfffffffeu;
            }
            if (e->next < 0) {
                e->previous = -1;
                break;
            }
            const int next = e->next;
            e->next = -1;
            e->previous = -1;
            e = &pool_.at(size_t(next));
        }
    } else {
        auto &next = pool_.at(size_t(successor));
        if (next.behavior != 4)
            return;
        next.behavior = 3;
        next.outsideTicks = 0;
        next.commandOrHealth = uint32_t(add32(tuning_.headHealth, mul32(state_.difficulty, 2)));
        next.hitFlash = 0;
        next.mode = originalMode == 4 ? 4 : 0;
        next.previous = -1;
        next.phase = 0;
        if (next.mode == 0) {
            next.mode = 1;
            if ((uint32_t(next.heading) & 1023u) < 513u) {
                next.phase = 768;
                next.turnRate = next.flags & 2 ? 64 : -64;
            } else {
                next.phase = 256;
                next.turnRate = next.flags & 2 ? -64 : 64;
            }
        } else {
            next.turnRate = 16;
            next.life = -1;
        }
        next.sprite = next.animationBase = tuning_.headSprite;
        next.colorCycle = 5;
        next.attackParameters = signedBits(tuning_.headAttack);
        next.flags |= 1;
    }
}
void CombatSystem::plantPod(const Entity &e) {
    const int x = asr32(e.x, 20), y = asr32(e.y, 20);
    if (x > 0 && x < player_.columns() && y > 0 && y < player_.rows())
        grid_[size_t(x + y * player_.columns())] = 0x01000001u;
}
void CombatSystem::beamDamage(size_t slot, int minimumX) {
    if (player_.beamHitConsumed || player_.beamMode != 4 || int(slot) != player_.beamTarget)
        return;
    player_.beamHitConsumed = true;
    sounds_.push_back(0);
    auto &e = pool_.at(slot);
    const int old = signedBits(e.commandOrHealth);
    --e.commandOrHealth;
    if (old >= 0) {
        e.hitFlash = 5;
        return;
    }
    sounds_.push_back(27);
    e.colorCycle = 7;
    award(500);
    if (e.behavior == 3) {
        splitChain(slot, e.next, e.mode);
        convertHead(slot, 9);
    } else {
        if (e.behavior == 4)
            splitChain(slot, e.next, e.mode);
        convertFragment(slot, 9);
    }
    plantPod(e);
    considerDrop(minimumX);
}
bool CombatSystem::contactsPlayer(const Entity &e, int radius) const {
    const auto &p = pool_.at(EntityPool::Player);
    return p.behavior != 14 && player_.transitionTicks == 0 &&
           magnitude(sub32(pixel(add32(p.x, player_.worldX)), pixel(e.x))) < radius &&
           magnitude(sub32(pixel(add32(p.y, player_.worldY)), pixel(e.y))) < radius;
}
void CombatSystem::deathAnnouncement() {
    auto &a = pool_.at(EntityPool::Announcement);
    a.behavior = 18;
    a.heading = 0;
    a.drawing = 16;
    a.animation = -1;
    a.sprite = 256;
    a.transform.noiseSeed = uint32_t(asr32(mul32(random_.below(512), 0x31415927), 9));
    a.transform.tileWidth = 8;
    a.transform.tileHeight = 1;
    a.transform.jitterY = 64;
    a.transform.jitterX = 240;
    a.transform.axisXX = 16;
    a.transform.axisXY = 0;
    a.transform.axisYX = 0;
    a.transform.axisYY = 96;
    a.transform.pivotX = a.transform.pivotY = 0;
    a.flags = 0;
    a.transform.flags = 0;
}
void CombatSystem::killPlayer(size_t hitter) {
    player_.transitionTicks = 1;
    deathAnnouncement();
    if (player_.arse) {
        // AoF 0040bb8e / 004294c0: every death selects one of eight voices.
        // Preserve its single RNG draw; the original's optional voice uses two.
        static constexpr int voices[]{1, 2, 3, 4, 31, 32, 33, 34};
        sounds_.push_back(voices[random_.below(8)]);
    } else if (random_.below(16) < 4)
        sounds_.push_back(random_.below(4) + 1);
    sounds_.push_back(random_.below(2) + 10);
    auto &p = pool_.at(EntityPool::Player);
    p.transform.noiseSeed = uint32_t(asr32(mul32(random_.below(512), 0x31415927), 9));
    p.transform.tileWidth = random_.below(8) + 1;
    p.transform.tileHeight = random_.below(8) + 16;
    p.transform.jitterY = 64;
    p.transform.jitterX = 240;
    p.transform.axisXX = 32;
    p.transform.axisXY = 0;
    p.transform.axisYX = 0;
    p.transform.axisYY = 32;
    p.transform.pivotX = p.transform.pivotY = 0;
    p.flags = 0;
    p.transform.flags = 2;
    p.drawing = 9;
    for (auto &point : p.history)
        point.y = p.y;
    p.historyCursor = 0;
    p.vy = 0;
    state_.travellingAtDeath = 0;
    for (size_t i = 0; i < EntityPool::Player; ++i)
        state_.travellingAtDeath += pool_.at(i).behavior == 11;
    // The source treats slot zero as a null hitter pointer.
    if (hitter) {
        auto &e = pool_.at(hitter);
        e.behavior = 12;
        e.drawing = 12;
        e.scale = 0;
        e.impactGrowthStep() = 1024;
        e.colorCycle = 7;
    }
}
void CombatSystem::playerContact(size_t slot) {
    if (contactsPlayer(pool_.at(slot), 16)) {
        state_.frameFlashTicks = 6;
        killPlayer(slot);
    }
}
bool CombatSystem::withinWorldSpriteBounds(const Entity &e, const ChainFrame &f, bool broad) const {
    const auto &s = assets_.sprite(size_t(e.sprite));
    const int x = pixel(e.x), y = pixel(e.y);
    if (broad)
        return (x < 0 ? magnitude(x) <= s.width : sub32(x, player_.worldWidth()) <= s.width) &&
               (y < 0 ? magnitude(y) <= s.height : sub32(y, player_.worldHeight()) <= s.height);
    // 0040e2e3 reads the source's fixed player_.worldWidth()-pixel world bound, not the
    // 240-pixel presentation width or the moving camera's right boundary.
    return (x < 0 ? magnitude(x) <= s.width : sub32(x, player_.worldWidth()) <= s.width) &&
           (y < sub32(f.minimumY, 32) ? magnitude(y) <= s.height
                                      : sub32(y, add32(f.maximumY, 32)) <= s.height);
}
void CombatSystem::writeEffectHistory(Entity &e, bool tag, bool conditional) {
    auto &p = e.history[e.historyCursor++ & 63u];
    p.x = e.x;
    p.y = e.y;
    p.scale = e.depth;
    if (tag)
        p.tag = 1;
    if (conditional) {
        p.tag = e.depth < e.depthLimit() ? 1 : 0;
        if (!p.tag) {
            const int old = e.heading;
            e.heading = sub32(old, 1);
            if (old < 1)
                e.behavior = 0;
        }
    }
}
void CombatSystem::updateSplit(Entity &e, const ChainFrame &f) {
    if (e.behavior != 16)
        return;
    e.heading = add32(e.heading, e.turnRate);
    pool_.move(e, e.vx, e.vy);
    playerContact(size_t(&e - &pool_.at(0)));
    if (e.mode == 0) {
        if (!withinWorldSpriteBounds(e, f, false))
            e.behavior = 0;
    } else if (e.mode == 1) {
        if ((e.x < fixed16(f.minimumX) && e.vx < 0) || (fixed16(f.maximumX) < e.x && e.vx > 0))
            e.vx = sub32(0, e.vx);
        if ((e.y < fixed16(f.minimumY) && e.vy < 0) || (fixed16(f.maximumY) < e.y && e.vy > 0))
            e.vy = sub32(0, e.vy);
    } else if (e.mode == 2) {
        const auto &p = pool_.at(EntityPool::Player);
        const int dx = sub32(add32(p.x, player_.worldX), e.x),
                  dy = sub32(add32(p.y, player_.worldY), e.y);
        e.vx = add32(e.vx, mul32(e.ax, sign(dx) * (sign(dx) == sign(e.vx) ? 1 : 2)));
        e.vy = add32(e.vy, mul32(e.ay, sign(dy) * (sign(dy) == sign(e.vy) ? 1 : 2)));
        // Source mode 2 performs a second move after changing velocity.
        pool_.move(e, e.vx, e.vy);
        if (!withinWorldSpriteBounds(e, f, true))
            e.behavior = 0;
    }
}
void CombatSystem::updateFlying(Entity &e, const ChainFrame &f) {
    if (e.sprite < 274) {
        if ((e.vy < 1 && pixel(e.y) < -40) || (e.vy > 0 && pixel(e.y) > player_.worldHeight() + 40))
            e.behavior = 0;
    } else if ((e.vx < 1 && pixel(e.x) < -40) ||
               (e.vx > 0 && pixel(e.x) > player_.worldWidth() + 40))
        e.behavior = 0;
    if (random_.below(256) < e.podPlantChance()) {
        const int x = add32(pixel(e.x), 8), y = pixel(e.y);
        if (x > 0 && x < player_.worldWidth() && y > 0 && y < player_.worldHeight())
            grid_[size_t(asr32(x, 4) + asr32(e.y, 20) * player_.columns())] = 0x01000001;
    }
    auto &v = e.sprite < 274 ? e.vx : e.vy;
    const int coordinate = e.sprite < 274 ? e.x : e.y;
    const int minimum = fixed16(e.sprite < 274 ? f.minimumX : f.minimumY),
              maximum = fixed16(e.sprite < 274 ? f.maximumX : f.maximumY);
    if (e.mode == 1) {
        if ((v < 1 && coordinate < minimum) || (v > 0 && maximum < coordinate))
            v = sub32(0, v);
    } else if (e.mode == 2) {
        const auto &p = pool_.at(EntityPool::Player);
        const int delta = e.sprite < 274 ? sub32(add32(p.x, player_.worldX), e.x)
                                         : sub32(add32(p.y, player_.worldY), e.y);
        const int accel = e.sprite < 274 ? e.ax : e.ay;
        v = add32(v, mul32(accel, sign(delta) * (sign(delta) == sign(v) ? 1 : 2)));
    }
    if (e.sprite < 274 && (e.flags & 4) && pool_.at(EntityPool::Player).behavior != 14 &&
        player_.transitionTicks == 0 &&
        magnitude(sub32(pixel(e.y), pixel(add32(pool_.at(EntityPool::Player).y, player_.worldY)))) <
            8) {
        state_.frameFlashTicks = 6;
        killPlayer(size_t(&e - &pool_.at(0)));
    }
    pool_.move(e, e.vx, e.vy);
}
void CombatSystem::shotDamage(size_t slot, int minimumX) {
    auto &e = pool_.at(slot);
    switch (e.behavior) {
    case 3:
    case 4: {
        const bool head = e.behavior == 3;
        sounds_.push_back(0);
        const int old = signedBits(e.commandOrHealth);
        --e.commandOrHealth;
        if (old >= 0) {
            e.hitFlash = 5;
            break;
        }
        splitChain(slot, e.next, e.mode);
        plantPod(e);
        if (head)
            convertHead(slot, 1);
        else
            convertFragment(slot, 3);
        considerDrop(minimumX);
        break;
    }
    case 7:
        burstEnemyShot(e, 5);
        break;
    case 13:
        award(100);
        if (e.life < 1) {
            e.companionHealth = sub32(e.companionHealth, 1);
            sounds_.push_back(14);
            e.sprite = 5;
        }
        break;
    case 16:
    case 17: {
        sounds_.push_back(0);
        const int old = signedBits(e.commandOrHealth);
        --e.commandOrHealth;
        if (old >= 0) {
            e.hitFlash = 5;
            break;
        }
        plantPod(e);
        convertFragment(slot, 3);
        considerDrop(minimumX);
        break;
    }
    default:
        break; // Source consumes contact even for unhandled active types.
    }
}
void CombatSystem::updatePlayerShot(Entity &e, const ChainFrame &f, bool advance) {
    auto &consumed = contactBudget(e);
    if (!advance && consumed)
        return;
    int x = asr32(add32(e.x, 0x80000), 20), y = asr32(add32(e.y, 0x80000), 20), width = 1;
    if (e.sprite > 106) {
        --x;
        width = 3;
    }
    if (e.sprite > 108) {
        --x;
        width += 2;
    }
    if (y >= 0 && y < player_.rows())
        for (int column = 0; column < width; ++column) {
            if (x >= 0 && x < player_.columns()) {
                auto &cell = grid_[size_t(x + y * player_.columns())];
                if ((cell & 255u) == 1) {
                    consumed = true;
                    // The original reuses its scan width as the cell's hit count.
                    // A contact can therefore shorten or extend this same loop.
                    width = int((cell >> 24) & 255u) + 1;
                    sounds_.push_back(5);
                    if (add32(tuning_.podResistance, state_.difficulty) < width) {
                        cell = 0;
                        award(50);
                        e.behavior = 12;
                        e.scale = 0;
                        e.impactGrowthStep() = 4096;
                        e.drawing = 12;
                        e.colorCycle = 7;
                    } else {
                        award(1);
                        cell = uint32_t(width) * 0x1000000u | uint32_t(width) * 0x10000u |
                               (cell & 255u) | 256u;
                        if (player_.sheepPower < 3) {
                            e.behavior = 12;
                            e.scale = 0;
                            e.impactGrowthStep() = 4096;
                            e.drawing = 12;
                            e.colorCycle = 7;
                        }
                    }
                }
                ++x;
            }
        }
    if (e.behavior == 2) {
        for (size_t slot = 0; slot < EntityPool::Player; ++slot)
            if (squareCollision(e, pool_.at(slot), 16)) {
                consumed = true;
                if (e.behavior != 0 && player_.sheepPower < 3)
                    e.behavior = 0;
                shotDamage(slot, f.minimumX);
                break;
            }
        if (advance && !withinWorldSpriteBounds(e, f, false))
            e.behavior = 0;
    }
    if (!advance)
        return;
    e.vx = add32(e.vx, e.ax);
    e.vy = add32(e.vy, e.ay);
    if (e.drawing == 15) {
        pool_.move(e, e.vx, e.vy);
    } else {
        const float oldPhase = state_.shotColorPhase;
        const auto firstParticle = e.historyCursor;
        const int particleX = e.x, particleY = e.y;
        for (int step = 0; step < e.phase; ++step) {
            auto &p = e.history[e.historyCursor++ & 63u];
            p.tag = random_.below(64);
            p.x = e.x;
            p.y = e.y;
            p.vx = p.vy = 0;
            state_.shotColorPhase += .1f;
            p.color = (p.color & 0xffff0000u) |
                      packOriginalColor(cycleColor(original::cycles[0], state_.shotColorPhase));
            pool_.move(e, e.vx, e.vy);
        }
        state_.shotColorPhase = oldPhase + .1f;
        // Source ages only indices 0..62, leaving record 63 untouched.
        for (size_t index = 0; index < 63; ++index) {
            auto &p = e.history[index];
            p.tag = sub32(p.tag, 4);
            p.x = add32(p.x, p.vx);
            p.y = add32(p.y, p.vy);
        }
        if (pool_.nativeMotion && e.behavior)
            pool_.nativeMotion->scheduleParticles(pool_, e, firstParticle, e.phase, particleX,
                                                  particleY);
    }
}
void CombatSystem::burstEnemyShot(Entity &e, int points) {
    e.behavior = 9;
    e.transform.noiseSeed = 0x31415927;
    e.transform.tileWidth = 1;
    e.transform.tileHeight = 8;
    e.transform.axisXX = 32;
    e.transform.axisXY = e.transform.axisYX = 0;
    e.transform.axisYY = 32;
    e.transform.pivotX = e.transform.pivotY = 0;
    e.transform.flags = 0;
    award(points);
    e.flags = 0xfffffffeu;
    e.drawing = 1;
    e.scale = 0;
}
void CombatSystem::companionDamage(size_t slot) {
    auto &e = pool_.at(slot);
    const int kind = e.behavior;
    if (kind == 7) {
        burstEnemyShot(e, 10);
        return;
    }
    if (kind != 3 && kind != 4 && kind != 16 && kind != 17)
        return;
    if (kind != 3)
        sounds_.push_back(0);
    const int old = signedBits(e.commandOrHealth);
    --e.commandOrHealth;
    if (old >= 0) {
        e.hitFlash = 5;
        return;
    }
    if (kind == 3)
        sounds_.push_back(0);
    if (kind == 3 || kind == 4)
        splitChain(slot, e.next, e.mode);
    const auto &p = pool_.at(EntityPool::Player);
    e.trailProfile = 0;
    e.vx = asr32(sub32(add32(p.x, player_.worldX), e.x), 4);
    e.vy = asr32(sub32(add32(p.y, player_.worldY), e.y), 4);
    e.ax = e.ay = 0;
    e.depthAcceleration = 0;
    e.glyphWidth() = e.glyphHeight() = 0x20000;
    e.speed = -128;
    e.scale = 0;
    e.flags = 0xfffffffeu;
    e.behavior = 5;
    e.drawing = 8;
    e.effectMode = 3;
    award(kind == 3 ? 200 : kind == 17 ? 50 : 40);
}
void CombatSystem::updateCompanion(Entity &e, const EntityCensus &census) {
    if (e.scale >= 1) {
        e.scale = sub32(e.scale, 2048);
        return;
    }
    if (e.life > 0)
        e.life = sub32(e.life, 1);
    if (e.companionHealth < 0) {
        e.companionHealth = e.companionMaximumHealth;
        sounds_.push_back(15);
        state_.frameFlashTicks = 12;
        e.life = 200;
    }
    e.ay = e.ax;
    const auto &target = census.nearestHead < 1 ? pool_.at(EntityPool::Player)
                                                : pool_.at(size_t(census.nearestHead));
    const int tx = census.nearestHead < 1 ? add32(target.x, player_.worldX) : target.x;
    const int ty = census.nearestHead < 1 ? add32(target.y, player_.worldY) : target.y;
    const int dx = sub32(tx, e.x), dy = sub32(ty, e.y);
    // Reverse-direction acceleration is doubled. Source sign(0) is +1.
    e.vx = add32(e.vx, mul32(e.ax, sign(dx) * (sign(dx) == sign(e.vx) ? 1 : 2)));
    e.vy = add32(e.vy, mul32(e.ay, sign(dy) * (sign(dy) == sign(e.vy) ? 1 : 2)));
    // Source boundary decisions use the completed move, not its first native
    // fraction. Otherwise a late crossing escapes and successive corrective
    // moves cancel each other while the head remains outside.
    const int endX = add32(e.x, e.vx), endY = add32(e.y, e.vy);
    auto bounce = [&](int end, int velocity, int bound) {
        if (end < 0)
            return !pool_.nativeMotion || velocity < 0;
        if (pixel(end) > bound)
            return !pool_.nativeMotion || velocity > 0;
        return false;
    };
    pool_.move(e, e.vx, e.vy);
    if (bounce(endX, e.vx, player_.worldWidth())) {
        e.vx = sub32(0, e.vx);
        pool_.move(e, e.vx, 0);
    }
    if (bounce(endY, e.vy, player_.worldHeight())) {
        e.vy = sub32(0, e.vy);
        pool_.move(e, 0, e.vy);
    }
    companionContacts(e);
}
void CombatSystem::companionContacts(Entity &e) {
    auto &consumed = contactBudget(e);
    if (pool_.nativeMotion && consumed)
        return;
    for (size_t slot = 0; slot < EntityPool::Companion; ++slot) {
        const auto &victim = pool_.at(slot);
        if (victim.behavior == 0 || !(victim.flags & 1) || victim.behavior == 5 ||
            victim.drawing == 1)
            continue;
        const int x = magnitude(sub32(pixel(e.x), pixel(victim.x))),
                  y = magnitude(sub32(pixel(e.y), pixel(victim.y)));
        const bool hit = e.life < 1 ? x <= 16 && y <= 16 : x < 16 || y < 16;
        if (hit) {
            consumed = true;
            companionDamage(slot);
            break;
        } // One damage opportunity per source interval.
    }
}
bool &CombatSystem::contactBudget(Entity &e) {
    const auto slot = size_t(&e - &pool_.at(0));
    if (pool_.nativeMotion) {
        auto generation = pool_.nativeMotion->generation(slot);
        if (generation != nativeHitGeneration_[slot]) {
            nativeHitConsumed_[slot] = false;
            nativeHitGeneration_[slot] = generation;
        }
    }
    return nativeHitConsumed_[slot];
}
void CombatSystem::revivalAnnouncement() {
    sounds_.push_back(28);
    auto &a = pool_.at(EntityPool::Announcement);
    a.behavior = 18;
    a.heading = 0;
    a.drawing = 16;
    a.animation = -1;
    a.sprite = 249;
    a.transform.noiseSeed = uint32_t(asr32(mul32(random_.below(512), 0x31415927), 9));
    a.transform.tileWidth = 2;
    a.transform.tileHeight = 2;
    a.transform.jitterY = 64;
    a.transform.jitterX = player_.width();
    a.transform.axisXX = 32;
    a.transform.axisXY = a.transform.axisYX = 0;
    a.transform.axisYY = 32;
    a.transform.pivotX = a.transform.pivotY = 0;
    a.flags = 0;
    a.transform.flags = 2;
}
void CombatSystem::updatePickup(Entity &e, const ChainFrame &f, bool advance) {
    if (advance)
        pool_.move(e, 0, e.vy);
    e.sprite = e.animationBase + (asr32(f.frameNumber, 1) & 1);
    if (e.y > fixed16(player_.worldHeight() + 20))
        e.behavior = 0;
    auto &player = pool_.at(EntityPool::Player);
    if (player_.transitionTicks == 1 || player.behavior == 14 ||
        magnitude(sub32(pixel(e.x), pixel(add32(player.x, player_.worldX)))) >= 16 ||
        magnitude(sub32(pixel(e.y), pixel(add32(player.y, player_.worldY)))) >= 16)
        return;

    e.scale = 1;
    e.effectMode = 7;
    if (player_.transitionTicks < 1)
        player_.sheepPower = add32(player_.sheepPower, 1);
    else
        revivalAnnouncement();
    e.colorCycle = 7;
    e.drawing = 8;
    const int centerX = add32(f.minimumX, asr32(sub32(f.maximumX, f.minimumX), 1));
    const int centerY = add32(f.minimumY, asr32(sub32(f.maximumY, f.minimumY), 1));
    e.vx = asr32(sub32(fixed16(centerX), e.x), 8);
    e.vy = asr32(sub32(fixed16(centerY), e.y), 7);
    e.depth = 4096;
    e.ax = e.ay = 0;

    // Award with the incremented multiplier BEFORE capping it at nine.
    award(250);
    e.flags = 0xfffffffeu;
    e.speed = 384;
    e.depthAcceleration = -32;
    e.glyphWidth() = e.glyphHeight() = 0x40000;
    e.behavior = 5;
    int sound = random_.below(4) + 20; // Drawn even when restoration replaces it.
    if (player_.sheepPower > 9) {
        player_.sheepPower = 9;
        formations_.createCompanion();
        e.effectMode = 7;
        sounds_.push_back(14);
        award(750);
    }
    for (auto &point : e.history) {
        point.x = e.x;
        point.y = e.y;
        point.scale = e.depth;
    }
    if (e.animationBase > 111) {
        sound = 29;
        player_.sheepPower = add32(state_.restorePower, 1);
        if (player_.sheepPower > 9)
            player_.sheepPower = 9;
    }
    if (player_.transitionTicks < 0 && !(player.flags & 128u) && player_.reverseTransition) {
        sounds_.push_back(24);
        state_.pendingRestorePower = player_.sheepPower;
    } else {
        if (player_.transitionTicks == 0)
            sounds_.push_back(sound);
        player_.transitionTicks = -20;
        player.drawing = 9;
        player.flags |= 128u;
    }
}
bool CombatSystem::supports(int behavior) {
    switch (uint32_t(behavior) & 255u) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 18:
        return true;
    default:
        return false;
    }
}
void CombatSystem::nativeContacts(const ChainFrame &f) {
    for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
        auto &e = pool_.at(slot);
        switch (e.behavior) {
        case 2:
            updatePlayerShot(e, f, false);
            break;
        case 3:
        case 4:
        case 7:
        case 16:
        case 17:
            playerContact(slot);
            if (e.behavior == 17 && e.sprite < 274 && (e.flags & 4) &&
                pool_.at(EntityPool::Player).behavior != 14 && player_.transitionTicks == 0 &&
                magnitude(sub32(pixel(e.y), pixel(add32(pool_.at(EntityPool::Player).y,
                                                        player_.worldY)))) < 8) {
                state_.frameFlashTicks = 6;
                killPlayer(slot);
            }
            break;
        case 13:
            if (e.scale < 1)
                companionContacts(e);
            break;
        case 15:
            updatePickup(e, f, false);
            break;
        default:
            break;
        }
    }
}
void CombatSystem::update(size_t slot, const ChainFrame &f, EntityCensus &census) {
    auto &e = pool_.at(slot);
    switch (uint32_t(e.behavior) & 255u) {
    case 2:
        updatePlayerShot(e, f);
        break;
    case 3:
        motion_.advanceHead(slot, f, census);
        playerContact(slot);
        if (player_.transitionTicks > 0)
            convertHead(slot, 1);
        beamDamage(slot, f.minimumX);
        motion_.steerHead(e, f, grid_);
        break;
    case 4:
        ++census.activeEnemies;
        if (player_.transitionTicks > 1)
            convertFragment(slot, 3);
        playerContact(slot);
        beamDamage(slot, f.minimumX);
        motion_.followBody(e, f.trailColorPhase);
        break;
    case 5:
        e.vx = add32(e.vx, e.ax);
        e.vy = add32(e.vy, e.ay);
        e.speed = add32(e.speed, e.depthAcceleration);
        pool_.move(e, e.vx, e.vy, e.speed);
        writeEffectHistory(e, true, false);
        e.scale = add32(e.scale, 2048);
        if (e.scale > 65536)
            e.behavior = 0;
        break;
    case 6:
        e.vx = add32(e.vx, e.ax);
        e.vy = add32(e.vy, e.ay);
        pool_.move(e, e.vx, e.vy, e.speed);
        writeEffectHistory(e, false, true);
        break;
    case 7:
        ++census.activeEnemies;
        pool_.move(e, e.vx, e.vy);
        beamDamage(slot, f.minimumX);
        playerContact(slot);
        if (!withinWorldSpriteBounds(e, f, false))
            e.behavior = 0;
        break;
    case 9:
        e.scale = add32(e.scale, 2048);
        if (e.scale > 65536)
            e.behavior = 0;
        break;
    case 12:
        e.scale = add32(e.scale, e.impactGrowthStep());
        if (e.scale > 65536)
            e.behavior = 0;
        break;
    case 13:
        updateCompanion(e, census);
        break;
    case 15:
        updatePickup(e, f);
        break;
    case 16:
        ++census.activeEnemies;
        beamDamage(slot, f.minimumX);
        updateSplit(e, f);
        break;
    case 17:
        ++census.activeEnemies;
        beamDamage(slot, f.minimumX);
        updateFlying(e, f);
        break;
    case 18:
        e.heading = add32(e.heading, 1);
        if (e.heading > 100)
            e.behavior = 0;
        break;
    default:
        throw std::invalid_argument("Combat dispatcher received an untranslated behavior");
    }
}
} // namespace grpp
