#include "formation_observation.hpp"
#include "original_levels.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace grpp;
struct NativeBackend {
    EntityPool pool;
    PlayerState player;
    GameRandom random{0x1982};
    FormationSystem formations{pool, player, random};
    EntityCensus counts;
    int level = 0;
    void begin(int index, int difficulty, int skip = 0) {
        level = index;
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            pool.at(i).behavior = 0;
        pool.at(0).behavior = 8;
        auto &p = pool.at(EntityPool::Player);
        p.x = fixed16(120);
        p.y = fixed16(240);
        p.behavior = 1;
        pool.at(EntityPool::Companion).behavior = index % 3 == 0 ? 13 : 0;
        player.worldX = fixed16(index % 2 ? 80 : 0);
        player.worldY = fixed16(index % 2 ? 160 : 0);
        player.scoreClock = 0;
        formations.prepareLevel(original::stages.at(size_t(index)), unsigned(index), difficulty,
                                skip);
        counts = {};
    }
    void seed() { formations.seedInitialSpawners(); }
    void retire() {
        for (size_t i = 0; i < EntityPool::Capacity; ++i) {
            auto &e = pool.at(i);
            if (e.behavior == 3 || e.behavior == 4 || e.behavior == 17)
                e.behavior = 0;
        }
    }
    void tick(int tick) {
        player.scoreClock = tick * 64;
        const int ox = pixel(player.worldX), oy = pixel(player.worldY);
        const SpawnFrame frame{ox + 16,
                               ox + 224,
                               oy + 16,
                               oy + 304,
                               tick >= 25 && tick < 29 ? 200 : 0,
                               tick < 70 || tick >= 73};
        counts = {};
        // Actual source order: ascending live slots. Only this declared family's
        // behaviors are dispatched here; no substitute enemy update is supplied.
        for (size_t i = 0; i < EntityPool::Capacity; ++i) {
            auto &e = pool.at(i);
            if (e.behavior == 10 || e.behavior == 11)
                formations.updateSpawner(e, frame, counts);
        }
    }
    void matrix(int scenario) {
        if (scenario != 27)
            begin(scenario == 26 ? 40 : 0, 0);
        auto &source = pool.at(12);
        source.x = fixed16(24);
        source.y = fixed16(80);
        source.commandOrHealth = uint32_t((scenario % 24 / 4) << 20 | scenario % 4);
        source.behavior = 11;
        if (scenario == 24)
            for (size_t i = 78; i < 86; ++i)
                pool.at(i).behavior = 3;
        if (scenario == 25)
            for (size_t i = 2; i <= 70; i += 2)
                pool.at(i).behavior = 18;
        if (scenario == 26 || scenario == 27)
            source.commandOrHealth = 0x00200003u;
        formations.materialize(source);
    }
    formation_check::Observation observe() const {
        const auto &t = formations.tuning();
        uint64_t h = 14695981039346656037ull;
        for (uint32_t word : {uint32_t(t.headHealth),
                              uint32_t(t.segmentHealth),
                              uint32_t(t.podResistance),
                              uint32_t(t.allowance),
                              uint32_t(t.segmentCount),
                              t.headAttack,
                              uint32_t(t.speedX),
                              uint32_t(t.speedY),
                              uint32_t(t.anchorSpeedX),
                              uint32_t(t.anchorSpeedY),
                              uint32_t(t.anchorDepthStep),
                              uint32_t(t.headSprite),
                              uint32_t(t.segmentSprite),
                              uint32_t(t.historySpacing),
                              t.splitTransform,
                              uint32_t(t.retargetInterval),
                              uint32_t(t.retargetTurnStep),
                              uint32_t(t.beamReach),
                              uint32_t(t.specialTurnChance),
                              uint32_t(t.flyingPlantsGrid),
                              uint32_t(t.flyingTracking),
                              uint32_t(t.flyingAlternateSpeed)})
            h = formation_check::append(h, word);
        return {formation_check::poolDigest(pool),
                h,
                {random.state(), uint32_t(player.scoreStep), uint32_t(formations.baselineAdvance()),
                 uint32_t(formations.activeThreshold()), uint32_t(formations.emitted()),
                 uint32_t(formations.requeueDistance()), uint32_t(formations.program().cursor()),
                 uint32_t(formations.programEnded()), uint32_t(counts.waiting),
                 uint32_t(counts.activeEnemies), uint32_t(player.scoreClock)}};
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: grpp_formation_checks ORIGINAL_FORMATION_FIXTURE");
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("Missing creation-family original fixture");
        NativeBackend backend;
        size_t snapshots = 0;
        formation_check::schedule(backend, [&](const char *kind, int level, int tick,
                                               const formation_check::Observation &actual) {
            std::string expectedKind;
            int expectedLevel, expectedTick;
            formation_check::Observation expected;
            input >> std::dec >> expectedKind >> expectedLevel >> expectedTick >> std::hex >>
                expected.pool >> expected.tuning;
            for (auto &word : expected.globals)
                input >> word;
            if (!input || expectedKind != kind || expectedLevel != level || expectedTick != tick)
                throw std::runtime_error("Original fixture schedule mismatch");
            if (!(actual == expected)) {
                std::cerr << "Mismatch " << kind << " level/case=" << level << " tick=" << tick
                          << "\nexpected " << expected << "\nactual   " << actual << '\n';
                throw std::runtime_error("Formation creation differs from original execution");
            }
            ++snapshots;
        });
        std::string marker;
        size_t expectedCount = 0;
        input >> marker >> std::dec >> expectedCount;
        if (!input || marker != "complete" || expectedCount != snapshots)
            throw std::runtime_error("Incomplete creation-family fixture");
        std::cout << "PASS: " << snapshots
                  << " original creation-family snapshots across 50 levels, 28 factory scenarios "
                     "and seven resume boundaries\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
