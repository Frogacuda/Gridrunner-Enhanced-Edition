#include "grpp/formations.hpp"
#include "grpp/native_motion.hpp"
#include <algorithm>
#include <stdexcept>

namespace grpp {
void FormationSystem::apply(const SettingValue &s) {
    switch (s.setting) {
    case LevelSetting::HeadHealth:
        tuning_.headHealth = s.value;
        break;
    case LevelSetting::SegmentHealth:
        tuning_.segmentHealth = s.value;
        break;
    case LevelSetting::AdvancePerTick:
        player_.scoreStep = s.value;
        break;
    case LevelSetting::FormationAllowance:
        tuning_.allowance = s.value;
        break;
    case LevelSetting::SegmentCount:
        tuning_.segmentCount = s.value;
        break;
    case LevelSetting::HeadAttackParameters:
        tuning_.headAttack = uint32_t(s.value);
        break;
    case LevelSetting::HeadSpeedX:
        tuning_.speedX = s.value;
        break;
    case LevelSetting::HeadSpeedY:
        tuning_.speedY = s.value;
        break;
    case LevelSetting::WorldAnchorSpeedX:
        tuning_.anchorSpeedX = s.value;
        break;
    case LevelSetting::WorldAnchorSpeedY:
        tuning_.anchorSpeedY = s.value;
        break;
    case LevelSetting::WorldAnchorDepthStep:
        tuning_.anchorDepthStep = s.value;
        break;
    case LevelSetting::HistorySpacing:
        tuning_.historySpacing = s.value;
        break;
    case LevelSetting::BeamReach:
        tuning_.beamReach = s.value;
        break;
    case LevelSetting::RetargetInterval:
        tuning_.retargetInterval = s.value;
        break;
    case LevelSetting::RetargetTurnStep:
        tuning_.retargetTurnStep = s.value;
        break;
    case LevelSetting::FlyingEnemyPlantsGrid:
        tuning_.flyingPlantsGrid = s.value;
        break;
    case LevelSetting::FlyingEnemyTracking:
        tuning_.flyingTracking = s.value;
        break;
    case LevelSetting::FlyingEnemyAlternateSpeed:
        tuning_.flyingAlternateSpeed = s.value;
        break;
    }
}
void FormationSystem::prepareLevel(const StageSpec &stage, unsigned levelIndex, int cycleDifficulty,
                                   int skipCommands) {
    if (levelIndex >= 50)
        throw std::invalid_argument("Level index must identify one of the original 50 records");
    stage_ = &stage;
    levelIndex_ = levelIndex;
    program_.emplace(stage.spawnProgram);
    // 00418d17 resets only these difficulty-derived values. Other tuning carries.
    const unsigned low = levelIndex & 15u, group = levelIndex >> 4;
    tuning_.specialTurnChance = int(low >> 1);
    tuning_.podResistance = int(low >> 2);
    tuning_.headAttack = group % 3u | std::min(group, 15u) << 18;
    tuning_.splitTransform = low * 2 + 8 | (((low >> 2) + 3) & 3u) << 8 | std::min(group, 5u) << 16;
    tuning_.retargetInterval = -1;
    tuning_.retargetTurnStep = 64;
    tuning_.flyingPlantsGrid = 0;
    tuning_.flyingTracking = 0;
    tuning_.flyingAlternateSpeed = 0;
    for (const auto &setting : stage.settings)
        apply(setting);
    // 00418e11 after overrides. The source has no lower clamp here.
    if (player_.endurance) {
        tuning_.allowance -= 2;
        player_.scoreStep = mul32(player_.scoreStep, 2);
    }
    tuning_.beamReach =
        add32(tuning_.beamReach, add32(player_.arse ? 300 : 0, mul32(cycleDifficulty, 32)));
    if (tuning_.beamReach > player_.height())
        tuning_.beamReach = player_.height();
    baselineAdvance_ = player_.scoreStep;
    activeThreshold_ = 100;
    // The owner supplies its signed rewind/prefix count. A fresh source entry
    // can supply -2; skipping consumes only positive counts but does not erase it.
    emitted_ = skipCommands;
    program_->skip(skipCommands);
}
const SpawnProgram &FormationSystem::program() const {
    if (!program_)
        throw std::logic_error("No level spawn program is bound");
    return *program_;
}
void FormationSystem::applyPattern(uint32_t command) {
    const unsigned selector = (command >> 16) & 15u;
    if (!selector)
        return;
    if (!stage_ || selector > stage_->formationPatterns.size())
        throw std::runtime_error("Formation pattern is outside recovered source data");
    for (const auto &setting : stage_->formationPatterns[selector - 1])
        apply(setting);
}
void FormationSystem::randomIdleMotion(Entity &e) {
    e.x = fixed16(random_.below(player_.width()) + player_.width() / 2);
    e.y = fixed16(random_.below(player_.height()) + player_.height() / 2);
    if (random_.below(8) < 4) {
        e.vx = mul32(random_.below(256) - 128, 2048);
        e.vy = 0;
    } else {
        e.vy = mul32(random_.below(256) - 128, 2048);
        e.vx = 0;
    }
    e.phase = random_.below(50) + 50;
}
void FormationSystem::fillHistory(Entity &e, bool includeHeading) {
    for (auto &point : e.history) {
        point.x = e.x;
        point.y = e.y;
        point.scale = e.depth;
        if (includeHeading)
            point.heading = e.heading;
    }
    e.historyCursor = 0;
}
void FormationSystem::remember(Entity &e, int32_t depth) {
    auto &point = e.history[e.historyCursor++ & 63];
    point.x = e.x;
    point.y = e.y;
    point.scale = depth;
}
void FormationSystem::seedInitialSpawners() {
    if (!program_)
        throw std::logic_error("Level must be prepared before constructing spawners");
    size_t slot = EntityPool::SpawnerFirst;
    for (; slot < EntityPool::SpawnerFirst + EntityPool::SpawnerCount; ++slot) {
        Entity &e = pool_.at(slot);
        randomIdleMotion(e);
        e.flags = 0;
        e.effectMode = 0;
        e.depth = int32_t((slot - EntityPool::SpawnerFirst) * 1024 + 0x1700);
        e.colorCycle = 13;
        e.targetX = fixed16(int((slot & 15) * 32 + 16));
        e.targetY = fixed16(player_.height());
        fillHistory(e, false);
        e.animation = -1;
        e.drawing = 11;
        const auto command = program_->next();
        e.commandOrHealth = command.value_or(0xffffffffu);
        if (!command) {
            e.behavior = 0;
            // Source assigns the loop variable to the limit before its increment.
            slot = EntityPool::SpawnerFirst + EntityPool::SpawnerCount + 1;
            break;
        }
        e.behavior = 10;
    }
    requeueDistance_ = int32_t((slot - EntityPool::SpawnerFirst - 1) * 1024 + 0x1700);
}
void FormationSystem::requeue(Entity &e) {
    if (program_->ended()) {
        e.behavior = 0;
        return;
    }
    randomIdleMotion(e);
    e.flags = 0;
    e.effectMode = 0;
    e.depth = add32(requeueDistance_, player_.scoreClock);
    e.colorCycle = 13;
    fillHistory(e, false);
    const auto command = program_->next();
    e.commandOrHealth = command.value_or(0xffffffffu);
    e.animation = -1;
    e.drawing = 11;
    e.behavior = command ? 10 : 0;
}
void FormationSystem::updateSpawner(Entity &e, const SpawnFrame &frame, EntityCensus &counts) {
    if (e.behavior != 10 && e.behavior != 11)
        throw std::invalid_argument("Spawner dispatcher received an unrelated behavior");
    if (!frame.enabled)
        return;
    if (!program_)
        throw std::logic_error("Spawner has no owning level program");
    if (e.behavior == 10) {
        ++counts.waiting;
        e.x = add32(e.x, e.vx);
        // Assembly 00413128..00413146 confirms the surprising minimumY upper
        // X bound. Preserve it; do not substitute maximumX as a cosmetic fix.
        if (e.x < fixed16(frame.minimumX) || frame.minimumY < pixel(e.x)) {
            e.vx = sub32(0, e.vx);
            e.x = add32(e.x, e.vx);
        }
        e.y = add32(e.y, e.vy);
        if (e.y < fixed16(frame.minimumY) || frame.maximumY < pixel(e.y)) {
            e.vy = sub32(0, e.vy);
            e.y = add32(e.y, e.vy);
        }
        const int oldPhase = e.phase;
        e.phase = sub32(e.phase, 1);
        if (oldPhase < 0) {
            e.phase = random_.below(50) + 50;
            if (!e.vx) {
                e.vx = random_.below(8) < 4 ? e.vy : sub32(0, e.vy);
                e.vy = 0;
            } else {
                e.vy = random_.below(8) < 4 ? e.vx : sub32(0, e.vx);
                e.vx = 0;
            }
        }
        remember(e, e.depth);
        if (sub32(e.depth, player_.scoreClock) >= 0x1200)
            return;
        int occupiedHeadSlots = 0, liveHeads = 0;
        for (size_t i = EntityPool::HeadFirst; i < EntityPool::HeadFirst + EntityPool::HeadCount;
             ++i)
            if (pool_.at(i).behavior != 0)
                ++occupiedHeadSlots;
        for (size_t i = 0; i < EntityPool::HeadFirst + EntityPool::HeadCount; ++i)
            if (pool_.at(i).behavior == 3)
                ++liveHeads;
        if (int(EntityPool::HeadCount) - liveHeads - occupiedHeadSlots < tuning_.allowance ||
            activeThreshold_ < frame.previousActiveEnemies) {
            player_.scoreStep = 0;
            return;
        }
        player_.scoreStep = baselineAdvance_;
        ++tuning_.allowance;
        activeThreshold_ = int((e.commandOrHealth >> 24) & 255);
        if (pool_.at(EntityPool::Companion).behavior != 0 && !program_->ended())
            activeThreshold_ += 2;
        applyPattern(e.commandOrHealth);
        ++emitted_;
        e.behavior = 11;
        e.mode = 0;
        e.phase = -1;
        e.depth = sub32(e.depth, player_.scoreClock);
        e.colorCycle = 7;
        const auto &p = pool_.at(EntityPool::Player);
        const int32_t px = add32(p.x, player_.worldX), py = add32(p.y, player_.worldY);
        e.x = px;
        e.y = py;
        for (auto &point : e.history)
            point.scale = -4096;
        const unsigned offset = e.commandOrHealth & 0x1f0u;
        switch (e.commandOrHealth & 15u) {
        case 0:
        case 1:
            e.targetX = offset ? fixed16(add32(frame.minimumX, int(offset)))
                               : add32(px, fixed16(random_.below(64) - 32));
            e.targetY = (e.commandOrHealth & 15u) == 0 ? -0x200000 : 0x2a00000;
            break;
        case 2:
        case 3:
            e.targetY = offset ? fixed16(add32(frame.minimumY, int(offset)))
                               : add32(py, fixed16(random_.below(64) - 32));
            e.targetX = (e.commandOrHealth & 15u) == 2 ? -0x200000 : 0x2000000;
            break;
        default:
            break; // Original retains the old target for other directions.
        }
        return;
    }
    ++counts.activeEnemies;
    if (e.mode == 0) {
        const int oldPhase = e.phase;
        e.phase = sub32(e.phase, 1);
        if (oldPhase < 1) {
            e.mode = 1;
            e.vx = asr32(sub32(e.targetX, e.x), 5);
            e.vy = asr32(sub32(e.targetY, e.y), 5);
            e.effectMode = 4;
            e.phase = 32;
        }
        pool_.move(e, e.vx, e.vy);
    } else if (e.mode == 1) {
        const int oldPhase = e.phase;
        e.phase = sub32(e.phase, 1);
        if (oldPhase < 1) {
            materialize(e);
            requeue(e);
        } else {
            pool_.move(e, e.vx, e.vy);
        }
    }
    // This write occurs even when requeue just retired the entity.
    remember(e, 4096);
}
int FormationSystem::freeBodyBefore(int slot) const {
    for (int i = slot - 1; i >= 0; --i)
        if (pool_.at(size_t(i)).behavior == 0)
            return i;
    throw std::runtime_error(
        "Original formation allocation precondition violated: no body slot remains");
}
bool FormationSystem::createCompanion() {
    if (player_.endurance)
        return false;
    auto &e = pool_.at(EntityPool::Companion);
    if (e.behavior != 0)
        return false;
    if (pool_.nativeMotion)
        pool_.nativeMotion->invalidate(EntityPool::Companion);
    e.x = fixed16(player_.width());
    e.y = fixed16(player_.height());
    e.vx = e.vy = 0;
    e.ax = e.ay = 0xc000;
    e.life = 0;
    e.companionHealth = int(levelIndex_) * 2 + 40;
    e.companionMaximumHealth = e.companionHealth;
    e.sprite = 0;
    e.drawing = 13;
    e.animation = -1;
    e.flags = 1;
    e.behavior = 13;
    e.scale = 65536;
    tuning_.allowance = sub32(tuning_.allowance, 2);
    player_.scoreStep = mul32(player_.scoreStep, 2);
    baselineAdvance_ = mul32(baselineAdvance_, 2);
    return true;
}
void FormationSystem::materialize(const Entity &source) {
    const int headSlot = pool_.firstFree(EntityPool::HeadFirst, EntityPool::HeadCount);
    if (headSlot < 0)
        return;
    int bodySlot = EntityPool::LastBodySlot;
    while (bodySlot >= 0 && pool_.at(size_t(bodySlot)).behavior != 0)
        --bodySlot;
    if (bodySlot < 0)
        throw std::runtime_error("No original body allocation slot");
    Entity &head = pool_.at(size_t(headSlot));
    --tuning_.allowance;
    head.outsideTicks = 0;
    head.x = source.x;
    head.y = source.y;
    head.depth = 4096;
    head.vx = tuning_.speedX;
    head.vy = tuning_.speedY;
    head.speed = 0;
    head.commandOrHealth = uint32_t(tuning_.headHealth);
    head.hitFlash = 0;
    head.attackParameters = signedBits(tuning_.headAttack);
    head.drawing = 6;
    head.animation = -1;
    head.sprite = tuning_.headSprite;
    head.animationBase = tuning_.headSprite;
    head.mode = 2;
    head.flags = 1;
    head.colorPhase = 0;
    head.colorCycle = 5;
    const unsigned direction = source.commandOrHealth & 15u,
                   family = (source.commandOrHealth >> 20) & 15u;
    auto setDirection = [&](bool targetPlayer, int modeOverride) {
        switch (direction) {
        case 0:
        case 1:
            head.heading = direction == 0 ? 0 : 512;
            if (targetPlayer) {
                const int32_t py = add32(pool_.at(EntityPool::Player).y, player_.worldY);
                const int32_t distance = fixed16(32 - random_.below(32));
                head.turnTargetY() = direction == 0 ? sub32(py, distance) : add32(py, distance);
            }
            break;
        case 2:
            head.heading = 256;
            head.mode = 0;
            break;
        case 3:
            head.heading = 768;
            head.mode = 0;
            break;
        default:
            break;
        }
        if (modeOverride >= 0 && (direction < 4 || family == 3))
            head.mode = modeOverride;
    };
    switch (family) {
    case 0:
        setDirection(true, -1);
        break;
    case 1:
        head.flags |= 2;
        setDirection(true, -1);
        break;
    case 2:
        switch (direction) {
        case 0:
            head.vx = tuning_.flyingAlternateSpeed;
            head.vy = 0x58000;
            head.ax = 0x2000;
            head.ay = 0;
            head.sprite = 0x87;
            break;
        case 1:
            head.vx = tuning_.flyingAlternateSpeed;
            head.vy = -0x58000;
            head.ax = 0x2000;
            head.ay = 0;
            head.sprite = 0x111;
            break;
        case 2:
            head.vx = 0x40000;
            head.vy = tuning_.flyingAlternateSpeed;
            head.ax = 0;
            head.ay = 0x2000;
            head.sprite = 0x113;
            break;
        case 3:
            head.vx = -0x40000;
            head.vy = tuning_.flyingAlternateSpeed;
            head.ax = 0;
            head.ay = -0x2000;
            head.sprite = 0x112;
            break;
        default:
            break;
        }
        tuning_.flyingAlternateSpeed = sub32(0, tuning_.flyingAlternateSpeed);
        head.behavior = 17;
        head.podPlantChance() = 128;
        head.drawing = 3;
        head.mode = tuning_.flyingTracking;
        if (tuning_.flyingPlantsGrid != 0)
            head.flags |= 4;
        return; // Source deliberately leaves links/history/animationBase stale.
    case 3:
        setDirection(false, 4);
        head.retargetCountdown() = -1;
        head.turnRate = 16;
        break;
    case 4:
        setDirection(true, 5);
        break;
    case 5:
        head.flags |= 2;
        setDirection(true, 5);
        break;
    default:
        break;
    }
    head.previous = 0;
    head.next = bodySlot;
    head.chainIndex = 0;
    fillHistory(head, true);
    head.behavior = 3;
    int previous = headSlot, index = 1;
    const int chainMode = head.mode;
    for (int segment = 0; segment < tuning_.segmentCount; ++segment) {
        const int following = freeBodyBefore(bodySlot);
        Entity &body = pool_.at(size_t(bodySlot));
        if (pool_.nativeMotion)
            pool_.nativeMotion->invalidate(size_t(bodySlot));
        body.x = head.x;
        body.y = head.y;
        body.depth = head.depth;
        body.commandOrHealth = uint32_t(tuning_.segmentHealth);
        body.hitFlash = 0;
        body.vx = head.vx;
        body.vy = head.vy;
        body.speed = head.speed;
        body.heading = head.heading;
        body.drawing = 6;
        body.animation = -1;
        body.sprite = tuning_.segmentSprite;
        body.animationBase = tuning_.segmentSprite;
        body.flags = 1;
        body.mode = chainMode;
        body.previous = previous;
        body.next = following;
        body.historySpacing = tuning_.historySpacing;
        body.chainIndex = index++;
        // 00410784 multiplies by a *single-precision* 0.1 constant. A double
        // literal changes later segments' stored phases (first exposed at 9).
        body.colorPhase = float(index) * 0.1f;
        body.colorCycle = 3;
        fillHistory(body, true);
        body.behavior = 4;
        previous = bodySlot;
        bodySlot = following;
    }
    pool_.at(size_t(previous)).next = -1;
}
} // namespace grpp
