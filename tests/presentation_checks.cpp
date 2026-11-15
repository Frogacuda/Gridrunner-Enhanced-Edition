#include "grpp/clock.hpp"
#include "grpp/presentation_clock.hpp"
#include <iostream>
#include <stdexcept>
using namespace grpp;
void require(bool c, const char *m) {
    if (!c)
        throw std::runtime_error(m);
}
int main() {
    try {
        for (auto rate : {60u, 120u, 144u, 165u, 240u, 60000u}) {
            const unsigned denominator = rate == 60000 ? 1001 : 1;
            PresentationClock display;
            display.rate(0, rate, denominator);
            FixedCadence motion(0, 10'000'000);
            uint64_t frames = 0, updates = 0;
            for (uint64_t ms = 0; ms <= 60000; ++ms) {
                const auto now = ms * 1'000'000;
                updates += motion.pending(now);
                frames += display.pending(now);
                require(!display.pending(now), "Presentation emitted two frames at one timestamp");
            }
            require(frames == uint64_t(60) * rate / denominator + 1,
                    "Rational presentation rate drifted");
            require(updates == 6001, "Display cadence changed simulation timing");
        }
        PresentationClock display;
        FixedCadence motion(0, 10'000'000);
        unsigned frames = 0, updates = 0;
        const unsigned rates[]{60, 144, 60000, 240, 0, 120};
        for (uint64_t ms = 0; ms <= 30000; ++ms) {
            auto now = ms * 1'000'000;
            if (ms % 5000 == 0 && ms < 30000) {
                auto rate = rates[ms / 5000];
                display.rate(now, rate, rate == 60000 ? 1001 : 1);
            }
            updates += motion.pending(now);
            frames += display.pending(now);
            require(!display.pending(now), "Refresh change caused a frame burst");
        }
        require(updates == 3001 && frames > 3410 && frames < 3430,
                "Realtime refresh switches lost time");
        display.rate(30'000'000'000, 0, 0);
        require(display.numerator() == 60 && display.denominator() == 1,
                "Unknown refresh did not fall back to 60 Hz");
        require(display.pending(40'000'000'000) && !display.pending(40'000'000'000),
                "Stall created an unbounded presentation backlog");
        std::cout << "Presentation: 60/120/144/165/240/59.94 Hz, live rate changes, 60 Hz "
                     "fallback, drift, stall and independent 100 Hz simulation pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
