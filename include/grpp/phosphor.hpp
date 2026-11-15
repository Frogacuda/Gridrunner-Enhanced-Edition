#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace grpp {
inline constexpr float BeamIntensity = .65f, NearBackgroundIntensity = .9f, PointBoost = 1.2f,
                       IntersectionBoost = 1.2f, MaximumWhitening = .5f,
                       RadianceEncodingScale = .5f;
inline float backgroundBeamEnergy(double depth) {
    const double original = (.25 + .75 * std::clamp(512. / depth, 0., 1.)) *
                            (.45 + .55 * std::clamp(2048. / depth, 0., 1.));
    double near = std::clamp((2048. - depth) / 1536., 0., 1.);
    near = near * near * (3 - 2 * near);
    return float(original + near * (double(NearBackgroundIntensity) / BeamIntensity - 1.));
}
inline std::array<float, 3> resolvePhosphor(std::array<float, 3> light, float coverage) {
    float join = std::clamp(coverage - 1.f, 0.f, 1.f);
    join = join * join * (3 - 2 * join);
    for (auto &channel : light)
        channel *= 1 + (IntersectionBoost - 1) * join;
    const float peak = std::max({light[0], light[1], light[2]});
    const float whitening =
        MaximumWhitening * (1 - std::exp(-std::max(peak - 1, 0.f) / MaximumWhitening));
    for (auto &channel : light) {
        channel /= std::max(1.f, peak);
        channel += (1 - channel) * whitening;
    }
    return light;
}
} // namespace grpp
