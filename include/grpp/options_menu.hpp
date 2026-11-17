#pragma once
#include "grpp/frontend.hpp"
#include "grpp/visuals.hpp"
#include <cmath>
#include <fstream>

namespace grpp {
enum class DisplayMode { Windowed, Borderless, Fullscreen };
#ifdef __ANDROID__
inline constexpr bool DisplaySelectionAvailable = false;
#else
inline constexpr bool DisplaySelectionAvailable = true;
#endif
constexpr DisplayMode platformDisplay(DisplayMode requested,
                                      bool selectionAvailable = DisplaySelectionAvailable) {
    return selectionAvailable ? requested : DisplayMode::Borderless;
}
inline bool controllerBackAllowed(FrontendView view) {
    switch (view) {
    case FrontendView::Playing:
    case FrontendView::Main:
    case FrontendView::About:
    case FrontendView::Players:
    case FrontendView::Slot:
    case FrontendView::Reader:
    case FrontendView::Farewell:
        return false;
    default:
        return true;
    }
}
struct PlayerOptions {
    int cap = 200;
    DisplayMode display = platformDisplay(DisplayMode::Windowed);
    int control = 1;
    int sensitivity = 10; // Tenths; absolute touch ignores this gain.
    bool touchEnabled() const { return control != 4; }
    int touchControlCount(bool secondaryInput) const {
        return secondaryInput || control == 4 ? 4 : 3;
    }
    float gain() const { return sensitivity / 10.f; }
    bool original = false;
    bool expanded = true;
    static constexpr int defaultOrientation(bool mobile = !DisplaySelectionAvailable) {
        return mobile ? 3 : 0;
    }
    int orientation = defaultOrientation(); // Upright, Right, Left, Android Auto.
    static constexpr int orientationCount(bool mobile = !DisplaySelectionAvailable) {
        return mobile ? 4 : 3;
    }
    int renderOrientation(bool mobile = !DisplaySelectionAvailable) const {
        return mobile || orientation == 3 ? 0 : orientation;
    }
    void load(const std::filesystem::path &path,
              bool displayAvailable = DisplaySelectionAvailable) {
        std::ifstream in(path);
        int version, c, d, m;
        if (in >> version >> c >> d >> m && (version >= 1 && version <= 6) &&
            (c == 30 || c == 60 || c == 120 || c == 200) && d >= 0 && d < 3 && m >= 1 && m <= 4) {
            int graphics = 0;
            if (version >= 2 && (!(in >> graphics) || graphics < 0 || graphics > 1))
                return;
            int rotation = 0;
            if (version >= 3 && (!(in >> rotation) || rotation < 0 || rotation > 3))
                return;
            int fullscreen = 0;
            if (version >= 5 && (!(in >> fullscreen) || fullscreen < 0 || fullscreen > 1))
                return;
            int gain = 10;
            if (version >= 6 && (!(in >> gain) || gain < 5 || gain > 30))
                return;
            sensitivity = gain;
            expanded = fullscreen != 0;
            orientation = rotation;
            original = graphics != 0;
            cap = c;
            display = platformDisplay(DisplayMode(d), displayAvailable);
            control = m;
        }
    }
    bool save(const std::filesystem::path &path,
              bool displayAvailable = DisplaySelectionAvailable) const {
        if (path.empty())
            return true;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        out << "6 " << cap << ' ' << int(platformDisplay(display, displayAvailable)) << ' '
            << control << ' ' << int(original) << ' ' << orientation << ' ' << int(expanded) << ' '
            << sensitivity << '\n';
        return bool(out);
    }
};
// Surface orientation is measured, not inferred from a successful OS request.
// Turns are Android/SDL display rotation: portrait, landscape, flipped portrait,
// flipped landscape. Renderer ids retain 0 / clockwise / counterclockwise; 3 is 180.
inline int androidOrientationFallback(int requested, int actualTurns) {
    if (requested == 3 || actualTurns < 0 || actualTurns > 3)
        return 0;
    const int desired = requested == 0 ? 0 : requested == 1 ? 1 : 3;
    const int turns = (desired - actualTurns + 4) % 4;
    return turns == 2 ? 3 : turns == 3 ? 2 : turns;
}
// Fixed-origin circular virtual stick in physical screen coordinates.
inline std::array<float, 2> orientDelta(float x, float y, int orientation) {
    if (orientation == 1)
        return {y, -x};
    if (orientation == 2)
        return {-y, x};
    if (orientation == 3)
        return {-x, -y};
    return {x, y};
}
inline bool gameplayMouseCapture(bool desktop, bool visible, bool focused, bool paused,
                                 bool playing) {
    return desktop && visible && focused && !paused && playing;
}
class OriginStick {
    float ox_ = 0, oy_ = 0, x_ = 0, y_ = 0, radius_ = 1;
    int orientation_ = 0;
    bool active_ = false;

  public:
    void begin(float x, float y, int width, int height, int orientation) {
        ox_ = x_ = x;
        oy_ = y_ = y;
        radius_ = std::max(1.f, .25f * std::min(width, height));
        orientation_ = orientation;
        active_ = true;
    }
    void move(float x, float y) {
        if (active_) {
            x_ = x;
            y_ = y;
        }
    }
    void cancel() { active_ = false; }
    std::array<float, 2> tick(double seconds, int width = 240, int height = 320,
                              float gain = 1) const {
        (void)height;
        if (!active_)
            return {};
        const auto d = orientDelta(x_ - ox_, y_ - oy_, orientation_);
        const float distance = std::hypot(d[0], d[1]);
        if (distance == 0)
            return {};
        const float travel = std::min(distance / radius_, 1.f),
                    t = std::clamp((travel - .18f) / .82f, 0.f, 1.f);
        const float speed = float((5. / .03) * (width / 240.) * 1.5 * t * t * seconds) * gain;
        return {d[0] / distance * speed, d[1] / distance * speed};
    }
};
// Position control with a fresh neutral finger origin for every drag.
class RelativeTouch {
    float fingerX_ = 0, fingerY_ = 0, shipX_ = 0, shipY_ = 0;
    int width_ = 240, height_ = 320;
    bool active_ = false;

  public:
    void begin(float x, float y, float shipX, float shipY, int width = 240, int height = 320) {
        width_ = width;
        height_ = height;
        fingerX_ = x;
        fingerY_ = y;
        shipX_ = std::clamp(shipX, 0.f, float(width - 1));
        shipY_ = std::clamp(shipY, 0.f, float(height - 1));
        active_ = true;
    }
    void cancel() { active_ = false; }
    std::array<float, 2> target(float x, float y, float gain = 1) {
        if (!active_)
            return {shipX_, shipY_};
        shipX_ = std::clamp(shipX_ + (x - fingerX_) * gain, 0.f, float(width_ - 1));
        shipY_ = std::clamp(shipY_ + (y - fingerY_) * gain, 0.f, float(height_ - 1));
        fingerX_ = x;
        fingerY_ = y;
        return {shipX_, shipY_};
    }
};
class PauseMenu {
    TileMap map_{240, 320};
    std::vector<std::array<int, 4>> bounds_;
    std::vector<uint32_t> palette_ = std::vector<uint32_t>(129);
    float phase_ = 0;
    int selected_ = 0, pressed_ = -1;
    bool held_ = false;
    int direction_ = 1, pressedDirection_ = 0;
    std::array<std::array<int, 4>, 2> carets_{};
    std::vector<int> actions_;

  public:
    PauseMenu() {
        for (int i = 0; i < 80; ++i)
            animate();
    }
    static constexpr int Count = 9;
    std::string message;
    int direction() const { return direction_; }
    const auto &carets() const { return carets_; }
    int selected() const { return selected_; }
    const auto &bounds() const { return bounds_; }
    const auto &actions() const { return actions_; }
    void open() {
        selected_ = 0;
        pressed_ = -1;
        held_ = false;
        message.clear();
    }
    void animate() {
        phase_ += .1f;
        advanceTextPalette(palette_, phase_);
    }
    void rebuild(const PlayerOptions &o, bool touchAvailable = true,
                 bool displayAvailable = DisplaySelectionAvailable) {
        const int selectedAction = actions_.empty() ? 0 : actions_[size_t(selected_)];
        map_.clear();
        MenuSpec spec{
            "PAUSED",
            3,
            78,
            6,
            66,
            2,
            4,
            {{"Unpause", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}, {"", {}}}};
        const char *rotations[] = {"Upright", "Right", "Left", "Auto"};
        const char *modes[] = {"Windowed", "Borderless", "Fullscreen"};
        std::vector<std::string> labels = {
            "Unpause",
            o.original ? "Graphics: Original" : "Graphics: Enhanced",
            "Frame cap: " + std::to_string(o.cap),
            std::string("Display: ") + modes[int(o.display)],
            std::string("Orientation: ") + rotations[o.orientation],
            o.expanded ? "View: Draw to Edge" : "View: Letterbox",
            std::string("Touch control: ") + (o.control == 4 ? "Off" : std::to_string(o.control)),
            "Control Sensitivity: < " + std::to_string(o.sensitivity / 10) + "." +
                std::to_string(o.sensitivity % 10) + "x >",
            "Exit"};
        actions_ = {0, 1, 2, 3, 4, 7, 5, 8, 6};
        spec.items.resize(actions_.size());
        auto hide = [&](int action) {
            const auto found = std::find(actions_.begin(), actions_.end(), action);
            if (found == actions_.end())
                return;
            const auto index = found - actions_.begin();
            labels.erase(labels.begin() + index);
            spec.items.erase(spec.items.begin() + index);
            actions_.erase(found);
        };
        if (!touchAvailable)
            hide(5);
        if (!displayAvailable)
            hide(3);
        selected_ =
            int(std::find(actions_.begin(), actions_.end(), selectedAction) - actions_.begin());
        if (selected_ >= int(actions_.size()))
            selected_ = 0;
        if (actions_.size() > 8)
            spec.itemStep = 3;
        bounds_ = layoutMenu(map_, spec, 6, labels);
        const auto row = size_t(std::find(actions_.begin(), actions_.end(), 8) - actions_.begin());
        const int start = (240 - int(labels[row].size()) * 8) / 2, y = bounds_[row][1];
        const int left = start + int(labels[row].find('<')) * 8,
                  right = start + int(labels[row].find('>')) * 8;
        carets_ = {std::array<int, 4>{left - 4, y, left + 12, bounds_[row][3]},
                   std::array<int, 4>{right - 4, y, right + 12, bounds_[row][3]}};

        if (!message.empty())
            map_.centeredText(message.c_str(), 37, (68u << 16) | 0xff00u);
    }
    // Returns the activated row; horizontal changes use the supplied direction.
    int input(const FrontendInput &i) {
        if (i.vertical)
            selected_ = (selected_ + (i.vertical > 0 ? 1 : int(actions_.size()) - 1)) %
                        int(actions_.size());
        int hover = i.pointerActive ? menuHit(bounds_, i.pointer.x, i.pointer.y) : -1;
        if (hover >= 0)
            selected_ = hover;
        bool down = (i.pointer.held & 1) != 0;
        auto caret = [&]() {
            for (int n = 0; n < 2; ++n) {
                const auto &r = carets_[size_t(n)];
                if (i.pointer.x > r[0] && i.pointer.x < r[2] && i.pointer.y > r[1] &&
                    i.pointer.y < r[3])
                    return n ? 1 : -1;
            }
            return 0;
        };
        direction_ = i.horizontal < 0 ? -1 : 1;
        if (down && !held_) {
            pressed_ = hover;
            pressedDirection_ = caret();
        }
        int action = -1;
        if (i.pointerReleased && !down && pressed_ >= 0 && hover == pressed_) {
            if (actions_[size_t(hover)] != 8)
                action = hover;
            else if (pressedDirection_ && caret() == pressedDirection_) {
                action = hover;
                direction_ = pressedDirection_;
            }
        }
        if (!down)
            pressed_ = -1;
        held_ = down;
        if (i.confirm || i.horizontal)
            action = selected_;
        if (i.back)
            action = 0;
        return action < 0 ? action : actions_[size_t(action)];
    }
    void draw(Canvas &c) const {
        c.alphaFill({0, 40, 240, 272}, 0, .5f);
        map_.draw(c, palette_);
        if (bounds_.empty())
            return;
        const auto &r = bounds_[size_t(selected_)];
        auto color = packOriginalColor(palette_[64]);
        c.selection(r, color);
    }
};
} // namespace grpp
