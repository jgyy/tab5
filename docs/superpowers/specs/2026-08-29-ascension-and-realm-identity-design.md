# Ascension & Realm Identity — Design Spec

## Overview

Cultivation currently dead-ends at realm 15 (Empyrean Realm, `NUM_REALMS - 1`):
`canBreakthrough()` returns `false` forever once there, and Qi keeps
accumulating from the player's generators with nothing left to spend it on.
Separately, the character's per-realm growth today is lopsided — a new
combat skill unlocks only every *other* realm (`SKILLS[]` in `skills.cpp`
gates at realm 0, 2, 4, …, 14), so odd realms (1, 3, 5, …, 15) currently add
nothing distinguishing beyond the existing linear HP/damage scaling formula.

This spec adds two related systems that close both gaps:

1. **Ascension** — a new prestige loop (`lib/core/ascension.h/.cpp`). Once
   the player reaches realm 15 and banks enough Qi past that point, the game
   automatically "ascends": qi/generators/realm hard-reset to a fresh start,
   and the Qi spent converts into a permanent, ever-growing Qi/sec
   multiplier. This gives the previously-wasted post-cap Qi a real payoff and
   keeps the idle loop from ever truly plateauing.
2. **Realm Identity traits** — a new parallel unlock table
   (`lib/core/traits.h/.cpp`) granting one passive, automatic trait on every
   *odd* realm, leaving the existing (tuned, tested) skill table on even
   realms completely untouched. Combined, every single realm now grants
   something new: a skill on even realms, a trait on odd ones.

Both are fully automated — no manual trigger of any kind, consistent with
this app having no touch controls left.

## Goals

- Ascension auto-triggers with no player input, the same way realm
  breakthroughs and generator purchases already do.
- The ascension bonus is a permanent, monotonically-growing multiplier on
  Qi/sec, so each successive climb from realm 0 is faster than the last —
  the loop keeps compounding indefinitely instead of flatlining at realm 15.
- Ascending is a full reset of the moment-to-moment economy (qi, generator
  counts, realm index) — "cultivation life" starts over — but never loses
  the permanent multiplier or the ascension count.
- Every realm (0–15) grants exactly one new character feature: skills keep
  their existing even-realm cadence unchanged; a new trait table fills in
  the odd realms.
- All 8 new traits are deterministic (no RNG anywhere in this project's
  combat, and this doesn't change that) and automatic (no activation input).
- Ascension progress and count persist across reboots via the existing NVS
  save file, following the save format's established version-migration
  pattern (v1→v2 already exists for brightness/volume).
- Every new numeric constant (ascension thresholds, trait magnitudes) is a
  small, named, easily-retuned value — first-pass numbers meant to be
  simulation-tuned during implementation, the same posture every prior
  numeric-balance guess in this project has taken.

## Non-Goals

- No manual/touch interaction anywhere in either system.
- No changes to the existing skill table's unlock cadence, cooldowns, or
  damage multipliers — `SKILLS[]` in `skills.cpp` is untouched.
- No changes to realm names, `REALM_QI_THRESHOLD[]`, or `NUM_REALMS` — the
  16-realm climb itself is unchanged; ascension resets *into* it, it doesn't
  extend it.
- No new equipment/loot/UI shop of any kind — that's a separate, later
  sub-project (loot/equipment layer) explicitly deferred.
- No multi-tier ascension currencies or ascension-only unlock trees —
  `insight` is a single scalar number feeding one multiplier formula, kept
  as simple as the existing realm-multiplier economy.
- Traits do not persist any state to NVS: like skills, they're pure
  functions of `realmIndex`, which is already part of `GameState` — nothing
  new needs saving for the trait system itself (only ascension's two new
  fields need a save-format change).

## Ascension Mechanic (`lib/core/ascension.h/.cpp`)

```cpp
#pragma once

struct AscensionState {
    uint32_t ascensionCount = 0;
    double insight = 0.0; // permanent prestige currency; never resets
};

// Permanent Qi/sec multiplier from accumulated insight - linear and modest
// (kInsightBonusPerPoint, first-pass guess 0.02 i.e. +2% per point) so it
// compounds ascension over ascension without exploding; retuned by
// simulation during implementation like every other balance constant here.
double qiMultiplierForInsight(double insight);

// Qi banked at the moment of ascension -> insight gained. sqrt-scaled (a
// well-worn incremental-game curve: large late-game Qi numbers convert into
// modest, sane insight gains) rather than linear, so a single very long
// AFK stretch at the cap can't detonate the multiplier.
double insightGainForQi(double qiAtAscension);

// True once realmIndex is at the cap AND qi has crossed a per-ascension-
// count threshold that itself grows with ascensionCount (mirrors how
// REALM_QI_THRESHOLD grows realm to realm) - so ascending twice in a row
// isn't instant even with the fresh multiplier already applied.
bool canAscend(const struct GameState& state, const AscensionState& ascension);

// If canAscend(): computes insight gained from state.qi, adds it into
// ascension.insight, increments ascension.ascensionCount, and resets state
// to a fresh GameState{} (qi=0, starting generator, realmIndex=0).
// Otherwise leaves both arguments unchanged and returns false.
bool attemptAscend(struct GameState& state, AscensionState& ascension);
```

`GameState` (in `economy.h`) is a forward-declared dependency here, not the
other way around — `ascension.h` includes `economy.h` for the real
definition, keeping the dependency direction the same as `skills.h`'s
relationship to `zone_combat.h`.

`economy.h`/`economy.cpp` gains one new parameter, defaulted so every
existing call site and test keeps compiling unchanged (the same pattern
`makeZoneMap`'s `seed`/`isBossZone` established):

```cpp
double qiPerSecond(const GameState& state, double ascensionMultiplier = 1.0);
```

`tick()` is unaffected in signature (still `tick(GameState&, double
dtSeconds)`) but internally multiplies by the *current* ascension multiplier
— since `tick()` doesn't have access to `AscensionState` today, it gains the
same defaulted parameter:

```cpp
void tick(GameState& state, double dtSeconds, double ascensionMultiplier = 1.0);
```

**Main loop wiring (`main.cpp`)**: a new file-scope `AscensionState
gAscensionState;` alongside `gState`. `tick(gState, dt, qiMultiplierForInsight(gAscensionState.insight))`
replaces the current no-multiplier call. Immediately after the existing
breakthrough `while` loop (ascension is checked *after* breakthroughs
resolve, not interleaved — a large offline-earnings injection should finish
climbing to realm 15 first, then ascend, in the same tick):

```cpp
if (canAscend(gState, gAscensionState)) {
    attemptAscend(gState, gAscensionState);
    triggerAscensionFx();
    playAscensionSfx();
}
```

A plain `if`, not `while`: `attemptAscend` resets `qi` to `0`, and the next
ascension threshold is strictly positive, so a second ascension can never
fire in the same tick.

## Realm Identity Traits (`lib/core/traits.h/.cpp`)

Mirrors `skills.h`'s data-table shape, but each trait is a distinct,
named mechanic rather than an interchangeable round-robin entry (traits
don't compete for a shared rotation slot the way skills do — every unlocked
trait is always "on"):

```cpp
#pragma once

struct TraitDef {
    const char* name;
    int unlockRealmIndex; // always odd: 1, 3, 5, 7, 9, 11, 13, 15
    const char* description; // short flavor text, for the stats panel / future UI
};

constexpr int NUM_TRAITS = 8;
extern const TraitDef TRAITS[NUM_TRAITS];

// TRAITS[i].unlockRealmIndex is the single source of truth each of these
// reads from - no realm number is hardcoded a second time here.
bool hasIronSkin(int realmIndex);          // realm >= 1
bool hasSteadyBreath(int realmIndex);      // realm >= 3
bool hasSoulEcho(int realmIndex);          // realm >= 5
bool hasExecution(int realmIndex);         // realm >= 7
bool hasSwiftFeet(int realmIndex);         // realm >= 9
bool hasRadiantAura(int realmIndex);       // realm >= 11
bool hasUndyingWill(int realmIndex);       // realm >= 13
bool hasEmpyreanRadiance(int realmIndex);  // realm >= 15

// 1.0 unless hasIronSkin(); then a flat reduction (first-pass guess 0.9,
// i.e. -10% incoming damage). Does not scale further with higher realms -
// one trait, one fixed effect, same posture as an unlocked skill.
float incomingDamageMultiplier(int realmIndex);

// 0 unless hasSteadyBreath(); then a flat HP/sec regen amount (first-pass
// guess proportional to playerMaxHp, e.g. 0.01 * playerMaxHp per second).
float regenPerSecond(int realmIndex, int playerMaxHp);

// 1.0 unless hasSwiftFeet(); then a flat walk/jump speed multiplier
// (first-pass guess 1.3, i.e. +30% movement speed).
float movementSpeedMultiplier(int realmIndex);

// 1.0 unless hasEmpyreanRadiance(); then a flat skill-damage multiplier
// (first-pass guess 1.2, i.e. +20% skill damage) - the capstone trait.
float skillDamageMultiplier(int realmIndex);

// Soul Echo / Execution / Radiant Aura / Undying Will have no standalone
// multiplier accessor - their thresholds/magnitudes are named constants
// (kSoulEchoInterval, kSoulEchoBonusMultiplier, kExecutionHpFraction,
// kExecutionBonusMultiplier, kRadiantAuraIntervalSeconds,
// kRadiantAuraDamageMultiplier) applied directly in zone_state.cpp guarded
// by their has*() gate, mirroring how kBossEnrageCooldownMultiplier is a
// named constant applied directly in zone_state.cpp rather than routed
// through an accessor function.
```

### Trait roster

| Realm | Trait | Mechanic |
|---|---|---|
| 1 Qi Condensation | Iron Skin | Flat % reduction to incoming enemy damage |
| 3 Core Formation | Steady Breath | Passive HP regen/sec while `Fighting` |
| 5 Soul Transformation | Soul Echo | Every `kSoulEchoInterval`-th landed player autoattack deals bonus damage |
| 7 Spirit Severing | Execution | Bonus damage when the hit drops the enemy at/below `kExecutionHpFraction` of its max HP |
| 9 Immortal Ascension | Swift Feet | Faster platform-to-platform walk/jump speed |
| 11 Heaven Immortal | Radiant Aura | Periodic small AoE tick damage to the current enemy, on its own timer independent of autoattack/skill cooldowns |
| 13 Daluo Immortal | Undying Will | Once per zone run, a would-be-fatal hit clamps the player to 1 HP instead of dying |
| 15 Empyrean Realm | Empyrean Radiance | Permanent skill-damage multiplier (capstone) |

### Integration (`lib/core/zone_combat.h/.cpp`, `lib/core/zone_state.h/.cpp`)

**Iron Skin** is the one trait that changes `zone_combat.h`'s public
surface, and only by a defaulted trailing parameter (same
backward-compatible pattern as `makeZoneMap`'s `seed`):

```cpp
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds,
                 float incomingDamageMultiplier = 1.0f);
```

Applied only to the damage `enemy` deals to `player` when its cooldown
fires, before the HP subtraction/clamp — every existing call site and
`test_zone_combat` case keeps compiling and passing unchanged via the
default. `zone_state.cpp`'s Fighting branch passes
`traits::incomingDamageMultiplier(currentRealmIndex)`.

Every other trait is implemented inside `tickZone()`'s existing Fighting
branch using the same before/after-diffing style `deriveZoneTickEvents()`
already established for higher-level events — no further changes to
`zone_combat.{h,cpp}`'s return types:

- **Steady Breath**: applied once at the top of the Fighting branch, before
  `tickCombat()` runs: `state.player.hp = min(player.maxHp, player.hp +
  regenPerSecond(...) * dt)`.
- **Soul Echo**: `ZoneState` gains `int playerAutoAttackCount = 0;` (reset
  in `startZone()`). Snapshot `enemy.hp` immediately before `tickCombat()`;
  if it's lower immediately after, the player's autoattack landed this tick
  (skill damage is applied in a separate, later statement, so this diff
  isolates the autoattack specifically) — increment the counter, and every
  `kSoulEchoInterval`-th landed hit adds `player.attackDamage *
  kSoulEchoBonusMultiplier` directly onto `enemy.hp`'s reduction, gated by
  `traits::hasSoulEcho(currentRealmIndex)`.
- **Execution**: after the normal hit lands (using the same before/after
  autoattack-landed detection as Soul Echo), if the enemy's HP is now at or
  below `kExecutionHpFraction` of its max, subtract an additional
  `player.attackDamage * kExecutionBonusMultiplier`, gated by
  `traits::hasExecution(currentRealmIndex)`.
- **Swift Feet**: the Walking-phase step calculation multiplies
  `kWalkSpeedUnitsPerSec` by `traits::movementSpeedMultiplier(currentRealmIndex)`.
  `makeJumpArc` gains a defaulted trailing `float speedMultiplier = 1.0f`
  parameter (again the `seed`-style backward-compatible pattern) so jump
  duration scales consistently with walk speed instead of the character
  walking fast but jumping at a fixed rate.
- **Radiant Aura**: `ZoneState` gains `float radiantAuraTimerSeconds =
  0.0f;` (reset in `startZone()`), advanced by `dt` only while `Fighting`;
  crossing `kRadiantAuraIntervalSeconds` deals `player.attackDamage *
  kRadiantAuraDamageMultiplier` to the enemy and resets the timer, gated by
  `traits::hasRadiantAura(currentRealmIndex)`.
- **Undying Will**: `ZoneState` gains `bool undyingWillUsedThisRun = false;`
  (reset in `startZone()`). Checked immediately after `tickCombat()` and
  before the existing `isDefeated(state.player)` branch: if `player.hp <= 0
  && traits::hasUndyingWill(currentRealmIndex) && !undyingWillUsedThisRun`,
  set `player.hp = 1` and `undyingWillUsedThisRun = true` instead of letting
  the defeat branch restart the zone.
- **Empyrean Radiance**: the existing skill-damage line in the Fighting
  branch gains one factor: `skillDamage = attackDamage * SKILLS[i].damageMultiplier
  * traits::skillDamageMultiplier(currentRealmIndex)`.

All eight are pure additions inside already-tested branches — no
`ZonePhase`, no new phase transitions, no change to the walk/jump/encounter
state machine's shape.

## Persistence (`lib/core/save.h/.cpp`)

`SaveData` bumps to v3, gaining two fields:

```cpp
constexpr uint16_t SAVE_VERSION = 3; // v3 added ascension count/insight; see v1, v2 migrations
struct SaveData {
    // ...existing fields unchanged...
    uint32_t ascensionCount = 0;
    double ascensionInsight = 0.0;
};
```

Today's `SaveData` layout (magic/version/qi/generatorCounts/realmIndex/
lastSaveEpochSeconds/brightness/volume, version 2) is preserved verbatim as
a new `SaveDataV2` fallback struct, exactly mirroring how today's
`deserializeSave()` already keeps a `SaveDataV1` fallback for the v1→v2
migration. `deserializeSave()` gains a third fallback branch, tried after
the current-version check and the V1 check both fail: parse as `SaveDataV2`,
and on success construct a `SaveData` with `ascensionCount = 0` /
`ascensionInsight = 0.0` (a pre-ascension save has never ascended) and every
other field copied across — a returning player's save is upgraded forward,
never reset to a fresh game.

`toSaveData`/`toGameState` gain the ascension half of the round-trip,
following the existing brightness/volume-as-trailing-defaulted-parameters
convention:

```cpp
SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness = 200,
                     uint8_t volume = 128, const AscensionState& ascension = AscensionState{});
AscensionState toAscensionState(const SaveData& data);
```

`main.cpp`'s boot path adds `gAscensionState = toAscensionState(save);`
alongside the existing `gState = toGameState(save);`, and `saveNow()` passes
`gAscensionState` into `toSaveData`.

## UI (`src/ui.cpp`, `src/ui.h`)

`drawHud`/`drawHeader` gain a `const AscensionState&` parameter (`ui.h`'s
`drawHud(M5GFX&, const GameState&, const ZoneState&)` becomes `drawHud(M5GFX&,
const GameState&, const ZoneState&, const AscensionState&)`). The header's
left-aligned text line gains an ascension readout only once `ascensionCount
> 0` — a fresh game's header is unchanged, so this never crowds the
already-tight header for the common early-game case:

```
"<Realm Name>  Qi/s <rate>  Asc <count> (x<multiplier>)"
```

using the same `formatQi`-style compact-number helper already used for the
Qi rate, so a large multiplier still fits. No new bar, no layout change to
`kHeaderHeight` or the stats panel — this is one more branch in an existing
`snprintf` call, the same low-risk shape the boss-encounters spec's HUD
change took for the enemy stat bar's label.

## Visual & Audio (`src/zone_view.h/.cpp`)

New trigger/SFX pair, following the existing
`triggerRealmBreakthroughFx`/`playBreakthroughSfx` naming and "latch a state
var, `renderZoneView()` reads and decays it" pattern:

```cpp
void triggerAscensionFx(); // reuses the breakthrough celebration's expanding
                            // ring/rays curve, its own state var, a distinct
                            // color (proposed: deep violet/gold, reading as
                            // "bigger" than a regular breakthrough's white/gold)
void playAscensionSfx();   // its own longer/grander fanfare, distinct from
                            // playBreakthroughSfx()
```

Both are fired from `main.cpp`'s new ascension branch, mirroring exactly how
`triggerRealmBreakthroughFx`/`playBreakthroughSfx` are fired from the
existing breakthrough branch today.

## Testing Plan

Native (`pio test -e native`), no device required:

- `test_ascension` (new): `qiMultiplierForInsight`/`insightGainForQi` pure-
  function cases across a spread of inputs (zero, small, large, and a
  never-negative/never-NaN sanity sweep); `canAscend`/`attemptAscend`
  truth-table cases — `false` below realm 15, `false` at realm 15 below
  threshold, `true` at/above threshold; a full `attemptAscend` call asserts
  `qi` resets to `0`, `generatorCounts` resets to the fresh-game default,
  `realmIndex` resets to `0`, `ascension.insight` increases by exactly
  `insightGainForQi(qiAtAscension)`, and `ascensionCount` increments by
  exactly 1; repeated ascension cycles (simulate ticking + auto-breakthrough
  + auto-ascend across several loops) assert the threshold genuinely grows
  each time rather than allowing an instant re-ascend.
- `test_traits` (new): every `has*()` gate is `false` immediately below its
  `unlockRealmIndex` and `true` at/above it, across the full realm range;
  `incomingDamageMultiplier`/`regenPerSecond`/`movementSpeedMultiplier`/
  `skillDamageMultiplier` return their unlocked/locked values correctly; a
  table-driven sweep of `TRAITS[]` asserts exactly 8 entries, one per odd
  realm 1–15, no gaps and no duplicates.
- `test_zone_combat`: new cases for `tickCombat`'s
  `incomingDamageMultiplier` parameter (reduced damage applied correctly,
  clamped at 0 same as before); every existing case re-asserted unchanged
  via the default argument.
- `test_zone_state`: new cases per trait, each isolated — Soul Echo's bonus
  fires only on the correct-cadence hit; Execution's bonus fires only once
  the post-hit HP fraction crosses the threshold; Swift Feet changes
  `posX`'s per-tick step and the resulting jump arc's duration; Radiant
  Aura fires only while `Fighting` and only every interval; Undying Will
  clamps to 1 HP exactly once per zone run and a second would-be-fatal hit
  in the same run is *not* saved; Empyrean Radiance scales the skill-damage
  line. Every existing Walking/Jumping/Fighting/Cleared/boss case must still
  pass unmodified (all new behavior is realm-gated `false` at realm 0, the
  existing tests' realm).
- `test_save`: new v2→v3 migration case (a hand-built v2-layout buffer
  deserializes into a `SaveData` with `ascensionCount == 0` /
  `ascensionInsight == 0.0` and every other field preserved) alongside the
  existing v1→v2 migration case, which must still pass unmodified; round-
  trip serialize/deserialize for a save with nonzero ascension fields.
- `test_economy`: `qiPerSecond`/`tick` with a non-1.0 `ascensionMultiplier`
  argument; every existing case (implicit 1.0 default) re-asserted
  unchanged.
- `test_zone_map`, `test_zone_textures`, `test_skills`, `test_fx`,
  `test_zone_events`, `test_offline_earnings`, `test_settings`, `test_hash`,
  `test_math3d`, `test_hittest`, `test_smoke` are unaffected and must still
  pass unmodified.

Build-only check: `pio run -e esp32p4_pioarduino`. Flashing and
visual/audio/balance confirmation on real hardware remains a follow-up, same
bar every prior spec in this repo has carried.

## Open Risks

- **Ascension threshold growth and insight-to-multiplier constants are
  first-pass numbers**, not yet run through a simulation test — during
  implementation, a simulation (in the spirit of the boss-encounters spec's
  clear-rate sweep) should measure real-time-to-first-ascension and
  time-to-Nth-ascension and tune `kInsightBonusPerPoint`/
  `kInsightQiDivisor`/the threshold growth rate toward a deliberate target
  (proposed: first ascension reachable well within a single realistic play
  session, not requiring multi-day AFK).
- **Trait magnitudes (10% damage reduction, regen rate, Soul Echo interval,
  Execution threshold, Swift Feet speed, Radiant Aura interval/damage,
  Empyrean Radiance's skill multiplier) are first-pass guesses**, tuned by
  inspection here and meant to be simulation-checked during implementation
  against the existing ~100% first-attempt clear-rate target the platforms
  revamp established — traits should make the climb comfortably easier
  without trivializing it.
- **No on-device visual/audio/FPS confirmation from this environment** —
  same caveat every prior spec here has carried.
- **Ascension resets `zoneRunIndex`/zone terrain indirectly**: because
  `restartZone()`/`startZone()` are keyed off `currentRealmIndex`, an
  ascension's realm reset to 0 means the zone view will show realm 0's
  palette/terrain again on the very next `tickZone()` call after the
  player's current zone loop ends — there's a brief window (the remainder
  of the in-progress zone) where the HUD's realm name has already changed
  but the zone view hasn't yet, matching exactly how a regular
  mid-zone breakthrough already behaves today (not a new inconsistency).
