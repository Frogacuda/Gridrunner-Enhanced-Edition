#pragma once
#include "grpp/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace grpp {
constexpr int LogicalWidth = 240, LogicalHeight = 320;
struct Rect {
    int x, y, width, height;
};
struct Paint {
    bool keyed = true;
    uint16_t foreground = 0, background = 0;
};
enum class HudAnchor { None, BottomLeft, BottomRight, TopRight, BottomCenter };
struct Draw {
    int sheet = -1;
    Rect source{};
    float x = 0, y = 0, width = 0, height = 0, angle = 0;
    Paint paint{};
    uint16_t color = 0;
    bool direct = false; // Source direct framebuffer primitive, already clipped.
    bool referenceOnly = false;
    bool squareFragment = false;
    bool silhouette = false;
    bool patternedBeam = false;
    float energy = 1, beamWidth = 1;
    int rampSteps = 0;
    uint16_t rampEndColor = 0;
    uint32_t rampColor = 0; // Enhanced ramp: +N fused, -N smooth, -(N+32) compensated.
    float endX = 0, endY = 0;
    float subpixelX = 0, subpixelY = 0;
    bool decorative = false;
    bool referenceGeometry = false;
    float opacity = 1;
    HudAnchor hudAnchor = HudAnchor::None;
    bool introGradient = false, smoothIntro = false, titleLogo = false, legacyTitleCredit = false;
    bool levelSelectArrow = false;
    int spriteId = -1; // Presentation replacements retain source UVs, including fragments.
};
// Only the noninteractive procedural background is eligible for resampling.
struct DecorativeGrid {
    bool valid = false;
    double frame = 0, advance = 0, advancePerFrame = 0, worldX = 0, worldY = 0;
    std::array<int, 6> pattern{};
    std::array<uint32_t, 3> colors{};
    size_t first = 0, last = 0;
    int width = 240, height = 320, cameraY = 240, projection = 262144;
    int sourceWidth = 0, sourceHeight = 0;
    double offsetX = 0, offsetY = 0;
    int gpuMode = 0;
    double displayScaleX = 1, displayScaleY = 1, beamWidthScale = 1;
};
struct DecorativeStars {
    bool valid = false;
    int frame = 0;
    std::array<std::array<int32_t, 3>, 1000> stars{};
    size_t first = 0, last = 0;
};
struct PlayfieldBounds {
    bool valid = false;
    int width = 0, height = 0;
    double originX = 0,
           originY = 0; // Actual world pan in source pixels, not decorative interpolation.
};
class Canvas {
  public:
    void clear(uint16_t color = 0);
    void dimensions(int width, int height) {
        width_ = width;
        height_ = height;
    }
    int width() const { return width_; }
    int height() const { return height_; }
    void centeredUi(const Canvas &ui);
    void sprite(const Assets &assets, size_t index, int cx, int cy, Paint paint = {});
    void image(int sheet, Rect source, float x, float y, float width, float height,
               Paint paint = {}, float angle = 0);
    void fill(Rect rectangle, uint16_t color);
    void rawFill(Rect rectangle, uint16_t color);
    void alphaFill(Rect rectangle, uint16_t color, float opacity);
    void selection(std::array<int, 4> bounds, uint16_t color);
    void line(int x, int y, int width, int height, uint16_t color);
    void text(const char *text, int x, int y, uint16_t foreground = 0xffff,
              uint16_t background = 0);
    // Enhanced presentation metadata; never consumed by the reference raster.
    void beam(float ax, float ay, float bx, float by, uint16_t color, float energy = 1,
              float width = 1, bool patterned = false);
    void referenceOnlySince(size_t first);
    void smoothIntroSince(size_t first) {
        referenceOnlySince(first);
        for (size_t i = first; i < draws_.size(); ++i)
            draws_[i].introGradient = true;
        draws_.at(first).referenceOnly = false;
        draws_.at(first).smoothIntro = true;
    }
    void markTitleLogo() { draws_.back().titleLogo = true; }
    void markLevelSelectArrow() { draws_.back().levelSelectArrow = true; }
    void identifySpriteLast(int id) { draws_.back().spriteId = id; }
    void markTitleCreditSince(size_t first, float y) {
        for (size_t i = first; i < draws_.size(); ++i)
            if (draws_[i].y == y)
                draws_[i].legacyTitleCredit = true;
    }
    void coalescePatternDotsSince(size_t first);
    void markFragment() {
        if (!draws_.empty())
            draws_.back().squareFragment = true;
    }
    void markSilhouette() {
        if (!draws_.empty())
            draws_.back().silhouette = true;
    }
    void subpixelLast(float x, float y) {
        if (!draws_.empty()) {
            draws_.back().subpixelX = x;
            draws_.back().subpixelY = y;
        }
    }
    const std::vector<Draw> &draws() const { return draws_; }
    void appendDraw(const Draw &draw) { draws_.push_back(draw); }
    void anchorSince(size_t first, HudAnchor anchor) {
        for (size_t i = first; i < draws_.size(); ++i)
            draws_[i].hudAnchor = anchor;
    }
    void translate(float x, float y) {
        for (auto &d : draws_) {
            d.x += x;
            d.y += y;
            d.endX += x;
            d.endY += y;
        }
    }
    void append(const Canvas &other, size_t first = 0) {
        draws_.insert(draws_.end(), other.draws_.begin() + first, other.draws_.end());
    }
    const DecorativeGrid &decorativeGrid() const { return decorative_; }
    void decorativeGrid(const DecorativeGrid &grid) { decorative_ = grid; }
    const PlayfieldBounds &playfield() const { return playfield_; }
    void playfield(const PlayfieldBounds &bounds) { playfield_ = bounds; }
    void markDecorativeLast() { draws_.back().decorative = true; }
    void markReferenceGeometrySince(size_t first) {
        for (size_t i = first; i < draws_.size(); ++i)
            if (draws_[i].sheet == -3 && !draws_[i].referenceOnly)
                draws_[i].referenceGeometry = true;
    }
    void replaceDecoration(const Canvas &replacement);
    const DecorativeStars &starField() const { return stars_; }
    void starField(const DecorativeStars &stars) { stars_ = stars; }
    void replaceStars(const Canvas &replacement);
    uint16_t clearColor() const { return clear_; }

  private:
    std::vector<Draw> draws_;
    uint16_t clear_ = 0;
    int width_ = 240, height_ = 320;
    DecorativeGrid decorative_{};
    DecorativeStars stars_{};
    PlayfieldBounds playfield_{};
};

// Reference software rasterizer. The original blitter treats clip right/bottom
// as exclusive even though 00407ea0 initialized them to width-1/height-1.
// Keep that discrepancy explicit; do not silently repair it during a port.
class Raster565 {
  public:
    void render(const Canvas &canvas, const Assets &assets,
                const std::vector<uint16_t> *initialPixels = nullptr);
    const std::vector<uint16_t> &pixels() const { return pixels_; }
    void setClip(Rect clip) {
        clip_ = clip;
        customClip_ = true;
    }

  private:
    std::vector<uint16_t> pixels_;
    bool customClip_ = false;
    Rect clip_{0, 0, LogicalWidth - 1, LogicalHeight - 1};
};

// Coordinates for rendering into a quarter-turned physical display.
inline bool orientationSwapsAxes(int orientation) { return orientation == 1 || orientation == 2; }
std::array<float, 2> orientationPointer(float x, float y, int width, int height, int orientation);
float controllerGain(float magnitude);
enum class Aspect { Fit, Integer, Stretch };
struct Viewport {
    int x = 0, y = 0, width = 0, height = 0;
    int logicalWidth = 240, logicalHeight = 320;
    bool toLogical(float px, float py, float &x, float &y) const;
};
Viewport makeViewport(int pixelWidth, int pixelHeight, Aspect mode, int logicalWidth = 240,
                      int logicalHeight = 320);

class TileMap {
  public:
    TileMap(int width, int height, int tileWidth = 8, int tileHeight = 8, int sheet = 0);
    void clear(uint32_t cell = 32);
    void put(int x, int y, uint32_t cell);
    uint32_t get(int x, int y) const;
    void text(const char *string, int x, int y, int colorIndex = 1);
    // 0040a583 uses allocated columns; hit rectangle includes an extra tile.
    Rect centeredText(const char *string, int row, uint32_t attributes);
    void scroll(int dx, int dy);
    void draw(Canvas &canvas, const std::vector<uint32_t> &palette) const;
    int columns() const { return columns_; }
    int rows() const { return rows_; }

  private:
    int columns_, rows_, tileWidth_, tileHeight_, sheet_;
    int scrollX_ = 0, scrollY_ = 0, columnOffset_ = 0, rowOffset_ = 0;
    std::vector<uint32_t> cells_;
};
} // namespace grpp
