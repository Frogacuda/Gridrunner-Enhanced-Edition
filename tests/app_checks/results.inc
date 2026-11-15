if (opt.inputBurstTest) {
    if (!burstInjected || !burstObserved)
        throw std::runtime_error("Short action burst lost");
    SDL_Log("Short action burst: %s (%s pipeline)", burstObserved ? "delivered" : "lost", "fixed");
}
if (opt.backTest) {
    if (!s.frontend->quitRequested())
        throw std::runtime_error("Back test: menu exit failed");
    SDL_Log("Back routing: no-touch Pause hidden/inactive; Esc/Android Back/controller B pause the "
            "live frontend; repeat suppression, frozen/resumed run, deliberate Exit and menu "
            "Back/quit PASS");
}
if (opt.pauseTest)
    SDL_Log("Pause integration: keyboard navigation, all four caps, three touch controls, graphics "
            "hot swap, three orientations, B pause/resume, Exit, frozen gameplay, P/Escape/Android "
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
