#include "grpp/replay.hpp"
#include "aof_levels.hpp"
#include "original_levels.hpp"
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace grpp {
namespace {
constexpr size_t MaxState = 4 * 1024 * 1024, MaxFile = 12 * 1024 * 1024;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
// Serialize named fields with fixed widths; never persist native object layouts.
struct Archive {
    std::vector<uint8_t> &bytes;
    bool reading = false;
    size_t pos = 0;
    template <class T> void one(T &v);
    template <class... T> void operator()(T &...v) { (one(v), ...); }
    template <class T, size_t N> void one(std::array<T, N> &v) {
        for (auto &x : v)
            one(x);
    }
    template <class T> void one(std::vector<T> &v) {
        uint32_t n = uint32_t(v.size());
        one(n);
        check(n <= MaxState / 4, "Replay vector too large");
        if (reading)
            v.resize(n);
        for (auto &x : v)
            one(x);
    }
};
void writeVarint(std::vector<uint8_t> &out, uint32_t n) {
    do {
        out.push_back(uint8_t(n & 127) | (n > 127 ? 128 : 0));
        n >>= 7;
    } while (n);
}
uint32_t readVarint(const std::vector<uint8_t> &in, size_t &p) {
    uint32_t n = 0;
    for (int shift = 0; shift <= 21; shift += 7) {
        check(p < in.size(), "Truncated replay count");
        auto b = in[p++];
        n |= uint32_t(b & 127) << shift;
        if (!(b & 128))
            return n;
    }
    throw std::runtime_error("Replay count overflow");
}
std::vector<uint8_t> compress(const std::vector<uint8_t> &in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() / 8);
    for (size_t p = 0; p < in.size();) {
        if (in[p])
            out.push_back(in[p++]);
        else {
            size_t start = p;
            while (p < in.size() && !in[p])
                ++p;
            out.push_back(0);
            writeVarint(out, uint32_t(p - start));
        }
    }
    return out;
}
std::vector<uint8_t> decompress(const std::vector<uint8_t> &in) {
    std::vector<uint8_t> out;
    for (size_t p = 0; p < in.size();) {
        auto b = in[p++];
        if (b) {
            check(out.size() < MaxState, "Replay state too large");
            out.push_back(b);
        } else {
            auto n = readVarint(in, p);
            check(n && n <= MaxState - out.size(), "Invalid replay state run");
            out.insert(out.end(), n, 0);
        }
    }
    return out;
}
uint32_t checksum(const std::vector<uint8_t> &v) {
    uint32_t h = 2166136261u;
    for (auto b : v)
        h = (h ^ b) * 16777619u;
    return h;
}
uint8_t buttonBits(uint32_t b) {
    check(!(b & ~0x61u), "Unsupported replay input");
    return uint8_t((b & 1) | ((b >> 4) & 6));
}
uint32_t buttonMask(uint8_t b) { return (b & 1) | ((b & 6) << 4); }
} // namespace
// Every scalar has an explicit little-endian width; no pointers, padding or ABI dumps.
struct ReplayState::Fields {
#include "replay_fields.inc"
    template <class A> static void game(A &a, Game &g) {
        for (size_t n = 0; n < EntityPool::Capacity; ++n)
            a(g.pool_.at(n));
        a(g.player_, g.combatState_, g.grid_, g.worldState_, g.census_);
        int columns = g.announcement_.columns(), rows = g.announcement_.rows();
        a(columns, rows);
        check(columns >= 1 && columns <= 51 && rows >= 1 && rows <= 51,
              "Invalid replay announcement");
        if (a.reading)
            g.announcement_ = TileMap((columns - 1) * 8, (rows - 1) * 8);
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                auto cell = g.announcement_.get(x, y);
                a(cell);
                if (a.reading)
                    g.announcement_.put(x, y, cell);
            }
        auto r = g.random_.state(), e = g.effects_.state();
        a(r, e);
        g.random_ = GameRandom(r);
        g.effects_ = EffectRandom(e);
        a(g.phase_, g.stage_, g.lives_, g.frame_, g.stagesCleared_, g.deaths_, g.introTicks_,
          g.clearTicks_, g.backgroundAdvance_, g.beamX_, g.beamY_, g.beginPending_, g.arse_,
          g.endurance_, g.arseStarted_, g.stageBackgrounds_, g.colorPhase_, g.frameClearColor_,
          g.background_, g.nativeVisualBackground_, g.textPalette_, g.campaignNotice_,
          g.nativeVisualRandom_, g.nativeVisualEffects_, g.nativeVisualPhase_, g.nativeVisualFrame_,
          g.nativePhase_, g.pendingPressed_, g.pendingHeld_);
        auto &f = g.formations_;
        a(f.tuning_, f.baselineAdvance_, f.activeThreshold_, f.emitted_, f.requeueDistance_,
          f.levelIndex_);
        if (a.reading) {
            check(f.levelIndex_ < 50, "Invalid replay formation level");
            f.stage_ = &(g.arse_ ? aof::stages : original::stages).at(f.levelIndex_);
            f.program_.emplace(f.stage_->spawnProgram);
        }
        bool program = bool(f.program_);
        a(program);
        if (program) {
            check(bool(f.program_), "Replay spawn program missing");
            auto &p = *f.program_;
            uint64_t cursor = p.cursor_;
            a(cursor, p.loops_, p.ended_);
            p.cursor_ = size_t(cursor);
            check(cursor <= p.commands_.size(), "Invalid replay spawn cursor");
        } else
            f.program_.reset();
        a(g.world_.laserVisible_, g.world_.forecastAnchor_, g.world_.forecastAnchorValid_,
          g.world_.renderedGrid_);
        a(g.combat_.nativeHitConsumed_, g.combat_.nativeHitGeneration_);
        check(bool(g.native_), "Replay requires native clock");
        auto &n = *g.native_;
        a(n.tracks_, n.generations_, n.playerX_, n.playerY_, n.playerExpectedX_, n.playerExpectedY_,
          n.playerInitialized_);
    }
};

namespace {
template <class T> void Archive::one(T &v) {
    if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        constexpr size_t width = std::is_same_v<T, bool> ? 1 : sizeof(T) > 4 ? 8 : 4;
        uint64_t bits = 0;
        if (!reading) {
            if constexpr (std::is_floating_point_v<T>)
                std::memcpy(&bits, &v, sizeof(T));
            else
                bits = uint64_t(v);
        }
        for (size_t n = 0; n < width; ++n) {
            if (reading) {
                check(pos < bytes.size(), "Truncated replay state");
                bits |= uint64_t(bytes[pos++]) << (8 * n);
            } else
                bytes.push_back(uint8_t(bits >> (8 * n)));
        }
        if (reading) {
            if constexpr (std::is_floating_point_v<T>)
                std::memcpy(&v, &bits, sizeof(T));
            else if constexpr (std::is_same_v<T, bool>) {
                check(bits <= 1, "Invalid replay boolean");
                v = bits != 0;
            } else
                v = T(bits);
        }
    } else
        ReplayState::Fields::value(*this, v);
}
} // namespace

std::vector<uint8_t> ReplayState::capture(Game &g) {
    std::vector<uint8_t> out;
    out.reserve(2300000);
    Archive a{out};
    int steps = g.nativeSubsteps();
    bool aof = g.arse(), endurance = g.endurance();
    unsigned stage = g.stage();
    a(steps, aof, endurance, stage);
    Fields::game(a, g);
    return compress(out);
}
void ReplayState::restore(Game &g, const std::vector<uint8_t> &compressed) {
    auto raw = decompress(compressed);
    Archive a{raw, true};
    int steps = 0;
    bool aof = false, endurance = false;
    unsigned stage = 0;
    a(steps, aof, endurance, stage);
    check((steps == 3 || steps == 6) && stage < 50, "Unsupported replay starting state");
    g.arse(aof);
    g.endurance(endurance);
    g.setNativeRate(true, steps);
    g.start(stage);
    Fields::game(a, g);
    check(a.pos == raw.size() && g.stage_ == stage && g.nativePhase_ < unsigned(steps),
          "Invalid replay checkpoint");
    g.records_ = nullptr;
    g.deferPresentation(true);
    g.pool_.nativeMotion = g.native_.get();
}
void ReplayRecording::begin(Game &g, unsigned profile) {
    discard();
    check(profile < 5, "Invalid replay profile");
    initial_ = ReplayState::capture(g);
    info_.profile = profile;
    info_.aof = g.arse();
    info_.stage = g.stage();
    info_.maxStage = g.stage() + 1;
    inputs_.reserve(64 * 1024);
    active_ = true;
}
void ReplayRecording::append(const LogicalInput &i) {
    if (!active_)
        return;
    check(info_.ticks < MaximumTicks, "Replay recording capacity exceeded");
    check(i.x >= 0 && i.x < (info_.aof ? 400 : 240) && i.y >= 0 && i.y < (info_.aof ? 400 : 320),
          "Replay target out of bounds");
    auto b = uint8_t(buttonBits(i.held) | (buttonBits(i.pressed) << 3));
    if (b != buttons_) {
        inputs_.push_back(0xc0);
        inputs_.push_back(b);
        buttons_ = b;
    }
    int dx = i.x - x_, dy = i.y - y_;
    if (dx >= -4 && dx <= 3 && dy >= -4 && dy <= 3)
        inputs_.push_back(uint8_t((dx + 4) | ((dy + 4) << 3)));
    else if (dx >= -64 && dx <= 63 && dy >= -64 && dy <= 63) {
        unsigned w = unsigned(dx + 64) | (unsigned(dy + 64) << 7);
        inputs_.push_back(uint8_t(0x40 | (w >> 8)));
        inputs_.push_back(uint8_t(w));
    } else {
        unsigned w = unsigned(i.x) | (unsigned(i.y) << 9);
        inputs_.push_back(uint8_t(0x80 | (w >> 16)));
        inputs_.push_back(uint8_t(w));
        inputs_.push_back(uint8_t(w >> 8));
    }
    x_ = i.x;
    y_ = i.y;
    ++info_.ticks;
}
void ReplayRecording::checkpoint(Game &g) {
    if (!active_)
        return;
    auto state = ReplayState::capture(g);
    inputs_.push_back(0xc3);
    writeVarint(inputs_, uint32_t(state.size()));
    inputs_.insert(inputs_.end(), state.begin(), state.end());
}
void ReplayRecording::resetInput() {
    if (active_) {
        inputs_.push_back(0xc1);
    }
}
void ReplayRecording::reachedStage(unsigned stage) {
    if (active_ && stage < 50)
        info_.maxStage = std::max(info_.maxStage, uint32_t(stage + 1));
}
void ReplayRecording::finish(int score, bool completed) {
    if (active_) {
        info_.score = uint32_t(score);
        info_.completed = completed;
        active_ = false;
        pending_ = info_.ticks != 0;
    }
}
void ReplayRecording::discard() {
    active_ = pending_ = false;
    info_ = {};
    std::vector<uint8_t>().swap(initial_);
    std::vector<uint8_t>().swap(inputs_);
    x_ = y_ = 0;
    buttons_ = 0;
}
namespace {
void metadata(Archive &a, ReplayInfo &i) {
    a(i.ticks, i.score, i.stage, i.profile, i.maxStage, i.aof, i.completed);
}
std::vector<uint8_t> readFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    check(bool(in), "Cannot open replay");
    auto n = in.tellg();
    check(n > 0 && n <= std::streamoff(MaxFile), "Invalid replay size");
    std::vector<uint8_t> b(size_t(n), 0);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(b.data()), n);
    check(bool(in), "Cannot read replay");
    return b;
}
ReplayInfo header(Archive &a, uint32_t &stateSize, uint32_t &inputSize) {
    uint32_t magic = 0, version = 0, rules = 0;
    a(magic, version, rules);
    check(magic == 0x50525247 && version == 2 && rules == 0x1000002, "Unsupported replay version");
    ReplayInfo i;
    metadata(a, i);
    a(stateSize, inputSize);
    check(i.ticks && i.ticks <= ReplayRecording::MaximumTicks && i.profile < 5 && i.stage < 50 &&
              i.maxStage >= i.stage + 1 && i.maxStage <= 50 && stateSize <= MaxState &&
              inputSize <= MaxFile,
          "Invalid replay header");
    return i;
}
} // namespace
void ReplayRecording::save(const std::filesystem::path &path) const {
    check(pending_, "No replay to save");
    std::vector<uint8_t> b;
    Archive a{b};
    uint32_t magic = 0x50525247, version = 2, rules = 0x1000002;
    a(magic, version, rules);
    auto i = info_;
    metadata(a, i);
    uint32_t s = uint32_t(initial_.size()), n = uint32_t(inputs_.size());
    a(s, n);
    b.insert(b.end(), initial_.begin(), initial_.end());
    b.insert(b.end(), inputs_.begin(), inputs_.end());
    auto digest = checksum(b);
    a(digest);
    check(b.size() <= MaxFile, "Replay file too large");
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp";
    auto backup = path;
    backup += ".bak";
    struct RemoveTemp {
        std::filesystem::path file;
        ~RemoveTemp() {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    } cleanup{temp};
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        check(bool(out), "Cannot create replay");
        out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
        out.close();
        check(bool(out), "Cannot write replay");
    }
    std::error_code ec;
    bool existed = std::filesystem::exists(path);
    if (existed) {
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path, backup);
    }
    try {
        std::filesystem::rename(temp, path);
    } catch (...) {
        if (existed)
            std::filesystem::rename(backup, path, ec);
        throw;
    }
    if (existed)
        std::filesystem::remove(backup, ec);
}
ReplayInfo ReplayRecording::inspect(const std::filesystem::path &p) {
    std::vector<uint8_t> b(42);
    std::ifstream in(p, std::ios::binary);
    in.read(reinterpret_cast<char *>(b.data()), 42);
    check(bool(in), "Cannot read replay header");
    Archive a{b, true};
    uint32_t s = 0, n = 0;
    return header(a, s, n);
}
void ReplayPlayback::load(Game &g, const std::filesystem::path &path) {
    auto b = readFile(path);
    Archive a{b, true};
    uint32_t s = 0, n = 0;
    auto info = header(a, s, n);
    check(a.pos + s + n + 4 == b.size(), "Replay length mismatch");
    uint32_t expected = 0;
    auto payloadEnd = b.size() - 4;
    Archive footer{b, true, payloadEnd};
    footer(expected);
    b.resize(payloadEnd);
    check(checksum(b) == expected, "Replay checksum mismatch");
    std::vector<uint8_t> state(b.begin() + a.pos, b.begin() + a.pos + s);
    ReplayState::restore(g, state);
    check(g.arse() == info.aof, "Replay mode mismatch");
    info_ = info;
    bytes_ = std::move(b);
    position_ = a.pos + s;
    ticks_ = 0;
    highestStage_ = g.stage() + 1;
    x_ = y_ = 0;
    buttons_ = 0;
}
bool ReplayPlayback::step(Game &g) {
    auto &b = bytes_;
    auto &p = position_;
    auto &info = info_;
    auto &ticks = ticks_;
    auto &highestStage = highestStage_;
    auto &x = x_;
    auto &y = y_;
    auto &buttons = buttons_;
    auto byte = [&]() {
        check(p < b.size(), "Truncated replay input");
        return b[p++];
    };
    while (p < b.size()) {
        auto tag = byte();
        if (tag == 0xc0) {
            buttons = byte();
            check(buttons < 64, "Invalid replay buttons");
            continue;
        }
        if (tag == 0xc1) {
            g.cancelPendingInput();
            continue;
        }
        if (tag == 0xc3) {
            auto size = readVarint(b, p);
            check(size <= MaxState && size <= b.size() - p, "Invalid replay transition");
            std::vector<uint8_t> transition(b.begin() + p, b.begin() + p + size);
            ReplayState::restore(g, transition);
            p += size;
            continue;
        }
        check(tag < 0xc0, "Unknown replay event");
        if (tag < 0x40) {
            x += int(tag & 7) - 4;
            y += int(tag >> 3) - 4;
        } else if (tag < 0x80) {
            unsigned word = ((tag & 63) << 8) | byte();
            x += int(word & 127) - 64;
            y += int(word >> 7) - 64;
        } else {
            check((tag & 0x3c) == 0, "Invalid absolute replay position");
            unsigned word = unsigned(tag & 3) << 16;
            word |= byte();
            word |= unsigned(byte()) << 8;
            x = int(word & 511);
            y = int(word >> 9);
        }
        check(ticks < info.ticks && x >= 0 && x < (info.aof ? 400 : 240) && y >= 0 &&
                  y < (info.aof ? 400 : 320),
              "Invalid replay position or duration");
        g.step({x, y, buttonMask(buttons & 7), buttonMask(buttons >> 3)});
        highestStage = std::max(highestStage, uint32_t(g.stage() + 1));
        ++ticks;
        if (ticks < info.ticks)
            return true;
    }
    check(ticks == info.ticks && uint32_t(g.combatState().score) == info.score,
          "Replay score or duration diverged");
    check(highestStage == info.maxStage, "Replay highest stage diverged");
    check(!info.completed || g.phase() == GamePhase::GameOver, "Replay terminal state diverged");
    return false;
}
ReplayInfo ReplayRecording::play(Game &g, const std::filesystem::path &path) {
    ReplayPlayback playback;
    playback.load(g, path);
    while (playback.step(g)) {
    }
    return playback.info();
}
} // namespace grpp
