#pragma once
#include <array>
#include <cstddef>
#include <glad/gl.h>
namespace grpp {
enum class Uniform : std::size_t {
    Image,
    Foreground,
    Background,
    Mode,
    UvBounds,
    LogicalSize,
    RadianceScale,
    SampleStep,
    Opacity,
    ViewportSize,
    BeamStyle,
    RoundCull,
    PhosphorResponse,
    Radiance,
    Exposure,
    Geometry,
    Lattice,
    Shading,
    Info,
    PlaneCount,
    PixelScale,
    BeamWidth,
    Intensity,
    Orientation,
    BakeScale,
    AtlasSize,
    RadianceAtlas,
    CoverageAtlas,
    Count
};
struct RendererUniforms {
    unsigned program = 0;
    std::array<int, std::size_t(Uniform::Count)> values{};
    inline static constexpr std::array<const char *, std::size_t(Uniform::Count)> names = {
        "image",       "foreground",    "background",       "mode",        "uvBounds",
        "logicalSize", "radianceScale", "sampleStep",       "opacity",     "viewportSize",
        "beamStyle",   "roundCull",     "phosphorResponse", "radiance",    "exposure",
        "geometry",    "lattice",       "shading",          "info",        "planeCount",
        "pixelScale",  "beamWidth",     "intensity",        "orientation", "bakeScale",
        "atlasSize",   "radianceAtlas", "coverageAtlas"};
    void load(unsigned linkedProgram) {
        program = linkedProgram;
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = glGetUniformLocation(program, names[i]);
    }
    int get(Uniform id) const { return values[std::size_t(id)]; }
};
} // namespace grpp
