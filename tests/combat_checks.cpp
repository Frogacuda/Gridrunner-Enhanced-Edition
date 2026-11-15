#include "combat_observation.hpp"
#include "original_levels.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace grpp;
struct NativeCombatBackend {
    EntityPool pool;
    PlayerState player;
    GameRandom random{0x1982};
    FormationSystem formations{pool, player, random};
    StageSpec stage = original::stages[0];
    FormationTuning &tuning = formations.tuning();
    CombatState state;
    WorldGrid grid{};
    Assets assets;
    std::vector<int> sounds;
    EntityCensus census;
    CombatSystem combat{pool, player, random, formations, state, grid, assets, sounds};
    int scatters = 0, promotions = 0, kills = 0, contacts = 0, drops = 0, detachedFollows = 0;
    bool runPlayer = false;
    int projectileContacts = 0;
    int collectedPickups = 0, companionHits = 0, companionShields = 0, shieldClockFrames = 0;
    explicit NativeCombatBackend(const char *path) {
        assets.load(path);
        stage.settings.push_back({LevelSetting::SegmentCount, 6});
    }
    void begin(int mode) {
        pool = {};
        player = {};
        state = {};
        grid = {};
        census = {};
        sounds.clear();
        random = GameRandom{0x1982};
        runPlayer = false;
        auto &p = pool.at(89);
        p.x = fixed16(120);
        p.y = fixed16(240);
        p.behavior = 14;
        player.sheepPower = 3;
        state.score = 1000;
        state.stageScore = 500;
        state.killsUntilDrop = 0;
        state.difficulty = 2;
        formations.prepareLevel(stage, 0, 0);
        for (size_t slot = 0; slot < 116; ++slot)
            for (auto &point : pool.at(slot).history)
                point.color = 0xcafe0000u + uint32_t(slot);
        auto &source = pool.at(12);
        source.x = fixed16(120);
        source.y = fixed16(160);
        source.commandOrHealth = 2;
        formations.materialize(source);
        tuning.splitTransform = mode < 0 ? 0 : uint32_t(mode << 8) | 255u;
    }
    void word(int slot, int field, int value) {
        auto &e = pool.at(size_t(slot));
        switch (field) {
        case 0:
            e.x = value;
            break;
        case 1:
            e.y = value;
            break;
        case 3:
            e.vx = value;
            break;
        case 4:
            e.vy = value;
            break;
        case 11:
            e.depthAcceleration = value;
            break;
        case 12:
            e.outsideTicks = value;
            break;
        case 13:
            e.next = value;
            break;
        case 14:
            e.previous = value;
            break;
        case 16:
            e.historySpacing = value;
            break;
        case 17:
            e.extent = value;
            break;
        case 18:
            e.behavior = value;
            break;
        case 19:
            e.drawing = value;
            break;
        case 20:
            e.commandOrHealth = uint32_t(value);
            break;
        case 21:
            e.hitFlash = value;
            break;
        case 22:
            e.life = value;
            break;
        case 36:
            e.attackParameters = value;
            break;
        case 37:
            e.mode = value;
            break;
        case 40:
            e.heading = value;
            break;
        case 42:
            e.flags = uint32_t(value);
            break;
        case 556:
            e.scale = value;
            break;
        case 558:
            e.effectMode = value;
            break;
        default:
            throw std::runtime_error("Unknown controlled input field");
        }
    }
    void split(int slot) {
        sounds.clear();
        auto &e = pool.at(size_t(slot));
        const int successor = e.next;
        combat.splitChain(size_t(slot), successor, pool.at(78).mode);
        scatters += e.behavior == 16 || e.behavior == 5;
        if (successor >= 0)
            promotions += pool.at(size_t(successor)).behavior == 3;
    }
    void selectBeam(int slot) {
        player.beamMode = 4;
        player.beamTarget = slot;
        player.beamHitConsumed = false;
    }
    void newBeam() { player.beamHitConsumed = false; }
    void beamGate(int condition) {
        if (condition == 0)
            player.beamMode = 3;
        if (condition == 1)
            player.beamTarget = 77;
        if (condition == 2)
            player.beamHitConsumed = true;
        if (condition == 3)
            pool.at(78).commandOrHealth = 0;
    }
    void dropInput(int countdown, int pending, int full) {
        state.killsUntilDrop = countdown;
        state.pendingRestorePower = pending;
        if (full)
            for (size_t i = 110; i < 115; ++i)
                pool.at(i).behavior = 15;
    }
    void drop() {
        sounds.clear();
        const int old = pool.at(110).behavior;
        combat.considerDrop(96);
        drops += old == 0 && pool.at(110).behavior == 15;
    }
    void contactInput(int kind, int distance) {
        auto &e = pool.at(kind == 3 ? 78 : 70);
        e.behavior = kind;
        e.x = fixed16(120 + distance);
        e.y = fixed16(240);
        e.vx = e.vy = 0;
        if (kind == 17) {
            e.sprite = 135;
            e.flags = 5;
            e.y = fixed16(240 + distance);
        }
        if (kind == 7)
            e.sprite = 64;
        pool.at(89).behavior = 1;
    }
    void effect(int which) {
        sounds.clear();
        auto &e = pool.at(78);
        if (which == 0)
            combat.convertHead(78, 9);
        if (which == 1)
            combat.convertFragment(70, 3);
        if (which == 2) {
            e.behavior = 6;
            e.depth = 64000;
            e.extent = 64000;
            e.speed = 512;
            e.heading = 1;
        }
        if (which == 3) {
            e.behavior = 9;
            e.scale = 0;
        }
        if (which == 4) {
            e.behavior = 12;
            e.scale = 0;
            e.life = 4096;
        }
        if (which == 5) {
            pool.at(115).behavior = 18;
            pool.at(115).heading = 0;
        }
    }
    void firing() {
        runPlayer = true;
        auto &p = pool.at(89);
        p.behavior = 1;
        p.sprite = 358;
        p.flags = 1;
        for (auto &point : p.history) {
            point.x = p.x;
            point.y = p.y;
        }
    }
    void shotInput(int kind, int sprite, int power, int health, int pod) {
        player.sheepPower = power;
        pool.at(89).behavior = 1;
        updatePlayer(pool, player, {120, 240, 0, 0}, sounds);
        pool.at(89).behavior = 14;
        for (size_t slot = 0; slot < 89; ++slot)
            pool.at(slot).behavior = 0;
        for (size_t slot = 100; slot < 110; ++slot)
            pool.at(slot).behavior = 0;
        const size_t target = kind == 4 ? 70 : 78;
        if (kind) {
            auto &actor = pool.at(target);
            actor.behavior = kind;
            actor.flags = 1;
            actor.drawing = 3;
            actor.x = fixed16(120);
            actor.y = fixed16(160);
            actor.vx = actor.vy = 0;
            actor.sprite = kind == 7 ? 64 : kind == 17 ? 135 : 116;
            actor.commandOrHealth = uint32_t(health);
            actor.attackParameters = -65536;
        }
        auto &shot = pool.at(90);
        shot.x = fixed16(120);
        shot.y = fixed16(160);
        shot.vx = shot.vy = shot.ax = shot.ay = 0;
        shot.sprite = sprite;
        shot.animation = -1;
        shot.drawing = sprite == 7 ? 2 : 15;
        if (pod) {
            state.difficulty = pod - 1;
            for (size_t x = 6; x <= 10; ++x)
                grid[x + 10 * 30] = 0x01000001;
            if (pod == 3)
                grid[8 + 10 * 30] = 0x03000001;
        }
        sounds.clear();
    }
    void pickupInput(int power, int transition, int restore, int flags) {
        state.killsUntilDrop = 0;
        state.pendingRestorePower = restore ? 4 : 0;
        combat.considerDrop(16);
        for (size_t slot = 0; slot < 89; ++slot)
            pool.at(slot).behavior = 0;
        auto &p = pool.at(89);
        p.behavior = 1;
        p.flags = uint32_t(flags);
        player.sheepPower = power;
        player.transitionTicks = transition;
        player.reverseTransition = transition < 0;
        auto &pickup = pool.at(110);
        pickup.x = p.x;
        pickup.y = sub32(p.y, pickup.vy);
        sounds.clear();
    }
    void companionInput(int which) {
        for (size_t slot = 0; slot < 89; ++slot)
            pool.at(slot).behavior = 0;
        formations.createCompanion();
        auto &c = pool.at(88);
        if (which == 0)
            return;
        c.scale = 0;
        if (which == 1)
            return;
        c.x = fixed16(120);
        c.y = fixed16(160);
        c.ax = 0;
        if (which == 9) {
            c.life = 3;
            firing();
            return;
        }
        if (which == 8) {
            player.sheepPower = 1;
            pool.at(89).behavior = 1;
            updatePlayer(pool, player, {120, 240, 0, 0}, sounds);
            pool.at(89).behavior = 14;
            auto &shot = pool.at(90);
            shot.x = c.x;
            shot.y = c.y;
            shot.vx = shot.vy = shot.ax = shot.ay = 0;
            return;
        }
        auto &victim = pool.at(which == 7 ? 1 : 78);
        victim.behavior = which == 7 ? 7 : 3;
        victim.x = c.x;
        victim.y = c.y;
        victim.vx = victim.vy = 0;
        victim.sprite = which == 7 ? 64 : 124;
        victim.flags = 1;
        victim.drawing = which == 7 ? 10 : 6;
        victim.animation = -1;
        victim.commandOrHealth = 0xffffffffu;
        victim.attackParameters = -65536;
        victim.next = -1;
        if (which == 3) {
            c.life = 2;
            victim.x = fixed16(400);
        }
        if (which == 4)
            c.companionHealth = -1;
        if (which == 5) {
            c.life = 2;
            victim.x = fixed16(136);
            victim.y = fixed16(176);
        }
        if (which == 6) {
            victim.behavior = 4;
            victim.previous = 0;
            victim.historySpacing = 4;
            pool.at(0).history[60].x = c.x;
            pool.at(0).history[60].y = c.y;
        }
    }
    void tick(int tick) {
        sounds.clear();
        census = {};
        const ChainFrame f{16, 224, 16, 304, float(tick) * .1f, tick};
        const int before = player.transitionTicks;
        for (size_t slot = 0; slot < 116; ++slot)
            if (CombatSystem::supports(pool.at(slot).behavior) ||
                (runPlayer && slot == 89 && pool.at(slot).behavior == 1)) {
                const int old = pool.at(slot).behavior;
                const int oldShield = pool.at(slot).life, oldScore = state.score;
                pool.animate(pool.at(slot), tick);
                if (runPlayer && slot == 89 && old == 1) {
                    shieldClockFrames += pool.at(88).life > 0;
                    updatePlayer(pool, player, {120, 240, 0, 0}, sounds);
                } else
                    combat.update(slot, f, census);
                kills += old != 5 && pool.at(slot).behavior == 5;
                detachedFollows +=
                    old == 4 && pool.at(slot).previous == -1 && pool.at(slot).x == 44;
                projectileContacts += old == 2 && state.score != oldScore;
                collectedPickups += old == 15 && pool.at(slot).behavior == 5;
                companionHits += old == 13 && state.score != oldScore;
                companionShields += old == 13 && oldShield < 1 && pool.at(slot).life == 200;
            }
        contacts += before == 0 && player.transitionTicks == 1;
    }
    combat_check::Observation observe() const {
        uint64_t gridHash = 14695981039346656037ull, soundHash = gridHash;
        // The golden executable has a 30x40 grid; expanded AOF cells have separate coverage.
        for (size_t i = 0; i < 30 * 40; ++i)
            gridHash = formation_check::append(gridHash, grid[i]);
        for (int s : sounds)
            soundHash = formation_check::append(soundHash, uint32_t(s));
        uint32_t shotPhase;
        std::memcpy(&shotPhase, &state.shotColorPhase, 4);
        return {combat_check::poolDigest(pool),
                gridHash,
                soundHash,
                {random.state(),
                 uint32_t(state.score),
                 uint32_t(state.stageScore),
                 uint32_t(state.killsUntilDrop),
                 uint32_t(state.pendingRestorePower),
                 uint32_t(state.restorePower),
                 uint32_t(state.difficulty),
                 uint32_t(state.frameFlashTicks),
                 uint32_t(state.travellingAtDeath),
                 uint32_t(player.transitionTicks),
                 uint32_t(player.sheepPower),
                 uint32_t(player.beamTarget),
                 uint32_t(player.beamMode),
                 uint32_t(player.beamHitConsumed),
                 uint32_t(census.activeEnemies),
                 uint32_t(census.nearestHead),
                 uint32_t(census.nearestX),
                 uint32_t(census.nearestY),
                 shotPhase,
                 uint32_t(tuning.allowance),
                 uint32_t(player.scoreStep),
                 uint32_t(formations.baselineAdvance()),
                 uint32_t(player.scoreClock)}};
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: grpp_combat_checks ASSETS ORIGINAL_COMBAT_FIXTURE");
        std::ifstream in(argv[2]);
        if (!in)
            throw std::runtime_error("Missing original combat fixture");
        NativeCombatBackend b(argv[1]);
        size_t count = 0;
        combat_check::schedule(b, [&](const char *kind, int scenario, int tick,
                                      const combat_check::Observation &actual) {
            std::string name;
            int s, t;
            combat_check::Observation expected;
            in >> std::dec >> name >> s >> t >> std::hex >> expected.pool >> expected.grid >>
                expected.sounds;
            for (auto &w : expected.state)
                in >> w;
            if (!in || name != kind || s != scenario || t != tick)
                throw std::runtime_error("Combat fixture schedule mismatch");
            if (!(expected == actual)) {
                std::cerr << kind << " scenario=" << scenario << " tick=" << tick << "\nexpected "
                          << expected << "\nactual   " << actual << '\n';
                throw std::runtime_error("Combat state differs from original execution");
            }
            ++count;
        });
        std::string marker;
        size_t expected;
        in >> marker >> std::dec >> expected;
        if (!in || marker != "complete" || expected != count)
            throw std::runtime_error("Incomplete original combat evidence");
        if (!b.scatters || !b.promotions || !b.kills || !b.contacts || !b.drops ||
            !b.detachedFollows || !b.projectileContacts)
            throw std::runtime_error("Combat comparison missed a declared event family");
        if (!b.collectedPickups || !b.companionHits || !b.companionShields || !b.shieldClockFrames)
            throw std::runtime_error("Reward/companion traversal missed a declared event family");
        std::cout << "PASS: " << count << " original combat/split/conversion/reward snapshots\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
