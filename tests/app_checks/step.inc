if (opt.backTest && backScript != rendered) {
    backScript = rendered;
    SDL_Keycode key = 0;
    auto expectView = [&](FrontendView view) {
        if (s.frontend->view() != view)
            throw std::runtime_error("Back test: wrong frontend screen at " +
                                     std::to_string(rendered));
    };
    auto expectPaused = [&](bool paused) {
        expectView(FrontendView::Playing);
        if (s.paused != paused)
            throw std::runtime_error("Back test: wrong pause state at " + std::to_string(rendered));
    };
    switch (rendered) {
    case 0:
        key = SDLK_RETURN;
        break;
    case 5:
        expectView(FrontendView::Main);
        key = SDLK_RETURN;
        break;
    case 10:
        expectView(FrontendView::Level);
        key = SDLK_DOWN;
        break;
    case 15:
        key = SDLK_RETURN;
        break;
    case 20: {
        expectPaused(false);
        if (s.hasTouch ||
            std::any_of(s.canvas.draws().begin(), s.canvas.draws().end(),
                        [](const Draw &d) { return d.hudAnchor == HudAnchor::TopRight; }))
            throw std::runtime_error("Back test: inaccessible Pause button drawn");
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = w * .98f;
        e.button.y = h * .01f;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        SDL_PushEvent(&e);
        break;
    }
    case 25:
        expectPaused(false);
        key = SDLK_ESCAPE;
        break;
    case 30: {
        expectPaused(true);
        backFrame = s.frontend->game().frame();
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        e.key.repeat = true;
        SDL_PushEvent(&e);
        break;
    }
    case 35:
        expectPaused(true);
        if (s.frontend->game().frame() != backFrame)
            throw std::runtime_error("Back test: paused gameplay advanced");
        key = SDLK_ESCAPE;
        break;
    case 40:
        expectPaused(false);
        if (s.frontend->game().frame() < backFrame)
            throw std::runtime_error("Back test: run restarted");
        key = SDLK_AC_BACK;
        break;
    case 45:
        expectPaused(true);
        break;
    case 50:
        expectPaused(false);
        break;
    case 55:
        expectPaused(true);
        break;
    case 60:
        expectPaused(false);
        key = SDLK_P;
        break;
    case 65:
        expectPaused(true);
        selectPauseAction(6);
        key = SDLK_RETURN;
        break;
    case 70:
        expectView(FrontendView::HighScores);
        key = SDLK_ESCAPE;
        break;
    case 75:
        expectView(FrontendView::Splash);
        key = SDLK_RETURN;
        break;
    case 80:
        expectView(FrontendView::Main);
        key = SDLK_AC_BACK;
        break;
    case 85:
        expectView(FrontendView::Farewell);
        key = SDLK_ESCAPE;
        break;
    }
    if (rendered == 42 || rendered == 52) {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
        SDL_PushEvent(&e);
    }
    if (rendered == 45 || rendered == 50 || rendered == 55) {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_START;
        SDL_PushEvent(&e);
    }
    if (key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        SDL_PushEvent(&e);
    }
}
if (opt.pointerTest && pointerScript != rendered) {
    pointerScript = rendered;
    const int test = rendered;
    s.settings.orientation = (test / 2) % 3;
    s.settings.expanded = (test / 6) % 2 != 0;
    s.settings.control = 1;
    s.game.arse(test >= 12);
    s.game.start();
    renderer.reloadPages(s.assets);
    s.touching = false;
    s.paused = false;
    SDL_SetWindowSize(window, test % 2 ? 720 : 1280, test % 2 ? 1280 : 720);
    SDL_PumpEvents();
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const int fw = s.fieldWidth(), fh = s.fieldHeight(), rotation = s.renderOrientation();
    const auto v = makeViewport(rotation ? ph : pw, rotation ? pw : ph, s.aspect, fw, fh);
    const std::array<std::array<float, 2>, 8> points{{{-40, fh * .5f},
                                                      {fw + 40.f, fh * .5f},
                                                      {fw * .5f, -40},
                                                      {fw * .5f, fh + 40.f},
                                                      {-40, -40},
                                                      {fw + 40.f, -40},
                                                      {-40, fh + 40.f},
                                                      {fw + 40.f, fh + 40.f}}};
    for (const auto &point : points) {
        const float ox = v.x + point[0] * v.width / fw, oy = v.y + point[1] * v.height / fh;
        const float px = rotation == 1 ? pw - oy : rotation == 2 ? oy : ox;
        const float py = rotation == 1 ? ox : rotation == 2 ? ph - ox : oy;
        for (bool touch : {false, true}) {
            const float x = touch ? px / pw : px * ww / pw, y = touch ? py / ph : py * wh / ph;
            if (!pointer(x, y, touch) || s.pointerPause ||
                std::abs(s.pointerX - std::clamp(point[0], 0.f, float(fw - 1))) > .001f ||
                std::abs(s.pointerY - std::clamp(point[1], 0.f, float(fh - 1))) > .001f)
                throw std::runtime_error("Exterior pointer did not snap to nearest valid boundary");
            s.paused = true;
            if (pointer(x, y, touch))
                throw std::runtime_error("Exterior pointer activates paused menu");
            s.paused = false;
        }
    }
    for (int sensitivity : {5, 10, 30}) {
        s.settings.sensitivity = sensitivity;
        s.pointerX = fw * .5f;
        s.pointerY = fh * .5f;
        // Logical +2,+3 mapped back into window event units.
        const float dx = 2.f * v.width / fw, dy = 3.f * v.height / fh;
        const float px = rotation == 1   ? -dy
                         : rotation == 2 ? dy
                                         : dx,
                    py = rotation == 1   ? dx
                         : rotation == 2 ? -dx
                                         : dy;
        relativeMouse(px * ww / pw, py * wh / ph);
        if (std::abs(s.pointerX - (fw * .5f + 2 * s.settings.gain())) > .001f ||
            std::abs(s.pointerY - (fh * .5f + 3 * s.settings.gain())) > .001f)
            throw std::runtime_error("Relative mouse scale/orientation/gain wrong");
        relativeMouse(px * ww / pw * 10000, py * wh / ph * 10000);
        if (s.pointerX != fw - 1 || s.pointerY != fh - 1)
            throw std::runtime_error("Relative mouse does not clamp");
        relativeMouse(-px * ww / pw, -py * wh / ph);
        if (std::abs(s.pointerX - (fw - 1 - 2 * s.settings.gain())) > .001f ||
            std::abs(s.pointerY - (fh - 1 - 3 * s.settings.gain())) > .001f)
            throw std::runtime_error("Relative mouse retains boundary overshoot");
    }
    s.settings.sensitivity = 10;
    cadence.reset(SDL_GetTicksNS());
}
if (opt.viewTest && viewScript != rendered) {
    viewScript = rendered;
    SDL_Keycode key = 0;
    if (rendered == 4 || rendered == 14 || rendered == 25)
        selectPauseAction(7);
    switch (rendered) {
    case 2:
    case 7:
    case 12:
    case 17:
    case 20:
    case 28:
        key = SDLK_P;
        break;
    case 3:
    case 13:
    case 23:
    case 24:
        key = SDLK_UP;
        break;
    case 5:
    case 15:
    case 22:
    case 26:
        key = SDLK_RETURN;
        break;
    case 21:
        key = SDLK_DOWN;
        break;
    }
    if (key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        SDL_PushEvent(&e);
    }
    if (rendered == 6 && !s.settings.expanded)
        throw std::runtime_error("View toggle did not enable Fullscreen");
    if (rendered == 16 && s.settings.expanded)
        throw std::runtime_error("View toggle did not restore Letterbox");
    if (rendered == 27 && (!s.settings.expanded || !s.reference))
        throw std::runtime_error("Fullscreen/Original combination failed");
    if (rendered == 30 || rendered == 34) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        const auto v = makeViewport(w, h, s.aspect, s.fieldWidth(), s.fieldHeight());
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = rendered == 30 ? w * .98f : v.x + v.width - 24.f * v.width / s.fieldWidth();
        e.button.y = 8.f * v.height / s.fieldHeight();
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        SDL_PushEvent(&e);
    }
    if (rendered == 31 && !s.paused)
        throw std::runtime_error("Relocated mouse pause target failed");
    if (rendered == 32 || rendered == 38) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_P;
        SDL_PushEvent(&e);
    }
    if (rendered == 35 && s.paused)
        throw std::runtime_error("Old pause hotspot remains active");
    if (rendered == 36) {
        SDL_Event e{};
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.fingerID = 42;
        e.tfinger.x = .98f;
        e.tfinger.y = .01f;
        SDL_PushEvent(&e);
    }
    if (rendered == 37 && !s.paused)
        throw std::runtime_error("Relocated touch pause target failed");
}
if (opt.editionTest && editionScript != rendered) {
    editionScript = rendered;
    SDL_Keycode key = rendered == 0    ? SDLK_RETURN
                      : rendered == 5  ? SDLK_DOWN
                      : rendered == 10 ? SDLK_RIGHT
                      : rendered == 30 ? SDLK_LEFT
                      : rendered == 50 ? SDLK_RETURN
                                       : 0;
    if (key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        SDL_PushEvent(&e);
    }
    if (rendered == 25 || rendered == 45 || rendered == 75) {
        const bool expected = rendered != 45;
        if (s.frontend->view() != FrontendView::Main || s.frontend->arseEdition() != expected ||
            s.assets.arse() != expected || audioArse != expected || !audio)
            throw std::runtime_error("Edition test: title/artwork/audio bank not switched");
    }
}
if (opt.touchTest && touchScript != rendered) {
    touchScript = rendered;
    const int test = rendered / 4, phase = rendered % 4;
    if (phase == 0) {
        s.settings.control = test % 3 + 1;
        s.settings.orientation = (test / 3) % 3;
        s.settings.expanded = test >= 18;
        s.game.arse(test % 18 >= 9);
        s.game.start();
        renderer.reloadPages(s.assets);
        s.touching = false;
        s.relativeTouch.cancel();
        s.originStick.cancel();
        SDL_SetWindowSize(window, 1000, 700);
        SDL_PumpEvents();
        cadence.reset(SDL_GetTicksNS());
    } else {
        const auto &p = s.game.entities().at(EntityPool::Player);
        touchAnchorX = std::clamp(p.x / 65536.f, 0.f, float(s.fieldWidth() - 1));
        touchAnchorY = std::clamp(p.y / 65536.f, 0.f, float(s.fieldHeight() - 1));
        SDL_Event e{};
        e.type = phase == 1   ? SDL_EVENT_FINGER_DOWN
                 : phase == 2 ? SDL_EVENT_FINGER_MOTION
                              : (test % 2 ? SDL_EVENT_FINGER_CANCELED : SDL_EVENT_FINGER_UP);
        e.tfinger.touchID = 1;
        e.tfinger.fingerID = 123;
        e.tfinger.x = phase == 1 ? (s.settings.expanded ? .15f : .02f) : .5f;
        e.tfinger.y = phase == 1 ? (s.settings.expanded ? .15f : .02f) : .5f;
        if (!SDL_PushEvent(&e))
            throw std::runtime_error(SDL_GetError());
    }
}
if (opt.pauseTest && lastScript != rendered) {
    lastScript = rendered;
    SDL_Keycode key = 0;
    switch (rendered) {
    case 5:
        key = SDLK_P;
        break;
    case 8:
    case 11:
    case 25:
    case 26:
    case 30:
        key = SDLK_DOWN;
        break;
    case 9:
    case 10:
    case 12:
    case 27:
    case 28:
    case 29:
    case 31:
    case 32:
    case 33:
        key = SDLK_RETURN;
        break;
    case 20:
    case 21:
    case 22:
        key = SDLK_RIGHT;
        break;
    case 40:
    case 50:
        key = SDLK_P;
        break;
    case 60:
        key = SDLK_ESCAPE;
        break;
    case 62:
    case 63:
        key = SDLK_AC_BACK;
        break;
    case 75:
        key = SDLK_P;
        break;
    case 76:
        key = SDLK_UP;
        break;
    case 77:
        key = SDLK_RETURN;
        break;
    }
    if (rendered == 26) {
        selectPauseAction(4);
        key = 0;
    }
    if (rendered == 30) {
        selectPauseAction(5);
        key = 0;
    }
    if (rendered == 28 && s.settings.orientation != 1)
        throw std::runtime_error("Pause test: clockwise orientation failed");
    if (rendered == 29 && s.settings.orientation != 2)
        throw std::runtime_error("Pause test: counterclockwise orientation failed");
    if (rendered == 30 && s.settings.orientation != 0)
        throw std::runtime_error("Pause test: upright orientation failed");
    if (rendered == 10 && !s.reference)
        throw std::runtime_error("Pause test: Original graphics not selected");
    if (rendered == 11 && s.reference)
        throw std::runtime_error("Pause test: Enhanced graphics not restored");
    if (rendered == 63 && !s.paused)
        throw std::runtime_error("Pause test: Android Back did not pause");
    if (rendered == 64 && s.paused)
        throw std::runtime_error("Pause test: Android Back did not resume");
    if (rendered == 65) {
        touchPauseX = s.pointerX;
        touchPauseY = s.pointerY;
    }
    if (rendered == 66 && (s.pointerX != touchPauseX || s.pointerY != touchPauseY))
        throw std::runtime_error("Pause test: pause touch changed gameplay target");
    if (rendered == 65 || rendered == 66 || rendered == 68 || rendered == 69) {
        SDL_Event e{};
        e.type = (rendered == 65 || rendered == 68) ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
        e.tfinger.fingerID = 42;
        e.tfinger.x = rendered < 68 ? .90f : .5f;
        e.tfinger.y = rendered < 68 ? .02f : .24f;
        SDL_PushEvent(&e);
    }
    if (rendered == 2 || rendered == 3) {
        if (rendered == 3 && (s.paused || s.mode != SessionMode::DirectPlay))
            throw std::runtime_error("Pause test: B paused gameplay");
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
        SDL_PushEvent(&e);
    }
    if (rendered == 3 && s.mode != SessionMode::DirectPlay)
        throw std::runtime_error("Pause test: B exited live gameplay");
    if (rendered == 78 && s.mode != SessionMode::Frontend)
        throw std::runtime_error("Pause test: Exit menu failed");
    if (rendered == 72 || rendered == 73) {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = rendered == 72 ? SDL_GAMEPAD_BUTTON_START : SDL_GAMEPAD_BUTTON_SOUTH;
        SDL_PushEvent(&e);
    }
    if (rendered == 67 && !s.paused)
        throw std::runtime_error("Pause test: touch target failed to open menu");
    if ((rendered == 70 || rendered == 74) && s.paused)
        throw std::runtime_error("Pause test: touch/controller unpause failed");
    if (key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        SDL_PushEvent(&e);
    }
    if (rendered == 6)
        pausedSource = s.game.frame();
    if (rendered > 6 && rendered < 40 && (!s.paused || s.game.frame() != pausedSource))
        throw std::runtime_error("Pause test: gameplay advanced while paused");
    if (rendered == 32 && s.settings.control != 2)
        throw std::runtime_error("Pause test: Control Mode 2 not selected");
    if (rendered == 33 && s.settings.control != 3)
        throw std::runtime_error("Pause test: Control Mode 3 not selected");
    if (rendered == 34) {
        selectPauseAction(8);
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_LEFT;
        SDL_PushEvent(&e);
    }
    if (rendered == 35) {
        if (s.settings.sensitivity != 9)
            throw std::runtime_error("Pause sensitivity left failed");
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_RIGHT;
        SDL_PushEvent(&e);
    }
    if (rendered == 36 && s.settings.sensitivity != 10)
        throw std::runtime_error("Pause sensitivity right failed");
    if (rendered == 34 && s.settings.control != 4)
        throw std::runtime_error("Pause test: Touch Off not selected");
    if (rendered == 36) {
        SDL_Event e{};
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.fingerID = 123;
        e.tfinger.x = .5f;
        e.tfinger.y = .24f;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_FINGER_UP;
        SDL_PushEvent(&e);
    }
    if (rendered == 37) {
        if (!s.paused || s.touching)
            throw std::runtime_error("Pause test: Off accepted touch input");
        selectPauseAction(5);
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_RETURN;
        SDL_PushEvent(&e);
    }
    if (rendered == 38 && s.settings.control != 1)
        throw std::runtime_error("Pause test: Touch control 1 not restored");
    if (rendered == 23 && s.settings.cap != 200)
        throw std::runtime_error("Pause test: cap cycle failed");
    if (rendered == 61 && s.paused)
        throw std::runtime_error("Pause test: Escape failed to unpause");
}
if (opt.inputBurstTest && rendered >= 8 && !burstInjected && s.game.player().transitionTicks <= 0) {
    burstInjected = true;
    burstNs = SDL_GetTicksNS();
    SDL_Event e{};
    e.common.timestamp = burstNs;
    if (opt.burstSource == "key") {
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_SPACE;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_KEY_UP;
        SDL_PushEvent(&e);
    } else if (opt.burstSource == "mouse") {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.button = SDL_BUTTON_RIGHT;
        e.button.x = w * .5f;
        e.button.y = h * .5f;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        SDL_PushEvent(&e);
    } else if (opt.burstSource == "touch") {
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.fingerID = 101;
        e.tfinger.x = e.tfinger.y = .5f;
        SDL_PushEvent(&e);
        e.tfinger.fingerID = 102;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_FINGER_UP;
        SDL_PushEvent(&e);
        e.tfinger.fingerID = 101;
        SDL_PushEvent(&e);
    } else {
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        SDL_PushEvent(&e);
    }
}

if (opt.touchOffTest && touchOffScript != rendered) {
    touchOffScript = rendered;
    auto finger = [&](Uint32 type) {
        SDL_Event e{};
        e.type = type;
        e.tfinger.touchID = 1;
        e.tfinger.fingerID = 123;
        e.tfinger.x = .9f;
        e.tfinger.y = .1f;
        SDL_PushEvent(&e);
    };
    auto mouse = [&](Uint32 type, SDL_MouseID id) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        SDL_Event e{};
        e.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            e.motion.which = id;
            e.motion.x = w * (id == 77 ? .2f : .9f);
            e.motion.y = h * (id == 77 ? .8f : .1f);
            e.motion.xrel = 7;
            e.motion.yrel = 3;
        } else {
            e.button.which = id;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = w * .9f;
            e.button.y = h * .1f;
        }
        SDL_PushEvent(&e);
    };
    auto key = [&](SDL_Keycode k) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = k;
        SDL_PushEvent(&e);
    };
    if (rendered == 3) {
        if (!secondaryInputAvailable())
            throw std::runtime_error("Touch Off test requires mouse/keyboard/controller");
        s.settings.control = 4;
        touchOffX = s.pointerX;
        touchOffY = s.pointerY;
    }
    if (rendered == 5)
        finger(SDL_EVENT_FINGER_DOWN);
    if (rendered == 6)
        finger(SDL_EVENT_FINGER_MOTION);
    if (rendered == 7)
        finger(SDL_EVENT_FINGER_UP);
    if (rendered == 8)
        finger(SDL_EVENT_FINGER_CANCELED);
    if (rendered == 10)
        mouse(SDL_EVENT_MOUSE_MOTION, SDL_TOUCH_MOUSEID);
    if (rendered == 11)
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_TOUCH_MOUSEID);
    if (rendered == 12 || rendered == 16)
        mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_TOUCH_MOUSEID);
    if (rendered == 14) {
        secondaryInputDetectedForTest = false;
        finger(SDL_EVENT_FINGER_DOWN);
    }
    if (rendered == 15)
        finger(SDL_EVENT_FINGER_MOTION);
    if (rendered == 20)
        secondaryInputDetectedForTest = true;
    if (rendered == 17)
        mouse(SDL_EVENT_MOUSE_MOTION, SDL_PEN_MOUSEID);
    if (rendered == 18)
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_PEN_MOUSEID);
    if (rendered == 19)
        mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_PEN_MOUSEID);
    if (rendered >= 4 && rendered <= 20 &&
        (s.pointerX != touchOffX || s.pointerY != touchOffY || s.touching || pointerHeld ||
         s.bombHeld))
        throw std::runtime_error(
            "Touch Off: finger or synthesized tap changed gameplay input at frame " +
            std::to_string(rendered));
    if (rendered == 22)
        key(SDLK_P);
    if (rendered == 24) {
        if (!s.paused)
            throw std::runtime_error("Touch Off: keyboard pause failed");
        finger(SDL_EVENT_FINGER_DOWN);
        finger(SDL_EVENT_FINGER_UP);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.which = SDL_TOUCH_MOUSEID;
        e.button.button = SDL_BUTTON_LEFT;
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        e.button.x = w * .5f;
        e.button.y = h * .24f;
        SDL_PushEvent(&e);
    }
    if (rendered == 27) {
        if (!s.paused)
            throw std::runtime_error("Touch Off: tap activated pause menu");
        key(SDLK_P);
    }
    if (rendered == 30) {
        if (s.paused)
            throw std::runtime_error("Touch Off: keyboard resume failed");
        s.settings.control = 1;
        finger(SDL_EVENT_FINGER_DOWN);
    }
    if (rendered == 32) {
        if (s.pointerX == touchOffX && s.pointerY == touchOffY)
            throw std::runtime_error("Touch Off: re-enabled touch failed");
        finger(SDL_EVENT_FINGER_UP);
    }
    if (rendered == 35) {
        s.settings.control = 4;
        touchOffX = s.pointerX;
        touchOffY = s.pointerY;
        mouse(SDL_EVENT_MOUSE_MOTION, 77);
    }
    if (rendered == 37 && s.pointerX == touchOffX && s.pointerY == touchOffY)
        throw std::runtime_error("Touch Off suppressed real mouse input");
    if (rendered == 40 || rendered == 44) {
        SDL_Event e{};
        e.type = rendered == 40 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.which = 77;
        e.button.button = SDL_BUTTON_RIGHT;
        e.button.x = 20;
        e.button.y = 20;
        SDL_PushEvent(&e);
    }
    if (rendered == 42) {
        if (!s.bombHeld)
            throw std::runtime_error("Real mouse bomb was blocked");
        finger(SDL_EVENT_FINGER_CANCELED);
    }
    if (rendered == 43 && !s.bombHeld)
        throw std::runtime_error("Disabled touch cancelled real mouse input");
    if (rendered == 45 && s.bombHeld)
        throw std::runtime_error("Real mouse release was blocked");
}

if (opt.replayWatchTest && watchScript != rendered) {
    watchScript = rendered;
    focused = true;
    SDL_Keycode key = 0;
    if (rendered == 5 || rendered == 18) key = SDLK_ESCAPE;
    if (rendered == 8 || rendered == 11) { selectPauseAction(1); key = SDLK_RETURN; }
    if (rendered == 13) { selectPauseAction(7); key = SDLK_RETURN; }
    if (rendered == 15) { selectPauseAction(4); key = SDLK_RETURN; }
    if (rendered == 6) {
        if (!s.paused) throw std::runtime_error("Replay did not pause");
        pausedWatchState = ReplayState::capture(const_cast<Game &>(s.frontend->game()));
    }
    if (rendered >= 6 && rendered <= 18 &&
        ReplayState::capture(const_cast<Game &>(s.frontend->game())) != pausedWatchState)
        throw std::runtime_error("Paused replay advanced or graphical options altered it");
    if (rendered == 10 && !s.reference) throw std::runtime_error("Replay Original toggle failed");
    if (rendered == 12 && s.reference) throw std::runtime_error("Replay Enhanced toggle failed");
    if (rendered == 17 && s.settings.orientation != 1)
        throw std::runtime_error("Replay orientation toggle failed");
    if (rendered == 19 && s.paused) throw std::runtime_error("Replay did not resume");
    if (rendered == 22) key = SDLK_SPACE; // Gameplay controls must be ignored.
    if (rendered == 23) key = SDLK_TAB;
    if (rendered == 25) key = SDLK_P;
    if (rendered == 27) { selectPauseAction(6); key = SDLK_RETURN; }
    if (rendered == 28 && (s.paused || s.frontend->view() != FrontendView::ReplayBrowse))
        throw std::runtime_error("Replay pause Exit did not return to list");
    if (key) {
        SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.key = key; SDL_PushEvent(&e);
    }
}
