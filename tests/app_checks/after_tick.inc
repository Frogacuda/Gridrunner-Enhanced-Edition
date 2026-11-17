if (opt.inputBurstTest && burstInjected && s.game.player().bombActive && !burstObserved) {
    burstObserved = true;
    SDL_Log("Burst action delivered");
}

if (opt.replayWatchTest && frontendAdvanced && s.frontend->watchingReplay()) {
    watchDecoder.step(*watchReference);
    if (ReplayState::capture(const_cast<Game &>(s.frontend->game())) != ReplayState::capture(*watchReference))
        throw std::runtime_error("Replay host pacing or live input changed simulation");
}
