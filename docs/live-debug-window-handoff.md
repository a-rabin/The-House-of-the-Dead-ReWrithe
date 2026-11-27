# Live debug window — handoff (verified)

Status as of 2026-11-27: implementation complete and **verified** — the build
succeeds, the new unit suite passes, and a headless run with the real arcade ROMs
showed all four pages populated with live data (including named creatures whose
HP changes as shots land, and an opcode trace that advances frame by frame).
Branch `live-debug-window`, based on `main` / `db219c6`.

Pushed to the fork `https://github.com/a-rabin/The-House-of-the-Dead-ReWrithe`
(remote `myfork`), branch URL:
<https://github.com/a-rabin/The-House-of-the-Dead-ReWrithe/tree/live-debug-window>.
Direct push to upstream `TawhidI-602/...` is not possible: the only authenticated
GitHub account is `a-rabin`, which has read but not write access there. Open a PR
from the fork.

## What this is

A developer-facing live view of the running game: the creatures on screen, the
scene / camera / event-script state, the event script's opcodes (a live trace and
a full disassembly of the current section), and the runtime's counters.

The reference repo <https://github.com/a-rabin/thotd-re> is an **offline / static
PySide6 archive viewer** (`tools/scene_viewer/`). It has no live game state and no
monsters, so it served as a UI/feature checklist only — none of its code is reused.
The user explicitly chose **both** an in-game overlay **and** a separate window,
and **both** a live opcode trace **and** a full disassembly.

## How it is built

Two views of **one** snapshot, so they cannot disagree:

- the in-game overlay reuses `render::Overlay` (the HUD's own debug-panel renderer);
- a second SDL window with a **software** renderer (deliberately no second GL
  context), created beside the game window. If SDL refuses one (headless / CI),
  the overlay still works.

F2 toggles the window (overlay + separate window); Tab cycles four pages:
**Monsters**, **Scene**, **Opcodes**, **Debug**. F1 keeps its existing meaning
(the plain debug-lines panel). The window is off by default, reads game memory
only, and has no effect on game logic.

## Files changed

| File | Change |
|---|---|
| `src/game/debug_window.h`, `src/game/debug_window.cpp` (new) | `DebugSnapshot` collector: creatures from `Runtime::shootable` (with `CreatureCategories::typeName`), scene/camera/event state, players, boundary counters. `DebugWindow::disassemble()` walks the live script using the interpreter's operand table. Overlay + separate-window renderers. |
| `src/port/game.h`, `src/port/event.cpp` | new `virtual void evt_trace(pc, opcode)` hook, called from `Game<P>::evt_run`. No-op by default: proofs / normal games are unaffected. |
| `src/game/runtime.h`, `src/game/runtime.cpp` | `Runtime` overrides it into a bounded ring buffer (`kEvtTraceMax` = 512), recorded **only** while `traceEnabled` (the window is open). |
| `src/game/event_script.h`, `src/game/event_script.cpp` | `evtOperandCount()` / `kEvtEnd` exported from the header so the interpreter and the disassembler share ONE operand table. |
| `src/main.cpp` | `DebugWindow debugWindow` + `gDebugSnapshot` declared in `main()`; F2/Tab key handling; per-frame snapshot + separate-window event pump/draw; `drawOverlay()` hook; a `HOTD_DEBUG_TRACE` headless dump of the same panels for smoke tests. |
| `CMakeLists.txt` | `src/game/debug_window.cpp` added to `HOTD_GAME_SOURCES`. |
| `tests/unit/test_debug.cpp` (new), `tests/CMakeLists.txt` | unit suite `debug`: the operand table, the disassembler's walk, and the bounded trace ring. |

## Remaining work (in order)

## Verification done (2026-11-27)

- **Build**: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DHOTD_FETCH_SDL3=ON -DHOTD_FETCH_STB=ON` then `cmake --build build --target hotd`.
  `debug_window.cpp` compiles cleanly and `hotd` links. `-DHOTD_FETCH_SDL3=ON` is
  required (no system SDL3 here; first configure takes ~2 minutes).
  `-DHOTD_FETCH_STB=ON` is required because with OGG off `src/game/audio.cpp`
  fails with a **pre-existing, unrelated** error (`Music` has no `rate`/`total`).
- **Unit tests**: new suite `debug` (`tests/unit/test_debug.cpp`) covers the shared
  operand table, the disassembler's instruction walk (fixed / list / CCTB shapes,
  the current-instruction mark, the data-section terminator) and the bounded trace
  ring. `./build/hotd_unit debug`: 3 tests, 30 checks, 0 failed. The full suite:
  61 tests, 348,392 checks, 0 failed; `ctest -L unit`: 15/15 passed.
- **Live smoke test** with real arcade ROMs:
  `HOTD_DEBUG_TRACE=PICK ./build/hotd --arcade /home/adrian/Desktop/Games/hotd.zip
  --frames 90 --autofire --mute --no-title` printed all four pages. A longer run
  (`HOTD_DEBUG_TRACE=1 --frames 1800`) showed named live creatures (Neil, Cyril)
  with HP changing as shots landed and the opcode trace advancing frame by frame.
  `HOTD_DEBUG_TRACE` is a headless trace of the same snapshot and panel lines the
  two views use, so it needs no GPU.

## Remaining work

1. **Optional**: verify the two graphical views in an interactive window (F2
   toggles; Tab cycles Monsters / Scene / Opcodes / Debug) — the headless trace
   verifies the shared data and text, but not GL/SDL presentation.
2. **Squash / rebase into `main`**, then open a PR from the fork.

## Notes

- The first commit message says "in-progress"; later commits carry the fixes and
  verification. This doc is the fuller handoff.
- When editing, match the surrounding code's comment density and idiom.
