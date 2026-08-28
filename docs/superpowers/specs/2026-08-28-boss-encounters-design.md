# Boss Encounters — Design Spec

## Overview

The MapleStory-style zone (`zone_map`/`zone_state`/`zone_combat`/`zone_textures`/
`zone_view`, from the `2026-08-27-maplestory-idle-zone`,
`2026-08-28-maplestory-idle-platforms`, and
`2026-08-28-maplestory-skills-and-graphics` specs) is the app's only screen:
the character auto-walks/jumps across a per-realm, per-loop-seeded set of
platforms, auto-fighting every monster it reaches, looping the same realm's
zone (reshuffled) until a rare cultivation-realm breakthrough moves it to the
next realm's zone. Every monster today is a scaled-up regular — three visual
tiers (`round`/`spiky`/`winged`, `lib/core/zone_map.cpp`'s
`kMaxDifficultyTier`), difficulty set purely by which platform it spawned on.

This spec adds **boss encounters**: every third zone loop is a dedicated
"boss zone" whose only monster is a single, much tougher boss placed on the
zone's last platform, with a one-time mid-fight enrage and a bonus Qi reward
on top of the zone's normal clear reward. It builds directly on the existing
state machine and difficulty formulas rather than replacing any of them.

## Goals

- Every `kBossZoneInterval`-th zone loop (proposed: every 3rd) replaces the
  normal monster roster with one boss on the last elevated platform; every
  other platform in that zone has no monsters, so the walk/jump route to the
  boss is unchanged machinery.
- Give the boss one deterministic mid-fight escalation: once its HP drops to
  half or below, its attack cooldown shortens once (latched, since it never
  heals) — a single threshold-based state change, not a scripted multi-phase
  attack pattern.
- Grant a Qi bonus, on top of the zone's own clear reward, the instant the
  boss dies.
- Make a boss fight visually and audibly distinct: its own silhouette (not
  one of the three regular tiers), its own HUD treatment on the enemy HP bar,
  and dedicated enrage/defeat FX and SFX.
- Keep every constant introduced here a small, named, easily-retuned value —
  boss HP/damage/enrage strength are first-pass numbers meant to be tuned via
  a simulation test during implementation, the same posture this project has
  taken with every prior numeric-balance guess (see Open Risks).

## Non-Goals

- No multi-phase attack patterns or telegraphed special attacks — one enrage
  threshold is the entire boss "mechanic," per the approved design direction.
- No manual/touch interaction — bosses are fought exactly like regular
  monsters, fully autoplayed.
- No changes to `zone_combat.{h,cpp}`'s public API — enrage is implemented as
  a direct mutation of the existing `CombatantState.attackCooldownSeconds`
  field from `zone_state.cpp`, not a new combat-resolution function.
- No changes to the regular (non-boss) monster difficulty formula, tier
  visuals, patrol motion, jump mechanic, skills, or economy/save format.
  Bosses are fully regenerated every time from `realmIndex` + `zoneRunIndex`
  + `seed`, so nothing new needs to persist to NVS — no save migration.
- No more than one boss per zone, and no boss sharing a zone with regular
  monsters — a boss zone's only monster is the boss.

## Zone Structure: Boss Zones

`MonsterSpawn` gains one field:

```cpp
struct MonsterSpawn {
    float x;
    int platformIndex;
    int maxHp;
    int damage;
    bool isBoss = false; // NEW — true for the single monster in a boss zone
};
```

`makeZoneMap` gains a third parameter, defaulted so every existing call site
(including every current test) keeps compiling unchanged, mirroring how
`seed` itself was added with a default in the platforms revamp:

```cpp
ZoneMap makeZoneMap(int realmIndex, int seed = 0, bool isBossZone = false);
```

Terrain generation (platform count/gaps/widths/heights) is **completely
unaffected** by `isBossZone` — a boss zone still rolls its usual 3-5
elevated platforms, so the route to the boss still varies loop to loop.
Only the monster-generation step branches:

- `isBossZone == false` (today's behavior, unchanged): the existing
  per-elevated-platform loop, 1-2 hash-jittered monsters per platform.
- `isBossZone == true`: skip that loop entirely. Place exactly one
  `MonsterSpawn` on `platforms.back()` (the last elevated platform), `x` =
  that platform's plain midpoint (not the jittered multi-monster placement —
  a boss is one deliberate encounter, not part of that system), `isBoss =
  true`, `maxHp`/`damage` from a new boss formula living alongside the
  existing tier formula in `zone_map.cpp`:

```cpp
constexpr int kBossBaseHp = 300, kBossHpPerRealm = 120;
constexpr int kBossBaseDamage = 18, kBossDamagePerRealm = 9;

int bossMaxHp(int realmIndex) { return kBossBaseHp + kBossHpPerRealm * realmIndex; }
int bossDamage(int realmIndex) { return kBossBaseDamage + kBossDamagePerRealm * realmIndex; }
```

**Which loops are boss zones** is decided by the caller, not by
`makeZoneMap` itself (keeping `makeZoneMap` a pure function of its explicit
arguments, directly testable for both branches without reverse-engineering
an interval formula). A new helper in `zone_state.h/.cpp`:

```cpp
constexpr int kBossZoneInterval = 3; // every Nth zone loop is a boss zone

// True for the 3rd, 6th, 9th... zone loop (1-indexed) - i.e. zoneRunIndex
// values 2, 5, 8, .... zoneRunIndex 0 (the very first zone of a session) is
// never a boss zone.
bool isBossZoneForRunIndex(int zoneRunIndex);
```

Both places that build a `ZoneMap` today call this helper and thread the
result through:

- `main.cpp`'s boot-time call:
  `makeZoneMap(gState.realmIndex, 0, isBossZoneForRunIndex(0))` (always
  `false` at `zoneRunIndex == 0`, spelled out explicitly rather than relied
  upon, so the boot call site stays correct if `kBossZoneInterval` is ever
  retuned to 1).
- `restartZone()`: `bool boss = isBossZoneForRunIndex(nextRunIndex); ZoneMap
  map = makeZoneMap(currentRealmIndex, nextRunIndex, boss);`

## Boss Combat: One-Time Enrage

`ZoneState` gains four fields, all reset to their fresh-start values in
`startZone()` (and therefore on every `restartZone()`, same as every other
per-zone field today):

```cpp
struct ZoneState {
    // ...existing fields unchanged...
    bool currentEncounterIsBoss = false; // set at the Walking->Fighting transition from the engaged spawn's isBoss
    bool bossEnraged = false;            // latches true once, never clears mid-fight (boss never heals)
    bool bossJustEnraged = false;        // pulses true on the single tickZone() call enrage triggers; reset every call
    bool bossJustDefeated = false;       // pulses true on the single tickZone() call a boss dies; reset every call
};
```

`bossJustEnraged`/`bossJustDefeated` follow the exact pattern
`skillFiredThisTick` already established: reset to `false` at the very top
of every `tickZone()` call, so a caller (`main.cpp`) can inspect them
immediately after that same call and reliably see them `true` only on the
tick the event happened.

Constants (`zone_state.h`, alongside the other zone-pacing constants):

```cpp
constexpr float kBossEnrageHpFraction = 0.5f;         // enrage triggers at <= 50% HP
constexpr float kBossEnrageCooldownMultiplier = 0.7f; // ~43% faster attacks once enraged
```

`tickZone()` changes, all inside the existing Walking/Fighting branches —
no new `ZonePhase`:

- **Walking → Fighting transition** (unchanged trigger condition): also
  snapshots `state.currentEncounterIsBoss = spawn.isBoss;` alongside the
  existing `state.enemy = makeEnemyCombatant(...)`.
- **Fighting**, after the existing `tickCombat`/skill-damage block and
  before the player/enemy defeat checks:

```cpp
if (state.currentEncounterIsBoss && !state.bossEnraged &&
    state.enemy.hp > 0 && state.enemy.hp <= state.enemy.maxHp / 2) {
    state.enemy.attackCooldownSeconds *= kBossEnrageCooldownMultiplier;
    state.bossEnraged = true;
    state.bossJustEnraged = true;
}
```

- **Fighting, enemy-defeat branch**: on top of the existing
  monster-defeated bookkeeping, a boss adds its bonus reward immediately and
  clears its own per-encounter flags:

```cpp
} else if (isDefeated(state.enemy)) {
    if (state.currentEncounterIsBoss) {
        state.qiRewardPending += proposedReward; // bonus, stacks with the zone's own clear reward below
        state.bossJustDefeated = true;
    }
    state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
    state.currentMonsterIndex = -1;
    state.currentEncounterIsBoss = false;
    state.bossEnraged = false;
    state.phase = ZonePhase::Walking;
    state.player.hp = state.player.maxHp;
}
```

- **Walking, Cleared transition**: changes from `=` to `+=` so a bonus
  staged above isn't clobbered — safe for non-boss zones too, since
  `qiRewardPending` is always `0.0` there until this line runs:

```cpp
state.qiRewardPending += proposedReward; // was `=`
```

A boss zone's total reward is therefore `2 * proposedReward` (bonus at boss
death + the zone's own clear reward moments later, once the character
finishes walking to the platform's edge) — a regular zone's total is
unchanged at `1 * proposedReward`.

Player defeat mid-boss-fight is unaffected: the existing
`isDefeated(state.player)` branch already calls `restartZone()`
unconditionally, which rebuilds a fresh `ZoneState` via `startZone()` —
`currentEncounterIsBoss`/`bossEnraged`/`qiRewardPending` all come back to
their zero/false defaults, same "no permanent penalty, only time lost"
behavior every other defeat in this app already has.

## Reward Wiring (`main.cpp`)

No signature changes to `tickZone()` — `proposedReward` is already computed
and passed in every call; the boss bonus reuses that same value from inside
`zone_state.cpp` rather than `main.cpp` computing a second number. `main.cpp`
adds two derived booleans next to its existing `skillFired`/`monsterDefeated`
ones, following the same `wasFighting`-gated pattern:

```cpp
bool bossEnrageTriggered = wasFighting && gZoneState.bossJustEnraged;
bool bossDefeated = wasFighting && !zoneRestarted && gZoneState.bossJustDefeated;
```

`bossEnrageTriggered` fires `triggerBossEnrageFx()` + `playBossEnrageSfx()`.
`bossDefeated` fires alongside the existing `monsterDefeated` block (a boss
kill is still a `monsterDefeated` kill, so `triggerLootPop()`/`playLootSfx()`
still fire too) an additional `triggerBossDefeatFx()` +
`playBossDefeatSfx()` — a distinct, bigger fanfare from the zone-clear
`playVictorySfx()` that plays moments later, so the two events read as
separate beats rather than the same sound twice.

## Visual & Audio (`zone_view.cpp`, `zone_textures.h/.cpp`, `ui.cpp`)

**Silhouette.** `drawMonster()` gains a trailing `bool isBoss` parameter.
When `true`, it ignores the regular `tierIndex` silhouette selection
entirely and instead draws a body noticeably bigger than tier 2's
(`bigRadius = radius + 14` vs. tier 2's `+6`) plus a simple 3-5-spike crown
across the top, drawn in a fixed gold accent color regardless of realm — the
same `fillTriangle` technique tier 1's spikes already use, so this reuses an
existing drawing primitive rather than introducing a new one. Distinct from
all three regular tiers at a glance, cheap to draw.

**Color.** A new deterministic color in `zone_textures.h/.cpp`:

```cpp
// Boss body color for a realm's zone - darker/more saturated than any
// regular monsterColor tier, tinted by the same per-realm hue. Deterministic.
RGB bossColor(int realmIndex);
```

**Enrage/defeat FX.** New trigger/SFX pairs in `zone_view.h/.cpp`, following
the existing naming and "latch a state var, `renderZoneView()` reads it and
decays it" pattern `triggerSkillFx`/`triggerLootPop`/
`triggerRealmBreakthroughFx` already use:

```cpp
void triggerBossEnrageFx();  // bigger/longer shakeOffset() amplitude + a brief color flash on the boss
void playBossEnrageSfx();    // a distinct, harsher tone from playHitSfx()/playAttackSfx()
void triggerBossDefeatFx();  // expanding ring/rays (same pulseEnvelope() curve triggerRealmBreakthroughFx()
                              // already uses), but its own state var and a crimson/gold color instead of
                              // breakthrough's white/gold - reuses the proven curve without visually
                              // conflating a boss kill with a realm breakthrough
void playBossDefeatSfx();    // its own short fanfare, distinct from playVictorySfx()'s zone-clear jingle
```

**HUD.** `ui.cpp`'s enemy stat bar (`columnRect(row, 1)`) already branches
its label text on whether there's a live encounter (`"Enemy HP d/d"` vs.
`"Enemy HP --"`); it gains one more branch for `currentEncounterIsBoss`:
label becomes `"BOSS HP d/d"` and the bar's fill/gloss colors swap from
`kEnemyHpColor`/`kEnemyHpGloss` to new `kBossHpColor`/`kBossHpGloss`
constants (a more alarming deep crimson/black) — same `drawBar()` call,
different arguments, no new bar or layout change.

## Testing Plan

Native (`pio test -e native`), no device required:

- `test_zone_map`: `makeZoneMap(realmIndex, seed, /*isBossZone=*/true)`
  produces exactly one monster, `isBoss == true`, `platformIndex` equal to
  the last platform's index, `x` at that platform's exact midpoint,
  `maxHp`/`damage` matching the new boss formula, across a sweep of
  realms/seeds; the `isBossZone == false` path (including the default
  argument) is asserted byte-for-byte unchanged against every existing
  monster-generation test. New cases for `isBossZoneForRunIndex()`: `false`
  at 0, 1; `true` at 2, 5, 8; `false` at 3, 4, 6, 7 — a full truth-table
  sweep across a wide range, not just a couple of spot checks.
- `test_zone_state`: Fighting a boss whose HP crosses the 50% threshold
  triggers enrage exactly once (`attackCooldownSeconds` shortened,
  `bossEnraged` latched `true`, `bossJustEnraged` `true` on that exact tick
  only and `false` on every tick before and after); defeating a boss adds
  the bonus into `qiRewardPending` on the defeat tick
  (`bossJustDefeated` `true` only then), and the zone's later Cleared
  transition stacks the base reward on top rather than overwriting it, so a
  full boss-zone clear totals `2 * proposedReward`; a regular zone's total
  stays exactly `1 * proposedReward` (regression check on the `=`→`+=`
  change); player defeat mid-boss-fight restarts the zone with
  `currentEncounterIsBoss`/`bossEnraged`/`qiRewardPending` all back at their
  fresh-start values. All existing Walking/Jumping/Fighting/Cleared cases
  must still pass unmodified.
- `test_zone_textures`: new `bossColor` cases (deterministic; distinct from
  `monsterColor`'s three tiers and from sky/ground/platform colors; distinct
  across realms).
- **New simulation-style test**, in the spirit of the platforms spec's own
  reachability sweep and the graphics spec's clear-rate simulation: run a
  full boss encounter (player stats from `makePlayerCombatant`, including
  its realm-scaled skill kit, vs. a boss from `bossMaxHp`/`bossDamage` with
  enrage applied at the threshold) at a spread of realms (0, 5, 10, 15) and
  report a first-attempt clear rate. Used during implementation to tune
  `kBossBaseHp`/`kBossHpPerRealm`/`kBossBaseDamage`/`kBossDamagePerRealm`/
  `kBossEnrageCooldownMultiplier` toward a deliberately-harder-than-regular
  target (proposed: roughly 70-80%, vs. a regular zone's ~100%) rather than
  guessed by inspection — see Open Risks.
- `test_zone_combat`, `test_economy`, `test_save`, `test_settings`,
  `test_offline_earnings`, `test_skills`, `test_fx`, `test_hittest`,
  `test_hash`, `test_math3d`, `test_smoke` are unaffected and must still
  pass unmodified.

Build-only check: `pio run -e esp32p4_pioarduino`. Flashing and
visual/audio/balance confirmation on real hardware remains a follow-up, same
bar every prior spec in this repo has carried.

## Open Risks

- **Boss HP/damage/enrage constants are first-pass numbers**, not yet run
  through the simulation test described above — the ~70-80% target
  first-attempt clear rate is a design intent, not a measured result, until
  that test exists and the constants are tuned against it during
  implementation.
- **No on-device visual/audio/FPS confirmation from this environment** —
  same caveat every prior spec here has carried.
- **`kBossZoneInterval = 3` is an unplaytested pacing guess** for how often
  the autoplay loop should hit a boss — a single named constant, cheap to
  retune later without touching any other logic.
- **A boss zone is a single fight** (vs. a regular zone's typical 4-7),
  so it's shorter in wall-clock walking+fighting time despite the doubled
  reward — this changes per-zone-type pacing but not overall economy
  correctness, and isn't mitigated further here.
