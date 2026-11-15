#include "beam_renderer.hpp"
#include "grid_shaders.hpp"
#include "grpp/background.hpp"
#include "grpp/phosphor.hpp"
#include "neon_shaders.hpp"
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
namespace grpp {
namespace {
unsigned compileBeam(unsigned kind, const char *source) {
    unsigned shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char text[2048]{};
        glGetShaderInfoLog(shader, sizeof text, nullptr, text);
        glDeleteShader(shader);
        throw std::runtime_error(text);
    }
    return shader;
}
} // namespace
BeamRenderer::BeamRenderer() {
    unsigned v = compileBeam(GL_VERTEX_SHADER, beamVertexSource),
             f = compileBeam(GL_FRAGMENT_SHADER, beamSampleFragmentSource);
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    int linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked)
        throw std::runtime_error("Neon beam link failed");
    sampleUniforms_.load(program_);
    glUseProgram(program_);
    glUniform4f(glGetUniformLocation(program_, "phosphorResponse"), PointBoost, IntersectionBoost,
                MaximumWhitening, RadianceEncodingScale);
    roundCullUniform_ = glGetUniformLocation(program_, "roundCull");
    v = compileBeam(GL_VERTEX_SHADER, beamResolveVertexSource);
    f = compileBeam(GL_FRAGMENT_SHADER, beamResolveFragmentSource);
    resolveProgram_ = glCreateProgram();
    glAttachShader(resolveProgram_, v);
    glAttachShader(resolveProgram_, f);
    glLinkProgram(resolveProgram_);
    glDeleteShader(v);
    glDeleteShader(f);
    glGetProgramiv(resolveProgram_, GL_LINK_STATUS, &linked);
    if (!linked)
        throw std::runtime_error("Phosphor resolve link failed");
    resolveUniforms_.load(resolveProgram_);
    glUseProgram(resolveProgram_);
    glUniform1i(glGetUniformLocation(resolveProgram_, "radiance"), 0);
    glUniform1i(glGetUniformLocation(resolveProgram_, "exposure"), 1);
    glUniform4f(glGetUniformLocation(resolveProgram_, "phosphorResponse"), PointBoost,
                IntersectionBoost, MaximumWhitening, RadianceEncodingScale);
    glGenTextures(1, &radianceTexture_);
    glGenTextures(1, &coverageTexture_);
    glGenFramebuffers(1, &radianceFramebuffer_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    for (unsigned i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i == 3 ? 4 : 2, GL_FLOAT, GL_FALSE, sizeof(neon::vectors::Vertex),
                              reinterpret_cast<void *>(size_t(i) * 8));
    }
}
BeamRenderer::~BeamRenderer() {
    glDeleteBuffers(1, &stampBuffer_);
    glDeleteVertexArrays(1, &stampVao_);
    glDeleteProgram(stampProgram_);
    glDeleteFramebuffers(1, &atlasFramebuffer_);
    glDeleteTextures(1, &atlasLight_);
    glDeleteTextures(1, &atlasCoverage_);
    glDeleteBuffers(1, &buffer_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
    for (auto p : gridPrograms_)
        glDeleteProgram(p);
    glDeleteProgram(resolveProgram_);
    glDeleteTextures(1, &radianceTexture_);
    glDeleteTextures(1, &coverageTexture_);
    glDeleteFramebuffers(1, &radianceFramebuffer_);
}
void BeamRenderer::beginFrame(int width, int height) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, radianceTexture_);
    if (radianceWidth_ != width || radianceHeight_ != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_INT_2_10_10_10_REV, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, radianceFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               radianceTexture_, 0);
        glBindTexture(GL_TEXTURE_2D, coverageTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                               coverageTexture_, 0);
        const GLenum attachments[]{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, attachments);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("RGB10_A2 beam framebuffer incomplete");
        radianceWidth_ = width;
        radianceHeight_ = height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, radianceFramebuffer_);
    glBindTexture(GL_TEXTURE_2D, 0);
}
void BeamRenderer::endFrame(unsigned target, int width, int height, int orientation) {
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glUseProgram(resolveProgram_);
    glUniform1i(resolveUniforms_.get(Uniform::Orientation), orientation);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, coverageTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, radianceTexture_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
void BeamRenderer::drawGrid(const DecorativeGrid &grid, double offset, int width, int height,
                            int logicalWidth, int logicalHeight, int x, int y) {
    atlasStats_ = {};
    const auto planes = projectGrid(grid, offset);
    const float sx = float(grid.displayScaleX * width / logicalWidth),
                sy = float(grid.displayScaleY * height / logicalHeight);
    const float w = std::clamp(float(2 * height / 720. * grid.beamWidthScale), .35f, 16.f);
    drawAtlas(planes, sx, sy, w, width, height, x, y);
}
void BeamRenderer::drawGridPlanes(const std::vector<GridPlane> &planes, float sx, float sy,
                                  float beamWidth, int width, int height, int x, int y,
                                  float bakeScale) {
    const int programIndex = bakeScale != 1 ? 1 : 0;
    unsigned &gridProgram = gridPrograms_[programIndex];
    if (!gridProgram) {
        const auto vertex = gridVertexSource(bakeScale != 1);
        std::string fragment;
        {
            fragment = beamFragmentSource;
            const std::string uniform = "uniform vec4 beamStyle;";
            const auto pos = fragment.find(uniform);
            if (pos == std::string::npos)
                throw std::logic_error("Beam style declaration missing");
            fragment.replace(pos, uniform.size(), "flat in vec4 beamStyle;");
        }
        const unsigned v = compileBeam(GL_VERTEX_SHADER, vertex.c_str()),
                       f = compileBeam(GL_FRAGMENT_SHADER, fragment.c_str());
        gridProgram = glCreateProgram();
        glAttachShader(gridProgram, v);
        glAttachShader(gridProgram, f);
        glLinkProgram(gridProgram);
        glDeleteShader(v);
        glDeleteShader(f);
        int ok = 0;
        glGetProgramiv(gridProgram, GL_LINK_STATUS, &ok);
        if (!ok)
            throw std::runtime_error("Grid instancing link failed");
        gridUniforms_[programIndex].load(gridProgram);
    }
    std::array<std::array<float, 4>, 32> geometry{}, lattice{}, shading{};
    std::array<std::array<int, 4>, 32> info{};
    int count = 0, instances = 0;
    for (const auto &p : planes) {
        if (!packOriginalColor(p.color) || p.energy <= 0 || p.firstX > p.lastX ||
            p.firstY > p.lastY)
            continue;
        if (count >= 32)
            throw std::runtime_error("Grid plane capacity exceeded");
        const int nx = p.lastX - p.firstX + 1, ny = p.lastY - p.firstY + 1,
                  rows = p.mode == 3 ? 2 : 1;
        const int primitives = p.mode == 0 ? 2
                               : p.mode == 1
                                   ? 4 * p.shades
                                   : 2 * rows * (std::abs(p.rx - p.ry) < 1.e-8 ? 2 : 1 + p.shades);
        const int total = nx * ny * primitives;
        geometry[size_t(count)] = {float(p.x), float(p.y), float(p.rx), float(p.ry)};
        lattice[size_t(count)] = {float(p.spacing), float(p.firstX), float(p.firstY), p.energy};
        shading[size_t(count)] = {float(((p.color >> 8) & 0xff0000u) / unsigned(p.shades)),
                                  float((p.color & 0xff0000u) / unsigned(p.shades)),
                                  float(((p.color & 0xff00u) << 8) / unsigned(p.shades)),
                                  float(p.shades)};
        info[size_t(count)] = {instances, ny, total, p.mode};
        instances += total;
        ++count;
    }
    if (!instances)
        return;
    const auto &uniforms = gridUniforms_[programIndex];
    glViewport(x, y, width, height);
    glUseProgram(gridProgram);
    glBindVertexArray(vao_);
    glScissor(x, y, width, height);
    if (bakeScale != 1)
        glUniform1f(uniforms.get(Uniform::BakeScale), bakeScale);
    glUniform4fv(uniforms.get(Uniform::Geometry), count, geometry[0].data());
    glUniform4fv(uniforms.get(Uniform::Lattice), count, lattice[0].data());
    glUniform4fv(uniforms.get(Uniform::Shading), count, shading[0].data());
    glUniform4iv(uniforms.get(Uniform::Info), count, info[0].data());
    glUniform1i(uniforms.get(Uniform::PlaneCount), count);
    glUniform2f(uniforms.get(Uniform::ViewportSize), float(width), float(height));
    glUniform2f(uniforms.get(Uniform::PixelScale), sx, sy);
    glUniform1f(uniforms.get(Uniform::BeamWidth), beamWidth);
    glUniform1i(uniforms.get(Uniform::RoundCull), roundCull_ ? 1 : 0);
    glUniform1f(uniforms.get(Uniform::Intensity), BeamIntensity);
    glUniform4f(uniforms.get(Uniform::PhosphorResponse), PointBoost, IntersectionBoost,
                MaximumWhitening, RadianceEncodingScale);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
}
void BeamRenderer::draw(const std::vector<Draw> &commands, int width, int height, int logicalWidth,
                        int logicalHeight, int x, int y) {
    using namespace neon::vectors;
    if (commands.empty())
        return;
    float left = float(width), right = 0, top = float(height), bottom = 0;
    for (const auto &d : commands) {
        const float ax = d.x * width / logicalWidth, bx = d.endX * width / logicalWidth,
                    ay = d.y * height / logicalHeight, by = d.endY * height / logicalHeight;
        if (!std::isfinite(ax) || !std::isfinite(bx) || !std::isfinite(ay) || !std::isfinite(by))
            throw std::runtime_error("Non-finite beam extent");
        left = std::min(left, std::min(ax, bx) - 64);
        right = std::max(right, std::max(ax, bx) + 64);
        top = std::min(top, std::min(ay, by) - 64);
        bottom = std::max(bottom, std::max(ay, by) + 64);
    }
    const int l = int(std::floor(std::clamp(left, 0.f, float(width)))),
              r = int(std::ceil(std::clamp(right, 0.f, float(width))));
    const int t = int(std::floor(std::clamp(top, 0.f, float(height)))),
              b = int(std::ceil(std::clamp(bottom, 0.f, float(height))));
    if (l >= r || t >= b)
        return;
    glScissor(x + l, y + height - b, r - l, b - t);
    BeamStyle style;
    style.intensity = BeamIntensity;
    style.width = 2.f;
    style.colour = 1;
    style.whiteHot = 0;
    style.halo = .38f;
    style.spread = 4;
    style.persistenceMs = 0;
    const auto &uniforms = sampleUniforms_;
    glUseProgram(program_);
    glUniform2f(uniforms.get(Uniform::ViewportSize), float(width), float(height));
    glUniform1i(roundCullUniform_, roundCull_ ? 1 : 0);
    glUniform4f(uniforms.get(Uniform::BeamStyle), style.intensity, style.halo, style.spread,
                style.whiteHot);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE);
    auto &scratch = scratch_;
    auto &strokes = scratch.strokes;
    strokes.clear();
    strokes.reserve(BeamMesh::MaxStrokes);
    auto &prepared = scratch.prepared;
    prepared.clear();
    prepared.reserve(commands.size());
    auto &faintPoints = scratch.faintPoints;
    faintPoints.clear();
    for (const auto &d : commands) {
        const bool dot = d.x == d.endX && d.y == d.endY,
                   gridDot = d.decorative && d.patternedBeam && dot;
        const int profile = gridDot ? 2 : d.patternedBeam ? 1 : 0;
        auto c = expand565(d.rampSteps ? d.rampEndColor : d.color, false);
        Stroke stroke{{d.x * 640.f / logicalWidth, d.y * 480.f / logicalHeight},
                      {d.endX * 640.f / logicalWidth, d.endY * 480.f / logicalHeight},
                      {c.r / 255.f, c.g / 255.f, c.b / 255.f},
                      d.energy * (gridDot ? 2.5f : 1.f),
                      d.beamWidth * (d.patternedBeam && dot ? 2.f / 1.4f : 1.f)};
        const float light =
            std::max({stroke.colour.r, stroke.colour.g, stroke.colour.b}) * stroke.energy;
        if (light <= 0)
            continue;
        const bool faint = gridDot && light > 0 && light * BeamIntensity < .01f;
        if (std::max(d.x, d.endX) * width / logicalWidth < -64 ||
            std::min(d.x, d.endX) * width / logicalWidth > width + 64 ||
            std::max(d.y, d.endY) * height / logicalHeight < -64 ||
            std::min(d.y, d.endY) * height / logicalHeight > height + 64)
            continue;
        Key key{};
        if (faint)
            key = {int(std::floor(d.x * width / logicalWidth)),
                   int(std::floor(d.y * height / logicalHeight)), profile, stroke.width};
        if (faint) {
            auto found = faintPoints.find(key);
            if (found != faintPoints.end()) {
                auto &existing = prepared[found->second.first].stroke;
                if (existing.energy + stroke.energy <= 8.f) {
                    const float weight = found->second.second, total = weight + light,
                                energy = existing.energy + stroke.energy;
                    existing.a = {(existing.a.x * weight + stroke.a.x * light) / total,
                                  (existing.a.y * weight + stroke.a.y * light) / total};
                    existing.b = existing.a;
                    existing.colour = {
                        (existing.colour.r * existing.energy + stroke.colour.r * stroke.energy) /
                            energy,
                        (existing.colour.g * existing.energy + stroke.colour.g * stroke.energy) /
                            energy,
                        (existing.colour.b * existing.energy + stroke.colour.b * stroke.energy) /
                            energy};
                    existing.energy = energy;
                    found->second.second = total;
                    continue;
                }
            }
        }
        prepared.push_back({stroke, profile});
        if (faint)
            faintPoints[key] = {prepared.size() - 1, light};
    }
    int profile = 0;
    auto flush = [&]() {
        if (strokes.empty())
            return;
        glUniform4f(uniforms.get(Uniform::BeamStyle), style.intensity, style.halo, style.spread,
                    style.whiteHot);
        const auto &vertices = mesh_.build(strokes, 0, width, height, style, false);
        if (mesh_.stats().dropped)
            throw std::runtime_error("Unexpected beam batch overflow");
        glDisableVertexAttribArray(4);
        glVertexAttrib4f(4, 0, 0, 0, 0);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(),
                     GL_STREAM_DRAW);
        for (unsigned i = 0; i < 4; ++i)
            glVertexAttribPointer(i, i == 3 ? 4 : 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void *>(size_t(i) * 8));
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));
        strokes.clear();
    };
    bool patterned = false;
    for (const auto &item : prepared) {
        const int nextProfile = item.profile;
        const bool gridDot = nextProfile == 2;
        if (nextProfile != profile) {
            flush();
            profile = nextProfile;
            patterned = nextProfile == 1 || nextProfile == 2;
            style.width = patterned ? 1.4f : 2.f;
            style.halo = gridDot ? .42f : patterned ? .14f : .38f;
            style.spread = gridDot ? 3.f : patterned ? 2.f : 4.f;
        }
        if (strokes.size() == BeamMesh::MaxStrokes)
            flush();
        strokes.push_back(item.stroke);
    }
    flush();
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
}
#include "grid_atlas.inl"
} // namespace grpp
