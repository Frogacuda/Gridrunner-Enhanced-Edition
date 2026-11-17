else if (arg == "--burst-source") {
    o.burstSource = value();
    if (o.burstSource != "key" && o.burstSource != "mouse" && o.burstSource != "touch" &&
        o.burstSource != "pad")
        throw std::runtime_error("Unknown burst source");
}
else if (arg == "--input-burst-test") {
    o.inputBurstTest = true;
    o.mode = SessionMode::DirectPlay;
    o.frames = 90;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
}
else if (arg == "--view-test") {
    o.viewTest = true;
    o.view = 0;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.mode = SessionMode::DirectPlay;
    o.frames = 50;
    o.windowWidth = 1280;
    o.windowHeight = 720;
}
else if (arg == "--pointer-test") {
    o.pointerTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.mode = SessionMode::DirectPlay;
    o.frames = 24;
}
else if (arg == "--cursor-test") {
    o.cursorTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.frames = 12;
}
else if (arg == "--back-test") {
    o.backTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.frames = 100;
    o.presentationHz = 60;
}
else if (arg == "--pause-test") {
    o.pauseTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.mode = SessionMode::DirectPlay;
    o.frames = 80;
}
else if (arg == "--touch-test") {
    o.touchTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.mode = SessionMode::DirectPlay;
    o.frames = 144;
}
else if (arg == "--edition-test") {
    o.editionTest = true;
    o.hidden = true;
    o.noSave = true;
    o.mode = SessionMode::Frontend;
    o.frames = 90;
}

else if (arg == "--touch-off-test") {
    o.touchOffTest = true;
    o.hidden = true;
    o.noSave = true;
    o.audio = false;
    o.mode = SessionMode::DirectPlay;
    o.frames = 60;
    o.presentationHz = 60;
}

else if (arg == "--replay-watch-test") {
    o.replayWatchTest = true;
    o.hidden = true;
    o.audio = false;
    o.frames = 35;
    o.mode = SessionMode::Frontend;
    o.saveDirectory = (std::filesystem::temp_directory_path() /
        ("grpp-watch-check-" + std::to_string(SDL_GetPerformanceCounter()))).string();
}

else if (arg == "--display-test") {
    o.displayTest = true;
    o.hidden = true;
    o.audio = false;
    o.frames = 3;
    o.saveDirectory = (std::filesystem::temp_directory_path() /
        ("grpp-display-check-" + std::to_string(SDL_GetPerformanceCounter()))).string();
}
