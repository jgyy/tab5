# Boss Encounters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every 3rd loop of the MapleStory-style zone becomes a dedicated boss zone — a single, much tougher monster on the last platform with a one-time mid-fight enrage and a bonus Qi reward — with its own silhouette, HUD treatment, and FX/SFX.

**Architecture:** `zone_map` gains an `isBoss` field on `MonsterSpawn` and an `isBossZone` branch in `makeZoneMap()` that swaps the normal per-platform monster loop for one boss on the last elevated platform. `zone_state` gains a small `isBossZoneForRunIndex()` helper (called by `restartZone()` and `main.cpp`'s boot call) plus four `ZoneState` fields and a `tickZone()` extension that latches a one-time enrage at half HP and stacks a bonus reward onto `qiRewardPending` on boss defeat. `zone_textures` gains one new deterministic color function. `zone_view` gains a boss silhouette, boss color selection, and dedicated enrage/defeat FX+SFX. `ui.cpp`'s enemy stat bar swaps label/color when fighting a boss. `main.cpp` threads the boss flag through its boot call and fires the new FX/SFX off two new one-tick event flags, mirroring how it already detects `skillFired`/`monsterDefeated`.

**Tech Stack:** C++ (Arduino framework), PlatformIO, M5Unified/M5GFX, Unity test framework (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-08-28-boss-encounters-design.md`

## Global Constraints

- No multi-phase attack patterns or telegraphed special attacks — one enrage threshold is the entire boss mechanic.
- No manual/touch interaction — bosses are fought exactly like regular monsters, fully autoplayed.
- No changes to `zone_combat.{h,cpp}`'s public API — enrage mutates the existing `CombatantState.attackCooldownSeconds` field directly from `zone_state.cpp`.
- No changes to the regular (non-boss) monster difficulty formula, tier visuals, patrol motion, jump mechanic, skills, economy, or save format — no NVS/save migration.
- No more than one boss per zone, and a boss zone's only monster is the boss.
- All boss art/SFX stays procedural (M5Canvas primitives / deterministic hash-color functions / `M5.Speaker.tone()`) — no imported image or audio assets.
- Every task must leave `pio test -e native` green before its commit; tasks touching `src/` must also leave `pio run -e esp32p4_pioarduino` green before its commit.

---

## Task 1: Boss zone generation (`lib/core/zone_map.h`/`.cpp`)

**Files:**
- Modify: `lib/core/zone_map.h`
- Modify: `lib/core/zone_map.cpp`
- Test: `test/test_zone_map/test_zone_map.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `MonsterSpawn::isBoss` (bool field, default `false`); `ZoneMap makeZoneMap(int realmIndex, int seed = 0, bool isBossZone = false)` (new third parameter); `int bossMaxHp(int realmIndex)`, `int bossDamage(int realmIndex)` (file-local to `zone_map.cpp`, not exported) — consumed by Task 2's `zone_state.cpp` (via the `isBossZone` param) and Task 4's `zone_view.cpp` (via `MonsterSpawn::isBoss`).

- [ ] **Step 1: Write the failing tests**

Add to `test/test_zone_map/test_zone_map.cpp` (before `int main`):

```cpp
void test_boss_zone_has_exactly_one_boss_monster_on_last_platform(void) {
    for (int realm = 0; realm < 16; realm += 3) {
        for (int seed = 0; seed < 10; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed, /*isBossZone=*/true);
            TEST_ASSERT_EQUAL(1, (int)m.monsters.size());
            TEST_ASSERT_TRUE(m.monsters[0].isBoss);
            int lastPlatformIndex = (int)m.platforms.size() - 1;
            TEST_ASSERT_EQUAL(lastPlatformIndex, m.monsters[0].platformIndex);
        }
    }
}

void test_boss_monster_sits_at_its_platforms_midpoint(void) {
    ZoneMap m = makeZoneMap(3, 7, true);
    const Platform& p = m.platforms.back();
    float mid = (p.x0 + p.x1) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, mid, m.monsters[0].x);
}

void test_boss_stats_match_the_boss_formula(void) {
    ZoneMap low = makeZoneMap(0, 0, true);
    ZoneMap high = makeZoneMap(10, 0, true);
    TEST_ASSERT_EQUAL(300, low.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(18, low.monsters[0].damage);
    TEST_ASSERT_EQUAL(300 + 120 * 10, high.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(18 + 9 * 10, high.monsters[0].damage);
}

// Regression: the isBossZone==false path (including its default) must be completely untouched.
void test_non_boss_zone_is_unaffected_by_the_boss_parameter(void) {
    ZoneMap withDefault = makeZoneMap(4, 2);
    ZoneMap withExplicitFalse = makeZoneMap(4, 2, false);
    TEST_ASSERT_EQUAL((int)withDefault.monsters.size(), (int)withExplicitFalse.monsters.size());
    for (size_t i = 0; i < withDefault.monsters.size(); ++i) {
        TEST_ASSERT_FALSE(withDefault.monsters[i].isBoss);
        TEST_ASSERT_FALSE(withExplicitFalse.monsters[i].isBoss);
        TEST_ASSERT_EQUAL(withDefault.monsters[i].maxHp, withExplicitFalse.monsters[i].maxHp);
    }
}
```

Add matching `RUN_TEST(...)` lines for all four in `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: FAIL to compile — `'isBoss' is not a member of 'MonsterSpawn'` and `too many arguments to function 'makeZoneMap'`.

- [ ] **Step 3: Add `isBoss` to `MonsterSpawn` and update `makeZoneMap`'s declaration**

In `lib/core/zone_map.h`, change:

```cpp
struct MonsterSpawn {
    float x;             // patrol-center x, world units [0, arenaWidth)
    int platformIndex;   // which Platform (index into ZoneMap::platforms) this monster patrols on
    int maxHp;
    int damage;          // damage dealt to the player per attack landed
};
```

to:

```cpp
struct MonsterSpawn {
    float x;             // patrol-center x, world units [0, arenaWidth)
    int platformIndex;   // which Platform (index into ZoneMap::platforms) this monster patrols on
    int maxHp;
    int damage;          // damage dealt to the player per attack landed
    bool isBoss = false; // true for the single monster in a boss zone (see makeZoneMap)
};
```

And change the `makeZoneMap` declaration from:

```cpp
ZoneMap makeZoneMap(int realmIndex, int seed = 0);
```

to:

```cpp
// `isBossZone`: when true, skips the normal per-platform monster loop entirely and places one
// much-tougher MonsterSpawn (isBoss = true) on the last elevated platform instead — every other
// platform in the zone has no monsters. Terrain generation (platform count/gaps/widths/heights)
// is completely unaffected by this flag. Deciding *which* zoneRunIndex loops are boss zones is
// the caller's job (see zone_state.h's isBossZoneForRunIndex()), not this function's.
ZoneMap makeZoneMap(int realmIndex, int seed = 0, bool isBossZone = false);
```

- [ ] **Step 4: Implement the boss branch in `zone_map.cpp`**

Add the boss stat formula to the anonymous namespace, right after the existing `constexpr int kMaxDifficultyTier = 2;` block:

```cpp
// Boss stats: a single much-tougher monster in lieu of the platform-tiered roster (see
// makeZoneMap's isBossZone branch) - deliberately not derived from the tier formula above,
// since a boss isn't "one more tier," it's its own fight. First-pass numbers, to be tuned in
// Task 2 against zone_state's boss-clear-rate simulation test.
constexpr int kBossBaseHp = 300, kBossHpPerRealm = 120;
constexpr int kBossBaseDamage = 18, kBossDamagePerRealm = 9;

int bossMaxHp(int realmIndex) { return kBossBaseHp + kBossHpPerRealm * realmIndex; }
int bossDamage(int realmIndex) { return kBossBaseDamage + kBossDamagePerRealm * realmIndex; }
```

Change the function signature from:

```cpp
ZoneMap makeZoneMap(int realmIndex, int seed) {
```

to:

```cpp
ZoneMap makeZoneMap(int realmIndex, int seed, bool isBossZone) {
```

Insert the boss branch right after `m.arenaWidth = m.platforms.back().x1;` and before the existing `int baseHp = 30 + 20 * realmIndex;` line:

```cpp
    m.arenaWidth = m.platforms.back().x1;

    if (isBossZone) {
        int bossPlatformIndex = numElevated;
        const Platform& p = m.platforms[static_cast<size_t>(bossPlatformIndex)];
        MonsterSpawn boss;
        boss.x = (p.x0 + p.x1) / 2.0f; // plain midpoint - a boss is one deliberate encounter,
                                        // not part of the jittered multi-monster placement system
        boss.platformIndex = bossPlatformIndex;
        boss.maxHp = bossMaxHp(realmIndex);
        boss.damage = bossDamage(realmIndex);
        boss.isBoss = true;
        m.monsters.push_back(boss);
        return m;
    }

    int baseHp = 30 + 20 * realmIndex;
```

The rest of the function (the existing per-platform loop and final `return m;`) is unchanged.

- [ ] **Step 5: Run tests to verify they pass**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: PASS (all cases, including the four new ones).

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite.

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_map.h lib/core/zone_map.cpp test/test_zone_map/test_zone_map.cpp
git commit -m "feat: add boss zone generation to zone_map"
```

---

## Task 2: Boss combat, enrage, and reward (`lib/core/zone_state.h`/`.cpp`)

**Files:**
- Modify: `lib/core/zone_state.h`
- Modify: `lib/core/zone_state.cpp`
- Test: `test/test_zone_state/test_zone_state.cpp`

**Interfaces:**
- Consumes: `MonsterSpawn::isBoss`, `makeZoneMap(realmIndex, seed, isBossZone)` (Task 1).
- Produces: `constexpr int kBossZoneInterval = 3`; `bool isBossZoneForRunIndex(int zoneRunIndex)`; `ZoneState::currentEncounterIsBoss`, `::bossEnraged`, `::bossJustEnraged`, `::bossJustDefeated` (all `bool`) — consumed by Task 5's `ui.cpp` (`currentEncounterIsBoss`) and Task 6's `main.cpp` (`isBossZoneForRunIndex`, `bossJustEnraged`, `bossJustDefeated`).

- [ ] **Step 1: Write the failing tests**

Add to `test/test_zone_state/test_zone_state.cpp` (before `int main`):

```cpp
void test_is_boss_zone_for_run_index_matches_every_third_loop(void) {
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(0));
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(1));
    TEST_ASSERT_TRUE(isBossZoneForRunIndex(2));
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(3));
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(4));
    TEST_ASSERT_TRUE(isBossZoneForRunIndex(5));
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(6));
    TEST_ASSERT_FALSE(isBossZoneForRunIndex(7));
    TEST_ASSERT_TRUE(isBossZoneForRunIndex(8));
}

void test_restart_zone_builds_a_boss_zone_on_the_right_loop(void) {
    ZoneState s = startZone(makeZoneMap(0), 0); // zoneRunIndex 0
    restartZone(s, 0); // -> zoneRunIndex 1, not a boss zone
    TEST_ASSERT_FALSE(s.map.monsters[0].isBoss);
    restartZone(s, 0); // -> zoneRunIndex 2, a boss zone
    TEST_ASSERT_EQUAL_INT(2, s.zoneRunIndex);
    TEST_ASSERT_EQUAL_INT(1, (int)s.map.monsters.size());
    TEST_ASSERT_TRUE(s.map.monsters[0].isBoss);
}

// Mirrors how the existing skill tests force specific HP values rather than waiting out real
// damage - forces the boss to exactly the enrage threshold, then confirms it fires exactly once.
void test_boss_enrage_triggers_once_at_half_hp(void) {
    ZoneMap m = makeZoneMap(0, 0, true);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    TEST_ASSERT_TRUE(s.currentEncounterIsBoss);

    float cooldownBeforeEnrage = s.enemy.attackCooldownSeconds;
    s.enemy.hp = s.enemy.maxHp / 2; // exactly at the threshold
    tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.bossEnraged);
    TEST_ASSERT_TRUE(s.bossJustEnraged);
    TEST_ASSERT_TRUE(s.enemy.attackCooldownSeconds < cooldownBeforeEnrage);

    // A further tick must NOT re-trigger (latched) or shorten the cooldown a second time.
    float cooldownAfterEnrage = s.enemy.attackCooldownSeconds;
    tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_FALSE(s.bossJustEnraged);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, cooldownAfterEnrage, s.enemy.attackCooldownSeconds);
}

void test_boss_defeat_grants_bonus_reward_and_flags_the_tick(void) {
    ZoneMap m = makeZoneMap(0, 0, true);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.enemy.hp = 1; // dies on the next landed autoattack
    bool sawDefeatFlag = false;
    for (int i = 0; i < 50 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
        if (s.bossJustDefeated) sawDefeatFlag = true;
    }
    TEST_ASSERT_TRUE(sawDefeatFlag);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 10.0, s.qiRewardPending); // bonus staged; zone-clear reward comes later
}

// End-to-end: a boss zone's total payout is double a regular zone's (bonus at the kill + the
// zone's own clear reward, both accumulated via `+=`), while a regular zone's total is
// unchanged at exactly proposedReward - the regression check on the `=` -> `+=` change.
void test_boss_zone_total_reward_is_double_a_regular_zones(void) {
    ZoneState boss = startZone(makeZoneMap(15, 0, true), 15);
    for (int i = 0; i < 5000 && boss.phase != ZonePhase::Cleared; ++i) {
        tickZone(boss, 0.1, 42.0, 15);
    }
    TEST_ASSERT_TRUE(boss.phase == ZonePhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 84.0, boss.qiRewardPending);

    ZoneState regular = startZone(makeZoneMap(15, 0, false), 15);
    for (int i = 0; i < 5000 && regular.phase != ZonePhase::Cleared; ++i) {
        tickZone(regular, 0.1, 42.0, 15);
    }
    TEST_ASSERT_TRUE(regular.phase == ZonePhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, regular.qiRewardPending);
}

void test_player_defeat_mid_boss_fight_resets_boss_state(void) {
    ZoneMap m = makeZoneMap(0, 0, true);
    ZoneState s = startZone(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    TEST_ASSERT_TRUE(s.currentEncounterIsBoss);
    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickZone(s, 0.1, 10.0, 0);
        if (s.phase == ZonePhase::Walking) { resetDetected = true; break; }
    }
    TEST_ASSERT_TRUE(resetDetected);
    TEST_ASSERT_FALSE(s.currentEncounterIsBoss);
    TEST_ASSERT_FALSE(s.bossEnraged);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
}

// Simulation-style balance check, mirroring the existing clearedOnFirstAttemptCount helper below
// but for boss zones. A boss is deliberately harder than a regular zone (~100% clear rate) but
// still meant to be beatable most of the time, not a wall - targets roughly 70-80%. If either of
// the next two tests fails once real numbers are in, retune kBossBaseHp/kBossHpPerRealm/
// kBossBaseDamage/kBossDamagePerRealm (zone_map.cpp) or kBossEnrageCooldownMultiplier (below) -
// not these tests - until back in range.
int bossClearedOnFirstAttemptCount(int realm, int trials) {
    int cleared = 0;
    for (int seed = 0; seed < trials; ++seed) {
        ZoneState s = startZone(makeZoneMap(realm, seed, /*isBossZone=*/true), realm, seed);
        for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared && s.zoneRunIndex == seed; ++i) {
            tickZone(s, 0.1, 42.0, realm);
        }
        if (s.phase == ZonePhase::Cleared && s.zoneRunIndex == seed) cleared++;
    }
    return cleared;
}

void test_boss_zone_clear_rate_is_challenging_but_reasonable_at_low_realm(void) {
    constexpr int kTrials = 30;
    int cleared = bossClearedOnFirstAttemptCount(1, kTrials);
    TEST_ASSERT_TRUE(cleared >= kTrials * 7 / 10);
}

void test_boss_zone_clear_rate_is_challenging_but_reasonable_at_high_realm(void) {
    constexpr int kTrials = 30;
    int cleared = bossClearedOnFirstAttemptCount(15, kTrials);
    TEST_ASSERT_TRUE(cleared >= kTrials * 7 / 10);
}
```

Add matching `RUN_TEST(...)` lines for all eight new test functions (not the `bossClearedOnFirstAttemptCount` helper) in `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: FAIL to compile — `'isBossZoneForRunIndex' was not declared`, `'currentEncounterIsBoss' is not a member of 'ZoneState'`, etc.

- [ ] **Step 3: Add the boss fields/constants to `zone_state.h`**

Add four fields to the end of the `ZoneState` struct (after `int zoneRunIndex = 0;`, before the closing `};`):

```cpp
    int zoneRunIndex = 0;         // seed `map` was built with; restartZone() bumps this so looping
                                   // the same realm doesn't keep regenerating the same layout
    bool currentEncounterIsBoss = false; // set at the Walking->Fighting transition from the
                                          // engaged spawn's isBoss; cleared when that encounter ends
    bool bossEnraged = false;            // latches true once, never clears mid-fight (boss never heals)
    bool bossJustEnraged = false;        // pulses true on the single tickZone() call enrage triggers;
                                          // reset every call, same contract as skillFiredThisTick
    bool bossJustDefeated = false;       // pulses true on the single tickZone() call a boss dies;
                                          // reset every call
};
```

Right after the `ZoneState` struct's closing `};` and before the `startZone` declaration, add:

```cpp
constexpr int kBossZoneInterval = 3; // every Nth zone loop is a boss zone
constexpr float kBossEnrageCooldownMultiplier = 0.7f; // ~43% faster attacks once enraged

// True for the 3rd, 6th, 9th... zone loop (1-indexed) - i.e. zoneRunIndex values 2, 5, 8, ....
// zoneRunIndex 0 (the very first zone of a session) is never a boss zone. Callers building a
// ZoneMap (main.cpp's boot call, restartZone() below) use this to decide the isBossZone argument
// to makeZoneMap() - makeZoneMap() itself has no opinion on which loops are boss zones.
bool isBossZoneForRunIndex(int zoneRunIndex);
```

- [ ] **Step 4: Implement `isBossZoneForRunIndex` and thread it through `startZone`/`restartZone` in `zone_state.cpp`**

Add the function definition right before `ZoneState startZone(...)`:

```cpp
bool isBossZoneForRunIndex(int zoneRunIndex) {
    return (zoneRunIndex + 1) % kBossZoneInterval == 0;
}
```

In `startZone`, add explicit resets for the four new fields right after `s.zoneRunIndex = zoneRunIndex;`:

```cpp
    s.zoneRunIndex = zoneRunIndex;
    s.currentEncounterIsBoss = false;
    s.bossEnraged = false;
    s.bossJustEnraged = false;
    s.bossJustDefeated = false;
    return s;
```

Change `restartZone` from:

```cpp
void restartZone(ZoneState& state, int currentRealmIndex) {
    int nextRunIndex = state.zoneRunIndex + 1;
    state = startZone(makeZoneMap(currentRealmIndex, nextRunIndex), currentRealmIndex, nextRunIndex);
}
```

to:

```cpp
void restartZone(ZoneState& state, int currentRealmIndex) {
    int nextRunIndex = state.zoneRunIndex + 1;
    bool boss = isBossZoneForRunIndex(nextRunIndex);
    state = startZone(makeZoneMap(currentRealmIndex, nextRunIndex, boss), currentRealmIndex, nextRunIndex);
}
```

- [ ] **Step 5: Implement enrage and boss reward in `tickZone`**

Change the top of `tickZone` from:

```cpp
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    state.skillFiredThisTick = -1; // reset every call - a caller inspects this immediately after
    if (state.phase == ZonePhase::Cleared) return;
```

to:

```cpp
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    state.skillFiredThisTick = -1; // reset every call - a caller inspects this immediately after
    state.bossJustEnraged = false; // same reset-every-call contract as skillFiredThisTick above
    state.bossJustDefeated = false;
    if (state.phase == ZonePhase::Cleared) return;
```

In the Walking branch, change the encounter-transition block from:

```cpp
        int engaged = findUndefeatedMonsterInRangeOnPlatform(state, state.currentPlatformIndex);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }
```

to:

```cpp
        int engaged = findUndefeatedMonsterInRangeOnPlatform(state, state.currentPlatformIndex);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            state.currentEncounterIsBoss = spawn.isBoss;
            return;
        }
```

Still in the Walking branch, change the Cleared transition's reward line from:

```cpp
                    state.phase = ZonePhase::Cleared;
                    state.qiRewardPending = proposedReward;
```

to:

```cpp
                    state.phase = ZonePhase::Cleared;
                    state.qiRewardPending += proposedReward; // was `=` - `+=` so a boss bonus staged
                                                               // earlier in this zone isn't clobbered;
                                                               // safe for non-boss zones too, since
                                                               // qiRewardPending is always 0.0 here
```

Finally, change the Fighting branch from:

```cpp
    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    int firedSkill = tickSkill(state.skill, dtSeconds, currentRealmIndex);
    if (firedSkill >= 0) {
        state.skillFiredThisTick = firedSkill;
        int skillDamage = static_cast<int>(state.player.attackDamage * SKILLS[firedSkill].damageMultiplier);
        state.enemy.hp -= skillDamage;
        if (state.enemy.hp < 0) state.enemy.hp = 0;
    }
    // Player defeat is checked FIRST: tickCombat() can land both attacks in the same call
    // whenever dtSeconds is large enough to cross both combatants' attack cooldowns at once (a
    // real occurrence here - see main.cpp's SFX delay() calls, which inflate the next loop()
    // iteration's dt). If both happened to drop to 0 HP on the same tick, checking the enemy
    // first would silently credit a win and leave the player's own defeat unhandled - the
    // character would keep walking/fighting at 0 HP until the next encounter's combat happens
    // to re-check isDefeated(player) on its own. Checking the player first means a simultaneous
    // double-KO is always a loss (and restarts the zone), never a masked win.
    if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    } else if (isDefeated(state.enemy)) {
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.phase = ZonePhase::Walking;
        // A zone can roll several monsters per platform now, not always exactly 3 - full-healing
        // on every kill keeps each fight its own "can I beat this one enemy" test (this module's
        // original design intent) instead of chip damage accumulating across the whole run.
        state.player.hp = state.player.maxHp;
    }
}
```

to:

```cpp
    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    int firedSkill = tickSkill(state.skill, dtSeconds, currentRealmIndex);
    if (firedSkill >= 0) {
        state.skillFiredThisTick = firedSkill;
        int skillDamage = static_cast<int>(state.player.attackDamage * SKILLS[firedSkill].damageMultiplier);
        state.enemy.hp -= skillDamage;
        if (state.enemy.hp < 0) state.enemy.hp = 0;
    }

    // Boss enrage: a one-time, HP-threshold escalation - checked after this tick's damage so it
    // can trigger the same tick a skill/autoattack crosses the 50% line, latched via bossEnraged
    // so it can never fire twice for the same encounter (a boss never heals).
    if (state.currentEncounterIsBoss && !state.bossEnraged &&
        state.enemy.hp > 0 && state.enemy.hp <= state.enemy.maxHp / 2) {
        state.enemy.attackCooldownSeconds *= kBossEnrageCooldownMultiplier;
        state.bossEnraged = true;
        state.bossJustEnraged = true;
    }

    // Player defeat is checked FIRST: tickCombat() can land both attacks in the same call
    // whenever dtSeconds is large enough to cross both combatants' attack cooldowns at once (a
    // real occurrence here - see main.cpp's SFX delay() calls, which inflate the next loop()
    // iteration's dt). If both happened to drop to 0 HP on the same tick, checking the enemy
    // first would silently credit a win and leave the player's own defeat unhandled - the
    // character would keep walking/fighting at 0 HP until the next encounter's combat happens
    // to re-check isDefeated(player) on its own. Checking the player first means a simultaneous
    // double-KO is always a loss (and restarts the zone), never a masked win.
    if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    } else if (isDefeated(state.enemy)) {
        if (state.currentEncounterIsBoss) {
            // Bonus reward, on top of the zone's own clear reward staged at the Cleared
            // transition above (now `+=`, not `=`, so this isn't clobbered).
            state.qiRewardPending += proposedReward;
            state.bossJustDefeated = true;
        }
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.currentEncounterIsBoss = false;
        state.bossEnraged = false;
        state.phase = ZonePhase::Walking;
        // A zone can roll several monsters per platform now, not always exactly 3 - full-healing
        // on every kill keeps each fight its own "can I beat this one enemy" test (this module's
        // original design intent) instead of chip damage accumulating across the whole run.
        state.player.hp = state.player.maxHp;
    }
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: PASS (all cases). If either `test_boss_zone_clear_rate_is_challenging_but_reasonable_at_*_realm` case fails, adjust `kBossBaseHp`/`kBossHpPerRealm`/`kBossBaseDamage`/`kBossDamagePerRealm` (in `zone_map.cpp`, from Task 1) or `kBossEnrageCooldownMultiplier` (this file) — weaken the boss if the clear rate is too low, strengthen it if implausibly high (e.g. >95%, which would mean the boss isn't harder than a regular zone at all) — then rerun this step until both land in `[70%, 100%]`.

- [ ] **Step 7: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite — in particular, `test_clearing_all_monsters_and_reaching_end_sets_reward` in `test_zone_state` (a non-boss zone) must still see exactly `42.0`, not `84.0`, confirming the `=`→`+=` change is safe for regular zones.

- [ ] **Step 8: Commit**

```bash
git add lib/core/zone_state.h lib/core/zone_state.cpp test/test_zone_state/test_zone_state.cpp
git commit -m "feat: add boss enrage and bonus reward to zone_state"
```

---

## Task 3: Boss color (`lib/core/zone_textures.h`/`.cpp`)

**Files:**
- Modify: `lib/core/zone_textures.h`
- Modify: `lib/core/zone_textures.cpp`
- Test: `test/test_zone_textures/test_zone_textures.cpp`

**Interfaces:**
- Consumes: nothing new (uses the existing `hashUnitFloat`/`hsvToRgb`/`realmHue` already in this file).
- Produces: `RGB bossColor(int realmIndex)` — consumed by Task 4's `zone_view.cpp`.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_zone_textures/test_zone_textures.cpp` (before `int main`):

```cpp
void test_boss_color_is_deterministic(void) {
    RGB a = bossColor(5);
    RGB b = bossColor(5);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_boss_color_differs_across_realms(void) {
    RGB a = bossColor(0);
    RGB b = bossColor(8);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_boss_color_is_darker_than_the_toughest_regular_tier(void) {
    RGB boss = bossColor(5);
    RGB tier2 = monsterColor(5, 2);
    int bossSum = boss.r + boss.g + boss.b;
    int tier2Sum = tier2.r + tier2.g + tier2.b;
    TEST_ASSERT_TRUE(bossSum < tier2Sum);
}
```

Add matching `RUN_TEST(...)` lines for all three in `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: FAIL to compile — `'bossColor' was not declared in this scope`.

- [ ] **Step 3: Declare and implement `bossColor`**

Add to `lib/core/zone_textures.h`, after the `monsterColor` declaration:

```cpp
// Boss body color for a realm's zone - darker and more saturated than any regular monsterColor
// tier (even tier 2, the toughest), so a boss reads as visually distinct at a glance. Same
// "opposite the background hue" family as monsterColor. Deterministic.
RGB bossColor(int realmIndex);
```

Add to `lib/core/zone_textures.cpp`, after the `monsterColor` definition:

```cpp
RGB bossColor(int realmIndex) {
    constexpr int kBossColorSalt = 99; // distinct from monsterColor's tierIndex-keyed salts (0,1,2)
    float hueJitter = (hashUnitFloat(realmIndex, kBossColorSalt) - 0.5f) * 20.0f; // +-10 degrees,
                                                                                   // same range monsterColor uses
    float hue = realmHue(realmIndex) + 180.0f + hueJitter; // opposite the background hue, same family as monsterColor
    return hsvToRgb(hue, 1.0f, 0.35f); // more saturated, darker than monsterColor's toughest tier (tier 2: sat 0.9, value 0.5)
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: PASS (all cases).

- [ ] **Step 5: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite.

- [ ] **Step 6: Commit**

```bash
git add lib/core/zone_textures.h lib/core/zone_textures.cpp test/test_zone_textures/test_zone_textures.cpp
git commit -m "feat: add boss body color to zone_textures"
```

---

## Task 4: Boss silhouette and FX/SFX (`src/zone_view.h`/`.cpp`)

**Files:**
- Modify: `src/zone_view.h`
- Modify: `src/zone_view.cpp`

**Interfaces:**
- Consumes: `MonsterSpawn::isBoss` (Task 1), `bossColor(int realmIndex)` (Task 3).
- Produces: `void triggerBossEnrageFx()`, `void playBossEnrageSfx()`, `void triggerBossDefeatFx()`, `void playBossDefeatSfx()` — consumed by Task 6's `main.cpp`.

This task lives entirely in `src/`, which the `native` PlatformIO environment excludes from its build (`build_src_filter = -<*>` in `platformio.ini`'s `[env:native]`), so there is no native unit test for it — verification is the hardware build.

- [ ] **Step 1: Add the boss silhouette to `drawMonster`**

In `src/zone_view.cpp`, change the `drawMonster` signature from:

```cpp
void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent, int tierIndex) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);

    if (tierIndex <= 0) {
```

to:

```cpp
void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent, int tierIndex,
                  bool isBoss) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);

    if (isBoss) {
        // Boss: biggest body plus a gold spike-crown across the top - visually distinct from all
        // three regular tiers at a glance, reusing the same fillTriangle spike technique tier 1
        // already uses below.
        int bigRadius = radius + 14;
        int cy = standY - bigRadius;
        canvas.fillCircle(screenX, cy, bigRadius, fill);
        constexpr int kCrownSpikes = 5;
        int crownBaseY = cy - bigRadius + 4;
        int crownSpan = bigRadius;
        for (int s = 0; s < kCrownSpikes; ++s) {
            int spikeX = screenX - crownSpan + (2 * crownSpan * s) / (kCrownSpikes - 1);
            canvas.fillTriangle(spikeX - 4, crownBaseY, spikeX + 4, crownBaseY, spikeX, crownBaseY - 14, TFT_GOLD);
        }
        radius = bigRadius; // so the eyes/current-ring below sit correctly on the enlarged body
    } else if (tierIndex <= 0) {
```

The `else if (tierIndex == 1)` and the final `else { // Tier 2 ... }` branches, and everything from `int eyeY = standY - radius;` to the end of the function, are unchanged.

Update the one call site inside `renderZoneView`, from:

```cpp
        int visualTier = (spawn.platformIndex - 1) % 3;
        RGB color = monsterColor(state.map.realmIndex, visualTier);
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent, visualTier);
```

to:

```cpp
        int visualTier = (spawn.platformIndex - 1) % 3;
        RGB color = spawn.isBoss ? bossColor(state.map.realmIndex) : monsterColor(state.map.realmIndex, visualTier);
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent, visualTier, spawn.isBoss);
```

- [ ] **Step 2: Add enrage/defeat FX state and drawing**

In `src/zone_view.cpp`'s anonymous namespace, add after the existing `gBreakthroughFxActive`/`gBreakthroughFxStartMs`/`kBreakthroughFxDurationMs`/`kBreakthroughRays`/`kBreakthroughMaxRadiusPx` block:

```cpp
// Boss enrage flash/shake state - a single trigger call latches both; renderZoneView() derives
// the current frame from elapsed time, same pattern the skill FX above already uses.
bool gBossEnrageFxActive = false;
uint32_t gBossEnrageFxStartMs = 0;
constexpr uint32_t kBossEnrageFxDurationMs = 260;
constexpr float kBossEnrageShakeAmplitudePx = 5.0f; // more than double kShakeAmplitudePx's 2.0f -
                                                     // a bigger jolt for a boss-only event

// Boss defeat celebration burst state - visually similar to the breakthrough celebration above
// (same expanding-ring-plus-rays technique) but its own state/color so a boss kill never reads
// as identical to a realm breakthrough.
bool gBossDefeatFxActive = false;
uint32_t gBossDefeatFxStartMs = 0;
constexpr uint32_t kBossDefeatFxDurationMs = 900; // longer than kBreakthroughFxDurationMs's 800ms
constexpr int kBossDefeatRays = 10;
constexpr float kBossDefeatMaxRadiusPx = 80.0f;
```

Add two drawing functions right after `drawBreakthroughFx`, still inside the anonymous namespace:

```cpp
// A brief red flash on the boss plus a bigger screen shake than a skill impact's - the mid-fight
// enrage escalation is a bigger event than a regular hit, so it reads as one.
void drawBossEnrageFx(M5Canvas& canvas, uint32_t nowMs, float& shakeX, float& shakeY) {
    if (!gBossEnrageFxActive) return;
    uint32_t elapsed = nowMs - gBossEnrageFxStartMs;
    if (elapsed >= kBossEnrageFxDurationMs) { gBossEnrageFxActive = false; return; }
    drawFlash(canvas, gLastEnemyScreenX, gLastEnemyScreenY, nowMs, gBossEnrageFxStartMs + kBossEnrageFxDurationMs,
              14, 22, TFT_RED, TFT_MAROON);
    float t = static_cast<float>(elapsed) / static_cast<float>(kBossEnrageFxDurationMs);
    shakeX += shakeOffset(t, kBossEnrageShakeAmplitudePx, 0.0f);
    shakeY += shakeOffset(t, kBossEnrageShakeAmplitudePx, kPi / 2.0f);
}

// An expanding red/gold ring plus radiating rays centered on the boss's last position - a bigger,
// differently-colored cousin of drawBreakthroughFx above, for a boss kill.
void drawBossDefeatFx(M5Canvas& canvas, uint32_t nowMs) {
    if (!gBossDefeatFxActive) return;
    uint32_t elapsed = nowMs - gBossDefeatFxStartMs;
    if (elapsed >= kBossDefeatFxDurationMs) { gBossDefeatFxActive = false; return; }

    float t = static_cast<float>(elapsed) / static_cast<float>(kBossDefeatFxDurationMs);
    float envelope = pulseEnvelope(t);
    int cx = gLastEnemyScreenX;
    int cy = gLastEnemyScreenY - 20;
    int ringRadius = static_cast<int>(kBossDefeatMaxRadiusPx * t);

    canvas.drawCircle(cx, cy, ringRadius, TFT_RED);
    if (ringRadius > 3) canvas.drawCircle(cx, cy, ringRadius - 3, TFT_GOLD);

    int rayLen = ringRadius + static_cast<int>(kBossDefeatMaxRadiusPx * 0.4f * envelope);
    for (int i = 0; i < kBossDefeatRays; ++i) {
        float angle = (2.0f * kPi / static_cast<float>(kBossDefeatRays)) * static_cast<float>(i);
        int ex = cx + static_cast<int>(std::cos(angle) * rayLen);
        int ey = cy + static_cast<int>(std::sin(angle) * rayLen);
        canvas.drawLine(cx, cy, ex, ey, TFT_ORANGE);
    }
}
```

- [ ] **Step 3: Wire the new drawing into `renderZoneView`**

Change:

```cpp
    drawLootPop(canvas, nowMs);
    drawBreakthroughFx(canvas, charX, charY, nowMs);

    uint32_t skillElapsed = nowMs - gSkillFxStartMs;
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
```

to:

```cpp
    drawLootPop(canvas, nowMs);
    drawBreakthroughFx(canvas, charX, charY, nowMs);
    drawBossDefeatFx(canvas, nowMs);

    uint32_t skillElapsed = nowMs - gSkillFxStartMs;
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    drawBossEnrageFx(canvas, nowMs, shakeX, shakeY);
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
```

- [ ] **Step 4: Add the trigger/SFX functions**

Add to `src/zone_view.cpp`, after `playBreakthroughSfx()`:

```cpp
void triggerBossEnrageFx() {
    gBossEnrageFxActive = true;
    gBossEnrageFxStartMs = millis();
}

void playBossEnrageSfx() {
    // A harsh low-to-high snarl, distinct from the plain single-tone playHitSfx()/playAttackSfx().
    M5.Speaker.tone(150.0f, 90);
    delay(60);
    M5.Speaker.tone(300.0f, 120);
}

void triggerBossDefeatFx() {
    gBossDefeatFxActive = true;
    gBossDefeatFxStartMs = millis();
}

void playBossDefeatSfx() {
    // A short triumphant sting distinct from playVictorySfx()'s zone-clear jingle and
    // playBreakthroughSfx()'s rising arpeggio.
    M5.Speaker.tone(784.0f, 100);
    delay(90);
    M5.Speaker.tone(587.0f, 80);
    delay(80);
    M5.Speaker.tone(1046.0f, 220);
}
```

Add the matching declarations to `src/zone_view.h`, after `playBreakthroughSfx();`:

```cpp
// Boss-only events: a brief red flash + bigger shake for the mid-fight enrage threshold, and a
// bigger red/gold ring-and-rays burst (distinct from triggerRealmBreakthroughFx()'s white/gold)
// for a boss kill - main.cpp fires these alongside its existing monsterDefeated/skillFired
// detection, gated on ZoneState::bossJustEnraged/bossJustDefeated.
void triggerBossEnrageFx();
void playBossEnrageSfx();
void triggerBossDefeatFx();
void playBossDefeatSfx();
```

- [ ] **Step 5: Build to verify it compiles**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: build succeeds with no errors.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite (this task touches no `lib/core` file, so this is a pure regression check).

- [ ] **Step 7: Commit**

```bash
git add src/zone_view.h src/zone_view.cpp
git commit -m "feat: add boss silhouette, colors, and enrage/defeat FX"
```

---

## Task 5: Boss HUD treatment (`src/ui.cpp`)

**Files:**
- Modify: `src/ui.cpp`

**Interfaces:**
- Consumes: `ZoneState::currentEncounterIsBoss` (Task 2).
- Produces: nothing new exported — `drawHud`'s signature is unchanged.

- [ ] **Step 1: Add boss bar colors**

In `src/ui.cpp`, add after `constexpr uint16_t kEnemyHpGloss = lgfx::color565(210, 90, 140);`:

```cpp
constexpr uint16_t kBossHpColor = lgfx::color565(90, 10, 90);   // deep crimson-purple - reads as
                                                                 // more alarming than a regular enemy
constexpr uint16_t kBossHpGloss = lgfx::color565(200, 80, 200);
```

- [ ] **Step 2: Branch the enemy bar's label and colors on `currentEncounterIsBoss`**

Change:

```cpp
    bool fighting = (zone.phase == ZonePhase::Fighting);
    float enemyFraction = (fighting && zone.enemy.maxHp > 0)
        ? static_cast<float>(zone.enemy.hp) / static_cast<float>(zone.enemy.maxHp)
        : 0.0f;
    char enemyLabel[32];
    if (fighting) {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP %d/%d", zone.enemy.hp, zone.enemy.maxHp);
    } else {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP --");
    }
    drawBar(panel, columnRect(row, 1), enemyFraction, kEnemyHpColor, kEnemyHpGloss, enemyLabel, 2,
            IconKind::Skull);
```

to:

```cpp
    bool fighting = (zone.phase == ZonePhase::Fighting);
    bool fightingBoss = fighting && zone.currentEncounterIsBoss;
    float enemyFraction = (fighting && zone.enemy.maxHp > 0)
        ? static_cast<float>(zone.enemy.hp) / static_cast<float>(zone.enemy.maxHp)
        : 0.0f;
    char enemyLabel[32];
    if (fightingBoss) {
        snprintf(enemyLabel, sizeof(enemyLabel), "BOSS HP %d/%d", zone.enemy.hp, zone.enemy.maxHp);
    } else if (fighting) {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP %d/%d", zone.enemy.hp, zone.enemy.maxHp);
    } else {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP --");
    }
    drawBar(panel, columnRect(row, 1), enemyFraction,
            fightingBoss ? kBossHpColor : kEnemyHpColor, fightingBoss ? kBossHpGloss : kEnemyHpGloss,
            enemyLabel, 2, IconKind::Skull);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: build succeeds with no errors.

- [ ] **Step 4: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite.

- [ ] **Step 5: Commit**

```bash
git add src/ui.cpp
git commit -m "feat: show a BOSS HP bar treatment during boss fights"
```

---

## Task 6: Wire boss zones and FX into the game loop (`src/main.cpp`)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `isBossZoneForRunIndex(int)` (Task 2), `ZoneState::bossJustEnraged`/`::bossJustDefeated` (Task 2), `triggerBossEnrageFx()`/`playBossEnrageSfx()`/`triggerBossDefeatFx()`/`playBossDefeatSfx()` (Task 4).
- Produces: nothing new exported — this is the final integration task.

- [ ] **Step 1: Thread the boss flag through the boot-time zone**

Change:

```cpp
    gZoneState = startZone(makeZoneMap(gState.realmIndex), gState.realmIndex);
```

to:

```cpp
    // isBossZoneForRunIndex(0) is always false (see zone_state.h) - spelled out explicitly
    // rather than relied upon, so this boot call stays correct if kBossZoneInterval is ever
    // retuned to 1.
    gZoneState = startZone(makeZoneMap(gState.realmIndex, 0, isBossZoneForRunIndex(0)), gState.realmIndex);
```

- [ ] **Step 2: Fire boss FX/SFX off the new one-tick event flags**

Change:

```cpp
    bool zoneRestarted = wasFighting && gZoneState.player.hp > playerHpBefore;
    bool enemyHit = wasFighting && !zoneRestarted && gZoneState.enemy.hp < enemyHpBefore;
    bool skillFired = wasFighting && gZoneState.skillFiredThisTick >= 0;
    // tickZone() only ever leaves Fighting for Walking via one of two paths: the enemy was just
    // defeated, or (zoneRestarted) the player was - excluding the latter leaves exactly "a kill
    // happened this tick", the same way the enemyHit/skillFired checks above already lean on
    // exact before/after ZoneState comparisons instead of a dedicated event flag.
    bool monsterDefeated = wasFighting && !zoneRestarted && gZoneState.phase == ZonePhase::Walking;
    if (enemyHit) {
        if (!skillFired) playAttackSfx(); // skip the plain-hit tone when the skill's own SFX will play this tick
        triggerAttackFlash();
        spawnDamageNumber(false, enemyHpBefore - gZoneState.enemy.hp, skillFired ? gZoneState.skillFiredThisTick : -1);
    }
    if (monsterDefeated) {
        triggerLootPop();
        playLootSfx();
    }
    if (skillFired) {
        triggerSkillFx(gZoneState.skillFiredThisTick);
        playSkillSfx(gZoneState.skillFiredThisTick);
    }
```

to:

```cpp
    bool zoneRestarted = wasFighting && gZoneState.player.hp > playerHpBefore;
    bool enemyHit = wasFighting && !zoneRestarted && gZoneState.enemy.hp < enemyHpBefore;
    bool skillFired = wasFighting && gZoneState.skillFiredThisTick >= 0;
    bool bossEnrageTriggered = wasFighting && gZoneState.bossJustEnraged;
    bool bossDefeated = wasFighting && !zoneRestarted && gZoneState.bossJustDefeated;
    // tickZone() only ever leaves Fighting for Walking via one of two paths: the enemy was just
    // defeated, or (zoneRestarted) the player was - excluding the latter leaves exactly "a kill
    // happened this tick", the same way the enemyHit/skillFired checks above already lean on
    // exact before/after ZoneState comparisons instead of a dedicated event flag.
    bool monsterDefeated = wasFighting && !zoneRestarted && gZoneState.phase == ZonePhase::Walking;
    if (enemyHit) {
        if (!skillFired) playAttackSfx(); // skip the plain-hit tone when the skill's own SFX will play this tick
        triggerAttackFlash();
        spawnDamageNumber(false, enemyHpBefore - gZoneState.enemy.hp, skillFired ? gZoneState.skillFiredThisTick : -1);
    }
    if (bossEnrageTriggered) {
        triggerBossEnrageFx();
        playBossEnrageSfx();
    }
    if (monsterDefeated) {
        triggerLootPop();
        playLootSfx();
        if (bossDefeated) {
            triggerBossDefeatFx();
            playBossDefeatSfx();
        }
    }
    if (skillFired) {
        triggerSkillFx(gZoneState.skillFiredThisTick);
        playSkillSfx(gZoneState.skillFiredThisTick);
    }
```

- [ ] **Step 3: Build to verify it compiles**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: build succeeds with no errors.

- [ ] **Step 4: Run the full native suite to confirm the whole feature is regression-free end to end**

Run: `python3 -m platformio test -e native`
Expected: PASS across every suite.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire boss zones into the boot call and game loop FX"
```

---

## Follow-up (not part of this plan)

Flashing and visual/audio/balance confirmation on real hardware remains open, same bar every prior spec/plan in this repo has carried — the boss's HP/damage/enrage constants are tuned against the native simulation test in Task 2, not a real device.
