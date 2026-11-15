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
