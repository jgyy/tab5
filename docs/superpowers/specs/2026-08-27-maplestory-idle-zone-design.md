# MapleStory-Style Idle Zone — Design Spec

## Overview

The Secret Realm raycasting trial — the app's entire screen today — is a
hand-rolled DDA raycaster that has never run on real hardware, has a
history of camera bugs (an instant-snap turn was only just fixed), and is
a poor match for this device: the ESP32-P4 has no GPU and no float SIMD to
speak of, so per-column 3D wall projection is real per-pixel-column CPU
cost with nothing to accelerate it. This revamp deletes the entire
raycasting stack and replaces it with a 2D MapleStory-style idle combat
scene: a flat side-view "zone" per cultivation realm, where the character
auto-walks across a static background and auto-fights procedurally-drawn
monsters, exactly matching this project's existing "the game plays
itself" philosophy.

The cultivation economy (Qi, generators, realm breakthroughs) is
**unchanged** except that `NUM_REALMS` grows from 7 to 16 — more realms
means more distinct zones to see, which is the whole point of this
redo. Combat resolution (`CombatantState`/`tickCombat`) is reused
verbatim (renamed `zone_combat`) — it's pure HP/damage/cooldown math with
no dependency on 2D vs. 3D positioning.

Separately, and unrelated to raycasting-vs-2D, the brightness/volume tap
rows are reported as completely unresponsive on real hardware. This spec
also covers investigating and fixing that.

## Goals

- Delete `raycast`, `trial_map`, `trial_state`, `trial_textures` (lib/core)
  and `trial_view` (src) — the entire raycasting stack — and their test
  suites.
- Replace them with a 2D side-view "zone" scene: one flat arena per
  realm, the character walks left-to-right across it, auto-fighting up to
  3 monsters of increasing difficulty along the way, then the zone clears
  and loops (or the realm has since advanced and a *new* zone's monsters
  are waiting next time).
- No camera, no facing angle, no turning — the single largest source of
  the raycasting mode's "doesn't turn properly" complaint is a heading
  that has to be eased every frame. A flat 2D side view never has a
  heading to get wrong.
- All character/monster/background art is procedural (drawn with M5Canvas
  primitives / deterministic hash-based color functions), matching this
  project's existing zero-image-asset approach — no imported sprites, no
  copyright exposure from reproducing anyone's actual MapleStory art.
- Expand `NUM_REALMS` from 7 to 16, giving 16 visually distinct zones
  (background hue rotates per realm) with monster stats that scale
  identically to how player stats already scale with `realmIndex`.
- Diagnose and fix the brightness/volume touch-unresponsiveness bug.
- Keep all persistence (NVS save/load, RTC-based offline earnings,
  brightness/volume settings) working unchanged — this is a rendering
  and content swap, not a save-format change.

## Non-Goals

- No change to `economy.{h,cpp}`'s formulas (`costForGenerator`,
  `realmMultiplier`, `qiPerSecond`), `save.{h,cpp}`'s schema, or
  `offline_earnings.{h,cpp}` — only the two data tables
  (`REALM_NAMES`/`REALM_QI_THRESHOLD`) and the `NUM_REALMS` constant grow.
- No new generators — `realmMultiplier` (1.15^realmIndex) already scales
  `qiPerSecond` indefinitely across all 16 realms without needing more
  generator tiers.
- No manual/touch control of the character — autoplay remains the only
  mode.
- No on-device FPS/visual validation from this environment — no physical
  Tab5 attached here. Native unit tests + a compile check are this
  environment's limit, same caveat every prior spec in this repo has
  carried. 2D primitive fills are drastically cheaper than the raycaster's
  per-column projection was, so FPS risk is much lower than the mode
  being replaced, but "much lower" isn't "confirmed."
- `math3d.h` is untouched — already unused dead code from an earlier
  (pre-raycasting) revamp, unrelated to this change.

## Realm Expansion: 7 → 16

Nothing outside `economy.cpp`/`realms.h` hardcodes `7` — `test_economy`,
`test_save`, `main.cpp`, and `ui.cpp` all loop or index off `NUM_REALMS`
already, so this is a pure data extension with no logic changes.

`lib/core/realms.h`:
```cpp
constexpr int NUM_REALMS = 16; // was 7
```

`lib/core/economy.cpp` — 9 new realms continuing the existing table's
~12x-per-realm growth curve, using generic cultivation-genre terms (not
tied to any specific copyrighted work, consistent with the existing 7):

| # | Realm | Qi Threshold |
|---|-------|---------------|
| 0 | Mortal Body | 0 |
| 1 | Qi Condensation | 100 |
| 2 | Foundation Establishment | 1,200 |
| 3 | Core Formation | 15,000 |
| 4 | Nascent Soul | 180,000 |
| 5 | Soul Transformation | 2,200,000 |
| 6 | Void Refinement | 27,000,000 |
| 7 | Spirit Severing | 320,000,000 |
| 8 | Dao Seeking | 3,800,000,000 |
| 9 | Immortal Ascension | 46,000,000,000 |
| 10 | Earth Immortal | 560,000,000,000 |
| 11 | Heaven Immortal | 6,800,000,000,000 |
| 12 | Golden Immortal | 82,000,000,000,000 |
| 13 | Daluo Immortal | 1,000,000,000,000,000 |
| 14 | Saint Realm | 12,000,000,000,000,000 |
| 15 | Empyrean Realm | 150,000,000,000,000,000 |

Rows 7-15 are a reasonable continuation of the existing curve, not a
freshly-simulated balance pass — same spirit as the existing table, which
was itself hand-picked round numbers rather than a derived formula. All
values comfortably fit `double` (safe to ~1e308). `ui.cpp`'s comment
referencing "REALM_QI_THRESHOLD tops out at 27,000,000" must be updated
to the new top value.

## Deletions

Entire files removed:
- `lib/core/raycast.h`, `lib/core/raycast.cpp`
- `lib/core/trial_map.h`, `lib/core/trial_map.cpp`
- `lib/core/trial_state.h`, `lib/core/trial_state.cpp`
- `lib/core/trial_textures.h`, `lib/core/trial_textures.cpp`
- `src/trial_view.h`, `src/trial_view.cpp`
- `test/test_raycast/test_raycast.cpp`
- `test/test_trial_map/test_trial_map.cpp`
- `test/test_trial_state/test_trial_state.cpp`
- `test/test_trial_textures/test_trial_textures.cpp`

Renamed (content changes noted below, not a pure `mv`):
- `lib/core/trial_combat.{h,cpp}` → `lib/core/zone_combat.{h,cpp}`
  (unchanged content — `CombatantState`/`tickCombat`/`makePlayerCombatant`/
  `makeEnemyCombatant` carry over verbatim).
- `test/test_trial_combat/` → `test/test_zone_combat/` (unchanged content).

`lib/core/color.h` is kept — still needed by `zone_textures.h`.

## What's Kept As-Is

`economy.{h,cpp}` (aside from the two table extensions above),
`save.{h,cpp}`, `offline_earnings.{h,cpp}`, `nvs_store.{h,cpp}`,
`rtc_store.{h,cpp}`, `settings.{h,cpp}`, `hittest.h`. `main.cpp`'s
unconditional 50ms economy tick, auto-buy, auto-breakthrough, and 15s
autosave loop are unchanged.

## New Modules

### `lib/core/zone_map.h` / `.cpp`

```cpp
struct MonsterSpawn {
    float x;      // position along the zone's arena, world units [0, kArenaWidth)
    int maxHp;
    int damage;
};

constexpr float kArenaWidth = 10.0f;

struct ZoneMap {
    int realmIndex;                      // drives background palette in zone_textures
    std::vector<MonsterSpawn> monsters;  // encountered in array order, increasing difficulty
};

// 3 monster spawns evenly spaced across the arena (x = 2.5, 5.0, 7.5). Stats scale from
// realmIndex using the same additive growth terms makePlayerCombatant already uses (+40
// maxHp / +6 damage per realmIndex), plus a fixed per-tier bonus, so realmIndex == 0
// reproduces today's exact Secret Realm numbers (30/8, 50/14, 80/22):
//   baseHp(realmIndex)     = 30 + 40 * realmIndex
//   baseDamage(realmIndex) = 8  + 6  * realmIndex
//   tier bonuses (monster 0,1,2): hp += {0, 20, 50}, damage += {0, 6, 14}
// Deterministic - identical every call for the same realmIndex.
ZoneMap makeZoneMap(int realmIndex);
```

This preserves the existing design intent verbatim (a weak cultivator can
genuinely lose to a later monster in the same zone, and a stronger realm
also means a harder zone) while finally giving every realm its own
distinct monster stats instead of all realms sharing one fixed map.

### `lib/core/zone_combat.h` / `.cpp`

Renamed from `trial_combat`, byte-for-byte identical logic.

### `lib/core/zone_state.h` / `.cpp`

```cpp
enum class ZonePhase { Walking, Fighting, Cleared };

constexpr float kWalkSpeedUnitsPerSec = 1.5f;  // == old kTravelSpeed
constexpr float kEncounterDistance = 0.3f;     // == old kEncounterRadius

struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;                 // 0..kArenaWidth
    ZonePhase phase = ZonePhase::Walking;
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
};

ZoneState startZone(const ZoneMap& map, int realmIndex);
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);
void restartZone(ZoneState& state, int currentRealmIndex);
```

Direct translation of `trial_state`'s Traveling/Fighting/Cleared logic
onto one axis, with all facing/turn-rate math deleted:
- **Walking**: if an undefeated monster is within `kEncounterDistance` of
  `posX`, enter Fighting. Otherwise step `posX` toward `kArenaWidth` at
  `kWalkSpeedUnitsPerSec`; reaching the end with every monster defeated
  sets `Cleared` + `qiRewardPending`.
- **Fighting**: `tickCombat(player, enemy, dtSeconds)`; monster defeat
  returns to Walking, player defeat calls `restartZone`.

**Behavioral change from `restartTrial`, deliberate:** `restartTrial`
preserved `state.map` across a restart because there was only ever one
map. Now that every realm has its *own* zone, `restartZone` must rebuild
the map too, not just player stats:

```cpp
void restartZone(ZoneState& state, int currentRealmIndex) {
    state = startZone(makeZoneMap(currentRealmIndex), currentRealmIndex);
}
```

This is what makes the 16 zones actually visible during play: every
restart boundary (a clean zone clear, or a player-defeat reset) re-derives
*both* the background/monsters (from `currentRealmIndex`) and the player's
combat stats — so as cultivation progress advances through realms in the
background, the character keeps moving into new, tougher-looking zones,
not just getting invisibly stronger in the same one forever. Exactly like
the existing `restartTrial` fix, `currentRealmIndex` is only consulted at
these restart boundaries, so it can't change stats or swap the
background out from under the player mid-fight.

### `lib/core/zone_textures.h` / `.cpp`

```cpp
// Vertical-gradient background endpoints for a realm's zone. Hue rotates 22.5 degrees per
// realm (360/16) so all 16 zones are visually distinct; deterministic - same realmIndex
// always returns the same colors.
RGB zoneSkyColor(int realmIndex);
RGB zoneGroundColor(int realmIndex);

// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);
```

Same deterministic-hash technique `trial_textures.cpp` already used for
wall shading (an integer hash, no RNG state), just producing solid fill
colors for shapes instead of per-texel wall shading.

### `src/zone_view.h` / `.cpp` (replaces `trial_view`)

```cpp
void initZoneView(M5GFX& display);
void renderZoneView(M5GFX& display, const ZoneState& state);

// Short-lived visual flashes at the character/monster position, triggered from main.cpp
// alongside the existing SFX calls at the same combat events. Purely cosmetic timing state
// local to this file - not part of ZoneState, not unit-tested.
void triggerAttackFlash();
void triggerHitFlash();

void playAttackSfx();  // unchanged
void playHitSfx();     // unchanged
void playVictorySfx(); // unchanged
```

**Rendering approach — simpler than the raycaster in a second way, not
just "no camera":** the raycaster drew into a small internal pixel buffer
(240x320) and scaled it up via `pushRotateZoom` purely to keep per-pixel
compute affordable. 2D primitive fills (`fillRect`/`fillCircle`/
`fillTriangle`) are cheap enough that this scene draws straight onto a
canvas sized to the *actual* viewport (no internal low-res buffer, no
scaling step, no scaling blur on the pixel-art edges) and pushes with a
plain `pushSprite`.

Per frame: fill background (`zoneSkyColor`/`zoneGroundColor` gradient) →
ground band → each undefeated monster (position mapped from world space
`[0, kArenaWidth]` to screen space `[0, viewportWidth]`, size scaling with
`maxHp` so tougher monsters read as visibly bigger, color from
`monsterColor`) → character (fixed simple pixel-art humanoid: circle head
+ body/limb rects, 2-frame walk cycle alternated on a local elapsed-time
counter while Walking, static "engaged" pose while Fighting) → any active
attack/hit flash (a brief starburst at the target's screen position).
Character screen X is `(state.posX / kArenaWidth) * viewportWidth` — the
whole arena is visible at once, so there is nothing to scroll and no
camera to desync.

## Brightness/Volume Touch Bug

Reported as completely unresponsive on real hardware. This is orthogonal
to the raycasting-vs-2D decision — `hitTestHud`/`drawHud`'s row math in
`ui.cpp` doesn't change in this revamp beyond a possible "Route" →
"Monsters" relabel (below), so whatever's broken today stays broken
unless root-caused separately.

Reading `main.cpp`/`ui.cpp` now, the rects and touch dispatch *look*
internally consistent (`hitTestHud` and `drawHud` share the same
`gLayout`, computed once from the real `display.width()/.height()`; the
touch check runs unconditionally every `loop()` iteration off
`M5.Touch.getDetail().wasClicked()`). Nothing here explains a *complete*
lack of response from static reading alone — the likely fault is in how
raw touch-panel coordinates get mapped into the display's logical
720x1280 portrait space (a common M5Stack failure mode: the touch
controller reporting a different rotation than the display is configured
for). This needs to be root-caused during implementation, not guessed
here — the implementation plan should:
1. Add a temporary serial print of raw `touch.x`/`touch.y` alongside the
   computed row rects, to see on real hardware whether touches land where
   expected, are rotated/mirrored, or aren't detected as clicks at all.
2. Check M5Unified/M5GFX's actual touch-to-display coordinate mapping for
   the `esp32-p4-evboard` (Tab5) board definition specifically, since this
   project already found the display itself needed board-specific
   verification (the 720x1280-not-1280x720 discovery).
3. Fix whatever mismatch is found (likely a rotation/axis correction
   applied to raw touch coordinates before hit-testing).

This can only be *confirmed* fixed by flashing to real hardware, same
caveat as the raycasting mode's own unconfirmed FPS.

## `main.cpp` Changes

- `gTrialState` (`TrialState`) → `gZoneState` (`ZoneState`).
- `setup()`: `gZoneState = startZone(makeZoneMap(gState.realmIndex),
  gState.realmIndex);` replaces the old `startTrial(makeSecretRealmMap(),
  ...)` call.
- The Cleared-transition block: `renderTrialView`/`tickTrial`/
  `restartTrial` calls become `renderZoneView`/`tickZone`/`restartZone`;
  `triggerAttackFlash()`/`triggerHitFlash()` calls added alongside the
  existing `playAttackSfx()`/`playHitSfx()` calls (same hp-before/after
  comparison already computes when to fire them).
- Everything else (economy tick, auto-buy, auto-breakthrough, autosave,
  brightness/volume touch handling) is unchanged.

`ui.cpp`: `raycastViewportBottom` → renamed `sceneViewportBottom` (used by
both `ui.cpp` and `zone_view.cpp` to agree on the split, same reason as
today). The "Route N/M" bar is relabeled "Monsters N/3"
(`monstersDefeated` count vs. total) — clearer feedback for a combat scene
than a raw positional fraction, and a straightforward relabel, not a new
mechanic.

## Testing Plan

Native (`pio test -e native`), no device required:
- Delete `test_raycast`, `test_trial_map`, `test_trial_state`,
  `test_trial_textures`.
- Rename `test_trial_combat` → `test_zone_combat` (unchanged content).
- New `test_zone_map`: `makeZoneMap(0)` reproduces today's exact Secret
  Realm numbers (30/8, 50/14, 80/22); stats increase with `realmIndex`;
  monster x-positions are strictly increasing and all `< kArenaWidth`;
  deterministic (same call twice → same result).
- New `test_zone_state`, adapted from `test_trial_state`'s cases minus
  the turning tests (no facing angle to test) plus one new case:
  `test_restart_zone_rebuilds_map_for_current_realm` — start at realm 0,
  clear or defeat-reset with `currentRealmIndex = 4`, assert
  `state.map.realmIndex == 4` and monster stats match
  `makeZoneMap(4).monsters`, not the original realm-0 map.
- New `test_zone_textures`: same style as the deleted
  `test_trial_textures` — determinism (same inputs → same output), and
  distinctness (different `realmIndex`/`tierIndex` → different colors).
- `test_economy`: no changes needed (already loops off `NUM_REALMS`), but
  must still pass with `NUM_REALMS = 16` and the extended tables.
- `test_save`, `test_settings`, `test_offline_earnings`, `test_hittest`,
  `test_smoke` are unaffected and must still pass unmodified.

Build-only check: `pio run -e esp32p4_pioarduino` (compile/link against
the real target). Flashing + visual/FPS/touch verification remains a
follow-up for whoever has the physical device, same bar every prior spec
in this repo has used.

## Open Risks

- **Brightness/volume root cause is unconfirmed until flashed** — the
  investigation plan above is a best-effort hypothesis from static
  reading, not a verified diagnosis.
- **Zone render performance is unconfirmed until flashed** — expected to
  be comfortably cheaper than the raycaster (primitive fills vs. per-pixel
  projection) but not benchmarked.
- **9 new realm thresholds are an extrapolated curve, not simulated** —
  if the actual time-to-clear-realm-15 turns out unreasonable at real
  generator income rates, thresholds are simple constants to retune later
  without touching any logic.
