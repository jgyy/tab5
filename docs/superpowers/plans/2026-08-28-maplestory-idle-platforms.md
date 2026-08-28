# MapleStory-Style Multi-Platform Zone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the live flat-arena zone into a 4-platform-per-realm terrain with a scripted (non-physics) jump between platforms and patrolling enemies, and rotate the display to true landscape, all while leaving combat balance, the economy layer, and autoplay-only behavior untouched.

**Architecture:** `zone_map` gains a `Platform` struct and a deterministic 4-platform generator (ground + 3 elevated, one monster per elevated platform, terrain hashed per-realm but bounded so every layout is reachable by a single fixed jump). `zone_state` gains a `JumpArc` type, a jump-position pure function, a patrol-position pure function, a new `ZonePhase::Jumping`, and a `tickZone` rewrite that's platform-scoped instead of arena-scoped. `zone_textures` gains one new color function. `zone_view` becomes 2D (screen X *and* Y) and draws platforms plus live patrol motion. `main.cpp` gains one `setRotation()` call. A new `lib/core/hash.h`/`.cpp` extracts the deterministic hash function `zone_textures.cpp` already has, so `zone_map.cpp` can reuse it (DRY) instead of duplicating it. The old flat-arena constant `kArenaWidth` is kept alive (with a comment marking it dead-but-temporarily-needed) through Task 2 purely so `src/zone_view.cpp` — not touched until Task 4 — keeps compiling, then deleted in Task 4's own cutover step, mirroring this project's established "old and new coexist until a cutover task" pattern rather than ever leaving a commit red.

**Tech Stack:** C++ (Arduino framework), PlatformIO, M5Unified/M5GFX, Unity test framework (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-08-28-maplestory-idle-platforms-design.md`

## Global Constraints

- No manual/touch control of the character, jump, or attacks — autoplay only, exactly like today.
- No enemy chasing or pathfinding — patrol is confined to the enemy's own platform.
- No real gravity/velocity simulation or collision detection for the jump — it's a scripted (closed-form) arc, per the spec's "Jump Mechanic" rationale.
- No changes to `economy.{h,cpp}`, `save.{h,cpp}`, `offline_earnings.{h,cpp}`, `zone_combat.{h,cpp}`, `NUM_REALMS`, or the monster difficulty formula (`baseHp = 30 + 20*realmIndex`, `baseDamage = 8 + 3*realmIndex`, tier bonuses `{0,20,50}` hp / `{0,6,14}` damage).
- No change to the "whole zone visible at once, no scrolling camera" property.
- All character/monster/platform/background art stays procedural (M5Canvas primitives / deterministic hash-color functions) — no imported image or audio assets.
- Every task must leave both `pio test -e native` and `pio run -e esp32p4_pioarduino` green before its commit.

---

## Task 1: Extract shared hash utility (`lib/core/hash.h`/`.cpp`)

**Files:**
- Create: `lib/core/hash.h`
- Create: `lib/core/hash.cpp`
- Modify: `lib/core/zone_textures.cpp`
- Create: `test/test_hash/test_hash.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `float hashUnitFloat(int a, int b)`, `float hashRange(int a, int b, float lo, float hi)` — consumed by Task 2's `zone_map.cpp` generator and (via this task's own refactor) `zone_textures.cpp`.

`zone_textures.cpp` currently has its own anonymous-namespace `hashValue(int,int)`. Task 2's platform generator needs the same deterministic-hash technique but can't call an anonymous-namespace function from another translation unit — so this task pulls it out into its own tiny module first, refactors `zone_textures.cpp` to call the extracted version, and proves via the *existing, unmodified* `test_zone_textures` suite that the refactor changed nothing observable.

- [ ] **Step 1: Write the failing test**

Create `test/test_hash/test_hash.cpp`:

```cpp
#include <unity.h>
#include "hash.h"

void setUp(void) {}
void tearDown(void) {}

void test_hash_unit_float_is_deterministic(void) {
    float a = hashUnitFloat(3, 7);
    float b = hashUnitFloat(3, 7);
    TEST_ASSERT_EQUAL_FLOAT(a, b);
}

void test_hash_unit_float_is_within_unit_range(void) {
    for (int i = 0; i < 20; ++i) {
        float v = hashUnitFloat(i, i * 3 + 1);
        TEST_ASSERT_TRUE(v >= 0.0f);
        TEST_ASSERT_TRUE(v < 1.0f);
    }
}

void test_hash_unit_float_differs_across_inputs(void) {
    float a = hashUnitFloat(1, 1);
    float b = hashUnitFloat(1, 2);
    TEST_ASSERT_TRUE(a != b);
}

void test_hash_range_maps_into_bounds(void) {
    for (int i = 0; i < 20; ++i) {
        float v = hashRange(i, 5, -2.0f, 3.0f);
        TEST_ASSERT_TRUE(v >= -2.0f);
        TEST_ASSERT_TRUE(v < 3.0f);
    }
}

void test_hash_range_is_deterministic(void) {
    float a = hashRange(9, 2, 0.0f, 10.0f);
    float b = hashRange(9, 2, 0.0f, 10.0f);
    TEST_ASSERT_EQUAL_FLOAT(a, b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_hash_unit_float_is_deterministic);
    RUN_TEST(test_hash_unit_float_is_within_unit_range);
    RUN_TEST(test_hash_unit_float_differs_across_inputs);
    RUN_TEST(test_hash_range_maps_into_bounds);
    RUN_TEST(test_hash_range_is_deterministic);
    return UNITY_END();
}
```

Also create `lib/core/hash.h` with just declarations (no `.cpp` body yet), so the test compiles but fails at link time:

```cpp
#pragma once

// Deterministic pseudo-random value in [0,1) from two integers - an integer hash, no RNG
// state, so the same (a,b) always returns the same value. Shared by any module that needs
// reproducible per-index "randomness" (zone_textures' color jitter, zone_map's platform-terrain
// generation).
float hashUnitFloat(int a, int b);

// Maps hashUnitFloat(a,b) into [lo, hi). Convenience wrapper for the common "pick a bounded
// value from a hash" pattern.
float hashRange(int a, int b, float lo, float hi);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_hash`
Expected: FAIL to link — `undefined reference to 'hashUnitFloat(int, int)'`.

- [ ] **Step 3: Create `lib/core/hash.cpp`**

```cpp
#include "hash.h"
#include <cstdint>

float hashUnitFloat(int a, int b) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float hashRange(int a, int b, float lo, float hi) {
    return lo + hashUnitFloat(a, b) * (hi - lo);
}
```

This is byte-for-byte the same hash `zone_textures.cpp` already has (moved, not changed), so any code calling it with the same inputs gets identical output to before.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_hash`
Expected: PASS (all 5 cases).

- [ ] **Step 5: Refactor `zone_textures.cpp` to use the extracted hash**

Change the top of `lib/core/zone_textures.cpp` from:

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
```

to:

```cpp
#include "zone_textures.h"
#include "hash.h"
#include <cstdint>
#include <cmath>

namespace {
constexpr float kDegreesPerRealm = 360.0f / 16.0f;

uint8_t toByte(float v) {
```

(deleting the local `hashValue` definition entirely — everything below `toByte` through the end of the file is unchanged) and change the one call site, in `monsterColor`, from:

```cpp
    float hueJitter = (hashValue(realmIndex, tierIndex) - 0.5f) * 20.0f; // +-10 degrees
```

to:

```cpp
    float hueJitter = (hashUnitFloat(realmIndex, tierIndex) - 0.5f) * 20.0f; // +-10 degrees
```

- [ ] **Step 6: Run the existing zone_textures suite to confirm the refactor changed nothing**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: PASS — all 5 pre-existing cases (`test_sky_color_is_deterministic`,
`test_different_realms_have_different_sky_colors`,
`test_sky_and_ground_colors_differ`, `test_monster_color_darkens_with_tier`,
`test_monster_color_is_deterministic`), unmodified, still pass. This is the
regression proof that the refactor is behavior-preserving.

- [ ] **Step 7: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 8: Build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 9: Commit**

```bash
git add lib/core/hash.h lib/core/hash.cpp lib/core/zone_textures.cpp test/test_hash/test_hash.cpp
git commit -m "refactor: extract shared hashUnitFloat/hashRange from zone_textures into hash.h"
```

---

## Task 2: `zone_map` platforms + `zone_state` jump/patrol/`tickZone` rewrite

**Files:**
- Modify: `lib/core/zone_map.h`
- Modify: `lib/core/zone_map.cpp`
- Modify: `test/test_zone_map/test_zone_map.cpp`
- Modify: `lib/core/zone_state.h`
- Modify: `lib/core/zone_state.cpp`
- Modify: `test/test_zone_state/test_zone_state.cpp`

**Interfaces:**
- Consumes: `hashRange(int,int,float,float)` (Task 1); `CombatantState`, `makePlayerCombatant`, `makeEnemyCombatant`, `tickCombat`, `isDefeated` (existing `zone_combat.h`, unchanged).
- Produces: `struct Platform { float x0, x1, y; }`, extended `struct MonsterSpawn { float x; int platformIndex; int maxHp; int damage; }`, extended `struct ZoneMap { int realmIndex; std::vector<Platform> platforms; std::vector<MonsterSpawn> monsters; float arenaWidth; }`, `constexpr float kMaxJumpGap = 2.5f`, `constexpr float kMaxJumpRise = 1.8f`, `constexpr float kMaxPlatformHeight = 4.0f`, rewritten `ZoneMap makeZoneMap(int realmIndex)`; `struct JumpArc { float fromX, fromY, toX, toY, elapsed, duration; }`, `JumpArc makeJumpArc(float,float,float,float)`, `void jumpArcPosition(const JumpArc&, float, float&, float&)`, `float patrolPositionX(float,float,float)`, `float patrolRangeForPlatform(const Platform&)`, constants `kMinJumpDuration`, `kJumpArcHeight`, `kLandingMargin`, `kMaxPatrolRange`, `kPatrolMargin`, `kPatrolSpeed`; `enum class ZonePhase { Walking, Jumping, Fighting, Cleared }`; extended `struct ZoneState` (adds `posY`, `currentPlatformIndex`, `jump`, `walkingElapsedSeconds`); `ZoneState startZone(const ZoneMap&, int)`, `void tickZone(ZoneState&, double, double, int)` (signature unchanged), `void restartZone(ZoneState&, int)` — all consumed by Task 4's `zone_view` and by `main.cpp` (unchanged call sites there).
- **Temporarily retains** `constexpr float kArenaWidth = 10.0f` in `zone_map.h`, unused by any code this task adds, kept solely so `src/zone_view.cpp` (not touched until Task 4) keeps compiling against the old 1D field it still reads. Task 4 deletes it as its own cutover step once `zone_view.cpp` no longer needs it.

This is the largest task in the plan because `zone_map` and `zone_state` are tightly coupled (the platform-scoped `tickZone` rewrite needs `Platform` from the very generator being introduced) and splitting them across separate commits would mean a commit in between where the two files disagree about `ZoneMap`'s shape — instead, everything lands in one task, in TDD sub-cycles, with a single commit once all of it compiles and passes together.

- [ ] **Step 1: Write the failing `zone_map` tests**

Replace the full contents of `test/test_zone_map/test_zone_map.cpp` with:

```cpp
#include <unity.h>
#include <cmath>
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
        TEST_ASSERT_TRUE(m.monsters[i].x < m.arenaWidth);
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
    for (size_t i = 0; i < a.platforms.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].x0, b.platforms[i].x0);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].y, b.platforms[i].y);
    }
}

void test_always_has_four_platforms_and_three_monsters(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        TEST_ASSERT_EQUAL(4, (int)m.platforms.size());
        TEST_ASSERT_EQUAL(3, (int)m.monsters.size());
    }
}

void test_reachability_invariant_holds_across_realms(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        for (size_t i = 1; i < m.platforms.size(); ++i) {
            float gap = m.platforms[i].x0 - m.platforms[i - 1].x1;
            float riseMagnitude = std::fabs(m.platforms[i].y - m.platforms[i - 1].y);
            TEST_ASSERT_TRUE(gap >= 0.0f);
            TEST_ASSERT_TRUE(gap <= kMaxJumpGap);
            TEST_ASSERT_TRUE(riseMagnitude <= kMaxJumpRise);
        }
    }
}

void test_platform_heights_within_bounds(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        for (const Platform& p : m.platforms) {
            TEST_ASSERT_TRUE(p.y >= 0.0f);
            TEST_ASSERT_TRUE(p.y <= kMaxPlatformHeight);
        }
    }
}

void test_monster_platform_index_matches_encounter_order(void) {
    ZoneMap m = makeZoneMap(6);
    TEST_ASSERT_EQUAL(1, m.monsters[0].platformIndex);
    TEST_ASSERT_EQUAL(2, m.monsters[1].platformIndex);
    TEST_ASSERT_EQUAL(3, m.monsters[2].platformIndex);
}

void test_layouts_are_distinct_across_realms(void) {
    ZoneMap a = makeZoneMap(0);
    ZoneMap b = makeZoneMap(8);
    bool anyDifference = false;
    for (size_t i = 0; i < a.platforms.size(); ++i) {
        if (a.platforms[i].y != b.platforms[i].y ||
            (a.platforms[i].x1 - a.platforms[i].x0) != (b.platforms[i].x1 - b.platforms[i].x0)) {
            anyDifference = true;
        }
    }
    TEST_ASSERT_TRUE(anyDifference);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_zero_matches_original_secret_realm_numbers);
    RUN_TEST(test_stats_increase_with_realm_index);
    RUN_TEST(test_monster_positions_are_increasing_and_within_arena);
    RUN_TEST(test_realm_index_is_recorded);
    RUN_TEST(test_deterministic);
    RUN_TEST(test_always_has_four_platforms_and_three_monsters);
    RUN_TEST(test_reachability_invariant_holds_across_realms);
    RUN_TEST(test_platform_heights_within_bounds);
    RUN_TEST(test_monster_platform_index_matches_encounter_order);
    RUN_TEST(test_layouts_are_distinct_across_realms);
    return UNITY_END();
}
```

Replace `lib/core/zone_map.h` with (declarations only, so the test compiles but fails to link):

```cpp
#pragma once
#include <vector>

struct Platform {
    float x0, x1;  // world-space horizontal extent, x0 < x1
    float y;       // height above the ground baseline, world units (0 = ground)
};

struct MonsterSpawn {
    float x;             // patrol-center x, world units [0, arenaWidth)
    int platformIndex;   // which Platform (index into ZoneMap::platforms) this monster patrols on
    int maxHp;
    int damage;          // damage dealt to the player per attack landed
};

constexpr float kMaxJumpGap = 2.5f;        // largest horizontal gap a single jump can cross
constexpr float kMaxJumpRise = 1.8f;       // largest height change (up or down) a single jump can cross
constexpr float kMaxPlatformHeight = 4.0f; // hard ceiling on any platform's height

// Retained temporarily for src/zone_view.cpp's still-1D screen mapping, which isn't rewritten
// until a later task - dead to zone_map.cpp/zone_state.cpp themselves as of this task. Deleted
// once zone_view.cpp is rewritten to use ZoneMap::arenaWidth instead.
constexpr float kArenaWidth = 10.0f;

struct ZoneMap {
    int realmIndex = 0;                  // which realm's zone this is - drives background/ledge palette
    std::vector<Platform> platforms;     // always 4: [0]=ground, [1..3]=elevated
    std::vector<MonsterSpawn> monsters;  // always 3, one per elevated platform, increasing difficulty
    float arenaWidth = 0.0f;             // == platforms.back().x1
};

// Builds a 4-platform zone for a realm: platform 0 is the ground baseline (x0=0); platforms 1-3
// are elevated, each placed at a deterministic (hash-based) gap/width/height-delta from the
// previous one, with height clamped into [0, kMaxPlatformHeight] and gap/height-delta bounded by
// kMaxJumpGap/kMaxJumpRise - so every generated layout is reachable by a single fixed jump at
// every platform boundary. One monster spawns at the midpoint of each elevated platform (in
// platform order, so difficulty tier 0/1/2 still corresponds to encounter order), with
// maxHp/damage from the same unchanged formula this project already used (baseHp = 30 +
// 20*realmIndex, baseDamage = 8 + 3*realmIndex, tier bonuses {0,20,50} hp / {0,6,14} damage;
// realmIndex == 0 reproduces the exact numbers the flat arena used: 30/8, 50/14, 80/22).
// Deterministic - identical every call for the same realmIndex.
ZoneMap makeZoneMap(int realmIndex);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: FAIL to link — `undefined reference to 'makeZoneMap(int)'`.

- [ ] **Step 3: Replace `lib/core/zone_map.cpp`**

```cpp
#include "zone_map.h"
#include "hash.h"

namespace {
constexpr int kNumElevatedPlatforms = 3;

// Salts distinguish which quantity is drawn from the same (realmIndex, platformIndex) pair, so
// a platform's gap/width/height-delta don't collide with each other.
constexpr int kGapSalt = 0;
constexpr int kWidthSalt = 1;
constexpr int kHeightSalt = 2;

float platformGap(int realmIndex, int platformIndex) {
    return hashRange(realmIndex, platformIndex * 3 + kGapSalt, 0.5f, kMaxJumpGap);
}

float platformWidth(int realmIndex, int platformIndex) {
    return hashRange(realmIndex, platformIndex * 3 + kWidthSalt, 1.5f, 3.0f);
}

float platformHeightDelta(int realmIndex, int platformIndex) {
    return hashRange(realmIndex, platformIndex * 3 + kHeightSalt, -kMaxJumpRise, kMaxJumpRise);
}

float groundWidth(int realmIndex) {
    return hashRange(realmIndex, -1, 2.5f, 4.0f); // salt -1: distinct from any elevated index*3+salt
}

float clampHeight(float y) {
    if (y < 0.0f) return 0.0f;
    if (y > kMaxPlatformHeight) return kMaxPlatformHeight;
    return y;
}
} // namespace

ZoneMap makeZoneMap(int realmIndex) {
    ZoneMap m;
    m.realmIndex = realmIndex;

    m.platforms.resize(1 + kNumElevatedPlatforms);
    m.platforms[0] = Platform{0.0f, groundWidth(realmIndex), 0.0f};

    float prevY = 0.0f;
    for (int i = 1; i <= kNumElevatedPlatforms; ++i) {
        float gap = platformGap(realmIndex, i);
        float width = platformWidth(realmIndex, i);
        float y = clampHeight(prevY + platformHeightDelta(realmIndex, i));
        float x0 = m.platforms[static_cast<size_t>(i - 1)].x1 + gap;
        m.platforms[static_cast<size_t>(i)] = Platform{x0, x0 + width, y};
        prevY = y;
    }

    m.arenaWidth = m.platforms.back().x1;

    int baseHp = 30 + 20 * realmIndex;
    int baseDamage = 8 + 3 * realmIndex;
    constexpr int kTierHpBonus[3] = {0, 20, 50};
    constexpr int kTierDamageBonus[3] = {0, 6, 14};

    m.monsters.resize(kNumElevatedPlatforms);
    for (int i = 0; i < kNumElevatedPlatforms; ++i) {
        const Platform& p = m.platforms[static_cast<size_t>(i + 1)];
        m.monsters[static_cast<size_t>(i)].x = (p.x0 + p.x1) / 2.0f;
        m.monsters[static_cast<size_t>(i)].platformIndex = i + 1;
        m.monsters[static_cast<size_t>(i)].maxHp = baseHp + kTierHpBonus[i];
        m.monsters[static_cast<size_t>(i)].damage = baseDamage + kTierDamageBonus[i];
    }
    return m;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_map`
Expected: PASS (all 10 cases).

- [ ] **Step 5: Write the failing `zone_state` pure-function tests**

Add `#include <cmath>` to the top of `test/test_zone_state/test_zone_state.cpp`, alongside the existing `#include <unity.h>` and `#include "zone_state.h"`.

Add these new test functions anywhere before `int main` (independent of `ZoneMap`/`ZoneState`):

```cpp
void test_jump_arc_starts_at_from_point(void) {
    JumpArc arc = makeJumpArc(1.0f, 0.0f, 4.0f, 2.0f);
    float x, y;
    jumpArcPosition(arc, 0.0f, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, y);
}

void test_jump_arc_ends_at_to_point(void) {
    JumpArc arc = makeJumpArc(1.0f, 0.0f, 4.0f, 2.0f);
    float x, y;
    jumpArcPosition(arc, arc.duration, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, y);
}

void test_jump_arc_midpoint_is_raised_above_linear_interpolation(void) {
    JumpArc arc = makeJumpArc(0.0f, 1.0f, 2.0f, 1.0f); // level start/end
    float x, y;
    jumpArcPosition(arc, arc.duration / 2.0f, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, x); // halfway horizontally
    TEST_ASSERT_TRUE(y > 1.0f); // above the level 1.0 baseline - the cosmetic hump
}

void test_jump_arc_duration_has_a_floor_for_zero_distance(void) {
    JumpArc arc = makeJumpArc(2.0f, 0.0f, 2.0f, 0.0f); // same point (a straight-down/up hop)
    TEST_ASSERT_TRUE(arc.duration >= kMinJumpDuration);
}

void test_patrol_position_returns_spawn_at_time_zero(void) {
    float x = patrolPositionX(5.0f, 0.5f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, x);
}

void test_patrol_position_stays_within_range(void) {
    for (int i = 0; i < 200; ++i) {
        float t = static_cast<float>(i) * 0.05f;
        float x = patrolPositionX(3.0f, 0.5f, t);
        TEST_ASSERT_TRUE(x >= 3.0f - 0.5f - 0.001f);
        TEST_ASSERT_TRUE(x <= 3.0f + 0.5f + 0.001f);
    }
}

void test_patrol_position_is_deterministic(void) {
    float a = patrolPositionX(2.0f, 0.6f, 1.3f);
    float b = patrolPositionX(2.0f, 0.6f, 1.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a, b);
}

void test_patrol_range_for_platform_stays_within_platform(void) {
    Platform narrow{0.0f, 1.0f, 0.0f}; // width 1.0
    float range = patrolRangeForPlatform(narrow);
    TEST_ASSERT_TRUE(range >= 0.0f);
    TEST_ASSERT_TRUE(range <= (narrow.x1 - narrow.x0) / 2.0f);
}

void test_patrol_range_for_platform_is_capped(void) {
    Platform wide{0.0f, 10.0f, 0.0f}; // wide enough that the cap, not the platform, binds
    float range = patrolRangeForPlatform(wide);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxPatrolRange, range);
}
```

Add the corresponding `RUN_TEST(...)` lines inside `main()`'s `UNITY_BEGIN()`/`UNITY_END()` block, after the existing ones (before `return UNITY_END();`):

```cpp
    RUN_TEST(test_jump_arc_starts_at_from_point);
    RUN_TEST(test_jump_arc_ends_at_to_point);
    RUN_TEST(test_jump_arc_midpoint_is_raised_above_linear_interpolation);
    RUN_TEST(test_jump_arc_duration_has_a_floor_for_zero_distance);
    RUN_TEST(test_patrol_position_returns_spawn_at_time_zero);
    RUN_TEST(test_patrol_position_stays_within_range);
    RUN_TEST(test_patrol_position_is_deterministic);
    RUN_TEST(test_patrol_range_for_platform_stays_within_platform);
    RUN_TEST(test_patrol_range_for_platform_is_capped);
```

Add the new declarations to `lib/core/zone_state.h`, just below the existing `kEncounterDistance` constant (leave `ZonePhase`/`ZoneState`/`startZone`/`tickZone`/`restartZone` as they are for this step — they're rewritten in Step 9 below):

```cpp
constexpr float kMinJumpDuration = 0.3f;   // seconds - floor so even a zero-distance jump reads as a hop
constexpr float kJumpArcHeight = 0.6f;     // world units - cosmetic up-then-down hump added on top of the linear height interpolation
constexpr float kLandingMargin = 0.2f;     // world units - land this far past a platform's x0, not exactly on the edge

constexpr float kMaxPatrolRange = 0.8f;    // world units - largest half-width a monster can patrol
constexpr float kPatrolMargin = 0.3f;      // world units - keeps a patrolling monster clear of its platform's edges
constexpr float kPatrolSpeed = 0.6f;       // world units/sec - full back-and-forth traversal speed

struct JumpArc {
    float fromX = 0.0f, fromY = 0.0f, toX = 0.0f, toY = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.0f; // seconds
};

// Builds a JumpArc from (fromX,fromY) to (toX,toY): duration is however long the horizontal
// distance takes at kWalkSpeedUnitsPerSec, floored at kMinJumpDuration so even a same-x hop off
// a ledge still visibly reads as a jump.
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY);

// Position along the arc at jump-elapsed-time `elapsed` (clamped to [0, arc.duration]): a
// straight-line interpolation between the two endpoints, with a sin(pi*t) hump added to the
// height so the arc reads as "up then down" whether the destination is higher, lower, or level
// with the start. Pure function of `arc` and `elapsed` - does not mutate `arc`.
void jumpArcPosition(const JumpArc& arc, float elapsed, float& outX, float& outY);

// Monster patrol position at zone-elapsed-walking-time `t` (always >= 0): a triangle wave of
// amplitude `patrolRange` centered on `spawnX`, period 4*patrolRange/kPatrolSpeed. Returns
// exactly spawnX at t=0. Pure function - patrol motion has no state beyond elapsed time.
float patrolPositionX(float spawnX, float patrolRange, float t);

// The patrol half-width for a monster on `platform`, clamped so it never reaches the
// platform's edges: min(kMaxPatrolRange, platformWidth/2 - kPatrolMargin), floored at 0 (a
// platform narrower than 2*kPatrolMargin degenerates to no patrol motion rather than a
// negative range).
float patrolRangeForPlatform(const Platform& platform);
```

- [ ] **Step 6: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: FAIL to link — `undefined reference to 'makeJumpArc(...)'` (and the other new functions). The pre-existing tests in this same file still compile fine at this point (their `ZoneState`/`tickZone` dependencies haven't changed yet), so this failure is purely link-time, isolated to the new symbols.

- [ ] **Step 7: Add the pure-function implementations to `lib/core/zone_state.cpp`**

Add near the top of `lib/core/zone_state.cpp`, after the existing `#include "zone_state.h"` and `#include <cmath>`, and before the existing `ZoneState startZone(...)` definition:

```cpp
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY) {
    JumpArc arc;
    arc.fromX = fromX;
    arc.fromY = fromY;
    arc.toX = toX;
    arc.toY = toY;
    arc.elapsed = 0.0f;
    float horizontalDistance = std::fabs(toX - fromX);
    float duration = horizontalDistance / kWalkSpeedUnitsPerSec;
    arc.duration = duration > kMinJumpDuration ? duration : kMinJumpDuration;
    return arc;
}

void jumpArcPosition(const JumpArc& arc, float elapsed, float& outX, float& outY) {
    float t = arc.duration > 0.0f ? elapsed / arc.duration : 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    outX = arc.fromX + (arc.toX - arc.fromX) * t;
    float baseY = arc.fromY + (arc.toY - arc.fromY) * t;
    outY = baseY + std::sin(kPi * t) * kJumpArcHeight;
}

float patrolPositionX(float spawnX, float patrolRange, float t) {
    if (patrolRange <= 0.0f) return spawnX;
    float period = 4.0f * patrolRange / kPatrolSpeed;
    float phase = std::fmod(t, period) / period; // t is always >= 0 (an elapsed-time accumulator)
    float tri;
    if (phase < 0.25f)      tri = 4.0f * phase;
    else if (phase < 0.75f) tri = 2.0f - 4.0f * phase;
    else                    tri = 4.0f * phase - 4.0f;
    return spawnX + tri * patrolRange;
}

float patrolRangeForPlatform(const Platform& platform) {
    float half = (platform.x1 - platform.x0) / 2.0f - kPatrolMargin;
    if (half < 0.0f) half = 0.0f;
    return half < kMaxPatrolRange ? half : kMaxPatrolRange;
}
```

- [ ] **Step 8: Run test to verify the pure functions pass**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: PASS (all 9 new pure-function cases, plus every pre-existing case — nothing else in this file changed yet).

- [ ] **Step 9: Write the failing `tickZone`/platform-traversal tests**

In `lib/core/zone_state.h`, replace:

```cpp
enum class ZonePhase { Walking, Fighting, Cleared };
```

with:

```cpp
enum class ZonePhase { Walking, Jumping, Fighting, Cleared };
```

Replace the `ZoneState` struct:

```cpp
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
```

with:

```cpp
struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;
    float posY = 0.0f;                    // height above ground baseline, world units
    int currentPlatformIndex = 0;         // which platform posX/posY sit on while Walking
    ZonePhase phase = ZonePhase::Walking;
    JumpArc jump;                         // only meaningful while phase == Jumping
    float walkingElapsedSeconds = 0.0f;   // drives monster patrol position; frozen outside Walking
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
};
```

Update the doc comment above `tickZone`'s declaration (signature itself unchanged) from:

```cpp
// Advances the zone by dtSeconds. While Walking: checks for a live, undefeated monster within
// kEncounterDistance of posX (entering Fighting if found), otherwise steps posX toward
// kArenaWidth at kWalkSpeedUnitsPerSec; reaching kArenaWidth with every monster defeated sets
// Cleared (qiRewardPending = proposedReward). While Fighting: resolves one combat tick; on
// monster defeat, marks it defeated and returns to Walking; on player defeat, calls
// restartZone(state, currentRealmIndex). No-op once Cleared (call restartZone to loop again).
// `currentRealmIndex` should be the caller's *live* realm index - only consulted at a restart
// boundary (player defeat here), so it can't change stats mid-fight.
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);
```

to:

```cpp
// Advances the zone by dtSeconds. While Walking: checks for a live, undefeated monster on the
// current platform within kEncounterDistance of posX (entering Fighting if found), otherwise
// steps posX toward the current platform's far edge at kWalkSpeedUnitsPerSec; reaching that edge
// starts a JumpArc to the next platform (Jumping) or, on the last platform with every monster
// defeated, sets Cleared (qiRewardPending = proposedReward). While Jumping: advances the arc and
// updates posX/posY from it; landing (elapsed >= duration) switches to Walking on the
// destination platform. While Fighting: resolves one combat tick; on monster defeat, marks it
// defeated and returns to Walking; on player defeat, calls restartZone(state,
// currentRealmIndex). No-op once Cleared (call restartZone to loop again). `currentRealmIndex`
// should be the caller's *live* realm index - only consulted at a restart boundary (player
// defeat here), so it can't change stats mid-fight.
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);
```

In `test/test_zone_state/test_zone_state.cpp`, replace `test_large_dt_does_not_skip_monsters` (the one existing test that directly reads `kArenaWidth`, which no longer reflects `ZoneState`'s traversal model):

```cpp
void test_large_dt_does_not_skip_monsters(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    bool reachedFirstMonster = false;
    for (int i = 0; i < 1000; ++i) {
        tickZone(s, 1.4, 10.0, 0);
        if (s.phase == ZonePhase::Fighting && s.currentMonsterIndex == 0) {
            reachedFirstMonster = true;
            break;
        }
        // Must never park at the end of the arena while a monster remains undefeated -
        // that would mean the walk step skipped past it without triggering an encounter.
        bool softLocked = (s.posX >= kArenaWidth) && !s.monstersDefeated[0];
        TEST_ASSERT_FALSE(softLocked);
    }
    TEST_ASSERT_TRUE(reachedFirstMonster);
}
```

with:

```cpp
void test_large_dt_does_not_skip_monsters(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    bool reachedFirstMonster = false;
    for (int i = 0; i < 2000; ++i) {
        tickZone(s, 1.4, 10.0, 0);
        if (s.phase == ZonePhase::Fighting && s.currentMonsterIndex == 0) {
            reachedFirstMonster = true;
            break;
        }
        // Must never be sitting at the end of the arena, on the last platform, while any
        // monster remains undefeated - that would mean a walk step skipped past one without
        // triggering an encounter (the original bug this regression guards against).
        const Platform& lastPlatform = s.map.platforms.back();
        bool onLastPlatform =
            s.currentPlatformIndex == static_cast<int>(s.map.platforms.size()) - 1;
        bool anyUndefeated =
            !s.monstersDefeated[0] || !s.monstersDefeated[1] || !s.monstersDefeated[2];
        bool softLocked = onLastPlatform && s.posX >= lastPlatform.x1 && anyUndefeated;
        TEST_ASSERT_FALSE(softLocked);
    }
    TEST_ASSERT_TRUE(reachedFirstMonster);
}
```

Add these new test functions (anywhere before `int main`):

```cpp
void test_reaching_non_final_platform_edge_triggers_jumping(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    TEST_ASSERT_EQUAL_INT(0, s.currentPlatformIndex); // still on the "from" platform until landing
}

void test_jumping_lands_on_destination_platform(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase != ZonePhase::Jumping; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    for (int i = 0; i < 50 && s.phase == ZonePhase::Jumping; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(1, s.currentPlatformIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, s.map.platforms[1].y, s.posY);
}

void test_walking_elapsed_seconds_freezes_while_fighting(void) {
    ZoneState s = startZone(makeZoneMap(15), 15); // strong player, quick fights
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    float elapsedAtEngage = s.walkingElapsedSeconds;
    for (int i = 0; i < 5 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
        if (s.phase == ZonePhase::Fighting) {
            TEST_ASSERT_FLOAT_WITHIN(0.0001f, elapsedAtEngage, s.walkingElapsedSeconds);
        }
    }
}

void test_restart_zone_rebuilds_platform_and_position_state(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    restartZone(s, 4);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(0, s.currentPlatformIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.walkingElapsedSeconds);
    ZoneMap expectedMap = makeZoneMap(4);
    TEST_ASSERT_EQUAL_INT((int)expectedMap.platforms.size(), (int)s.map.platforms.size());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expectedMap.platforms[1].y, s.map.platforms[1].y);
}
```

Add the corresponding `RUN_TEST(...)` lines inside `main()`, after the ones added in Step 5:

```cpp
    RUN_TEST(test_reaching_non_final_platform_edge_triggers_jumping);
    RUN_TEST(test_jumping_lands_on_destination_platform);
    RUN_TEST(test_walking_elapsed_seconds_freezes_while_fighting);
    RUN_TEST(test_restart_zone_rebuilds_platform_and_position_state);
```

- [ ] **Step 10: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: FAIL to compile — `zone_state.cpp`'s `startZone`/`restartZone`/anonymous-namespace helpers/`tickZone` still reference the old `ZoneState` shape.

- [ ] **Step 11: Replace the rest of `lib/core/zone_state.cpp`**

Replace everything in `lib/core/zone_state.cpp` from `ZoneState startZone(...)` through the end of the file (i.e., everything *after* the `patrolRangeForPlatform` function added in Step 7) with:

```cpp
ZoneState startZone(const ZoneMap& map, int realmIndex) {
    ZoneState s;
    s.map = map;
    s.posX = 0.0f;
    s.posY = 0.0f;
    s.currentPlatformIndex = 0;
    s.phase = ZonePhase::Walking;
    s.jump = JumpArc{};
    s.walkingElapsedSeconds = 0.0f;
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
// The nearest undefeated monster on `platformIndex` within encounter range of posX, or -1.
int findUndefeatedMonsterInRangeOnPlatform(const ZoneState& state, int platformIndex) {
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        const MonsterSpawn& spawn = state.map.monsters[i];
        if (spawn.platformIndex != platformIndex) continue;
        float dist = std::fabs(spawn.x - state.posX);
        if (dist <= kEncounterDistance) return static_cast<int>(i);
    }
    return -1;
}

bool allMonstersDefeated(const ZoneState& state) {
    for (bool defeated : state.monstersDefeated) {
        if (!defeated) return false;
    }
    return true;
}

// The nearest undefeated monster's x position at or ahead of posX on the current platform, or
// that platform's x1 if none remain ahead on it. Used to clamp a single tick's walk step so a
// large dt can never carry posX past an undefeated monster, or off the platform's edge, without
// triggering the appropriate transition (encounter or jump).
float walkTargetXOnCurrentPlatform(const ZoneState& state) {
    const Platform& platform = state.map.platforms[static_cast<size_t>(state.currentPlatformIndex)];
    float nearest = platform.x1;
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        const MonsterSpawn& spawn = state.map.monsters[i];
        if (spawn.platformIndex != state.currentPlatformIndex) continue;
        if (spawn.x >= state.posX && spawn.x < nearest) nearest = spawn.x;
    }
    return nearest;
}
} // namespace

void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == ZonePhase::Cleared) return;

    if (state.phase == ZonePhase::Walking) {
        state.walkingElapsedSeconds += static_cast<float>(dtSeconds);

        int engaged = findUndefeatedMonsterInRangeOnPlatform(state, state.currentPlatformIndex);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        float step = kWalkSpeedUnitsPerSec * static_cast<float>(dtSeconds);
        float maxStep = walkTargetXOnCurrentPlatform(state) - state.posX;
        if (maxStep < 0.0f) maxStep = 0.0f;
        if (step > maxStep) step = maxStep;
        state.posX += step;

        const Platform& platform =
            state.map.platforms[static_cast<size_t>(state.currentPlatformIndex)];
        if (state.posX >= platform.x1) {
            state.posX = platform.x1;
            bool isLastPlatform =
                (state.currentPlatformIndex == static_cast<int>(state.map.platforms.size()) - 1);
            if (isLastPlatform) {
                if (allMonstersDefeated(state)) {
                    state.phase = ZonePhase::Cleared;
                    state.qiRewardPending = proposedReward;
                }
                // Otherwise: nothing left to walk into and no next platform - the skip-clamp
                // above already prevents reaching here with an undefeated monster still ahead
                // on this platform, so this branch only fires once every monster is defeated.
            } else {
                const Platform& next =
                    state.map.platforms[static_cast<size_t>(state.currentPlatformIndex + 1)];
                float landingX = next.x0 + kLandingMargin;
                state.jump = makeJumpArc(state.posX, platform.y, landingX, next.y);
                state.phase = ZonePhase::Jumping;
            }
        }
        return;
    }

    if (state.phase == ZonePhase::Jumping) {
        state.jump.elapsed += static_cast<float>(dtSeconds);
        jumpArcPosition(state.jump, state.jump.elapsed, state.posX, state.posY);
        if (state.jump.elapsed >= state.jump.duration) {
            state.currentPlatformIndex += 1;
            state.posX = state.jump.toX;
            state.posY = state.jump.toY;
            state.phase = ZonePhase::Walking;
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

- [ ] **Step 12: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: PASS (all 20 cases: 12 original — one adapted in Step 9 — plus 8 new from Steps 5 and 9).

- [ ] **Step 13: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 14: Build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully — `src/zone_view.cpp` still compiles unchanged against the retained `kArenaWidth` constant and the still-present `posX`/`phase`/`monsters[i].x` fields it reads; it renders the flat-arena view exactly as before (not yet aware of platforms/jumping/patrol), which is fine for now — Task 4 rewrites it.

- [ ] **Step 15: Commit**

```bash
git add lib/core/zone_map.h lib/core/zone_map.cpp test/test_zone_map/test_zone_map.cpp \
        lib/core/zone_state.h lib/core/zone_state.cpp test/test_zone_state/test_zone_state.cpp
git commit -m "feat: add platform terrain generation, jump arc, and patrol motion to the zone"
```

---

## Task 3: `zone_textures` — `platformColor`

**Files:**
- Modify: `lib/core/zone_textures.h`
- Modify: `lib/core/zone_textures.cpp`
- Modify: `test/test_zone_textures/test_zone_textures.cpp`

**Interfaces:**
- Consumes: `RGB` (`lib/core/color.h`, unchanged), the file-local `hsvToRgb`/`realmHue` helpers (unchanged, already in `zone_textures.cpp`).
- Produces: `RGB platformColor(int realmIndex)` — consumed by Task 4's `zone_view`.

- [ ] **Step 1: Write the failing test**

Add to `test/test_zone_textures/test_zone_textures.cpp`, before `int main`:

```cpp
void test_platform_color_is_deterministic(void) {
    RGB a = platformColor(4);
    RGB b = platformColor(4);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_platform_color_differs_from_sky_and_ground(void) {
    RGB platform = platformColor(2);
    RGB sky = zoneSkyColor(2);
    RGB ground = zoneGroundColor(2);
    TEST_ASSERT_TRUE(platform.r != sky.r || platform.g != sky.g || platform.b != sky.b);
    TEST_ASSERT_TRUE(platform.r != ground.r || platform.g != ground.g || platform.b != ground.b);
}

void test_platform_color_differs_across_realms(void) {
    RGB a = platformColor(0);
    RGB b = platformColor(9);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}
```

Add the `RUN_TEST(...)` lines inside `main()`, after the existing ones:

```cpp
    RUN_TEST(test_platform_color_is_deterministic);
    RUN_TEST(test_platform_color_differs_from_sky_and_ground);
    RUN_TEST(test_platform_color_differs_across_realms);
```

Add the declaration to `lib/core/zone_textures.h`, after `zoneGroundColor`'s declaration:

```cpp
// Ledge fill color for a realm's elevated platforms - distinct from both zoneSkyColor and
// zoneGroundColor, tinted by the same per-realm hue. Deterministic.
RGB platformColor(int realmIndex);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: FAIL to link — `undefined reference to 'platformColor(int)'`.

- [ ] **Step 3: Add the implementation to `lib/core/zone_textures.cpp`**

Add after `zoneGroundColor`'s definition:

```cpp
RGB platformColor(int realmIndex) {
    return hsvToRgb(realmHue(realmIndex), 0.45f, 0.65f);
}
```

(Saturation/value chosen between `zoneSkyColor`'s `(0.35, 0.85)` and `zoneGroundColor`'s `(0.55, 0.45)`, so ledges read as visually distinct from both bands while still belonging to the same realm-hue family.)

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: PASS (all 8 cases: 5 existing + 3 new).

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS.

- [ ] **Step 6: Build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_textures.h lib/core/zone_textures.cpp test/test_zone_textures/test_zone_textures.cpp
git commit -m "feat: add platformColor for zone ledge rendering"
```

---

## Task 4: `zone_view` — 2D rendering, and the `kArenaWidth` cutover

**Files:**
- Modify: `src/zone_view.cpp`
- Modify: `lib/core/zone_map.h`

**Interfaces:**
- Consumes: `Platform`, `MonsterSpawn`, `ZoneMap` (Task 2, via `zone_state.h`); `patrolPositionX`, `patrolRangeForPlatform`, `ZonePhase` (Task 2, via `zone_state.h`); `platformColor` (Task 3, via `zone_textures.h`); `kHeaderHeight`, `sceneViewportBottom` (existing `ui.h`, unchanged).
- Produces: no new public declarations — `src/zone_view.h`'s public API (`initZoneView`, `renderZoneView`, `triggerAttackFlash`, `triggerHitFlash`, `playAttackSfx`, `playHitSfx`, `playVictorySfx`) is unchanged; only the internal rendering logic changes. This task also deletes the now-fully-unused `kArenaWidth` constant from `lib/core/zone_map.h` (its last consumer is the old `zone_view.cpp` rewritten in this same task).

This file is excluded from `env:native` (`platformio.ini`'s `build_src_filter = -<*>` for that environment, same as every prior `src/`-layer file in this project), so there's no native unit test here — verification is the hardware build, same precedent `zone_view.cpp` already established.

- [ ] **Step 1: Replace the internals of `src/zone_view.cpp`**

Replace the entire file with:

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

// Reserve headroom above the tallest possible platform (kMaxPlatformHeight) for its monster's
// sprite (radius up to 40px below) plus margin, so nothing generated at the height ceiling
// clips off the top of the viewport.
constexpr int kTopMarginPx = 60;

int screenXFor(float worldX, float arenaWidth) {
    float frac = arenaWidth > 0.0f ? worldX / arenaWidth : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return static_cast<int>(frac * gViewportW);
}

int screenYFor(float worldY, int groundY) {
    float frac = worldY / kMaxPlatformHeight; // 0 (ground) .. 1 (tallest possible platform)
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int usableRise = groundY - kTopMarginPx;
    if (usableRise < 0) usableRise = 0;
    return groundY - static_cast<int>(frac * usableRise);
}

void drawBackground(M5Canvas& canvas, int realmIndex) {
    RGB sky = zoneSkyColor(realmIndex);
    RGB ground = zoneGroundColor(realmIndex);
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    canvas.fillRect(0, 0, gViewportW, groundTop, canvas.color565(sky.r, sky.g, sky.b));
    canvas.fillRect(0, groundTop, gViewportW, gViewportH - groundTop,
                     canvas.color565(ground.r, ground.g, ground.b));
}

void drawPlatform(M5Canvas& canvas, int screenX0, int screenX1, int screenY, RGB color) {
    constexpr int kLedgeThickness = 10;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillRect(screenX0, screenY, screenX1 - screenX0, kLedgeThickness, fill);
}

void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillCircle(screenX, standY - radius, radius, fill);
    canvas.fillCircle(screenX - radius / 3, standY - radius, 2, TFT_BLACK); // eye
    canvas.fillCircle(screenX + radius / 3, standY - radius, 2, TFT_BLACK); // eye
    if (isCurrent) {
        canvas.drawCircle(screenX, standY - radius, radius + 3, TFT_YELLOW);
    }
}

void drawCharacter(M5Canvas& canvas, int screenX, int standY, ZonePhase phase, uint32_t nowMs) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    constexpr int kAirborneLegTuck = 6; // airborne pose: legs tucked up higher than walk/idle
    bool walking = (phase == ZonePhase::Walking);
    bool jumping = (phase == ZonePhase::Jumping);
    int bob = (walking && ((nowMs / 150) % 2 == 0)) ? 0 : 2; // 2-frame walk cycle
    int legTuck = jumping ? kAirborneLegTuck : 0;
    int headY = standY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = standY - kBodyHeight + bob;
    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);
    canvas.fillRect(screenX - 5, standY - 4 + bob - legTuck, 4, 4, TFT_NAVY);          // left leg
    canvas.fillRect(screenX + 1, standY - (bob == 0 ? 4 : 8) - legTuck, 4, 4, TFT_NAVY); // right leg
}

void drawFlash(M5Canvas& canvas, int screenX, int standY, uint32_t nowMs, uint32_t untilMs) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, standY - 20, 6, TFT_YELLOW);
    canvas.drawCircle(screenX, standY - 20, 10, TFT_ORANGE);
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

    RGB ledgeColor = platformColor(state.map.realmIndex);
    for (size_t i = 1; i < state.map.platforms.size(); ++i) { // [0] is the ground band above
        const Platform& p = state.map.platforms[i];
        int sx0 = screenXFor(p.x0, state.map.arenaWidth);
        int sx1 = screenXFor(p.x1, state.map.arenaWidth);
        int sy = screenYFor(p.y, groundY);
        drawPlatform(canvas, sx0, sx1, sy, ledgeColor);
    }

    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        bool isCurrent = (state.phase == ZonePhase::Fighting &&
                           state.currentMonsterIndex == static_cast<int>(i));
        const MonsterSpawn& spawn = state.map.monsters[i];
        const Platform& platform = state.map.platforms[static_cast<size_t>(spawn.platformIndex)];
        float liveX = isCurrent
            ? spawn.x
            : patrolPositionX(spawn.x, patrolRangeForPlatform(platform), state.walkingElapsedSeconds);
        int mx = screenXFor(liveX, state.map.arenaWidth);
        int my = screenYFor(platform.y, groundY);
        RGB color = monsterColor(state.map.realmIndex, static_cast<int>(i));
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent);
        if (isCurrent) drawFlash(canvas, mx, my, nowMs, gAttackFlashUntilMs);
    }

    int charX = screenXFor(state.posX, state.map.arenaWidth);
    int charY = screenYFor(state.posY, groundY);
    drawCharacter(canvas, charX, charY, state.phase, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);

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

(`Platform`, `MonsterSpawn`, `ZonePhase`, `patrolPositionX`, `patrolRangeForPlatform`, and `kMaxPlatformHeight` all reach this file already, transitively, through `zone_view.h`'s existing `#include "zone_state.h"` — which itself includes `zone_map.h` — so no new `#include` lines are needed.)

- [ ] **Step 2: Delete the now-unused `kArenaWidth` constant**

In `lib/core/zone_map.h`, remove:

```cpp
// Retained temporarily for src/zone_view.cpp's still-1D screen mapping, which isn't rewritten
// until a later task - dead to zone_map.cpp/zone_state.cpp themselves as of this task. Deleted
// once zone_view.cpp is rewritten to use ZoneMap::arenaWidth instead.
constexpr float kArenaWidth = 10.0f;
```

entirely (nothing calls it anymore after Step 1's rewrite).

- [ ] **Step 3: Build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 4: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS (this file isn't part of the native build, but confirms the header change didn't break anything that does compile natively).

- [ ] **Step 5: Commit**

```bash
git add src/zone_view.cpp lib/core/zone_map.h
git commit -m "feat: render platforms, live patrol motion, and a jump pose; retire kArenaWidth"
```

---

## Task 5: Landscape rotation and stale-comment fixes

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/ui.h`

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new (a `setRotation()` call and two comment corrections; no signature changes anywhere).

- [ ] **Step 1: Add the rotation call in `src/main.cpp`**

Change:

```cpp
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Zone starting");

    M5.Display.fillScreen(TFT_BLACK);
```

to:

```cpp
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Rotates the panel from its native portrait orientation to landscape (width() becomes
    // 1280, height() becomes 720) for the MapleStory-style wide side-view zone. Rotation value
    // 1 is an unconfirmed guess (M5GFX convention: even rotations are typically a panel's
    // native orientation, odd rotations are the 90-degree-rotated one) - if the image appears
    // mirrored or upside-down on real hardware, try 3 instead.
    M5.Display.setRotation(1);

    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Zone starting");

    M5.Display.fillScreen(TFT_BLACK);
```

- [ ] **Step 2: Fix the stale portrait-only comment in `src/main.cpp`**

Change:

```cpp
// The HUD (header + stats panel) is pushed as two offscreen-sprite blits covering most of a
// 720x1280 portrait screen, which costs much more per call than the raycast viewport's own
// blit. It's also just text/bars with no motion of its own, so it doesn't need to redraw at
// full render-loop rate — throttling it keeps the raycast view's own redraw (every loop
// iteration, see below) unaffected, while still forcing an immediate redraw right after any
// touch that actually changes state, so brightness/volume taps still feel responsive.
```

to:

```cpp
// The HUD (header + stats panel) is pushed as two offscreen-sprite blits covering most of a
// 1280x720 landscape screen (the panel's native hardware orientation is portrait 720x1280;
// setup() rotates it to landscape for the wide MapleStory-style zone view), which costs much
// more per call than the raycast viewport's own blit used to. It's also just text/bars with no
// motion of its own, so it doesn't need to redraw at full render-loop rate — throttling it
// keeps the zone view's own redraw (every loop iteration, see below) unaffected, while still
// forcing an immediate redraw right after any touch that actually changes state, so brightness/
// volume taps still feel responsive.
```

- [ ] **Step 3: Fix the stale portrait-only comment in `src/ui.h`**

Change:

```cpp
// The real M5Tab5 panel is portrait (720x1280, confirmed against the fetched M5GFX source -
// NOT the 1280x720 landscape shape it's often assumed to be), so the layout here is a vertical
// stack (header -> zone viewport -> stats panel), computed at runtime from
// M5.Display.width()/.height() rather than hardcoded.
constexpr int kHeaderHeight = 64;
```

to:

```cpp
// The real M5Tab5 panel's native hardware orientation is portrait (720x1280, confirmed against
// the fetched M5GFX source - NOT the 1280x720 landscape shape it's often assumed to be).
// main.cpp's setup() rotates it to landscape (setRotation(1)) for the MapleStory-style wide
// zone view, giving width()==1280/height()==720 from here on - the layout below is still a
// vertical stack (header -> zone viewport -> stats panel), computed at runtime from
// M5.Display.width()/.height() rather than hardcoded, so it adapts to whichever rotation is
// actually active.
constexpr int kHeaderHeight = 64;
```

- [ ] **Step 4: Build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 5: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS (neither changed file is compiled into `env:native`, but confirms nothing else regressed).

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/ui.h
git commit -m "feat: rotate display to landscape for the MapleStory-style zone"
```

---

## Task 6: Final verification

**Files:** none (verification only).

- [ ] **Step 1: Run the full native suite**

Run: `python3 -m platformio test -e native`
Expected: All suites PASS — `test_economy`, `test_hash`, `test_hittest`, `test_math3d`, `test_offline_earnings`, `test_save`, `test_settings`, `test_smoke`, `test_zone_combat`, `test_zone_map`, `test_zone_state`, `test_zone_textures`.

- [ ] **Step 2: Run the hardware build check**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 3: Confirm no leftover references to the retired flat-arena constant**

Run: `grep -rn "kArenaWidth" lib/ src/ test/`
Expected: no matches — confirms `kArenaWidth` was fully migrated to `ZoneMap::arenaWidth` and cleanly deleted in Task 4.

No commit for this task — it's a verification gate over work already committed in Tasks 1-5, not a code change of its own. If either check fails here, the offending task's commit needs a follow-up fix before considering this plan complete.
