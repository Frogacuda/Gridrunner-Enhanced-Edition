#include "gles_renderer.hpp"
#include "beam_renderer.hpp"
#include "grpp/background.hpp"
#include "grpp/expanded_presentation.hpp"
#include "grpp/original_overscan.hpp"
#include "grpp/phosphor.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glad/gl.h>
#include <stdexcept>
#include <vector>

namespace grpp {
namespace {
const char *vertexSource = R"(#version 300 es
precision highp float;
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 vertexColor;
uniform vec2 logicalSize;
out vec4 tint;
out vec2 uv;
void main() { tint=vertexColor; uv=texcoord; gl_Position=vec4(position.x*2.0/logicalSize.x-1.0,1.0-position.y*2.0/logicalSize.y,0.0,1.0); }
)";
const char *fragmentSource = R"(#version 300 es
precision highp float;
uniform sampler2D image;
uniform vec4 foreground;
uniform vec4 background;
uniform vec4 uvBounds;
uniform int mode;
uniform float opacity;
uniform float radianceScale;
uniform vec2 sampleStep;
in vec2 uv;
in vec4 tint;
layout(location=0) out vec4 outputColor;layout(location=1) out vec4 beamCoverage;
float maskAt(vec2 p){if(any(lessThan(p,uvBounds.xy))||any(greaterThan(p,uvBounds.zw)))return 0.0;return texture(image,p).a;}
vec3 encodeLight(vec3 rgb){
#ifdef PHOSPHOR_COLOR
 return vec3(1)-exp(-max(rgb,vec3(0))*radianceScale);
#else
 return rgb;
#endif
}
vec3 encodePremultiplied(vec3 rgb,float a){return a>0.0?encodeLight(rgb/a)*a:encodeLight(rgb);}
void main() {
    beamCoverage=vec4(0);
    if(mode==4){float b=clamp((127.0-203.0/256.0*(159.5-abs(uv.y-159.5)))/255.0,0.0,1.0);outputColor=vec4(encodeLight(vec3(0,0,b)),1);beamCoverage.a=1.0;return;}
    if(mode==3){float a=0.0;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)a+=maskAt(uv+vec2(float(x),float(y))*sampleStep);outputColor=vec4(encodeLight(foreground.rgb*a*(0.30/9.0)),0.0);return;}
    if(mode==2) { outputColor=vec4(encodePremultiplied(tint.rgb,tint.a),tint.a);beamCoverage.a=tint.a;return; }
    vec4 c=texture(image,clamp(uv,uvBounds.xy,uvBounds.zw));
    if(mode==0) { outputColor=vec4(encodeLight(c.rgb),1.0);beamCoverage.a=1.0;return; }
    vec3 rgb=foreground.a>0.5 ? foreground.rgb*c.a : c.rgb;
    float a=c.a;
    if(background.a>0.5) { rgb+=background.rgb*(1.0-c.a); a=1.0; }
    outputColor=vec4(encodePremultiplied(rgb,a)*opacity,a*opacity);
    beamCoverage.a=a*opacity;
})";
unsigned shader(GLenum stage, const char *source) {
    unsigned result = glCreateShader(stage);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    int ok = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(result, sizeof log, nullptr, log);
        glDeleteShader(result);
        throw std::runtime_error(std::string("GLES shader compile failed: ") + log);
    }
    return result;
}
void setColor(int uniform, uint16_t value, bool enabled = true) {
    const auto c = expand565(value, false);
    glUniform4f(uniform, c.r / 255.f, c.g / 255.f, c.b / 255.f, enabled ? 1.f : 0.f);
}
unsigned spriteProgram(const char *fragment) {
    const unsigned vs = shader(GL_VERTEX_SHADER, vertexSource),
                   fs = shader(GL_FRAGMENT_SHADER, fragment);
    const unsigned result = glCreateProgram();
    glAttachShader(result, vs);
    glAttachShader(result, fs);
    glLinkProgram(result);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int linked = 0;
    glGetProgramiv(result, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096]{};
        glGetProgramInfoLog(result, sizeof log, nullptr, log);
        glDeleteProgram(result);
        throw std::runtime_error(log);
    }
    return result;
}
} // namespace
void GlesRenderer::selectProgram(bool phosphor) {
    activeUniforms_ = &spriteUniforms_[phosphor ? 1 : 0];
    program_ = phosphor ? phosphorProgram_ : plainProgram_;
    const auto &uniforms = programUniforms_[phosphor ? 1 : 0];
    foreground_ = uniforms[0];
    background_ = uniforms[1];
    mode_ = uniforms[2];
    uvBounds_ = uniforms[3];
}
void GlesRenderer::frameQueue(unsigned limit) { frameQueueLimit_ = syncAvailable_ ? limit : 0; }
bool GlesRenderer::windowChanged() {
    // A Windows fullscreen switch can replace the drawable while keeping its
    // context handle. Detach first so SDL cannot short-circuit the rebind.
    if (!SDL_GL_MakeCurrent(window_, nullptr) || !SDL_GL_MakeCurrent(window_, context_))
        return false;
    for (auto frame : gpuFrames_)
        glDeleteSync(static_cast<GLsync>(frame));
    gpuFrames_.clear();
    return true;
}
bool GlesRenderer::frameReady() {
    while (!gpuFrames_.empty()) {
        const auto &frame = gpuFrames_.front();
        const GLenum status = glClientWaitSync(static_cast<GLsync>(frame), 0, 0);
        if (status == GL_TIMEOUT_EXPIRED)
            break;
        if (status == GL_WAIT_FAILED)
            throw std::runtime_error("GPU completion fence failed");
        glDeleteSync(static_cast<GLsync>(frame));
        gpuFrames_.pop_front();
    }
    return !frameQueueLimit_ || gpuFrames_.size() < frameQueueLimit_;
}
GlesRenderer::GlesRenderer(SDL_Window *window, const Assets &assets) : window_(window) {
    context_ = SDL_GL_CreateContext(window);
    if (!context_)
        throw std::runtime_error(std::string("Cannot create GLES 3 context: ") + SDL_GetError());
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)))
        throw std::runtime_error("Cannot load GLES entry points");
    // GLES 3.0 has sync objects, even when this desktop loader skips its GL 3.2 table.
    if (!glad_glFenceSync)
        glad_glFenceSync =
            reinterpret_cast<PFNGLFENCESYNCPROC>(SDL_GL_GetProcAddress("glFenceSync"));
    if (!glad_glClientWaitSync)
        glad_glClientWaitSync =
            reinterpret_cast<PFNGLCLIENTWAITSYNCPROC>(SDL_GL_GetProcAddress("glClientWaitSync"));
    if (!glad_glDeleteSync)
        glad_glDeleteSync =
            reinterpret_cast<PFNGLDELETESYNCPROC>(SDL_GL_GetProcAddress("glDeleteSync"));
    syncAvailable_ = glad_glFenceSync && glad_glClientWaitSync && glad_glDeleteSync;
    driver_ = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (driver_.find("OpenGL ES 3.") == std::string::npos)
        throw std::runtime_error("A GLES 3.x context is required; got " + driver_);
    SDL_GL_SetSwapInterval(1);
    plainProgram_ = spriteProgram(fragmentSource);
    std::string encoded(fragmentSource);
    encoded.insert(encoded.find('\n') + 1, "#define PHOSPHOR_COLOR\n");
    phosphorProgram_ = spriteProgram(encoded.c_str());
    for (int n = 0; n < 2; ++n) {
        const unsigned p = n ? phosphorProgram_ : plainProgram_;
        spriteUniforms_[n].load(p);
        programUniforms_[n] = {
            glGetUniformLocation(p, "foreground"), glGetUniformLocation(p, "background"),
            glGetUniformLocation(p, "mode"), glGetUniformLocation(p, "uvBounds")};
    }
    selectProgram(false);
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "image"), 0);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
                          reinterpret_cast<void *>(sizeof(float) * 2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
                          reinterpret_cast<void *>(sizeof(float) * 4));
    glGenTextures(int(textures_.size()), textures_.data());
    glGenTextures(1, &referenceTexture_);
    glGenTextures(1, &rotationTexture_);
    glGenFramebuffers(1, &rotationFramebuffer_);
    reloadPages(assets);
    // Presentation-only RGBA artwork bypasses the source RGB565 pages and prescale.
    constexpr std::array<const char *, ArtworkCount> artworkFiles{
        "enhanced-title.tga", "enhanced-title-aof.tga", "enhanced-splash.tga",
        "enhanced-got-you.tga", "enhanced-level-up.tga", "enhanced-sheepie-save.tga",
        "enhanced-level-arrow.tga"};
    for (size_t i = 0; i < artworkTextures_.size(); ++i) {
        const auto path = assets.root() / artworkFiles[i];
        if (!std::filesystem::exists(path))
            continue;
        std::ifstream in(path, std::ios::binary);
        std::array<unsigned char, 18> header{};
        in.read(reinterpret_cast<char *>(header.data()), header.size());
        const int w = header[12] | (header[13] << 8), h = header[14] | (header[15] << 8);
        if (!in || header[1] || header[2] != 2 || header[16] != 32 || header[17] != 0x28 || w < 1 ||
            h < 1 || w > 4096 || h > 4096)
            throw std::runtime_error("Invalid Enhanced artwork: " + path.string());
        in.ignore(header[0]);
        std::vector<Rgba> pixels(size_t(w) * h);
        in.read(reinterpret_cast<char *>(pixels.data()), std::streamsize(pixels.size() * 4));
        if (!in)
            throw std::runtime_error("Truncated Enhanced artwork: " + path.string());
        for (auto &p : pixels) {
            std::swap(p.r, p.b);
            p.r = uint8_t((unsigned(p.r) * p.a + 127) / 255);
            p.g = uint8_t((unsigned(p.g) * p.a + 127) / 255);
            p.b = uint8_t((unsigned(p.b) * p.a + 127) / 255);
        }
        glGenTextures(1, &artworkTextures_[i]);
        glBindTexture(GL_TEXTURE_2D, artworkTextures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        artworkSizes_[i] = {w, h};
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    beams_ = std::make_unique<BeamRenderer>();
}
GlesRenderer::~GlesRenderer() {
    if (context_) {
        for (const auto &frame : gpuFrames_)
            glDeleteSync(static_cast<GLsync>(frame));
        beams_.reset();
        if (plainProgram_)
            glDeleteProgram(plainProgram_);
        if (phosphorProgram_)
            glDeleteProgram(phosphorProgram_);
        if (buffer_)
            glDeleteBuffers(1, &buffer_);
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
        glDeleteTextures(int(textures_.size()), textures_.data());
        glDeleteTextures(int(artworkTextures_.size()), artworkTextures_.data());
        glDeleteTextures(1, &rotationTexture_);
        glDeleteFramebuffers(1, &rotationFramebuffer_);
        if (referenceTexture_)
            glDeleteTextures(1, &referenceTexture_);
        SDL_GL_DestroyContext(context_);
    }
}
void GlesRenderer::upload(unsigned texture, const Image &image, bool opaque, bool prescale) {
    uploadPixels(texture, image.pixels, image.width, image.height, opaque, prescale);
}
void GlesRenderer::uploadPixels(unsigned texture, const std::vector<uint16_t> &source, int width,
                                int height, bool opaque, bool prescale) {
    if (rgb565Uploads_ && rgb565Available_ && texture == referenceTexture_ && opaque) {
        const auto priorError = glGetError();
        if (priorError != GL_NO_ERROR)
            throw std::runtime_error("GLES error before RGB565 upload " +
                                     std::to_string(priorError));
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (referenceTexture565_ && referenceTextureWidth_ == width &&
            referenceTextureHeight_ == height)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                            source.data());
        else
            glTexImage2D(GL_TEXTURE_2D, 0, 0x8D62 /* GL_RGB565 */, width, height, 0, GL_RGB,
                         GL_UNSIGNED_SHORT_5_6_5, source.data());
        const auto uploadError = glGetError();
        if (uploadError == GL_NO_ERROR) {
            referenceTexture565_ = true;
            referenceTextureWidth_ = width;
            referenceTextureHeight_ = height;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return;
        }
        // Retry this frame with a fresh RGBA allocation and avoid repeated driver
        // failures. Never hide errors that happened before the format attempt.
        rgb565Available_ = false;
        referenceTextureWidth_ = referenceTextureHeight_ = 0;
        SDL_Log("RGB565 framebuffer upload rejected (%u); using RGBA", unsigned(uploadError));
    }
    const int scale = opaque || !prescale ? 1 : 3;
    std::vector<Rgba> temporary;
    auto &pixels = texture == referenceTexture_ ? referencePixels_ : temporary;
    pixels.resize(size_t(width * scale) * height * scale);
    for (int y = 0; y < height * scale; ++y)
        for (int x = 0; x < width * scale; ++x)
            pixels[size_t(y) * width * scale + x] =
                expand565(source[size_t(y / scale) * width + x / scale], !opaque);

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture == referenceTexture_ && !referenceTexture565_ &&
        referenceTextureWidth_ == width * scale && referenceTextureHeight_ == height * scale)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width * scale, height * scale, GL_RGBA,
                        GL_UNSIGNED_BYTE, pixels.data());
    else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width * scale, height * scale, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        if (texture == referenceTexture_) {
            referenceTextureWidth_ = width * scale;
            referenceTextureHeight_ = height * scale;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (texture == referenceTexture_)
        referenceTexture565_ = false;
}
void GlesRenderer::reloadPages(const Assets &assets) {
    for (size_t i = 0; i < textures_.size(); ++i)
        upload(textures_[i], assets.page(i));
}
void GlesRenderer::present(const Canvas &sourceCanvas, const Assets &assets, bool reference,
                           bool linear, Aspect aspect, const std::filesystem::path &capture,
                           double decorativeFrameOffset, int orientation, bool expanded,
                           bool boundaries, const std::array<float, 2> *desktopCursor,
                           const std::vector<uint16_t> *softwareBackdrop) {
    // Original mode is a complete 240x320 RGB565 raster enlarged by nearest sampling.
    if (reference)
        linear = false;
    Canvas sampled;
    const bool resample = !reference && (!expanded || sourceCanvas.starField().valid) &&
                          (sourceCanvas.decorativeGrid().valid || sourceCanvas.starField().valid);
    if (resample)
        sampled = resampleDecoration(sourceCanvas, decorativeFrameOffset, 12);
    const Canvas *selected = resample ? &sampled : &sourceCanvas;
    if (orientation < 0 || orientation > 3)
        throw std::invalid_argument("Invalid display orientation");
    int outputWidth = 0, outputHeight = 0;
    SDL_GetWindowSizeInPixels(window_, &outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;
    int width = orientationSwapsAxes(orientation) ? outputHeight : outputWidth,
        height = orientationSwapsAxes(orientation) ? outputWidth : outputHeight;
    Canvas expandedCanvas, referenceSource;
    int referenceWidth = sourceCanvas.width(), referenceHeight = sourceCanvas.height();
    if (expanded) {
        if (reference) {
            const auto view =
                makeViewport(width, height, aspect, sourceCanvas.width(), sourceCanvas.height());
            const auto body = originalOverscan(sourceCanvas, assets, width, height, aspect,
                                               (double(SDL_GetTicksNS()) / 1.e9), boundaries);
            raster_.render(body.actors, assets, &body.background);

            referenceWidth = body.actors.width();
            referenceHeight = body.actors.height();
            uploadPixels(referenceTexture_, raster_.pixels(), referenceWidth, referenceHeight, true,
                         false);
            const float sx = float(view.width) / sourceCanvas.width(),
                        sy = float(view.height) / sourceCanvas.height();
            referenceSource.dimensions(width, height);
            referenceSource.clear(sourceCanvas.clearColor());
            referenceSource.image(-2, {0, 0, referenceWidth, referenceHeight},
                                  view.x - body.padX * sx, view.y - body.padY * sy,
                                  referenceWidth * sx, referenceHeight * sy, {false, 0, 0});
            selected = &referenceSource;
        } else {
            expandedCanvas =
                expandedPresentation(*selected, width, height, aspect, decorativeFrameOffset,
                                     (double(SDL_GetTicksNS()) / 1.e9), boundaries, 12);
            selected = &expandedCanvas;
        }
    }
    const Canvas &canvas = *selected;

    const int logicalWidth = canvas.width(), logicalHeight = canvas.height();
    const bool phosphor =
        (!reference || expanded) &&
        std::any_of(canvas.draws().begin(), canvas.draws().end(), [](const Draw &d) {
            return (d.sheet == -3 || d.sheet == -4) && !d.referenceOnly;
        });
    const bool resolveDirect = orientation && phosphor && directResolve_;
    if (orientation && !resolveDirect) {
        glBindTexture(GL_TEXTURE_2D, rotationTexture_);
        if (rotationWidth_ != width || rotationHeight_ != height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         nullptr);
            rotationWidth_ = width;
            rotationHeight_ = height;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, rotationFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               rotationTexture_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Orientation framebuffer incomplete");
    } else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

    selectProgram(phosphor);
    if (phosphor)
        beams_->beginFrame(width, height);
    const float radianceScale = phosphor ? RadianceEncodingScale : 1.f;
    auto normalBlend = [&]() {
        if (phosphor)
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        else
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    };
    normalBlend();
    viewport_ = makeViewport(width, height, aspect, logicalWidth, logicalHeight);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, phosphor ? 0.f : 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (viewport_.width == 0 || viewport_.height == 0)
        return;
    glViewport(viewport_.x, height - viewport_.y - viewport_.height, viewport_.width,
               viewport_.height);
    const auto clear = expand565(canvas.clearColor(), false);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport_.x, height - viewport_.y - viewport_.height, viewport_.width,
              viewport_.height);
    const auto encodeClear = [&](unsigned char value) {
        const float c = value / 255.f;
        return phosphor ? 1 - std::exp(-c * RadianceEncodingScale) : c;
    };
    const GLfloat clearInk[]{encodeClear(clear.r), encodeClear(clear.g), encodeClear(clear.b),
                             phosphor ? 0.f : 1.f};
    if (phosphor) {
        const GLfloat zero[]{0, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, clearInk);
        glClearBufferfv(GL_COLOR, 1, zero);
    } else {
        glClearColor(clearInk[0], clearInk[1], clearInk[2], clearInk[3]);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glActiveTexture(GL_TEXTURE0);
    glUniform2f(uniform(Uniform::LogicalSize), float(logicalWidth), float(logicalHeight));
    glUniform1f(uniform(Uniform::RadianceScale), radianceScale);
    auto &referenceDraw = referenceDrawScratch_;
    referenceDraw.clear();
    if (reference && !expanded) {
        raster_.render(canvas, assets, softwareBackdrop);

        uploadPixels(referenceTexture_, raster_.pixels(), logicalWidth, logicalHeight, true, false);
        referenceDraw.push_back({-2,
                                 {0, 0, logicalWidth, logicalHeight},
                                 0,
                                 0,
                                 float(logicalWidth),
                                 float(logicalHeight),
                                 0,
                                 {false, 0, 0},
                                 0});
    }
    auto &batch = batchScratch_;
    batch.clear();
    auto &beamBatch = beamBatchScratch_;
    beamBatch.clear();
    auto flushBeams = [&]() {
        if (beamBatch.empty())
            return;
        glScissor(viewport_.x, height - viewport_.y - viewport_.height, viewport_.width,
                  viewport_.height);
        beams_->draw(beamBatch, viewport_.width, viewport_.height, logicalWidth, logicalHeight,
                     viewport_.x, height - viewport_.y - viewport_.height);
        beamBatch.clear();
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        normalBlend();
    };
    bool batchDirect = false;
    auto clip = [&](bool direct) {
        int cw = direct ? viewport_.width
                        : int(std::ceil(viewport_.width * float(logicalWidth - 1) / logicalWidth));
        int ch = direct
                     ? viewport_.height
                     : int(std::ceil(viewport_.height * float(logicalHeight - 1) / logicalHeight));
        glScissor(viewport_.x, height - viewport_.y - ch, cw, ch);
    };
    auto flush = [&]() {
        if (batch.empty())
            return;
        clip(batchDirect);
        glUniform1i(mode_, 2);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(batch.size() * sizeof(float)), batch.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(batch.size() / 8));
        batch.clear();
    };
    for (auto draw : reference && !expanded ? referenceDraw : canvas.draws()) {
        if (draw.referenceOnly)
            continue;
        if (!reference && draw.legacyTitleCredit && artworkTextures_[StandardTitle])
            continue;
        if (draw.sheet == -4) {
            flush();
            flushBeams();
            glScissor(viewport_.x, height - viewport_.y - viewport_.height, viewport_.width,
                      viewport_.height);
            beams_->drawGrid(canvas.decorativeGrid(), decorativeFrameOffset, viewport_.width,
                             viewport_.height, logicalWidth, logicalHeight, viewport_.x,
                             height - viewport_.y - viewport_.height);
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            normalBlend();
            continue;
        }
        if (draw.sheet == -3) {
            flush();
            beamBatch.push_back(draw);
            continue;
        }
        flushBeams();
        if (draw.sheet == -1 && !draw.smoothIntro) {
            if (!batch.empty() && batchDirect != draw.direct)
                flush();
            batchDirect = draw.direct;
            auto color = expand565(draw.color, false);
            constexpr int corners[6][2]{{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
            for (auto &corner : corners) {
                batch.insert(batch.end(),
                             {draw.x + corner[0] * draw.width, draw.y + corner[1] * draw.height, 0,
                              0, color.r / 255.f * draw.opacity, color.g / 255.f * draw.opacity,
                              color.b / 255.f * draw.opacity, draw.opacity});
            }
            continue;
        }
        flush();
        clip(draw.direct || draw.sheet == -2);
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        const Artwork artwork = draw.titleLogo          ? (assets.arse() ? AofTitle : StandardTitle)
                                : draw.levelSelectArrow ? LevelArrow
                                : draw.spriteId == 0    ? Splash
                                : draw.spriteId == 256  ? GotYou
                                : draw.spriteId == 248  ? LevelUp
                                : draw.spriteId == 249  ? SheepieSave
                                                       : NoArtwork;
        const bool enhancedArtwork =
            !reference && artwork != NoArtwork && artworkTextures_[size_t(artwork)];
        const bool mirrorArrow = enhancedArtwork && draw.levelSelectArrow && draw.spriteId == 3;
        if (draw.smoothIntro) {
            v0 = -draw.y / draw.height;
            v1 = (logicalHeight - draw.y) / draw.height;
            draw.x = draw.y = 0;
            draw.width = float(logicalWidth);
            draw.height = float(logicalHeight);
            clip(true);
        }
        if (draw.sheet != -1) {
            const int tw = enhancedArtwork    ? artworkSizes_[size_t(artwork)][0]
                           : draw.sheet == -2 ? referenceWidth
                                              : assets.page(size_t(draw.sheet)).width;
            const int th = enhancedArtwork    ? artworkSizes_[size_t(artwork)][1]
                           : draw.sheet == -2 ? referenceHeight
                                              : assets.page(size_t(draw.sheet)).height;
            if (enhancedArtwork && draw.titleLogo)
                draw.height = draw.width * th / tw;
            glBindTexture(GL_TEXTURE_2D, enhancedArtwork    ? artworkTextures_[size_t(artwork)]
                                         : draw.sheet == -2 ? referenceTexture_
                                                            : textures_.at(size_t(draw.sheet)));
            // The 6x Got You! texture contains crisp pixel faces beside vector lettering.
            const bool pixelFaces = enhancedArtwork && artwork == GotYou;
            const int filter = linear && !draw.squareFragment && !pixelFaces ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            u0 = float(draw.source.x) / tw;
            v0 = float(draw.source.y) / th;
            u1 = float(draw.source.x + draw.source.width) / tw;
            v1 = float(draw.source.y + draw.source.height) / th;
            if (enhancedArtwork) {
                const auto &s = assets.sprite(size_t(draw.spriteId));
                u0 = float(draw.source.x - s.x) / s.width;
                v0 = float(draw.source.y - s.y) / s.height;
                u1 = float(draw.source.x + draw.source.width - s.x) / s.width;
                v1 = float(draw.source.y + draw.source.height - s.y) / s.height;
                if (mirrorArrow) {
                    const float left = 1.f - u1;
                    u1 = 1.f - u0;
                    u0 = left;
                }
            }
            const int scale = enhancedArtwork || draw.sheet == -2 ? 1 : 3;
            glUniform4f(uvBounds_, u0 + .5f / (tw * scale), v0 + .5f / (th * scale),
                        u1 - .5f / (tw * scale), v1 - .5f / (th * scale));
            if (enhancedArtwork && filter == GL_NEAREST) {
                // Resolve exact texel-boundary ties consistently across separate tile quads.
                constexpr float tieBias = .001f;
                u0 += tieBias / tw;
                u1 += tieBias / tw;
                v0 += tieBias / th;
                v1 += tieBias / th;
            }
        }
        glUniform1i(mode_, draw.smoothIntro ? 4 : draw.sheet == -1 ? 2 : draw.paint.keyed ? 1 : 0);
        setColor(foreground_, draw.sheet == -1 ? draw.color : draw.paint.foreground,
                 draw.sheet == -1 || draw.paint.foreground != 0);
        setColor(background_, draw.paint.background, draw.paint.background != 0);
        const float cx = draw.x + draw.subpixelX + draw.width * .5f,
                    cy = draw.y + draw.subpixelY + draw.height * .5f;
        const float c = std::cos(draw.angle), s = std::sin(draw.angle);
        std::array<float, 48> vertices{};
        constexpr int corners[6][2]{{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (size_t i = 0; i < 6; ++i) {
            const float dx = (corners[i][0] - .5f) * draw.width,
                        dy = (corners[i][1] - .5f) * draw.height;
            vertices[i * 8] = cx + c * dx - s * dy;
            vertices[i * 8 + 1] = cy + s * dx + c * dy;
            vertices[i * 8 + 2] = bool(corners[i][0]) != mirrorArrow ? u1 : u0;
            vertices[i * 8 + 3] = corners[i][1] ? v1 : v0;
        }
        if (draw.silhouette && !reference) {
            auto glow = vertices;
            const float tw = float(assets.page(size_t(draw.sheet)).width),
                        th = float(assets.page(size_t(draw.sheet)).height);
            for (size_t i = 0; i < 6; ++i) {
                float sx = corners[i][0] ? 1.f : -1.f, sy = corners[i][1] ? 1.f : -1.f;
                glow[i * 8] += sx * 2;
                glow[i * 8 + 1] += sy * 2;
                glow[i * 8 + 2] += sx * 2 / tw;
                glow[i * 8 + 3] += sy * 2 / th;
            }
            glUniform2f(uniform(Uniform::SampleStep), 1.5f / tw, 1.5f / th);
            glUniform1i(mode_, 3);
            glBlendFuncSeparate(GL_ONE, phosphor ? GL_ONE_MINUS_SRC_COLOR : GL_ONE, GL_ZERO,
                                GL_ONE);
            glBufferData(GL_ARRAY_BUFFER, sizeof glow, glow.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            normalBlend();
            glUniform1i(mode_, 1);
        }
        glUniform1f(uniform(Uniform::Opacity),
                    draw.opacity * (draw.silhouette && !reference ? .55f : 1.f));
        glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    flush();
    flushBeams();
    glDisable(GL_SCISSOR_TEST);
    if (phosphor)
        beams_->endFrame(resolveDirect ? 0
                         : orientation ? rotationFramebuffer_
                                       : 0,
                         resolveDirect ? outputWidth : width, resolveDirect ? outputHeight : height,
                         resolveDirect ? orientation : 0);
    if (orientation && !resolveDirect) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, outputWidth, outputHeight);
        selectProgram(false);
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rotationTexture_);
        glUniform2f(uniform(Uniform::LogicalSize), 240.f, 320.f);
        glUniform1f(uniform(Uniform::RadianceScale), 1.f);
        glUniform1i(mode_, 0);
        glUniform4f(uvBounds_, 0, 0, 1, 1);
        glDisable(GL_BLEND);
        constexpr int corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        std::array<float, 48> vertices{};
        for (size_t i = 0; i < 6; ++i) {
            const float x = float(corners[i][0]), y = float(corners[i][1]);
            vertices[i * 8] = x * 240;
            vertices[i * 8 + 1] = y * 320;
            vertices[i * 8 + 2] = orientation == 3 ? 1 - x : orientation == 1 ? y : 1 - y;
            vertices[i * 8 + 3] = orientation == 3 ? y : orientation == 1 ? x : 1 - x;
        }
        glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_BLEND);
    }
    if (desktopCursor) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        if (ww > 0 && wh > 0) {
            const float x = (*desktopCursor)[0] * outputWidth / ww,
                        y = (*desktopCursor)[1] * outputHeight / wh;
            const float scale = std::clamp(SDL_GetWindowDisplayScale(window_), 1.f, 3.f);
            std::vector<float> vertices;
            vertices.reserve(18 * 8);
            auto triangle = [&](float ax, float ay, float bx, float by, float cx, float cy,
                                float ink) {
                for (const auto p : {std::array<float, 2>{ax, ay}, {bx, by}, {cx, cy}})
                    vertices.insert(vertices.end(),
                                    {x + p[0] * scale, y + p[1] * scale, 0, 0, ink, ink, ink, 1});
            };
            auto rectangle = [&](float l, float t, float r, float b, float ink) {
                triangle(l, t, r, t, r, b, ink);
                triangle(l, t, r, b, l, b, ink);
            };
            // A small open centre leaves the exact target unobscured.
            for (int pass = 0; pass < 2; ++pass) {
                const float pad = pass ? 0.f : 1.f, ink = float(pass);
                rectangle(-10 - pad, -1 - pad, -3 + pad, 1 + pad, ink);
                rectangle(3 - pad, -1 - pad, 10 + pad, 1 + pad, ink);
                rectangle(-1 - pad, -10 - pad, 1 + pad, -3 + pad, ink);
                rectangle(-1 - pad, 3 - pad, 1 + pad, 10 + pad, ink);
            }
            // Physical screen overlay: unaffected by rotation, letterboxing or phosphor.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, outputWidth, outputHeight);
            glDisable(GL_SCISSOR_TEST);
            selectProgram(false);
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            glUniform2f(uniform(Uniform::LogicalSize), float(outputWidth), float(outputHeight));
            glUniform1i(mode_, 2);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(),
                         GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, int(vertices.size() / 8));
        }
    }
    if (!capture.empty())
        screenshot(capture);

    if (frameQueueLimit_) {
        if (gpuFrames_.size() >= 128)
            throw std::runtime_error("GPU frame queue exceeded capacity");
        auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!fence)
            throw std::runtime_error("GPU completion fence unavailable");
        gpuFrames_.push_back(fence);
        glFlush();
    }
    SDL_GL_SwapWindow(window_);
}
void GlesRenderer::screenshot(const std::filesystem::path &path) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    std::vector<uint8_t> pixels(size_t(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x)
            out.write(reinterpret_cast<const char *>(pixels.data() + (size_t(y) * w + x) * 4), 3);
    if (!out)
        throw std::runtime_error("Cannot save screenshot");
}
} // namespace grpp
