# Checks

Configure with BUILD_TESTING=ON. `cmake --build build --config Release --target check`
builds and runs registered checks; `ctest --test-dir build -C Release --output-on-failure`
runs already-built checks. GRPP_BUILD_APP=OFF runs core checks without SDL/display.
GRPP_RUN_RENDER_TESTS=ON also registers GPU checks requiring OpenGL ES 3.

Core checks cover fixed-point rules, spawning/chains, contacts/rewards, world
hazards, profile/menu flow, campaign continuation, native timing and presentation
state purity. App checks compile main.cpp with GRPP_APP_CHECKS and include the
small scripts under app_checks/. The release target excludes those scripts and
their command-line switches. They exercise real menu/input flow with synthetic SDL
events, including short action bursts, pause/Back, touch and pointer boundaries.

Renderer checks compare software transports, Original hot restores, bounded
phosphor behavior, cursor overlays, all effective rotations and ordinary versus
deferred presentation. renderer_capture_checks emits frozen-scene hashes for both
editions, views, shapes and orientations; its optional phone case is a saved-renderer
reference tied to the test capture environment. Such captures are regression checks,
not original-game framebuffer evidence.

Fixture provenance is summarized in fixtures/PROVENANCE.md. Original execution
fixtures are numeric observations captured externally from an identified executable.
Their original extraction harness is maintained separately; no original executable,
machine-code loader or analysis-tool dependency is included here. Checked-in
completion markers and record counts detect truncated fixture files.

Input replays are ordinary native-run recordings and test the port's connected
session flow. They do not provide an independent original-game expected-state
oracle. Source-rule tests assert identified mechanisms. A passing suite does not
claim identical full-game outcomes after changed native contact timing.

The supplemental campaign_audit, frontend_stage_audit and native_campaign_audit
targets drive ordinary inputs and write reports; use their usage text for paths.
Their completion/timeout results are exploratory and are not an automated claim
that every level is beatable. Keep generated reports outside the public source.
