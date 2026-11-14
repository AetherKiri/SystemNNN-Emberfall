# SystemNNN game session

Use `emberfall::scene::GameSession` for a game directory. A session keeps the scheduler, script stack, script variables, graphics, transitions and pending input alive until it is destroyed. The public interface is in `include/emberfall/game_session.hpp`. The older `first_frame` entry points are retained for existing diagnostic callers.

Call `step_frame(elapsed_ms, input)`, then read `surface()` for the current top-left RGB8 frame. Live Android updates pass `FrameTiming{elapsed_ms, pace(elapsed_ms)}`. Elapsed time updates the selection countdown and speaking state. The animation budget advances drawing, overlaps, and layer effects. Copy surface pixels before the next change if they must be retained. A dialogue stays pending until `input.advance`. A selection takes a one-based `input.choice`. A completed overlap exposes its final frame before the next VM step. Dialogue carries the source bytes, speaker bytes, message and table IDs.

For live Android story playback, the host can supply an overlap capture callback for the current step. It adds the displayed message plane to the outgoing scene when the script takes an overlap snapshot. If host capture is unavailable, the snapshot uses the last rendered native frame. A new decorative preview can use its supplied background before it has rendered a frame. Layer changes since that frame are drawn into the wipe target.

Call `tick_ui_background(input, loop, elapsed_ms)` once per displayed background update. The frame clock repays elapsed time within the current `DRAW` or `OVERRAP` wait, up to the game's `maxSkipFrameBackScript` limit. An `ENDIF` yields in the background. The PC global save's `noSkipFrame` limits both story and background animations to one tick per update. Use `step_ui_background` to prime the first frame or step through a controlled sequence.

Read-only skip is checked at STARTMESSAGE with the same script/serial identity as PRINT, so it stops before the first unread line's VOICE and effects; an unread STARTKOMA re-enables effect commands while message skipping continues. AUTOMESSAGE, CANNOTCLICK and CANNOTSKIP are tracked from the script (`script_restrictions()`), with `cannotSkipCheckKillShift`/`autoMessageCheckKillShift` from `game.xtx`. Platforms ask `request_advance()` before a click (it also answers for Draw/Overlap waits), drive skip advances only while `skip_effective()`, and advance on `auto_ready()`, which also runs the script auto-message timer from `printmessage.xtx`. `step_frame` itself does not gate advances, so recorded input journals replay unchanged.

`set_ctrl_skip()` tracks the held Ctrl key. `CANNOTSKIP` blocks Ctrl-driven text advancement, and script audio evaluates Ctrl with the current skip settings and the message ID supplied by `STARTMESSAGE`. The script's `MOUSENUMBER` is exposed as `FrameState::script_mouse` and uses the original pointer field in a PC story save.

PC story loads are staged in a candidate session. A save with a `LOG` block replaces the backlog, including an explicitly empty log. Without `LOG`, an in-game load retains the previous session's complete history and owned jump checkpoints. Pass that story session to `load_pc_save_candidate`; a load from the title keeps the new session's configured `firstMessage` and `barMessage` entries instead. The host must prepare its message-plane and runtime references before retiring the previous owner. Failed validation leaves the previous session usable. `clearAutoAfterLoad` controls whether a successful load clears auto and skip.

Current-message voice replay state survives the end of PCM playback. Its original per-channel lifetime advances on PRINT, LPRINT and APPEND, and `playLoadVoiceFlag` replays a one-shot voice only when loading a message. Native snapshots with this voice state use version 17; readers retain compatibility with older snapshots. PC save layout is unchanged.

Build and run the desktop framebuffer capture from the repository root.

```sh
cmake -S platform/scene -B build/scene-host-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build build/scene-host-debug --parallel --target nnn-scene-player
build/scene-host-debug/nnn-scene-player "/path/to/DeadEndAegis [zh]" --entry sys.spt --limit 85 --verify --capture 17 build/game-frames/deadendaegis-zh.png
```

Each PNG contains the native layer framebuffer at the specified dialogue. The companion JSON records the script word, message, active layers, source packages, canvas extents and trimmed sprite bounds. Dialogue and choices are available to the presentation frontend. The desktop runner automatically clicks dialogue, consumes timed frames and chooses the requested option. Use `--choice 2` for the second branch, `--click-draw` to simulate a click during drawing, and `--list` to see the resource bindings at each dialogue.

Graphic lookup checks loose game files first, then the prefix-matched package and other available package indexes. An 8A sprite keeps its original canvas position when its transparent margins are dropped from the cache. A missing graphic or an unhandled effect is reported once per session. BUFFER retains a texture off-screen; MAE retains the preceding effect. SETCG records gallery registration and can resolve its graphic from the bound layer or the game's CG list.

For audio, set `SessionOptions::audio.sample_rate` to the device rate before creating the session. On the game thread, `step_frame` dispatches script events and reads the requested VPK range or loose file. VAW byte 57 selects PACKTYPE 2 (wrapped Ogg Vorbis) or PACKTYPE 0 (raw 8/16/24-bit PCM WAVE), as in `CWaveData::LoadWaveRoutine`. WGQ music supports PACKTYPE 6 and the legacy PACKTYPE 2 offset used by `CWaveMusic::OpenNewFile`. The decoder converts PCM to signed 16-bit samples for mixing. On the audio thread, call `session.audio().pull(dst, frames)` with space for `2 * frames` samples. Silence, resampling, mixing and fades all advance on this device clock. Keep pulling while the VM waits for input. The pull path does not open files or decode Vorbis. The optional `take_audio_events()` queue mirrors already-applied requests. Set `queue_audio_events=false` for a PCM-only frontend.

Music IDs are one-based; script SE and UI SE IDs are zero-based. Call `play_system_se(id)` for a menu cue from `nya/systemse.xtx`. Speech checks loose chapter and system voices before the matching VTB/VPK. Only the requested VPK member is read. The cache holds decoded clips (64 MiB by default); active tracks also retain their segments. All MML parts of the current score are ready before playback starts, so a device buffer can cross the intro/loop boundary without a gap. `set_character_slider(prefix, steps)` adjusts the corresponding role in `spt/charaname.xtx`.

This game's bytecode matches upstream `scriptDefine.h`: suffixes `66/67` set the next SE/voice fade, `68/69` set SE/voice volume, `6a` sets music volume and `6c` records a character voice flag. Music plays/stops and music volume changes carry their own fade time (tenths of a second); the host can also call `set_next_music_fade`.

To reproduce the opening excerpt and the trace (one row per VM request, with decoded file, rate, channels and frames for plays):

```sh
game="/path/to/DeadEndAegis [zh]"
build/scene-host-debug/nnn-scene-player "$game" --entry sys.spt --limit 85 --verify \
  --dialogue-ms 1500 --wait-voice --wav-start-ms 9500 --wav-ms 22000 \
  --wav build/game-audio/deadendaegis-zh.wav --audio-stats build/game-audio/deadendaegis-zh.json
cmake --build build/scene-host-debug --target nnn-audio-check
build/scene-host-debug/nnn-core/nnn-audio-check "$game"
```

`--wait-voice` lets foreground speech finish before the simulated reader clicks. For the real debug menu, use `--entry dea_debug.spt --limit 12 --click-draw --choice 1` or `--choice 2`. The separate audio check exercises all four voices, role gain, SE and system routing, fade endpoints at 48 kHz, and sample-exact transitions of bgm01, bgm10, bgm17 and the MML-free short.wgq at their native rate.

## Audit game setup fields

Run the read-only audit with Python 3. It uses the local upstream reference checkout, or a directory supplied with `--upstream`.

```sh
python platform/scene/tools/nnn_config_audit.py "/path/to/DeadEndAegis [zh]"
python platform/scene/tools/nnn_config_audit.py --discover "/path/to/game installations" --json > build/config-audit.json
python -m unittest discover -s platform/scene/tools
```

The report lists fields and line numbers from `init/*.xtx` and the matching `init2/*.fxf` overrides. The JSON includes literal query sites and candidate formatted keys found in the upstream C++ sources. `SUPPORTED`, `PARTIAL`, `UNSUPPORTED_BEHAVIOR` and `PC_WINDOW_ONLY` reflect individually checked contracts. `REVIEW` records upstream evidence pending a behavior check. `UNKNOWN` marks keys without a query the scanner could match. A listed field may depend on another setting or the current mode.
