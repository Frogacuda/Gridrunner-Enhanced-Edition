int backScript = -1, backFrame = -1;
int lastScript = -1;
int pausedSource = -1;
float touchPauseX = 0, touchPauseY = 0;
int touchScript = -1;
float touchAnchorX = 0, touchAnchorY = 0;
int editionScript = -1;
int viewScript = -1;
int pointerScript = -1;
auto selectPauseAction = [&](int action) {
    FrontendInput input;
    input.vertical = 1;
    for (size_t n = 0; n < s.pauseMenu.actions().size() &&
                       s.pauseMenu.actions()[size_t(s.pauseMenu.selected())] != action;
         ++n)
        s.pauseMenu.input(input);
};
bool burstInjected = false, burstObserved = false;
uint64_t burstNs = 0;

int touchOffScript = -1;
float touchOffX = 0, touchOffY = 0;

int watchScript = -1;
std::vector<uint8_t> pausedWatchState;
std::unique_ptr<Game> watchReference;
ReplayPlayback watchDecoder;
if (opt.replayWatchTest) {
    auto fixture = std::make_unique<Game>(s.assets);
    fixture->setNativeRate(true, 6);
    fixture->arse(opt.arse);
    fixture->start();
    ReplayRecording recording;
    recording.begin(*fixture, 0);
    for (int t = 0; t < 2400; ++t) {
        LogicalInput i{120, 240, t % 80 == 0 ? 32u : 0u, 0};
        recording.append(i);
        fixture->step(i);
        recording.reachedStage(fixture->stage());
    }
    recording.finish(fixture->combatState().score, false);
    auto path = s.frontend->replayPath(0);
    recording.save(path);
    auto chooseWatch = [&](int n) {
        FrontendInput i;
        i.vertical = 1;
        for (int k = 0; k < n; ++k) s.frontend->step(i);
        i = {};
        i.confirm = true;
        s.frontend->step(i);
    };
    chooseWatch(0); chooseWatch(4); chooseWatch(0); chooseWatch(4); chooseWatch(0);
    if (!s.frontend->watchingReplay()) throw std::runtime_error("Watch setup failed");
    watchReference = std::make_unique<Game>(s.assets);
    watchDecoder.load(*watchReference, path);
    renderer.reloadPages(s.assets);
}

if (opt.displayTest) {
    for (auto mode : {DisplayMode::Borderless, DisplayMode::Fullscreen,
                      DisplayMode::Borderless, DisplayMode::Windowed,
                      DisplayMode::Fullscreen, DisplayMode::Windowed}) {
        if (!applyDisplay(mode) || s.settings.display != mode ||
            bool(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != (mode != DisplayMode::Windowed))
            throw std::runtime_error("Display transition state mismatch");
    }
    rejectNextDisplay = true;
    if (applyDisplay(DisplayMode::Fullscreen) || s.settings.display != DisplayMode::Windowed ||
        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ||
        std::filesystem::exists(displayPendingFile))
        throw std::runtime_error("Rejected display did not recover windowed");
    PlayerOptions recovered;
    recovered.load(optionsFile);
    if (recovered.display != DisplayMode::Windowed)
        throw std::runtime_error("Failed display mode persisted");
}
