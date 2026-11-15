if (opt.inputBurstTest && burstInjected && s.game.player().bombActive && !burstObserved) {
    burstObserved = true;
    SDL_Log("Burst action delivered");
}
