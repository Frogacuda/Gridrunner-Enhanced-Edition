#include "grpp/visuals.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: frame_checks ORIGINAL_FRAME_FIXTURE");
        std::ifstream fixture(argv[1]);
        if (!fixture)
            throw std::runtime_error("Missing original frame-prelude fixture");
        int count, frame, ticks, expectedTicks, rows = 0;
        uint32_t initial, color, expectedColor, expectedClear, random;
        std::string kind;
        while (fixture >> kind >> count >> initial >> frame >> expectedTicks >> expectedColor >>
               expectedClear >> random) {
            if (kind != "frame-clear")
                throw std::runtime_error("Unexpected frame fixture row");
            if (frame == 0) {
                ticks = count;
                color = initial;
            }
            auto clear = grpp::advanceFrameClear(ticks, color);
            if (ticks != expectedTicks || color != expectedColor || clear != expectedClear ||
                random != 0x1982)
                throw std::runtime_error("Original frame prelude mismatch at row " +
                                         std::to_string(rows));
            ++rows;
        }
        if (!fixture.eof() || rows != 585)
            throw std::runtime_error("Incomplete frame-prelude evidence");
        std::cout << rows << " original frame-prelude comparisons passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
