# Architecture

## Owners and timing

Frontend owns the real menu/profile/session flow and its Game. The application
host polls SDL, translates input, chooses display settings, schedules presentation
and submits sound commands. A direct-play entry uses a separate Game for headless
captures and integration checks. It does not replace the normal frontend flow.

Game owns entities, player state, the cell grid, formation/spawn state, records,
source random streams and effects. NativeMotion integrates movement and contacts
at 200 Hz while decisions retain the 30 ms source interval. FixedCadence advances
from deadlines, so display refresh does not determine game speed. PresentationClock
independently tracks the display rate and configured cap. GPU fences bound queued
work without blocking event polling or simulation; unsupported sync disables the
queue limit safely.

Positions and velocities retain source fixed-point units and wrapping arithmetic.
Entity words can be reused by different behaviors; documented aliases do not imply
one uniform meaning across types. Source-owned draw effects and random consumption
remain in source-frame ordering. Deferred presentation rebuilds only what the
renderer needs; it does not advance simulation or consume another gameplay draw.

## Game mechanisms

| Files | Responsibility |
| --- | --- |
| mechanisms / native_motion | Entity storage, source arithmetic, player and native movement |
| formations / chains | Spawn admission, formation scripts, linked enemies and turns |
| combat / world | Contacts, rewards, pickups, hazards and grid-zap progression |
| stages / generated tables | Campaign data, captions, programs and original constants |
| visuals / background | Sprite commands, grid recipes, decorative projection and camera |
| frontend / persistence | Menus, profiles, records, campaign continuation and atomic saves |
| expanded_presentation / original_overscan | Expanded drawing with unchanged world/panning |
| gles_renderer / beam_renderer / grid_atlas | GPU submission, phosphor resolve and grid cells |
| main / audio | Platform lifecycle, input, presentation cadence and audio transport |

Standard uses a 240x320 logical viewport into a 480x640 world. Arse on Fire uses
a 400x400 viewport into an 800x800 world and retains its separate campaign rules.
Endurance is independent of the selected edition. Per-level background recipes
are deterministic presentation state and do not replace gameplay random draws.

## Rendering

Original renders an integer RGB565 scene, including overscan grid/stars, actors
and HUD, then uses nearest output. RGB565 texture upload has a checked RGBA fallback.
Enhanced uses the same gameplay state with semantic vector metadata, sprites
prescaled by exact nearest replication, and native-resolution beams.

Grid Arms expands projected planes into repeated beam arms. Cell Atlas selects
cells, bakes their accepted shading into bounded radiance/coverage textures, and
stamps disjoint occupied regions; other planes use Arms. The atlas is bounded to
5 MiB and submissions to 8,192 stamps per draw. Uniform locations, meshes and
scratch vectors are reused. Distant planes are retained.

The beam shader distinguishes ordinary segments, patterned points and depth
ramps. Faint samples sharing a physical pixel aggregate before batching, retaining
fractional centroids and radiance. Near grid intensity reaches 90%; points and
intersections receive a 20% response boost. Logarithmic RGB10_A2/R8 accumulation
and a smooth whitening response approach a 50% over-intensity limit without a
hard visible clamp. Temporal persistence is not used.

Draw to Edge extends presentation, not the gameplay world, and retains source
projection/panning. HUD anchors follow the display. Boundary marks use actual
world pan and pulse between green at 40% energy and cyan at 70%. They neither
crop actors nor establish new collision walls. The Original path draws those
marks in its software scene; Enhanced uses ordinary vector beam profiles.

Rotation has one effective transform for rendering, absolute pointer inverse,
relative deltas and touch-stick origin. On Android, request the selected platform
orientation, then compare with measured display orientation. An honored request
adds no correction; an ignored request is compensated. Auto follows the surface.
The internal render half-turn id is distinct from the saved Auto setting. Cancel
held origins when the transform changes. No accelerometer is required.

## Input and lifecycle

InputLatch retains short action edges until a simulation update consumes them.
Relative touch and mouse clamp after each displacement and discard overshoot.
Absolute touch remains coordinate based. The virtual stick uses a fixed physical
origin, circular travel bound and the analog controller response curve. Sensitivity
scales target movement without changing the ship's intentional follow smoothing.

Mouse capture applies only to focused live desktop play. Pause, menus, interludes,
game over and focus loss release it. The host draws a physical-window crosshair
in menus and restores the OS cursor outside the window. Back pauses a live run;
only the explicit pause-menu Exit action aborts it. Touch capability controls the
presence of touch options and the on-screen Pause button.

Backgrounding suspends updates and resets cadence on return. Save writes use
validated temporary data, atomic replacement and a last-good backup. Profiles,
records and display options have separate ownership and persist in per-user storage.

## Source correspondence and intentional adaptations

The Standard data/mechanisms are based on the 2002-08-09 PC executable, SHA256
`342c06e95886fb610071d9b511a52fa0eb64e2307ac6767d146a63e286a0782a`.
Arse on Fire campaign tables originate from SHA256
`dcf7e257a7ed8014a91f01dd1fcf35fbff7c357452838223cc16639e82dfcad6`.
Generated headers are typed data, not executable images. Addresses beside source
rules identify corresponding routines; the running game maps no original code.

Modern inputs, native motion updates, expanded drawing, vector/phosphor styling,
capability-gated menus and atomic saves are deliberate host adaptations. Per-level
grid selection/reset is consistent across selection, start, continue and progression.
Companion and hazard boundary decisions use completed source moves at native update
rates, so fractional updates do not trap companions outside the world or duplicate
contact budgets. Display changes must not change gameplay bounds, source decisions,
random order or draw-owned effects. Tests distinguish captured-original fixtures,
source-rule assertions and saved-port/image regression checks.
