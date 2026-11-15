#include "grpp/assets.hpp"
#include "grpp/numeric.hpp"
#include "original_tables.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace grpp {
namespace {
uint16_t read16(const std::vector<uint8_t> &b, size_t p) {
    return uint16_t(b.at(p) | uint16_t(b.at(p + 1)) << 8);
}
} // namespace
Image loadTga(const std::filesystem::path &file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read TGA: " + file.string());
    return decodeTga({std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()});
}
Image decodeTga(const std::vector<uint8_t> &b) {
    if (b.size() < 18 || b[1] != 0 || b[2] != 2 || b[16] != 16)
        throw std::runtime_error("Expected an uncompressed, non-paletted 16-bit TGA");
    Image image;
    image.width = read16(b, 12);
    image.height = read16(b, 14);
    if (image.width < 1 || image.height < 1 || image.width > 4096 || image.height > 4096)
        throw std::runtime_error("Invalid TGA dimensions");
    const size_t start = 18 + size_t(b[0]);
    const size_t count = size_t(image.width) * image.height;
    if (start > b.size() || (b.size() - start) / 2 < count)
        throw std::runtime_error("Truncated TGA pixels");
    image.pixels.resize(count);
    const bool topOrigin = (b[17] & 0x20) != 0, rightOrigin = (b[17] & 0x10) != 0;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const int sourceY = topOrigin ? y : image.height - 1 - y;
            const int sourceX = rightOrigin ? image.width - 1 - x : x;
            const uint16_t p = read16(b, start + 2 * (size_t(sourceY) * image.width + sourceX));
            // 0040803e: 5:5:5 input -> 5:6:5, green low bit stays zero.
            image.pixels[size_t(y) * image.width + x] =
                uint16_t(((p & 0x7c00) << 1) | ((p & 0x03e0) << 1) | (p & 31));
        }
    return image;
}
Rgba expand565(uint16_t p, bool colorKey) noexcept {
    if (colorKey && p == 0)
        return {0, 0, 0, 0};
    const unsigned r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)),
            uint8_t((b << 3) | (b >> 2)), 255};
}
uint16_t packOriginalColor(uint32_t color) noexcept {
    return uint16_t(((color >> 16) & 0xf800u) | (((color >> 18) & 63u) << 5) |
                    ((color >> 11) & 31u));
}
uint32_t cycleColor(const ColorCycle &cycle, float phase) {
    uint32_t packed = 0;
    for (unsigned c = 0; c < 3; ++c) {
        // Original 00407c43/00407d5d uses a signed half-range and x87 double math.
        // Truncate after the sine and scale; integer interpolation changes color steps.
        const auto half = asr32(sub32(cycle.maximum[c], cycle.minimum[c]), 1);
        const auto channel =
            int32_t((std::sin(double(phase) * double(cycle.rate[c])) + 1.0) * half) +
            cycle.minimum[c];
        packed |= (uint32_t(channel) & 255u) << (24 - 8 * c);
    }
    return packed;
}
uint16_t combine565(uint16_t a, uint16_t b) noexcept {
    // Literal packed-channel mechanism from 00405df0. This is *not* alpha averaging.
    uint16_t sum = uint16_t(((a >> 1) & 0x7bef) + ((b >> 1) & 0x7bef));
    if (sum & 0x8410) {
        if (sum & 0x8000)
            sum = uint16_t((sum & 0x07ff) + 0x7800);
        if (sum & 0x0400)
            sum = uint16_t((sum & 0xf81f) + 0x03e0);
        if (sum & 0x0010)
            sum = uint16_t((sum & 0xffe0) + 0x000f);
    }
    return uint16_t(sum << 1);
}
void Assets::load(const std::filesystem::path &root) {
    root_ = root;
    sprites_.assign(original::sprites.begin(), original::sprites.end());
    for (size_t i = 0; i < pages_.size(); ++i) {
        const auto &spec = original::sheets[i];
        if (*spec.filename)
            pages_[i] = loadTga(arse_ && std::filesystem::exists(root / "aof" / spec.filename)
                                    ? root / "aof" / spec.filename
                                    : root / spec.filename);
        else
            pages_[i] = {spec.width, spec.height,
                         std::vector<uint16_t>(size_t(spec.width) * spec.height)};
        if (pages_[i].width != spec.width || pages_[i].height != spec.height)
            throw std::runtime_error("Original sheet dimensions mismatch: " +
                                     std::string(spec.filename));
    }
    atlasX_ = atlasY_ = atlasBottom_ = 0;
}
void Assets::loadCampaignImage() {
    pages_[5] = loadTga(root_ / "es.tga");
    if (pages_[5].width != 256 || pages_[5].height != 256)
        throw std::runtime_error("Campaign image dimensions differ from source");
}
Image Assets::extract(size_t index) const {
    const auto &s = sprite(index);
    const auto &sheet = page(size_t(s.sheet));
    if (s.width < 0 || s.height < 0 || s.x < 0 || s.y < 0 || s.x + s.width > sheet.width ||
        s.y + s.height > sheet.height)
        throw std::runtime_error("Sprite rectangle is outside its source sheet");
    Image image{s.width, s.height, std::vector<uint16_t>(size_t(s.width) * s.height)};
    for (int y = 0; y < s.height; ++y)
        std::copy_n(sheet.pixels.begin() + size_t(s.y + y) * sheet.width + s.x, s.width,
                    image.pixels.begin() + size_t(y) * s.width);
    return image;
}
void Assets::appendGenerated(Image image, size_t descriptor) {
    // Preserve the source's early next-row test and running maximum height.
    if (atlasBottom_ < atlasY_ + image.height)
        atlasBottom_ = atlasY_ + image.height;
    if (255 < atlasX_ + image.width) {
        atlasX_ = 0;
        atlasY_ = atlasBottom_;
    }
    if (atlasY_ + image.height > pages_[5].height)
        throw std::runtime_error("Generated sprite atlas is full");
    if (descriptor >= sprites_.size())
        throw std::runtime_error("Generated sprite descriptor out of bounds");
    sprites_[descriptor] = {5, atlasX_, atlasY_, image.width, image.height};
    for (int y = 0; y < image.height; ++y)
        std::copy_n(image.pixels.begin() + size_t(y) * image.width, image.width,
                    pages_[5].pixels.begin() + size_t(atlasY_ + y) * pages_[5].width + atlasX_);
    atlasX_ += image.width;
}
void Assets::makeOrientations(size_t sourceSprite, size_t family) {
    // 00408a9e: two source animation frames, four rotations of each. The
    // descriptor base is 0x74, not 0x78 (the latter is the first write).
    const std::array<int, 8> descriptorOrder{4, 0, 2, 6, 3, 7, 1, 5};
    for (size_t step = 0; step < descriptorOrder.size(); ++step) {
        const Image source = extract(sourceSprite + (step >= 4 ? 1 : 0));
        const int orientation = int(step % 4);
        const bool swap = orientation >= 2;
        Image image{swap ? source.height : source.width, swap ? source.width : source.height, {}};
        image.pixels.resize(size_t(image.width) * image.height);
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) {
                int sx = x, sy = y;
                switch (orientation) {
                case 0:
                    break;
                case 1:
                    sx = source.width - 1 - x;
                    sy = source.height - 1 - y;
                    break;
                case 2:
                    sx = y;
                    sy = source.height - 1 - x;
                    break;
                case 3:
                    sx = source.width - 1 - y;
                    sy = x;
                    break;
                }
                image.pixels[size_t(y) * image.width + x] = source.at(sx, sy);
            }
        appendGenerated(std::move(image), 116 + family * 8 + size_t(descriptorOrder[step]));
    }
}
} // namespace grpp
