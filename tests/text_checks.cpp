#include "text_observation.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: text_checks ASSETS ORIGINAL_TEXT_FIXTURE");
        grpp::Assets assets;
        assets.load(argv[1]);
        std::ifstream fixture(argv[2]);
        if (!fixture)
            throw std::runtime_error("Missing original text evidence");
        const std::string expected{std::istreambuf_iterator<char>(fixture), {}};
        if (expected != text_check::nativeObservations(assets))
            throw std::runtime_error("Original text/palette observations differ");
        std::cout << "160 palette frames, all 50 stage tile maps/rasters and two interludes match "
                     "original execution\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
