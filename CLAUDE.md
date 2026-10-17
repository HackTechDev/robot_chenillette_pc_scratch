# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C++20 / Qt6 Widgets desktop app (Linux): a Scratch-like block editor to program the BatMobile Zumo robot (`../robot_chenillette`) and run the program over classic Bluetooth (SPP/RFCOMM). Sibling of the remote control `../robot_chenillette_pc_qt6`, from which `config.h`, `link.*`, `bluez.*`, `pairing.*`, `bt_pairing.cpp` and the connection part of `window.cpp` are copied unchanged — keep them in sync. UI strings and comments are in French.

Not yet tested with the real robot; checked offscreen (runner output on a socketpair, snapping, JSON round trip, undo/redo, autosave restore, stage positions, zoom anchoring, wheel steps, panning, arrange, ghost insert/remove/drop via synthetic drag events, step mode, every example run in the stage, screenshots). A modal dialog (e.g. `confirmDiscard` with `modified_` set) hangs an offscreen test: reset `modified_` first.

## Build / Run

```bash
./build.sh            # → build/batmobile_scratch + build/bt_pairing
./launchScratch.sh
```

Offscreen smoke test: `QT_QPA_PLATFORM=offscreen`, link the object files with a test program (`#define private public` to reach `Link::fd_`, plug a `socketpair` end in it, `setConnected(true)`), `canvas().load(json)`, `run(canvas().startScript())`, pump events while `runner().isRunning()`. Exit with `QCoreApplication::exit()`, not `quit()` (closing opens the modal confirmation). Call `QStandardPaths::setTestModeEnabled(true)` (and set the application name) so the test does not overwrite the user's autosave.

## Architecture

- **`blocks`** — `Kind`, the `Specs` table (text with `%1`, color, `Shape`, `Param`, command char, default) in enum order, palette `categories()`. `Block` = linked list (`next`) + `body` for loops, owned by `unique_ptr`. Value clamp/format/parse (comma decimal). JSON (`blocksToJson` / `blocksFromJson`, which drops a hat not at the top and anything after a `CapC`).
- **`blockview`** — geometry and painting shared by palette and canvas: `layout()` places a stack recursively into `Item`s (pre-order = paint order) with the owning `slot` (`BlockPtr *`) so a block can be detached; Scratch shapes (notch/tab, hat dome, C). `drag()` / `fromMime()`: every drag is a `QDrag` carrying the stack as JSON + hot spot.
- **`examples/*.batmobile`** — example programs compiled in as Qt resources (`:/examples/…`, `qt_add_resources` in CMake), listed in `Examples` in `window.cpp`. Angles assume the stage calibration (0.5 s ≈ 90°). Durations have 2 decimals (`Range` in `blocks.cpp`; display trims to ≥ 1 decimal).
- **`Palette`** — template blocks; dragging creates a fresh block. Accepts drops from the canvas = delete.
- **`Canvas`** — `Script { pos, top }` in `vector<unique_ptr>` (stable addresses). Drag: detaches block + followers (`std::move(*slot)`), runs `QDrag`; `IgnoreAction` → put back into the slot. Never prune empty scripts during a drag (the slot may be a `Script::top`). `findSnap`: after a block, inside a loop body, or above a non-hat script (within 30 px). Click on a value → `QLineEdit` overlay; click elsewhere → `scriptClicked(script top)`, double-click → `scriptDoubleClicked`: the window runs on a single click only when the robot is not connected (otherwise a status hint), on a double-click always. `setRunning(true)` blocks structural edits because the `Runner` holds raw `Block*`.
  - **Ghost preview** — during a drag over the canvas, `dragMoveEvent` removes the previous ghost, runs `findSnap` on the clean model, then `showGhost`: a slot snap really inserts `incoming_` into the model (`ghostSlot_` / `ghostTail_`, painted gray via `ghostBlocks_`, followers pushed down); an above-script snap only lays out `ghostItems_`. `removeGhost()` gives the stack back to `incoming_`; `dropEvent` removes it then calls `drop()`; `endDrop()` runs on leave and after `QDrag::exec`. While a ghost is inserted, `edited()` is a no-op and `Window::autosave()` postpones (`hasGhost()`).
  - **Zoom / pan** — model coordinates (scripts, `Item`s, `pressPos_`, mime hot spot) are unzoomed; `paintEvent` scales by `zoom_`, every event position goes through `toModel()`, the minimum size and the value editor are scaled, the drag pixmap is rendered at `zoom_`. `setZoom()` keeps the anchor under the mouse: `sendPostedEvents(LayoutRequest)` so the `QScrollArea` (found by `scrollArea()`) applies the new size before the scroll bars are set. Background drag (or middle button) pans the scroll bars with global positions.
  - **Wheel on a value** — `stepValue()` changes it immediately, `wheelCommit_` (600 ms) records one history step; `commitEdit()` flushes it first, so undo/press/drag never lose it. Plain wheel elsewhere is ignored so the scroll area scrolls; Ctrl+wheel zooms.
  - **Background menu** — `arrangeScripts()` (column at `FirstScript.x`, top-to-bottom order) and `removeAll()`, both undoable.
  - **History** — every modification goes through `edited()`, which appends the full `save()` JSON to `history_` (deduplicated, max 200); `undo()` / `redo()` `restore()` a state. `load()` / `clear()` reset the history and `saved_`; `isModified()` = current state ≠ `saved_` (so undoing back to the saved state clears the `*`). Undo is disabled while running.
- **`Runner`** — timer-driven interpreter (no threads, `Qt::PreciseTimer`), frame stack for loops. Timed block: send cmd, wait, send `Stop`. A loop iteration with no wait yields (`MinWaitMs`) so ■ keeps working. Sends `SpeedLevelDefault` at start; `stop()` sends `Stop`. Every command also goes out as `sent(char)`. `Go*` blocks (`Param::None` with a `cmd`) send without waiting and set `rolling_`; `stopRolling()` sends `Stop` at the natural end of the program and before a step-mode pause (any `Stop` sent clears `rolling_`). Step mode (`start(first, true)`): `step()` pauses (`paused_`, `paused(next)`) before each action block (not Start/Repeat/Forever) once one has run since the last `resume()`; `resume(true)` runs one more, `resume(false)` continues; `stop()` works while paused.
- **`Stage`** — top-down simulation fed by `Runner::sent` (same protocol as the robot): integrates the current motion with a nanosecond `QElapsedTimer` (ms `restart()` truncation lost ~3 %), 16 ms frames only while moving. 480 × 360 units; `UnitsPerSpeed` / `DegreesPerSpeed` are guesses to calibrate on the real robot.
- **`Window`** — header (devices, connect, ▶ F5, ⏭ F10, ■), `runOrContinue()` starts or resumes a paused run, palette | canvas | stage, undo/redo, zoom, Exemples ▾ (`openExample()`: no file name, title from `untitled_`, also autosaved) + file buttons, ✕. Esc = stop. Link lost / disconnect / close stop the runner. Unsaved changes asked on New/Open (not on Quit: autosave). **Autosave**: 1 s after each change and on close, `QSaveFile` to `AppDataLocation/autosave.batmobile` with `{program, saved, fileName}`; restored in the constructor.

## Command Protocol

Same as the remote control and the firmware: `z s q d` move, `v c` claw, `w x` head (held until `' '`), `0`–`9` speed `(n + 1) × 40`, `' '` stop. No keep-alive in the firmware: every command that moves must be followed by a stop, and the safety stops (■, Esc, disconnect, close, link loss) must stay.
