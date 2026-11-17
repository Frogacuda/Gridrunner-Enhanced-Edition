if (opt.inputBurstTest) {
    if (!burstInjected || !burstObserved)
        throw std::runtime_error("Short action burst lost");
    SDL_Log("Short action burst: %s (%s pipeline)", burstObserved ? "delivered" : "lost", "fixed");
}
if (opt.backTest) {
    if (!s.frontend->quitRequested())
        throw std::runtime_error("Back test: menu exit failed");
    SDL_Log("Back routing: no-touch Pause hidden/inactive; Esc/Android Back/Start pause the "
            "live frontend; repeat suppression, frozen/resumed run, deliberate Exit and menu "
            "Back/quit PASS");
}
if (opt.pauseTest)
    SDL_Log("Pause integration: keyboard navigation, all four caps, touch controls, graphics "
            "hot swap, three orientations, Start pause/resume, Exit, frozen gameplay, P/Escape/Android "
            "Back, touch and controller resume PASS");
if (opt.cursorTest)
    SDL_Log("Software menu cursor: title/gameplay/pause states PASS");
if (opt.touchTest)
    SDL_Log("Touch integration: 36 cases, all three modes/orientations, original/AoF geometry, "
            "both view modes, down/drag/release/cancel PASS");
if (opt.editionTest)
    SDL_Log("Edition integration: live title toggle via Right/Left/Enter, matching sprite/audio "
            "banks, title remains open PASS");
if (opt.pointerTest && rendered == 24)
    SDL_Log("Exterior pointer: 384 mouse/touch edge and corner clamps; two editions, both view "
            "modes, landscape/portrait, all orientations; paused-menu rejection; 72 bounded "
            "relative mouse gain/orientation/reversal cases PASS");

if (opt.touchOffTest)
    SDL_Log("Touch Off: finger tap/drag/release/cancel, synthetic touch/pen mouse events, paused "
            "menus, re-enable, real mouse and keyboard PASS");

if (opt.replayWatchTest) {
    if (s.frontend->view() != FrontendView::ReplayBrowse || s.frontend->replay().pending())
        throw std::runtime_error("Replay watch final state failed");
    SDL_Log("Replay watch: paced playback, frozen pause, graphics/view/orientation, ignored controls, Exit PASS");
}

if (opt.displayTest)
    SDL_Log("Display modes: Windowed/Borderless/Fullscreen transitions, failure rollback and saved recovery PASS");
