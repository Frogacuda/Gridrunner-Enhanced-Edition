# Gridrunner++ Enhanced 1.0

A native Windows and Android edition of Llamasoft's Gridrunner++, including
Arse on Fire and an independent Endurance mode. Enhanced graphics add glowing
vector beams and a grid rendered at the display's resolution. Original graphics
retain the RGB565 software presentation. Both share the same gameplay core.

## Install and play

On Windows, extract the whole download and run `grpp_enhanced.exe`. Keep `assets/`
and `SDL3.dll` beside it. On Android, install the APK; Android 7 or later and
OpenGL ES 3 graphics are required. The APK includes ARM64 and ARMv7 libraries.
No network or shared-storage permission is needed.

Select Play, adjust the starting level with Left/Right, then press Down to select
Start. Starting levels follow the active profile's progress. Standard and Arse on
Fire have separate campaigns and records; Endurance can be selected at level one.

## Controls

The ship fires automatically. Secondary-fire commands apply to Arse on Fire.

- Mouse: bounded relative movement during play. Left button supplies secondary
  fire; Right button uses the bomb. The pointer is released outside live play.
- Keyboard: arrow keys move/navigate; Enter confirms; Space uses the bomb; Shift
  supplies secondary fire; Tab selects an unlocked alternate craft.
- Controller: left stick/D-pad move or navigate; A confirms/uses the bomb;
  B supplies secondary fire in Arse on Fire; X selects an unlocked alternate craft.
- Esc or Android Back opens pause during a run; Start or P also
  pauses. Back in pause resumes. Choose Exit in pause to end the run. Back in
  other menus navigates back or exits. F11 switches desktop window/fullscreen.

Touch Control 1 maps the touch position directly to a target. Control 2 uses
relative dragging, discarding excess movement at a playfield edge. Control 3 is a
fixed-origin virtual stick: one quarter of the shorter screen dimension is full
travel. Sensitivity ranges from 0.5x to 3.0x in 0.1 steps and affects relative
touch, virtual stick, mouse and analog controller input. Absolute touch ignores it.
Touch control appears only with direct touch hardware. A fourth option, Off, disables touch when a controller, keyboard or mouse is connected; touch falls back to Control 1 if those inputs are disconnected. The on-screen Pause button is hidden when touch is unavailable or disabled. B is unused in standard gameplay and in menus with an Exit choice.

In Standard, a second touch uses the bomb. In Arse on Fire, hold a second finger
for secondary fire; use a third finger or double-tap with the second for the bomb.

## Display and defaults

Fresh settings use Enhanced graphics, 200 Hz motion updates, Draw to Edge,
Control 1 and 1.0x sensitivity. Presentation follows the display refresh rate up
to the selected cap; changing that cap does not change gameplay speed.

Letterbox preserves the original viewport. Draw to Edge fills extra horizontal
or vertical space without enlarging the gameplay world or changing its panning.
Score, lives and multiplier follow the display edges. Scrolling green/cyan dashed
lines indicate world bounds; actors can still draw outside them.

Android defaults to Orientation Auto, following the system rotation setting.
Upright, Right and Left lock the intended view. If the device forces a different
orientation, rendering and pointer input compensate. Desktop orientation rotates
the view within the window. Display mode and View are separate settings.

Profiles, records and options are stored in the per-user application directory,
not beside the executable. Android app updates retain data when signed with the
same certificate. Removing an app can remove its local data. `--save-dir PATH`
selects another desktop save location; `--help` lists command-line options.

## Credits and rights

Original games, artwork, audio, text and related material: Llamasoft and their
respective authors. Modern port and presentation contributions: Travis Fahs and
contributors. Original contributions use 0BSD; original game/asset rights and
third-party licenses remain separate. See LICENSE.txt, THIRD-PARTY-NOTICES.txt
and LICENSES/. Inclusion does not grant permission to reuse original assets.

## Build from source

Use CMake 3.22+, a C++17 compiler and SDL 3.2.24. SDL is an external dependency;
provide its source through `SDL3_SOURCE_DIR` or an installed package through
`SDL3_DIR`. Windows builds use the MSVC x64 toolchain. Windows and Android are
the tested release platforms; other desktop ports have not been validated.

```sh
cmake -S . -B build -DSDL3_SOURCE_DIR=/path/to/SDL3 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Windows, `build.ps1 -SDL3SourceDir C:/path/to/SDL3` builds and runs the checks.
The application target is `grpp_app`; it produces `grpp_enhanced.exe` on Windows
and `libmain.so` on Android. Tests build separately and do not add diagnostic
commands or synthetic input scripts to the release executable.

For core-only checks without SDL or a display, configure `GRPP_BUILD_APP=OFF`.
For GPU rendering checks, configure `GRPP_RUN_RENDER_TESTS=ON` on a GLES 3 system.
`cmake --build build --config Release --target check` builds and runs registered
checks. See tests/README.md for fixture provenance and scope.

### Android

Use Java 21, SDK platform/build tools 35, NDK 27.2.12479018 and SDK CMake 3.22.1.
The included wrapper selects Gradle 8.12 and Android Gradle Plugin 8.7.3. SDL Java
and native sources must come from the same checkout. Android's application ID is
`org.openminotaur.gridrunnerplusplus`.

```powershell
./tools/build_android.ps1 -AndroidSdk C:/Android/sdk -SdlSource C:/src/SDL3 `
    -JavaHome C:/Java/jdk-21 -NativeBuildDirectory C:/build/grpp-android
```

A short native build path avoids Windows NDK object-path limits. The script
builds both ARM ABIs and verifies APK signing/alignment. For full static package
checks, run tools/verify_android.ps1 with `-Apk`, `-AndroidSdk` and `-JavaHome`.

Release signing is optional for local builds. `-PandroidSigningFile=PATH` or
`OPENMINOTAUR_SIGNING_FILE` selects a private JSON file with `storeFile`,
`storePassword` and `keyAlias`; `keyPassword` and `storeType` are optional. Keep
that file and the keystore outside the public tree. Without private settings,
the helper creates a local development key in the ignored android/local folder.
A locally signed APK cannot update a release-signed install.

## Source layout

- `include/grpp/`, `src/`: typed game mechanisms, frontend, presentation and host.
- `generated/`: checked-in typed tables and text extracted from identified games.
  Building needs neither an original executable nor reverse-engineering tools.
- `assets/`: runtime game data; its rights are reserved separately from code.
- `third_party/`: GLAD loader and the independently authored Neon vector support.
- `android/`: platform bridge, Gradle wrapper and application resources.
- `tests/`: core, original-output fixture, renderer and input/menu checks.
- `docs/ARCHITECTURE.md`: ownership, timing, numerical rules and intentional changes.

Project C++ uses .clang-format. Preserve fixed-point arithmetic, source scheduling,
random draw ownership and draw-owned effects when changing mechanisms. Repainting
or changing display settings must not advance simulation. A passing saved-output
check is regression evidence; it is not proof of full original-game equivalence.
