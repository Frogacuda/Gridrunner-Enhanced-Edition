#include "grpp/mechanisms.hpp"
#include "grpp/native_motion.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace grpp {
int EntityPool::firstFree(size_t begin, size_t count) const {
    if (begin > Capacity || count > Capacity - begin)
        throw std::out_of_range("Entity slot range");
    for (size_t i = begin; i < begin + count; ++i)
        if (entries_[i].behavior == 0) {
            if (nativeMotion)
                nativeMotion->invalidate(i);
            return int(i);
        }
    return -1;
}
void EntityPool::reset(size_t begin, size_t count, bool menuParticles) {
    if (begin > Capacity || count > Capacity - begin)
        throw std::out_of_range("Entity slot range");
    for (size_t i = begin; i < begin + count; ++i) {
        // 0040ade3/0040ae71 reset only these fields; stale position/history is
        // intentionally retained until the creating caller initializes it.
        auto &e = entries_[i];
        e.animation = -1;
        e.drawing = 0;
        e.scale = 0;
        e.attackParameters = 0;
        e.behavior = menuParticles ? 19 : 0;
    }
}
void EntityPool::animate(Entity &e, int32_t frame) {
    if (e.animation == 1) {
        if (e.animationFrames <= 0)
            throw std::runtime_error("Animated entity has no frames");
        e.sprite = e.animationBase + asr32(frame, unsigned(e.animationShift)) % e.animationFrames;
    }
}
void EntityPool::updateRange(size_t begin, size_t count, int32_t frame,
                             const std::function<void(Entity &, size_t)> &update) {
    if (begin > Capacity || count > Capacity - begin)
        throw std::out_of_range("Entity slot range");
    for (size_t i = begin; i < begin + count; ++i)
        if (entries_[i].behavior != 0) {
            animate(entries_[i], frame);
            update(entries_[i], i);
        }
}
bool squareCollision(const Entity &a, const Entity &b, int radius) {
    if (b.behavior == 0 || !(b.flags & 1) || b.behavior == 5 || b.drawing == 1)
        return false;
    return std::abs(pixel(a.x) - pixel(b.x)) <= radius &&
           std::abs(pixel(a.y) - pixel(b.y)) <= radius;
}
bool outsideX(const Entity &e, const SpriteRect &sprite) {
    const int x = pixel(e.x);
    return x < 0 ? sprite.width < -x : sprite.width < x - LogicalWidth;
}
bool outsideY(const Entity &e, const SpriteRect &sprite) {
    const int y = pixel(e.y);
    return y < 0 ? sprite.height < -y : sprite.height < y - LogicalHeight;
}
void updateMenuParticle(Entity &e, const Assets &assets) {
    e.x = add32(e.x, e.vx);
    e.y = add32(e.y, e.vy);
    const auto &s = assets.sprite(size_t(e.sprite));
    if (outsideX(e, s))
        e.x = fixed16(e.vx < 1 ? (s.width >> 1) + 238 : 2 - (s.width >> 1));
    if (outsideY(e, s))
        e.y = fixed16(e.vy < 1 ? (s.height >> 1) + 318 : 2 - (s.height >> 1));
}
namespace {
int32_t clampPlayerCoordinate(int32_t coordinate, int lastPixel) {
    // Source compares the integer pixel, allowing fractional positions within
    // the last pixel. std::clamp(value,0,lastPixel<<16) would change that path.
    if (coordinate < 0)
        return 0;
    if (pixel(coordinate) > lastPixel)
        return fixed16(lastPixel);
    return coordinate;
}
void initializeShot(Entity &shot, int32_t x, int32_t y, bool secondary, int side, int power) {
    shot.x = x;
    shot.y = y;
    shot.animation = 1;
    shot.behavior = 2;
    shot.drawing = secondary ? 15 : 2;
    shot.animationShift = 1;
    shot.animationFrames = 10;
    shot.vx = 0;
    shot.ax = 0;
    shot.historyCursor = 0;
    if (secondary) {
        shot.animation = -1;
        // The source changes only the direction-dependent fields in this arm.
        const int broad = power >= 4 ? 2 : 0;
        shot.sprite = (side & 1) ? 108 + broad : 107 + broad;
        shot.vy = (side & 1) ? 0x80000 : -0x80000;
        shot.ay = (side & 1) ? 0x2000 : -0x2000;
    } else {
        shot.sprite = shot.animationBase = 7;
        shot.vy = -65536;
        shot.ay = -4096;
        shot.phase = 6; // Secondary construction leaves this shared word stale.
        for (auto &point : shot.history)
            point.tag = -1;
    }
}
} // namespace
void updatePlayer(EntityPool &pool, PlayerState &state, const LogicalInput &input,
                  std::vector<int> &sounds) {
    Entity &player = pool.at(EntityPool::Player);
    const uint32_t cursor = player.historyCursor++;
    player.history[cursor & 63].x = player.x;
    player.history[cursor & 63].y = player.y;
    if (state.transitionTicks != 0)
        state.transitionTicks = add32(state.transitionTicks, state.reverseTransition ? -1 : 1);
    player.targetX = fixed16(input.x);
    if (state.transitionTicks < 1) {
        if (state.transitionTicks == -1) {
            player.drawing = 0;
            player.flags &= ~128u;
        }
        state.scoreClock =
            add32(state.scoreClock,
                  mul32(state.scoreStep,
                        pool.at(EntityPool::Companion).companionShieldTicks() > 0 ? 16 : 1));
        player.targetY = fixed16(input.y);
        if (state.alternateCraftUnlocked && (input.pressed & 0x40u)) {
            state.alternateCraft ^= 1;
            player.sprite = state.selectedPlayer + 358 + state.alternateCraft * 5;
        }
        if ((input.held & 0x20u) && !state.bombActive) {
            sounds.push_back(27);
            state.bombActive = true;
            state.bombTicks = state.sheepPower * 32 + 50;
            state.beamTarget = 0;
        }
        if (state.transitionTicks == 0 || (player.flags & 128u)) {
            --state.shotCountdown;
            if (state.shotCountdown < 0) {
                state.shotCountdown = state.shotInterval;
                if (state.arse)
                    state.fireParity ^= 1;
                const int slot = pool.firstFree(EntityPool::ShotFirst, EntityPool::ShotCount);
                if (slot >= 0) {
                    initializeShot(pool.at(size_t(slot)), add32(player.x, state.worldX),
                                   add32(player.y, state.worldY), false, 0, state.sheepPower);
                    if (state.arse) {
                        auto &shot = pool.at(size_t(slot));
                        shot.ay = -8192;
                        if (input.held & 1u) {
                            shot.vy = shot.ay = 0;
                            shot.vx = state.fireParity ? -65536 : 65536;
                            shot.ax = state.fireParity ? -8192 : 8192;
                        }
                    }
                    sounds.push_back(6);
                }
                if (state.sheepPower > 1) {
                    const int followerSlot = pool.firstFree(
                        EntityPool::ShotFirst + EntityPool::ShotCount, EntityPool::ShotCount);
                    if (followerSlot >= 0) {
                        const uint32_t lag =
                            player.historyCursor - uint32_t(state.followerShotIndex * 4 + 4);
                        const auto &point = player.history[lag & 63];
                        initializeShot(pool.at(size_t(followerSlot)), add32(point.x, state.worldX),
                                       add32(point.y, state.worldY), true,
                                       state.followerShotIndex - 1, state.sheepPower);
                        if (state.arse)
                            pool.at(size_t(followerSlot)).ay =
                                mul32(pool.at(size_t(followerSlot)).ay, 3) / 2;
                        sounds.push_back(6);
                        state.followerShotIndex = (state.followerShotIndex + 1) % state.sheepPower;
                    }
                }
            }
        }
        if (pool.nativeMotion)
            pool.nativeMotion->player(player, state, input);
        else {
            player.x = add32(player.x, asr32(sub32(player.targetX, player.x), 3));
            player.y = add32(player.y, asr32(sub32(player.targetY, player.y), 3));
        }
        player.x = clampPlayerCoordinate(player.x, state.width() - 1);
        player.y = clampPlayerCoordinate(player.y, state.height() - 1);
    } else {
        if (pool.nativeMotion)
            pool.nativeMotion->player(player, state, input);
        else
            player.x = clampPlayerCoordinate(
                add32(player.x, asr32(sub32(player.targetX, player.x), 3)), state.width() - 1);
    }
}
void Scores::load(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read score file: " + path.string());
    Scores candidate;
    candidate.guest = guest;
    file >> std::hex;
    for (auto &score : candidate.normal)
        file >> score.name >> score.points;
    for (auto &score : candidate.underworld)
        file >> score.name >> score.points;
    for (auto &player : candidate.players)
        file >> player.name >> player.bestLevel >> player.difficulty >> player.level >>
            player.lives >> player.score;
    if (!file)
        throw std::runtime_error("Invalid score/profile data");
    std::vector<std::string> tail;
    std::string token;
    while (file >> token)
        tail.push_back(token);
    auto hex = [](const std::string &word) {
        size_t used = 0;
        const auto v = std::stoull(word, &used, 16);
        if (used != word.size() || v > 0xffffffffull)
            throw std::runtime_error("Invalid score number");
        return uint32_t(v);
    };
    try {
        size_t flags = 0;
        if (tail.size() == 5) {
            candidate.extendedFormat = true;
            candidate.enduranceMaster = {tail[0], hex(tail[1])};
            candidate.enduranceLevel = hex(tail[2]);
            flags = 3;
        } else if (tail.size() != 2)
            throw std::runtime_error("Invalid score file tail");
        candidate.selectedPlayer = hex(tail[flags]);
        candidate.alternateCraftUnlocked = hex(tail[flags + 1]);
    } catch (const std::exception &) {
        throw std::runtime_error("Invalid score file tail");
    }
    if (candidate.selectedPlayer > 4 || candidate.alternateCraftUnlocked > 1 ||
        candidate.enduranceLevel > 50 || candidate.enduranceMaster.name.size() > 19)
        throw std::runtime_error("Invalid score flags/record");
    for (const auto &p : candidate.players)
        if (p.bestLevel > 50 || p.level > 50 || p.lives > 0x7fffffffu || p.difficulty > 0x7fffffffu)
            throw std::runtime_error("Invalid original checkpoint");
    for (const auto &score : candidate.normal)
        if (score.name.size() > 19)
            throw std::runtime_error("Score name too long");
    for (const auto &score : candidate.underworld)
        if (score.name.size() > 19)
            throw std::runtime_error("Score name too long");
    for (const auto &player : candidate.players)
        if (player.name.size() > 19)
            throw std::runtime_error("Player name too long");
    *this = std::move(candidate);
}
void Scores::save(const std::filesystem::path &path) const {
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("Cannot write score file: " + path.string());
    file << std::hex;
    for (const auto &score : normal)
        file << score.name << ' ' << score.points << ' ';
    for (const auto &score : underworld)
        file << score.name << ' ' << score.points << ' ';
    for (const auto &player : players)
        file << player.name << ' ' << player.bestLevel << ' ' << player.difficulty << ' '
             << player.level << ' ' << player.lives << ' ' << player.score << ' ';
    if (extendedFormat)
        file << enduranceMaster.name << ' ' << enduranceMaster.points << ' ' << enduranceLevel
             << ' ';
    file << selectedPlayer << ' ' << alternateCraftUnlocked << '\n';
    file.flush();
    if (!file)
        throw std::runtime_error("Failed writing score file");
}
bool Scores::recordEndurance(unsigned level, uint32_t points, const std::string &name) {
    if (level > enduranceLevel || (level == enduranceLevel && points > enduranceMaster.points)) {
        enduranceLevel = level;
        enduranceMaster = {name, points};
        extendedFormat = true;
        return true;
    }
    return false;
}
int Scores::insertNormal(Score score) {
    // 0041a824: ties do not displace an existing score.
    size_t position = 0;
    while (position < normal.size() &&
           signedBits(normal[position].points) >= signedBits(score.points))
        ++position;
    if (position == normal.size())
        return int(position);
    for (size_t i = normal.size() - 1; i > position; --i)
        normal[i] = normal[i - 1];
    normal[position] = std::move(score);
    return int(position);
}
bool Scores::recordStage(unsigned stage, int32_t points, const std::string &name) {
    auto &record = underworld.at(stage);
    if (signedBits(record.points) >= points)
        return false;
    record = {name, uint32_t(points)};
    return true;
}
bool Scores::recordCheckpoint(unsigned slot, int lives, unsigned level, int32_t score,
                              int difficulty) {
    if (slot > players.size())
        throw std::out_of_range("Player checkpoint slot");
    auto &p = slot == players.size() ? guest : players[slot];
    if (signedBits(p.lives) > lives || (signedBits(p.lives) == lives && p.level >= level))
        return false;
    p.lives = uint32_t(lives);
    p.level = level;
    p.score = uint32_t(score);
    p.difficulty = uint32_t(difficulty);
    return true;
}
} // namespace grpp
