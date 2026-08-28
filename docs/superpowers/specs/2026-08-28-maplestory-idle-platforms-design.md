# MapleStory-Style Platforming Zone — Design Spec

## Overview

Yesterday's revamp (`2026-08-27-maplestory-idle-zone-design.md`) replaced the
raycasting Secret Realm trial with a flat, single-axis "zone": the character
auto-walks left-to-right across a static background, auto-fighting 3
monsters of increasing difficulty, on a portrait 720x1280 screen. That system
is now live (`zone_map`/`zone_state`/`zone_combat`/`zone_textures`/
`zone_view`) and this spec builds directly on it rather than replacing it.

This revamp pushes the same zone further toward an actual MapleStory idle
game: a true landscape orientation, a multi-platform terrain per zone instead
of one flat line, monsters that patrol their platform instead of standing
still, and a jump the character uses to cross between platforms. Per the
approved design direction:

- **Autoplay stays.** The character's AI decides when to walk, jump, and
  fight — no manual touch controls. Jumping is an automatic behavior, not a
  player action.
- **The display physically rotates to landscape** (1280x720 logical), not
  just "more of the same portrait view."
- **Enemies patrol in place** (back and forth within their own platform) and
  stop to fight when the character reaches them — no chasing, no
  pathfinding.
- **Each of the 16 realms gets a genuinely different-looking platform
  layout**, generated deterministically, not one shared template.

The cultivation economy, save format, combat resolution (`zone_combat`), and
the "whole zone visible at once, no scrolling camera" property are all
unchanged.

## Goals

- Rotate the display to landscape (1280x720) and adapt the existing
  header/viewport/panel layout to the new aspect ratio.
- Extend `ZoneMap` from one flat line to 4 platforms (1 ground + 3 elevated),
  with each elevated platform's height and horizontal gap chosen
  deterministically per `realmIndex` so all 16 realms look distinct, while
  guaranteeing every generated layout is reachable by a fixed jump.
- Give the character a jump: a short, scripted (not physics-simulated) arc
  from the edge of one platform to a fixed landing point on the next,
  triggered automatically when auto-walking reaches a platform's edge.
- Make monsters patrol back and forth within their platform's bounds instead
  of standing at a fixed point, freezing in place only while actually being
  fought.
- Keep the monster count (3), the just-retuned difficulty formula
  (`baseHp = 30 + 20*realmIndex`, `baseDamage = 8 + 3*realmIndex`, tier
  bonuses `{0,20,50}`/`{0,6,14}`), and `zone_combat` completely untouched —
  this revamp changes terrain and motion, not combat balance.

## Non-Goals

- No manual/touch control of the character, jump, or attacks — autoplay
  remains the only mode, per the approved design direction.
- No enemy chasing or pathfinding across platforms — patrol is confined to
  the enemy's own platform, exactly like today's "stands still" behavior
  except now it paces.
- No real gravity/velocity simulation or platform collision detection for
  the jump — see "Jump Mechanic" below for why a scripted arc is used
  instead, and why that's a deliberate simplification, not a cut corner.
- No change to `economy.{h,cpp}`, `save.{h,cpp}`, `offline_earnings.{h,cpp}`,
  `zone_combat.{h,cpp}`, `NUM_REALMS`, or the monster difficulty formula.
- No change to the "whole zone visible at once, no scrolling camera"
  property — screen-space mapping changes from 1D to 2D, but the entire
  generated arena for the current realm is still drawn in one frame.
- No on-device visual/FPS/touch confirmation from this environment — same
  caveat every prior spec here has carried. Native unit tests + a compile
  check are this environment's limit.

## Display: Landscape Rotation

`M5.Display.setRotation()` is called once during `setup()` (before
`initHud`/`initZoneView`, since both size their canvases off
`display.width()/.height()`) to switch the panel from its native portrait
orientation to landscape, giving `width() == 1280`, `height() == 720`.

`ui.h`'s layout is already computed at runtime from live
`display.width()/.height()` rather than hardcoded — `computeLayout()`,
`sceneViewportBottom()`, and every `*Rect()` helper carry over unchanged.
Verified by arithmetic: with `kHeaderHeight = 64` and the existing 50/50
viewport/panel split, the panel gets `(720-64)/2 = 328px`; the panel's own
content (`breakthroughY..volumeY` rows plus padding/gaps) sums to ~298px, so
the existing row heights still fit with margin — no row-height retuning
needed. The zone viewport itself becomes short and wide (1280x328-ish)
instead of tall and narrow, which is the correct silhouette for a
side-scrolling platformer and is accounted for in the platform-height budget
below.

Touch-coordinate mapping is an existing open, unconfirmed bug (brightness/
volume unresponsiveness, from yesterday's spec). Rotation changes the
coordinate space touch has to map into, so this becomes another variable in
that same investigation rather than a separate one — the diagnostic logging
already added to `main.cpp`'s touch handler stays in place and gets exercised
under the new rotation on the next hardware flash.

## Terrain: `Platform` and the extended `ZoneMap`

```cpp
struct Platform {
    float x0, x1;  // world-space horizontal extent, x0 < x1
    float y;       // height above the ground baseline, world units (0 = ground)
};

struct MonsterSpawn {
    float x;            // patrol-center x, world units (unchanged field)
    int platformIndex;  // NEW — which Platform this monster patrols on
    int maxHp;
    int damage;
};

struct ZoneMap {
    int realmIndex;
    std::vector<Platform> platforms;      // NEW — always 4: [0]=ground, [1..3]=elevated
    std::vector<MonsterSpawn> monsters;   // unchanged count (3), one per elevated platform
    float arenaWidth;                     // NEW — replaces the fixed kArenaWidth constant;
                                           // == platforms.back().x1
};
```

`kArenaWidth` (today a fixed `10.0f` constant) is retired in favor of this
per-map `arenaWidth` field, since realms now generate different total
lengths. Every place that reads `kArenaWidth` today (`zone_state.cpp`'s
Cleared check and skip-clamp, `zone_view.cpp`'s `screenXFor`) switches to
reading `state.map.arenaWidth` instead.

**Generation algorithm** (`makeZoneMap(realmIndex)`), always producing 4
platforms and 3 monsters:

1. Platform 0 (ground): `x0 = 0`, `y = 0`, width chosen deterministically
   from `hash(realmIndex, 0)` in `[2.5, 4.0]` world units — a short safe
   walk before the first jump, playing the same role the old arena's
   "walk to x=2.5 before the first monster" stretch did.
2. For elevated platforms `i = 1, 2, 3`: three independent values are drawn
   per platform by calling the existing two-int `hashValue(a, b)` with a
   distinct second-argument encoding per quantity — `hashValue(realmIndex,
   i*3 + 0)` for the gap-from-previous (mapped into `[0.5, kMaxJumpGap]`),
   `hashValue(realmIndex, i*3 + 1)` for width (mapped into `[1.5, 3.0]`),
   `hashValue(realmIndex, i*3 + 2)` for height-delta (mapped into
   `[-kMaxJumpRise, kMaxJumpRise]`), applied as `y_i = clamp(y_{i-1} + delta,
   0, kMaxPlatformHeight)` — the clamp (not just a bounded delta) is what
   keeps worst-case terrain height visually bounded regardless of how an
   unlucky hash sequence walks, independent of `realmIndex` magnitude.
3. One monster per elevated platform, in platform order (so difficulty tier
   0/1/2 still corresponds to encounter order): spawn `x` = that platform's
   midpoint, `platformIndex` = that platform's index, `maxHp`/`damage` from
   the **unchanged** existing formula.

Constants (tuned for the ~328px-tall landscape viewport derived above, at an
assumed ~40px/world-unit vertical scale leaving headroom for sprites and a
ground margin):
- `kMaxJumpGap = 2.5f` — largest horizontal gap a single jump can cross.
- `kMaxJumpRise = 1.8f` — largest height change a single jump can cross (up
  or down).
- `kMaxPlatformHeight = 4.0f` — hard ceiling on any platform's height,
  regardless of cumulative deltas.

Because every step's gap/rise is drawn from a bounded range by
construction, **every generated map is reachable by a single fixed jump
arc at every platform boundary** — this is an invariant to unit-test across
all 16 realms (and a wide sweep beyond, to catch any hash edge case), not
something verified by eyeballing 16 hand-tuned layouts.

`hash(...)` reuses the same integer-hash technique `zone_textures.cpp`
already uses for deterministic colors (no RNG state, same call always
returns the same value) — no new hashing approach is introduced.

## Jump Mechanic: a scripted arc, not simulated physics

The character never receives player input, so the outcome of every jump is
already known at the moment it's triggered (the destination platform,
guaranteed reachable by construction above). Simulating gravity/velocity
with per-tick collision detection would add real failure modes (tunneling
through a platform edge, landing a half-pixel short) purely to reproduce a
result that isn't actually in question. Instead, a jump is a closed-form
function of elapsed time — the same style this codebase already uses for
`tickCombat`'s cooldown math and `zone_textures`' hash-based colors.

```cpp
struct JumpArc {
    float fromX, fromY, toX, toY;
    float elapsed = 0.0f;
    float duration = 0.0f;  // seconds
};
```

- `duration = max(kMinJumpDuration, horizontalDistance / kWalkSpeedUnitsPerSec)`
  (the character keeps its normal walking horizontal speed during a jump);
  `kMinJumpDuration = 0.3f` so even a tiny gap still visibly reads as a hop.
- Position at time `t = clamp(elapsed/duration, 0, 1)`:
  `posX = lerp(fromX, toX, t)`; `posY = lerp(fromY, toY, t) + sin(π·t) *
  kJumpArcHeight`. The `sin(π·t)` term adds a cosmetic up-then-down hump on
  top of the straight-line height interpolation, so the arc looks like a
  jump whether the destination is higher, lower, or level with the start —
  one formula handles "jump up," "jump across," and "hop down" without a
  special case for each. `kJumpArcHeight = 0.6f` world units.
- Landing point on the destination platform is `x0 + kLandingMargin`
  (`kLandingMargin = 0.2f`), not the exact edge — avoids float-equality
  edge cases in the very next tick's "am I on this platform" checks.

This is exposed as a new `ZonePhase`, not an implicit flag layered onto
`Walking`, since this project's state machine already models modes as
explicit enum values:

```cpp
enum class ZonePhase { Walking, Jumping, Fighting, Cleared };
```

## Enemy Patrol

A monster's on-screen position becomes a pure function of *zone elapsed
walking time* rather than a fixed point — a triangle wave back and forth
within its own platform, clamped so it never reaches the platform's edges:

- `patrolRange = min(kMaxPatrolRange, platformWidth/2 - kPatrolMargin)`
  (`kMaxPatrolRange = 0.8f`, `kPatrolMargin = 0.3f`).
- Standard triangle wave of period `T = 4 * patrolRange / kPatrolSpeed`
  (`kPatrolSpeed = 0.6f` units/sec, slower than the player's walk so it
  doesn't look frantic), amplitude `patrolRange`, centered on the monster's
  spawn `x`.

`ZoneState` gains a `float walkingElapsedSeconds` accumulator, advanced only
while `phase == Walking` (frozen during `Jumping`, `Fighting`, and
`Cleared`) — patrol motion pauses the instant a monster becomes
`currentMonsterIndex` and is being fought, exactly like the character itself
already stops moving to fight today. `tickCombat` and everything in
`zone_combat` is untouched; patrol is purely a rendering-position concern
layered on top of the same defeated/undefeated bookkeeping that already
exists.

## `zone_state` Changes

```cpp
struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;
    float posY = 0.0f;                 // NEW — height above ground baseline
    int currentPlatformIndex = 0;      // NEW — which platform posX/posY sit on while Walking
    ZonePhase phase = ZonePhase::Walking;
    JumpArc jump;                      // NEW — only meaningful while phase == Jumping
    float walkingElapsedSeconds = 0.0f; // NEW — drives monster patrol position
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
};
```

`tickZone` logic, extending the existing Walking/Fighting/Cleared machine:

- **Walking**: exactly as today, but scoped to `currentPlatformIndex` — only
  monsters on that platform are candidates for
  `findUndefeatedMonsterInRange` (still a 1D x-distance check, since by
  definition everything relevant while Walking shares one platform's `y`).
  The existing skip-clamp (`nearestUndefeatedMonsterXAhead`, from the recent
  "clamp walk step against skipped monsters" fix) is preserved, now also
  clamping against the current platform's `x1` in addition to any
  undefeated monster ahead. Reaching `x1` with the platform's monster(s)
  defeated: if this is the last platform, same Cleared transition as today
  (using `map.arenaWidth` instead of `kArenaWidth`); otherwise, start a
  `JumpArc` toward the next platform and switch to `Jumping`.
- **Jumping** (new): advance `jump.elapsed`; while `elapsed < duration`,
  update `posX`/`posY` from the arc formula above (so `zone_view` never
  reimplements arc math — it just reads `state.posX/posY` like it already
  does today). At `elapsed >= duration`: snap to the landing point, set
  `currentPlatformIndex` to the destination, `phase = Walking`.
- **Fighting**: unchanged — still resolves via `tickCombat`, same
  monster-defeated/player-defeated transitions, same `restartZone` call on
  player defeat (which now also re-derives a fresh platform layout for
  whatever realm is current, same as it already re-derives the flat map
  today).
- **Cleared**: unchanged.

`restartZone` and `startZone` change only insofar as they now also
initialize `posY = 0`, `currentPlatformIndex = 0`, `walkingElapsedSeconds =
0`.

## `zone_textures` Addition

```cpp
// Ledge fill color for a realm's elevated platforms — distinct from both
// zoneSkyColor and zoneGroundColor, tinted by the same per-realm hue.
// Deterministic.
RGB platformColor(int realmIndex);
```

## `zone_view` Changes

Screen mapping becomes 2D:

- `screenXFor(worldX)` normalizes by `state.map.arenaWidth` (was the fixed
  `kArenaWidth`).
- `screenYFor(worldY)` maps `[0, kMaxPlatformHeight]` world units to
  `[groundY, groundY - reservedHeight]` screen pixels, where `reservedHeight`
  leaves headroom above the tallest possible platform for a monster sprite
  (radius up to 40px today) plus margin — concretely, `groundY` stays at
  `gViewportH * 0.85f` as today, and the usable rise budget above it is
  `groundY - kTopMargin` (`kTopMargin` chosen to keep the tallest platform's
  monster fully on-screen).

Per-frame drawing, extending today's background → monsters → character
order:
- Background (unchanged: sky/ground gradient from `zoneSkyColor`/
  `zoneGroundColor`).
- Each platform (new): a filled rect from `platformColor`, at its mapped
  screen rect — ground platform can be folded into the existing ground-band
  fill, elevated platforms draw as distinct ledges.
- Each undefeated monster: drawn at its **live patrol position**
  (`patrolX(state.walkingElapsedSeconds)` when not the current combatant, or
  its frozen spawn/engagement position when it is), on its platform's mapped
  screen `y` — otherwise the same `drawMonster` as today.
- Character: drawn at `screenXFor(state.posX)`, `screenYFor(state.posY)`;
  walk-cycle bob plays during `Walking`, a fixed "airborne" pose (e.g., legs
  tucked, no bob) during `Jumping`, the existing static "engaged" pose
  during `Fighting` — same function, one new pose branch.
- Attack/hit flash: unchanged, still keyed off `phase == Fighting`.

## `main.cpp` / `ui.cpp` Changes

- `setup()`: `M5.Display.setRotation(<landscape value>)` added before
  `initHud`/`initZoneView` — the exact rotation constant (0-3) needed to
  reach true landscape on the Tab5's panel is a hardware-verification detail
  for the implementation task, not guessed here (same spirit as this
  project's prior board-specific display/touch investigations).
- Everything else in `main.cpp`'s loop (economy tick, auto-buy,
  auto-breakthrough, autosave, SFX/flash triggers, HUD redraw throttling) is
  unchanged — `tickZone`'s signature doesn't change.
- `ui.cpp`/`ui.h`: no structural changes beyond what "Display: Landscape
  Rotation" above already covers (the layout math already adapts to
  whatever `width()/height()` report).

## Testing Plan

Native (`pio test -e native`), no device required:

- `test_zone_map`: extend/adapt for the new `Platform`/`arenaWidth` fields.
  New property tests swept across realms 0-15 (and beyond, to stress the
  hash): every generated map has exactly 4 platforms and 3 monsters; every
  consecutive platform pair's gap `<= kMaxJumpGap` and height delta
  `<= kMaxJumpRise` in magnitude (the reachability invariant); every
  platform's `y` is within `[0, kMaxPlatformHeight]`; each monster's
  `platformIndex` matches its encounter order; determinism (same
  `realmIndex` twice → identical map); distinctness (different realms
  produce different terrain — at least one differing height or gap between
  two sampled realms). Existing monster-stat assertions (30/8, 50/14, 80/22
  at realm 0, scaling formula) unchanged since the difficulty formula
  doesn't change.
- New pure-function tests for the jump arc formula: at `t=0` position equals
  `(fromX, fromY)`; at `t=1` (elapsed==duration) equals `(toX, toY)`; at
  `t=0.5` height is offset above the linear interpolation by `kJumpArcHeight`
  (within tolerance); duration respects `kMinJumpDuration` for a
  zero-distance edge case.
- New pure-function tests for patrol position: stays within
  `[spawnX - patrolRange, spawnX + patrolRange]` over a wide sweep of `t`;
  determinism; returns to exactly `spawnX` at `t=0`.
- `test_zone_state`: existing Walking/Fighting/Cleared cases adapted to the
  4-platform map; new cases — reaching a non-final platform's edge with its
  monster defeated enters `Jumping`; `Jumping` transitions to `Walking` on
  the correct destination platform once `elapsed >= duration`; patrol
  freezes (position doesn't change) while a monster is the current
  combatant; `restartZone` rebuilds both map and platform/position state for
  the current realm.
- `test_zone_textures`: add `platformColor` cases (deterministic, distinct
  from sky/ground, distinct across realms) alongside existing cases.
- `test_economy`, `test_save`, `test_settings`, `test_offline_earnings`,
  `test_hittest`, `test_smoke`, `test_zone_combat` are unaffected and must
  still pass unmodified.

Build-only check: `pio run -e esp32p4_pioarduino`. Flashing + visual/FPS/
touch/rotation verification remains a hardware follow-up, same bar as every
prior spec in this repo.

## Open Risks

- **Landscape rotation's exact `setRotation()` value and touch-coordinate
  behavior are unconfirmed until flashed** — this compounds with the
  already-open brightness/volume touch bug rather than resolving it in
  isolation.
- **The 328px-tall viewport budget is arithmetic, not a rendered
  screenshot** — `kMaxPlatformHeight`/vertical scale are chosen to fit that
  budget on paper; actual on-device appearance (sprite overlap, readability)
  is unconfirmed until flashed.
- **Per-realm arena length now varies** (roughly 7.5-19.5 world units
  depending on generated gaps/widths) since it's a sum of hashed platform
  widths and gaps rather than a fixed constant — walking-time-to-clear a
  zone will vary somewhat by realm. This doesn't affect combat difficulty or
  clearability (both untouched), only pacing, and isn't mitigated further
  here.
- **Jump arc constants (`kMaxJumpGap`, `kMaxJumpRise`, `kJumpArcHeight`,
  etc.) are first-pass estimates**, not simulated against a full 16-realm
  render — they're simple constants to retune later without touching any
  reachability logic, same posture this project has taken with every prior
  numeric-balance guess.
