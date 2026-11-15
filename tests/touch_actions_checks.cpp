#include "grpp/touch_actions.hpp"
#include <stdexcept>
using namespace grpp;
void check(bool x) {
    if (!x)
        throw std::runtime_error("Touch action regression");
}
int main() {
    TouchActions a;
    a.down(2, 0, .5f, .5f);
    check(a.secondary() && !a.bomb());
    a.down(3, 10, .1f, .1f);
    check(a.bomb());
    a.up(3, 20);
    check(a.secondary());
    a.up(2, 30);
    a.sampled();
    check(!a.secondary() && !a.bomb());
    a.reset();
    a.down(2, 100, .5f, .5f);
    a.up(2, 150);
    a.down(9, 250, .52f, .5f);
    check(a.bomb());
    a.up(9, 260);
    check(!a.secondary());
    a.reset();
    a.down(2, 100, .5f, .5f);
    a.up(2, 150, true);
    a.down(2, 200, .5f, .5f);
    check(!a.bomb());
    a.reset();
    a.down(2, 100, .5f, .5f);
    a.move(2, .8f, .5f);
    a.up(2, 150);
    a.down(2, 200, .5f, .5f);
    check(!a.bomb());
    a.reset();
    a.down(2, 100, .5f, .5f);
    a.up(2, 150);
    a.down(2, 600, .5f, .5f);
    check(!a.bomb());
    a.reset();
    a.down(2, 0, .5f, .5f);
    a.down(3, 1, 0, 0);
    a.up(3, 2);
    a.up(2, 3);
    check(a.bomb());
    a.sampled();
    check(!a.bomb());
    a.reset();
    check(!a.secondary() && !a.bomb());
}
