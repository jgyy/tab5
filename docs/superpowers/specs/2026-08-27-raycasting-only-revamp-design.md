# Raycasting-Only Revamp — Design Spec

## Overview

The idle-game screen (rotating crystal rasterizer + touch-driven
generator-shop/breakthrough HUD) is the buggiest, most complex hand-rolled
UI in this codebase, and the Secret Realm raycasting trial already
autoplays end-to-end. This revamp deletes the idle screen entirely and
makes the raycasting trial the app's only screen, shown from first boot,
forever. The cultivation economy (Qi, generators, realm breakthroughs)
keeps running exactly as it does today — it just loses its visible shop
UI and becomes a pure invisible driver of raycast difficulty and rewards.

This is a UI/integration revamp, not a new-feature spec: almost all of the
logic involved (`economy`, `save`, `offline_earnings`, `raycast`,
`trial_map`, `trial_combat`, `trial_state`, `trial_textures`) is existing,
tested code that keeps its current behavior. What changes is (a) what's
deleted, (b) how the screen is laid out, (c) two correctness fixes
surfaced while tracing the existing trial code for this work.

## Goals

- Delete the idle-game screen (crystal rasterizer, generator/breakthrough
  panel, "Enter Secret Realm"/"Return to Cultivation" buttons, the
  `ViewMode` switch) so the app has exactly one screen.
- The raycasting trial starts autoplaying immediately on boot, with no
  realm-based unlock gate (there is no other screen to gate it behind
  anymore).
- New single-screen layout: thin status header at top → raycast viewport
  filling the top half of the remaining screen → a bottom-half status
  panel (breakthrough progress, player/enemy HP, route progress,
  brightness/volume rows — the only remaining touch controls).
- Fix the camera to turn smoothly (interpolated) instead of snapping
  instantly to a new heading in one tick.
- Fix `restartTrial` to re-derive player combat stats from the *current*
  realm index on every restart, not the realm index frozen at the first
  ever `startTrial()` call — required for the character to visibly grow
  stronger over an indefinite autoplay session.
- Keep all persistence (NVS save/load, RTC-based offline earnings,
  brightness/volume) working unchanged.

## Non-Goals

- No changes to the economy's numbers/formulas, save format, or
  offline-earnings math.
- No new maze content, enemy balance changes, or additional raycast
  features (minimap, doors, etc.) — layout and the two fixes above only.
- No on-device FPS/visual validation — this environment has no physical
  Tab5 attached. Native unit tests and a compile check are the limit of
  what can be verified here; real-hardware verification is a follow-up
  for whoever has the device (same caveat the original Secret Realm spec
  already carried).
- No manual/touch navigation of the raycast view — autoplay remains the
  only mode, matching this project's existing "the game plays itself"
  philosophy.

## Deletions

Entire files removed (no longer referenced by anything once the idle
screen is gone):
- `lib/core/mesh.h`, `lib/core/mesh.cpp`
- `lib/core/rasterizer.h`, `lib/core/rasterizer.cpp`
- `lib/core/framebuffer.h`
- `test/test_mesh/test_mesh.cpp`
- `test/test_rasterizer/test_rasterizer.cpp`

`lib/core/color.h` is **kept** — it's a leaf header (defines `RGB`) also
used by `trial_textures.h` and `trial_view.cpp`'s wall shading, independent
of the rasterizer/framebuffer being removed.

Removed from `src/main.cpp`: the `ViewMode` enum and `gViewMode`, the
crystal rendering block (`gFramebuffer`, `gCanvas`, `gCrystalX/Y`,
`gRotation`, the per-pixel `drawPixel` blit loop, `refreshRealmVisual`'s
call site for display purposes), `gBaseMesh`/`gRealmVisual`, the
`HUD_BUTTON_ENTER_SECRET_REALM`/`HUD_BUTTON_RETURN_TO_CULTIVATION` touch
branches, and the `kSecretRealmUnlockRealmIndex` gate check.

Removed from `ui.h`/`ui.cpp`: `kRenderSize`, `kCrystalTopGap`,
`kCrystalBottomGap`, `kReturnButtonHeight`, `kSecretRealmUnlockRealmIndex`,
`computeLayout`'s generator/breakthrough/enter-realm geometry, `drawHud`'s
panel body (generator rows, breakthrough button, enter-realm button),
`enterSecretRealmRect`/`returnButtonRect`/`generatorRowRect`/
`breakthroughRect`, and the corresponding branches in `hitTestHud`. The
`HudButton` enum drops `HUD_BUTTON_BREAKTHROUGH`,
`HUD_BUTTON_ENTER_SECRET_REALM`, `HUD_BUTTON_RETURN_TO_CULTIVATION`, and
`HUD_BUTTON_GENERATOR_BASE`; only the brightness/volume button ids remain.

Removed from `trial_view.cpp`: the "Return to Cultivation" button draw
call and its `kReturnButtonHeight`-based rect math; `#include
"framebuffer.h"` is replaced with `#include "color.h"` (the only thing
that header actually needed).

## What's Kept As-Is

`economy.h/cpp` (Qi/generators/realms/breakthroughs), `save.h/cpp`
(NVS-backed persistence, v1→v2 migration), `offline_earnings.h/cpp`,
`nvs_store.h/cpp`, `rtc_store.h/cpp`, `settings.h/cpp`
(brightness/volume clamping), `hittest.h`, `trial_map.h/cpp`,
`trial_combat.h/cpp`, `trial_textures.h/cpp`, `raycast.h/cpp` — all
unchanged. `main.cpp`'s unconditional 50ms economy tick, auto-buy, and
15s autosave loop are unchanged; they already run independent of which
screen is showing today, so nothing about *that* needs to move.

## New Screen Layout

Approved layout (720×1280 portrait logical coordinates, matching the
existing convention — see `ui.h`'s existing comment on why):

```
+--------------------+
| Realm 3  Qi/s 12.4k| <- status header (kHeaderHeight, existing drawHeader() reused)
+--------------------+
|                    |
|    3D RAYCAST      |
|      VIEW           |
|   (top half of      |
|  remaining screen)  |
|                    |
+--------------------+
| [==Breakthrough==] |
| Player HP [======] |
| Enemy HP  [====--] |
| Route     [==----] |
| Brightness [-64+]  |
| Volume     [-96+]  |
+--------------------+
```

Shared layout constant/function (in `ui.h`, alongside the existing
`kHeaderHeight`, following this codebase's established pattern of sharing
layout math between the file that draws the panel and the file that draws
the viewport, so the two can never disagree about the split point):

```cpp
constexpr int kHeaderHeight = 64; // unchanged
// The y-coordinate where the raycast viewport ends and the stats/settings
// panel begins: the header plus half of whatever screen space remains.
// A function (not a constant) because it depends on the live display
// height, the same reason the old crystal-viewport math lived in
// computeLayout() rather than being a fixed constant.
int raycastViewportBottom(int screenH);
```

`trial_view.cpp` centers its scaled raycast push within
`[kHeaderHeight, raycastViewportBottom(screenH)]` (replacing today's
`[kHeaderHeight, screenH - kReturnButtonHeight]` range) — same centering
approach, new bounds, `kTrialZoom` re-tuned if needed so the view fills
its half without visually overflowing into the stats panel.

`ui.cpp`'s new bottom panel (replacing the deleted generator/breakthrough
panel) stacks, top to bottom, starting at `raycastViewportBottom(screenH)`:
1. Breakthrough progress bar (`state.qi` vs
   `REALM_QI_THRESHOLD[realmIndex + 1]`, read-only — no button, since
   breakthroughs are already fully automatic). Realm name and Qi/s stay in
   the top header via the existing `drawHeader`, so they aren't repeated
   here.
2. Player HP bar, Enemy HP bar (enemy bar shown empty/greyed when not
   currently fighting), Route progress bar — mirrors the data already
   drawn inside `trial_view.cpp`'s slim top strip today, surfaced here at
   larger size instead.
3. Brightness row, Volume row — unchanged behavior/hit-testing, just
   moved to this panel's bottom.

`hitTestHud` shrinks to only the brightness/volume row rects; every other
branch is deleted since nothing else on screen is tappable anymore.

## Camera Turning Fix

Root-caused via a native (no-hardware) repro: a standalone driver linking
`trial_state.cpp`/`trial_map.cpp`/`trial_combat.cpp` and stepping
`tickTrial` in a loop confirms `facingRadians` **does** reach the correct
heading at each of the maze's 3 corners (0 → 1.558 → 3.142 rad across the
clockwise loop) — but it jumps there in exactly one tick, with no
interpolation across frames. On real hardware at whatever FPS this
renders at, a same-tick heading jump reads as a hard cut, not a turn.

Fix, in `lib/core/trial_state.cpp`'s `tickTrial` (Traveling branch): compute
the desired heading via `atan2(dy, dx)` as today, then move
`state.facingRadians` toward it by at most
`kTurnRateRadiansPerSec * dtSeconds`, using shortest-signed-angle-difference
math so it always turns the short way around the ±π wrap boundary:

```cpp
constexpr float kTurnRateRadiansPerSec = 3.14159265f; // 180 deg/sec -> a 90 deg
                                                        // corner turn takes ~0.5s
float shortestAngleDiff(float from, float to); // wraps to [-pi, pi]
```

Movement (`posX`/`posY`) keeps stepping directly toward the waypoint every
tick, unaffected — only the *rendered* facing eases toward the travel
direction, so the fix can't change any existing position-based test
outcome. `startTrial`/`restartTrial` also initialize `facingRadians` to
the desired heading toward the first waypoint (`route[1]`), rather than
leaving it hardcoded at `0.0f`, so there's no jump-cut at trial start
either — today the default of `0.0f` happens to already match the first
segment's direction on this specific map (a coincidence, not something to
keep relying on).

## Live-Realm-Index Fix

`restartTrial` currently does:

```cpp
void restartTrial(TrialState& state) {
    TrialMap map = state.map;
    int realmIndex = state.realmIndexAtStart; // frozen at the very first startTrial() ever
    state = startTrial(map, realmIndex);
}
```

Since the idle screen is gone and this now runs unattended indefinitely,
the trial must re-derive player stats from the realm the hidden economy
has *actually* reached by the time of each restart, or the character can
never get stronger no matter how long it runs. Fix: thread the live realm
index in from the caller instead of reusing the frozen one:

```cpp
void tickTrial(TrialState& state, double dtSeconds, double proposedReward,
               int currentRealmIndex);
void restartTrial(TrialState& state, int currentRealmIndex);
```

`tickTrial` passes `currentRealmIndex` through to its internal
`restartTrial` call on player defeat; `main.cpp`'s post-Cleared restart
call passes `gState.realmIndex` directly. Player HP/attack are only ever
recomputed at a restart boundary (already a full-HP reset point today), so
this can't cause a stat change mid-fight.

## `main.cpp` Loop Changes

`loop()` drops the `if (gViewMode == ...) { ... } else { ... }` branch
entirely — there's only one branch left, unconditional:
1. Economy tick / auto-buy / auto-breakthrough / autosave (unchanged,
   already unconditional today).
2. Touch handling: only the brightness/volume row branches remain.
3. `tickTrial(gTrialState, dt, reward, gState.realmIndex)` +
   `renderTrialView(...)` every loop iteration (today's `else` branch,
   now the only branch) — the Cleared/reward/restart handling is
   unchanged except passing `gState.realmIndex` into the new
   `restartTrial` call.
4. `gTrialState` is initialized once in `setup()` (was previously
   deferred to first entry into `TrialGround` via `gTrialStarted`/lazy
   init — no longer needed since there's no other mode to have started
   from).

The boot sequence (offline-earnings splash, RTC seeding) is unchanged.

## Testing Plan

Native (`pio test -e native`), no device required:
- Delete `test_mesh`, `test_rasterizer`.
- Update `test_trial_state.cpp` for the new `tickTrial`/`restartTrial`
  signatures (pass a realm index through every existing call site).
- New test: after a tick that changes the desired heading, assert
  `facingRadians` has moved *partway* toward the target but is not yet
  equal to it (proves interpolation is actually happening, not an
  instant snap) — then assert it converges to the target within a bounded
  number of further ticks.
- New test: run a full clear-and-restart cycle passing realm index N,
  then restart again passing a *different* realm index M, and assert the
  post-restart player's `maxHp`/`attackDamage` match `makePlayerCombatant(M)`,
  not `makePlayerCombatant(N)`.
- All other existing suites (`test_economy`, `test_save`, `test_settings`,
  `test_offline_earnings`, `test_hittest`, `test_raycast`, `test_trial_map`,
  `test_trial_combat`, `test_trial_textures`, `test_smoke`) are unaffected
  and must still pass unmodified.

Build-only check: `pio run -e esp32p4_pioarduino` (compiles/links against
the real target; confirms nothing in `main.cpp`/`ui.cpp`/`trial_view.cpp`
broke, same as the original Secret Realm spec's "compiles successfully"
bar — flashing/visual/FPS verification stays a follow-up for real
hardware, as noted in Non-Goals).

## Open Risk

`kTrialZoom`'s current value (2.5, tuned for the old "between header and
return-button strip" region) will very likely need adjusting now that the
raycast viewport is deliberately half the previous available height —
this needs the same on-device empirical check the README already flags as
outstanding, not a guess made here.
