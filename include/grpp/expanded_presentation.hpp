#pragma once
#include "grpp/render.hpp"

namespace grpp {
// Output-only composition. The original viewport still defines gameplay/input.
Canvas expandedPresentation(const Canvas &source, int pixelWidth, int pixelHeight, Aspect aspect,
                            double frameOffset, double seconds, bool boundaries = true,
                            int compact = 0);
} // namespace grpp
