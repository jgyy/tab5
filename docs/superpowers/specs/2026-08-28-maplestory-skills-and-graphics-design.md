# MapleStory Skills & Graphics Revamp — Design Spec

## Overview

Two independent changes, bundled because they land together:

1. **Remove the brightness/volume touch controls.** They're the only tappable
   elements left anywhere in the app, and they've been under active
   (unresolved) investigation as unresponsive on real hardware across the
   last two specs. Rather than keep debugging touch, this revamp deletes the
   feature outright — the game already plays itself end-to-end with zero
   input; this makes that literally true.
2. **Push the zone further toward an actual MapleStory idle game**: a
   realm-gated character skill kit (fully automatic, matching this game's
   "it plays itself" philosophy), and a graphics pass across combat effects,
   character animation, monster variety, and environmental dressing — all
   within this project's existing constraints (procedural M5Canvas
   primitives only, no image/audio assets, ~30fps render budget).

Builds directly on the current zone system (`zone_map`/`zone_state`/
`zone_combat`/`zone_textures`/`zone_view`) from
`2026-08-28-maplestory-idle-platforms-design.md`. That system's terrain,
jump mechanic, and patrol motion are unchanged by this revamp.

## Goals

- Delete `HUD_BUTTON_BRIGHTNESS_*`/`VOLUME_*`, `hitTestHud()`,
  `flashSettingsButton()`, `drawSettingsHalf()`, the settings row, and
  `main.cpp`'s touch-handling block. Device brightness/volume keep
  loading from and saving to NVS and get applied at boot exactly as today —
  they simply stop being player-adjustable. The reclaimed panel height goes
  to the zone viewport.
- Add a `lib/core/skills.h/.cpp` module: 8 realm-gated skills, fully
  automatic, firing in round-robin rotation among whatever's currently
  unlocked during `Fighting`, dealing bonus damage on top of the untouched
  `zone_combat` autoattack.
- Add combat FX: floating damage numbers, a skill projectile + impact burst,
  screen shake on skill impacts, tiered hit-spark sizes.
- Improve character rendering: arms, a 4-frame walk cycle, a "casting" pose,
  a per-realm aura ring.
- Give monsters tier-distinct silhouettes instead of a uniform colored
  circle.
- Add environmental dressing: parallax background elements and ground
  texture, both realm-flavored.
- Any new math with a defined right answer (skill cycling/unlock counting,
  shake/rise/parallax curves) lives in `lib/core` and is unit-tested; drawing
  itself stays in `src/`, unvalidated on real hardware like everything else
  here, with bounded per-frame element counts so worst-case draw cost stays
  predictable.

## Non-Goals

- No manual/touch control of anything, including skills — autoplay remains
  the only mode. This revamp *removes* the app's last touch controls; it
  does not add new ones.
- No job/class selection UI — one character, one growing skill kit, unlocked
  by realm exactly like generators already are (`unlockRealmIndex`).
- No changes to `zone_combat.h/.cpp`'s existing autoattack resolution,
  `zone_map`'s terrain generation, the jump mechanic, or patrol motion.
- No changes to the cultivation economy, save format, or offline-earnings
  math. `SaveData.brightness`/`volume` fields are unchanged — still
  persisted, still applied at boot, just not editable from the UI anymore.
- No true alpha blending for damage numbers/particles — M5Canvas has no
  real per-pixel alpha; "fade" effects are approximated with a hard cutoff
  after a fixed duration, not a color ramp, and the spec is explicit about
  this rather than implying real fading.
- No enemy skills — the skill kit is player-only, keeping this revamp
  scoped to "the character's growing arsenal," which is the MapleStory
  flavor being targeted.
- No on-device visual/FPS/touch confirmation from this environment — same
  caveat every prior spec here has carried.

## 1. Brightness/Volume Removal

Delete entirely:
- `ui.h`: the `HudButton` enum, `hitTestHud()`, `flashSettingsButton()`
  declarations.
- `ui.cpp`: `drawSettingsHalf()`, `settingsRowRect()`, `kSettingsRowHeight`,
  `gSettingsFlashButton`/`gSettingsFlashUntilMs`/`kSettingsFlashDurationMs`,
  `hitTestHud()`, `flashSettingsButton()`, the two `drawSettingsHalf(...)`
  calls in `drawHud()`, and `settingsY` from `Layout`.
- `main.cpp`: the entire `M5.Touch.getDetail()` block (the diagnostic
  `Serial.printf("[TOUCH] ...")` included — it existed specifically to debug
  this feature) and its `stateChanged`/`saveNow()`/forced-redraw tail.

`kPanelHeight` drops its trailing `+ kSectionGap + kSettingsRowHeight` term:

```cpp
constexpr int kPanelHeight = kPanelTopPad
    + kBreakthroughBarHeight + kSectionGap
    + kHpBarHeight + kSectionGap
    + kRouteBarHeight + kPanelTopPad;
```

This alone grows the zone viewport by 54px (`kSectionGap + kSettingsRowHeight`
= 6 + 48), since `sceneViewportBottom()`/`initZoneView()` already derive the
viewport height from `screenH - kPanelHeight`  — no other layout code
changes.

`drawHud()`'s `uint8_t brightness, uint8_t volume` parameters are dropped —
they exist today solely to feed `drawSettingsHalf()`, which no longer
exists, so keeping them would leave two unused parameters. New signature:
`void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone);`.
Both of `main.cpp`'s call sites (the throttled periodic redraw and the
forced redraw right before the "Cleared!" pause) drop the trailing
`gBrightness, gVolume` arguments to match.

Kept unchanged: `gBrightness`/`gVolume` globals, `settings.h/.cpp`
(`clampBrightness`/`clampVolume`), the boot-time load-clamp-apply sequence in
`setup()` (lines ~110-113 of today's `main.cpp`), and `SaveData`'s
`brightness`/`volume` fields including their save/load path. `Rect`/
`rectContains` (`hittest.h`) stay — `breakthroughRect()`/`hpRowRect()`/
`routeRect()` still use them for layout, just no longer for hit-testing.

## 2. Skill System (`lib/core/skills.h/.cpp`, new module)

```cpp
enum class SkillVisual { Slash, Fireball, FrostShard, LightningBolt, VoidSpike, PhoenixNova, Earthquake, Starfall };

struct SkillDef {
    const char* name;
    int unlockRealmIndex;   // player's kit includes every SKILLS[i] with unlockRealmIndex <= realmIndex
    float cooldownSeconds;
    float damageMultiplier; // bonus damage = player.attackDamage * damageMultiplier
    SkillVisual visual;
};

constexpr int NUM_SKILLS = 8;
extern const SkillDef SKILLS[NUM_SKILLS];

struct SkillState {
    float timer = 0.0f;   // counts up toward the currently-cycled skill's cooldown
    int cycleIndex = 0;   // round-robin cursor among currently-unlocked skills
};

// Count of SKILLS[i] with unlockRealmIndex <= realmIndex. Always >= 1 once realmIndex >= 0,
// since SKILLS[0].unlockRealmIndex == 0 (mirrors generator 0 always being owned from the start).
int countUnlockedSkills(int realmIndex);

// Advances state.timer by dtSeconds against the cooldown of SKILLS[state.cycleIndex]. Below
// cooldown: returns -1, state unchanged except the timer. At/above cooldown: resets timer to 0,
// advances cycleIndex to the next unlocked skill (wrapping via modulo the current unlocked
// count — NOT a fixed NUM_SKILLS, so a just-unlocked skill folds into the rotation without a
// discontinuity), and returns the index of the skill that just fired.
int tickSkill(SkillState& state, double dtSeconds, int realmIndex);
```

Skill table (cooldown and multiplier both climb monotonically — early skills
fire often for a small bonus, late skills are rare but hit hard, so the
rotation never trivializes the existing HP-scaling combat balance):

| # | Name               | Unlock realm | Cooldown | Multiplier | Visual        |
|---|---------------------|:---:|:---:|:---:|---------------|
| 0 | Sword Qi Slash      | 0   | 3.0s | 1.5x | Slash         |
| 1 | Flame Palm          | 2   | 3.5s | 1.8x | Fireball      |
| 2 | Frost Needle        | 4   | 4.0s | 2.2x | FrostShard    |
| 3 | Thunderclap Fist    | 6   | 4.5s | 2.6x | LightningBolt |
| 4 | Void Piercer        | 8   | 5.0s | 3.0x | VoidSpike     |
| 5 | Phoenix Nova        | 10  | 5.5s | 3.4x | PhoenixNova   |
| 6 | Earthquake Palm     | 12  | 6.0s | 3.8x | Earthquake    |
| 7 | Starfall Judgment   | 14  | 6.5s | 4.2x | Starfall      |

`countUnlockedSkills` scans all 8 entries rather than assuming the table
stays sorted by `unlockRealmIndex` — cheap at this size, and doesn't silently
break if a future edit reorders the table.

### `zone_state` integration

`ZoneState` gains:

```cpp
SkillState skill;
int skillFiredThisTick = -1; // which SKILLS[] index fired on the most recent tickZone() call, or -1
```

Both default-construct correctly with no explicit `startZone()`/`restartZone()`
changes needed (`SkillState`'s own default member initializers already zero
it).

`tickZone()` resets `skillFiredThisTick = -1` as its very first statement
(before the `Cleared` early-return and the phase branch), since it's a
one-tick signal a caller inspects immediately after each call — same
reasoning as this codebase's existing before/after HP diffing, generalized
into a state field instead of requiring the caller to diff. Inside the
`Fighting` branch, after the existing `tickCombat(...)` call:

```cpp
// Fighting
tickCombat(state.player, state.enemy, dtSeconds);
int fired = tickSkill(state.skill, dtSeconds, currentRealmIndex);
if (fired >= 0) {
    state.skillFiredThisTick = fired;
    int dmg = static_cast<int>(state.player.attackDamage * SKILLS[fired].damageMultiplier);
    state.enemy.hp -= dmg;
    if (state.enemy.hp < 0) state.enemy.hp = 0;
}
if (isDefeated(state.enemy)) { ... } // unchanged from here down
```

`state.skill.timer` only advances while `Fighting` (this function is only
called from that branch), so cooldowns freeze during `Walking`/`Jumping`
exactly like `walkingElapsedSeconds` already freezes outside `Walking` —
consistent with the rest of this state machine. It is **not** reset between
encounters (only `startZone`/`restartZone` reset it), so the skill rotation
carries its rhythm across consecutive monsters in the same run.

If a skill and the base autoattack both land in the same `tickZone()` call
(possible with a large `dtSeconds`), their damage is applied together and
`main.cpp` sees one combined HP delta — matching this codebase's existing
tolerance for that kind of tick-level imprecision (`zone_combat.h` already
documents player+enemy attacks landing in the same call).

## 3. Combat FX (`zone_view.h/.cpp`, plus new `lib/core/fx.h/.cpp`)

New pure-math module, `lib/core/fx.h/.cpp` (no M5GFX dependency, unit-tested):

```cpp
// Decaying oscillating offset in pixels for t in [0,1] (elapsed/duration since a triggering
// event). 0 at t=0 and t=1; bounded within +-amplitudePx in between. phaseRadians lets X/Y
// axes use the same t without moving in lockstep.
float shakeOffset(float t, float amplitudePx, float phaseRadians);

// Upward pixel offset for a floating damage number at elapsedSeconds since spawn - constant
// speed, no physics. Callers add this (already negative) to the number's y each frame.
float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond);

// Wraps a drifting background element's x position at elapsedSeconds, drifting from seedX at
// pxPerSecond, into [0, viewportW). fmod-based so it wraps with no jump at the boundary.
float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW);
```

### Why no position needs to be threaded through the trigger calls

During `Fighting`, both the character's screen position and the current
monster's screen position are static (the character doesn't move, and the
render loop already draws the current monster at its frozen `spawn.x`, not
its patrol position) — this is the same property the *existing*
`triggerAttackFlash()`/`triggerHitFlash()` already rely on: they take no
position arguments today, and `drawFlash()` simply re-derives `charX`/`mx`
at render time. All new FX below follow that exact precedent: trigger
functions take only the minimal semantic payload (amount, skill index),
never coordinates, and `zone_view.cpp` re-derives position live from
`ZoneState` each frame.

### `zone_view.h` additions

```cpp
// Spawns a floating combat-text number, drawn rising from the player's or the current enemy's
// position over the next ~700ms then removed outright (no fade - see spec's alpha-blending
// non-goal). skillIndex is the SKILLS[] index if this damage came from a skill (colors/sizes
// the number accordingly), or -1 for a plain autoattack hit.
void spawnDamageNumber(bool onPlayer, int amount, int skillIndex);

// Starts a skill's projectile-travel-then-impact-burst animation (from the character to the
// current enemy) and a short screen shake. Call once per fired skill, immediately after
// tickZone() reports one via ZoneState::skillFiredThisTick - do not wait for the next throttled
// render, for the same reason triggerAttackFlash()/triggerHitFlash() are called eagerly today.
void triggerSkillFx(int skillIndex);

// Procedural cast chime, pitch scaled by skillIndex so later (stronger) skills sound more
// dramatic - distinct from the existing single-tone playAttackSfx()/playHitSfx().
void playSkillSfx(int skillIndex);
```

### `zone_view.cpp` internals

- `ActiveSkillFx { bool active; int skillIndex; uint32_t startMs; }` — one
  slot (skills don't overlap in practice: the fastest cooldown is 3.0s and
  the full travel+impact animation is well under a second). `triggerSkillFx`
  overwrites it.
- `kSkillTravelMs = 220`, `kSkillImpactMs = 160`. While
  `elapsed < kSkillTravelMs`: draw a small shape (color/form keyed off
  `SKILLS[skillIndex].visual`) lerped from the character's screen position to
  the current enemy's. While `kSkillTravelMs <= elapsed < total`: draw an
  impact burst at the enemy's position. Past total: inactive.
- Screen shake rides the same trigger window: `kShakeDurationMs = 140`,
  `kShakeAmplitudePx = 5.0f`. Final `canvas.pushSprite(...)` call in
  `renderZoneView()` becomes
  `canvas.pushSprite(shakeOffset(t, kShakeAmplitudePx, 0.0f), kHeaderHeight + shakeOffset(t, kShakeAmplitudePx, kPi/2))`
  (90°-phase-shifted per axis so it reads as jitter, not a single diagonal
  kick) while a skill FX is active and `elapsed < kShakeDurationMs`, `(0, kHeaderHeight)`
  otherwise. Reserved for skill impacts only — basic hits keep today's
  flash-only feedback, so shake stays a "this was the big one" cue.
- `DamageNumber { bool active; bool onPlayer; int amount; int skillIndex; uint32_t spawnMs; }`,
  fixed array `kMaxDamageNumbers = 8` (linear-scan free-slot allocation,
  overwrite-oldest if full — this project already uses small fixed arrays
  like this for map data, no need for dynamic allocation at this scale).
  `kDamageNumberDurationMs = 700`, `kDamageNumberRisePxPerSec = 40.0f`. Drawn
  at render time via `damageNumberRiseOffsetPx`, at the player's or current
  enemy's live screen position; `skillIndex >= 0` draws bigger and in the
  skill's color, plain hits draw smaller in white — visually distinguishing
  "that was a skill" from "that was the autoattack," same intent as the
  varied hit-spark size below.
- Hit sparks: existing `drawFlash()` gains a size/color parameter — basic
  hits keep today's small yellow spark, skill impacts get a bigger burst
  colored per `SkillVisual`.

`main.cpp`'s existing post-`tickZone()` block extends (unchanged lines kept
for context):

```cpp
bool enemyHit = wasFighting && gZoneState.enemy.hp < enemyHpBefore;
bool skillFired = wasFighting && gZoneState.skillFiredThisTick >= 0;
if (enemyHit) {
    playAttackSfx();
    triggerAttackFlash();
    spawnDamageNumber(false, enemyHpBefore - gZoneState.enemy.hp, skillFired ? gZoneState.skillFiredThisTick : -1);
}
if (skillFired) {
    triggerSkillFx(gZoneState.skillFiredThisTick);
    playSkillSfx(gZoneState.skillFiredThisTick);
}
if (wasFighting && gZoneState.player.hp < playerHpBefore) {
    playHitSfx();
    triggerHitFlash();
    spawnDamageNumber(true, playerHpBefore - gZoneState.player.hp, -1);
}
```

## 4. Character Animation (`zone_view.cpp`)

`drawCharacter()` gains:
- **Arms**: two short rects/lines from the shoulders, angled down during
  `Walking`/idle, raised during the new casting pose (below).
- **4-frame walk cycle** (was 2-frame): bob height cycles through
  `{0, 1, 2, 1}` px indexed by `(nowMs / 100) % 4` while `Walking`, replacing
  the current `(nowMs / 150) % 2 == 0 ? 0 : 2` two-step — smoother motion at
  a similar cadence.
- **Casting pose**: while a skill FX is active
  (`gSkillFx.active && elapsed < kCastingPoseMs`, `kCastingPoseMs = 200`) and
  `phase == Fighting`, arms raise overhead and legs hold still, overriding
  the normal `Fighting` static pose for that brief window — read from
  `zone_view.cpp`'s own module-static FX state (already latched by
  `triggerSkillFx`), not from `ZoneState`, for the same throttled-render
  reason `skillFiredThisTick` can't be read directly at render time (see
  §2/§3).
- **Per-realm aura ring**: `canvas.drawCircle(screenX, bodyCenterY, kAuraRadius, auraColor565)`
  every frame, color from a new `zone_textures` function:

```cpp
// Faint ring color around the character, reusing the zone's own per-realm hue (not an
// arbitrary rainbow) with saturation climbing from 0.5 at realm 0 to 0.8 at realm 15 - a
// subtle "aura strengthens with cultivation" progression. Deterministic.
RGB characterAuraColor(int realmIndex);
```

## 5. Monster Variety (`zone_view.cpp`)

`drawMonster()` branches on `tierIndex` (0/1/2, already passed in via the
existing per-monster loop) for silhouette shape, keeping the existing
`monsterColor(realmIndex, tierIndex)` for fill and the existing eyes/
current-ring treatment layered on top of whichever shape:

- **Tier 0** (unchanged): today's filled circle.
- **Tier 1**: a diamond body (`fillTriangle` x2, or a rotated-square
  approximation) with 3-4 small triangular spikes around the rim —
  reads sharper/angrier than tier 0 at a glance.
- **Tier 2**: the existing circle body, enlarged, plus two small triangular
  "wings" or "horns" (`fillTriangle`) on either side — reads as the biggest,
  most elaborate silhouette, matching "toughest monster on the platform."

## 6. Environment Richness (`zone_view.cpp`)

- **Parallax elements**: a fixed array of ~6 elements
  `{ float seedX; float speedPxPerSec; }`, (re)seeded via `hashRange` only
  when `realmIndex` changes (mirrors the existing `gLastBackgroundRealm`
  cache — cheap, not recomputed every frame). Drawn behind platforms, in
  front of the sky/ground fill, positioned via `parallaxWrapX`. Shape/color
  tier by realm: realms 0-5 draw flattened-oval "clouds" in a light tint of
  the sky color; realms 6-11 draw small warm "ember" dots; realms 12-15 draw
  tiny cross-shaped "stars" — three visually distinct bands across the 16
  realms, all reusing the existing per-realm hue math rather than
  introducing an unrelated palette.
- **Ground texture**: a handful of short deterministic tick-marks/tufts
  along the ground band, x-positions from `hashRange(realmIndex, i, 0, viewportW)`
  — reuses the existing `hash.h` utility directly in `zone_view.cpp` (no new
  `lib/core` surface needed; this is decoration, not something with a
  correctness property worth unit-testing, unlike the placement-bounds math
  elsewhere in this project).

## Testing Plan

Native (`pio test -e native`), no device required:

- **New `test_skills`**: `countUnlockedSkills` at realm 0 (=1), realm 7
  (=4), realm 15 (=8); table sanity (cooldown and multiplier both strictly
  increase index-over-index, `SKILLS[0].unlockRealmIndex == 0`); `tickSkill`
  returns -1 before cooldown, fires at/after cooldown and resets timer;
  round-robin only cycles through currently-unlocked indices (fires skill 0
  repeatedly at realm 0 since only one is unlocked; at realm 4, cycles
  0,1,2,0,1,2,...); determinism.
- **New `test_fx`**: `shakeOffset(0,...) == 0` and `shakeOffset(1,...) == 0`,
  bounded within `amplitudePx` for `t` in `(0,1)`; `damageNumberRiseOffsetPx`
  is `0` at `elapsedSeconds == 0` and strictly more negative as it grows;
  `parallaxWrapX` stays within `[0, viewportW)` across a sweep of
  `elapsedSeconds` including values that wrap multiple times around.
- **`test_zone_state`**: extend the existing `Fighting`-phase cases —
  `skillFiredThisTick` is `-1` on a tick where no skill fires and holds the
  fired index on the tick it does; the fired skill's bonus damage is applied
  to `enemy.hp` (clamped at 0, same as `zone_combat`'s own clamp); a skill
  firing on the tick that would also defeat the enemy still transitions to
  `Walking` correctly (order-of-operations check against the existing
  defeat logic); `skill.timer` does not advance during `Walking`/`Jumping`;
  `startZone`/`restartZone` reset `skill`/`skillFiredThisTick` to fresh
  values.
- **`test_zone_textures`**: add `characterAuraColor` cases — deterministic,
  distinct across realms, saturation trends upward from realm 0 to realm 15.
- `test_economy`, `test_save`, `test_settings`, `test_offline_earnings`,
  `test_hittest`, `test_smoke`, `test_zone_combat`, `test_zone_map`,
  `test_hash` are unaffected and must still pass unmodified. (`test_hittest`
  in particular is unaffected because `Rect`/`rectContains` themselves are
  untouched — only their one hit-testing *caller*, `hitTestHud`, is deleted.)

Build-only check: `pio run -e esp32p4_pioarduino`. `src/` changes (touch
removal, all new rendering/SFX) get no native tests, consistent with this
project's existing split — validated on real hardware, not in this
environment.

## Open Risks

- **No on-device FPS/visual confirmation.** This adds real per-frame draw
  volume (parallax elements, ground texture, damage numbers, tiered monster
  shapes, an aura ring) on top of an already-unconfirmed render budget (see
  the platforms spec's own open risk on this). Every new per-frame
  collection is bounded by a small fixed constant (8 damage numbers, ~6
  parallax elements, a handful of ground tufts) specifically so the
  worst case stays predictable, but actual achieved FPS remains a hardware
  follow-up like everything else in this project.
- **Skill damage balance is a first-pass guess**, not simulated against the
  existing HP-scaling formula across all 16 realms — multipliers/cooldowns
  are simple constants to retune later without touching the cycling logic
  itself, same posture this project has taken with every prior numeric
  balance guess (jump arc constants, patrol speed, etc.).
- **Removing all touch input is a one-way door for this codebase**: any
  future feature that wants a tap target starts from zero, not from a
  partially-working brightness/volume implementation. Given two full specs'
  worth of unresolved touch debugging with no confirmed root cause, this is
  judged a worthwhile trade, not an oversight.
