#pragma once
#include "grpp/render.hpp"
#include "renderer_uniforms.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <deque>
#include <memory>
#include <string>

namespace grpp {
class BeamRenderer;
class GlesRenderer {
  public:
    GlesRenderer(SDL_Window *window, const Assets &assets);
    ~GlesRenderer();
    GlesRenderer(const GlesRenderer &) = delete;
    GlesRenderer &operator=(const GlesRenderer &) = delete;
    void present(const Canvas &canvas, const Assets &assets, bool reference, bool linear,
                 Aspect aspect, const std::filesystem::path &capture = {},
                 double decorativeFrameOffset = 0, int orientation = 0, bool expanded = false,
                 bool boundaries = true, const std::array<float, 2> *desktopCursor = nullptr,
                 const std::vector<uint16_t> *softwareBackdrop = nullptr);
    // Opaque Original framebuffer transport; transparent surfaces retain RGBA.
    void rgb565Uploads(bool enabled) { rgb565Uploads_ = enabled; }
    bool rgb565Available() const { return rgb565Available_; }
    // Direct and intermediate resolves share output; retained for rendering checks.
    void directResolve(bool enabled) { directResolve_ = enabled; }
    void frameQueue(unsigned limit);
    bool frameReady();
    bool windowChanged();
    void screenshot(const std::filesystem::path &path);
    const Viewport &viewport() const { return viewport_; }
    const std::string &driver() const { return driver_; }
    void reloadPages(const Assets &assets);

  private:
    void upload(unsigned texture, const Image &image, bool opaque = false, bool prescale = true);
    void uploadPixels(unsigned texture, const std::vector<uint16_t> &source, int width, int height,
                      bool opaque, bool prescale);
    void selectProgram(bool phosphor);
    SDL_Window *window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    unsigned program_ = 0, buffer_ = 0, vao_ = 0, referenceTexture_ = 0;
    unsigned plainProgram_ = 0, phosphorProgram_ = 0;
    RendererUniforms spriteUniforms_[2];
    const RendererUniforms *activeUniforms_ = nullptr;
    int uniform(Uniform id) const { return activeUniforms_->get(id); }
    bool directResolve_ = true;
    std::vector<Draw> referenceDrawScratch_, beamBatchScratch_;
    std::vector<float> batchScratch_;
    std::array<std::array<int, 4>, 2> programUniforms_{};
    unsigned rotationTexture_ = 0, rotationFramebuffer_ = 0;
    int rotationWidth_ = 0, rotationHeight_ = 0;
    std::array<unsigned, 10> textures_{};
    enum Artwork { StandardTitle, AofTitle, Splash, GotYou, LevelUp, SheepieSave, LevelArrow,
                   ArtworkCount, NoArtwork = -1 };
    std::array<unsigned, ArtworkCount> artworkTextures_{};
    std::array<std::array<int, 2>, ArtworkCount> artworkSizes_{};
    int foreground_ = -1, background_ = -1, mode_ = -1, uvBounds_ = -1;
    Raster565 raster_;
    std::vector<Rgba> referencePixels_;
    int referenceTextureWidth_ = 0, referenceTextureHeight_ = 0;
    bool rgb565Uploads_ = true, rgb565Available_ = true, referenceTexture565_ = false;
    Viewport viewport_{};
    std::string driver_;
    std::unique_ptr<BeamRenderer> beams_;
    std::deque<void *> gpuFrames_;
    unsigned frameQueueLimit_ = 0;
    bool syncAvailable_ = false;
};
} // namespace grpp
