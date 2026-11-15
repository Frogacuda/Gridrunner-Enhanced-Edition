#pragma once
#include "grpp/render.hpp"
namespace grpp {
struct OriginalOverscan {
    Canvas actors;
    std::vector<uint16_t> background;
    int padX = 0, padY = 0;
};
// Original integer pattern raster, extended around the existing source viewport.
OriginalOverscan originalOverscan(const Canvas &, const Assets &, int width, int height, Aspect,
                                  double seconds, bool boundaries = true);
} // namespace grpp
