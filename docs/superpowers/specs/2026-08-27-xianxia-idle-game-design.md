# Xianxia Idle Game for M5Stack Tab5 — Design Spec

## Overview

A single-player idle/incremental game running natively on the M5Stack Tab5
(ESP32-P4). The player cultivates **Qi**, buys passive **Cultivation
Methods** that generate it over time, and periodically taps **Attempt
Breakthrough** to advance through **Cultivation Realms**. The centerpiece of
the screen is a procedurally-grown, rotating low-poly crystal ("Dantian
Core") rendered with a custom software 3D rasterizer, which visually evolves
— more facets, a new color palette — at each realm.

## Hardware Context & Constraints

- **SoC**: ESP32-P4, dual-core RISC-V @ 360MHz + LP core @ 40MHz, hardware
  FPU, no GPU / no 3D pipeline. 32MB PSRAM, 16MB flash.
- **Display**: 5" IPS, 1280×720, MIPI-DSI, driven through M5GFX.
- **Touch**: capacitive, exposed through M5GFX/M5Unified.
- **RTC**: RX8130CE, battery-backed, keeps ticking while the device is off
  (via the main battery; a small supercap bridges brief battery-out
  periods). Used for offline-earnings, not for wall-clock display.
- **No hardware 3D**: any 3D rendering is software rasterization on the
  CPU. A full 1280×720 3D scene at interactive framerate is not realistic;
  the design renders 3D into a smaller offscreen buffer and lets M5GFX's
  sprite scale/push composite it onto the full-resolution panel, with sharp
  2D UI drawn natively on top.
- **Toolchain**: PlatformIO + Arduino framework, `pioarduino`
  `platform-espressif32` fork, M5Unified + M5GFX libraries — this exact
  combination is the one validated in the Tab5 vendor docs
  (`docs/Tab5.pdf`, PlatformIO section). No LVGL — the HUD is hand-rolled
  with M5GFX primitives to keep the dependency footprint small and give
  the game a bespoke look.
- **Connectivity**: the device is connected over USB and enumerates as
  `/dev/ttyACM0` (Espressif USB-JTAG/serial). PlatformIO is not yet
  installed on this machine and will be set up as part of implementation.

## Goals / Non-Goals

**Goals (v1):**
- A playable idle loop: passive Qi generation, purchasable generators,
  realm breakthroughs, persistent progress, RTC-based offline earnings.
- A rotating, procedurally-evolving 3D crystal centerpiece as the visual
  hook, running at a smooth framerate (target 20–30 FPS, tuned on
  hardware).
- Save survives power cycles.

**Non-goals (v1, explicitly deferred):**
- No Wi-Fi/NTP time sync — offline earnings only need *elapsed* RTC time
  between two readings, not correct wall-clock time, so this isn't needed
  for correctness.
- No tap-to-earn on the 3D model (confirmed: pure auto-idle; taps are for
  menus/buttons only).
- No prestige/rebirth system beyond the realm ladder itself.
- No multi-object 3D scene — one centerpiece mesh only.

## Architecture

### Layer composition (per frame)
1. **3D layer**: the crystal is rasterized into a small offscreen
   `M5Canvas` (starting size 320×320, to be tuned once real FPS is
   measurable on hardware), then scaled/pushed onto the main framebuffer
   via M5GFX's sprite push/scale API.
2. **2D HUD layer**: realm name, Qi counter, Qi/sec rate, generator buy
   list, and the breakthrough button, drawn directly with M5GFX
   text/shape primitives around the 3D viewport.

### Game loop
- Fixed-timestep simulation tick (e.g. 20Hz) drives economy math (Qi
  accrual, generator effects), decoupled from the render loop so a slow
  frame never desyncs the economy.
- Render loop runs as fast as the rasterizer allows, capped to the target
  FPS.

### Module breakdown
- `render3d/` — mesh struct, matrix/projection math, rasterizer
  (transform → project → cull → depth-sort → shade → fill).
- `crystal/` — procedural mesh generation and per-realm palette/growth
  function (base icosahedron + realm-driven vertex displacement).
- `game/` — economy state (currency, generators, realm), tick update,
  cost-curve math, breakthrough logic.
- `save/` — NVS-backed persistence, load/save, offline-earnings
  calculation.
- `ui/` — HUD drawing + touch hit-testing for buttons.
- `main.cpp` — board bring-up (M5Unified `begin()`), wiring the above
  together.

## Theme & Content Design

### Realms
Mortal Body (start) → Qi Condensation → Foundation Establishment → Core
Formation → Nascent Soul → Soul Transformation → Void Refinement (v1 cap;
more can be appended later without structural changes). Each realm has a Qi
threshold to unlock **Attempt Breakthrough**; thresholds scale roughly
10–15× per realm.

### Currency & Generators ("Cultivation Methods")
Standard idle-game exponential cost curve: `cost(n) = base_cost *
growth_rate^n` (growth_rate ≈ 1.12–1.15), where `n` is the number currently
owned. Starter set, unlocked progressively by realm:
1. Breathing Technique — available from the start, cheapest, lowest rate.
2. Spirit Herb Garden — unlocked at Qi Condensation.
3. Meditation Formation — unlocked at Foundation Establishment.
4. Disciple Cultivators — unlocked at Core Formation.
5. Spirit Vein Tap — unlocked at Nascent Soul.
6. Ancient Formation Array — unlocked at Soul Transformation.

### Breakthrough
A deliberate tap action (not automatic) once the Qi threshold for the next
realm is reached. On breakthrough: unlocks the next generator, applies a
modest passive-rate multiplier, and re-generates the centerpiece mesh/
palette for the new realm.

## 3D Rendering Design

### Mesh representation
A single base icosahedron (12 vertices, 20 faces) as the seed mesh, with an
optional one-time Loop-style subdivision for higher realms. Subdivision
level 0 (20 faces) for the earliest realms, level 1 (80 faces) once enough
performance headroom is confirmed on hardware; level 2 (320 faces) is a
stretch goal only if profiling allows — this ceiling is an explicit open
risk (see below), decided empirically rather than assumed.

### Procedural growth per realm
For each realm tier: displace every vertex outward along its own normalized
direction from the mesh center by a deterministic pseudo-random offset
(hashed from vertex index + realm index — no RNG state needed, so it's
reproducible), scaled by a jaggedness factor that increases with realm
tier, clamped to a max displacement to prevent self-intersecting geometry.
Pair this with a per-realm color: a base fill color that shifts across the
realm ladder (pale/clear → blue → green → gold → purple → crimson →
radiant white-gold), plus a rim-glow term (see shading below) whose
intensity also increases with realm, to sell the "growing cultivation aura"
feel. **This displacement/palette function is a good candidate for hands-on
implementation** — it's a creative/tuning decision (how jagged/ornate
should realm N look?), not boilerplate.

### Rasterization pipeline
Per frame: rotate mesh (slow constant auto-spin, representing "circulating
Qi") → transform vertices to camera space → perspective-project → backface
-cull (signed area or normal·view test) → depth-sort faces back-to-front by
average Z (painter's algorithm — sufficient for one small, roughly-convex
blob; no z-buffer needed) → shade each face with a fixed-direction Lambert
term (`dot(normal, light_dir)`, clamped ≥ 0) plus the rim-glow term
(`1 - dot(normal, view_dir)`, raised to a power, tinted by the realm's glow
color) → fill the triangle into the offscreen canvas.

### Performance strategy
All math in `float` — the ESP32-P4's RISC-V core has a hardware FPU, so
this is simpler to write and read than fixed-point and expected to be fast
enough at these triangle counts; fixed-point is only a fallback if on-device
profiling proves float is the bottleneck (not expected). Triangle budget
target: low hundreds at the highest realm tier.

## Persistence & Offline Earnings

### Save data
Stored via the Arduino `Preferences` (NVS) API as a small versioned struct:

```
struct SaveData {
  uint32_t magic;                       // format marker
  uint16_t version;                     // schema version, for migrations
  double   qi;                          // current currency
  uint32_t generatorCounts[NUM_GENERATORS];
  uint8_t  realmIndex;
  int64_t  lastSaveEpochSeconds;        // from RTC at time of save
  uint32_t checksum;
};
```

### Save triggers
Autosave every ~15s, plus immediately on generator purchase and on
breakthrough. Whether M5Unified exposes a pre-shutdown hook to force a
final save on the power-off button sequence is an open item to confirm
during implementation (see Open Risks) — periodic autosave is the
guaranteed fallback either way.

### Offline earnings calculation
On boot (when a valid prior save exists):
```
elapsed   = clamp(rtc_now - save.lastSaveEpochSeconds, 0, MAX_OFFLINE_SECONDS)  // cap e.g. 24h
offlineQi = elapsed * qiPerSecondAtSaveTime
qi       += offlineQi
```
Show a "While you cultivated in seclusion, you gained `{offlineQi}` Qi"
screen before entering the main view. If `rtc_now` is before the saved
timestamp (RTC unset or clock moved backward), `elapsed` clamps to 0 and no
bonus is granted — no crash, no negative Qi. On the very first boot ever
(no valid save), the offline screen is skipped entirely.

## UI/HUD Design

Three regions on the 1280×720 panel: a top strip with realm name, Qi
counter, and Qi/sec rate; the 3D viewport (scaled offscreen canvas)
occupying the center-left; a generator list (icon/name/owned/cost/effect,
each a tappable buy button) plus the breakthrough button (only
enabled/highlighted once its threshold is met) on the right or bottom.
Touch is handled via M5GFX/M5Unified's touch API with simple rectangular
hit-testing per button — no UI framework needed given the hand-rolled
approach.

## Error Handling

- Missing or checksum-invalid NVS save → fall back to fresh-game defaults
  rather than crashing.
- RTC read failure or a nonsensical (negative/huge) elapsed delta → skip
  the offline-earnings bonus for that boot rather than granting a bogus
  amount.
- Taps on an unaffordable generator or a not-yet-unlocked breakthrough
  simply no-op — no error dialogs needed for an idle game.

## Testing & Validation Plan

No simulator exists for this display/rasterizer/hardware combination, so
validation is on-device via the already-detected `/dev/ttyACM0`: flash,
watch serial output, confirm visually on the actual screen. The very first
implementation milestone is deliberately a minimal "hello triangle" spike —
bring up the display, rasterize one rotating flat-shaded shape in an
offscreen canvas scaled to the panel, and measure real framerate — because
the rendering approach is the highest-uncertainty part of this design and
should be de-risked before the full mesh/economy system is built on top of
it. Economy math (cost curves, offline-earning calculation) can be
sanity-checked as plain functions before wiring into the UI; consider a
compile-time debug flag that speeds up simulated time, to test long-run
progression without waiting in real time.

## Open Risks / Validation Needed

- Exact offscreen canvas resolution vs. achieved FPS trade-off — unmeasured
  until the "hello triangle" spike runs on real hardware.
- Whether M5Unified exposes a pre-shutdown hook to force a final save, or
  whether periodic autosave is the only mechanism available.
- Triangle budget ceiling (subdivision level 1 vs. 2) — decided by
  profiling, not assumed.
- Confirming M5Unified's RTC wrapper (`M5.Rtc` or equivalent) supports
  reading/writing epoch seconds as needed for the offline-earnings diff.

## Rough Milestones (for the implementation plan)

1. Project scaffold: PlatformIO env (per the vendor-provided config),
   board bring-up, display/touch init — flash a trivial program to confirm
   the toolchain and USB flashing work end-to-end.
2. Rendering spike: one rotating flat-shaded low-poly shape in an offscreen
   canvas scaled to the panel — validates the approach and tunes
   resolution/FPS.
3. Procedural crystal mesh + per-realm growth/palette function.
4. Game economy core (currency, generators, cost curve, tick loop),
   validated headless (serial-printed) before wiring to UI.
5. HUD/UI: currency display, generator list, buy interaction, breakthrough
   interaction.
6. Persistence: NVS save/load, autosave triggers.
7. RTC offline-earnings calculation + "welcome back" screen.
8. Polish pass: rim glow tuning, breakthrough transition, balance pass.
9. README update.
