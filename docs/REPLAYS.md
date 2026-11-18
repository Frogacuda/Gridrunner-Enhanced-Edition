# Replays

The game records the newest run automatically. Recording begins at the
first gameplay tick, with a typed starting-state snapshot. Recording stops when
the run reaches Game Over, but the original Game Over screen remains visible.
Dismissing that screen, or choosing the pause menu's Exit action during a run,
opens Save Replay? with Yes/No. Yes opens ten slots for the
active profile; occupied slots show mode, highest stage reached and final score.
Occupied slots require overwrite confirmation. Cancel returns to
the prompt. No discards the in-memory recording. Window close also offers the
prompt, then closes after saving or declining. Forced process termination cannot
show a prompt and loses an unsaved recording.

Replays use the main application's private save directory. Standard and Arse on
Fire share ten slots within each profile. The four named profile indices and
Guest each have their own replay directory. Profile renaming does not change its replay namespace. Files are stored below the private
save directory at `replays/profile-N/slot-1.grr` through `slot-10.grr`.

No replay directory, temporary replay or replay file is written until the player
chooses a save slot (and confirms an overwrite). Recordings live in RAM; declining,
starting a new run or exiting discards them. Profile/high-score saves retain their
normal behavior. Save failures preserve the pending recording and offer retry or
back navigation. Replay writes use a temporary file and replace the chosen slot
with rollback protection. Temporary files are cleaned on a failed write.

## Input and pause handling

Each native tick records resolved logical coordinates, after device input,
sensitivity, orientation and boundary handling. One byte stores two small deltas
(-4..3), two bytes store two medium deltas (-64..63), and three bytes store any
absolute position in either viewport. Large Control 1 jumps are lossless. Held
and pressed action masks are separate from position and are emitted only when
the combined button state changes. Their current values are applied on every tick,
including repeated pressed masks if present in the original consumed stream.

Paused wall time and menu navigation do not advance replay time. Input resets on
pause/resume/focus changes are recorded between ticks without adding duration.
Campaign information screens likewise add no playback delay; a typed state
transition preserves their effect on inherited simulation state before play
continues. Recording is limited to 1,080,000 native ticks (90 minutes at 200 Hz). If recording
runs out of capacity or allocation fails, gameplay continues and the replay is
reported unavailable; a truncated run is not presented as a complete recording.

## State and file format

Version 2 files contain a rules identifier, mode/profile/start-level/score/tick
metadata (including highest stage reached), a compressed typed starting state, input events and a whole-file FNV-1a
checksum. The checksum detects accidental corruption; it is not authentication.
Integers are little-endian with fixed widths. Float/double values preserve their
IEEE bit patterns on the supported little-endian targets. The snapshot traverses
named state fields, not C++ memory, padding, references, addresses or object layouts.
Zero runs compress the mostly reset movement-history state. Replay schema changes
must update the format/rules identifier and the explicit field traversal.

The snapshot preserves menu-advanced RNG, profile/craft state, residual entity
fields, formation script loops, native integrators, combat budgets, world caches
and announcements. Playback uses a separate Game with profile writes disabled.
The test player verifies consumed tick count and final score, and the tests compare
serialized simulation state against live runs, including a second run in one
frontend session and naturally completed Game Over.

## Watching saved replays

Open Save Slots/Replays, select a named profile or Guest, then choose Watch Saved
Replays immediately above Exit. The list shows the profile's ten shared Standard /
AoF replay slots, with maximum stage and final score. Empty or damaged slots leave
the browser open with an error. Exit returns to the selected profile menu.

Playback advances the existing decoder on the fixed game clock, with the recorded
100/200 Hz rate. Live movement, bombs, secondary fire, craft switching and confirm
buttons do not alter the recorded run. Esc / Android Back / Start / P opens the
normal pause menu; graphical settings remain available and paused time consumes
no replay ticks. Pause menu Exit stops watching and returns to the replay list.
Playback also returns there automatically at the end. It uses a separate Game
with no profile writes, high-score changes or new recording. Standard/AoF assets
and audio follow the replay and return to the selected edition afterward.

`ReplayPlayback` is the incremental decoder; `ReplayRecording::play` uses that same
decoder for verification.
Replay formats and gameplay rules are versioned; incompatible files are rejected.
The file checksum detects accidental corruption and does not authenticate scores.
Replays are local files; playback does not upload or submit anything.

## Checks

`ctest --test-dir build -C Release -R replays --output-on-failure` covers automatic
recording, small/medium/absolute movement, button changes, input resets, checkpoint
reconstruction, corruption rejection, Game Over/quit prompts, No/discard, overwrite,
slot ten, window close and profile isolation. The renderer suite also captures the
new prompts and slot list.
