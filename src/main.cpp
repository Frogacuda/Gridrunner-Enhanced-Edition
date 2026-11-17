#include "audio.hpp"
#include "gles_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/clock.hpp"
#include "grpp/frontend.hpp"
#include "grpp/game.hpp"
#include "grpp/mechanisms.hpp"
#include "grpp/options_menu.hpp"
#include "grpp/presentation_clock.hpp"
#include "grpp/touch_actions.hpp"
#include "original_tables.hpp"
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace grpp;
struct AppLifecycle {
    std::atomic<bool> active{true};
    static bool SDLCALL watch(void *context, SDL_Event *event) {
        auto &state = *static_cast<AppLifecycle *>(context);
        if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
            event->type == SDL_EVENT_DID_ENTER_BACKGROUND)
            state.active.store(false);
        else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
            state.active.store(true);
        return true;
    }
    AppLifecycle() { SDL_AddEventWatch(watch, this); }
    ~AppLifecycle() { SDL_RemoveEventWatch(watch, this); }
};
enum class SessionMode { Frontend, DirectPlay };

struct Options {
    std::filesystem::path assets, capture, saveDirectory;
    bool arse = false, endurance = false;
    int view = -1, windowWidth = 720, windowHeight = 960, presentationHz = 0;
    bool windowed = false, hidden = false, startPaused = false, noSave = false, audio = true,
         reference = false, help = false;
    int frames = 0;
    SessionMode mode = SessionMode::Frontend;
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/fields.inc"
#endif
};
Options options(int argc, char **argv) {
    Options o;
    const char *base = SDL_GetBasePath();
    o.assets = std::filesystem::path(base ? base : ".") / "assets";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> const char * {
            if (++i >= argc)
                throw std::runtime_error("Missing value for " + arg);
            return argv[i];
        };
        if (arg == "--help" || arg == "-h")
            o.help = true;
        else if (arg == "--assets")
            o.assets = value();
        else if (arg == "--draw-to-edge")
            o.view = 1;
        else if (arg == "--letterbox")
            o.view = 0;
        else if (arg == "--window-size") {
            o.windowWidth = std::stoi(value());
            o.windowHeight = std::stoi(value());
            if (o.windowWidth < 64 || o.windowHeight < 64 || o.windowWidth > 8192 ||
                o.windowHeight > 8192)
                throw std::runtime_error("Invalid window size");
        } else if (arg == "--fps") {
            o.presentationHz = std::stoi(value());
            if (o.presentationHz < 0 || o.presentationHz > 1000)
                throw std::runtime_error("--fps expects 0 (auto) or 10..1000");
            if (o.presentationHz > 0 && o.presentationHz < 10)
                throw std::runtime_error("--fps expects 0 (auto) or 10..1000");
        } else if (arg == "--hidden")
            o.hidden = true;
        else if (arg == "--paused") {
            o.startPaused = true;
            o.mode = SessionMode::DirectPlay;
        }
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/parse.inc"
#endif
        else if (arg == "--windowed")
            o.windowed = true;
        else if (arg == "--save-dir")
            o.saveDirectory = value();
        else if (arg == "--no-save")
            o.noSave = true;
        else if (arg == "--aof") {
            o.arse = true;
            o.mode = SessionMode::DirectPlay;
        } else if (arg == "--endurance") {
            o.endurance = true;
            o.mode = SessionMode::DirectPlay;
        } else if (arg == "--play")
            o.mode = SessionMode::DirectPlay;
        else if (arg == "--reference")
            o.reference = true;
        else if (arg == "--capture")
            o.capture = value();
        else if (arg == "--frames")
            o.frames = std::stoi(value());
        else if (arg == "--no-audio")
            o.audio = false;
        else
            throw std::runtime_error("Unknown argument: " + arg);
    }
    return o;
}
struct Session {
    Assets assets;
    Game game{assets};
    std::unique_ptr<Frontend> frontend;
    Canvas canvas;
    InputLatch latch;
    int32_t tick = 0;
    float colorPhase = 0;
    unsigned presentNumerator = 60, presentDenominator = 1;
    SessionMode mode = SessionMode::Frontend;
    float pointerX = 120, pointerY = 240;
    PlayerOptions settings;
    PauseMenu pauseMenu;
    OriginStick originStick;
    RelativeTouch relativeTouch;
    bool hasTouch = false;
    int androidSurfaceTurns = -1;
    int renderOrientation() const {
#ifdef __ANDROID__
        return androidOrientationFallback(settings.orientation, androidSurfaceTurns);
#else
        return settings.renderOrientation();
#endif
    }
    bool pointerPause = false;
    float menuX = 120, menuY = 160;
    bool paused = false, reference = false;
    Aspect aspect = Aspect::Fit;
    SDL_Gamepad *pad = nullptr;
    SDL_FingerID activeFinger = 0;
    bool touching = false;
    bool suppressMenuRelease = false;
    bool bombHeld = false;
    TouchActions touchActions;
    bool blockSouth = false;

    const Canvas &activeCanvas() const {
        return mode == SessionMode::Frontend && frontend ? frontend->canvas()
               : mode == SessionMode::DirectPlay         ? game.canvas()
                                                         : canvas;
    }
    int fieldWidth() const { return activeCanvas().width(); }
    int fieldHeight() const { return activeCanvas().height(); }
    bool playing() const {
        return mode == SessionMode::DirectPlay ||
               (mode == SessionMode::Frontend && frontend &&
                (frontend->view() == FrontendView::Playing || frontend->watchingReplay()));
    }
    void draw() {
        const bool referenceBackground = reference,
                   ordinaryActors = !settings.expanded || !playing();
        if (mode == SessionMode::Frontend)
            frontend->preparePresentation(referenceBackground, ordinaryActors);
        else if (mode == SessionMode::DirectPlay)
            game.renderNative(referenceBackground, ordinaryActors);
        canvas.clear();
        if (!settings.expanded || !playing())
            canvas = mode == SessionMode::Frontend ? frontend->canvas() : game.canvas();
        if (settings.expanded && playing())
            canvas = mode == SessionMode::DirectPlay ? game.expandedCanvas()
                                                     : frontend->expandedCanvas();
        if (mode == SessionMode::DirectPlay && game.phase() == GamePhase::GameOver) {
            canvas.fill({8, 276, 224, 20}, 0);
            canvas.text("TAP / ENTER / A TO RESTART", 16, 282);
        }
        if (paused) {
            Canvas overlay;
            pauseMenu.draw(overlay);
            canvas.centeredUi(overlay);
        } else if (hasTouch && settings.touchEnabled() &&
                   (mode == SessionMode::DirectPlay ||
                    (mode == SessionMode::Frontend &&
                     (frontend->view() == FrontendView::Playing || frontend->watchingReplay())))) {
            const auto first = canvas.draws().size();
            TileMap button(canvas.width(), canvas.height());
            button.text("PAUSE", canvas.width() / 8 - 6, 0, 66);
            std::vector<uint32_t> palette(129, cycleColor(original::cycles[0], colorPhase));
            button.draw(canvas, palette);
            canvas.anchorSince(first, HudAnchor::TopRight);
        }
    }
};
float axis(SDL_Gamepad *pad, SDL_GamepadAxis axis) {
    return pad ? std::clamp(SDL_GetGamepadAxis(pad, axis) / 32767.f, -1.f, 1.f) : 0;
}
int run(const Options &opt) {
    if (opt.help) {
        std::puts("Gridrunner++ Enhanced 1.1.0\n"
                  "--assets PATH       Asset directory\n"
                  "--save-dir PATH     Save/settings directory\n"
                  "--windowed          Recover in a window\n"
                  "--no-audio          Disable sound\n"
                  "--reference         Original graphics\n"
                  "--letterbox         Preserve the original viewport\n"
                  "--draw-to-edge      Fill the display beyond that viewport\n"
                  "--window-size W H   Initial desktop window size\n"
                  "--fps N             Presentation cap (0 = automatic)\n"
                  "--play / --aof      Start Standard / Arse on Fire directly\n"
                  "--endurance         Start an endurance run\n"
                  "--paused            Start a direct run paused\n"
                  "--hidden --frames N --capture PATH --no-save: headless capture controls");
        return 0;
    }

#ifdef GRPP_APP_CHECKS
    if (opt.editionTest)
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
#endif
    // Finger input is handled directly; do not also synthesize mouse clicks.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetAppMetadata("Gridrunner++ Enhanced", "1.1.0", "org.llamasoft.grpp.enhanced");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (opt.audio ? SDL_INIT_AUDIO : 0)))
        throw std::runtime_error(SDL_GetError());
    AppLifecycle lifecycle;
    struct SdlGuard {
        ~SdlGuard() { SDL_Quit(); }
    } sdlGuard;
    auto session = std::make_unique<Session>();
    auto &s = *session;
    s.mode = opt.mode;
    s.reference = opt.reference;
    s.assets.load(opt.assets);
    std::filesystem::path saveFile;
    if (!opt.noSave) {
        auto directory = opt.saveDirectory;
        if (directory.empty()) {
            char *path = SDL_GetPrefPath("Llamasoft", "GridrunnerPlusPlus-Enhanced");
            if (!path)
                throw std::runtime_error(SDL_GetError());
            directory = path;
            SDL_free(path);
        }
        saveFile = directory / "hst.txt";
    }
#ifdef GRPP_APP_CHECKS
    bool secondaryInputDetectedForTest = true;
#endif
    auto secondaryInputAvailable = [&]() {
#ifdef GRPP_APP_CHECKS
        if (opt.touchOffTest && !secondaryInputDetectedForTest)
            return false;
#endif
        return s.pad || SDL_HasKeyboard() || SDL_HasMouse();
    };
    auto touchEnabled = [&]() { return s.settings.touchEnabled(); };
    auto mouseInputAllowed = [&](SDL_MouseID id) {
        return id != SDL_TOUCH_MOUSEID && (id != SDL_PEN_MOUSEID || touchEnabled());
    };
    auto touchControl = [&]() { return s.settings.control == 4 ? 1 : s.settings.control; };
    auto directTouchAvailable = [&]() {
#ifdef GRPP_APP_CHECKS
        if (opt.backTest)
            return false;
#endif
        int count = 0;
        auto *devices = SDL_GetTouchDevices(&count);
        bool direct = false;
        for (int n = 0; n < count; ++n)
            direct |= SDL_GetTouchDeviceType(devices[n]) == SDL_TOUCH_DEVICE_DIRECT;
        SDL_free(devices);
#ifdef GRPP_APP_CHECKS
        direct = direct || opt.pauseTest || opt.touchTest || opt.viewTest || opt.touchOffTest;
#endif
        return direct;
    };
    s.hasTouch = directTouchAvailable();
    s.frontend = std::make_unique<Frontend>(s.assets, saveFile);
    s.frontend->editionMenu(true);
    s.game.arse(opt.arse);
    s.game.endurance(opt.endurance);
    s.game.stageBackgrounds(true);
    s.frontend->setNativeRate(true, 6);
    s.frontend->enableReplays(!opt.noSave);
    s.game.setNativeRate(true, 6);
    s.frontend->deferPresentation(true);
    s.game.deferPresentation(true);
    const auto optionsFile =
        saveFile.empty() ? std::filesystem::path{} : saveFile.parent_path() / "options.txt";
    if (!optionsFile.empty())
        s.settings.load(optionsFile);
    auto displayPendingFile = optionsFile;
    if (!displayPendingFile.empty())
        displayPendingFile += ".display-pending";
    const bool interruptedDisplay =
        !displayPendingFile.empty() && std::filesystem::exists(displayPendingFile);
    if (DisplaySelectionAvailable && (opt.windowed || interruptedDisplay)) {
        s.settings.display = DisplayMode::Windowed;
        s.settings.save(optionsFile);
        SDL_Log("Display recovery: starting windowed");
    }
    if (opt.view >= 0)
        s.settings.expanded = opt.view != 0;
    if (opt.presentationHz)
        s.settings.cap = opt.presentationHz;
    if (opt.reference)
        s.settings.original = true;
    s.reference = s.settings.original;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_Window *window =
        SDL_CreateWindow("Gridrunner++ Enhanced", opt.windowWidth, opt.windowHeight,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                             (!DisplaySelectionAvailable ? SDL_WINDOW_FULLSCREEN : 0) |
                             (opt.hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!window)
        throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> windowGuard(window,
                                                                          SDL_DestroyWindow);
    if (DisplaySelectionAvailable) {
        if (auto *icon = SDL_LoadBMP((opt.assets / "app-icon.bmp").string().c_str())) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }
    struct DesktopCursorGuard {
        ~DesktopCursorGuard() {
            if (DisplaySelectionAvailable)
                SDL_ShowCursor();
        }
    } cursorGuard;
    GlesRenderer renderer(window, s.assets);
    renderer.frameQueue(1);
    if (!SDL_GL_SetSwapInterval(0))
        throw std::runtime_error("Native input clock requires nonblocking swap: " +
                                 std::string(SDL_GetError()));
    std::unique_ptr<Audio> audio;
    if (opt.audio)
        audio = std::make_unique<Audio>(opt.assets, s.assets.arse());
    if (audio && s.mode == SessionMode::Frontend)
        for (int sound : s.frontend->sounds())
            audio->play(size_t(sound));
    bool audioArse = s.assets.arse();
    int gamepadCount = 0;
    SDL_JoystickID *pads = SDL_GetGamepads(&gamepadCount);
    if (gamepadCount > 0)
        s.pad = SDL_OpenGamepad(pads[0]);
    SDL_free(pads);
    struct PadGuard {
        Session &session;
        ~PadGuard() {
            if (session.pad)
                SDL_CloseGamepad(session.pad);
        }
    } padGuard{s};
    if (s.mode == SessionMode::DirectPlay) {
        s.game.start();
        renderer.reloadPages(s.assets);
    }
    SDL_Log("Renderer: %s", renderer.driver().c_str());
    bool running = true, focused = true, pointerActive = true;
    bool cursorKnown = false, cursorShown = false, mouseInside = SDL_GetMouseFocus() == window;
    bool full = false;
    int rendered = 0;
    FrontendInput menuInput;
    bool pointerHeld = false, alternateHeld = false;
    int stickMenuX = 0, stickMenuY = 0;
    constexpr int steps = 6;
    FixedCadence cadence(SDL_GetTicksNS(), FixedCadence::Period / steps, 4 * steps);
    FixedCadence pauseCadence(SDL_GetTicksNS());
    PresentationClock presentation(SDL_GetTicksNS());
    Uint64 nextRefreshCheck = 0;
    unsigned menuPhase = 0;
    double decorativeOffset = 0;
    double decorativeCameraFraction = 0;
    DecorativeCamera decorativeCamera;
    auto measureDecoration = [&]() {
        const double fraction = cadence.fraction(SDL_GetTicksNS());
        const bool menu = s.mode == SessionMode::Frontend &&
                          s.frontend->view() != FrontendView::Playing &&
                          !s.frontend->watchingReplay();
        decorativeOffset =
            (menu ? double((menuPhase + steps - 1) % steps) + fraction : fraction) / steps;
        decorativeCameraFraction = menu ? decorativeOffset : fraction;
    };
    auto freezeDecoration = [&]() {
        if (focused && !s.paused)
            measureDecoration();
    };

    auto togglePause = [&]() {
        if (s.mode == SessionMode::Frontend && s.frontend->view() != FrontendView::Playing &&
            !s.frontend->watchingReplay())
            return;
        freezeDecoration();
        s.paused = !s.paused;
        cadence.reset(SDL_GetTicksNS());
        s.touchActions.reset();
        s.latch.reset();
        s.bombHeld = false;
        s.touching = false;
        s.originStick.cancel();
        s.relativeTouch.cancel();
        pointerHeld = alternateHeld = false;
        s.blockSouth = true;
        menuInput = {};
        s.frontend->cancelInput();
        s.game.cancelPendingInput();
        if (s.paused) {
            s.pauseMenu.open();
            s.pauseMenu.animate();
            s.pauseMenu.rebuild(s.settings, s.hasTouch, DisplaySelectionAvailable);
            pauseCadence.reset(SDL_GetTicksNS());
            pointerActive = false;
        }
        if (audio)
            audio->stop();
    };
    int windowX = 0, windowY = 0, windowW = 720, windowH = 960;
    SDL_GetWindowPosition(window, &windowX, &windowY);
#ifdef GRPP_APP_CHECKS
    bool rejectNextDisplay = false;
#endif
    bool displayChangePending = interruptedDisplay;
    auto clearDisplayPending = [&]() {
        if (!displayPendingFile.empty()) {
            std::error_code error;
            std::filesystem::remove(displayPendingFile, error);
        }
        displayChangePending = false;
    };
    auto applyDisplay = [&](DisplayMode desired) {
        desired = platformDisplay(desired);
        if (!DisplaySelectionAvailable) {
            s.settings.display = DisplayMode::Borderless;
            full = true;
            return SDL_SetWindowFullscreenMode(window, nullptr) &&
                   SDL_SetWindowFullscreen(window, true);
        }
        const auto old = s.settings.display;
        if (old == DisplayMode::Windowed) {
            SDL_GetWindowPosition(window, &windowX, &windowY);
            SDL_GetWindowSize(window, &windowW, &windowH);
        }
        if (DisplaySelectionAvailable && desired != DisplayMode::Windowed) {
            displayChangePending = true;
            if (!displayPendingFile.empty()) {
                std::ofstream pending(displayPendingFile);
                pending << int(desired) << '\n';
            }
        }
        auto apply = [&](DisplayMode mode) {
#ifdef GRPP_APP_CHECKS
            if (rejectNextDisplay && mode != DisplayMode::Windowed) {
                rejectNextDisplay = false;
                SDL_SetError("Simulated driver rejection");
                return false;
            }
#endif
            // Complete the previous transition before changing its mode or geometry.
            // Windows can otherwise leave an exclusive window minimized/unfocused.
            SDL_SetWindowRelativeMouseMode(window, false);
            const auto display = SDL_GetDisplayForWindow(window);
            SDL_DisplayMode current{};
            if (mode == DisplayMode::Fullscreen) {
                const auto *available = SDL_GetCurrentDisplayMode(display);
                if (!available)
                    return false;
                current = *available;
            }
            if (!SDL_SetWindowFullscreen(window, false) || !SDL_SyncWindow(window))
                return false;
            if (!opt.hidden) {
                SDL_ShowWindow(window);
                SDL_RestoreWindow(window);
                SDL_SyncWindow(window);
                SDL_RaiseWindow(window);
            }
            bool applied;
            if (mode == DisplayMode::Windowed)
                applied = SDL_SetWindowFullscreenMode(window, nullptr) &&
                          SDL_SetWindowBordered(window, true) &&
                          SDL_SetWindowSize(window, windowW, windowH) &&
                          SDL_SetWindowPosition(window, windowX, windowY);
            else
                applied = SDL_SetWindowFullscreenMode(
                              window, mode == DisplayMode::Fullscreen ? &current : nullptr) &&
                          SDL_SetWindowFullscreen(window, true);
            if (!applied || !SDL_SyncWindow(window))
                return false;
            if (!opt.hidden) {
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                    SDL_RestoreWindow(window);
                SDL_ShowWindow(window);
                SDL_RaiseWindow(window);
                SDL_SyncWindow(window);
            }
            if (!renderer.windowChanged())
                return false;
            const auto flags = SDL_GetWindowFlags(window);
            return bool(flags & SDL_WINDOW_FULLSCREEN) == (mode != DisplayMode::Windowed) &&
                   (opt.hidden || !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)));
        };
        if (!apply(desired)) {
            SDL_Log("Display transition failed: %s; recovering windowed", SDL_GetError());
            apply(DisplayMode::Windowed);
            s.settings.display = DisplayMode::Windowed;
            full = false;
            s.settings.save(optionsFile);
            clearDisplayPending();
            s.pauseMenu.message = "Display change failed - windowed";
            return false;
        }
        s.settings.display = desired;
        full = desired != DisplayMode::Windowed;
        nextRefreshCheck = 0;
        cadence.reset(SDL_GetTicksNS());
        cursorKnown = false;
        return true;
    };
    auto applyOrientation = [&]() {
#ifdef __ANDROID__
        // SDL dispatches custom messages to the Activity on the UI thread.
        if (!SDL_SendAndroidMessage(0x8052, s.settings.orientation)) {
            s.pauseMenu.message = "Orientation change failed";
            return false;
        }
#endif
        return true;
    };
    applyOrientation();
    const auto startupDisplay = s.settings.display;
    bool startupDisplayPending =
        DisplaySelectionAvailable && startupDisplay != DisplayMode::Windowed;
    if (!DisplaySelectionAvailable)
        applyDisplay(DisplayMode::Borderless);
    else
        s.settings.display = DisplayMode::Windowed;
    auto selectSession = [&](SessionMode value) {
        s.mode = value;
        if (value == SessionMode::DirectPlay) {
            s.game.start();
            s.paused = false;
            s.blockSouth = true;
            s.latch.reset();
            renderer.reloadPages(s.assets);
            if (audio)
                for (int sound : s.game.sounds())
                    audio->play(size_t(sound));
        }
    };
    auto routeBack = [&]() {
        pointerActive = false;
        if (s.playing() &&
            (s.mode == SessionMode::Frontend ? s.frontend->game() : s.game).phase() !=
                GamePhase::GameOver)
            togglePause();
        else if (s.mode == SessionMode::Frontend)
            menuInput.back = true;
        else
            selectSession(SessionMode::Frontend);
    };
    float lastPointerNX = .5f, lastPointerNY = .75f;
    auto pointer = [&](float px, float py, bool normalized) -> bool {
        s.pointerPause = false;
        int ww = 0, wh = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (!ww || !wh)
            return false;
        const float pixelX = normalized ? px * pw : px * float(pw) / ww;
        const float pixelY = normalized ? py * ph : py * float(ph) / wh;
        float lx = 0, ly = 0;
        const bool onScreen = pixelX >= 0 && pixelY >= 0 && pixelX < pw && pixelY < ph;
        if (!onScreen && (!s.playing() || s.paused))
            return false;
        const auto point = orientationPointer(pixelX, pixelY, pw, ph, s.renderOrientation());
        const auto viewport = makeViewport(orientationSwapsAxes(s.renderOrientation()) ? ph : pw,
                                           orientationSwapsAxes(s.renderOrientation()) ? pw : ph,
                                           s.aspect, s.fieldWidth(), s.fieldHeight());
        const bool inside = viewport.toLogical(point[0], point[1], lx, ly);
        const bool pauseCorner =
            s.hasTouch && touchEnabled() && onScreen && s.settings.expanded && s.playing() &&
            !s.paused &&
            point[0] >= (orientationSwapsAxes(s.renderOrientation()) ? ph : pw) -
                            48.f * viewport.width / s.fieldWidth() &&
            point[1] >= 0 && point[1] < 16.f * viewport.height / s.fieldHeight();
        if (pauseCorner) {
            lx = float(s.fieldWidth() - 24);
            ly = 8;
        }
        s.pointerPause = s.hasTouch && touchEnabled() && s.playing() && !s.paused &&
                         (s.settings.expanded ? pauseCorner
                                              : (inside && lx >= s.fieldWidth() - 48 &&
                                                 lx < s.fieldWidth() && ly >= 0 && ly < 16));
        if (inside || pauseCorner || (s.playing() && !s.paused)) {
            lastPointerNX = pixelX / pw;
            lastPointerNY = pixelY / ph;
            s.menuX = lx;
            s.menuY = ly;
            if (!s.pointerPause && !s.paused &&
                !(s.playing() && touchControl() >= 2 && s.touching)) {
                s.pointerX = std::clamp(lx, 0.f, float(s.fieldWidth() - 1));
                s.pointerY = std::clamp(ly, 0.f, float(s.fieldHeight() - 1));
            }
            pointerActive = true;
            return true;
        }
        pointerActive = false;
        s.menuX = s.menuY = -1000;
        if (s.mode == SessionMode::Frontend)
            s.frontend->cancelInput();
        return false;
    };
    bool mouseCaptured = false;
    auto liveGameplay = [&]() {
        return s.playing() &&
               (s.mode == SessionMode::Frontend ? s.frontend->game() : s.game).phase() ==
                   GamePhase::Playing;
    };
    auto captureMouse = [&]() {
        const bool desired = gameplayMouseCapture(DisplaySelectionAvailable, !opt.hidden, focused,
                                                  s.paused, liveGameplay());
        if (desired == mouseCaptured)
            return;
        if (SDL_SetWindowRelativeMouseMode(window, desired)) {
            mouseCaptured = desired;
            s.pointerPause = false;
            SDL_SetWindowMouseGrab(window, desired);
            if (!desired)
                SDL_CaptureMouse(false);
            else {
                const auto &p = (s.mode == SessionMode::Frontend ? s.frontend->game() : s.game)
                                    .entities()
                                    .at(EntityPool::Player);
                s.pointerX = float(p.x) / 65536;
                s.pointerY = float(p.y) / 65536;
            }
        }
    };
    auto relativeMouse = [&](float dx, float dy) {
        int ww = 0, wh = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (!ww || !wh)
            return;
        const auto d = orientDelta(dx * pw / ww, dy * ph / wh, s.renderOrientation());
        const auto v = makeViewport(orientationSwapsAxes(s.renderOrientation()) ? ph : pw,
                                    orientationSwapsAxes(s.renderOrientation()) ? pw : ph, s.aspect,
                                    s.fieldWidth(), s.fieldHeight());
        s.pointerX = std::clamp(s.pointerX + d[0] * s.fieldWidth() / v.width * s.settings.gain(),
                                0.f, float(s.fieldWidth() - 1));
        s.pointerY = std::clamp(s.pointerY + d[1] * s.fieldHeight() / v.height * s.settings.gain(),
                                0.f, float(s.fieldHeight() - 1));
        pointerActive = false;
        s.pointerPause = false;
    };
    bool surfaceOrientationDirty = true;
    int lastRenderOrientation = s.renderOrientation();
    auto syncSurfaceOrientation = [&]() {
#ifdef __ANDROID__
        if (surfaceOrientationDirty || s.androidSurfaceTurns < 0) {
            switch (SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(window))) {
            case SDL_ORIENTATION_PORTRAIT:
                s.androidSurfaceTurns = 0;
                break;
            case SDL_ORIENTATION_LANDSCAPE:
                s.androidSurfaceTurns = 1;
                break;
            case SDL_ORIENTATION_PORTRAIT_FLIPPED:
                s.androidSurfaceTurns = 2;
                break;
            case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
                s.androidSurfaceTurns = 3;
                break;
            default:
                break; // Keep the last measured orientation while the surface changes.
            }
            surfaceOrientationDirty = false;
        }
#endif
        const int rotation = s.renderOrientation();
        if (rotation != lastRenderOrientation) {
            s.touching = false;
            s.originStick.cancel();
            s.relativeTouch.cancel();
            s.touchActions.reset();
            pointerHeld = false;
            pointerActive = false;
            s.bombHeld = false;
            lastRenderOrientation = rotation;
        }
    };
    syncSurfaceOrientation();
    if (opt.startPaused)
        togglePause();
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/setup.inc"
#endif
    while (running && (!opt.frames || rendered < opt.frames)) {
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/step.inc"
#endif
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_DISPLAY_ORIENTATION:
                surfaceOrientationDirty = true;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                surfaceOrientationDirty = true;
                if (s.touching) {
                    s.touching = false;
                    s.originStick.cancel();
                    s.relativeTouch.cancel();
                    s.touchActions.reset();
                    pointerHeld = false;
                    s.bombHeld = false;
                }
                break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                surfaceOrientationDirty = true;
                nextRefreshCheck = 0;
                break;
            case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
                nextRefreshCheck = 0;
                break;
            case SDL_EVENT_QUIT:
                if (s.mode == SessionMode::Frontend && !opt.noSave) {
                    if (s.paused)
                        togglePause();
                    s.suppressMenuRelease = pointerHeld || s.touching;
                    pointerHeld = false;
                    s.bombHeld = false;
                    s.touching = false;
                    s.touchActions.reset();
                    s.originStick.cancel();
                    s.relativeTouch.cancel();
                    s.latch.reset();
                    running = !s.frontend->requestQuit();
                } else
                    running = false;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                freezeDecoration();
                s.originStick.cancel();
                focused = false;
                s.touchActions.reset();
                s.latch.reset();
                s.bombHeld = false;
                s.touching = false;
                pointerHeld = alternateHeld = false;
                menuInput = {};
                s.frontend->cancelInput();
                s.game.cancelPendingInput();
                if (audio)
                    audio->stop();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                surfaceOrientationDirty = true;
                focused = true;
                if (DisplaySelectionAvailable)
                    renderer.windowChanged();
                cursorKnown = false;
                mouseInside = SDL_GetMouseFocus() == window;
                cadence.reset(SDL_GetTicksNS());
                nextRefreshCheck = 0;
                break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                mouseInside = true;
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                mouseInside = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (mouseInputAllowed(event.motion.which)) {
                    mouseInside = true;
                    if (mouseCaptured)
                        relativeMouse(event.motion.xrel, event.motion.yrel);
                    else
                        pointer(event.motion.x, event.motion.y, false);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!mouseInputAllowed(event.button.which))
                    break;
                s.suppressMenuRelease = false;
                mouseInside = true;
                if (!mouseCaptured && !pointer(event.button.x, event.button.y, false) &&
                    (s.paused || !s.playing()))
                    break;
                if (s.paused) {
                    if (event.button.button == SDL_BUTTON_LEFT)
                        pointerHeld = true;
                    break;
                }
                if (s.pointerPause) {
                    togglePause();
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    s.bombHeld = true;
                    if (s.playing())
                        s.latch.press(0x20u);
                } else if (s.mode == SessionMode::DirectPlay &&
                           s.game.phase() == GamePhase::GameOver)
                    selectSession(SessionMode::DirectPlay);
                else if (s.mode == SessionMode::Frontend || s.playing())
                    pointerHeld = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!mouseInputAllowed(event.button.which))
                    break;
                if (s.suppressMenuRelease && event.button.button == SDL_BUTTON_LEFT) {
                    s.suppressMenuRelease = false;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT)
                    s.bombHeld = false;
                else {
                    const bool inside =
                        !mouseCaptured && pointer(event.button.x, event.button.y, false);
                    pointerHeld = false;
                    menuInput.pointerReleased = inside;
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
                if (!touchEnabled())
                    break;
                s.hasTouch = directTouchAvailable();
                if (s.paused) {
                    if (pointer(event.tfinger.x, event.tfinger.y, true))
                        pointerHeld = true;
                    break;
                }
                if (!s.touching) {
                    s.touching = true;
                    s.activeFinger = event.tfinger.fingerID;
                    if (!pointer(event.tfinger.x, event.tfinger.y, true)) {
                        s.touching = false;
                        break;
                    }
                    if (s.pointerPause) {
                        togglePause();
                        break;
                    }
                    if (s.playing() && touchControl() == 3) {
                        int w = 0, h = 0;
                        SDL_GetWindowSizeInPixels(window, &w, &h);
                        s.originStick.begin(event.tfinger.x * w, event.tfinger.y * h, w, h,
                                            s.renderOrientation());
                    }
                    if (s.playing() && touchControl() >= 2) {
                        const auto &g =
                            s.mode == SessionMode::Frontend ? s.frontend->game() : s.game;
                        const auto &p = g.entities().at(EntityPool::Player);
                        s.relativeTouch.begin(s.menuX, s.menuY, float(p.x) / 65536,
                                              float(p.y) / 65536, s.fieldWidth(), s.fieldHeight());
                        const auto target =
                            s.relativeTouch.target(s.menuX, s.menuY, s.settings.gain());
                        s.pointerX = target[0];
                        s.pointerY = target[1];
                    }
                    if (s.mode == SessionMode::DirectPlay && s.game.phase() == GamePhase::GameOver)
                        selectSession(SessionMode::DirectPlay);
                    else if (s.mode == SessionMode::Frontend || s.playing())
                        pointerHeld = true;
                } else if (s.playing() &&
                           (s.mode == SessionMode::Frontend ? s.frontend->game().arse()
                                                            : s.game.arse()))
                    s.touchActions.down(event.tfinger.fingerID, SDL_GetTicks(), event.tfinger.x,
                                        event.tfinger.y);
                else {
                    s.bombHeld = true;
                    if (s.playing())
                        s.latch.press(0x20u);
                }
                break;
            case SDL_EVENT_FINGER_MOTION:
                if (!touchEnabled())
                    break;
                s.touchActions.move(event.tfinger.fingerID, event.tfinger.x, event.tfinger.y);
                if (s.paused) {
                    pointer(event.tfinger.x, event.tfinger.y, true);
                    break;
                }
                if (s.touching && s.activeFinger == event.tfinger.fingerID) {
                    pointer(event.tfinger.x, event.tfinger.y, true);
                    if (s.playing() && touchControl() == 3) {
                        int w = 0, h = 0;
                        SDL_GetWindowSizeInPixels(window, &w, &h);
                        s.originStick.move(event.tfinger.x * w, event.tfinger.y * h);
                    }
                    if (s.playing() && touchControl() == 2) {
                        const auto target =
                            s.relativeTouch.target(s.menuX, s.menuY, s.settings.gain());
                        s.pointerX = target[0];
                        s.pointerY = target[1];
                    }
                }
                break;
            case SDL_EVENT_FINGER_UP:
                if (!touchEnabled())
                    break;
                if (s.paused) {
                    const bool inside = pointer(event.tfinger.x, event.tfinger.y, true);
                    pointerHeld = false;
                    menuInput.pointerReleased = inside;
                    break;
                }
                if (s.touching && s.activeFinger == event.tfinger.fingerID) {
                    s.originStick.cancel();
                    s.relativeTouch.cancel();
                    s.touching = false;
                    s.touchActions.reset();
                    pointerHeld = false;
                    menuInput.pointerReleased = true;
                } else {
                    s.touchActions.up(event.tfinger.fingerID, SDL_GetTicks());
                    s.bombHeld = false;
                }
                break;
            case SDL_EVENT_FINGER_CANCELED:
                if (!touchEnabled())
                    break;
                if (s.touching && s.activeFinger == event.tfinger.fingerID) {
                    s.originStick.cancel();
                    s.relativeTouch.cancel();
                    s.touching = false;
                    s.touchActions.reset();
                    s.latch.reset();
                    pointerHeld = false;
                    pointerActive = false;
                    menuInput.pointerReleased = false;
                    s.frontend->cancelInput();
                    s.game.cancelPendingInput();
                } else {
                    s.touchActions.up(event.tfinger.fingerID, SDL_GetTicks(), true);
                    s.bombHeld = false;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat)
                    break;
                if (s.paused) {
                    pointerActive = false;
                    switch (event.key.key) {
                    case SDLK_P:
                    case SDLK_ESCAPE:
                    case SDLK_AC_BACK:
                        togglePause();
                        break;
                    case SDLK_RETURN:
                    case SDLK_SPACE:
                        menuInput.confirm = true;
                        break;
                    case SDLK_UP:
                        menuInput.vertical = -1;
                        break;
                    case SDLK_DOWN:
                        menuInput.vertical = 1;
                        break;
                    case SDLK_LEFT:
                        menuInput.horizontal = -1;
                        break;
                    case SDLK_RIGHT:
                        menuInput.horizontal = 1;
                        break;
                    default:
                        break;
                    }
                    break;
                }
                switch (event.key.key) {
                case SDLK_ESCAPE:
                case SDLK_AC_BACK:
                    routeBack();
                    break;
                case SDLK_RETURN:
                    pointerActive = false;
                    if (s.mode == SessionMode::Frontend)
                        menuInput.confirm = true;
                    else if (s.mode == SessionMode::DirectPlay &&
                             s.game.phase() == GamePhase::GameOver)
                        selectSession(SessionMode::DirectPlay);
                    break;
                case SDLK_LEFT:
                    pointerActive = false;
                    if (s.mode == SessionMode::Frontend)
                        menuInput.horizontal = -1;
                    break;
                case SDLK_RIGHT:
                    pointerActive = false;
                    if (s.mode == SessionMode::Frontend)
                        menuInput.horizontal = 1;
                    break;
                case SDLK_UP:
                    menuInput.vertical = -1;
                    pointerActive = false;
                    break;
                case SDLK_DOWN:
                    menuInput.vertical = 1;
                    pointerActive = false;
                    break;
                case SDLK_BACKSPACE:
                    menuInput.erase = true;
                    break;
                case SDLK_TAB:
                    alternateHeld = true;
                    if (s.playing())
                        s.latch.press(0x40u);
                    break;
                case SDLK_SPACE:
                    if (s.mode == SessionMode::Frontend &&
                        s.frontend->view() != FrontendView::Playing)
                        menuInput.confirm = true;
                    else {
                        s.bombHeld = true;
                        if (s.playing())
                            s.latch.press(0x20u);
                    }
                    break;
                case SDLK_P:
                    togglePause();
                    break;
                case SDLK_F11:
                    applyDisplay(full ? DisplayMode::Windowed : DisplayMode::Borderless);
                    break;
                default:
                    break;
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_SPACE)
                    s.bombHeld = false;
                if (event.key.key == SDLK_TAB)
                    alternateHeld = false;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!s.pad)
                    s.pad = SDL_OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (s.pad && SDL_GetGamepadID(s.pad) == event.gdevice.which) {
                    SDL_CloseGamepad(s.pad);
                    s.pad = nullptr;
                    s.latch.reset();
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (s.paused) {
                    pointerActive = false;
                    switch (event.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_START:
                        togglePause();
                        break;
                    case SDL_GAMEPAD_BUTTON_SOUTH:
                        menuInput.confirm = true;
                        break;
                    case SDL_GAMEPAD_BUTTON_DPAD_UP:
                        menuInput.vertical = -1;
                        break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                        menuInput.vertical = 1;
                        break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                        menuInput.horizontal = -1;
                        break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                        menuInput.horizontal = 1;
                        break;
                    default:
                        break;
                    }
                    break;
                }
                switch (event.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    if (s.mode == SessionMode::Frontend)
                        menuInput.horizontal = -1;
                    pointerActive = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    if (s.mode == SessionMode::Frontend)
                        menuInput.horizontal = 1;
                    pointerActive = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    menuInput.vertical = -1;
                    pointerActive = false;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    menuInput.vertical = 1;
                    pointerActive = false;
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    if (s.playing() && !s.blockSouth)
                        s.latch.press(0x20u);
                    if (s.mode == SessionMode::Frontend) {
                        menuInput.confirm = true;
                        pointerActive = false;
                    } else if (s.mode == SessionMode::DirectPlay &&
                               s.game.phase() == GamePhase::GameOver)
                        selectSession(SessionMode::DirectPlay);
                    break;
                case SDL_GAMEPAD_BUTTON_START:
                    if (s.mode == SessionMode::Frontend &&
                        s.frontend->view() != FrontendView::Playing) {
                        menuInput.confirm = true;
                        pointerActive = false;
                        break;
                    }
                    togglePause();
                    break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    if (s.playing()) {
                        if (s.mode == SessionMode::Frontend ? s.frontend->game().arse()
                                                            : s.game.arse())
                            s.latch.press(1u);
                    } else if (s.mode == SessionMode::Frontend &&
                               controllerBackAllowed(s.frontend->view()))
                        routeBack();
                    break;
                case SDL_GAMEPAD_BUTTON_WEST:
                    if (s.playing())
                        s.latch.press(0x40u);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
        if (!lifecycle.active.load()) {
            SDL_Delay(20);
            continue;
        }
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/after_input.inc"
#endif
        const Uint64 now = SDL_GetTicksNS();
        syncSurfaceOrientation();
        captureMouse();
        if (now >= nextRefreshCheck) {
            s.hasTouch = directTouchAvailable();
            uint32_t numerator = 60, denominator = 1;
            if (const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))) {
                if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
                    numerator = uint32_t(mode->refresh_rate_numerator);
                    denominator = uint32_t(mode->refresh_rate_denominator);
                } else if (std::isfinite(mode->refresh_rate) && mode->refresh_rate >= 10 &&
                           mode->refresh_rate <= 1000) {
                    numerator = uint32_t(std::lround(mode->refresh_rate * 1000));
                    denominator = 1000;
                }
            }
            if (double(numerator) / denominator > s.settings.cap) {
                numerator = uint32_t(s.settings.cap);
                denominator = 1;
            }
            if (presentation.rate(now, numerator, denominator) || nextRefreshCheck == 0)
                SDL_Log("Presentation target: %u/%u Hz (%s); simulation clock unchanged",
                        presentation.numerator(), presentation.denominator(),
                        "display adaptive, capped");
            s.presentNumerator = presentation.numerator();
            s.presentDenominator = presentation.denominator();
            nextRefreshCheck = now + 1'000'000'000;
        }
        if (s.paused) {
            const float px = axis(s.pad, SDL_GAMEPAD_AXIS_LEFTX),
                        py = axis(s.pad, SDL_GAMEPAD_AXIS_LEFTY);
            const int mx = px > .5f    ? 1
                           : px < -.5f ? -1
                                       : 0,
                      my = py > .5f    ? 1
                           : py < -.5f ? -1
                                       : 0;
            if (mx && mx != stickMenuX) {
                menuInput.horizontal = mx;
                pointerActive = false;
            }
            if (my && my != stickMenuY) {
                menuInput.vertical = my;
                pointerActive = false;
            }
            stickMenuX = mx;
            stickMenuY = my;
            for (unsigned n = pauseCadence.pending(now); n; --n)
                s.pauseMenu.animate();
            menuInput.pointer = {int(s.menuX - (s.fieldWidth() - 240) * .5f),
                                 int(s.menuY - (s.fieldHeight() - 320) * .5f),
                                 pointerHeld ? 1u : 0u, 0};
            menuInput.pointerActive = pointerActive;
            int action = s.pauseMenu.input(menuInput), direction = s.pauseMenu.direction();
            menuInput = {};
            if (action == 0)
                togglePause();
            else if (action == 1) {
                s.settings.original = !s.settings.original;
                s.reference = s.settings.original;
            } else if (action == 2) {
                const int caps[] = {30, 60, 120, 200};
                int at = 0;
                while (at < 3 && caps[at] != s.settings.cap)
                    ++at;
                s.settings.cap = caps[(at + direction + 4) % 4];
                nextRefreshCheck = 0;
            } else if (action == 3)
                applyDisplay(DisplayMode((int(s.settings.display) + direction + 3) % 3));
            else if (action == 4) {
                const int previous = s.settings.orientation,
                          count = PlayerOptions::orientationCount();
                s.settings.orientation = (previous + direction + count) % count;
                if (!applyOrientation())
                    s.settings.orientation = previous;
                s.touching = false;
                s.originStick.cancel();
                s.relativeTouch.cancel();
            } else if (action == 5) {
                const int count = s.settings.touchControlCount(secondaryInputAvailable());
                s.settings.control = 1 + (s.settings.control - 1 + direction + count) % count;
                s.originStick.cancel();
                s.relativeTouch.cancel();
                s.touching = false;
                s.touchActions.reset();
                s.bombHeld = false;
                pointerHeld = false;
            } else if (action == 6) {
                togglePause();
                if (s.mode == SessionMode::Frontend)
                    menuInput.back = true;
                else
                    selectSession(SessionMode::Frontend);
            } else if (action == 7)
                s.settings.expanded = !s.settings.expanded;
            else if (action == 8)
                s.settings.sensitivity = std::clamp(s.settings.sensitivity + direction, 5, 30);
            if (action > 0 && !s.settings.save(optionsFile))
                s.pauseMenu.message = "Options save failed";
            s.pauseMenu.rebuild(s.settings, s.hasTouch, DisplaySelectionAvailable);
        }
        syncSurfaceOrientation();
        captureMouse();
        const unsigned due = focused && !s.paused ? cadence.pending(now) : 0;
        for (unsigned tick = 0; tick < due && running; ++tick) {
            if (s.playing() && touchControl() == 3 && s.touching) {
                const auto delta = s.originStick.tick(.03 / steps, s.fieldWidth(), s.fieldHeight(),
                                                      s.settings.gain());
                s.pointerX =
                    std::clamp(s.pointerX + float(delta[0]), 0.f, float(s.fieldWidth() - 1));
                s.pointerY =
                    std::clamp(s.pointerY + float(delta[1]), 0.f, float(s.fieldHeight() - 1));
            }
            const bool *keys = SDL_GetKeyboardState(nullptr);
            const float inputStep = 5.f / steps * s.fieldWidth() / 240.f;
            s.pointerX = std::clamp(s.pointerX + inputStep * (int(keys[SDL_SCANCODE_RIGHT]) -
                                                              int(keys[SDL_SCANCODE_LEFT])),
                                    0.f, float(s.fieldWidth() - 1));
            s.pointerY = std::clamp(s.pointerY + inputStep * (int(keys[SDL_SCANCODE_DOWN]) -
                                                              int(keys[SDL_SCANCODE_UP])),
                                    0.f, float(s.fieldHeight() - 1));
            float ax = axis(s.pad, SDL_GAMEPAD_AXIS_LEFTX),
                  ay = axis(s.pad, SDL_GAMEPAD_AXIS_LEFTY);
            if (s.mode == SessionMode::Frontend && s.frontend->view() != FrontendView::Playing) {
                int x = ax > .5f ? 1 : ax < -.5f ? -1 : 0, y = ay > .5f ? 1 : ay < -.5f ? -1 : 0;
                if (x && x != stickMenuX) {
                    menuInput.horizontal = x;
                    pointerActive = false;
                }
                if (y && y != stickMenuY) {
                    menuInput.vertical = y;
                    pointerActive = false;
                }
                stickMenuX = x;
                stickMenuY = y;
                ax = ay = 0;
            }
            const float magnitude = std::hypot(ax, ay);
            if (magnitude > .18f) {
                const float gain = controllerGain(magnitude) * inputStep * s.settings.gain();
                s.pointerX =
                    std::clamp(s.pointerX + ax / magnitude * gain, 0.f, float(s.fieldWidth() - 1));
                s.pointerY =
                    std::clamp(s.pointerY + ay / magnitude * gain, 0.f, float(s.fieldHeight() - 1));
                pointerActive = false;
            }
            (void)pointerActive;
            const bool south = s.pad && SDL_GetGamepadButton(s.pad, SDL_GAMEPAD_BUTTON_SOUTH);
            if (!south)
                s.blockSouth = false;
            LogicalInput input;
            input = s.latch.sample(
                int(s.pointerX), int(s.pointerY),
                ((s.bombHeld || s.touchActions.bomb() || (south && !s.blockSouth)) ? 0x20u : 0u) |
                    (((pointerHeld &&
                       !(s.touching && (s.mode == SessionMode::Frontend ? s.frontend->game().arse()
                                                                        : s.game.arse()))) ||
                      s.touchActions.secondary() ||
                      (s.playing() &&
                       (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ||
                        (s.pad && SDL_GetGamepadButton(s.pad, SDL_GAMEPAD_BUTTON_EAST) &&
                         (s.mode == SessionMode::Frontend ? s.frontend->game().arse()
                                                          : s.game.arse())))))
                         ? 1u
                         : 0u) |
                    ((alternateHeld ||
                      (s.pad && SDL_GetGamepadButton(s.pad, SDL_GAMEPAD_BUTTON_WEST)))
                         ? 0x40u
                         : 0u));
            s.touchActions.sampled();
            bool frontendAdvanced = false;
            if (s.mode == SessionMode::Frontend && running &&
                (s.frontend->view() == FrontendView::Playing || s.frontend->watchingReplay() ||
                 menuPhase == 0)) {
                const auto revision = s.frontend->assetRevision();
                const auto view = s.frontend->view();
                const bool wasGameplay =
                    view == FrontendView::Playing || s.frontend->watchingReplay();
                const int previousWidth = s.fieldWidth(), previousHeight = s.fieldHeight();
                menuInput.pointer = input;
                menuInput.pointerActive = pointerActive;
                s.frontend->step(menuInput);
                frontendAdvanced = true;
                if (wasGameplay && !s.playing()) {
                    s.suppressMenuRelease = pointerHeld || s.touching;
                    pointerHeld = false;
                    s.bombHeld = false;
                    s.touching = false;
                    s.activeFinger = 0;
                    s.touchActions.reset();
                    s.originStick.cancel();
                    s.relativeTouch.cancel();
                    s.latch.reset();
                    s.blockSouth = true;
                }
                menuInput = {};
                if (!wasGameplay && s.playing()) {
                    s.blockSouth = true;
                    s.bombHeld = false;
                    s.latch.reset();
                    if (previousWidth != s.fieldWidth() || previousHeight != s.fieldHeight()) {
                        if (pointerActive)
                            pointer(lastPointerNX, lastPointerNY, true);
                        else {
                            const auto &p = s.frontend->game().entities().at(EntityPool::Player);
                            s.pointerX = float(p.x) / 65536;
                            s.pointerY = std::min(float(p.y) / 65536, float(s.fieldHeight() - 1));
                        }
                    }
                }
                if (revision != s.frontend->assetRevision())
                    renderer.reloadPages(s.assets);
                if (audio && audioArse != s.assets.arse()) {
                    audioArse = s.assets.arse();
                    audio = std::make_unique<Audio>(opt.assets, audioArse);
                    cadence.reset(SDL_GetTicksNS());
                }
                if (audio && s.frontend->audioResetRequested())
                    audio->stop();
                if (audio)
                    for (int sound : s.frontend->sounds())
                        audio->play(size_t(sound));
                if (s.frontend->quitRequested())
                    running = false;
            }
            if (s.mode == SessionMode::DirectPlay && running) {
                const auto revision = s.game.assetRevision();
                s.game.step(input);
                if (revision != s.game.assetRevision())
                    renderer.reloadPages(s.assets);
                if (audio)
                    for (int sound : s.game.sounds())
                        audio->play(size_t(sound));
            }
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/after_tick.inc"
#endif
            if (running)
                s.tick = add32(s.tick, 1);
            s.colorPhase += .1f / steps;
            if (s.mode == SessionMode::Frontend && frontendAdvanced)
                decorativeCamera.capture(s.frontend->canvas(), s.frontend->assetRevision());
            else if (s.mode == SessionMode::DirectPlay)
                decorativeCamera.capture(s.game.canvas(), (1ull << 32) | s.game.assetRevision());
            else if (s.mode != SessionMode::Frontend)
                decorativeCamera.reset();
            menuPhase = (menuPhase + 1) % steps;
            // Fixed deadlines preserve the 30 ms simulation cadence across display rates.
        }
        const bool gpuReady = renderer.frameReady();
        const auto presentNow = SDL_GetTicksNS();
        if (!presentation.due(presentNow) || !gpuReady) {
            SDL_Delay(1);
            continue;
        }
        if (!presentation.pending(presentNow)) {
            SDL_Delay(1);
            continue;
        }
#ifdef GRPP_APP_CHECKS
        if (opt.cursorTest) {
            focused = true;
            mouseInside = true;
            if (rendered == 4)
                selectSession(SessionMode::DirectPlay);
            if (rendered == 8)
                s.paused = true;
        }
#endif
        const bool menuCursor = !liveGameplay() || s.paused;
        const bool nativeCursor = !focused || (!mouseInside && menuCursor);
        if (DisplaySelectionAvailable && (!cursorKnown || cursorShown != nativeCursor)) {
            if (nativeCursor) {
                SDL_SetCursor(SDL_GetDefaultCursor());
                SDL_ShowCursor();
            } else
                SDL_HideCursor();
            cursorKnown = true;
            cursorShown = nativeCursor;
        }
        const bool softwareCursor =
            DisplaySelectionAvailable && menuCursor && focused && mouseInside;
#ifdef GRPP_APP_CHECKS
        if (opt.cursorTest &&
            softwareCursor !=
                (rendered < 4 || rendered >= 8 ||
                 (s.mode == SessionMode::Frontend ? s.frontend->game() : s.game).phase() !=
                     GamePhase::Playing))
            throw std::runtime_error("Software title/gameplay/pause cursor mismatch");
#endif
        std::array<float, 2> desktopMouse{};
        SDL_GetMouseState(&desktopMouse[0], &desktopMouse[1]);
#ifdef GRPP_APP_CHECKS
        if (opt.cursorTest)
            desktopMouse = {40, 60};
#endif
        s.draw();
        const auto capture =
            (opt.frames && rendered == opt.frames - 1) ? opt.capture : std::filesystem::path{};
        if (focused && !s.paused)
            measureDecoration();
        decorativeCamera.apply(s.canvas, decorativeCameraFraction);
        renderer.present(s.canvas, s.assets, s.reference, true, s.aspect, capture, decorativeOffset,
                         s.renderOrientation(), s.settings.expanded, s.playing(),
                         softwareCursor ? &desktopMouse : nullptr);
        if (displayChangePending &&
            (opt.hidden ||
             ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
              !(SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)))))
            clearDisplayPending();
        // Establish a visible, drawable window before restoring saved fullscreen.
        if (startupDisplayPending) {
            startupDisplayPending = false;
            applyDisplay(startupDisplay);
        }
        ++rendered;
    }
#ifdef GRPP_APP_CHECKS
#include "../tests/app_checks/results.inc"
#endif
    s.frontend->close();
    if (s.frontend->saveError().rfind("SAVE FAILED", 0) == 0)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gridrunner++ save failed",
                                 s.frontend->saveError().c_str(), window);
    SDL_Log("Completed %d presentation frames; %d host updates (%s); GL errors: none", rendered,
            int(s.tick), "200 Hz");
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        return run(options(argc, argv));
    } catch (const std::exception &error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
