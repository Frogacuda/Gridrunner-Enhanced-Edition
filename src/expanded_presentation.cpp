#include "grpp/expanded_presentation.hpp"
#include "grpp/background.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grpp {
Canvas expandedPresentation(const Canvas &source, int width, int height, Aspect aspect,
                            double frameOffset, double seconds, bool boundaries, int compact) {
    if (width < 1 || height < 1 || !std::isfinite(seconds))
        throw std::invalid_argument("Invalid expanded presentation");
    Canvas result;
    result.dimensions(width, height);
    result.clear(source.clearColor());
    const auto v = makeViewport(width, height, aspect, source.width(), source.height());
    const float sx = float(v.width) / source.width(), sy = float(v.height) / source.height();
    auto map = [&](Draw d, float x, float y) {
        d.x = d.x * sx + x;
        d.y = d.y * sy + y;
        d.width *= sx;
        d.height *= sy;
        d.endX = d.endX * sx + x;
        d.endY = d.endY * sy + y;
        d.subpixelX *= sx;
        d.subpixelY *= sy;
        // BeamMesh scales stroke width by viewport height. Preserve the source
        // viewport's thickness when vertical overscan makes the output taller.
        if (d.sheet == -3)
            d.beamWidth *= float(v.height) / height;
        result.appendDraw(d);
    };
    if (source.decorativeGrid().valid) {
        auto grid = source.decorativeGrid();
        grid.sourceWidth = source.width();
        grid.sourceHeight = source.height();
        grid.width = int(std::ceil(width / sx));
        grid.height = int(std::ceil(height / sy));
        grid.offsetX = v.x / sx;
        grid.offsetY = v.y / sy;
        Canvas background;
        drawPreciseBackground(background, grid, frameOffset, compact);
        if (compact >= 4) {
            grid.gpuMode = compact;
            grid.first = 0;
            grid.last = 1;
            grid.displayScaleX = sx;
            grid.displayScaleY = sy;
            grid.beamWidthScale = float(v.height) / height;
            result.decorativeGrid(grid);
        }
        for (const auto &d : background.draws())
            map(d, 0, 0);
    }
    for (const auto &d : source.draws()) {
        if (d.referenceOnly || d.decorative)
            continue;
        float x = float(v.x), y = float(v.y);
        if (d.hudAnchor == HudAnchor::BottomLeft) {
            x = 0;
            y = float(height - v.height);
        } else if (d.hudAnchor == HudAnchor::BottomRight) {
            x = float(width - v.width);
            y = float(height - v.height);
        } else if (d.hudAnchor == HudAnchor::TopRight) {
            x = float(width - v.width);
            y = 0;
        } else if (d.hudAnchor == HudAnchor::BottomCenter)
            y = float(height - v.height);
        map(d, x, y);
    }
    if (boundaries && source.playfield().valid) {
        // Green at 40% -> cyan at 70% -> green, over two seconds.
        const float pulse = float((1 - std::cos(seconds * 3.141592653589793)) * .5);
        const uint32_t blue = uint32_t(std::lround(255 * pulse));
        const uint16_t ink = packOriginalColor(0x00ff0000u | (blue << 8));
        const float energy = .4f + .3f * pulse;
        const float dash = 4.f * std::min(sx, sy), period = dash * 2;
        auto line = [&](float x, float y, float length, bool vertical) {
            for (float a = 0; a < length; a += period) {
                const float b = std::min(a + dash, length);
                // Dashes are explicit segments: use the regular glowing vector
                // profile rather than the thin patterned-grid profile.
                result.beam(x + (vertical ? 0 : a), y + (vertical ? a : 0), x + (vertical ? 0 : b),
                            y + (vertical ? b : 0), ink, energy, 1.2f * v.height / height, false);
            }
        };
        const auto &field = source.playfield();
        const float left = float(v.x - field.originX * sx),
                    right = float(v.x + (field.width - field.originX) * sx);
        const float top = float(v.y - field.originY * sy),
                    bottom = float(v.y + (field.height - field.originY) * sy);
        line(left, top, right - left, false);
        line(left, bottom, right - left, false);
        line(left, top, bottom - top, true);
        line(right, top, bottom - top, true);
    }
    return result;
}
} // namespace grpp
