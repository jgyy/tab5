# tab5
The Tab5 is a highly expandable, portable smart IoT terminal designed for developers, integrating a dual-core architecture and rich hardware resources. It is built around the ESP32-P4 SoC based on the RISC-V architecture, featuring 16MB Flash and 32MB PSRAM for high-performance application development.

Full hardware datasheet: `docs/Tab5.pdf`.

## Xianxia Idle Game

A cultivation-themed idle game running natively on the Tab5. Cultivate **Qi**, buy
passive **Cultivation Methods** (generators), and advance through seven **Cultivation
Realms** — Mortal Body, Qi Condensation, Foundation Establishment, Core Formation,
Nascent Soul, Soul Transformation, Void Refinement — each breakthrough spending the
next realm's Qi threshold.

The economy no longer has a screen of its own. A later revamp deleted the rotating-crystal
idle view and its touch-driven generator/breakthrough shop entirely — it was the buggiest,
most complex hand-rolled UI in this codebase, and the raycasting Secret Realm trial (below)
already autoplayed end-to-end without it. Qi/generators/realm breakthroughs still run
exactly as before, just invisibly now: they drive the Secret Realm trial's combat
difficulty and Qi rewards, and surface only as read-only stats — not a shop, nothing here
is tappable — in the app's stats panel.

The game plays itself, idle-game style: generators are auto-bought and realm
breakthroughs auto-triggered every 50ms tick (breakthrough is checked first, then one
purchase attempt per generator in unlock order) as Qi allows. There's no manual
tap-to-buy/tap-to-breakthrough anymore since there's no shop UI left to tap it on;
automation was already winning that race every time before the shop screen was removed,
so nothing playable was lost by deleting the losing path. Automated actions don't force
an immediate save (to spare NVS flash write endurance from very frequent writes) — the
existing 15-second periodic autosave covers them; brightness/volume taps (the only touch
controls left, see below) still save immediately.

The Secret Realm raycasting trial (below) is now the app's entire UI, autoplaying from
first boot with no unlock gate — there's no other screen left to gate it behind. It's a
hand-rolled M5GFX HUD (no LVGL), composited into offscreen `M5Canvas` sprites and pushed
to the display once per frame to avoid flicker. Despite the panel being physically
landscape, M5GFX reports the Tab5's display as 720x1280 (portrait logical coordinates) —
confirmed by reading the M5GFX source and a live serial print on real hardware — so the
single screen is laid out as a vertical stack: a thin status header at top, the raycast
viewport filling the top half of the remaining screen, and a read-only stats/settings
panel filling the bottom half (breakthrough progress, player/enemy HP, route progress,
then brightness/volume — the only remaining touch controls), computed at runtime from
`M5.Display.width()`/`.height()` rather than hardcoded. Held normally in landscape, this
reads correctly on the physical device. The header bar shows battery percentage and
charging state (a real reading from `M5.Power`) alongside the current realm name and
Qi/sec rate; there's deliberately no clock, since without Wi-Fi/NTP there'd be nothing to
keep it from silently drifting. Long realm names in the header and large Qi/sec and
offline-earnings figures are handled with measured `textWidth()`-based sizing and compact
`K`/`M`/`B` number formatting (e.g. `2.2M` instead of `2200000`).

Progress persists across power cycles via NVS (verified to survive real reboots, not
just a same-boot false positive), and the Tab5's battery-backed RTC grants offline
earnings on boot, computed with a custom epoch conversion (since `timegm()` isn't
available in this toolchain) checked against leap-year and century-boundary edge cases.

Design spec: `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-xianxia-idle-game.md`

### Secret Realm (raycasting trial mode)

The Secret Realm trial is no longer something you reach — it *is* the screen, running from
the moment the device boots, with no realm-based unlock gate. (There's nothing left to gate
it behind: the idle view it used to launch from via a HUD button is gone.) It's a small
fixed maze rendered with classic DDA raycasting (Wolfenstein/Doom-style: one ray per screen
column, textured walls, billboarded enemy sprites depth-tested against the wall raycast so
nearer walls correctly hide them) — raycasting was chosen over the old crystal's triangle
rasterizer because it's the right tool for a full-screen first-person view and sidesteps a
painter's-algorithm rasterizer's inability to correctly sort multiple independent
overlapping objects, a distinction that matters even more now that this is the only screen
in the app.

Like the rest of this game, the trial **autoplays**: the camera follows a scripted waypoint
route through the maze and auto-fights enemies it encounters (tick-based, deterministic, no
RNG), the same "the game plays itself" philosophy the old crystal and the generators already
used. Combat stats derive from cultivation progress (`playerMaxHP = 100 + 40 * realmIndex`,
`playerAttackDamage = 10 + 6 * realmIndex`), so a weak cultivator can genuinely lose to a
later enemy — on defeat, the trial resets to its start with full HP and no permanent penalty
(only time lost); clearing it grants a Qi reward back into the main economy and loops. Wall
textures and the attack/hit/victory sound effects are procedurally generated in code (a
deterministic hash-based pattern) — no imported image or audio assets, consistent with the
rest of this project.

Making this trial the app's only screen surfaced two real bugs while tracing its code for
that change. First, the camera used to snap instantly to a new heading in a single tick at
each of the maze's corners; on real hardware, at whatever FPS this renders at, an
instantaneous heading jump reads as a hard cut, not a turn. `tickTrial` now eases
`facingRadians` toward the desired heading at a fixed turn rate (`kTurnRateRadiansPerSec`,
180 deg/sec — about half a second for the maze's 90-degree corners), always turning the
shorter way around the ±π wrap boundary; movement toward the waypoint keeps stepping
straight ahead regardless of how far the camera has turned to face it yet. Second,
`restartTrial` used to re-derive player combat stats from whichever realm the trial had
first ever started at, frozen for the rest of the run — since the trial now runs unattended
indefinitely with no idle screen to re-enter from, that meant the character could never
visibly grow stronger no matter how long it ran. It now takes the caller's *live* realm
index (`gState.realmIndex`) on every restart instead, so player HP/attack keep pace with
cultivation progress across an indefinite autoplay session.

Design spec: `docs/superpowers/specs/2026-08-27-secret-realm-raycasting-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-secret-realm-raycasting.md`

The screen deletion, the new single-screen layout, and both fixes above are their own
follow-up revamp:
Design spec: `docs/superpowers/specs/2026-08-27-raycasting-only-revamp-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-raycasting-only-revamp.md`

**Known limitation — not yet validated on real hardware.** Unlike the old crystal's
`kRenderSize = 240`, which was empirically tuned against measured on-device FPS (Task 8 of
the original idle-game plan), no physical Tab5 was connected while this mode was built, so
its render resolution (240×320 internally) and display scale (`kTrialZoom = 2.5`) are a
conservative starting guess, not a benchmarked value — raycasting is much cheaper per pixel
than the old crystal's triangle rasterizer, but the actual achievable FPS at this scale is
unconfirmed. The raycasting-only revamp makes this *more* likely to need retuning, not less:
`kTrialZoom` was originally tuned to cover roughly the whole screen between the header and a
return-button strip, and the viewport is now deliberately half that height, to make room for
the stats panel below it. If it runs slower than expected on real hardware, `kTrialZoom` in
`src/trial_view.cpp` is the cheap knob to lower first (it only affects display scale, not
raycasting cost); `kTrialViewWidth`/`kTrialViewHeight` affect actual compute cost. The full
esp32p4_pioarduino build does compile and link successfully (verified in this environment),
but flashing and visually/FPS-testing it on the device is the next step for whoever has it.

### Settings: brightness & volume

Two compact rows at the bottom of the stats/settings panel — "Brightness" and "Volume" — each a
single tappable strip split into a "-" left half and "+" right half, stepping in `kSettingsStep`
(32) increments. Both apply immediately (`M5.Display.setBrightness()` /
`M5.Speaker.setVolume()`) and persist across reboots via the save file. Brightness is clamped
to a floor above zero (`kMinBrightness`) deliberately — a fully black screen has no way to see
the "+" button that would recover from it; volume has no such floor since 0 (mute) is a normal
setting.

The save format gained a schema v2 for this (adding `brightness`/`volume` fields to
`SaveData`), with a migration path: a save written before this change still loads with its
progress intact and fresh-game default brightness/volume filled in, rather than failing
validation and resetting everything.

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

Game logic (3D vector/matrix math, the idle-game economy, save serialization and its
v1->v2 migration, offline-earnings math, HUD hit-testing, DDA raycasting, the Secret
Realm's fixed map/route/enemies, its combat resolution, its autoplay orchestration, its
procedural wall textures, and brightness/volume clamping) is hardware-agnostic C++ under
`lib/core/`, unit-tested on the host machine — no device required. 78 test cases across 12
suites, all passing:

```bash
python3 -m platformio test -e native
```

`src/` (display setup, the game loop, HUD drawing, NVS/RTC glue) is Arduino/hardware
glue instead, and was validated on the physical Tab5 rather than in `native` tests — 12+
flashes across this project with zero panic/crash/watchdog signatures.

### Project Layout

- `lib/core/` — hardware-agnostic game logic, unit-tested via the `native` PlatformIO
  environment: `math3d` (vector/matrix math left over from the deleted crystal renderer;
  still unit-tested, no longer used by any production code), `economy`/`save`/
  `offline_earnings` (the idle-game loop and persistence, including the v1->v2 save
  migration), `raycast` (DDA raycasting core), `trial_map`/`trial_combat`/`trial_state`/
  `trial_textures` (the Secret Realm's fixed map, combat resolution, autoplay orchestration,
  and procedural wall textures), `settings` (brightness/volume clamping).
- `src/` — Arduino/M5Unified/M5GFX glue: `main.cpp` (setup/loop, the 50ms game tick,
  automation, and driving the always-on Secret Realm trial — there's no `ViewMode` switch
  anymore, just the one screen), `ui.h`/`ui.cpp` (header and stats/settings panel layout,
  drawing, and hit-testing for the brightness/volume rows), `trial_view.h`/`trial_view.cpp`
  (raycast rendering and SFX for the Secret Realm), `nvs_store`/`rtc_store` (persistence and
  offline-earnings glue).
- `test/` — one PlatformIO test suite per `lib/core/` module.
- `docs/superpowers/specs/`, `docs/superpowers/plans/` — design specs and implementation
  plans for the xianxia idle game, the Secret Realm trial mode, and the raycasting-only
  revamp that made it the app's only screen.
