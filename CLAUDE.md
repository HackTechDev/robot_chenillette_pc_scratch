# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C++20 / Qt6 Widgets desktop app (Linux): a Scratch-like block editor to program the BatMobile Zumo robot (`../robot_chenillette`) and run the program over classic Bluetooth (SPP/RFCOMM). Sibling of the remote control `../robot_chenillette_pc_qt6`, from which `config.h`, `link.*`, `bluez.*`, `pairing.*`, `bt_pairing.cpp` and the connection part of `window.cpp` are copied unchanged — keep them in sync. UI strings and comments are in French.

Not yet tested with the real robot; checked offscreen (runner output on a socketpair, snapping, JSON round trip, screenshot).

## Build / Run

```bash
./build.sh            # → build/batmobile_scratch + build/bt_pairing
./launchScratch.sh
```

Offscreen smoke test: `QT_QPA_PLATFORM=offscreen`, link the object files with a test program (`#define private public` to reach `Link::fd_`, plug a `socketpair` end in it, `setConnected(true)`), `canvas().load(json)`, `run(canvas().startScript())`, pump events while `runner().isRunning()`. Exit with `QCoreApplication::exit()`, not `quit()` (closing opens the modal confirmation).

## Architecture

- **`blocks`** — `Kind`, the `Specs` table (text with `%1`, color, `Shape`, `Param`, command char, default) in enum order, palette `categories()`. `Block` = linked list (`next`) + `body` for loops, owned by `unique_ptr`. Value clamp/format/parse (comma decimal). JSON (`blocksToJson` / `blocksFromJson`, which drops a hat not at the top and anything after a `CapC`).
- **`blockview`** — geometry and painting shared by palette and canvas: `layout()` places a stack recursively into `Item`s (pre-order = paint order) with the owning `slot` (`BlockPtr *`) so a block can be detached; Scratch shapes (notch/tab, hat dome, C). `drag()` / `fromMime()`: every drag is a `QDrag` carrying the stack as JSON + hot spot.
- **`Palette`** — template blocks; dragging creates a fresh block. Accepts drops from the canvas = delete.
- **`Canvas`** — `Script { pos, top }` in `vector<unique_ptr>` (stable addresses). Drag: detaches block + followers (`std::move(*slot)`), runs `QDrag`; `IgnoreAction` → put back into the slot. Never prune empty scripts during a drag (the slot may be a `Script::top`). `findSnap`: after a block, inside a loop body, or above a non-hat script (within 30 px). Click on a value → `QLineEdit` overlay; click elsewhere → `runRequested(script top)`. `setRunning(true)` blocks structural edits because the `Runner` holds raw `Block*`.
- **`Runner`** — timer-driven interpreter (no threads), frame stack for loops. Timed block: send cmd, wait, send `Stop`. A loop iteration with no wait yields (`MinWaitMs`) so ■ keeps working. Sends `SpeedLevelDefault` at start; `stop()` sends `Stop`.
- **`Window`** — header (devices, connect, ▶, ■), palette | canvas, file buttons, ✕. Esc = stop. Link lost / disconnect / close stop the runner. Unsaved changes asked on New/Open/Quit.

## Command Protocol

Same as the remote control and the firmware: `z s q d` move, `v c` claw, `w x` head (held until `' '`), `0`–`9` speed `(n + 1) × 40`, `' '` stop. No keep-alive in the firmware: every command that moves must be followed by a stop, and the safety stops (■, Esc, disconnect, close, link loss) must stay.
