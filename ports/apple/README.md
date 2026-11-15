# Experimental Apple builds

This separate CMake entry point reuses the game and renderer. The source-root
Windows/Android configuration is unchanged unless this Apple entry point is used.
Use a separate build directory. These configurations require validation on a Mac;
they are not part of the validated 1.0 release.

## macOS: GLES through ANGLE/Metal

Requirements: Xcode with command-line tools, CMake 3.22+, SDL 3.2.24 source, and
Metal-enabled ANGLE dynamic libraries for the selected architecture. Provide
`libEGL.dylib`, `libGLESv2.dylib` and their non-system runtime dependencies in one
directory. ANGLE is an external dependency; this project does not download or
build it. Retain its license and dependency notices when distributing libraries.

From the public source root:

```sh
cmake -S ports/apple -B build-macos -G Xcode \
  -DSDL3_SOURCE_DIR=/path/to/SDL3 \
  -DGRPP_ANGLE_DIR=/path/to/angle/out/Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-macos --config Release --target grpp_app
codesign --force --deep --sign - "build-macos/engine/Release/Gridrunner++ Enhanced.app"
open "build-macos/engine/Release/Gridrunner++ Enhanced.app"
```

For Intel use `x86_64`; a universal build requires matching universal ANGLE
libraries and `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Build ANGLE with its
Metal backend enabled and consult its current build instructions for compatible
SDK/toolchain requirements. Record the ANGLE revision used for testing.

The application forces SDL's EGL route, loads ANGLE from its own Frameworks
directory, selects Metal, and rejects a renderer that does not identify Metal.
The GLES shaders, GPU Arms, Cell Atlas, phosphor response and Original software
raster remain shared with the main application. No Metal shader rewrite is used.
CMake bundles assets/notices and fixes up dynamic-library dependencies after the
build. The command above applies a development ad-hoc signature after dependency fixup.
Use the appropriate distribution identity and entitlements for a public build. Notarization and release
signing are separate distribution steps; no signing credentials are stored here.

## iOS: native GLES startup configuration

SDL 3.2.24's UIKit backend uses Apple's native GLES context integration. Its
macOS EGL/ANGLE integration cannot be enabled on iOS by changing a library path.
This initial iOS configuration therefore uses native GLES 3, which Apple has
deprecated. It does **not** provide an iOS ANGLE/Metal backend. That needs an EGL
surface/context bridge or a separate renderer backend.

From the public source root, on a Mac:

```sh
cmake -S ports/apple -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DSDL3_SOURCE_DIR=/path/to/SDL3 \
  -DGRPP_APPLE_SIGNING_TEAM=YOUR_TEAM \
  -DGRPP_APPLE_BUNDLE_ID=your.organization.gridrunner
cmake --build build-ios --config Release --target grpp_app
```

For the simulator use a separate directory and `iphonesimulator`; choose an
architecture supported by the host. Open the generated Xcode project to select
and run on a device. SDL is statically linked on iOS. Assets and required notices
are bundle resources. The app targets iPhone/iPad and hides desktop display modes.
The supplied plist permits portrait and landscape; manual orientation locking,
safe-area layout, App Store icon/catalog preparation and device lifecycle behavior
still need Apple-specific validation/work. This is a development starting point,
not an App Store submission package.

## Validation

Check startup and asset loading outside the source directory; Standard and Arse
on Fire; Original and Enhanced graphics; portrait/landscape Draw to Edge; HUD and
scrolling boundaries; touch/controller input and Touch Off; audio interruption;
background/resume; Retina resizing; and frame pacing. Verify the macOS renderer
reports Metal and that the completed bundle runs without the ANGLE build directory.
Compare rendering against the existing reference captures, allowing for documented
driver rounding differences rather than assuming exact cross-GPU hashes.

Run the portable core suite separately on the Mac:

```sh
cmake -S . -B build-apple-core -DGRPP_BUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build build-apple-core --parallel
ctest --test-dir build-apple-core --output-on-failure
```

Upstream references: [ANGLE build setup](https://github.com/google/angle/blob/main/doc/DevSetup.md),
[SDL iOS](https://wiki.libsdl.org/SDL3/README-ios),
[SDL EGL selection](https://wiki.libsdl.org/SDL3/SDL_HINT_VIDEO_FORCE_EGL), and
[Apple GLES deprecation](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/OpenGLES_ProgrammingGuide/).
