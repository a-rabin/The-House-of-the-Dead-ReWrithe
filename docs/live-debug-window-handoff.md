# Live debug window — handoff (in progress)

Status as of 2026-11-25: implementation complete, **not yet verified** (the final
compile and live smoke test are outstanding). Branch `live-debug-window`, commit
`3fc5264` ("Add a live debug window (in-progress)"), based on `main` / `db219c6`.

Pushed to the fork `https://github.com/a-rabin/The-House-of-the-Dead-ReWrithe`
(remote `myfork`), branch URL:
<https://github.com/a-rabin/The-House-of-the-Dead-ReWrithe/tree/live-debug-window>.
Direct push to upstream `TawhidI-602/...` is not possible: the only authenticated
GitHub account is `a-rabin`, which has read but not write access there. Open a PR
from the fork once verified.

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
| `src/main.cpp` | `DebugWindow debugWindow` + `gDebugSnapshot` declared in `main()`; F2/Tab key handling; per-frame snapshot + separate-window event pump/draw; `drawOverlay()` hook. |
| `CMakeLists.txt` | `src/game/debug_window.cpp` added to `HOTD_GAME_SOURCES`. |

## Remaining work (in order)

1. **Build.** A build directory exists, configured with:
   ```
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
         -DHOTD_FETCH_SDL3=ON -DHOTD_FETCH_STB=ON
   cmake --build build --target hotd -j$(nproc)
   ```
   `-DHOTD_FETCH_SDL3=ON` is required (no system SDL3 here; first configure takes
   ~2 minutes). `-DHOTD_FETCH_STB=ON` is required because with OGG support off
   `src/game/audio.cpp` fails with a **pre-existing, unrelated** error
   (`Music` has no members `rate`/`total`) — that reconfigure was done, but the
   build was not rerun. **`debug_window.cpp` has never been compiled yet.**
2. **Fix compile errors** in the new code, then verify `hotd` links.
3. **Live smoke test**: start a game, F2, confirm all four pages populate and the
   overlay and separate window show identical text; check the opcode trace moves
   as the event script runs. Headless path (no game files needed to reach the
   menu): `./build/hotd --no-launcher --frames N` plus the repo's existing
   `HOTD_TRACE_*` / `HOTD_DRAW_HASH` tooling.
4. **No-regression check**: with the window closed the trace hook must be a
   no-op; compare frame hashes with the existing differential tooling.
5. **Squash / rebase into `main`** once verified, then open a PR from the fork.

## Notes

- The commit message records the same "unverified" status; this doc is the fuller
  handoff. Everything above is reconstructible from the code — keep this file
  focused on status and the order of remaining steps.
- When editing, match the surrounding code's comment density and idiom.
