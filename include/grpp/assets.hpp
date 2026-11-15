#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace grpp {
struct SheetSpec {
    const char *filename;
    int width, height;
};
struct SpriteRect {
    int sheet, x, y, width, height;
};
struct ColorCycle {
    std::array<float, 4> rate;
    std::array<int32_t, 4> minimum, maximum;
};
struct Image {
    int width = 0, height = 0;
    std::vector<uint16_t> pixels;
    uint16_t at(int x, int y) const { return pixels.at(size_t(y) * width + x); }
};
struct Rgba {
    uint8_t r, g, b, a;
};
static_assert(sizeof(Rgba) == 4);
Image loadTga(const std::filesystem::path &file);
Image decodeTga(const std::vector<uint8_t> &bytes);
Rgba expand565(uint16_t value, bool colorKey = true) noexcept;
uint16_t packOriginalColor(uint32_t rrggbb00) noexcept;
uint32_t cycleColor(const ColorCycle &cycle, float phase);
uint16_t combine565(uint16_t a, uint16_t b) noexcept;

class Assets {
  public:
    void load(const std::filesystem::path &root);
    void arse(bool enabled) { arse_ = enabled; }
    bool arse() const { return arse_; }
    const Image &page(size_t index) const { return pages_.at(index); }
    const SpriteRect &sprite(size_t index) const { return sprites_.at(index); }
    Image extract(size_t index) const;
    size_t spriteCount() const { return sprites_.size(); }
    const std::filesystem::path &root() const { return root_; }
    // Recovered level setup generates four rotations of each of two adjacent
    // source frames into page 5 (eight descriptors per family).
    // Original placement order and descriptor numbering are retained.
    void makeOrientations(size_t sourceSprite, size_t family);
    void loadCampaignImage(); // Source temporarily replaces generated page 5.
  private:
    void appendGenerated(Image image, size_t descriptor);
    std::array<Image, 10> pages_;
    std::vector<SpriteRect> sprites_;
    std::filesystem::path root_;
    bool arse_ = false;
    int atlasX_ = 0, atlasY_ = 0, atlasBottom_ = 0;
};
} // namespace grpp
