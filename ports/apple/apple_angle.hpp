#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>

inline void prepareAppleAngle() {
    const auto frameworks = std::filesystem::path(SDL_GetBasePath()) / "../Frameworks";
    const auto egl = (frameworks / "libEGL.dylib").lexically_normal().string();
    const auto gles = (frameworks / "libGLESv2.dylib").lexically_normal().string();
    if (!std::filesystem::is_regular_file(egl) || !std::filesystem::is_regular_file(gles))
        throw std::runtime_error("ANGLE libraries are missing from the application bundle");
    SDL_SetHint(SDL_HINT_EGL_LIBRARY, egl.c_str());
    SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, gles.c_str());
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "ANGLE_DEFAULT_PLATFORM", "metal", true);
}
inline SDL_EGLAttrib *SDLCALL appleAngleDisplayAttributes(void *) {
    auto *attributes = static_cast<SDL_EGLAttrib *>(SDL_malloc(3 * sizeof(SDL_EGLAttrib)));
    if (attributes) {
        attributes[0] = 0x3209; // EGL_PLATFORM_ANGLE_TYPE_ANGLE
        attributes[1] = 0x3489; // EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
        attributes[2] = 0x3038; // EGL_NONE; SDL frees the returned array.
    }
    return attributes;
}
inline void configureAppleAngle() {
    if (!SDL_GL_SetAttribute(SDL_GL_EGL_PLATFORM, 0x3202)) // EGL_PLATFORM_ANGLE_ANGLE
        throw std::runtime_error(SDL_GetError());
    SDL_EGL_SetAttributeCallbacks(appleAngleDisplayAttributes, nullptr, nullptr, nullptr);
}
