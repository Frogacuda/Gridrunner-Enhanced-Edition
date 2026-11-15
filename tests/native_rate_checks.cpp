#include "grpp/clock.hpp"
#include "grpp/game.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
namespace grpp {
struct NativeRateTests {
    static void isolate(Game &g) {
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            g.pool_.at(i).behavior = 0;
        auto &p = g.pool_.at(EntityPool::Player);
        p.behavior = 1;
        p.drawing = 0;
        p.flags = 1;
        p.x = fixed16(120);
        p.y = fixed16(240);
        g.player_.transitionTicks = 0;
        g.player_.worldX = p.x;
        g.player_.worldY = p.y;
        g.player_.shotCountdown = 100000;
        g.grid_ = {};
        g.worldState_ = {};
        g.phase_ = GamePhase::Playing;
        g.beginPending_ = false;
        if (g.native_)
            g.native_->reset();
        g.nativePhase_ = 0;
    }
    static Entity &entity(Game &g, size_t slot) { return g.pool_.at(slot); }
    static PlayerState &player(Game &g) { return g.player_; }
    static WorldGrid &grid(Game &g) { return g.grid_; }
    static WorldState &world(Game &g) { return g.worldState_; }
    static void clearStage(Game &g) {
        g.phase_ = GamePhase::StageClear;
        g.clearTicks_ = 100;
        g.player_.bombActive = 0;
    }
    static uint64_t state(const Game &g) {
        uint64_t h = 1469598103934665603ull;
        auto hash = [&](const void *p, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                h ^= static_cast<const unsigned char *>(p)[i];
                h *= 1099511628211ull;
            }
        };
        hash(&g.pool_, sizeof g.pool_);
        hash(&g.player_, sizeof g.player_);
        hash(&g.combatState_, sizeof g.combatState_);
        hash(&g.worldState_, sizeof g.worldState_);
        hash(&g.grid_, sizeof g.grid_);
        hash(&g.frame_, sizeof g.frame_);
        auto r = g.random_.state(), e = g.effects_.state();
        hash(&r, sizeof r);
        hash(&e, sizeof e);
        for (int s : g.sounds_)
            hash(&s, sizeof s);
        return h;
    }
};
} // namespace grpp
uint64_t raster(const Canvas &c, const Assets &a) {
    auto r = std::make_unique<Raster565>();
    r->render(c, a);
    uint64_t h = 0;
    for (auto p : r->pixels())
        h = h * 31 + p;
    return h;
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: native_rate_checks ASSETS");
        Assets assets;
        assets.load(argv[1]);
        // Exact signed fixed-point transport, including wrap, negative remainders,
        // multiple moves in one source dispatch, and slot conversion/teleport.
        auto pool = std::make_unique<EntityPool>();
        auto motion = std::make_unique<NativeMotion>();
        pool->nativeMotion = motion.get();
        auto &e = pool->at(1);
        e.behavior = 7;
        e.drawing = 10;
        for (int delta : {INT32_MIN, INT32_MAX, -100001, -65536, -5, -1, 0, 1, 5, 65536, 100001})
            for (int initial : {INT32_MIN, INT32_MAX, -1234567, 0, 1234567}) {
                motion->reset();
                motion->beginInterval();
                e.x = initial;
                e.y = initial;
                e.depth = initial;
                pool->move(e, delta, sub32(0, delta), delta);
                pool->move(e, delta, sub32(0, delta), delta);
                motion->continueMove(*pool, 1);
                motion->continueMove(*pool, 1);
                require(e.x == add32(initial, add32(delta, delta)) &&
                            e.y == sub32(initial, add32(delta, delta)) && e.depth == e.x,
                        "Native transport lost source distance or overflow semantics");
            }
        motion->beginInterval();
        pool->move(e, 65536, 0);
        e.behavior = 12;
        const int retired = e.x;
        motion->continueMove(*pool, 1);
        require(e.x == retired, "Converted slot inherited old velocity");
        e.behavior = 7;
        motion->beginInterval();
        pool->move(e, 1, 0);
        e.x = 123;
        motion->continueMove(*pool, 1);
        require(e.x == 123, "Teleport retained fractional residue");
        e.behavior = 0;
        require(pool->firstFree(1, 1) == 1, "Slot reuse fixture failed");
        e.behavior = 7;
        e.x = 0;
        motion->beginInterval();
        pool->move(e, 3, 0);
        motion->continueMove(*pool, 1);
        motion->continueMove(*pool, 1);
        require(e.x == 3, "Reused slot retained motion state");
        motion->reset();
        e.x = 0;
        e.y = fixed16(240);
        PlayerState follow;
        int expectedX = 0;
        for (int tick = 0; tick < 300; ++tick) {
            int target = tick % 31 < 15 ? 239 : 120;
            expectedX = add32(expectedX, asr32(sub32(fixed16(target), expectedX), 3));
            for (int k = 0; k < 3; ++k)
                motion->player(e, follow, {target, 240, 0, 0});
            require(std::abs(e.x - expectedX) <= 8 && e.y == fixed16(240),
                    "Time-based player following changed the source gain");
        }
        // Histories store actual past substep states, not a future frame blend.
        motion->reset();
        e.behavior = 3;
        e.drawing = 6;
        e.x = 0;
        e.y = 0;
        e.depth = 4096;
        auto &body = pool->at(2);
        body.behavior = 4;
        body.previous = 1;
        body.historySpacing = 4;
        std::vector<int> past;
        for (int i = 0; i < 80; ++i) {
            if (i % 3 == 0) {
                motion->beginInterval();
                pool->move(e, 65537, 0);
            } else
                motion->continueMove(*pool, 1);
            motion->record(*pool, 1);
            past.push_back(e.x);
            bool followed = motion->follow(*pool, body);
            motion->record(*pool, 2);
            if (i >= 9)
                require(followed && body.x == past[size_t(i - 9)],
                        "Chain spacing changed its elapsed-time delay");
        }
        motion->reset();
        past.clear();
        e.x = 0;
        auto &earlierBody = pool->at(0);
        earlierBody.behavior = 4;
        earlierBody.previous = 1;
        earlierBody.historySpacing = 4;
        for (int i = 0; i < 80; ++i) {
            if (i % 3 == 0)
                motion->beginInterval();
            bool followed = motion->follow(*pool, earlierBody);
            motion->record(*pool, 0);
            if (i % 3 == 0)
                pool->move(e, 65537, 0);
            else
                motion->continueMove(*pool, 1);
            motion->record(*pool, 1);
            past.push_back(e.x);
            if (i >= 12)
                require(followed && earlierBody.x == past[size_t(i - 12)],
                        "Ascending slot order changed chain delay");
        }
        auto legacy = std::make_unique<Game>(assets), native = std::make_unique<Game>(assets);
        native->setNativeRate(true);
        legacy->start();
        native->start();
        NativeRateTests::isolate(*legacy);
        NativeRateTests::isolate(*native);
        NativeRateTests::player(*legacy).shotCountdown =
            NativeRateTests::player(*native).shotCountdown = 0;
        int legacyShots = 0, nativeShots = 0;
        for (int tick = 0; tick < 300; ++tick) {
            LogicalInput in{120, 240, tick < 10 ? 32u : 0u, 0};
            legacy->step(in);
            for (int s : legacy->sounds())
                legacyShots += s == 6;
            for (int k = 0; k < 3; ++k) {
                native->step(in);
                for (int s : native->sounds())
                    nativeShots += s == 6;
                for (size_t slot = 90; slot < 100; ++slot) {
                    const auto &shot = native->entities().at(slot);
                    if (shot.behavior == 2 && shot.drawing == 2)
                        for (const auto &point : shot.history)
                            if (point.tag > 0)
                                require(point.y >= shot.y,
                                        "Particle was drawn ahead of the actual shot");
                }
            }
            require(legacy->frame() == native->frame() &&
                        legacy->player().bombTicks == native->player().bombTicks &&
                        legacy->player().bombActive == native->player().bombActive,
                    "Source timer duration changed");
            require(legacy->randomState() == native->randomState(),
                    "Extra motion/render updates consumed gameplay randomness");
            require(legacy->player().scoreClock == native->player().scoreClock &&
                        legacy->player().shotCountdown == native->player().shotCountdown,
                    "Source event clocks changed");
            for (size_t slot = 90; slot < 100; ++slot) {
                const auto &a = legacy->entities().at(slot);
                const auto &b = native->entities().at(slot);
                if (a.behavior == 2 && a.drawing == 2)
                    require(std::memcmp(a.history.data(), b.history.data(), sizeof(a.history)) == 0,
                            "Native particle spacing/count/aging changed at a source boundary");
                if (a.behavior != b.behavior || a.x != b.x || a.y != b.y || a.vy != b.vy) {
                    std::cerr << "tick=" << tick << " slot=" << slot << " legacy=" << a.behavior
                              << "," << a.x << "," << a.y << "," << a.vy << " native=" << b.behavior
                              << "," << b.x << "," << b.y << "," << b.vy << "\n";
                    throw std::runtime_error(
                        "Projectile equal-time displacement/lifetime diverged in empty arena");
                }
            }
        }
        require(legacyShots == 100 && nativeShots == 100, "Autofire changed its 90 ms period");
        // Equal-time fixtures cover every free-moving active owner. Drawing is a
        // nonmutating sprite family here so this isolates integration semantics.
        for (int behavior : {3, 5, 6, 7, 8, 11, 13, 15, 16, 17})
            for (int variant = 0; variant < ((behavior == 16 || behavior == 17) ? 3 : 1);
                 ++variant) {
                legacy->start();
                native->start();
                NativeRateTests::isolate(*legacy);
                NativeRateTests::isolate(*native);
                size_t slot = behavior == 3    ? 78
                              : behavior == 8  ? 0
                              : behavior == 11 ? 12
                              : behavior == 13 ? 88
                                               : 1;
                for (Game *g : {legacy.get(), native.get()}) {
                    auto &a = NativeRateTests::entity(*g, slot);
                    a = {};
                    a.behavior = behavior;
                    a.drawing = 10;
                    a.sprite = a.animationBase = 64;
                    a.x = fixed16(300);
                    a.y = fixed16(400);
                    a.vx = 65537;
                    a.vy = 32771;
                    a.ax = 32;
                    a.ay = -16;
                    a.depth = 8192;
                    a.speed = 32;
                    a.extent = 1000000;
                    a.phase = 32;
                    a.mode = behavior == 11 ? 1 : behavior == 3 ? 4 : variant;
                    if (behavior == 3) {
                        a.vx = a.vy = 256;
                        a.heading = 256;
                        a.attackParameters = -65536;
                        a.life = 1000;
                    }
                }
                for (int interval = 0; interval < 10; ++interval) {
                    legacy->step({120, 240, 0, 0});
                    for (int k = 0; k < 3; ++k)
                        native->step({120, 240, 0, 0});
                    const auto &a = legacy->entities().at(slot);
                    const auto &b = native->entities().at(slot);
                    if (a.behavior != b.behavior || a.x != b.x || a.y != b.y ||
                        a.depth != b.depth || a.vx != b.vx || a.vy != b.vy || a.scale != b.scale) {
                        std::cerr << "Owner " << behavior << " interval " << interval << '\n';
                        throw std::runtime_error("Active-owner elapsed-time motion changed");
                    }
                    require(legacy->randomState() == native->randomState(),
                            "Motion owner changed random decision frequency");
                }
            }
        // One real input sample between source events changes the collision body.
        NativeRateTests::isolate(*native);
        auto &enemy = NativeRateTests::entity(*native, 1);
        enemy.behavior = 16;
        enemy.drawing = 3;
        enemy.sprite = 274;
        enemy.flags = 1;
        enemy.mode = 0;
        enemy.x = fixed16(257);
        enemy.y = fixed16(480);
        enemy.vx = enemy.vy = 0;
        native->step({120, 240, 0, 0});
        require(native->player().transitionTicks == 0, "Early-contact fixture starts in contact");
        int oldX = native->entities().at(89).x;
        native->step({239, 240, 0, 0});
        require(native->entities().at(89).x > oldX && native->player().transitionTicks == 1,
                "Fresh substep input failed to move and collide the actual ship");
        // Piercing projectiles must not deal triple damage while overlapping.
        NativeRateTests::isolate(*native);
        NativeRateTests::player(*native).sheepPower = 3;
        auto &target = NativeRateTests::entity(*native, 1);
        target.behavior = 16;
        target.drawing = 3;
        target.sprite = 274;
        target.flags = 1;
        target.mode = 0;
        target.x = fixed16(400);
        target.y = fixed16(400);
        target.vx = target.vy = 0;
        target.commandOrHealth = 1000;
        auto &shot = NativeRateTests::entity(*native, 90);
        shot.behavior = 2;
        shot.drawing = 15;
        shot.sprite = 107;
        shot.x = target.x;
        shot.y = fixed16(380);
        shot.vx = shot.ax = shot.ay = 0;
        shot.vy = fixed16(18);
        native->step({120, 240, 0, 0});
        require(target.commandOrHealth == 1000, "Projectile fixture starts in contact");
        native->step({120, 240, 0, 0});
        require(target.commandOrHealth == 999, "Substep projectile contact was missed");
        native->step({120, 240, 0, 0});
        require(target.commandOrHealth == 999, "Persistent contact multiplied damage");
        NativeRateTests::isolate(*native);
        auto &companionTarget = NativeRateTests::entity(*native, 1);
        companionTarget = {};
        companionTarget.behavior = 16;
        companionTarget.drawing = 3;
        companionTarget.sprite = 274;
        companionTarget.flags = 1;
        companionTarget.x = fixed16(300);
        companionTarget.y = fixed16(400);
        companionTarget.commandOrHealth = 1000;
        auto &companion = NativeRateTests::entity(*native, 88);
        companion = {};
        companion.behavior = 13;
        companion.drawing = 13;
        companion.x = companionTarget.x;
        companion.y = companionTarget.y;
        for (int i = 0; i < 3; ++i)
            native->step({120, 240, 0, 0});
        require(companionTarget.commandOrHealth == 999,
                "Companion damage multiplied across motion updates");
        native->step({120, 240, 0, 0});
        require(companionTarget.commandOrHealth == 998, "Companion damage budget failed to reset");
        // World hazards also test the fresh ship position without growing faster.
        NativeRateTests::isolate(*native);
        NativeRateTests::grid(*native)[16 + 30 * 30] = 0x01000001;
        native->step({120, 240, 0, 0});
        require(native->player().transitionTicks == 0, "Hazard fixture starts in contact");
        auto cell = NativeRateTests::grid(*native)[16 + 30 * 30];
        native->step({239, 240, 0, 0});
        require(native->player().transitionTicks == 1 &&
                    NativeRateTests::grid(*native)[16 + 30 * 30] == cell,
                "Substep hazard missed contact or advanced growth");
        // The final source laser interval remains dangerous after its countdown
        // reaches zero; extra checks must not shorten it by two substeps.
        NativeRateTests::isolate(*native);
        auto &w = NativeRateTests::world(*native);
        w.laserTicks = 1;
        w.laserX = 251;
        w.laserY = 400;
        native->step({120, 240, 0, 0});
        require(w.laserTicks == 0 && native->player().transitionTicks == 0,
                "Laser end-interval fixture failed");
        native->step({239, 240, 0, 0});
        require(native->player().transitionTicks == 1,
                "Final laser interval ended two motion steps early");
        NativeRateTests::isolate(*native);
        NativeRateTests::player(*native).sheepPower = 1;
        auto &pickup = NativeRateTests::entity(*native, 110);
        pickup.behavior = 15;
        pickup.drawing = 3;
        pickup.sprite = pickup.animationBase = 104;
        pickup.x = fixed16(257);
        pickup.y = fixed16(480);
        pickup.vy = 0;
        native->step({120, 240, 0, 0});
        int score = native->combatState().score;
        native->step({239, 240, 0, 0});
        auto pickupRng = native->randomState();
        require(native->player().sheepPower == 2 && native->combatState().score == score + 500,
                "Substep pickup used wrong multiplier or missed contact");
        native->step({239, 240, 0, 0});
        require(native->player().sheepPower == 2 && native->combatState().score == score + 500 &&
                    native->randomState() == pickupRng,
                "Pickup awarded or drew randomness twice");
        NativeRateTests::isolate(*native);
        NativeRateTests::player(*native).bombActive = 0;
        native->step({120, 240, 0, 0});
        native->step({120, 240, 32, 32});
        native->step({120, 240, 0, 0});
        native->step({120, 240, 0, 0});
        require(native->player().bombActive != 0, "Button tap between source events was lost");
        NativeRateTests::isolate(*native);
        NativeRateTests::player(*native).bombActive = 0;
        native->step({120, 240, 0, 0});
        native->step({120, 240, 32, 32});
        native->cancelPendingInput();
        native->step({120, 240, 0, 0});
        native->step({120, 240, 0, 0});
        require(native->player().bombActive == 0, "Focus cancellation retained a queued action");
        // Exercised live draw families must neither mutate live state nor RNG.
        auto live = std::make_unique<Game>(assets);
        live->setNativeRate(true);
        live->start();
        for (int i = 0; i < 1200; ++i) {
            live->step({120, 270, i % 600 < 10 ? 32u : 0u, 0});
            if (i % 17 == 0) {
                auto before = NativeRateTests::state(*live);
                live->renderNative();
                auto image = raster(live->canvas(), assets);
                for (int p = 0; p < 3; ++p) {
                    live->renderNative();
                    require(NativeRateTests::state(*live) == before &&
                                raster(live->canvas(), assets) == image,
                            "Repeated native rendering changed state, RNG or cached pixels");
                }
            }
        }
        // Interlude expires at the original 100*30 ms boundary, not 100*10 ms.
        NativeRateTests::isolate(*native);
        NativeRateTests::clearStage(*native);
        for (int i = 0; i < 297; ++i)
            native->step({120, 240, 0, 0});
        require(native->stage() == 0, "Stage interlude expired early");
        for (int i = 0; i < 3; ++i)
            native->step({120, 240, 0, 0});
        require(native->stage() == 1, "Stage interlude duration changed");
        for (uint64_t hz : {30, 60, 90, 100, 120, 144, 165, 240}) {
            FixedCadence clock(0, 10'000'000);
            int count = 0;
            for (uint64_t i = 0; i <= hz * 30; ++i)
                count += clock.pending(i * 1'000'000'000 / hz);
            require(count == 3001, "Native scheduling depends on display rate");
        }
        std::cout << "Native 100 Hz: signed transport, source RNG/timers/autofire/projectile "
                     "equivalence, real early contacts, piercing budget, hazards, history delays, "
                     "render purity, stage timing and eight host rates pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
