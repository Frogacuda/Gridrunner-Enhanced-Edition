#pragma once
#include "grpp/visuals.hpp"
#include "original_levels.hpp"
#include <cstdio>
#include <sstream>
namespace text_check {
template <class Pixels> inline uint64_t pixelHash(const Pixels &pixels) {
    uint64_t h = 14695981039346656037ull;
    for (auto p : pixels) {
        h = (h ^ uint8_t(p)) * 1099511628211ull;
        h = (h ^ uint8_t(p >> 8)) * 1099511628211ull;
    }
    return h;
}
inline std::string nativeObservations(const grpp::Assets &assets) {
    using namespace grpp;
    std::ostringstream out;
    std::vector<uint32_t> palette(129, 0);
    float phase = 0;
    for (int tick = 0; tick < 160; ++tick) {
        advanceTextPalette(palette, phase);
        out << "palette " << tick;
        for (auto value : palette)
            out << ' ' << value;
        out << '\n';
        phase += .1f;
    }
    for (size_t stage = 0; stage < 52; ++stage) {
        TileMap map(240, 320);
        char title[48];
        std::snprintf(title, sizeof title, "LEVEL %u", unsigned(stage + 1));
        const auto rect = stage < 50 ? map.centeredText(title, 18, 0x44ff00)
                                     : map.centeredText("SHEEPIEZAPPER RECHARGE!", 19, 0x41ff00);
        if (stage < 50)
            map.centeredText(original::stages[stage].caption, 22, 0x41ff00);
        if (stage == 51)
            map.centeredText("NEW LEVEL HI SCORE!", 23, 0x44ff00);
        Canvas canvas;
        canvas.clear(0x1234);
        map.draw(canvas, palette);
        Raster565 raster;
        raster.render(canvas, assets);
        out << "text " << stage << ' ' << rect.x << ' ' << rect.y << ' ' << rect.width << ' '
            << rect.height << ' ' << pixelHash(raster.pixels());
        for (int y = 0; y < map.rows(); ++y)
            for (int x = 0; x < map.columns(); ++x)
                out << ' ' << map.get(x, y);
        out << '\n';
    }
    return out.str();
}
} // namespace text_check
