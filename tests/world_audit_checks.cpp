#include "grpp/native_motion.hpp"
#include "grpp/world.hpp"
#include <fstream>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool v, const std::string &m) {
    if (!v)
        throw std::runtime_error(m);
}
struct Rig {
    EntityPool pool;
    PlayerState p;
    GameRandom rng{0x1982};
    FormationSystem formations{pool, p, rng};
    CombatState cs;
    WorldGrid grid{};
    Assets assets;
    std::vector<int> sounds;
    CombatSystem combat{pool, p, rng, formations, cs, grid, assets, sounds};
    WorldState state;
    WorldSystem world{pool, p, cs, combat, grid, formations.tuning(), state, sounds};
    Canvas canvas;
    NativeMotion native;
    Rig(const char *path, bool aof, int steps) : native(steps) {
        assets.load(path);
        p.arse = aof;
        canvas.dimensions(p.width(), p.height());
        pool.at(89).behavior = 1;
        p.transitionTicks = 1;
    }
};
struct Expected {
    int x, y, z, vx, vy, vz;
    bool forecast = false;
    int lasers = 0, lx = 0, ly = 0;
};
void reference(Expected &e, bool aof, const ChainFrame &f) {
    e.x = add32(e.x, e.vx);
    e.y = add32(e.y, e.vy);
    e.z = add32(e.z, e.vz);
    const int minx = aof ? 0 : f.minimumX, maxx = aof ? 800 : f.maximumX,
              miny = aof ? 0 : f.minimumY, maxy = aof ? 800 : f.maximumY;
    if ((pixel(e.x) < minx && e.vx < 0) || (pixel(e.x) > maxx && e.vx > 0))
        e.vx = -e.vx;
    if ((pixel(e.y) < miny && e.vy < 0) || (pixel(e.y) > maxy && e.vy > 0))
        e.vy = -e.vy;
    if (aof ? ((e.z < 4096 && e.vz < 0) || (e.z > 20480 && e.vz > 0)) : (e.z < 4096 || e.z > 20480))
        e.vz = -e.vz;
    e.forecast = e.z + e.vz * 32 < 4096;
    if (e.z < 4096) {
        ++e.lasers;
        e.lx = pixel(e.x);
        e.ly = pixel(e.y);
    }
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "world audit ASSETS CSV");
        std::ofstream csv(argv[2]);
        csv << "edition,substeps,frame,x,y,depth,vx,vy,vz,forecast,laser_ticks\n";
        int comparisons = 0;
        for (bool aof : {false, true})
            for (int steps : {1, 3, 6}) {
                auto r = std::make_unique<Rig>(argv[1], aof, steps == 1 ? 6 : steps);
                r->p.transitionTicks = 0;
                r->pool.at(89).behavior = 14;
                auto &e = r->pool.at(0);
                e.behavior = 8;
                e.drawing = 8;
                e.x = fixed16(200);
                e.y = fixed16(250);
                e.depth = 8192;
                e.vx = 123457;
                e.vy = 234567;
                e.speed = aof ? -512 : -256;
                Expected expected{e.x, e.y, e.depth, e.vx, e.vy, e.speed};
                if (steps > 1)
                    r->pool.nativeMotion = &r->native;
                for (int frame = 0; frame < 650; ++frame) {
                    ChainFrame f{136, 344, 256, 544, .1f, frame};
                    reference(expected, aof, f);
                    r->sounds.clear();
                    if (steps > 1)
                        r->native.beginInterval();
                    r->world.anchor(e, f);
                    for (int sub = 1; sub < steps; ++sub)
                        r->native.continueMove(r->pool, 0);

                    check(e.x == expected.x && e.y == expected.y && e.depth == expected.z &&
                              e.vx == expected.vx && e.vy == expected.vy && e.speed == expected.vz,
                          "Cube endpoint/bounce mismatch edition=" + std::to_string(aof) +
                              " substeps=" + std::to_string(steps) +
                              " frame=" + std::to_string(frame));
                    check(r->state.forecast == expected.forecast, "Cube warning timing mismatch");
                    if (expected.z < 4096)
                        check(r->state.laserX == expected.lx && r->state.laserY == expected.ly &&
                                  r->state.laserTicks == 10,
                              "Cube zap origin/timer mismatch edition=" + std::to_string(aof) +
                                  " substeps=" + std::to_string(steps) +
                                  " frame=" + std::to_string(frame));
                    r->canvas.clear();
                    EffectRandom effects{uint32_t(frame + 1)};
                    SourceVisuals visual(r->canvas, r->assets, r->p, r->rng, effects);
                    r->world.lasers(visual, f);
                    csv << (aof ? "aof" : "standard") << ',' << steps << ',' << frame << ',' << e.x
                        << ',' << e.y << ',' << e.depth << ',' << e.vx << ',' << e.vy << ','
                        << e.speed << ',' << r->state.forecast << ',' << r->state.laserTicks
                        << '\n';
                    ++comparisons;
                }
            }
        for (bool aof : {false, true}) {
            auto r = std::make_unique<Rig>(argv[1], aof, 6);
            r->grid[3 + 3 * r->p.columns()] = 0x01000001u;
            auto &cell = r->grid[3 + 3 * r->p.columns()];
            uint32_t expected = cell;
            int active = 0, ended = 0;
            for (int frame = 0; frame < 250; ++frame) {
                auto old = expected;
                if ((old & 255) == 1 && (frame & 1)) {
                    unsigned growth = (old >> 16) & 255, damage = old >> 24;
                    if (!(old & 256)) {
                        if (++growth == damage)
                            old |= 256;
                    } else if (--growth < 1) {
                        if (++damage < 8)
                            old &= 255;
                        else {
                            old = 2;
                            growth = damage = 0;
                        }
                    }
                    expected = (damage << 24) | (growth << 16) | (old & 65535);
                } else if ((old & 255) == 2) {
                    int radius = int(old >> 16) + (aof ? 8 : 4);
                    expected = radius > r->formations.tuning().beamReach + 32
                                   ? 0
                                   : (old & 65535) | (uint32_t(radius) << 16);
                }
                EffectRandom effects{1};
                SourceVisuals v(r->canvas, r->assets, r->p, r->rng, effects);
                ChainFrame f{16, 224, 16, 304, .1f, frame};
                r->world.grid(v, f, 0);
                check(cell == expected, "Grid lifecycle mismatch edition=" + std::to_string(aof) +
                                            " frame=" + std::to_string(frame));
                if ((cell & 255) == 2)
                    ++active;
                if (!cell)
                    ++ended;
                static std::ofstream gridCsv(std::string(argv[2]) + ".grid.csv");
                if (frame == 0 && !aof)
                    gridCsv << "edition,frame,cell,kind,growth_or_radius,expected_cell\n";
                gridCsv << (aof ? "aof" : "standard") << ',' << frame << ',' << cell << ','
                        << (cell & 255u) << ',' << (cell >> 16) << ',' << expected << '\n';
                const auto saved = cell;
                for (int paint = 0; paint < 3; ++paint)
                    r->world.grid(v, f, 0, false);
                check(cell == saved, "Rendering advanced grid hazard");
            }
            check(active > 0 && ended > 0, "Grid zap did not activate/expire");
        }
        int contacts = 0;
        for (bool aof : {false, true})
            for (int kind : {0, 1, 2})
                for (int dx : {0, 7, 8, 15, 16, 31, 32, 319, 320, 399, 400})
                    for (int dy : {0, 7, 8, 15, 16})
                        for (bool invulnerable : {false, true}) {
                            auto r = std::make_unique<Rig>(argv[1], aof, 6);
                            auto &ship = r->pool.at(89);
                            ship.behavior = 1;
                            ship.x = fixed16(48 + dx);
                            ship.y = fixed16(48 + dy);
                            r->p.transitionTicks = invulnerable ? 2 : 0;
                            const int radius = kind == 0 ? 16 : kind == 1 ? 32 : r->p.height();
                            const bool expected =
                                !invulnerable &&
                                (kind == 0 ? (dx < 16 && dy < 16)
                                           : ((dx < radius && dy < 8) || (dy < radius && dx < 8)));
                            if (kind == 0)
                                r->grid[3 + 3 * r->p.columns()] = 0x01000001u;
                            if (kind == 1)
                                r->grid[3 + 3 * r->p.columns()] = (32u << 16) | 2u;
                            if (kind == 2) {
                                r->state.laserX = r->state.laserY = 48;
                                r->state.laserTicks = 10;
                                EffectRandom effects{1};
                                SourceVisuals v(r->canvas, r->assets, r->p, r->rng, effects);
                                r->world.lasers(v, {16, 224, 16, 304, .1f, 0});
                            }
                            const int before = r->p.transitionTicks;
                            r->world.nativeContacts();
                            check((r->p.transitionTicks == 1) == expected,
                                  "Hazard cross/pod collision or invulnerability mismatch");
                            if (invulnerable)
                                check(r->p.transitionTicks == before,
                                      "Hazard bypassed transition invulnerability");
                            ++contacts;
                        }
        for (bool aof : {false, true}) {
            auto r = std::make_unique<Rig>(argv[1], aof, 6);
            r->p.transitionTicks = 1;
            r->state.laserTicks = 10;
            EffectRandom effects{1};
            SourceVisuals v(r->canvas, r->assets, r->p, r->rng, effects);
            ChainFrame f{16, 224, 16, 304, .1f, 0};
            for (int tick = 0; tick < 12; ++tick) {
                r->world.lasers(v, f);
                check(r->state.laserTicks == std::max(0, 9 - tick), "Cube zap lifetime mismatch");
                const int before = r->state.laserTicks;
                for (int paint = 0; paint < 5; ++paint)
                    r->world.lasers(v, f, false);
                check(r->state.laserTicks == before, "Rendering consumed zap lifetime");
            }
        }
        std::cout << contacts
                  << " hazard collision/invulnerability boundaries and zap expiry PASS\n";
        std::cout << comparisons
                  << " cube endpoint comparisons and both full grid lifecycles PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
