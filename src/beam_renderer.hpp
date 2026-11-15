#pragma once
#include "grpp/background.hpp"
#include "grpp/render.hpp"
#include "renderer_uniforms.hpp"
#include "vector_scene.h"
#include <unordered_map>
namespace grpp {
class BeamRenderer {
  public:
    BeamRenderer();
    ~BeamRenderer();
    void beginFrame(int width, int height);
    void endFrame(unsigned target, int width, int height, int orientation = 0);
    struct AtlasStats {
        int bakedPlanes = 0, directPlanes = 0, stamps = 0;
        uint64_t bytes = 0;
    };
    AtlasStats atlasStats() const { return atlasStats_; }
    void drawGrid(const DecorativeGrid &, double frameOffset, int width, int height,
                  int logicalWidth, int logicalHeight, int x, int y);
    void draw(const std::vector<Draw> &commands, int width, int height, int logicalWidth = 240,
              int logicalHeight = 320, int x = 0, int y = 0);

  private:
    struct Prepared {
        neon::vectors::Stroke stroke;
        int profile;
    };
    struct Key {
        int x, y, profile;
        float width;
        bool operator==(const Key &b) const {
            return x == b.x && y == b.y && profile == b.profile && width == b.width;
        }
    };
    struct Hash {
        size_t operator()(const Key &k) const {
            return std::hash<int>{}(k.x) ^ (std::hash<int>{}(k.y) << 1) ^
                   (std::hash<float>{}(k.width) << 2) ^ size_t(k.profile);
        }
    };
    struct Scratch {
        std::vector<neon::vectors::Stroke> strokes;
        std::vector<Prepared> prepared;
        std::unordered_map<Key, std::pair<size_t, float>, Hash> faintPoints;
    };
    Scratch scratch_;
    RendererUniforms sampleUniforms_, resolveUniforms_, gridUniforms_[2];
    unsigned gridPrograms_[2]{};
    void drawGridPlanes(const std::vector<GridPlane> &, float sx, float sy, float beamWidth,
                        int width, int height, int x, int y, float bakeScale = 1);
    void drawAtlas(const std::vector<GridPlane> &, float sx, float sy, float beamWidth, int width,
                   int height, int x, int y);
    struct Stamp {
        float x0, y0, x1, y1, u0, v0, u1, v1;
    };
    std::vector<Stamp> stamps_;
    unsigned atlasFramebuffer_ = 0, atlasLight_ = 0, atlasCoverage_ = 0, stampProgram_ = 0,
             stampVao_ = 0, stampBuffer_ = 0;
    RendererUniforms stampUniforms_;
    AtlasStats atlasStats_;
    unsigned program_ = 0, vao_ = 0, buffer_ = 0;
    unsigned resolveProgram_ = 0, radianceTexture_ = 0, coverageTexture_ = 0,
             radianceFramebuffer_ = 0;
    int radianceWidth_ = 0, radianceHeight_ = 0;
    static constexpr bool roundCull_ = true;
    int roundCullUniform_ = -1;
    neon::vectors::BeamMesh mesh_;
};
} // namespace grpp
