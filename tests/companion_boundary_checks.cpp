#include "grpp/combat.hpp"
#include "grpp/native_motion.hpp"
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
struct Rig {
    EntityPool pool;
    PlayerState player;
    GameRandom random{1};
    FormationSystem formations{pool, player, random};
    CombatState state;
    WorldGrid grid{};
    std::vector<int> sounds;
    CombatSystem combat;
    NativeMotion motion;
    int steps, frame = 0;
    Rig(const Assets &assets, bool aof, int rate)
        : combat(pool, player, random, formations, state, grid, assets, sounds),
          motion(rate == 1 ? 6 : rate), steps(rate) {
        player.arse = aof;
        player.transitionTicks = 1;
        auto &target = pool.at(EntityPool::Player);
        target.behavior = 14;
        target.x = fixed16(player.width());
        target.y = fixed16(player.height());
        auto &head = entity();
        head.behavior = 13;
        head.drawing = 13;
        head.scale = 0;
        head.companionHealth = head.companionMaximumHealth = 1000;
        head.x = fixed16(player.width());
        head.y = fixed16(player.height());
        if (steps > 1)
            pool.nativeMotion = &motion;
    }
    Entity &entity() { return pool.at(EntityPool::Companion); }
    void tick() {
        if (steps > 1)
            motion.beginInterval();
        combat.beginNativeInterval();
        EntityCensus census;
        combat.update(EntityPool::Companion,
                      {16, player.width() - 16, 16, player.height() - 16, .1f, frame++}, census);
        for (int n = 1; n < steps; ++n)
            motion.continueMove(pool, EntityPool::Companion);
    }
    bool inside() {
        const auto &e = entity();
        return e.x >= 0 && pixel(e.x) <= player.worldWidth() && e.y >= 0 &&
               pixel(e.y) <= player.worldHeight();
    }
};
int main(int argc, char **argv) {
    try {
        check(argc == 2, "ASSETS required");
        Assets assets;
        assets.load(argv[1]);
        int comparisons = 0, recoveries = 0;
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (int acceleration : {0, 0xc000})
                    for (int edge = 0; edge < 8; ++edge) {
                        auto legacy = std::make_unique<Rig>(assets, aof, 1),
                             native = std::make_unique<Rig>(assets, aof, steps);
                        auto &e = legacy->entity();
                        const int near = fixed16(2) + 32768, speed = fixed16(steps * 2) + 123;
                        e.ax = e.ay = acceleration;
                        const int right = fixed16(legacy->player.worldWidth()) - near,
                                  bottom = fixed16(legacy->player.worldHeight()) - near;
                        if (edge == 0 || edge == 4 || edge == 6) {
                            e.x = near;
                            e.vx = -speed;
                        }
                        if (edge == 1 || edge == 5 || edge == 7) {
                            e.x = right;
                            e.vx = speed;
                        }
                        if (edge == 2 || edge == 4 || edge == 5) {
                            e.y = near;
                            e.vy = -speed;
                        }
                        if (edge == 3 || edge == 6 || edge == 7) {
                            e.y = bottom;
                            e.vy = speed;
                        }
                        native->entity() = e;
                        for (int frame = 0; frame < 200; ++frame) {
                            legacy->tick();
                            native->tick();
                            const auto &a = legacy->entity();
                            const auto &b = native->entity();
                            check(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy,
                                  "Native sheep-head bounce differs from completed legacy move "
                                  "(late boundary crossing)");
                            check(native->inside(), "Native sheep head escaped the world");
                            ++comparisons;
                            check(legacy->random.state() == native->random.state(),
                                  "Boundary fix consumed random draws");
                        }
                    }
        for (bool aof : {false, true})
            for (int steps : {3, 6})
                for (int edge = 0; edge < 8; ++edge)
                    for (bool outward : {false, true}) {
                        auto rig = std::make_unique<Rig>(assets, aof, steps);
                        auto &e = rig->entity();
                        e.ax = e.ay = 0xc000;
                        const int far = fixed16(12), speed = fixed16(6) * (outward ? -1 : 1);
                        if (edge == 0 || edge == 4 || edge == 6) {
                            e.x = -far;
                            e.vx = speed;
                        }
                        if (edge == 1 || edge == 5 || edge == 7) {
                            e.x = fixed16(rig->player.worldWidth()) + far;
                            e.vx = -speed;
                        }
                        if (edge == 2 || edge == 4 || edge == 5) {
                            e.y = -far;
                            e.vy = speed;
                        }
                        if (edge == 3 || edge == 6 || edge == 7) {
                            e.y = fixed16(rig->player.worldHeight()) + far;
                            e.vy = -speed;
                        }
                        bool entered = false;
                        for (int frame = 0; frame < 32; ++frame) {
                            rig->tick();
                            if (rig->inside()) {
                                entered = true;
                                break;
                            }
                        }
                        check(
                            entered,
                            "Exterior sheep head repeatedly reversed instead of returning inside");
                        ++recoveries;
                    }
        std::cout << comparisons << " completed-move parity checks and " << recoveries
                  << " exterior recoveries passed, both editions and native rates\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
