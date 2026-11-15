#pragma once
#include "grpp/render.hpp"
namespace grpp {
// Mainline presentation policy, independent of gameplay randomness.
std::array<int, 6> stageBackground(unsigned stage);
struct GridPlane {
    double x, y, spacing, rx, ry;
    uint32_t color;
    float energy;
    int shades, mode, firstX, lastX, firstY, lastY;
};
std::vector<GridPlane> projectGrid(const DecorativeGrid &, double frameOffset = 0);
void drawPreciseBackground(Canvas &, const DecorativeGrid &, double frameOffset = 0,
                           int compact = 0);
void drawPreciseStars(Canvas &, const DecorativeStars &, double frameOffset = 0);
Canvas resampleDecoration(const Canvas &, double frameOffset, int compact = 0);
// Interpolates only a copy of the decorative recipe, never a game entity.
class DecorativeCamera {
  public:
    void capture(const Canvas &, uint64_t continuity);
    void apply(Canvas &, double fraction) const;
    void reset() { valid_ = false; }

  private:
    bool valid_ = false;
    uint64_t continuity_ = 0;
    double previousX_ = 0, previousY_ = 0, currentX_ = 0, currentY_ = 0;
};
} // namespace grpp
