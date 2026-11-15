#include "chain_observation.hpp"
#include "original_levels.hpp"
#include <fstream>
#include <iostream>

namespace {
using namespace grpp;
struct NativeChainBackend {
    EntityPool pool;
    PlayerState player;
    GameRandom random{0x1982};
    FormationSystem formations{pool, player, random};
    FormationTuning tuning;
    Assets assets;
    ChainMotion motion{pool, player, random, tuning, assets};
    WorldGrid grid{};
    EntityCensus census;
    unsigned seenModes = 0;
    int followed = 0, wrapped = 0, createdAttacks = 0, completedTurns = 0, retiredChains = 0,
        randomTurns = 0;
    int integratedHeads = 0, integratedBodies = 0;
    explicit NativeChainBackend(const char *path) { assets.load(path); }
    void begin(int level) {
        pool = {};
        grid = {};
        census = {};
        player.worldX = player.worldY = 0;
        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot)
            for (auto &p : pool.at(slot).history)
                p.color = 0xa5a50000u + uint32_t(slot);
        auto &p = pool.at(89);
        p.x = fixed16(120);
        p.y = fixed16(240);
        p.behavior = 14;
        pool.at(88).x = fixed16(120);
        pool.at(88).y = fixed16(160);
        formations.prepareLevel(original::stages.at(size_t(level)), unsigned(level), 0);
        tuning = formations.tuning();
    }
    void construct(int scenario) {
        begin(31);
        constexpr int families[]{0, 1, 3, 4, 5};
        auto &source = pool.at(12);
        source.x = fixed16(120);
        source.y = fixed16(160);
        source.commandOrHealth = uint32_t(families[scenario / 4] << 20 | scenario % 4);
        formations.materialize(source);
    }
    void focus(const chain_check::Input &in) {
        begin(0);
        auto &e = pool.at(78);
        e.behavior = 3;
        e.next = -1;
        e.sprite = 124;
        e.x = fixed16(in.x);
        e.y = fixed16(in.y);
        e.heading = in.heading;
        e.mode = in.mode;
        e.flags = uint32_t(in.flags);
        e.phase = in.targetHeading;
        e.turnRate = in.turnRate;
        e.life = in.countdown;
        e.attackParameters = -65536;
        pool.at(89).x = fixed16(in.playerX);
        pool.at(89).y = fixed16(in.playerY);
        player.worldX = fixed16(in.worldX);
        player.worldY = fixed16(in.worldY);
        grid.fill(uint32_t(in.pod));
        tuning.specialTurnChance = in.turnChance;
    }
    void retirement() {
        construct(0);
        auto &e = pool.at(78);
        e.x = fixed16(-1000);
        e.y = fixed16(-1000);
        e.outsideTicks = 249;
    }
    void attack(int scenario) {
        focus({});
        auto &e = pool.at(78);
        e.attackParameters = scenario == 19 ? -65536 : int(0xff0000u | uint32_t(scenario % 4));
        if (scenario == 16 || scenario == 17)
            e.attackParameters = 0xff0003;
        e.flags = scenario & 4 ? 3 : 1;
        pool.at(89).x = fixed16(scenario & 1 ? 60 : 180);
        pool.at(89).y = fixed16(scenario & 2 ? 100 : 220);
        if (scenario == 16)
            pool.at(89).x = e.x;
        if (scenario == 17)
            pool.at(89).y = e.y;
        if (scenario == 18)
            for (size_t i = 1; i < 11; ++i)
                pool.at(i).behavior = 7;
        motion.createAttack(e);
        const int activeShots = int(pool.at(1).behavior == 7);
        if (scenario == 18 || scenario == 19) {
            if (scenario == 19 && activeShots)
                throw std::runtime_error("Negative attack threshold unexpectedly emitted a shot");
        } else if (!activeShots)
            throw std::runtime_error("Guaranteed attack failed to allocate original first slot");
    }
    void tick(int tick) {
        census = {};
        const ChainFrame frame{16, 224, 16, 304, float(tick) * .1f};
        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
            auto &e = pool.at(slot);
            if (e.behavior == 3) {
                const auto oldCursor = e.historyCursor;
                const auto oldMode = unsigned(e.mode) & 255u;
                if (oldMode <= 5)
                    seenModes |= 1u << oldMode;
                int shotsBefore = 0;
                for (size_t i = 1; i < 11; ++i)
                    shotsBefore += pool.at(i).behavior == 7;
                motion.advanceHead(slot, frame, census);
                if (e.behavior == 0) {
                    ++retiredChains;
                    for (int next = e.next; next >= 0; next = pool.at(size_t(next)).next)
                        if (pool.at(size_t(next)).behavior)
                            throw std::runtime_error("Retirement left an active chain segment");
                    if (e.historyCursor != oldCursor + 1)
                        throw std::runtime_error(
                            "Retirement suppressed the final source trail write");
                }
                motion.steerHead(e, frame, grid);
                completedTurns += oldMode == 1 && (unsigned(e.mode) & 255u) != 1;
                randomTurns += tuning.specialTurnChance == 512 && oldMode != 1 &&
                               (unsigned(e.mode) & 255u) == 1;
                for (size_t i = 1; i < 11; ++i)
                    createdAttacks += pool.at(i).behavior == 7;
                createdAttacks -= shotsBefore;
                wrapped += (e.historyCursor & 63u) == 0;
            } else if (e.behavior == 4) {
                ++census.activeEnemies;
                motion.followBody(e, frame.trailColorPhase);
                ++followed;
                wrapped += (e.historyCursor & 63u) == 0;
            }
        }
    }
    void lifecycle() {
        begin(0);
        formations.seedInitialSpawners();
    }
    void integratedTick(int tick) {
        ChainMotion liveMotion{pool, player, random, formations.tuning(), assets};
        const int previousActive = census.activeEnemies;
        census = {};
        player.scoreClock = tick * 16;
        const ChainFrame frame{16, 224, 16, 304, float(tick) * .1f};
        const SpawnFrame spawnFrame{16, 224, 16, 304, previousActive, true};
        for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
            auto &e = pool.at(slot);
            if (e.behavior == 10 || e.behavior == 11)
                formations.updateSpawner(e, spawnFrame, census);
            else if (e.behavior == 3) {
                ++integratedHeads;
                liveMotion.advanceHead(slot, frame, census);
                liveMotion.steerHead(e, frame, grid);
            } else if (e.behavior == 4) {
                ++integratedBodies;
                ++census.activeEnemies;
                liveMotion.followBody(e, frame.trailColorPhase);
            } else if (e.behavior == 17)
                throw std::runtime_error("Integration schedule entered unrelated flyer behavior");
        }
        // liveMotion reads the formation owner's live tuning, including changes
        // by earlier spawners in this same traversal.
    }
    chain_check::Observation observe() const {
        return {formation_check::poolDigest(pool),
                {random.state(), uint32_t(census.activeEnemies), uint32_t(census.nearestX),
                 uint32_t(census.nearestY), uint32_t(census.nearestHead)}};
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: grpp_chain_checks ASSETS ORIGINAL_CHAIN_FIXTURE");
        std::ifstream input(argv[2]);
        if (!input)
            throw std::runtime_error("Missing original chain fixture");
        NativeChainBackend backend(argv[1]);
        std::string marker;
        uint64_t sineHash = 14695981039346656037ull, expectedHash;
        for (int32_t v : headingSines())
            sineHash = formation_check::append(sineHash, uint32_t(v));
        input >> marker >> std::hex >> expectedHash;
        if (!input || marker != "sines" || sineHash != expectedHash)
            throw std::runtime_error("Heading table differs from original initialization");
        size_t snapshots = 0;
        chain_check::schedule(backend, [&](const char *kind, int scenario, int tick,
                                           const chain_check::Observation &actual) {
            std::string expectedKind;
            int expectedScenario, expectedTick;
            chain_check::Observation expected{};
            input >> std::dec >> expectedKind >> expectedScenario >> expectedTick >> std::hex >>
                expected.pool;
            for (auto &value : expected.state)
                input >> value;
            if (!input || expectedKind != kind || expectedScenario != scenario ||
                expectedTick != tick)
                throw std::runtime_error("Chain schedule mismatch");
            if (!(expected == actual)) {
                std::cerr << kind << " scenario=" << scenario << " tick=" << tick << "\nexpected "
                          << expected << "\nactual   " << actual << '\n';
                throw std::runtime_error("Native chain phases differ from original execution");
            }
            ++snapshots;
        });
        size_t expectedCount;
        input >> marker >> std::dec >> expectedCount;
        if (!input || marker != "complete" || snapshots != expectedCount)
            throw std::runtime_error("Incomplete original chain fixture");
        if (backend.seenModes != 63u || !backend.followed || !backend.wrapped ||
            !backend.createdAttacks || !backend.completedTurns || backend.retiredChains != 1 ||
            backend.randomTurns < 2)
            throw std::runtime_error(
                "Chain schedule failed to exercise the declared motion/retirement/turn branches");
        if (!backend.integratedHeads || !backend.integratedBodies)
            throw std::runtime_error(
                "Spawner/chain traversal did not actually materialize and update a chain");
        std::cout << "PASS: original heading table and " << snapshots
                  << " chain/steering/attack snapshots\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
