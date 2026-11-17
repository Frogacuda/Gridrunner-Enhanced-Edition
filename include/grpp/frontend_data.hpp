#pragma once
#include "grpp/mechanisms.hpp"
namespace grpp {
struct MenuItem {
    const char *label;
    std::array<int, 4> bounds;
    int color = -1;
};
struct MenuSpec {
    const char *title;
    int row, titleColor, gap, firstColor, colorStep, itemStep;
    std::vector<MenuItem> items;
};
struct TextLine {
    const char *text;
    int row, color;
};
} // namespace grpp
