#include "grpp/assets.hpp"
#include "grpp/mechanisms.hpp"
#include "grpp/stages.hpp"
#include "original_levels.hpp"
#include "original_tables.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace grpp;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename Function> void throws(Function f, const char *message) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void numeric() {
    check(add32(INT32_MAX, 1) == INT32_MIN, "Signed overflow must wrap");
    check(sub32(INT32_MIN, 1) == INT32_MAX, "Signed underflow must wrap");
    check(asr32(-65537, 16) == -2, "Negative fixed positions must round down");
    GameRandom game(1);
    for (uint32_t expected : {41u, 18467u, 6334u, 26500u, 19169u})
        check(game.next() == expected, "VC6 rand sequence changed");
    EffectRandom effect(0x31415926u);
    check(effect.next() == 0x18a0ac93u && effect.next() == 0xaf505649u &&
              effect.next() == 0x74a82b24u,
          "Arithmetic-shift effect stream changed");
    GameRandom isolated(1);
    effect.next();
    check(isolated.next() == 41, "Game/effect random streams must be independent");
    check(packOriginalColor(0xff000000) == 0xf800 && packOriginalColor(0x00ff0000) == 0x07e0 &&
              packOriginalColor(0x0000ff00) == 0x001f,
          "Original color layout");
    check(expand565(0).a == 0 && expand565(0, false).a == 255, "Zero color key modes");
    check(combine565(0xffff, 0xffff) == 0xf7de, "Original combine is not a modern alpha blend");
    check(cycleColor(original::cycles[7], 0) == 0x97977f00u,
          "Color midpoint/truncation must match x87 source");
}
void assetChecks(const std::filesystem::path &root) {
    Assets assets;
    assets.load(root);
    check(assets.spriteCount() == 369, "Original sprite catalog incomplete");
    for (size_t i = 0; i < assets.spriteCount(); ++i) {
        const auto image = assets.extract(i);
        check(image.pixels.size() == size_t(image.width) * image.height,
              "Sprite extraction bounds");
    }
    throws([] { decodeTga({0, 1, 2}); }, "Truncated TGA accepted");
    // Asymmetric 2x2 TGA, bottom-left origin; verify orientation and 5:5:5 -> 5:6:5.
    std::vector<uint8_t> tga(26, 0);
    tga[2] = 2;
    tga[12] = 2;
    tga[14] = 2;
    tga[16] = 16;
    const uint16_t values[4]{0x001f, 0x7fff, 0x7c00, 0x03e0};
    for (size_t i = 0; i < 4; ++i) {
        tga[18 + i * 2] = uint8_t(values[i]);
        tga[19 + i * 2] = uint8_t(values[i] >> 8);
    }
    auto image = decodeTga(tga);
    check(image.at(0, 0) == 0xf800 && image.at(1, 0) == 0x07c0 && image.at(0, 1) == 31 &&
              image.at(1, 1) == 0xffdf,
          "TGA conversion/origin");
    for (const auto &level : original::stages) {
        assets.load(root);
        for (size_t family = 0; family < level.orientationSources.size(); ++family) {
            const size_t source = size_t(level.orientationSources[family]);
            const auto originalA = assets.extract(source), originalB = assets.extract(source + 1);
            assets.makeOrientations(source, family);
            const auto identityA = assets.extract(120 + family * 8),
                       identityB = assets.extract(119 + family * 8);
            check(identityA.pixels == originalA.pixels && identityB.pixels == originalB.pixels,
                  "Adjacent animation frames must remain distinct");
            const auto rotate180 = assets.extract(116 + family * 8),
                       rotate90 = assets.extract(118 + family * 8);
            check(rotate180.at(0, 0) == originalA.at(originalA.width - 1, originalA.height - 1),
                  "180-degree atlas mapping");
            check(rotate90.width == originalA.height && rotate90.height == originalA.width,
                  "Rotated sprite dimensions");
            for (int y = 0; y < rotate90.height; ++y)
                for (int x = 0; x < rotate90.width; ++x)
                    check(rotate90.at(x, y) == originalA.at(y, originalA.height - 1 - x),
                          "90-degree atlas pixels");
        }
    }
    assets.load(root);
    Canvas canvas;
    canvas.clear(0x1234);
    canvas.sprite(assets, 358, 120, 100);
    Raster565 raster;
    raster.render(canvas, assets);
    check(raster.pixels().back() == 0x1234, "Original final clip pixel must remain untouched");
    canvas.clear();
    canvas.sprite(assets, 358, -1000, -1000);
    raster.render(canvas, assets);
    check(std::all_of(raster.pixels().begin(), raster.pixels().end(),
                      [](uint16_t p) { return p == 0; }),
          "Offscreen sprite must not write raster");
}
void stages() {
    SpawnProgram first(original::stages[0].spawnProgram);
    std::vector<uint32_t> commands;
    while (auto word = first.next())
        commands.push_back(*word);
    check(commands.size() == 23 && commands.front() == 0x7f010022 && commands.back() == 0x7f0400f3,
          "First stage includes its post-marker final spawn");
    size_t total = 0;
    for (const auto &stage : original::stages) {
        SpawnProgram program(stage.spawnProgram);
        size_t count = 0;
        while (program.next()) {
            ++count;
            check(count < 100000, "Stage expansion not bounded");
        }
        check(count > 0 && program.ended(), "Stage lacks termination or spawn entities");
        total += count;
    }
    check(total == 2163, "All original stage commands, including post-marker spawns");
    const std::vector<uint32_t> nested{0x80010000, 1,          0x80010000, 2,
                                       0x81000000, 0x81000000, 0x8f000000, 3};
    SpawnProgram program(nested);
    std::vector<uint32_t> actual;
    while (auto word = program.next())
        actual.push_back(*word);
    check(actual == std::vector<uint32_t>({1, 2, 2, 1, 2, 2, 3}),
          "Source loops and post-marker final command");
    SpawnProgram terminal(nested);
    terminal.skip(100);
    check(!terminal.ended() && terminal.cursor() == 6 && terminal.next() == 3 && terminal.ended() &&
              !terminal.next(),
          "Skip must leave the final marker for the next entity request");
    SpawnProgram skip(nested);
    skip.skip(4);
    check(skip.next() == 2, "Skip must preserve loop state");
    const std::vector<uint32_t> broken{0x81000000};
    throws(
        [&] {
            SpawnProgram invalid(broken);
            invalid.next();
        },
        "Malformed loop accepted");
    std::cout << "50 stage programs: " << total << " expanded spawn commands\n";
}
void player() {
    EntityPool pool;
    auto &p = pool.at(EntityPool::Player);
    p.behavior = 1;
    p.x = fixed16(120);
    p.y = fixed16(240);
    for (auto &point : p.history)
        point = {p.x, p.y};
    PlayerState state;
    std::vector<int> sounds;
    updatePlayer(pool, state, {200, 80, 0, 0}, sounds);
    check(p.x == fixed16(130) && p.y == fixed16(220), "Original one-eighth follow movement");
    check(p.history[0].x == fixed16(120) && p.history[0].y == fixed16(240),
          "Trail must record position before movement");
    check(sounds == std::vector<int>({6}) && pool.at(90).x == fixed16(120),
          "Shot must spawn before player moves");
    check(pool.at(90).animation == 1 && pool.at(90).vy == -65536 && pool.at(90).ay == -4096,
          "Primary shot creation");
    check(pool.at(90).history[0].tag == -1 && pool.at(90).history[63].tag == -1,
          "Primary shot creation invalidates all trail tags");
    sounds.clear();
    updatePlayer(pool, state, {200, 80, 0x20, 0}, sounds);
    check(state.bombActive && state.bombTicks == 82 && sounds == std::vector<int>({27}),
          "Bomb creation");
    sounds.clear();
    updatePlayer(pool, state, {200, 80, 0x20, 0}, sounds);
    check(sounds.empty(), "Held bomb must not retrigger");
    updatePlayer(pool, state, {200, 80, 0, 0}, sounds);
    check(pool.at(91).behavior == 2, "Autofire interval is three source ticks");
    EntityPool scheduler;
    scheduler.at(0).behavior = 1;
    int visits = 0;
    scheduler.updateRange(0, 3, 0, [&](Entity &, size_t index) {
        ++visits;
        if (index == 0)
            scheduler.at(2).behavior = 1;
    });
    check(visits == 2, "Later newborn slots must run in the same ascending traversal");
    scheduler.at(0).x = 123;
    scheduler.reset(0, 1);
    check(scheduler.at(0).x == 123 && scheduler.at(0).behavior == 0,
          "Pool reset must not erase unrelated fields");
    Entity a, b;
    b.behavior = 3;
    b.flags = 1;
    b.x = fixed16(16);
    b.y = fixed16(16);
    check(squareCollision(a, b, 16), "Original collision is inclusive axis-aligned square");
    b.drawing = 1;
    check(!squareCollision(a, b, 16), "Special drawing state excludes collision");
}
void inputAndScores(const std::filesystem::path &root) {
    auto view = makeViewport(1920, 1080, Aspect::Fit);
    float x, y;
    check(view.width == 810 && view.height == 1080 && view.x == 555, "Portrait letterbox");
    check(view.toLogical(960, 540, x, y) && x == 120 && y == 160, "Pointer viewport inversion");
    check(!view.toLogical(0, 540, x, y), "Letterbox touches must not enter game");
    view = makeViewport(1920, 1080, Aspect::Integer);
    check(view.width == 720 && view.height == 960, "Integer scaling");
    InputLatch latch;
    check(latch.sample(1, 2, 32).pressed == 32 && latch.sample(1, 2, 32).pressed == 0,
          "Input edge lifetime");
    latch.reset();
    check(latch.sample(1, 2, 0).held == 0, "Focus reset");
    Scores scores;
    scores.load(root / "original-hst.txt");
    check(scores.normal[0].points == 0x40e667 && scores.normal[0].name == "MUSK_OX",
          "Original scores loaded");
    check(scores.players[0].level == 13 && scores.players[0].lives == 13 &&
              scores.players[0].score == 0x3b9286 && scores.players[0].difficulty == 0,
          "Original profile field order from x86 varargs");
    auto copy = scores;
    check(copy.insertNormal({"TIED", scores.normal[0].points}) == 1,
          "Ties must follow existing entries");
    check(copy.normal[0].name == "MUSK_OX" && copy.normal[1].name == "TIED",
          "High-score insertion ordering");
    const auto temporary = std::filesystem::temp_directory_path() / "grpp-score-roundtrip-test.txt";
    scores.save(temporary);
    Scores roundtrip;
    roundtrip.load(temporary);
    std::filesystem::remove(temporary);
    check(roundtrip.players[0].name == scores.players[0].name &&
              roundtrip.normal[9].points == scores.normal[9].points,
          "Score serialization roundtrip");
}
uint64_t hashPixel(uint64_t h, uint16_t value) {
    h = (h ^ uint8_t(value)) * 1099511628211ull;
    return (h ^ uint8_t(value >> 8)) * 1099511628211ull;
}
void originalOracle(const std::filesystem::path &root, const std::filesystem::path &fixture) {
    std::ifstream file(fixture);
    check(bool(file), "Missing original routine oracle fixture");
    file >> std::hex;
    std::string kind;
    Assets assets;
    assets.load(root);
    unsigned colors = 0, rasters = 0, blends = 0, playerTicks = 0;
    bool complete = false;
    EntityPool playerPool;
    PlayerState playerState;
    while (file >> kind) {
        if (kind == "blend") {
            uint64_t expected = 0;
            file >> expected;
            uint64_t actual = 14695981039346656037ull;
            for (uint32_t p = 0; p < 65536; ++p) {
                actual = hashPixel(actual, combine565(uint16_t(p), 0));
                actual = hashPixel(actual, combine565(uint16_t(p), 65535));
                actual = hashPixel(actual, combine565(uint16_t(p), uint16_t(p ^ 0xffff)));
            }
            check(actual == expected, "196608 packed blends differ from original x86 routine");
            ++blends;
        } else if (kind == "cycle") {
            unsigned index;
            uint32_t bits, expected;
            file >> index >> bits >> expected;
            float phase;
            std::memcpy(&phase, &bits, sizeof phase);
            const auto actual = cycleColor(original::cycles.at(index), phase);
            if (actual != expected) {
                std::cerr << "Color oracle index=" << index << " phase=" << phase
                          << " expected=" << std::hex << expected << " actual=" << actual
                          << std::dec << '\n';
                throw std::runtime_error("Color oscillator differs from original x87 routine");
            }
            ++colors;
        } else if (kind == "raster") {
            int sample;
            uint64_t expected;
            file >> sample >> expected;
            const auto &rect = assets.sprite(sample < 12 ? 358 : 0);
            const int mode = sample % 4;
            const int x = sample % 3 == 0 ? -6 : sample % 3 == 1 ? 20 : 231;
            const int y = sample % 3 == 0 ? -7 : sample % 3 == 1 ? 80 : 311;
            Canvas canvas;
            canvas.clear(0x1234);
            canvas.image(rect.sheet, {rect.x, rect.y, rect.width, rect.height}, float(x), float(y),
                         float(rect.width), float(rect.height),
                         {true, uint16_t(mode & 1 ? 0xf800 : 0), uint16_t(mode & 2 ? 0x001f : 0)});
            Raster565 raster;
            raster.render(canvas, assets);
            uint64_t actual = 14695981039346656037ull;
            for (uint16_t p : raster.pixels())
                actual = hashPixel(actual, p);
            check(actual == expected, "Clipped sprite raster differs from original x86 blitter");
            ++rasters;
        } else if (kind == "player") {
            int scenario = 0, tick = 0;
            file >> scenario >> tick;
            const int transitions[3]{0, 5, -4};
            if (tick == 0) {
                playerPool = EntityPool{};
                playerState = PlayerState{};
                auto &p = playerPool.at(89);
                p.x = fixed16(120);
                p.y = fixed16(240);
                p.sprite = 360;
                p.behavior = 1;
                p.flags = 1;
                for (auto &point : p.history)
                    point = {p.x, p.y};
                playerState.sheepPower = scenario % 4 + 1;
                playerState.transitionTicks = transitions[scenario / 4];
                playerState.reverseTransition = playerState.transitionTicks > 0;
                playerState.alternateCraftUnlocked = true;
                playerState.selectedPlayer = 2;
            }
            std::vector<int> sounds;
            updatePlayer(playerPool, playerState,
                         {80 + tick * 17 % 300 - 20, 50 + tick * 11 % 380 - 20,
                          tick >= 11 && tick <= 16 ? 32u : 0u, tick == 20 ? 64u : 0u},
                         sounds);
            const auto &p = playerPool.at(89);
            const std::array<uint32_t, 10> values{uint32_t(p.x),
                                                  uint32_t(p.y),
                                                  uint32_t(playerState.transitionTicks),
                                                  uint32_t(playerState.scoreClock),
                                                  uint32_t(playerState.shotCountdown),
                                                  uint32_t(playerState.followerShotIndex),
                                                  uint32_t(playerState.bombActive),
                                                  uint32_t(playerState.bombTicks),
                                                  uint32_t(p.sprite),
                                                  uint32_t(sounds.size())};
            for (size_t field = 0; field < values.size(); ++field) {
                uint32_t expected = 0;
                file >> expected;
                if (values[field] != expected) {
                    std::cerr << "Player oracle scenario=" << scenario << " tick=" << tick
                              << " field=" << field << " expected=" << expected
                              << " actual=" << values[field] << '\n';
                    throw std::runtime_error("Player state differs from original dispatcher");
                }
            }
            for (size_t slot = 90; slot < 110; ++slot) {
                const auto &shot = playerPool.at(slot);
                for (uint32_t value :
                     {uint32_t(shot.x), uint32_t(shot.y), uint32_t(shot.behavior),
                      uint32_t(shot.sprite), uint32_t(shot.vy), uint32_t(shot.ay)}) {
                    uint32_t expected = 0;
                    file >> expected;
                    check(value == expected,
                          "Projectile creation differs from original player dispatcher");
                }
            }
            ++playerTicks;
        } else if (kind == "complete") {
            int flag = 0;
            file >> flag;
            complete = flag == 1;
        } else
            throw std::runtime_error("Unknown original fixture record");
    }
    check(complete && blends == 1 && colors == 1120 && rasters == 24 && playerTicks == 768,
          "Original oracle fixture is incomplete");
    std::cout << "Original oracle: 196608 blends, 1120 x87 colors, 24 clipped rasters, 768 player "
                 "ticks match\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: grpp_core_checks ASSET_DIRECTORY ORIGINAL_FIXTURE");
        const std::filesystem::path root = argv[1];
        numeric();
        assetChecks(root);
        stages();
        player();
        inputAndScores(root);
        originalOracle(root, argv[2]);
        std::cout << "PASS: numeric, original assets/atlas, all stage scripts, player/scheduler, "
                     "input, scores\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
