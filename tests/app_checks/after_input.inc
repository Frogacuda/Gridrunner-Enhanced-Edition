if (opt.touchTest) {
    focused = true;
    const int phase = rendered % 4;
    if ((phase == 1 || phase == 2) && (!s.touching || !pointerActive))
        throw std::runtime_error("Touch integration: letterbox touch rejected");
    if (phase == 1 && s.settings.control >= 2 &&
        (std::abs(s.pointerX - touchAnchorX) > .01f || std::abs(s.pointerY - touchAnchorY) > .01f))
        throw std::runtime_error(
            "Touch integration: neutral mode=" + std::to_string(s.settings.control) +
            " test=" + std::to_string(rendered / 4) + " target=" + std::to_string(s.pointerX) +
            "," + std::to_string(s.pointerY) + " anchor=" + std::to_string(touchAnchorX) + "," +
            std::to_string(touchAnchorY));
    if (phase == 2 && s.settings.control == 2) {
        const auto t = s.relativeTouch.target(s.menuX, s.menuY, s.settings.gain());
        if (std::abs(t[0] - s.pointerX) > .01f || std::abs(t[1] - s.pointerY) > .01f)
            throw std::runtime_error("Touch integration: relative drag lost");
    }
    if (phase == 3 && s.touching)
        throw std::runtime_error("Touch integration: release lost");
}
