#pragma once
#include "grpp/combat.hpp"
#include <algorithm>
#include <string>

namespace grpp {
// Source input/frame prelude: flash palette selection and byte-wise clear.
// This countdown is distinct from the source's unused camera-shake countdown.
uint16_t advanceFrameClear(int &flashTicks, uint32_t &clearColor);
// 004167ce: shift history and inject five oscillators once per logical frame.
void advanceTextPalette(std::vector<uint32_t> &palette, float phase);
struct GlyphEffect {
    int width, height, stride, trails, turnStep, depthTurn;
    std::vector<std::string> rows;
};
// Stateful source drawing is performed once per logical frame and cached for
// presentation. Higher host refresh rates never consume extra RNG or move a
// dying craft again.
class SourceVisuals {
  public:
    SourceVisuals(Canvas &canvas, const Assets &assets, PlayerState &player, GameRandom &random,
                  EffectRandom &effects)
        : canvas_(canvas), assets_(assets), player_(player), random_(random), effects_(effects) {}
    void frame(int number, float colorPhase) {
        frame_ = number;
        phase_ = colorPhase;
    }
    void blitterOffset(int x, int y) {
        offsetX_ = x;
        offsetY_ = y;
    }
    void entity(Entity &entity);
    void sprite(int id, int x, int y, uint16_t foreground = 0);
    void beamSprite(int id, int x, int y, uint16_t foreground = 0);
    static float beamDepthEnergy(int depth, int nearDepth = 4096) {
        return .45f +
               .55f * std::clamp(float(nearDepth) / float(std::max(depth, nearDepth)), 0.f, 1.f);
    }
    void pixel(int x, int y, uint16_t color);
    void cross(int x, int y, uint16_t color);
    void horizontal(int x, int y, int length, uint16_t color, uint32_t mask = ~0u,
                    bool patterned = false);
    void vertical(int x, int y, int length, uint16_t color, uint32_t mask = ~0u,
                  bool patterned = false);
    void lightning(int x1, int y1, int x2, int y2, int segments, uint16_t color);
    void background(int advance, const std::array<int, 6> &pattern, double subframe = 0,
                    double advancePerFrame = 0, bool referenceGeometry = true);
    uint32_t color(int index, float offset = 0) const;
    uint32_t noiseMask() { return effects_.next(); }
    int cameraX() const { return grpp::pixel(player_.worldX); }
    int cameraY() const { return grpp::pixel(player_.worldY); }
    int viewWidth() const { return canvas_.width(); }
    int viewHeight() const { return canvas_.height(); }

  private:
    static int pixel(int value) { return grpp::pixel(value); }
    void transformed(Entity &entity, bool world, bool fixed);
    void glyphTrail(Entity &entity, int mode);
    void pattern(uint32_t color, int ox, int oy, int width, int height, int rx, int ry, int shades,
                 int mode);
    void patternReference(uint32_t color, int ox, int oy, int width, int height, int rx, int ry,
                          int shades, int mode);
    float backgroundEnergy_ = 1;
    float vectorEnergy_ = 1;
    bool backgroundDrawing_ = false;
    float spriteOffsetX_ = 0, spriteOffsetY_ = 0;
    Canvas &canvas_;
    const Assets &assets_;
    PlayerState &player_;
    GameRandom &random_;
    EffectRandom &effects_;
    int frame_ = 0, offsetX_ = 0, offsetY_ = 0;
    float phase_ = 0;
};
} // namespace grpp
