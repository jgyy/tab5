# MapleStory-Style Idle Zone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the raycasting Secret Realm trial and replace it with a 2D MapleStory-style idle combat scene (one flat side-view "zone" per cultivation realm, autoplaying), expand the cultivation realms from 7 to 16, and add diagnostics for the unresponsive brightness/volume touch controls.

**Architecture:** The cultivation economy (`economy`/`save`/`offline_earnings`) is untouched except two data tables. A new `zone_map`/`zone_state`/`zone_textures` trio (hardware-agnostic, `lib/core/`, unit-tested natively) replaces `raycast`/`trial_map`/`trial_state`/`trial_textures`; `zone_combat` is `trial_combat` renamed with unchanged logic. A new `zone_view` (hardware-dependent, `src/`) replaces `trial_view`, drawing directly onto a viewport-sized `M5Canvas` with primitive fills instead of a raycast pixel buffer. The old and new stacks briefly coexist so every task keeps the build green; a final cutover task swaps `main.cpp`/`ui.cpp` over and deletes the entire old stack in one commit.

**Tech Stack:** C++ (Arduino framework), PlatformIO, M5Unified/M5GFX, Unity test framework (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-08-27-maplestory-idle-zone-design.md`

## Global Constraints

- No changes to `economy.{h,cpp}`'s formulas, `save.{h,cpp}`'s schema, or `offline_earnings.{h,cpp}` — only `NUM_REALMS`, `REALM_NAMES`, and `REALM_QI_THRESHOLD` grow (7 → 16 realms).
- No new generators.
- No manual/touch control of the character — autoplay only, exactly like today's trial.
- All character/monster/background art is procedural (M5Canvas primitives / deterministic hash-color functions) — no imported image or audio assets.
- Every task must leave both `pio test -e native` and `pio run -e esp32p4_pioarduino` green before its commit (the old raycasting stack and the new zone stack coexist until the final cutover task, specifically so this holds at every intermediate commit).

---

## Task 1: Expand cultivation realms from 7 to 16

**Files:**
- Modify: `lib/core/realms.h`
- Modify: `lib/core/economy.cpp`
- Modify: `test/test_economy/test_economy.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `NUM_REALMS == 16`, `REALM_NAMES[0..15]`, `REALM_QI_THRESHOLD[0..15]` — consumed by every later task that scales stats off `realmIndex` (Task 3's `zone_map`, Task 4's `zone_state`/`zone_combat` player scaling, Task 7's `main.cpp`).

- [ ] **Step 1: Write the failing test**

Append to `test/test_economy/test_economy.cpp`, just above the closing `int main` block:

```cpp
void test_realm_count_is_sixteen_with_expected_names(void) {
    TEST_ASSERT_EQUAL(16, NUM_REALMS);
    TEST_ASSERT_EQUAL_STRING("Mortal Body", REALM_NAMES[0]);
    TEST_ASSERT_EQUAL_STRING("Void Refinement", REALM_NAMES[6]);
    TEST_ASSERT_EQUAL_STRING("Empyrean Realm", REALM_NAMES[15]);
    TEST_ASSERT_EQUAL_DOUBLE(27000000.0, REALM_QI_THRESHOLD[6]);
    TEST_ASSERT_EQUAL_DOUBLE(150000000000000000.0, REALM_QI_THRESHOLD[15]);
}
```

And add `RUN_TEST(test_realm_count_is_sixteen_with_expected_names);` to the `RUN_TEST` list in `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_economy`
Expected: FAIL — `NUM_REALMS` is still 7, `REALM_NAMES[15]`/`REALM_QI_THRESHOLD[15]` are out of bounds (undefined, but the string/threshold comparisons will not match).

- [ ] **Step 3: Update `lib/core/realms.h`**

```cpp
#pragma once

// Shared by mesh.h (visual growth/palette tables) and economy.h (names/thresholds),
// kept as its own tiny header so neither module depends on the other for this count.
constexpr int NUM_REALMS = 16;
```

- [ ] **Step 4: Update `lib/core/economy.cpp`'s two tables**

Replace the `REALM_NAMES` and `REALM_QI_THRESHOLD` definitions with:

```cpp
const char* const REALM_NAMES[NUM_REALMS] = {
    "Mortal Body", "Qi Condensation", "Foundation Establishment",
    "Core Formation", "Nascent Soul", "Soul Transformation", "Void Refinement",
    "Spirit Severing", "Dao Seeking", "Immortal Ascension", "Earth Immortal",
    "Heaven Immortal", "Golden Immortal", "Daluo Immortal", "Saint Realm",
    "Empyrean Realm"
};

const double REALM_QI_THRESHOLD[NUM_REALMS] = {
    0.0, 100.0, 1200.0, 15000.0, 180000.0, 2200000.0, 27000000.0,
    320000000.0, 3800000000.0, 46000000000.0, 560000000000.0,
    6800000000000.0, 82000000000000.0, 1000000000000000.0,
    12000000000000000.0, 150000000000000000.0
};
```

(`GENERATORS`, `costForGenerator`, `realmMultiplier`, `qiPerSecond`, and every other function in this file are unchanged.)

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_economy`
Expected: PASS (all `test_economy` cases, including the new one).

- [ ] **Step 6: Run the full native suite to confirm nothing else broke**

Run: `python3 -m platformio test -e native`
Expected: All suites still PASS (nothing else hardcodes `7`).

- [ ] **Step 7: Commit**

```bash
git add lib/core/realms.h lib/core/economy.cpp test/test_economy/test_economy.cpp
git commit -m "feat: expand cultivation realms from 7 to 16"
```

---

## Task 2: Add `zone_combat` (renamed `trial_combat`, unchanged logic)

**Files:**
- Create: `lib/core/zone_combat.h`
- Create: `lib/core/zone_combat.cpp`
- Create: `test/test_zone_combat/test_zone_combat.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct CombatantState`, `makePlayerCombatant(int realmIndex)`, `makeEnemyCombatant(int maxHp, int damage)`, `tickCombat(CombatantState&, CombatantState&, double)`, `isDefeated(const CombatantState&)`, `kPlayerAttackCooldownSeconds`, `kEnemyAttackCooldownSeconds` — consumed by Task 4's `zone_state`.

This is a pure rename with byte-identical logic (`trial_combat.{h,cpp}` stay in place, untouched, until Task 7 deletes them alongside the rest of the old stack) — there is no new behavior to TDD here, so this task's "test" is the pre-existing coverage now compiling and passing against the new filename.

- [ ] **Step 1: Create `lib/core/zone_combat.h`**

```cpp
#pragma once

struct CombatantState {
    int hp = 0;
    int maxHp = 0;
    int attackDamage = 0;
    float attackCooldownSeconds = 1.0f;
    float attackTimer = 0.0f; // counts up to attackCooldownSeconds, then fires and resets to 0
};

constexpr float kPlayerAttackCooldownSeconds = 1.0f;
constexpr float kEnemyAttackCooldownSeconds = 1.2f;

// Player combat stats derived from cultivation progress: maxHp = 100 + 40 * realmIndex,
// attackDamage = 10 + 6 * realmIndex.
CombatantState makePlayerCombatant(int realmIndex);

// Enemy combat stats from a spawn definition's maxHp/damage.
CombatantState makeEnemyCombatant(int maxHp, int damage);

// Advances both combatants' attack timers by dtSeconds; whichever timer(s) reach their
// cooldown deal their attackDamage to the other (hp clamped at 0) and reset to 0. Both can
// land in the same call if both cooldowns elapse within dtSeconds. Returns true if at least
// one attack landed this call.
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds);

bool isDefeated(const CombatantState& c);
```

- [ ] **Step 2: Create `lib/core/zone_combat.cpp`**

```cpp
#include "zone_combat.h"

CombatantState makePlayerCombatant(int realmIndex) {
    CombatantState c;
    c.maxHp = 100 + 40 * realmIndex;
    c.hp = c.maxHp;
    c.attackDamage = 10 + 6 * realmIndex;
    c.attackCooldownSeconds = kPlayerAttackCooldownSeconds;
    c.attackTimer = 0.0f;
    return c;
}

CombatantState makeEnemyCombatant(int maxHp, int damage) {
    CombatantState c;
    c.maxHp = maxHp;
    c.hp = maxHp;
    c.attackDamage = damage;
    c.attackCooldownSeconds = kEnemyAttackCooldownSeconds;
    c.attackTimer = 0.0f;
    return c;
}

namespace {
bool tickOne(CombatantState& attacker, CombatantState& defender, double dtSeconds) {
    attacker.attackTimer += static_cast<float>(dtSeconds);
    if (attacker.attackTimer < attacker.attackCooldownSeconds) return false;
    attacker.attackTimer = 0.0f;
    defender.hp -= attacker.attackDamage;
    if (defender.hp < 0) defender.hp = 0;
    return true;
}
} // namespace

bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds) {
    bool playerLanded = tickOne(player, enemy, dtSeconds);
    bool enemyLanded = tickOne(enemy, player, dtSeconds);
    return playerLanded || enemyLanded;
}

bool isDefeated(const CombatantState& c) {
    return c.hp <= 0;
}
```

- [ ] **Step 3: Create `test/test_zone_combat/test_zone_combat.cpp`**

```cpp
#include <unity.h>
#include "zone_combat.h"

void setUp(void) {}
void tearDown(void) {}

void test_player_combatant_scales_with_realm(void) {
    CombatantState r0 = makePlayerCombatant(0);
    CombatantState r3 = makePlayerCombatant(3);
    TEST_ASSERT_EQUAL_INT(100, r0.maxHp);
    TEST_ASSERT_EQUAL_INT(10, r0.attackDamage);
    TEST_ASSERT_EQUAL_INT(220, r3.maxHp);
    TEST_ASSERT_EQUAL_INT(28, r3.attackDamage);
}

void test_is_defeated_when_hp_zero_or_below(void) {
    CombatantState c = makePlayerCombatant(0);
    c.hp = 0;
    TEST_ASSERT_TRUE(isDefeated(c));
    c.hp = 1;
    TEST_ASSERT_FALSE(isDefeated(c));
}

void test_tick_combat_no_attack_before_cooldown_elapses(void) {
    CombatantState player = makePlayerCombatant(0);      // 1.0s cooldown
    CombatantState enemy = makeEnemyCombatant(30, 8);    // 1.2s cooldown
    tickCombat(player, enemy, 0.5);
    TEST_ASSERT_EQUAL_INT(30, enemy.hp);
    TEST_ASSERT_EQUAL_INT(100, player.hp);
}

void test_tick_combat_player_attack_lands_at_cooldown(void) {
    CombatantState player = makePlayerCombatant(0);   // damage 10, cooldown 1.0s
    CombatantState enemy = makeEnemyCombatant(30, 8); // cooldown 1.2s, won't fire yet
    bool landed = tickCombat(player, enemy, 1.0);
    TEST_ASSERT_TRUE(landed);
    TEST_ASSERT_EQUAL_INT(20, enemy.hp);
    TEST_ASSERT_EQUAL_INT(100, player.hp); // enemy hasn't reached its own cooldown yet
}

void test_tick_combat_enemy_damage_clamps_player_hp_at_zero(void) {
    CombatantState player = makePlayerCombatant(0);
    player.hp = 5;
    CombatantState enemy = makeEnemyCombatant(30, 8);
    tickCombat(player, enemy, 1.2); // enemy's cooldown elapses, deals 8 damage
    TEST_ASSERT_EQUAL_INT(0, player.hp);
    TEST_ASSERT_TRUE(isDefeated(player));
}

void test_tick_combat_is_deterministic(void) {
    CombatantState p1 = makePlayerCombatant(1);
    CombatantState e1 = makeEnemyCombatant(50, 14);
    CombatantState p2 = makePlayerCombatant(1);
    CombatantState e2 = makeEnemyCombatant(50, 14);
    for (int i = 0; i < 10; ++i) {
        tickCombat(p1, e1, 0.3);
        tickCombat(p2, e2, 0.3);
    }
    TEST_ASSERT_EQUAL_INT(p1.hp, p2.hp);
    TEST_ASSERT_EQUAL_INT(e1.hp, e2.hp);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_player_combatant_scales_with_realm);
    RUN_TEST(test_is_defeated_when_hp_zero_or_below);
    RUN_TEST(test_tick_combat_no_attack_before_cooldown_elapses);
    RUN_TEST(test_tick_combat_player_attack_lands_at_cooldown);
    RUN_TEST(test_tick_combat_enemy_damage_clamps_player_hp_at_zero);
    RUN_TEST(test_tick_combat_is_deterministic);
    return UNITY_END();
}
```

- [ ] **Step 4: Run the new test suite**

Run: `python3 -m platformio test -e native -f test_zone_combat`
Expected: PASS (identical assertions to `test_trial_combat`, now against `zone_combat.h`).

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS, including both `test_trial_combat` (untouched) and the new `test_zone_combat`.

- [ ] **Step 6: Commit**

```bash
git add lib/core/zone_combat.h lib/core/zone_combat.cpp test/test_zone_combat/test_zone_combat.cpp
git commit -m "feat: add zone_combat (trial_combat renamed, unchanged logic)"
```

---

## Task 3: Add `zone_map`

**Files:**
- Create: `lib/core/zone_map.h`
- Create: `lib/core/zone_map.cpp`
- Create: `test/test_zone_map/test_zone_map.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct MonsterSpawn { float x; int maxHp; int damage; }`, `struct ZoneMap { int realmIndex; std::vector<MonsterSpawn> monsters; }`, `constexpr float kArenaWidth = 10.0f`, `ZoneMap makeZoneMap(int realmIndex)` — consumed by Task 4's `zone_state` and Task 6's `zone_view`.

- [ ] **Step 1: Write the failing test**

Create `test/test_zone_map/test_zone_map.cpp`:

```cpp
#include <unity.h>
#include "zone_map.h"

void setUp(void) {}
void tearDown(void) {}

void test_realm_zero_matches_original_secret_realm_numbers(void) {
    ZoneMap m = makeZoneMap(0);
    TEST_ASSERT_EQUAL(3, (int)m.monsters.size());
    TEST_ASSERT_EQUAL(30, m.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(8, m.monsters[0].damage);
    TEST_ASSERT_EQUAL(50, m.monsters[1].maxHp);
    TEST_ASSERT_EQUAL(14, m.monsters[1].damage);
    TEST_ASSERT_EQUAL(80, m.monsters[2].maxHp);
    TEST_ASSERT_EQUAL(22, m.monsters[2].damage);
}

void test_stats_increase_with_realm_index(void) {
    ZoneMap low = makeZoneMap(0);
    ZoneMap high = makeZoneMap(5);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(high.monsters[i].maxHp > low.monsters[i].maxHp);
        TEST_ASSERT_TRUE(high.monsters[i].damage > low.monsters[i].damage);
    }
}

void test_monster_positions_are_increasing_and_within_arena(void) {
    ZoneMap m = makeZoneMap(3);
    TEST_ASSERT_TRUE(m.monsters[0].x < m.monsters[1].x);
    TEST_ASSERT_TRUE(m.monsters[1].x < m.monsters[2].x);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(m.monsters[i].x >= 0.0f);
        TEST_ASSERT_TRUE(m.monsters[i].x < kArenaWidth);
    }
}

void test_realm_index_is_recorded(void) {
    ZoneMap m = makeZoneMap(7);
    TEST_ASSERT_EQUAL(7, m.realmIndex);
}

void test_deterministic(void) {
    ZoneMap a = makeZoneMap(4);
    ZoneMap b = makeZoneMap(4);
    TEST_ASSERT_EQUAL(a.monsters[0].maxHp, b.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(a.monsters[2].damage, b.monsters[2].damage);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_zero_matches_original_secret_realm_numbers);
    RUN_TEST(test_stats_increase_with_realm_index);
    RUN_TEST(test_monster_positions_are_increasing_and_within_arena);
    RUN_TEST(test_realm_index_is_recorded);
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
```

Also create `lib/core/zone_map.h` with just the types (no `makeZoneMap` body yet) so the test compiles but fails at link time:

```cpp
#pragma once
#include <vector>

struct MonsterSpawn {
    float x;      // position along the zone's arena, world units [0, kArenaWidth)
    int maxHp;
    int damage;   // damage dealt to the player per attack landed
};

constexpr float kArenaWidth = 10.0f;

struct ZoneMap {
    int realmIndex = 0;                  // which realm's zone this is - drives background palette
    std::vector<MonsterSpawn> monsters;  // encountered in array order, increasing difficulty
};

// Builds the fixed zone for a realm: 3 monster spawns evenly spaced across the arena
// (x = 2.5, 5.0, 7.5), with maxHp/damage scaling from realmIndex using the same additive
// growth terms makePlayerCombatant already uses for the player (+40 maxHp / +6 damage per
// realmIndex), plus a fixed per-tier bonus. realmIndex == 0 reproduces the exact numbers the
// old fixed Secret Realm map used (30/8, 50/14, 80/22). Deterministic - identical every call
// for the same realmIndex.
ZoneMap makeZoneMap(int realmIndex);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: FAIL to link — `undefined reference to 'makeZoneMap(int)'`.

- [ ] **Step 3: Create `lib/core/zone_map.cpp`**

```cpp
#include "zone_map.h"

ZoneMap makeZoneMap(int realmIndex) {
    ZoneMap m;
    m.realmIndex = realmIndex;

    int baseHp = 30 + 40 * realmIndex;
    int baseDamage = 8 + 6 * realmIndex;
    constexpr int kTierHpBonus[3] = {0, 20, 50};
    constexpr int kTierDamageBonus[3] = {0, 6, 14};
    constexpr float kSpawnX[3] = {2.5f, 5.0f, 7.5f};

    m.monsters.resize(3);
    for (int i = 0; i < 3; ++i) {
        m.monsters[i].x = kSpawnX[i];
        m.monsters[i].maxHp = baseHp + kTierHpBonus[i];
        m.monsters[i].damage = baseDamage + kTierDamageBonus[i];
    }
    return m;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: PASS (all 5 cases).

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/core/zone_map.h lib/core/zone_map.cpp test/test_zone_map/test_zone_map.cpp
git commit -m "feat: add zone_map (per-realm zone/monster-spawn definitions)"
```

---

## Task 4: Add `zone_state`

**Files:**
- Create: `lib/core/zone_state.h`
- Create: `lib/core/zone_state.cpp`
- Create: `test/test_zone_state/test_zone_state.cpp`

**Interfaces:**
- Consumes: `MonsterSpawn`, `ZoneMap`, `kArenaWidth`, `makeZoneMap(int)` (Task 3); `CombatantState`, `makePlayerCombatant(int)`, `makeEnemyCombatant(int,int)`, `tickCombat(...)`, `isDefeated(...)` (Task 2).
- Produces: `enum class ZonePhase { Walking, Fighting, Cleared }`, `struct ZoneState { ZoneMap map; float posX; ZonePhase phase; CombatantState player; int currentMonsterIndex; CombatantState enemy; std::vector<bool> monstersDefeated; double qiRewardPending; }`, `ZoneState startZone(const ZoneMap&, int)`, `void tickZone(ZoneState&, double, double, int)`, `void restartZone(ZoneState&, int)` — consumed by Task 6's `zone_view` and Task 7's `main.cpp`/`ui.cpp`.

- [ ] **Step 1: Write the failing tests**

Create `test/test_zone_state/test_zone_state.cpp`:

```cpp
#include <unity.h>
#include "zone_state.h"

void setUp(void) {}
void tearDown(void) {}

void test_start_zone_begins_walking_at_arena_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(100, s.player.maxHp); // realmIndex 0
}

void test_tick_moves_toward_far_end(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    float startX = s.posX;
    tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.posX > startX);
}

void test_reaching_monster_enters_fighting(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentMonsterIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_monster_resumes_walking(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 15); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    for (int i = 0; i < 500 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_TRUE(s.monstersDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack, well before it could win
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickZone(s, 0.1, 10.0, 0);
        if (s.phase == ZonePhase::Walking) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
}

void test_clearing_all_monsters_and_reaching_end_sets_reward(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 15); // strong enough to one-shot-ish every monster
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared; ++i) {
        tickZone(s, 0.1, 42.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_zone_resets_state(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 15);
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared; ++i) {
        tickZone(s, 0.1, 42.0, 15);
    }
    restartZone(s, 15);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.monstersDefeated[0]);
}

void test_restart_uses_current_realm_index_not_frozen_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0); // started weak (realm 0)
    // Simulate the hidden cultivation economy having advanced to realm 4 by the time this
    // restart happens.
    restartZone(s, 4);
    CombatantState expected = makePlayerCombatant(4);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
    TEST_ASSERT_EQUAL_INT(expected.attackDamage, s.player.attackDamage);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.hp); // full HP at the new cap after restart
}

void test_tick_zone_restart_on_defeat_uses_passed_in_realm_index(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickZone(s, 0.1, 10.0, 5); // economy has since advanced to realm 5 by the time of defeat
        if (s.phase == ZonePhase::Walking) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    CombatantState expected = makePlayerCombatant(5);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
}

void test_restart_zone_rebuilds_map_for_current_realm(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0); // started weak, realm-0 zone
    restartZone(s, 4); // hidden economy has since advanced to realm 4
    TEST_ASSERT_EQUAL_INT(4, s.map.realmIndex);
    ZoneMap expectedMap = makeZoneMap(4);
    TEST_ASSERT_EQUAL_INT(expectedMap.monsters[0].maxHp, s.map.monsters[0].maxHp);
    TEST_ASSERT_EQUAL_INT(expectedMap.monsters[2].damage, s.map.monsters[2].damage);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_start_zone_begins_walking_at_arena_start);
    RUN_TEST(test_tick_moves_toward_far_end);
    RUN_TEST(test_reaching_monster_enters_fighting);
    RUN_TEST(test_defeating_monster_resumes_walking);
    RUN_TEST(test_player_defeat_resets_to_start);
    RUN_TEST(test_clearing_all_monsters_and_reaching_end_sets_reward);
    RUN_TEST(test_restart_zone_resets_state);
    RUN_TEST(test_restart_uses_current_realm_index_not_frozen_start);
    RUN_TEST(test_tick_zone_restart_on_defeat_uses_passed_in_realm_index);
    RUN_TEST(test_restart_zone_rebuilds_map_for_current_realm);
    return UNITY_END();
}
```

Create `lib/core/zone_state.h` (declarations only, so the test compiles but fails to link):

```cpp
#pragma once
#include <vector>
#include "zone_map.h"
#include "zone_combat.h"

enum class ZonePhase { Walking, Fighting, Cleared };

constexpr float kWalkSpeedUnitsPerSec = 1.5f;  // == old trial's kTravelSpeed
constexpr float kEncounterDistance = 0.3f;     // == old trial's kEncounterRadius

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

// Fresh zone at the arena's start (posX = 0), Walking, player stats derived from realmIndex.
ZoneState startZone(const ZoneMap& map, int realmIndex);

// Advances the zone by dtSeconds. While Walking: checks for a live, undefeated monster within
// kEncounterDistance of posX (entering Fighting if found), otherwise steps posX toward
// kArenaWidth at kWalkSpeedUnitsPerSec; reaching kArenaWidth with every monster defeated sets
// Cleared (qiRewardPending = proposedReward). While Fighting: resolves one combat tick; on
// monster defeat, marks it defeated and returns to Walking; on player defeat, calls
// restartZone(state, currentRealmIndex). No-op once Cleared (call restartZone to loop again).
// `currentRealmIndex` should be the caller's *live* realm index - only consulted at a restart
// boundary (player defeat here), so it can't change stats mid-fight.
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);

// Resets to a fresh zone for currentRealmIndex - rebuilds the map (via makeZoneMap) too, not
// just player stats, so a restart after a realm breakthrough actually shows the new realm's
// zone (background palette + monster stats), not the zone it started in.
void restartZone(ZoneState& state, int currentRealmIndex);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: FAIL to link — `undefined reference to 'startZone(...)'` etc.

- [ ] **Step 3: Create `lib/core/zone_state.cpp`**

```cpp
#include "zone_state.h"
#include <cmath>

ZoneState startZone(const ZoneMap& map, int realmIndex) {
    ZoneState s;
    s.map = map;
    s.posX = 0.0f;
    s.phase = ZonePhase::Walking;
    s.player = makePlayerCombatant(realmIndex);
    s.currentMonsterIndex = -1;
    s.monstersDefeated.assign(map.monsters.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartZone(ZoneState& state, int currentRealmIndex) {
    state = startZone(makeZoneMap(currentRealmIndex), currentRealmIndex);
}

namespace {
int findUndefeatedMonsterInRange(const ZoneState& state) {
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        float dist = std::fabs(state.map.monsters[i].x - state.posX);
        if (dist <= kEncounterDistance) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool allMonstersDefeated(const ZoneState& state) {
    for (bool defeated : state.monstersDefeated) {
        if (!defeated) return false;
    }
    return true;
}
} // namespace

void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == ZonePhase::Cleared) return;

    if (state.phase == ZonePhase::Walking) {
        int engaged = findUndefeatedMonsterInRange(state);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        float step = kWalkSpeedUnitsPerSec * static_cast<float>(dtSeconds);
        state.posX += step;
        if (state.posX >= kArenaWidth) {
            state.posX = kArenaWidth;
            if (allMonstersDefeated(state)) {
                state.phase = ZonePhase::Cleared;
                state.qiRewardPending = proposedReward;
            }
        }
        return;
    }

    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    if (isDefeated(state.enemy)) {
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.phase = ZonePhase::Walking;
    } else if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: PASS (all 10 cases).

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/core/zone_state.h lib/core/zone_state.cpp test/test_zone_state/test_zone_state.cpp
git commit -m "feat: add zone_state (camera-free Walking/Fighting/Cleared combat loop)"
```

---

## Task 5: Add `zone_textures`

**Files:**
- Create: `lib/core/zone_textures.h`
- Create: `lib/core/zone_textures.cpp`
- Create: `test/test_zone_textures/test_zone_textures.cpp`

**Interfaces:**
- Consumes: `struct RGB { uint8_t r, g, b; }` from `lib/core/color.h`.
- Produces: `RGB zoneSkyColor(int realmIndex)`, `RGB zoneGroundColor(int realmIndex)`, `RGB monsterColor(int realmIndex, int tierIndex)` — consumed by Task 6's `zone_view`.

- [ ] **Step 1: Write the failing test**

Create `test/test_zone_textures/test_zone_textures.cpp`:

```cpp
#include <unity.h>
#include "zone_textures.h"

void setUp(void) {}
void tearDown(void) {}

void test_sky_color_is_deterministic(void) {
    RGB a = zoneSkyColor(3);
    RGB b = zoneSkyColor(3);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_different_realms_have_different_sky_colors(void) {
    RGB a = zoneSkyColor(0);
    RGB b = zoneSkyColor(8);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_sky_and_ground_colors_differ(void) {
    RGB sky = zoneSkyColor(2);
    RGB ground = zoneGroundColor(2);
    TEST_ASSERT_TRUE(sky.r != ground.r || sky.g != ground.g || sky.b != ground.b);
}

void test_monster_color_darkens_with_tier(void) {
    RGB tier0 = monsterColor(5, 0);
    RGB tier2 = monsterColor(5, 2);
    // Tier 2 (toughest) is darker overall than tier 0 (weakest) - sum of channels is lower.
    int sum0 = tier0.r + tier0.g + tier0.b;
    int sum2 = tier2.r + tier2.g + tier2.b;
    TEST_ASSERT_TRUE(sum2 < sum0);
}

void test_monster_color_is_deterministic(void) {
    RGB a = monsterColor(6, 1);
    RGB b = monsterColor(6, 1);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sky_color_is_deterministic);
    RUN_TEST(test_different_realms_have_different_sky_colors);
    RUN_TEST(test_sky_and_ground_colors_differ);
    RUN_TEST(test_monster_color_darkens_with_tier);
    RUN_TEST(test_monster_color_is_deterministic);
    return UNITY_END();
}
```

Create `lib/core/zone_textures.h` (declarations only):

```cpp
#pragma once
#include "color.h"

// Vertical-gradient background endpoints for a realm's zone: hue rotates 22.5 degrees per
// realm (360/16) so all 16 zones are visually distinct. Deterministic - same realmIndex
// always returns the same colors.
RGB zoneSkyColor(int realmIndex);
RGB zoneGroundColor(int realmIndex);

// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: FAIL to link — `undefined reference to 'zoneSkyColor(int)'` etc.

- [ ] **Step 3: Create `lib/core/zone_textures.cpp`**

```cpp
#include "zone_textures.h"
#include <cstdint>
#include <cmath>

namespace {
constexpr float kDegreesPerRealm = 360.0f / 16.0f;

// Deterministic pseudo-random value in [0,1) from two small integers - same technique the
// deleted trial_textures.cpp used for wall shading (an integer hash, no RNG state).
float hashValue(int a, int b) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

uint8_t toByte(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;
    return static_cast<uint8_t>(v * 255.0f);
}

// Standard HSV -> RGB conversion; h in degrees (any range, wrapped), s and v in [0,1].
RGB hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float hp = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r1, g1, b1;
    if      (hp < 1.0f) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
    else                { r1 = c; g1 = 0; b1 = x; }
    float m = v - c;
    return RGB{toByte(r1 + m), toByte(g1 + m), toByte(b1 + m)};
}

float realmHue(int realmIndex) {
    return static_cast<float>(realmIndex) * kDegreesPerRealm;
}
} // namespace

RGB zoneSkyColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.35f, 0.85f);
}

RGB zoneGroundColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.55f, 0.45f);
}

RGB monsterColor(int realmIndex, int tierIndex) {
    float t = static_cast<float>(tierIndex) / 2.0f; // 0, 0.5, 1.0 for tiers 0,1,2
    float hueJitter = (hashValue(realmIndex, tierIndex) - 0.5f) * 20.0f; // +-10 degrees
    float hue = realmHue(realmIndex) + 180.0f + hueJitter; // opposite the background hue
    float value = 0.85f - 0.35f * t;    // darkens with tier
    float saturation = 0.6f + 0.3f * t; // more saturated (angrier) with tier
    return hsvToRgb(hue, saturation, value);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: PASS (all 5 cases).

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/core/zone_textures.h lib/core/zone_textures.cpp test/test_zone_textures/test_zone_textures.cpp
git commit -m "feat: add zone_textures (procedural per-realm background/monster colors)"
```

---

## Task 6: Add `zone_view` (hardware rendering layer)

**Files:**
- Create: `src/zone_view.h`
- Create: `src/zone_view.cpp`
- Modify: `src/ui.h` (rename `raycastViewportBottom` → `sceneViewportBottom`)
- Modify: `src/ui.cpp` (same rename, at both the definition and its one internal call site)
- Modify: `src/trial_view.cpp` (update its two call sites to the new name, so the still-live old stack keeps compiling until Task 7 deletes it)

**Interfaces:**
- Consumes: `ZoneState`, `ZonePhase`, `ZoneMap` (Task 4); `zoneSkyColor`/`zoneGroundColor`/`monsterColor` (Task 5); `kArenaWidth` (Task 3); `kHeaderHeight`/`sceneViewportBottom(int)` (this task, from `ui.h`).
- Produces: `void initZoneView(M5GFX&)`, `void renderZoneView(M5GFX&, const ZoneState&)`, `void triggerAttackFlash()`, `void triggerHitFlash()`, `void playAttackSfx()`, `void playHitSfx()`, `void playVictorySfx()` — consumed by Task 7's `main.cpp`.

This file touches M5GFX/M5Unified APIs unavailable under the `native` test environment (`platformio.ini`'s `env:native` excludes `src/` entirely via `build_src_filter = -<*>`), so there is no unit test here — verification is the hardware build.

- [ ] **Step 1: Rename `raycastViewportBottom` to `sceneViewportBottom` in `src/ui.h`**

Change:
```cpp
int raycastViewportBottom(int screenH);
```
to:
```cpp
int sceneViewportBottom(int screenH);
```
(Leave the preceding comment as-is for now — Task 7 does a full comment pass over `ui.h`/`ui.cpp` when it removes every other raycast-era reference.)

- [ ] **Step 2: Update the definition and call site in `src/ui.cpp`**

Change:
```cpp
int raycastViewportBottom(int screenH) {
    return kHeaderHeight + (screenH - kHeaderHeight) / 2;
}
```
to:
```cpp
int sceneViewportBottom(int screenH) {
    return kHeaderHeight + (screenH - kHeaderHeight) / 2;
}
```
And inside `computeLayout()`, change:
```cpp
gLayout.panelY0 = raycastViewportBottom(screenH);
```
to:
```cpp
gLayout.panelY0 = sceneViewportBottom(screenH);
```

- [ ] **Step 3: Update the two call sites in `src/trial_view.cpp`**

Change:
```cpp
#include "ui.h" // kHeaderHeight, raycastViewportBottom() - shared viewport bounds
```
to:
```cpp
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
```
And change:
```cpp
float availableBottom = raycastViewportBottom(display.height());
```
to:
```cpp
float availableBottom = sceneViewportBottom(display.height());
```

- [ ] **Step 4: Build to confirm the rename didn't break the still-live old stack**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully (this only renamed a function; `trial_view.cpp` is otherwise unchanged).

- [ ] **Step 5: Create `src/zone_view.h`**

```cpp
#pragma once
#include <M5Unified.h>
#include "zone_state.h"

// Allocates the offscreen canvas used for the zone scene. Call once from setup().
void initZoneView(M5GFX& display);

// Renders one frame of the MapleStory-style zone (background, ground, monsters, character)
// into an internal offscreen canvas sized to the actual viewport, then pushes it directly -
// no internal low-res buffer or scaling, since 2D primitive fills are cheap enough to draw at
// native resolution. Player/enemy HP and progress are drawn by ui.cpp's drawHud() instead, not
// here. Does not advance `state` - call tickZone() separately in the game loop.
void renderZoneView(M5GFX& display, const ZoneState& state);

// Short-lived visual flashes at the character/monster position, triggered by main.cpp
// alongside the existing SFX calls at the same combat events. Purely cosmetic timing state
// local to this file - not part of ZoneState, not unit-tested.
void triggerAttackFlash();
void triggerHitFlash();

// Simple procedural SFX (no imported audio assets) - unchanged from the deleted trial_view.
void playAttackSfx();
void playHitSfx();
void playVictorySfx();
```

- [ ] **Step 6: Create `src/zone_view.cpp`**

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds

namespace {
M5Canvas* gZoneCanvas = nullptr;
int gViewportW = 0;
int gViewportH = 0;

uint32_t gAttackFlashUntilMs = 0; // flash on the monster - player's attack landed
uint32_t gHitFlashUntilMs = 0;    // flash on the character - enemy's attack landed
constexpr uint32_t kFlashDurationMs = 150;

int screenXFor(float worldX) {
    float frac = worldX / kArenaWidth;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return static_cast<int>(frac * gViewportW);
}

void drawBackground(M5Canvas& canvas, int realmIndex) {
    RGB sky = zoneSkyColor(realmIndex);
    RGB ground = zoneGroundColor(realmIndex);
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    canvas.fillRect(0, 0, gViewportW, groundTop, canvas.color565(sky.r, sky.g, sky.b));
    canvas.fillRect(0, groundTop, gViewportW, gViewportH - groundTop,
                     canvas.color565(ground.r, ground.g, ground.b));
}

void drawMonster(M5Canvas& canvas, int screenX, int groundY, int maxHp, RGB color, bool isCurrent) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillCircle(screenX, groundY - radius, radius, fill);
    canvas.fillCircle(screenX - radius / 3, groundY - radius, 2, TFT_BLACK); // eye
    canvas.fillCircle(screenX + radius / 3, groundY - radius, 2, TFT_BLACK); // eye
    if (isCurrent) {
        canvas.drawCircle(screenX, groundY - radius, radius + 3, TFT_YELLOW);
    }
}

void drawCharacter(M5Canvas& canvas, int screenX, int groundY, bool walking, uint32_t nowMs) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    int bob = (walking && ((nowMs / 150) % 2 == 0)) ? 0 : 2; // 2-frame walk cycle
    int headY = groundY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = groundY - kBodyHeight + bob;
    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);
    canvas.fillRect(screenX - 5, groundY - 4 + bob, 4, 4, TFT_NAVY);          // left leg
    canvas.fillRect(screenX + 1, groundY - (bob == 0 ? 4 : 8), 4, 4, TFT_NAVY); // right leg (alternates)
}

void drawFlash(M5Canvas& canvas, int screenX, int groundY, uint32_t nowMs, uint32_t untilMs) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, groundY - 20, 6, TFT_YELLOW);
    canvas.drawCircle(screenX, groundY - 20, 10, TFT_ORANGE);
}
} // namespace

void initZoneView(M5GFX& display) {
    if (gZoneCanvas) return;
    gViewportW = display.width();
    gViewportH = sceneViewportBottom(display.height()) - kHeaderHeight;
    gZoneCanvas = new M5Canvas(&display);
    gZoneCanvas->createSprite(gViewportW, gViewportH);
}

void renderZoneView(M5GFX& display, const ZoneState& state) {
    if (!gZoneCanvas) return;
    M5Canvas& canvas = *gZoneCanvas;
    uint32_t nowMs = millis();

    drawBackground(canvas, state.map.realmIndex);
    int groundY = static_cast<int>(gViewportH * 0.85f);

    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        bool isCurrent = (state.phase == ZonePhase::Fighting &&
                           state.currentMonsterIndex == static_cast<int>(i));
        int mx = screenXFor(state.map.monsters[i].x);
        RGB color = monsterColor(state.map.realmIndex, static_cast<int>(i));
        drawMonster(canvas, mx, groundY, state.map.monsters[i].maxHp, color, isCurrent);
        if (isCurrent) drawFlash(canvas, mx, groundY, nowMs, gAttackFlashUntilMs);
    }

    int charX = screenXFor(state.posX);
    drawCharacter(canvas, charX, groundY, state.phase == ZonePhase::Walking, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, groundY, nowMs, gHitFlashUntilMs);

    canvas.pushSprite(0, kHeaderHeight);
}

void triggerAttackFlash() {
    gAttackFlashUntilMs = millis() + kFlashDurationMs;
}

void triggerHitFlash() {
    gHitFlashUntilMs = millis() + kFlashDurationMs;
}

void playAttackSfx() {
    M5.Speaker.tone(880.0f, 60);
}

void playHitSfx() {
    M5.Speaker.tone(220.0f, 100);
}

void playVictorySfx() {
    M5.Speaker.tone(660.0f, 80);
    delay(90);
    M5.Speaker.tone(880.0f, 80);
    delay(90);
    M5.Speaker.tone(1320.0f, 160);
}
```

- [ ] **Step 7: Build to confirm `zone_view.cpp` compiles**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully. (`zone_view.cpp`/`.h` are not referenced by `main.cpp` yet, but PlatformIO compiles every `src/*.cpp` file, so this alone proves it's syntactically/type correct against the real M5GFX/M5Unified headers.)

- [ ] **Step 8: Run the native suite (unaffected by this task, confirm no regression)**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 9: Commit**

```bash
git add src/zone_view.h src/zone_view.cpp src/ui.h src/ui.cpp src/trial_view.cpp
git commit -m "feat: add zone_view (2D side-view rendering, no camera/scaling buffer needed)"
```

---

## Task 7: Cut over to the zone stack and delete the raycasting stack

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Delete: `lib/core/raycast.h`, `lib/core/raycast.cpp`
- Delete: `lib/core/trial_map.h`, `lib/core/trial_map.cpp`
- Delete: `lib/core/trial_state.h`, `lib/core/trial_state.cpp`
- Delete: `lib/core/trial_textures.h`, `lib/core/trial_textures.cpp`
- Delete: `lib/core/trial_combat.h`, `lib/core/trial_combat.cpp`
- Delete: `src/trial_view.h`, `src/trial_view.cpp`
- Delete: `test/test_raycast/test_raycast.cpp` (and its now-empty directory)
- Delete: `test/test_trial_map/test_trial_map.cpp` (and directory)
- Delete: `test/test_trial_state/test_trial_state.cpp` (and directory)
- Delete: `test/test_trial_textures/test_trial_textures.cpp` (and directory)
- Delete: `test/test_trial_combat/test_trial_combat.cpp` (and directory)

**Interfaces:**
- Consumes: everything from Tasks 1–6 (`zone_map`, `zone_state`, `zone_combat`, `zone_textures`, `zone_view`).
- Produces: nothing new — this is the integration point where `main.cpp`/`ui.cpp` actually use the new stack instead of the old one.

- [ ] **Step 1: Update `src/ui.h`**

Change the include and the `drawHud` signature's doc comment/parameter. Replace:
```cpp
#include <M5Unified.h>
#include "economy.h"
#include "trial_state.h"
#include "hittest.h"
```
with:
```cpp
#include <M5Unified.h>
#include "economy.h"
#include "zone_state.h"
#include "hittest.h"
```

Replace:
```cpp
// The real M5Tab5 panel is portrait (720x1280, confirmed against the fetched M5GFX source -
// NOT the 1280x720 landscape shape it's often assumed to be), so the layout here is a vertical
// stack (header -> raycast viewport -> stats panel), computed at runtime from
// M5.Display.width()/.height() rather than hardcoded.
constexpr int kHeaderHeight = 64;

// The y-coordinate, in absolute screen space, where the raycast viewport ends and the stats/
// settings panel begins: the header plus half of whatever screen space remains below it.
// A function (not a constant) because it depends on the live display height - shared between
// trial_view.cpp (which centers the raycast view within this range) and ui.cpp (which draws
// the panel starting here), so the two can never disagree about where the split sits.
int sceneViewportBottom(int screenH);
```
with:
```cpp
// The real M5Tab5 panel is portrait (720x1280, confirmed against the fetched M5GFX source -
// NOT the 1280x720 landscape shape it's often assumed to be), so the layout here is a vertical
// stack (header -> zone viewport -> stats panel), computed at runtime from
// M5.Display.width()/.height() rather than hardcoded.
constexpr int kHeaderHeight = 64;

// The y-coordinate, in absolute screen space, where the zone viewport ends and the stats/
// settings panel begins: the header plus half of whatever screen space remains below it.
// A function (not a constant) because it depends on the live display height - shared between
// zone_view.cpp (which sizes its canvas to this range) and ui.cpp (which draws the panel
// starting here), so the two can never disagree about where the split sits.
int sceneViewportBottom(int screenH);
```

Replace the `drawHud` declaration and its comment:
```cpp
// Draws the full HUD (header bar + stats/settings panel) into offscreen sprites, then pushes
// each to `display` in one blit apiece. Keeps every redraw atomic on the physical screen -
// drawing primitives directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker. The panel shows: breakthrough progress (read-only - breakthroughs
// are fully automatic), player HP, enemy HP (empty when not currently fighting), route
// progress, then the brightness/volume rows (raw 0-255 device-setting values, not part of
// GameState) as a pair of tappable rows; see HUD_BUTTON_BRIGHTNESS_*/HUD_BUTTON_VOLUME_* for
// their hit-test ids.
void drawHud(M5GFX& display, const GameState& state, const TrialState& trial,
             uint8_t brightness, uint8_t volume);
```
with:
```cpp
// Draws the full HUD (header bar + stats/settings panel) into offscreen sprites, then pushes
// each to `display` in one blit apiece. Keeps every redraw atomic on the physical screen -
// drawing primitives directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker. The panel shows: breakthrough progress (read-only - breakthroughs
// are fully automatic), player HP, enemy HP (empty when not currently fighting), monsters-
// defeated progress, then the brightness/volume rows (raw 0-255 device-setting values, not
// part of GameState) as a pair of tappable rows; see HUD_BUTTON_BRIGHTNESS_*/
// HUD_BUTTON_VOLUME_* for their hit-test ids.
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone,
             uint8_t brightness, uint8_t volume);
```

- [ ] **Step 2: Update `src/ui.cpp`**

Update the top comment about `REALM_QI_THRESHOLD`'s top value. Replace:
```cpp
// Compact display formatting for Qi-scale numbers, which grow into the tens of
// millions (REALM_QI_THRESHOLD tops out at 27,000,000) — a raw "%.0f" would
// overflow a bar's label at any reasonable text size. Declared in ui.h (not
// anonymous-namespace-local) so main.cpp's welcome-back screen can reuse it.
```
with:
```cpp
// Compact display formatting for Qi-scale numbers (REALM_QI_THRESHOLD now tops out at
// 150,000,000,000,000,000 across 16 realms) — a raw "%.0f" would overflow a bar's label at
// any reasonable text size. Declared in ui.h (not anonymous-namespace-local) so main.cpp's
// welcome-back screen can reuse it.
```

Replace the `drawHud` function body's player-HP/enemy-HP/route section. Replace:
```cpp
void drawHud(M5GFX& display, const GameState& state, const TrialState& trial,
             uint8_t brightness, uint8_t volume) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);

    M5Canvas& panel = *gPanelCanvas;
    panel.fillScreen(TFT_BLACK);

    float breakthroughFraction = 1.0f;
    char btLabel[40];
    if (state.realmIndex < NUM_REALMS - 1) {
        breakthroughFraction =
            static_cast<float>(state.qi / REALM_QI_THRESHOLD[state.realmIndex + 1]);
        snprintf(btLabel, sizeof(btLabel), "Breakthrough %d%%",
                 static_cast<int>(breakthroughFraction * 100));
    } else {
        snprintf(btLabel, sizeof(btLabel), "Max Realm Reached");
    }
    drawBar(panel, breakthroughRect(), breakthroughFraction, TFT_ORANGE, btLabel);

    float playerFraction = trial.player.maxHp > 0
        ? static_cast<float>(trial.player.hp) / static_cast<float>(trial.player.maxHp)
        : 0.0f;
    char playerLabel[32];
    snprintf(playerLabel, sizeof(playerLabel), "Player HP %d/%d", trial.player.hp, trial.player.maxHp);
    drawBar(panel, playerHpRect(), playerFraction, TFT_GREEN, playerLabel);

    bool fighting = (trial.phase == TrialPhase::Fighting);
    float enemyFraction = (fighting && trial.enemy.maxHp > 0)
        ? static_cast<float>(trial.enemy.hp) / static_cast<float>(trial.enemy.maxHp)
        : 0.0f;
    char enemyLabel[32];
    if (fighting) {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP %d/%d", trial.enemy.hp, trial.enemy.maxHp);
    } else {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP --");
    }
    drawBar(panel, enemyHpRect(), enemyFraction, TFT_RED, enemyLabel);

    float routeFraction;
    char routeLabel[32];
    if (trial.phase == TrialPhase::Cleared) {
        routeFraction = 1.0f;
        snprintf(routeLabel, sizeof(routeLabel), "Cleared!");
    } else {
        int lastIndex = trial.map.route.size() > 1 ? static_cast<int>(trial.map.route.size()) - 1 : 1;
        routeFraction = static_cast<float>(trial.currentWaypointIndex) / static_cast<float>(lastIndex);
        snprintf(routeLabel, sizeof(routeLabel), "Route %d/%d", trial.currentWaypointIndex, lastIndex);
    }
    drawBar(panel, routeRect(), routeFraction, TFT_CYAN, routeLabel);
```
with:
```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone,
             uint8_t brightness, uint8_t volume) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);

    M5Canvas& panel = *gPanelCanvas;
    panel.fillScreen(TFT_BLACK);

    float breakthroughFraction = 1.0f;
    char btLabel[40];
    if (state.realmIndex < NUM_REALMS - 1) {
        breakthroughFraction =
            static_cast<float>(state.qi / REALM_QI_THRESHOLD[state.realmIndex + 1]);
        snprintf(btLabel, sizeof(btLabel), "Breakthrough %d%%",
                 static_cast<int>(breakthroughFraction * 100));
    } else {
        snprintf(btLabel, sizeof(btLabel), "Max Realm Reached");
    }
    drawBar(panel, breakthroughRect(), breakthroughFraction, TFT_ORANGE, btLabel);

    float playerFraction = zone.player.maxHp > 0
        ? static_cast<float>(zone.player.hp) / static_cast<float>(zone.player.maxHp)
        : 0.0f;
    char playerLabel[32];
    snprintf(playerLabel, sizeof(playerLabel), "Player HP %d/%d", zone.player.hp, zone.player.maxHp);
    drawBar(panel, playerHpRect(), playerFraction, TFT_GREEN, playerLabel);

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
    drawBar(panel, enemyHpRect(), enemyFraction, TFT_RED, enemyLabel);

    int totalMonsters = static_cast<int>(zone.map.monsters.size());
    int defeatedCount = 0;
    for (bool d : zone.monstersDefeated) { if (d) defeatedCount++; }
    float monstersFraction = totalMonsters > 0
        ? static_cast<float>(defeatedCount) / static_cast<float>(totalMonsters)
        : 0.0f;
    char monstersLabel[32];
    if (zone.phase == ZonePhase::Cleared) {
        monstersFraction = 1.0f;
        snprintf(monstersLabel, sizeof(monstersLabel), "Cleared!");
    } else {
        snprintf(monstersLabel, sizeof(monstersLabel), "Monsters %d/%d", defeatedCount, totalMonsters);
    }
    drawBar(panel, routeRect(), monstersFraction, TFT_CYAN, monstersLabel);
```
(The rest of `drawHud` — brightness/volume row drawing — and `hitTestHud`/`initHud`/`drawHeader`/`computeLayout`/the `Layout` struct/`Rect` helpers are unchanged; `routeRect()`/`gLayout.routeY` keep their existing internal names, only their displayed content changes.)

- [ ] **Step 3: Update `src/main.cpp`**

Replace the boot log message (the old raycasting mode's name, now stale):
```cpp
    Serial.println("[BOOT] Secret Realm starting");
```
with:
```cpp
    Serial.println("[BOOT] Zone starting");
```

Replace the includes:
```cpp
#include <M5Unified.h>
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"
#include "trial_map.h"
#include "trial_state.h"
#include "trial_view.h"
#include "settings.h"
```
with:
```cpp
#include <M5Unified.h>
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"
#include "zone_map.h"
#include "zone_state.h"
#include "zone_view.h"
#include "settings.h"
```

Replace the globals:
```cpp
GameState gState;
TrialState gTrialState;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;
uint32_t gLastTrialTickMs = 0;
```
with:
```cpp
GameState gState;
ZoneState gZoneState;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;
uint32_t gLastZoneTickMs = 0;
```

In `setup()`, replace:
```cpp
    initHud(M5.Display);
    initTrialView(M5.Display);

    // The trial starts immediately and runs forever - there's no other screen to enter it
    // from anymore, and no unlock gate: even a fresh realm-0 character autoplays from boot,
    // consistent with "a weak cultivator can genuinely lose" already being the intended design.
    gTrialState = startTrial(makeSecretRealmMap(), gState.realmIndex);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
    gLastTrialTickMs = millis();
    saveNow();
```
with:
```cpp
    initHud(M5.Display);
    initZoneView(M5.Display);

    // The zone starts immediately and runs forever - there's no other screen to enter it
    // from anymore, and no unlock gate: even a fresh realm-0 character autoplays from boot,
    // consistent with "a weak cultivator can genuinely lose" already being the intended design.
    gZoneState = startZone(makeZoneMap(gState.realmIndex), gState.realmIndex);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
    gLastZoneTickMs = millis();
    saveNow();
```

In `loop()`, replace the trial-tick-and-render block:
```cpp
    uint32_t nowTrial = millis();
    double dt = (nowTrial - gLastTrialTickMs) / 1000.0;
    gLastTrialTickMs = nowTrial;

    // Reward scales with the Qi needed for the player's *next* breakthrough (or stays
    // at the final realm's own threshold once there's no next realm), so clearing the
    // trial is always worth a meaningful fraction of "how far you have left to go."
    int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
    double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
    TrialPhase phaseBefore = gTrialState.phase;
    bool wasFighting = (phaseBefore == TrialPhase::Fighting);
    int enemyHpBefore = gTrialState.enemy.hp;
    int playerHpBefore = gTrialState.player.hp;

    tickTrial(gTrialState, dt, reward, gState.realmIndex);

    if (wasFighting && gTrialState.enemy.hp < enemyHpBefore) playAttackSfx();
    if (wasFighting && gTrialState.player.hp < playerHpBefore) playHitSfx();

    if (phaseBefore != TrialPhase::Cleared && gTrialState.phase == TrialPhase::Cleared) {
        // Apply the reward exactly once, on the single tick this transition happens
        // (checking qiRewardPending > 0 every frame instead would re-apply it every
        // frame after, since tickTrial() leaves it set while parked in Cleared).
        playVictorySfx();
        gState.qi += gTrialState.qiRewardPending;
        saveNow();
        renderTrialView(M5.Display, gTrialState); // show the raycast frame...
        drawHud(M5.Display, gState, gTrialState, gBrightness, gVolume); // ...with "Cleared!" in the route bar, before pausing
        gLastHudDrawMs = now; // this was an explicit/forced draw; keep the throttle in sync
        delay(1500);
        restartTrial(gTrialState, gState.realmIndex); // resets qiRewardPending to 0.0 and loops back
        gLastTrialTickMs = millis(); // avoid a huge simulated dt on the next tick from the ~1.7s of
                                      // delay() above (SFX + the pause) that gLastTrialTickMs doesn't
                                      // otherwise account for
    } else {
        renderTrialView(M5.Display, gTrialState);
    }
```
with:
```cpp
    uint32_t nowZone = millis();
    double dt = (nowZone - gLastZoneTickMs) / 1000.0;
    gLastZoneTickMs = nowZone;

    // Reward scales with the Qi needed for the player's *next* breakthrough (or stays
    // at the final realm's own threshold once there's no next realm), so clearing the
    // zone is always worth a meaningful fraction of "how far you have left to go."
    int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
    double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
    ZonePhase phaseBefore = gZoneState.phase;
    bool wasFighting = (phaseBefore == ZonePhase::Fighting);
    int enemyHpBefore = gZoneState.enemy.hp;
    int playerHpBefore = gZoneState.player.hp;

    tickZone(gZoneState, dt, reward, gState.realmIndex);

    if (wasFighting && gZoneState.enemy.hp < enemyHpBefore) { playAttackSfx(); triggerAttackFlash(); }
    if (wasFighting && gZoneState.player.hp < playerHpBefore) { playHitSfx(); triggerHitFlash(); }

    if (phaseBefore != ZonePhase::Cleared && gZoneState.phase == ZonePhase::Cleared) {
        // Apply the reward exactly once, on the single tick this transition happens
        // (checking qiRewardPending > 0 every frame instead would re-apply it every
        // frame after, since tickZone() leaves it set while parked in Cleared).
        playVictorySfx();
        gState.qi += gZoneState.qiRewardPending;
        saveNow();
        renderZoneView(M5.Display, gZoneState); // show the cleared frame...
        drawHud(M5.Display, gState, gZoneState, gBrightness, gVolume); // ...with "Cleared!" in the monsters bar, before pausing
        gLastHudDrawMs = now; // this was an explicit/forced draw; keep the throttle in sync
        delay(1500);
        restartZone(gZoneState, gState.realmIndex); // rebuilds the map for the current realm and loops back
        gLastZoneTickMs = millis(); // avoid a huge simulated dt on the next tick from the ~1.7s of
                                     // delay() above (SFX + the pause) that gLastZoneTickMs doesn't
                                     // otherwise account for
    } else {
        renderZoneView(M5.Display, gZoneState);
    }
```

Finally, replace the throttled HUD redraw at the very end of `loop()`:
```cpp
    if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
        drawHud(M5.Display, gState, gTrialState, gBrightness, gVolume);
        gLastHudDrawMs = now;
    }
}
```
with:
```cpp
    if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
        drawHud(M5.Display, gState, gZoneState, gBrightness, gVolume);
        gLastHudDrawMs = now;
    }
}
```

Everything else in `main.cpp` (the boot sequence, economy tick/auto-buy/auto-breakthrough, touch handling for brightness/volume, autosave) is unchanged.

- [ ] **Step 4: Delete the entire raycasting stack**

```bash
git rm lib/core/raycast.h lib/core/raycast.cpp \
       lib/core/trial_map.h lib/core/trial_map.cpp \
       lib/core/trial_state.h lib/core/trial_state.cpp \
       lib/core/trial_textures.h lib/core/trial_textures.cpp \
       lib/core/trial_combat.h lib/core/trial_combat.cpp \
       src/trial_view.h src/trial_view.cpp \
       test/test_raycast/test_raycast.cpp \
       test/test_trial_map/test_trial_map.cpp \
       test/test_trial_state/test_trial_state.cpp \
       test/test_trial_textures/test_trial_textures.cpp \
       test/test_trial_combat/test_trial_combat.cpp
```

(This also removes the now-empty `test/test_raycast/`, `test/test_trial_map/`, `test/test_trial_state/`, `test/test_trial_textures/`, `test/test_trial_combat/` directories.)

- [ ] **Step 5: Build the real target**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully — no leftover references to `TrialState`/`trial_view.h`/etc. anywhere.

- [ ] **Step 6: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS — `test_economy`, `test_save`, `test_settings`, `test_offline_earnings`, `test_hittest`, `test_smoke`, `test_zone_map`, `test_zone_state`, `test_zone_textures`, `test_zone_combat`. (`test_raycast`/`test_trial_map`/`test_trial_state`/`test_trial_textures`/`test_trial_combat` no longer exist, so they're absent from the run, not failing.)

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp src/ui.h src/ui.cpp
git commit -m "feat: cut over to the zone stack; delete the raycasting Secret Realm trial"
```

---

## Task 8: Brightness/volume touch investigation and diagnostics

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new — this task adds a code comment documenting what was investigated and ruled out, plus a serial diagnostic log, so the next real-hardware flash produces actionable data instead of a silent "it still doesn't work."

This project's own vendored library sources (`~/.platformio` / `.pio/libdeps/esp32p4_pioarduino/{M5Unified,M5GFX}`) were read directly to investigate this, rather than guessing. Findings, so this isn't re-investigated from scratch later:

- **Ruled out:** a touch/display rotation mismatch (the spec's original leading hypothesis). M5GFX's Tab5 board-detection code (`M5GFX.cpp`, the `board_M5Tab5` branch) configures the panel at `panel_width=720, panel_height=1280, offset_rotation=0` and the touch controller at `x_min=0, x_max=719, y_min=0, y_max=1279, offset_rotation=0` — identical native coordinate spaces, no rotation applied to either. `M5.Touch`'s enable path (`Touch.begin(_displays.front().touch() ? &_displays.front() : nullptr)` in `M5Unified.cpp`) also correctly detects the Tab5's touch panel and enables `M5.Touch`.
- **A real, if inconclusive, finding:** `M5Unified::begin()` calls `Display.init()` first (which fully initializes the GT911/ST7123 touch controller's I2C communication as part of M5GFX's Tab5 autodetect), then calls `_setup_i2c(board)`, which for the Tab5 board calls `In_I2C.begin(I2C_NUM_1, GPIO_31, GPIO_32)` — the *exact same* I2C port and pins (confirmed against `M5Unified.cpp`'s per-board pin table) the touch controller was just wired up on. `I2C_Class::begin()` calls `m5gfx::i2c::init()`, which explicitly tears down (`release()`) and rebuilds any already-initialized port. This is a real, confirmed-via-source redundant re-init happening immediately after touch setup completes. It is *not* a confirmed root cause — an ESP-IDF I2C bus teardown/rebuild on the same port and pins does not generally corrupt a downstream I2C peripheral's own chip-side state, and both touch driver classes (`Touch_GT911`, `Touch_ST7123`) guard their one-time reset/config sequence behind an `_inited` flag that a second `initTouch()` call would just no-op past — so there is no clean one-line app-level fix to re-run here. Flagging it as a documented oddity rather than claiming a fix that can't be verified from source alone.
- **No bug found** in this project's own `main.cpp`/`ui.cpp` touch dispatch code (`M5.update()` → `M5.Touch.getDetail()` → `.wasClicked()` → `hitTestHud()`) — it matches the standard M5Unified usage pattern exactly.

Given no confirmed root cause survives this level of scrutiny, this task adds a diagnostic instead of a speculative fix: a serial log of every touch click's raw coordinates and the row it hit-tested against, so the next person to flash real hardware gets actual data (are touches detected at all? if so, are the coordinates in the expected range?) instead of an unexplained "still doesn't work."

- [ ] **Step 1: Add diagnostic logging to the touch-handling block in `src/main.cpp`**

Replace:
```cpp
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        bool stateChanged = false;
```
with:
```cpp
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        // Diagnostic for the brightness/volume unresponsiveness report: this project's own
        // vendored M5Unified/M5GFX source was read to rule out a touch/display rotation
        // mismatch (none found - both are configured identically for the Tab5, offset_rotation
        // 0 on both) and to confirm main.cpp's M5.Touch usage matches the standard pattern.
        // No confirmed root cause survived that reading, so this line exists to get real data
        // on the next hardware flash: does a touch even register (this line printing at all),
        // and if so, is touch.x/touch.y within the row it should have hit (button != -1)?
        Serial.printf("[TOUCH] raw=(%d,%d) hitTestHud=%d\n", touch.x, touch.y, button);
        bool stateChanged = false;
```

- [ ] **Step 2: Build the real target**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 3: Run the full native suite (unaffected, confirm no regression)**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "debug: add touch diagnostic logging for brightness/volume investigation"
```

---

## Final Verification

After Task 8, run both full checks once more from the repo root:

```bash
python3 -m platformio test -e native
python3 -m platformio run -e esp32p4_pioarduino
```

Both must pass/build cleanly. At that point: the raycasting stack is fully gone, the app boots straight into a 16-realm MapleStory-style idle zone, and the brightness/volume investigation's findings (and a diagnostic to move it forward) are in place pending a real-hardware flash.
