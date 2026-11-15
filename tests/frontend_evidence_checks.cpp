#include "frontend_observation.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: frontend_evidence_checks ASSETS ORIGINAL_FIXTURE");
        grpp::Assets assets;
        assets.load(argv[1]);
        std::ifstream input(argv[2]);
        if (!input)
            throw std::runtime_error("Missing original frontend evidence");
        const std::string expected{std::istreambuf_iterator<char>(input), {}};
        if (expected != frontend_check::nativeEvidence(assets))
            throw std::runtime_error("Original frontend observations differ");
        std::cout << "Original menu geometry/hits, 45 rankings, 450 stage records, 900 "
                     "checkpoints, nine star rasters and eight reader pages pass\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
