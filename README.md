# omarecord

Region, window and monitor screen recording for [Niri](https://github.com/YaLTeR/niri)
with live on-screen annotation. One binary, two subcommands:

- `omarecord select` picks a rect and prints it as JSON.
- `omarecord overlay` draws a recording border, a toolbar and fading strokes over
  that rect, and relays toolbar events over a Unix socket.

Neither subcommand starts a recorder. They are primitives for an orchestrator;
the author drives them from a recorder script behind a desktop-shell panel, and
the same contract works from any script that can spawn processes and speak
newline JSON.

## Why

Niri has no region recorder with annotation. `gpu-screen-recorder` already
captures a `WxH+X+Y` rect from KMS at full speed, and it records whatever is on
scanout, including a layer-shell surface drawn over the region. omarecord
supplies the two missing pieces: choosing the rect and drawing over it.

## How it fits together

```
panel button
  └─ recorder script (orchestrator, owns state)
       ├─ omarecord select --mode region      → {"output":"DP-4","x":…,"w":…}
       ├─ gpu-screen-recorder -w 800x400+100+-1400 -o out.mp4
       └─ omarecord overlay --rect 800x400+100+-1400 --socket /run/user/1000/rec.sock
            ← {"cmd":"elapsed","seconds":12}   (orchestrator, once a second)
            → {"event":"stop"}                  (user pressed Stop)
```

## Requirements

- Niri (`niri msg --json` is the only compositor interface used)
- Qt 6.8 and `layer-shell-qt` (build time)
- `grim` and `wl-clipboard` on `PATH` for `--mode window` and Space in the
  selector; the flake's wrapper adds them along with `niri`
- `gpu-screen-recorder` for the actual capture. Its `gsr-kms-server` must be the
  setcap wrapper; on NixOS set `programs.gpu-screen-recorder.enable = true;`

## Install

Run without installing:

```sh
nix run github:Kabilan108/omarecord -- select --mode monitor
```

As a flake input:

```nix
{
  inputs.omarecord.url = "github:Kabilan108/omarecord";
  # environment.systemPackages = [ inputs.omarecord.packages.x86_64-linux.omarecord ];
}
```

## Usage

### `omarecord select`

```
omarecord select [--mode region|window|monitor] [--output NAME]
```

Prints one line of JSON on stdout and exits:

```json
{"output":"DP-4","x":100,"y":-1400,"w":800,"h":400}
```

Coordinates are Niri global logical pixels, so `x` and `y` are negative for
outputs placed above or left of the origin.

| Mode | Behaviour |
|---|---|
| `region` (default) | Dims `--output` (or the focused output) and waits for a drag. Enter or button release confirms, Space picks the focused window, Ctrl+A picks the whole output, Esc cancels. The rect is clamped to that output. |
| `window` | Rect of the focused window, no UI. With `--output`, the window must be on that output. |
| `monitor` | Logical rect of `--output` or the focused output, no UI. |

Exit codes: `0` printed a rect, `1` cancelled (Esc, or a signal before a
choice), `2` error, with the reason on stderr. Running `select` while another
`select` is up dismisses the first one, which exits `1`.

### `omarecord overlay`

```
omarecord overlay --output NAME --rect WxH+X+Y --socket PATH
```

Long-running. Creates a full-output layer surface on the `overlay` layer, draws
a 2 px border outside the rect (red recording, amber drawing, grey dashed
paused) and a toolbar just outside the rect's top edge (bottom edge if there is
no room; a 24 px corner pill that expands on hover when the rect is the whole
output). Input passes through everywhere except the toolbar until Draw is
turned on. Exits `0` on `quit`, `2` on bad arguments or if the surface cannot be
created.

`--rect` uses gpu-screen-recorder's form: `WxH+X+Y` with the sign after the
plus, so a rect at y = -1400 is `800x400+100+-1400`. gpu-screen-recorder
treats `-` used as a separator as the start of a monitor name.

The overlay listens on `--socket` (one client at a time, newline-delimited JSON
objects):

| Direction | Message | Meaning |
|---|---|---|
| in | `{"cmd":"paused","value":true}` | grey border, timer frozen; `false` resumes |
| in | `{"cmd":"elapsed","seconds":12}` | sets the timer text; send once a second |
| in | `{"cmd":"quit"}` | exit 0 |
| out | `{"event":"ready"}` | surface is mapped; also re-sent to each new client |
| out | `{"event":"pause"}` | user pressed Pause/Resume |
| out | `{"event":"stop"}` | user pressed Stop |

The overlay keeps no clock of its own and never changes pause state by itself;
it reports button presses and reflects what the orchestrator tells it.

#### Keys in draw mode

Draw mode is entered from the toolbar's Draw button. While it is on:

| Key | Action |
|---|---|
| `P` `A` `R` `H` | Pen, arrow, rectangle, highlighter |
| `1` – `4` | Colour: red, yellow, green, blue |
| wheel | Stroke width (3, 6, 10 px; the highlighter is 4x wider) |
| `L` | Hold: strokes persist until cleared instead of fading |
| `C` | Clear all strokes |
| `Esc` | Leave draw mode |

Strokes fade 5 s after the pointer is released, over 0.7 s.

#### Environment

| Variable | Default | Meaning |
|---|---|---|
| `OMARECORD_FADE_SECONDS` | `5` | Seconds a stroke stays fully visible |
| `OMARECORD_FADE_DURATION` | `0.7` | Seconds the fade takes |

Both accept fractional values.

### Driving it from a shell

```sh
#!/usr/bin/env bash
set -euo pipefail

region=$(omarecord select --mode region)
output=$(jq -r .output <<<"$region")
geometry=$(jq -r '"\(.w)x\(.h)+\(.x)+\(.y)"' <<<"$region")
sock="${XDG_RUNTIME_DIR}/omarecord-$$.sock"

gpu-screen-recorder -w "$geometry" -f 30 -cursor yes -o "$HOME/rec.mp4" &
gsr=$!
omarecord overlay --output "$output" --rect "$geometry" --socket "$sock" &
overlay=$!

until [ -S "$sock" ]; do sleep 0.1; done
python3 - "$sock" "$gsr" <<'PY'
import json, os, signal, socket, sys, time
sock, gsr = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_UNIX)
s.connect(sock)
s.setblocking(False)
start = time.monotonic()
buf = b""
while True:
    seconds = int(time.monotonic() - start)
    s.send(json.dumps({"cmd": "elapsed", "seconds": seconds}).encode() + b"\n")
    try:
        buf += s.recv(4096)
    except BlockingIOError:
        pass
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        if json.loads(line).get("event") == "stop":
            os.kill(gsr, signal.SIGINT)
            s.send(b'{"cmd":"quit"}\n')
            sys.exit(0)
    time.sleep(1)
PY
wait "$gsr" "$overlay"
```

To send one command by hand:

```sh
printf '{"cmd":"elapsed","seconds":42}\n' | socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR/omarecord-1234.sock"
```

## Limitations

- A region lives on one output; drags are clamped to the output the selector
  was opened on.
- Window mode takes the window's rect at start and does not follow it. The
  border stays visible so a mismatch is obvious.
- Niri IPC reports no position for tiled windows, so window mode captures the
  output with `grim`, runs `niri msg action screenshot-window`, and finds the
  window buffer inside the output image. `screenshot-window` shows Niri's
  screenshot notification and puts the image on the clipboard; omarecord
  restores the previous clipboard contents afterwards.
- No countdown, no cursor effects, no keystroke display.

## Development

```sh
nix develop            # or let direnv load .envrc
make smoke             # build and run the headless Qt Test suite (offscreen)
make check             # smoke + clang-tidy + clazy where available
nix build              # sandboxed build, runs the same suite
```

Live checks need a Niri session: `./build/omarecord select --mode monitor`
prints the focused output.

| Path | Purpose |
|---|---|
| `src/select*`, `src/selector-window*` | `select` subcommand and its layer-shell picker |
| `src/overlay*` | `overlay` subcommand, socket protocol, state, window |
| `src/window-resolve*`, `src/window-locate*` | focused-window rect resolution |
| `src/niri*`, `src/rect*` | Niri IPC parsers, `WxH+X+Y` geometry |
| `tests/` | headless smoke suite |
| `docs/plan.md` | design record: decisions, contracts, work slices |
| `AGENTS.md` | full file table and conventions |

## Credits

The selector, toolbar icons, instance lock and drawing code come from
[OmaSnap](https://github.com/Kabilan108/omasnap), a Niri port of Tobi Lütke's
[omasnap](https://github.com/tobi/omasnap). The Neucha font in `assets/` is
under the SIL Open Font License (`assets/OFL.txt`).

## License

MIT. See [LICENSE](LICENSE); the notice keeps OmaSnap's copyright line because
substantial code is derived from it.
