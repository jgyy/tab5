# tab5
The Tab5 is a highly expandable, portable smart IoT terminal designed for developers, integrating a dual-core architecture and rich hardware resources. It is built around the ESP32-P4 SoC based on the RISC-V architecture, featuring 16MB Flash and 32MB PSRAM for high-performance application development.

Full hardware datasheet: `docs/Tab5.pdf`.

## Xianxia Idle Game

A cultivation-themed idle game running natively on the Tab5. Cultivate **Qi**, buy
passive **Cultivation Methods** (generators), and advance through seven **Cultivation
Realms** — Mortal Body, Qi Condensation, Foundation Establishment, Core Formation,
Nascent Soul, Soul Transformation, Void Refinement — each breakthrough spending the
next realm's Qi threshold. The centerpiece is a procedurally-shaded, rotating low-poly
crystal (a plain 20-face icosahedron with per-realm vertex displacement and color
palette) rendered with a custom software 3D rasterizer, since the ESP32-P4 has no GPU.
It draws into a 240x240 offscreen buffer at roughly 27-28 FPS on real hardware (measured
on a debug build; a release build would likely be faster, not yet benchmarked) — Task 8
found no FPS headroom to justify subdividing the mesh further.

The game plays itself, idle-game style: generators are auto-bought and realm
breakthroughs auto-triggered every 50ms tick (breakthrough is checked first, then one
purchase attempt per generator in unlock order) as Qi allows. Manual tap-to-buy and
tap-to-breakthrough are still wired up, but automation checks every 50ms, so in practice
it wins the race before you can tap. Automated actions don't force an immediate save (to
spare NVS flash write endurance from very frequent writes) — the existing 15-second
periodic autosave covers them; manual taps still save immediately.

The whole UI is a hand-rolled M5GFX HUD (no LVGL), composited into offscreen `M5Canvas`
sprites and pushed to the display once per frame to avoid flicker. Despite the panel
being physically landscape, M5GFX reports the Tab5's display as 720x1280 (portrait
logical coordinates) — confirmed by reading the M5GFX source and a live serial print on
real hardware — so the UI is laid out as a vertical stack (status header bar, then the
crystal viewport, then the generator/breakthrough panel filling down to the bottom),
computed at runtime from `M5.Display.width()`/`.height()` rather than hardcoded. Held
normally in landscape, this reads correctly on the physical device. The header bar
shows battery percentage and charging state (a real reading from `M5.Power`) alongside
the current realm name and Qi/sec rate; there's deliberately no clock, since without
Wi-Fi/NTP there'd be nothing to keep it from silently drifting. Long generator names and
large Qi totals are handled with measured `textWidth()`-based sizing and compact
`K`/`M`/`B` number formatting (e.g. `2.2M` instead of `2200000`).

Progress persists across power cycles via NVS (verified to survive real reboots, not
just a same-boot false positive), and the Tab5's battery-backed RTC grants offline
earnings on boot, computed with a custom epoch conversion (since `timegm()` isn't
available in this toolchain) checked against leap-year and century-boundary edge cases.

Design spec: `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-xianxia-idle-game.md`

### Building & Flashing

Requires [PlatformIO](https://platformio.org/):

```bash
python3 -m pip install --user platformio
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```

Serial console (115200 baud):
```bash
python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200
```

### Running Tests

Game logic (3D math, procedural mesh growth, the software rasterizer, the idle-game
economy, save serialization, offline-earnings math, HUD hit-testing) is hardware-agnostic
C++ under `lib/core/`, unit-tested on the host machine — no device required. 48 test
cases across 8 suites, all passing:

```bash
python3 -m platformio test -e native
```

`src/` (display setup, the game loop, HUD drawing, NVS/RTC glue) is Arduino/hardware
glue instead, and was validated on the physical Tab5 rather than in `native` tests — 12+
flashes across this project with zero panic/crash/watchdog signatures.

### Project Layout

- `lib/core/` — hardware-agnostic game logic, unit-tested via the `native` PlatformIO environment.
- `src/` — Arduino/M5Unified/M5GFX glue: `main.cpp` (setup/loop, the 50ms game tick,
  automation), `ui.h`/`ui.cpp` (HUD layout and drawing), `nvs_store`/`rtc_store`
  (persistence and offline-earnings glue).
- `test/` — one PlatformIO test suite per `lib/core/` module.
- `docs/superpowers/specs/`, `docs/superpowers/plans/` — design spec and implementation plan for this feature.
