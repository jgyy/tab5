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
existing 15-second periodic autosave covers them; brightness/volume (no longer
tap-adjustable, see below) still persist across reboots via the save file.

The Secret Realm raycasting trial (below) is now the app's entire UI, autoplaying from
first boot with no unlock gate — there's no other screen left to gate it behind. It's a
hand-rolled M5GFX HUD (no LVGL), composited into offscreen `M5Canvas` sprites and pushed
to the display once per frame to avoid flicker. Despite the panel being physically
landscape, M5GFX reports the Tab5's display as 720x1280 (portrait logical coordinates) —
confirmed by reading the M5GFX source and a live serial print on real hardware — so the
single screen is laid out as a vertical stack: a thin status header at top, the raycast
viewport filling nearly all of the remaining screen, and a compact, fixed-height
read-only stats strip anchored to the bottom (breakthrough progress, player/enemy HP
sharing one row, route progress), computed at runtime from `M5.Display.width()`/`.height()`
rather than hardcoded. Held normally in landscape, this reads correctly on the physical
device. The header bar shows battery percentage and
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
its render resolution (240×320 internally) is a conservative starting guess, not a
benchmarked value — raycasting is much cheaper per pixel than the old crystal's triangle
rasterizer, but the actual achievable FPS at this resolution is unconfirmed. Viewport *fit*
is no longer part of this risk: the display scale used to be a fixed constant (`kTrialZoom`)
tuned for the old, much taller "header to return-button strip" region, and it silently
overflowed into the header and stats panel once the raycasting-only revamp halved the
viewport height. That constant is gone — `renderTrialView()` in `src/trial_view.cpp` now
computes the zoom live from the actual viewport (`raycastViewportBottom()`/`kHeaderHeight`),
so it can never overflow regardless of display size or future layout changes. If it runs
slower than expected on real hardware, `kTrialViewWidth`/`kTrialViewHeight` in
`src/trial_view.cpp` are the knobs that affect actual compute cost. The full
esp32p4_pioarduino build does compile and link successfully (verified in this environment),
but flashing and visually/FPS-testing it on the device is the next step for whoever has it.

### Character skills & graphics pass

The character has a growing, fully-automatic skill kit — 8 realm-gated skills
(`lib/core/skills.h`), one unlocking every 2 realms, firing round-robin among whatever's
currently unlocked while `Fighting`, dealing bonus damage on top of the untouched
`zone_combat` autoattack. No manual activation — this app has no touch controls left, and
skills don't add any.

Combat now shows floating damage numbers, a skill projectile that travels from the character
to the current enemy and bursts on impact (color-coded per skill), and a brief screen shake
reserved for skill impacts. The character has arms, a 4-frame walk cycle, a "casting" pose
synced to skill fires, and a per-realm aura ring. Monsters get tier-distinct silhouettes
(round/spiky/winged) instead of a uniform colored circle. The background now drifts with
parallax clouds/embers/stars (realm-dependent) and deterministic ground texture.

All of this is procedural M5Canvas drawing — no image or audio assets — consistent with the
rest of this project. New deterministic math (skill unlock/cycling, shake/rise/parallax
curves) lives in `lib/core/skills.{h,cpp}` and `lib/core/fx.{h,cpp}` and is unit-tested; the
drawing itself is hardware glue in `src/zone_view.cpp`, unvalidated on real hardware like
everything else in this project.

Design spec: `docs/superpowers/specs/2026-08-28-maplestory-skills-and-graphics-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-28-maplestory-skills-and-graphics.md`

### Settings: brightness & volume

Two full specs' worth of investigation never found a confirmed root cause for the brightness/
volume rows being unresponsive to touch on real hardware, so the interactive controls were
removed outright rather than continuing to chase it — this app now has **no touch controls at
all**. `gBrightness`/`gVolume` still load from and save to NVS, and still apply via
`M5.Display.setBrightness()`/`M5.Speaker.setVolume()` at boot exactly as before; they're simply
fixed for the session rather than player-adjustable. The freed panel space went to the zone
viewport.

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
v1->v2 migration, offline-earnings math, HUD hit-testing, the MapleStory-style zone's
terrain generation, jump arc, patrol motion, combat resolution, procedural textures, and
autoplay state machine, brightness/volume clamping, the character skill kit, and its FX
curves) is hardware-agnostic C++ under
`lib/core/`, unit-tested on the host machine — no device required. 131 test cases across 14
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
  migration), `zone_map`/`zone_combat`/`zone_state`/`zone_textures` (the MapleStory-style
  zone's terrain generation, combat resolution, autoplay state machine, and procedural
  colors), `settings` (brightness/volume clamping), `skills` (realm-gated automatic combat
  skills), `fx` (pure shake/damage-number/parallax curves for zone_view).
- `src/` — Arduino/M5Unified/M5GFX glue: `main.cpp` (setup/loop, the 50ms game tick,
  automation, and driving the always-on zone view — there's no `ViewMode` switch anymore,
  just the one screen), `ui.h`/`ui.cpp` (header and stats panel layout and drawing; no
  tappable controls remain), `zone_view.h`/`zone_view.cpp` (zone rendering, combat/skill FX,
  and SFX), `nvs_store`/`rtc_store` (persistence and offline-earnings glue).
- `test/` — one PlatformIO test suite per `lib/core/` module.
- `docs/superpowers/specs/`, `docs/superpowers/plans/` — design specs and implementation
  plans for the xianxia idle game, the Secret Realm trial mode, and the raycasting-only
  revamp that made it the app's only screen.
