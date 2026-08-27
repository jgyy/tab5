# Secret Realm — Raycasting Trial Ground — Design Spec

## Overview

A new, autoplaying "Secret Realm" mode for the xianxia idle game: a small
first-person raycasted dungeon (Wolfenstein/Doom-style DDA raycasting,
textured walls, billboarded enemy sprites) that a cultivator can enter from
the idle view once they reach Foundation Establishment. The camera
auto-navigates a fixed maze along a scripted route, auto-fights enemies it
encounters using combat stats derived from the player's cultivation
progress, and grants a Qi reward back into the main economy on each clear
before looping. This mirrors the existing game's core design philosophy —
"the game plays itself" — rather than introducing manual FPS controls
(movement/aim) as the primary interface.

This spec covers Phase 1 only. Combat/enemies are included per an explicit
scope decision (see Non-Goals); doors, elevators, secret passages,
projectiles-as-objects, multiplayer, a level editor, and manual movement as
the primary interface are not.

## Relationship to the Existing Crystal Renderer

This is **new, separate code**, not a rework of `math3d.h`/`rasterizer.cpp`.
The crystal's renderer is a generic 3D transform-and-triangle pipeline
(rotate → project → cull → depth-sort → shade → fill) suited to one small
object. Raycasting is a different, specialized technique for full-screen
first-person views: one ray per screen column walked through a 2D grid via
DDA (digital differential analysis), which sidesteps the "sort overlapping
objects" problem the existing painter's-algorithm rasterizer would have if
asked to render a full room (see the codebase-context discussion that
motivated this spec: painter's algorithm sorts individual faces, not whole
objects, and breaks down once independent meshes can visually overlap).
Both renderers will coexist, selected by which view mode is active.

## Hardware Capabilities Used

Confirmed from `docs/Tab5.pdf` and the vendored M5Unified/M5GFX sources:
- **Multi-touch**: `M5.Touch` exposes `getCount()`/`getDetail(index)` for
  more than one simultaneous touch point — not required by this spec
  (autoplay is primary), but available if manual tap-to-attack wants a
  concurrent gesture later.
- **BMI270 6-axis IMU** (accelerometer + gyroscope) — not used in Phase 1,
  noted as a possible future input for an optional look-around camera.
- **1W speaker + ES8388 codec**, `M5.Speaker` — used for Phase 1 SFX.
- **No GPU** — confirmed no 3D pipeline anywhere in the M5GFX panel/bus
  layer for this board; raycasting is pure CPU work, same constraint the
  crystal renderer already lives under.

## Goals / Non-Goals

**Goals (Phase 1):**
- A full-screen (or near-full-screen — exact coverage pending the
  performance spike below) raycasted 3D view: textured walls, floor/ceiling,
  billboarded enemy sprites correctly occluded by walls.
- Autoplay navigation through a small, fixed, hand-authored maze via a
  scripted waypoint route — no general pathfinding needed.
- Autoplay combat: player auto-attacks enemies encountered along the route;
  enemies auto-attack back. Player combat stats derive from cultivation
  progress (`realmIndex`), so a weak cultivator can genuinely lose and must
  grow the idle economy before retrying — this is the one place failure is
  real in this otherwise always-progresses idle game.
- A Qi reward on clearing the trial, looping automatically afterward.
- Basic SFX (attack/hit/victory) via simple procedural tones.
- Entry point gated behind Foundation Establishment (`realmIndex >= 2`).

**Non-goals (Phase 1, explicitly deferred):**
- Manual movement/aim as the primary interface (autoplay is primary per
  explicit decision; tap-to-attack override may be added later the same way
  tap-to-buy overrides generator automation today, but is not required for
  this phase).
- Doors, elevators, secret passages, inclined floors/ceilings, transparent
  walls, projectiles as world objects, multiplayer, a level editor,
  procedural map generation. All were in the reference cub3D-style brief
  that inspired this feature; none fit an autoplaying idle game.
- Persisting mid-trial progress across power cycles — the trial resets to
  its start whenever the mode is entered; only the Qi it grants persists
  (via the existing `qi` field).
- Imported bitmap texture assets (see Rendering Design's interpretation of
  "textured").

## Architecture

### Module breakdown (mirrors the existing lib/core convention: hardware-agnostic, unit-tested logic; thin Arduino glue in src/)
- `lib/core/raycast.h/.cpp` — DDA grid raycasting: ray-wall intersection,
  per-column wall distance/height, texture-coordinate calculation, a
  per-column depth buffer for sprite occlusion. New 2D-grid math, distinct
  from `math3d.h`'s 3D transform pipeline.
- `lib/core/trial_map.h/.cpp` — the fixed maze grid (wall/floor cell types),
  hand-authored, plus the scripted waypoint route and enemy spawn points.
- `lib/core/trial_combat.h/.cpp` — enemy HP/damage state, player HP/damage
  derived from `GameState.realmIndex`, tick-based auto-attack resolution,
  defeat/reward logic. Deterministic — no RNG, matching this codebase's
  existing preference (`hashJaggedness` is deterministic-pseudo-random for
  the same reason).
- `lib/core/trial_state.h/.cpp` — ties the above together: advances the
  player along the route each tick (or holds position during an active
  fight), tracks trial-clear/trial-fail transitions, exposes the Qi reward
  to apply back to `GameState.qi` on clear.
- `src/trial_view.h/.cpp` — Arduino glue: runs the raycaster into an
  offscreen buffer, blits it to the display, draws a slim trial HUD strip
  (enemy HP bar / progress), triggers SFX via `M5.Speaker`.
- `main.cpp` — a `ViewMode { Idle, TrialGround }` switch in `loop()`. The
  50ms economy tick, automation, and autosave continue running
  unconditionally regardless of which mode is rendered — only the render
  branch changes. This requires no change to existing tick/automation code.
- `ui.cpp`/`ui.h` — one new HUD button ("Enter Secret Realm"), gated and
  drawn the same way the breakthrough button already is.

### Performance de-risking (first implementation task)
Given the crystal already has **zero** measured FPS headroom at 240×240
with a 20-face mesh, the first task is a hardware spike: render a bare
single-room raycast (no enemies, no textures) at 2-3 candidate resolutions
on the real Tab5 and measure actual FPS, the same empirical approach that
locked in `kRenderSize = 240` for the crystal (Task 8). Screen coverage,
maze complexity, and texture resolution are decided from those numbers, not
assumed up front.

Also fix, as part of this work, `main.cpp`'s current per-pixel
`gCanvas->drawPixel()` blit loop — at a much larger raycast viewport this
would mean 900K+ individual calls per frame. Switch to
`M5Canvas::pushImage()` from a raw pixel buffer; this benefits the crystal
view too.

## Content & Level Design

A single small fixed maze (~10×10 grid), authored as a plain grid of cell
types (wall-type id or floor), in the same spirit as the crystal's
hand-authored base icosahedron. Illustrative sketch (not final):

```
##########
#..E.....#
#.##.##.##
#.#....#.#
#.#.##.#.#
#...E....#
##.####.##
#...#..G.#
#.E.#.####
##########
```
(`E` = enemy spawn, `G` = goal chamber, `#` = wall, `.` = floor)

A scripted, ordered waypoint list threads through the open floor cells past
each enemy spawn to the goal. The camera advances along it at constant
speed each tick, turning smoothly to face the next waypoint — deterministic
and directly unit-testable (given elapsed time, position is fully
predictable), the same spirit as the crystal's fixed `gRotation += 0.02f`
per frame.

## Rendering Design

### Wall rendering
Classic DDA raycasting: per screen column, step the ray through the grid to
the nearest wall hit, compute wall slice height from perpendicular
distance, draw a vertical textured strip.

**"Textured" interpretation — flagging this explicitly for review**: rather
than importing bitmap art (which this codebase has no pipeline for
anywhere — the crystal's visuals are 100% procedural, no asset files in
`lib/core/`), Phase 1 generates each wall type's texture procedurally at
startup — a small in-memory pattern (e.g. a stone-block or spirit-vein grid)
using the same deterministic-hash technique as `hashJaggedness`, sampled
via nearest-neighbor UV lookup per column. This satisfies "textured walls"
visually while staying consistent with the project's zero-external-asset
convention. **If you pictured actual imported pixel art, say so when you
review this** — that would need a real texture format/storage/conversion
pipeline this project doesn't have yet.

### Enemy rendering
Billboarded sprites (always face the camera), rendered after the wall pass,
each column's sprite pixels depth-tested against that column's wall
distance (the standard Doom/Wolfenstein sprite-occlusion technique) so
enemies correctly hide behind nearer walls.

### Floor/ceiling
Flat shaded (solid color per cultivation-realm palette, reusing `RGB` from
`color.h`), not texture-mapped, to keep Phase 1's per-pixel cost bounded
until the benchmark spike shows real headroom.

## Combat & Progression Tie-in

Combat stats derive from `GameState`, giving the trial genuine stakes tied
to idle-game progress rather than being an isolated deterministic sim:

- `playerMaxHP = 100 + 40 * realmIndex`
- `playerAttackDamage = 10 + 6 * realmIndex`
- Each enemy spawn has hand-authored HP/damage, tuned so early realms can
  clear the first encounter or two but plausibly lose deeper in the maze —
  the intended loop is "grow the idle economy, then come back stronger."
- Combat is tick-based and deterministic: player and enemy exchange damage
  on independent cooldowns until one reaches zero HP. No RNG.
- **On player defeat**: the trial resets to its start (HP restored, enemies
  respawned) with **no permanent penalty** — only time lost, consistent
  with this being a forgiving idle-game feature rather than a punishing
  roguelike. (Flagging this as a default worth confirming — an alternative
  would dock a small amount of Qi on failure.)
- **On clearing the trial** (all enemies defeated + goal reached): grant a
  Qi reward (proposed: a fraction of `REALM_QI_THRESHOLD[realmIndex + 1]`,
  scaling with progress the same way the rest of the economy does), show a
  brief "Secret Realm Cleared!" message (mirroring the existing "While you
  cultivated in seclusion..." boot screen), then loop back to the start
  automatically.

## Sound

Simple procedural tones via `M5.Speaker` (exact API shape to confirm during
implementation — flagged as an open risk below, same as the original
spec's RTC-wrapper uncertainty): a short blip on attack, a short low tone on
taking a hit, a brief ascending jingle on clearing the trial. No audio
assets/files — generated tones only, consistent with this project's
no-external-assets convention.

## UI/HUD Design

- Idle view: one new button, "Enter Secret Realm," styled and hit-tested
  like the existing breakthrough button, enabled only once
  `realmIndex >= 2`.
- Trial view: a slim top strip (enemy HP bar when in combat, otherwise
  route progress) plus a "Return to Cultivation" button to switch back to
  the idle view manually at any time — the economy keeps ticking underneath
  regardless of which view is showing.

## Testing & Validation Plan

Following this codebase's established split: pure logic in `lib/core/` is
unit-tested on the `native` PlatformIO environment, no device required.
- `raycast`: DDA ray-wall intersection distances against a known small grid
  with hand-computed expected hits.
- `trial_map`: waypoint route ordering/geometry sanity checks.
- `trial_combat`: HP/damage/defeat resolution, including the
  `realmIndex`-derived scaling formulas, and the no-RNG determinism
  property (same inputs → same outcome), mirroring `hashJaggedness`'s
  existing determinism test.
- `trial_state`: given elapsed time, waypoint-following position is exactly
  predictable; combat-pause-then-resume transitions are correct.
- Device-only (no `native` test, validated on real hardware per existing
  convention): the raycast column-fill loop's actual FPS, the `pushImage`
  blit, and SFX playback.

## Open Risks / Validation Needed

- Achievable FPS/resolution for a full-screen (or near-full-screen) raycast
  view is **unknown until the Task 1 hardware spike runs** — screen
  coverage and maze size may need to shrink from what's sketched here.
- The procedural-texture interpretation of "textured walls" may not match
  what was pictured — needs explicit confirmation on spec review.
- `M5.Speaker`'s exact tone-playback API needs confirming against the
  vendored M5Unified version pinned in `platformio.ini`.
- HP/damage numbers above are a starting point for on-device balance
  tuning, not final values.

## Rough Milestones (for the implementation plan)

1. Hardware spike: bare single-room raycast at 2-3 candidate resolutions,
   measure real FPS on the Tab5; fix the `pushImage` blit as part of this
   task.
2. `raycast` core (DDA, projection, per-column depth buffer) + unit tests.
3. `trial_map` (fixed maze grid + waypoint route) + unit tests.
4. Wall+floor/ceiling rendering wired into `src/trial_view.cpp`, validated
   on-device.
5. Procedural wall textures.
6. `trial_combat` + `trial_state` (autoplay navigation, combat resolution,
   defeat/retry, clear/reward loop) + unit tests.
7. Enemy sprite rendering + occlusion.
8. Mode-switch integration in `main.cpp`/`ui.cpp` (entry button, HUD strip,
   return button).
9. SFX.
10. Balance pass (HP/damage tuning) + README update.
