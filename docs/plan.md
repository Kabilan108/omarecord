# omarecord — v1 plan

Region/window/monitor screen recording for Niri with live on-screen annotation.
Two small native primitives (`select`, `overlay`) driven by the existing
`stillsuit-recorder` orchestrator; `gpu-screen-recorder` does the capture.

Status: agreed 2026-09-19, not started. Decisions below are settled unless a
spike overturns them; anything marked *open* is not.

## Decisions (settled)

| Topic | Decision |
|---|---|
| Relationship to stillsuit | Sole recorder path. `stillsuit-recorder` (Python) stays the orchestrator and state owner; omarecord supplies `select` and `overlay`. Monitor mode with no annotation is byte-for-byte the current meeting flow. |
| Capture backend | `gpu-screen-recorder` KMS path, `-w WxH+X+Y` (global Niri coordinates; `-region` is deprecated). `-cursor yes` so the compositor cursor is in the video. |
| Annotations | Drawn on a transparent layer surface over the region; captured by gsr because it reads scanout (verified). Baked into the file. |
| Stroke lifetime | Fade after 5 s (last 0.7 s animates). `Hold` toggle switches to persist-until-clear. Both durations env-configurable. |
| Tools v1 | Pen, arrow, rectangle, highlighter. Colours 1–4, width via scroll. |
| Cursor | gsr's captured cursor only. No spotlight, no click ripple. |
| Draw mode | Toolbar button only, no hotkey. Pass-through until Draw is on; Esc or the button turns it off. |
| Toolbar placement | Outside the recorded rect (top edge, falls back to bottom). Full-monitor: 24 px pill in a corner, expands on hover. |
| Window mode | Rect of the focused window at start; does not follow the window. Border stays visible so a mismatch is obvious. |
| Countdown | None. |
| Format | mp4/h264 30 fps vfr, `OMARECORD_FPS` env override. GIF and WebM export from the completed panel via ffmpeg (GIF uses palettegen). |
| Audio defaults | Region/window: desktop off, mic off. Monitor: unchanged (desktop on, mic off). |
| Privacy (`block-out-from`) | Not in v1. KMS path ignores it; portal path crashed in the spike and cannot crop. |
| Keystroke display | v1.1. evdev + xkbcommon; `input` group already added to `user.nix`. |
| Trimming/editing | Later. |
| Stack | New repo, C++/Qt6 + layer-shell-qt, Nix flake, `make check` smoke suite, lifting from OmaSnap (`/vault/repos/omasnap`, `niri-native`). |

## Spike results (2026-09-19)

- `gpu-screen-recorder -w 800x400+100+-1400` on DP-4 (logical y = -1504) → 800×400 file with correct content. Coordinates are Niri global space.
- OmaSnap's layer-shell overlay appears in the gsr recording. Overlay-drawn annotations will be captured.
- `-w portal` shows the GNOME "Share Screen" dialog (Window / Display tabs, a bogus "Virtual" entry Niri cannot back), then gsr dumped core after PipeWire negotiation. Not pursued.
- gsr must run outside a sandbox: `gsr-kms-server` is a setcap wrapper. Fine from the user session.
- Pointer position is unavailable to a pass-through layer surface and Niri IPC has no pointer events. Moot after dropping spotlight.

## Components

### `omarecord select`

```
omarecord select [--mode region|window|monitor] [--output NAME]
→ stdout: {"output":"DP-4","x":100,"y":-1400,"w":800,"h":400}
→ exit 1 on cancel (Esc), 2 on error
```

Lift from OmaSnap: the `Phase::Select` half of `CaptureEditor` (dim, crosshair,
drag rect, pointer readout), Niri IPC helpers in `capture.cpp`
(`focused-output`, `focused-window`, `workspaces`), `instance-lock`, `icons`.
Keys as OmaSnap: drag = region, Space = focused window, Ctrl+A = monitor,
Enter/release = confirm, Esc = cancel. `--mode window|monitor` skips the
interactive step and prints immediately. Output is clamped to one output.

### `omarecord overlay`

```
omarecord overlay --output DP-4 --rect WxH+X+Y --socket PATH
```

Long-running. Layer surface on `overlay` layer, anchored to all edges,
exclusive zone -1, keyboard interactivity `none` while pass-through and
`on_demand` while drawing. Input region: toolbar only (pass-through) or
toolbar + rect (drawing). Draws:

- 2 px border outside the rect: red recording, amber drawing, grey dashed paused.
- Toolbar: `● 00:12 · Draw · Hold · Pause · Stop` plus colour swatches/width when drawing.
- Strokes with per-stroke birth time; fade unless Hold.

Socket protocol (newline JSON, one client at a time):

| Inbound | Effect |
|---|---|
| `{"cmd":"paused","value":true}` | grey border, timer frozen |
| `{"cmd":"elapsed","seconds":N}` | timer text (sent by orchestrator each second, keeps one clock) |
| `{"cmd":"quit"}` | exit 0 |

| Outbound | Meaning |
|---|---|
| `{"event":"pause"}` | user pressed Pause/Resume |
| `{"event":"stop"}` | user pressed Stop |

Overlay never touches gsr or `recording.json`. Toolbar events go out the
socket; the orchestrator acts on them.

Lift from OmaSnap: `Annotation` model and the pen/arrow/rect/highlighter
rendering from `editor.cpp`, `icons`, Neucha font, toolbar button layout.
Not lifted: operation log, undo, text/OCR/markers, export, pin.

### `stillsuit-recorder` changes (`~/dotfiles/bin/stillsuit-recorder`)

- `start` gains `--target region|window|monitor` (default `monitor`) and
  `--annotate` (implied for region/window, off for monitor unless passed).
- Region/window: run `omarecord select`, spawn gsr with `-w WxH+X+Y`, spawn
  `omarecord overlay`, connect to its socket, write both PIDs.
- Orchestrator thread/loop: forwards `elapsed` each second, maps overlay
  `pause`/`stop` events to the existing `toggle_pause`/`stop_recording`.
- `toggle-pause`, `stop`, `cancel`: also notify/quit the overlay.
- `export --format gif|webm`: ffmpeg in the background, progress into state.
- `recording.json` additions: `target`, `rect {output,x,y,w,h}`, `annotate`,
  `export {format, phase, output, error}`. `schemaVersion` stays 1 (additive).

### stillsuit-shell changes (`~/dotfiles/packages/stillsuit-shell`)

- `RecordingPanel.qml`: capture row `Monitor | Region | Window`; audio
  defaults switch with target; `Annotate` checkbox visible for Monitor only.
- `RecordingService.qml`: `start(...)` passes `--target`/`--annotate`; new
  `exportAs(format)`.
- Completed state: `GIF` and `WebM` buttons with progress and resulting path.
- `RecordingWidget.qml`: label shows `REC DP-4 800×400` for non-monitor targets.
- Fixtures under `src/tests/recording-meetings` and `d5-workflows`: add
  region/export phases.
- Nix: `recorder-helper.nix` gains `omarecord` and `ffmpeg` in `runtimeInputs`;
  flake input for the omarecord repo.

## Work slices

Each slice ends with something runnable and a check.

1. **Repo skeleton.** CMake, flake with devshell and package, `make check`
   (offscreen Qt Test), `AGENTS.md`. Check: `nix build` succeeds, one trivial test passes.
2. **`omarecord select`.** Lift selector. Check: smoke test drives an offscreen
   region drag and asserts the JSON; manual run on DP-4 and eDP-1 prints
   rects matching `niri msg outputs`.
3. **`stillsuit-recorder --target region`, no overlay.** Check: Region from the
   panel → selector → file in `~/media/recordings` with the selected size.
   First visible milestone.
4. **`omarecord overlay` border + toolbar + socket, no drawing.** Check: pause
   and stop from the toolbar drive the recorder; timer matches panel elapsed;
   border is outside the captured frame (ffmpeg frame grab).
5. **Drawing.** Pen/arrow/rect/highlighter, fade, Hold, Esc. Check: smoke
   test for stroke lifetime; manual recording shows strokes and their fade.
6. **Window and monitor targets, pill toolbar.** Check: Space in selector
   records the focused window rect; full-monitor annotate shows the pill.
7. **Exports.** GIF/WebM buttons. Check: outputs play, GIF is palette-based.
8. **Stillsuit polish and fixtures.** Widget label, fixtures, `d5-workflows`
   run green.

v1.1 backlog: keystroke display (evdev + xkbcommon, pill at bottom edge),
click ripple, portal/private mode once gsr's portal path is stable, trimming.

## Open

- Overlay input-region switching on Niri: confirm layer-shell-qt exposes
  `wl_surface.set_input_region` changes at runtime, otherwise re-create the
  surface on Draw toggle (slice 4).
- Whether the orchestrator's socket loop lives in the existing Python
  (threads + `selectors`) or as a small `stillsuit-recorder daemon` child.
  Decide in slice 3.
