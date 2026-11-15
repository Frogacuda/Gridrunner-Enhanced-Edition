// Controlled fixtures, not automated campaign clears. Complete spawn programs
// are drained with enemy slots released after inspection to exercise late waves.
#include "aof_levels.hpp"
#include "grpp/combat.hpp"
#include "grpp/native_motion.hpp"
#include "original_levels.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
using namespace grpp;
void require(bool v, const std::string &message) {
    if (!v)
        throw std::runtime_error(message);
}
struct Rig {
    EntityPool pool;
    PlayerState player;
    GameRandom random{0x1982};
    FormationSystem formations{pool, player, random};
    CombatState state;
    WorldGrid grid{};
    std::vector<int> sounds;
    CombatSystem combat;
    NativeMotion native;
    int steps;
    Rig(const Assets &assets, bool aof, int rate)
        : combat(pool, player, random, formations, state, grid, assets, sounds),
          native(rate == 1 ? 3 : rate), steps(rate) {
        player.arse = aof;
        pool.at(89).behavior = 14;
        pool.at(89).x = fixed16(player.width() / 2);
        pool.at(89).y = fixed16(player.height() / 2);
        player.worldX = pool.at(89).x;
        player.worldY = pool.at(89).y;
        if (rate > 1)
            pool.nativeMotion = &native;
    }
    ChainFrame frame(int tick = 0) const {
        return {pixel(player.worldX) + 16,
                pixel(player.worldX) + player.width() - 16,
                pixel(player.worldY) + 16,
                pixel(player.worldY) + player.height() - 16,
                .1f,
                tick};
    }
    void tick(size_t slot, int number = 0) {
        if (steps > 1)
            native.beginInterval();
        combat.beginNativeInterval();
        EntityCensus census;
        combat.update(slot, frame(number), census);
        for (int sub = 1; sub < steps; ++sub)
            native.continueMove(pool, slot);
    }
};
struct SpawnRecord {
    int x, y, behavior, mode, heading, vx, vy, health, segments, spacing;
    uint32_t random;
    bool operator==(const SpawnRecord &b) const {
        return x == b.x && y == b.y && behavior == b.behavior && mode == b.mode &&
               heading == b.heading && vx == b.vx && vy == b.vy && health == b.health &&
               segments == b.segments && spacing == b.spacing && random == b.random;
    }
};
std::vector<SpawnRecord> drain(Rig &rig, bool aof, unsigned stage, int difficulty,
                               std::set<unsigned> &families, bool crowded = false) {
    auto *r = &rig;
    const int steps = r->steps;
    r->native.reset();
    r->player.scoreClock = 0;
    // Keep this owner's real tuning/RNG through successive stage scripts,
    // including the final formation-pattern overrides of earlier levels.
    r->formations.prepareLevel((aof ? aof::stages : original::stages)[stage], stage, difficulty);
    r->formations.seedInitialSpawners();
    std::vector<SpawnRecord> records;
    SpawnProgram expected((aof ? aof::stages : original::stages)[stage].spawnProgram);
    size_t commands = 0;
    while (expected.next()) {
        require(++commands < 10000, "Unbounded stage program");
    }
    for (int tick = 0; tick < 100000; ++tick) {
        if (steps > 1)
            r->native.beginInterval();
        EntityCensus census;
        r->player.scoreClock = add32(r->player.scoreClock, r->player.scoreStep);
        const auto f = r->frame(tick);
        SpawnFrame spawn{f.minimumX, f.maximumX, f.minimumY, f.maximumY, 0, true};
        if (crowded)
            for (size_t slot = 0; slot < 88; ++slot) {
                const int behavior = r->pool.at(slot).behavior;
                if (behavior == 3 || behavior == 4 || behavior == 11 || behavior == 17)
                    ++spawn.previousActiveEnemies;
            }
        for (size_t slot = 12; slot < 28; ++slot) {
            auto &spawner = r->pool.at(slot);
            if (spawner.behavior != 10 && spawner.behavior != 11)
                continue;
            const auto command = spawner.commandOrHealth;
            std::array<bool, 8> occupied{};
            for (size_t headSlot = 78; headSlot < 86; ++headSlot)
                occupied[headSlot - 78] = r->pool.at(headSlot).behavior != 0;
            r->formations.updateSpawner(spawner, spawn, census);
            for (size_t headSlot = 78; headSlot < 86; ++headSlot) {
                auto &head = r->pool.at(headSlot);
                if (!head.behavior || occupied[headSlot - 78])
                    continue;
                const auto family = (command >> 20) & 15u;
                families.insert(family);
                require(head.behavior == (family == 2 ? 17 : 3),
                        "Materialized wrong enemy family at stage " + std::to_string(stage + 1));
                int segments = 0, spacing = 0, previous = int(headSlot), next = head.next;
                if (head.behavior == 3) {
                    std::set<int> visited;
                    while (next >= 0) {
                        require(next <= EntityPool::LastBodySlot && visited.insert(next).second,
                                "Invalid/cyclic materialized chain");
                        const auto &body = r->pool.at(size_t(next));
                        require(body.behavior == 4 && body.previous == previous,
                                "Broken chain predecessor");
                        require(body.historySpacing > 0 && body.historySpacing <= 64,
                                "Invalid history delay");
                        require(body.commandOrHealth ==
                                    uint32_t(r->formations.tuning().segmentHealth),
                                "Incorrect late-wave body health");
                        spacing = body.historySpacing;
                        ++segments;
                        previous = next;
                        next = body.next;
                    }
                    require(segments == r->formations.tuning().segmentCount,
                            "Late-wave segment count mismatch");
                }
                records.push_back({head.x, head.y, head.behavior, head.mode, head.heading, head.vx,
                                   head.vy, signedBits(head.commandOrHealth), segments, spacing,
                                   r->random.state()});
                // This fixture inspects emission only. It explicitly frees the
                // inspected formation; ordinary-input sweeps test live play.
                if (!crowded)
                    for (size_t enemy = 0; enemy < 88; ++enemy) {
                        auto &actor = r->pool.at(enemy);
                        if (actor.behavior != 10 && actor.behavior != 11)
                            actor.behavior = 0;
                    }
            }
        }
        for (int sub = 1; sub < steps; ++sub)
            for (size_t slot = 12; slot < 28; ++slot)
                r->native.continueMove(r->pool, slot);
        // Saturation fixture: hold formations for 128 events to exercise real
        // admission backpressure, then release inspected slots as a batch.
        if (crowded && (tick % 128 == 127))
            for (size_t enemy = 0; enemy < 88; ++enemy) {
                auto &actor = r->pool.at(enemy);
                if (actor.behavior != 10 && actor.behavior != 11)
                    actor.behavior = 0;
            }
        bool waiting = false;
        for (size_t slot = 12; slot < 28; ++slot) {
            const int behavior = r->pool.at(slot).behavior;
            waiting |= behavior == 10 || behavior == 11;
        }
        if (!waiting) {
            require(r->formations.programEnded() && records.size() == commands,
                    "Stage program did not emit every command");
            for (size_t enemy = 0; enemy < 88; ++enemy)
                r->pool.at(enemy).behavior = 0;
            return records;
        }
    }
    throw std::runtime_error("Spawn program stalled at stage " + std::to_string(stage + 1) +
                             " aof=" + std::to_string(aof) + " rate=" + std::to_string(steps) +
                             " crowded=" + std::to_string(crowded) +
                             " emitted=" + std::to_string(records.size()) +
                             " expected=" + std::to_string(commands) +
                             " allowance=" + std::to_string(r->formations.tuning().allowance));
}
int main(int argc, char **argv) {
    try {
        require(argc == 3, "ASSETS REPORT required");
        Assets assets;
        assets.load(argv[1]);
        std::ofstream report(argv[2]);
        require(bool(report), "Cannot open report");
        report << "edition,difficulty,crowded,stage,substeps,formations,result\n";
        size_t formations = 0, boundaries = 0, retirements = 0, chainTicks = 0, damageChecks = 0,
               attackChecks = 0, podChecks = 0;
        std::set<unsigned> families;
        for (bool aof : {false, true})
            for (int difficulty : {0, 2, 10})
                for (bool crowded : {false, true}) {
                    auto source = std::make_unique<Rig>(assets, aof, 1),
                         native100 = std::make_unique<Rig>(assets, aof, 3),
                         native200 = std::make_unique<Rig>(assets, aof, 6);
                    for (unsigned stage = 0; stage < 50; ++stage) {
                        const auto legacy =
                            drain(*source, aof, stage, difficulty, families, crowded);
                        for (auto *owner : {native100.get(), native200.get()}) {
                            const int steps = owner->steps;
                            const auto native =
                                drain(*owner, aof, stage, difficulty, families, crowded);
                            require(native == legacy, "Native formation/RNG mismatch stage=" +
                                                          std::to_string(stage + 1) +
                                                          " steps=" + std::to_string(steps));
                            formations += native.size();
                            require(owner->formations.tuning().beamReach <= owner->player.height(),
                                    "Difficulty beam reach exceeded edition cap");
                            report << (aof ? "aof" : "standard") << ',' << difficulty << ','
                                   << crowded << ',' << stage + 1 << ',' << steps << ','
                                   << native.size() << ",pass\n";
                        }
                    }
                }
        // Sustained motion from all edges/corners, including exterior starts. Bounce
        // owners must reenter; exit owners must retire instead of blocking a level.
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (int sprite : {116, 135, 273, 274, 275})
                    for (int mode : {1, 2})
                        for (int edge = 0; edge < 8; ++edge)
                            for (bool outside : {false, true})
                                for (int pan : {0, 1, 2}) {
                                    const int behavior = sprite == 116 ? 16 : 17;
                                    auto r = std::make_unique<Rig>(assets, aof, steps);
                                    r->player.worldX = fixed16(pan * r->player.width() / 2);
                                    r->player.worldY = fixed16(pan * r->player.height() / 2);
                                    auto &e = r->pool.at(78);
                                    e.behavior = behavior;
                                    e.drawing = 3;
                                    e.sprite = sprite;
                                    e.mode = mode;
                                    e.next = -1;
                                    e.x = fixed16(r->player.width());
                                    e.y = fixed16(r->player.height());
                                    e.vx = fixed16(7) + 123;
                                    e.vy = fixed16(9) + 57;
                                    e.ax = e.ay = 4096;
                                    e.podPlantChance() = 0;
                                    const auto f = r->frame();
                                    const int near = outside ? -12 : 2;
                                    if (edge == 0 || edge == 4 || edge == 6) {
                                        e.x = fixed16(f.minimumX + near);
                                        e.vx = -e.vx;
                                    }
                                    if (edge == 1 || edge == 5 || edge == 7)
                                        e.x = fixed16(f.maximumX - near);
                                    if (edge == 2 || edge == 4 || edge == 5) {
                                        e.y = fixed16(f.minimumY + near);
                                        e.vy = -e.vy;
                                    }
                                    if (edge == 3 || edge == 6 || edge == 7)
                                        e.y = fixed16(f.maximumY - near);
                                    bool returned = false;
                                    for (int tick = 0; tick < 256 && e.behavior; ++tick) {
                                        r->tick(78, tick);
                                        returned |= e.x >= fixed16(f.minimumX) &&
                                                    e.x <= fixed16(f.maximumX) &&
                                                    e.y >= fixed16(f.minimumY) &&
                                                    e.y <= fixed16(f.maximumY);
                                    }
                                    require(returned || !e.behavior,
                                            "Enemy remained stranded at world edge: sprite=" +
                                                std::to_string(sprite) +
                                                " mode=" + std::to_string(mode) +
                                                " edge=" + std::to_string(edge) +
                                                " rate=" + std::to_string(steps) +
                                                " pan=" + std::to_string(pan) +
                                                " exterior=" + std::to_string(outside) +
                                                " position=" + std::to_string(pixel(e.x)) + "," +
                                                std::to_string(pixel(e.y)));
                                    ++boundaries;
                                }
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (int direction = 0; direction < 4; ++direction)
                    for (int behavior : {7, 16, 17}) {
                        auto r = std::make_unique<Rig>(assets, aof, steps);
                        auto &e = r->pool.at(78);
                        e.behavior = behavior;
                        e.drawing = 3;
                        e.sprite = behavior == 17 ? (direction < 2 ? 135 : 275) : 64;
                        e.x = fixed16(r->player.width());
                        e.y = fixed16(r->player.height());
                        e.mode = 0;
                        e.podPlantChance() = 0;
                        if (direction < 2)
                            e.vy = fixed16(direction ? -9 : 9);
                        else
                            e.vx = fixed16(direction == 2 ? -9 : 9);
                        for (int tick = 0; tick < 400 && e.behavior; ++tick)
                            r->tick(78, tick);
                        require(!e.behavior, "Escaping projectile/split/flyer failed to retire");
                        ++retirements;
                    }
        require(families == std::set<unsigned>({0, 1, 2, 3, 4, 5}),
                "Campaign omitted an enemy family");
        // Every chain family/direction, both link orders, source-history wrap and
        // real beam-triggered promotion/scatter. The player is absent in this
        // fixture so contacts cannot obscure link/timing failures.
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (unsigned family : {0u, 1u, 3u, 4u, 5u})
                    for (unsigned direction = 0; direction < 4; ++direction)
                        for (int scatter : {0, 1, 2, 3, 4})
                            for (auto sizeDelay :
                                 {std::pair<int, int>{1, 2}, {6, 4}, {16, 3}, {32, 8}}) {
                                auto r = std::make_unique<Rig>(assets, aof, steps);
                                r->formations.prepareLevel(
                                    (aof ? aof::stages : original::stages)[49], 49, 2);
                                auto &tuning = r->formations.tuning();
                                tuning.segmentCount = sizeDelay.first;
                                tuning.historySpacing = sizeDelay.second;
                                tuning.splitTransform =
                                    scatter == 4 ? 0u : (255u | uint32_t(scatter) << 8);
                                Entity source;
                                source.x = fixed16(direction == 2   ? -12
                                                   : direction == 3 ? r->player.worldWidth() + 12
                                                                    : r->player.width());
                                source.y = fixed16(direction == 0   ? -12
                                                   : direction == 1 ? r->player.worldHeight() + 12
                                                                    : r->player.height());
                                source.commandOrHealth = family << 20 | direction;
                                r->formations.materialize(source);
                                for (int tick = 0; tick < 400; ++tick) {
                                    if (steps > 1)
                                        r->native.beginInterval();
                                    r->combat.beginNativeInterval();
                                    EntityCensus census;
                                    if (tick == 60 || tick == 90) {
                                        // Pick an actual chain body; beamDamage owns the complete
                                        // split+conversion sequence, not a hand-built substitute.
                                        for (size_t slot = 0; slot < 88; ++slot)
                                            if (r->pool.at(slot).behavior == 4) {
                                                r->pool.at(slot).commandOrHealth = 0xffffffffu;
                                                r->player.beamMode = 4;
                                                r->player.beamTarget = int(slot);
                                                r->player.beamHitConsumed = false;
                                                r->combat.beamDamage(slot, r->frame().minimumX);
                                                r->player.beamMode = 0;
                                                break;
                                            }
                                    }
                                    for (int sub = 0; sub < steps; ++sub)
                                        for (size_t slot = 0; slot < 88; ++slot) {
                                            auto &e = r->pool.at(slot);
                                            if (!e.behavior)
                                                continue;
                                            if (sub == 0) {
                                                if (CombatSystem::supports(e.behavior))
                                                    r->combat.update(slot, r->frame(tick), census);
                                            } else if (e.behavior == 4)
                                                r->native.follow(r->pool, e);
                                            else
                                                r->native.continueMove(r->pool, slot);
                                            if (steps > 1)
                                                r->native.record(r->pool, slot);
                                            if (e.behavior == 4) {
                                                require(e.previous >= 0 &&
                                                            e.previous < int(EntityPool::Capacity),
                                                        "Body lost its live predecessor");
                                                require(e.next < 0 ||
                                                            (e.next < int(EntityPool::Capacity) &&
                                                             r->pool.at(size_t(e.next)).previous ==
                                                                 int(slot)),
                                                        "Split left inconsistent successor link");
                                            }
                                            require(e.behavior != 3 || e.outsideTicks <= 250,
                                                    "Offscreen chain failed to retire");
                                        }
                                    ++chainTicks;
                                }
                            }
        // Persistent piercing shots and the companion have one damage opportunity
        // per 30ms event, irrespective of the number of real contact substeps.
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (int kind : {3, 4, 16, 17})
                    for (bool companion : {false, true}) {
                        auto r = std::make_unique<Rig>(assets, aof, steps);
                        auto &target = r->pool.at(78);
                        target.behavior = kind;
                        target.drawing = 3;
                        target.flags = 1;
                        target.commandOrHealth = 1000;
                        target.next = -1;
                        target.previous = -1;
                        target.x = fixed16(200);
                        target.y = fixed16(240);
                        target.sprite = 116;
                        auto &weapon = r->pool.at(companion ? 88 : 90);
                        weapon.behavior = companion ? 13 : 2;
                        weapon.drawing = companion ? 13 : 15;
                        weapon.sprite = 107;
                        weapon.x = target.x;
                        weapon.y = target.y;
                        r->player.sheepPower = 3;
                        for (int tick = 0; tick < 40; ++tick) {
                            r->combat.beginNativeInterval();
                            for (int sub = 0; sub < steps; ++sub) {
                                r->combat.nativeContacts(r->frame(tick));
                                require(target.commandOrHealth == uint32_t(999 - tick),
                                        "Native contact multiplied or lost damage");
                                ++damageChecks;
                            }
                        }
                    }
        // Independent attack oracle: inclusive probability threshold, reverse fire,
        // cardinal/aimed families, quadrant and axis ties. Decisions/random draws
        // must occur once per source event rather than once per motion fraction.
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (int type : {0, 1, 2, 3})
                    for (int chance : {-1, 0, 32, 255})
                        for (bool reverse : {false, true})
                            for (int dx : {-80, 0, 80})
                                for (int dy : {-120, 0, 120}) {
                                    auto r = std::make_unique<Rig>(assets, aof, steps);
                                    auto &head = r->pool.at(78);
                                    head.behavior = 3;
                                    head.drawing = 6;
                                    head.sprite = 124;
                                    head.next = -1;
                                    head.mode = 4;
                                    head.retargetCountdown() = 10000;
                                    head.x = fixed16(r->player.width());
                                    head.y = fixed16(r->player.height());
                                    head.attackParameters =
                                        signedBits(uint32_t(chance) * 65536u | uint32_t(type));
                                    head.flags = reverse ? 3u : 1u;
                                    r->pool.at(89).x =
                                        sub32(add32(head.x, fixed16(dx)), r->player.worldX);
                                    r->pool.at(89).y =
                                        sub32(add32(head.y, fixed16(dy)), r->player.worldY);
                                    GameRandom expected = r->random;
                                    for (int tick = 0; tick < 64; ++tick) {
                                        const bool fires = expected.below(256) <= chance;
                                        r->tick(78, tick);
                                        auto &shot = r->pool.at(1);
                                        require((shot.behavior == 7) == fires,
                                                "Enemy attack chance changed");
                                        require(r->random.state() == expected.state(),
                                                "Enemy attack RNG frequency changed");
                                        if (fires) {
                                            int vx = 0, vy = reverse ? -0x60000 : 0x60000;
                                            if (type == 2 || type == 3)
                                                vy = dy > 0 ? 0x60000 : -0x60000;
                                            if (type == 3 && std::abs(dx) >= std::abs(dy)) {
                                                vx = dx > 0 ? 0x90000 : -0x90000;
                                                vy = 0;
                                            }
                                            require(shot.vx == vx && shot.vy == vy &&
                                                        shot.x == head.x && shot.y == head.y,
                                                    "Enemy attack direction/origin changed");
                                            require(shot.commandOrHealth == 0xffffffffu &&
                                                        shot.flags == 1,
                                                    "Enemy projectile initialization changed");
                                            shot.behavior = 0;
                                        }
                                        ++attackChecks;
                                    }
                                }
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6})
                for (int chance : {0, 128, 256})
                    for (int x : {-9, 0, 16, 479, 480, 799, 800})
                        for (int y : {-1, 0, 16, 639, 640, 799, 800}) {
                            auto r = std::make_unique<Rig>(assets, aof, steps);
                            auto &flyer = r->pool.at(78);
                            flyer.behavior = 17;
                            flyer.drawing = 3;
                            flyer.sprite = 135;
                            flyer.x = fixed16(x);
                            flyer.y = fixed16(y);
                            flyer.podPlantChance() = chance;
                            GameRandom expected = r->random;
                            const bool plants = expected.below(256) < chance && x + 8 > 0 &&
                                                x + 8 < r->player.worldWidth() && y > 0 &&
                                                y < r->player.worldHeight();
                            r->tick(78);
                            int cells = 0;
                            for (auto cell : r->grid)
                                cells += cell != 0;
                            require(cells == int(plants) && r->random.state() == expected.state(),
                                    "Flyer planting probability/bounds/RNG changed");
                            if (plants)
                                require(r->grid[size_t((x + 8) / 16 +
                                                       (y / 16) * r->player.columns())] ==
                                            0x01000001u,
                                        "Flyer planted wrong grid cell");
                            ++podChecks;
                        }
        std::cout << formations
                  << " native formation comparisons across all 50 stages/both editions; "
                  << boundaries << " edge recoveries; " << retirements
                  << " directional retirements; " << chainTicks << " chain source intervals; "
                  << damageChecks << " contact-budget checks; " << attackChecks
                  << " attack decisions; " << podChecks << " planting boundaries PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
