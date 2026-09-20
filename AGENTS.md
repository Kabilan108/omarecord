# omarecord — Agent Guide

Niri-native screen recording helpers: `omarecord select` picks a region,
window, or monitor and prints it as JSON; `omarecord overlay` draws the
recording border, toolbar, and live annotations over the region while
`gpu-screen-recorder` (driven by `stillsuit-recorder`) captures the screen.
Read `docs/plan.md` first: it holds the settled decisions and the CLI/socket
contracts. Do not re-open settled decisions.

## Principles

- **Two primitives, no orchestration.** omarecord never starts gpu-screen-recorder
  and never touches `recording.json`. It prints a rect, or it draws and relays
  toolbar events over its socket.
- **Niri only.** Output and window discovery via `niri msg --json`. Layer
  surfaces via layer-shell-qt.
- **Speed first, no settings UI.** Env vars and CLI flags only.
- **No backwards compatibility.** Break flags or protocol when it simplifies.
- **Single binary.** Both subcommands live in `omarecord`.

## Layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | Subcommand dispatch only |
| `src/select.cpp/.hpp` | `select` subcommand |
| `src/overlay.cpp/.hpp` | `overlay` subcommand |
| `src/niri.cpp/.hpp` | Niri IPC queries and pure JSON parsers |
| `src/rect.cpp/.hpp` | `RegionRect`, WxH+X+Y parsing/formatting |
| `src/icons.cpp/.hpp` | Vector toolbar icons (from OmaSnap) |
| `src/instance-lock.cpp/.hpp` | Single-instance lock (from OmaSnap) |
| `tests/*-smoke.cpp/.hpp` | Headless Qt Test; registered in `tests/smoke-main.cpp` |
| `docs/plan.md` | Decisions, contracts, work slices |

Source lifted from OmaSnap lives at `/vault/repos/omasnap` (branch
`niri-native`); copy what you need, trim what you don't.

## Verification

`nix develop --command make smoke` builds and runs the headless suite
(`QT_QPA_PLATFORM=offscreen`). `nix build` runs the same suite in the sandbox.
Capture output to a file and check the exit code; never pipe it through
`tail`/`grep`. Live checks against the compositor need a real session:
`./build/omarecord select --mode monitor` should print the focused output.

## Conventions

- C++23, Qt 6.8, `-Wall -Wextra -Wpedantic` clean.
- Comments explain why, never what. No commented-out code.
- One `@fileoverview` line per header. Pure parsers take bytes and an
  `error` out-param so tests never need Niri.
