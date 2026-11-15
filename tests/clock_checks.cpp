#include "grpp/clock.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace grpp;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: clock_checks OUTPUT_CSV");
        std::ofstream report(argv[1]);
        if (!report)
            throw std::runtime_error("Cannot write clock report");
        report << "display_hz,seconds,old_ticks,new_ticks,expected_ticks\n";
        for (uint64_t hz : {30, 60, 90, 100, 120, 144, 165, 240}) {
            FixedCadence clock;
            uint64_t oldDeadline = 0;
            int oldTicks = 0, newTicks = 0;
            for (uint64_t frame = 0; frame <= hz * 30; ++frame) {
                auto now = frame * 1'000'000'000 / hz;
                if (now >= oldDeadline) {
                    ++oldTicks;
                    oldDeadline = now + FixedCadence::Period;
                }
                newTicks += clock.pending(now);
            }
            if (newTicks != 1001)
                throw std::runtime_error("Display refresh altered simulation rate");
            report << hz << ",30," << oldTicks << ',' << newTicks << ",1001\n";
        }
        FixedCadence clock;
        clock.pending(0);
        if (clock.pending(1'000'000'000) != 4 || clock.pending(1'000'000'000) != 0)
            throw std::runtime_error("Long stall catch-up is unbounded");
        clock.reset(9'000'000'000);
        if (clock.pending(9'000'000'000) != 1)
            throw std::runtime_error("Focus/pause reset accumulated stale time");
        std::cout << "30 ms simulation: 1001 ticks including both endpoints of 30 seconds at "
                     "30..240 Hz; pause/stall bounds pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
