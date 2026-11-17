#include "grpp/clock.hpp"
#include "grpp/native_motion.hpp"
#include "grpp/options_menu.hpp"
#include <cstring>
#include <iostream>
#include <memory>
using namespace grpp;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
namespace grpp {
struct NativeRateTests {
    static void isolate(Game &g) {
        for (size_t i = 0; i < EntityPool::Capacity; ++i)
            g.pool_.at(i) = {};
        auto &p = g.pool_.at(EntityPool::Player);
        p.behavior = 1;
        p.flags = 1;
        p.x = fixed16(120);
        p.y = fixed16(240);
        g.player_.transitionTicks = 0;
        g.player_.shotCountdown = 100000;
        g.player_.worldX = p.x;
        g.player_.worldY = p.y;
        g.grid_ = {};
        g.worldState_ = {};
        g.phase_ = GamePhase::Playing;
        g.beginPending_ = false;
        if (g.native_)
            g.native_->reset();
        g.nativePhase_ = 0;
    }
    static Entity &entity(Game &g, size_t slot) { return g.pool_.at(slot); }
    static void run(Assets &assets) {
        auto old = std::make_unique<Game>(assets), fast = std::make_unique<Game>(assets);
        fast->setNativeRate(true, 6);
        old->start();
        fast->start();
        isolate(*old);
        isolate(*fast);
        old->player_.shotCountdown = fast->player_.shotCountdown = 0;
        int shotsA = 0, shotsB = 0;
        for (int i = 0; i < 300; ++i) {
            LogicalInput input{120, 240, i >= 20 && i < 22 ? 32u : 0u, i == 20 ? 32u : 0u};
            old->step(input);
            for (int s : old->sounds())
                shotsA += s == 6;
            for (int k = 0; k < 6; ++k) {
                if (k)
                    input.pressed = 0;
                fast->step(input);
                for (int s : fast->sounds())
                    shotsB += s == 6;
            }
            check(old->frame() == fast->frame(), "200 Hz source clock drift");
            check(old->randomState() == fast->randomState(), "200 Hz consumed extra source RNG");
            check(old->player_.bombTicks == fast->player_.bombTicks &&
                      old->player_.shotCountdown == fast->player_.shotCountdown,
                  "200 Hz bomb/autofire timing drift");
            for (size_t slot = 90; slot < 100; ++slot) {
                const auto &a = old->pool_.at(slot);
                const auto &b = fast->pool_.at(slot);
                if (a.behavior == 2 && a.drawing == 2)
                    check(std::memcmp(a.history.data(), b.history.data(), sizeof(a.history)) == 0,
                          "200 Hz particle payload/spacing/aging changed");
                check(a.behavior == b.behavior && a.x == b.x && a.y == b.y,
                      "200 Hz projectile transport/lifetime drift");
            }
        }
        check(shotsA == shotsB && shotsA > 50, "200 Hz firing rate changed");
        // Equal-time fixtures cover every free-moving active owner. Drawing is a
        // nonmutating sprite family here so this isolates integration semantics.
        for (int behavior : {3, 5, 6, 7, 8, 11, 13, 15, 16, 17})
            for (int variant = 0; variant < ((behavior == 16 || behavior == 17) ? 3 : 1);
                 ++variant) {
                old->start();
                fast->start();
                NativeRateTests::isolate(*old);
                NativeRateTests::isolate(*fast);
                size_t slot = behavior == 3    ? 78
                              : behavior == 8  ? 0
                              : behavior == 11 ? 12
                              : behavior == 13 ? 88
                                               : 1;
                for (Game *g : {old.get(), fast.get()}) {
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
                    old->step({120, 240, 0, 0});
                    for (int k = 0; k < 6; ++k)
                        fast->step({120, 240, 0, 0});
                    const auto &a = old->entities().at(slot);
                    const auto &b = fast->entities().at(slot);
                    if (a.behavior != b.behavior || a.x != b.x || a.y != b.y ||
                        a.depth != b.depth || a.vx != b.vx || a.vy != b.vy || a.scale != b.scale) {
                        std::cerr << "Owner " << behavior << " interval " << interval << '\n';
                        throw std::runtime_error("Active-owner elapsed-time motion changed");
                    }
                    check(old->randomState() == fast->randomState(),
                          "Motion owner changed random decision frequency");
                }
            }
        isolate(*fast);
        auto &t = fast->pool_.at(1);
        t.behavior = 16;
        t.drawing = 3;
        t.sprite = 274;
        t.flags = 1;
        t.x = fixed16(300);
        t.y = fixed16(400);
        t.commandOrHealth = 1000;
        auto &c = fast->pool_.at(88);
        c.behavior = 13;
        c.drawing = 13;
        c.x = t.x;
        c.y = t.y;
        for (int i = 0; i < 6; ++i)
            fast->step({120, 240, 0, 0});
        check(t.commandOrHealth == 999, "200 Hz multiplied contact damage");
        fast->step({120, 240, 0, 0});
        check(t.commandOrHealth == 998, "200 Hz damage budget did not reset");
        isolate(*fast);
        fast->step({120, 240, 0, 0});
        auto x = fast->pool_.at(89).x;
        fast->step({239, 240, 0, 0});
        check(fast->pool_.at(89).x > x, "200 Hz input waited for source tick");
        isolate(*fast);
        fast->grid_[16 + 30 * 30] = 0x01000001;
        fast->step({120, 240, 0, 0});
        auto cell = fast->grid_[16 + 30 * 30];
        for (int k = 1; k < 6; ++k)
            fast->step({239, 240, 0, 0});
        check(fast->player_.transitionTicks == 1 && fast->grid_[16 + 30 * 30] == cell,
              "200 Hz hazard missed contact or advanced source growth");
        isolate(*fast);
        fast->phase_ = GamePhase::StageClear;
        fast->clearTicks_ = 100;
        for (int i = 0; i < 594; ++i)
            fast->step({120, 240, 0, 0});
        check(fast->stage() == 0, "200 Hz interlude expired early");
        for (int i = 0; i < 6; ++i)
            fast->step({120, 240, 0, 0});
        check(fast->stage() == 1, "200 Hz interlude duration changed");
    }
};
} // namespace grpp
int main(int argc, char **argv) {
    try {
        check(argc == 3, "Usage: options_checks ASSETS TEMP");
        Assets assets;
        assets.load(argv[1]);
        auto pool = std::make_unique<EntityPool>();
        auto motion = std::make_unique<NativeMotion>(6);
        pool->nativeMotion = motion.get();
        auto &e = pool->at(1);
        e.behavior = 7;
        e.drawing = 10;
        for (int delta : {INT32_MIN, INT32_MAX, -100001, -5, -1, 0, 1, 5, 100001})
            for (int initial : {INT32_MIN, INT32_MAX, -12345, 0, 12345}) {
                motion->reset();
                motion->beginInterval();
                e.x = initial;
                pool->move(e, delta, 0);
                pool->move(e, delta, 0);
                for (int i = 1; i < 6; ++i)
                    motion->continueMove(*pool, 1);
                check(e.x == add32(initial, add32(delta, delta)),
                      "200 Hz signed remainder/wrap transport lost distance");
            }
        for (uint64_t hz : {30, 60, 120, 144, 165, 200, 240}) {
            FixedCadence clock(0, 5000000, 24);
            int count = 0;
            for (uint64_t i = 0; i <= hz * 30; ++i)
                count += clock.pending(i * 1000000000 / hz);
            check(count == 6001, "200 Hz schedule depends on display refresh");
        }
        NativeRateTests::run(assets);
        check(controllerGain(.18f) == 0 && std::abs(controllerGain(1) - 1.5f) < 1e-6,
              "Controller deadzone/full gain changed");
        check(std::abs(controllerGain(.59f) - .375f) < 1e-5,
              "Controller precision curve is not quadratic");
        for (int rotation : {1, 2}) {
            const auto p = orientationPointer(240.5f, 320.5f, 960, 720, rotation);
            check(rotation == 1 ? (p[0] == 320.5f && p[1] == 719.5f)
                                : (p[0] == 399.5f && p[1] == 240.5f),
                  "Orientation pointer inverse is incorrect");
        }
        for (int rotation : {1, 2})
            for (const auto target :
                 {std::array<float, 2>{120, 240}, {120, 264}, {216, 4}, {0, 0}, {239, 319}}) {
                const float vx = target[0] * 3 + .001f, vy = target[1] * 3 + .001f;
                const float px = rotation == 1 ? 960 - vy : vy, py = rotation == 1 ? vx : 720 - vx;
                const auto point = orientationPointer(px, py, 960, 720, rotation);
                float x, y;
                check(makeViewport(720, 960, Aspect::Fit).toLogical(point[0], point[1], x, y) &&
                          std::abs(x - target[0]) < .001f && std::abs(y - target[1]) < .001f,
                      "Rotated gameplay/menu pointer round trip failed");
            }
        OriginStick touch, coarse;
        touch.begin(0, 0, 720, 1280, 0);
        coarse.begin(0, 0, 720, 1280, 0);
        touch.move(120, 0);
        coarse.move(120, 0);
        double total = 0;
        for (int n = 0; n < 6; ++n)
            total += touch.tick(.005)[0];
        check(std::abs(total - coarse.tick(.03)[0]) < 1e-5,
              "Stick speed depends on simulation frequency");
        check(PlayerOptions::defaultOrientation(true) == 3 &&
                  PlayerOptions::defaultOrientation(false) == 0,
              "Default orientation must be Android Auto / desktop Upright");
        const int quarterTurns[] = {0, 1, 3, 2};
        for (int actual = 0; actual < 4; ++actual) {
            check(androidOrientationFallback(3, actual) == 0, "Auto must follow actual surface");
            for (int requested = 0; requested < 3; ++requested) {
                const int desired = requested == 0 ? 0 : requested == 1 ? 1 : 3;
                const int compensation = androidOrientationFallback(requested, actual);
                check((actual + quarterTurns[compensation]) % 4 == desired,
                      "Requested and actual orientation do not compose");
                if (actual == desired)
                    check(compensation == 0, "Honored request rotated twice");
            }
        }
        check(androidOrientationFallback(0, 1) == 2 && androidOrientationFallback(1, 1) == 0 &&
                  androidOrientationFallback(2, 1) == 3,
              "Force-landscape fallback incorrect");
        check(androidOrientationFallback(0, -1) == 0, "Unknown orientation guessed a rotation");
        const auto inverted = orientationPointer(20, 30, 640, 360, 3);
        check(inverted == std::array<float, 2>{620, 330}, "180-degree pointer inverse wrong");
        check(orientDelta(2, 3, 3) == std::array<float, 2>{-2, -3} && !orientationSwapsAxes(3),
              "180-degree delta or aspect wrong");
        for (int rotation = 0; rotation < 4; ++rotation)
            for (auto screen : {std::array<int, 2>{1280, 640}, {480, 960}})
                for (auto field : {std::array<int, 2>{240, 320}, {400, 400}}) {
                    const bool swap = orientationSwapsAxes(rotation);
                    const auto v =
                        makeViewport(swap ? screen[1] : screen[0], swap ? screen[0] : screen[1],
                                     Aspect::Fit, field[0], field[1]);
                    for (auto target : {std::array<float, 2>{0, 0},
                                        {float(field[0] - 1), float(field[1] - 1)},
                                        {field[0] * .37f, field[1] * .61f}}) {
                        const float x = v.x + target[0] * v.width / field[0],
                                    y = v.y + target[1] * v.height / field[1];
                        const float px = rotation == 1   ? screen[0] - y
                                         : rotation == 2 ? y
                                         : rotation == 3 ? screen[0] - x
                                                         : x;
                        const float py = rotation == 1   ? x
                                         : rotation == 2 ? screen[1] - x
                                         : rotation == 3 ? screen[1] - y
                                                         : y;
                        const auto inverse =
                            orientationPointer(px, py, screen[0], screen[1], rotation);
                        float lx, ly;
                        v.toLogical(inverse[0], inverse[1], lx, ly);
                        check(std::abs(lx - target[0]) < .001f && std::abs(ly - target[1]) < .001f,
                              "Compensated touch/menu coordinates do not round-trip");
                    }
                }
        PlayerOptions rotationOptions;
        check(PlayerOptions::orientationCount(true) == 4 &&
                  PlayerOptions::orientationCount(false) == 3,
              "Orientation choices do not follow platform");
        for (int mode = 0; mode < 4; ++mode) {
            rotationOptions.orientation = mode;
            check(rotationOptions.renderOrientation(true) == 0, "Android rotates twice");
            check(rotationOptions.renderOrientation(false) == (mode == 3 ? 0 : mode),
                  "Desktop rotation changed");
        }
        check(!controllerBackAllowed(FrontendView::Playing) &&
                  !controllerBackAllowed(FrontendView::Main) &&
                  !controllerBackAllowed(FrontendView::About) &&
                  !controllerBackAllowed(FrontendView::Players) &&
                  !controllerBackAllowed(FrontendView::Slot) &&
                  !controllerBackAllowed(FrontendView::Reader) &&
                  controllerBackAllowed(FrontendView::Level),
              "Controller B routing failed");
        PlayerOptions off;
        off.control = 4;
        check(!off.touchEnabled() && off.touchControlCount(true) == 4 &&
                  off.touchControlCount(false) == 4 && PlayerOptions{}.touchEnabled() &&
                  PlayerOptions{}.touchControlCount(false) == 3,
              "Touch Off must remain disabled without a secondary input");
        PlayerOptions settings;
        PauseMenu menu;
        menu.open();
        menu.animate();
        menu.rebuild(settings);
        FrontendInput i;
        i.vertical = 1;
        check(menu.input(i) == -1 && menu.selected() == 1, "Pause selection navigation failed");
        i = {};
        i.confirm = true;
        check(menu.input(i) == 1, "Graphics activation failed");
        i = {};
        i.back = true;
        check(menu.input(i) == 0, "Pause back failed");
        auto r = menu.bounds()[3];
        i = {};
        i.pointerActive = true;
        i.pointer = {(r[0] + r[2]) / 2, (r[1] + r[3]) / 2, 1, 1};
        check(menu.input(i) == -1, "Pause activated before release");
        i.pointer.held = 0;
        i.pointerReleased = true;
        check(menu.input(i) == 3, "Pause pointer release failed");
        std::filesystem::path path = std::filesystem::path(argv[2]) / "options.txt";
        for (auto mode :
             {DisplayMode::Windowed, DisplayMode::Borderless, DisplayMode::Fullscreen}) {
            check(platformDisplay(mode, false) == DisplayMode::Borderless,
                  "Android permits a non-borderless display mode");
            settings.display = mode;
            check(settings.save(path, false), "Android options save failed");
            PlayerOptions androidOptions;
            androidOptions.load(path, true);
            check(androidOptions.display == DisplayMode::Borderless,
                  "Android persists a non-borderless display mode");
            settings.save(path, true);
            androidOptions.load(path, false);
            check(androidOptions.display == DisplayMode::Borderless,
                  "Legacy display setting overrides Android borderless policy");
        }
        settings.original = true;
        settings.orientation = 2;
        settings.cap = 120;
        settings.control = 2;
        settings.display = DisplayMode::Borderless;
        check(settings.save(path), "Options save failed");
        PlayerOptions loaded;
        loaded.load(path);
        check(loaded.cap == 120 && loaded.control == 2 && loaded.original &&
                  loaded.orientation == 2 && loaded.display == DisplayMode::Borderless,
              "Options did not persist");
        std::ofstream(path) << "1 999 8 -1\n";
        loaded.load(path);
        check(loaded.cap == 120 && loaded.control == 2, "Invalid options replaced valid settings");
        std::ofstream(path) << "1 60 0 1\n";
        loaded.load(path);
        check(loaded.cap == 60 && !loaded.original && loaded.orientation == 0,
              "Version 1 options migration failed");
        std::ofstream(path) << "2 120 0 2 1\n";
        loaded.load(path);
        check(loaded.original && loaded.orientation == 0 && loaded.control == 2,
              "Version 2 options migration failed");
        std::ofstream(path) << "5 200 0 3 0 0 1\n";
        loaded.load(path);
        check(loaded.control == 3 && loaded.sensitivity == 10, "Default sensitivity failed");
        for (int rotation = 0; rotation < 4; ++rotation) {
            settings.orientation = rotation;
            settings.save(path);
            loaded.load(path);
            check(loaded.orientation == rotation, "Orientation selection did not persist");
        }
        for (int mode = 1; mode <= 4; ++mode)
            for (int gain = 5; gain <= 30; ++gain) {
                settings.control = mode;
                settings.sensitivity = gain;
                settings.save(path);
                loaded.load(path);
                check(loaded.control == mode && loaded.sensitivity == gain,
                      "Sensitivity/mode roundtrip failed");
            }
        std::ofstream(path) << "6 200 0 1 0 0 0 31\n";
        loaded.load(path);
        check(loaded.sensitivity == 30 && loaded.control == 4, "Invalid gain overwrote settings");
        std::cout << "200 Hz timing, transport, RNG, autofire, contacts, stage timing, display "
                     "cadence, touch and pause options pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
