# Ascension & Realm Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an automated prestige ("Ascension") loop that resets the cultivation economy for a permanent Qi/sec multiplier once realm 15 caps out, and fill in a passive "Realm Identity" trait on every odd realm (1,3,...,15) so every single realm now grants a new character feature.

**Architecture:** Two new, independent `lib/core/` modules (`ascension`, `traits`) following this codebase's existing pattern of hardware-agnostic, unit-tested pure logic. `ascension` plugs into `economy` (a defaulted multiplier parameter) and `save` (two new persisted fields, v2->v3 migration). `traits` plugs into `zone_combat` (one defaulted parameter) and `zone_state` (new fields + inline hooks in `tickZone`'s existing Fighting/Walking branches — no new `ZonePhase`). `main.cpp`/`ui.cpp`/`zone_view.cpp` wire both into the boot/tick/draw loop and give ascension its own FX/SFX and HUD readout.

**Tech Stack:** C++17, PlatformIO (`native` env for unit tests via Unity, `esp32p4_pioarduino` env for the real build), M5Unified/M5GFX for hardware glue.

**Spec:** `docs/superpowers/specs/2026-08-29-ascension-and-realm-identity-design.md`

## Global Constraints

- No manual/touch interaction anywhere — both systems are fully automatic, matching every existing automated system in this app.
- No RNG anywhere in combat — every new trait/ascension mechanic is a deterministic function of realm index, tick count, or accumulated state.
- Every new public function parameter that could break an existing call site or test must be defaulted so existing code keeps compiling unchanged (the established `seed = 0` / `isBossZone = false` pattern from `makeZoneMap`).
- `lib/core/` changes must stay hardware-agnostic and covered by `pio test -e native`; `src/` changes are Arduino/M5GFX glue, build-checked only via `pio run -e esp32p4_pioarduino`.
- Header declarations carry default parameter values; `.cpp` definitions never repeat them (matches `makeZoneMap`/`zone_map.cpp` today).
- Run `pio test -e native` after every task and `pio run -e esp32p4_pioarduino` at the end — both must stay green throughout.

---

### Task 1: Ascension core module

**Files:**
- Create: `lib/core/ascension.h`
- Create: `lib/core/ascension.cpp`
- Test: `test/test_ascension/test_ascension.cpp`

**Interfaces:**
- Consumes: `GameState`/`NUM_REALMS` from `lib/core/economy.h` (already exists).
- Produces: `struct AscensionState { uint32_t ascensionCount; double insight; }`; `double qiMultiplierForInsight(double insight)`; `double insightGainForQi(double qiAtAscension)`; `double ascensionQiThreshold(uint32_t ascensionCount)`; `bool canAscend(const GameState&, const AscensionState&)`; `bool attemptAscend(GameState&, AscensionState&)`. Task 2 (economy multiplier), Task 3 (save persistence), and Task 7 (main loop) all consume these exact names.

- [ ] **Step 1: Write the failing tests**

Create `test/test_ascension/test_ascension.cpp`:

```cpp
// test/test_ascension/test_ascension.cpp
#include <unity.h>
#include "ascension.h"
#include "economy.h"

void setUp(void) {}
void tearDown(void) {}

void test_qi_multiplier_for_insight_is_one_at_zero_insight(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.0, qiMultiplierForInsight(0.0));
}

void test_qi_multiplier_for_insight_grows_with_insight(void) {
    double m10 = qiMultiplierForInsight(10.0);
    double m20 = qiMultiplierForInsight(20.0);
    TEST_ASSERT_TRUE(m20 > m10);
    TEST_ASSERT_TRUE(m10 > 1.0);
}

void test_insight_gain_for_zero_or_negative_qi_is_zero(void) {
    TEST_ASSERT_EQUAL_DOUBLE(0.0, insightGainForQi(0.0));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, insightGainForQi(-100.0));
}

void test_insight_gain_grows_with_qi(void) {
    double small = insightGainForQi(1.0e17);
    double large = insightGainForQi(1.0e19);
    TEST_ASSERT_TRUE(large > small);
    TEST_ASSERT_TRUE(small >= 1.0); // the first ascension threshold itself yields a nonzero gain
}

void test_ascension_threshold_grows_with_ascension_count(void) {
    double t0 = ascensionQiThreshold(0);
    double t1 = ascensionQiThreshold(1);
    double t2 = ascensionQiThreshold(2);
    TEST_ASSERT_TRUE(t1 > t0);
    TEST_ASSERT_TRUE(t2 > t1);
}

void test_cannot_ascend_below_max_realm(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 2;
    state.qi = 1.0e30; // absurdly large qi - realm alone must still gate this
    AscensionState ascension;
    TEST_ASSERT_FALSE(canAscend(state, ascension));
}

void test_cannot_ascend_below_qi_threshold(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    // At this magnitude (1e17), subtracting a small constant like 1.0 is swallowed entirely by
    // double precision (a no-op) - use a fractional reduction instead so it's unambiguously below.
    state.qi = ascensionQiThreshold(0) / 2.0;
    AscensionState ascension;
    TEST_ASSERT_FALSE(canAscend(state, ascension));
}

void test_can_ascend_at_max_realm_and_threshold(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = ascensionQiThreshold(0);
    AscensionState ascension;
    TEST_ASSERT_TRUE(canAscend(state, ascension));
}

void test_attempt_ascend_resets_game_state(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = ascensionQiThreshold(0) + 500.0;
    state.generatorCounts[0] = 40;
    state.generatorCounts[3] = 12;
    AscensionState ascension;
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, state.qi);
    TEST_ASSERT_EQUAL(0, state.realmIndex);
    TEST_ASSERT_EQUAL(1, state.generatorCounts[0]); // fresh-game default, not zero
    TEST_ASSERT_EQUAL(0, state.generatorCounts[3]);
}

void test_attempt_ascend_grants_insight_and_increments_count(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    double qiAtAscension = ascensionQiThreshold(0) + 500.0;
    state.qi = qiAtAscension;
    AscensionState ascension;
    double expectedGain = insightGainForQi(qiAtAscension);
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(expectedGain, ascension.insight);
    TEST_ASSERT_EQUAL_UINT32(1, ascension.ascensionCount);
}

void test_attempt_ascend_fails_and_leaves_state_unchanged_when_not_eligible(void) {
    GameState state;
    state.realmIndex = 5;
    state.qi = 999.0;
    AscensionState ascension;
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(999.0, state.qi);
    TEST_ASSERT_EQUAL(5, state.realmIndex);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, ascension.insight);
    TEST_ASSERT_EQUAL_UINT32(0, ascension.ascensionCount);
}

void test_repeated_ascensions_require_growing_qi_each_time(void) {
    GameState state;
    AscensionState ascension;
    for (int cycle = 0; cycle < 3; ++cycle) {
        double thresholdBefore = ascensionQiThreshold(ascension.ascensionCount);
        state.realmIndex = NUM_REALMS - 1;
        state.qi = thresholdBefore; // exactly enough to ascend this cycle, not the next
        TEST_ASSERT_TRUE(canAscend(state, ascension));
        bool ok = attemptAscend(state, ascension);
        TEST_ASSERT_TRUE(ok);
        // Immediately re-checking the fresh (reset) state must not allow an instant second
        // ascension - qi is back to 0 and the next threshold only grew.
        TEST_ASSERT_FALSE(canAscend(state, ascension));
    }
    TEST_ASSERT_EQUAL_UINT32(3, ascension.ascensionCount);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_qi_multiplier_for_insight_is_one_at_zero_insight);
    RUN_TEST(test_qi_multiplier_for_insight_grows_with_insight);
    RUN_TEST(test_insight_gain_for_zero_or_negative_qi_is_zero);
    RUN_TEST(test_insight_gain_grows_with_qi);
    RUN_TEST(test_ascension_threshold_grows_with_ascension_count);
    RUN_TEST(test_cannot_ascend_below_max_realm);
    RUN_TEST(test_cannot_ascend_below_qi_threshold);
    RUN_TEST(test_can_ascend_at_max_realm_and_threshold);
    RUN_TEST(test_attempt_ascend_resets_game_state);
    RUN_TEST(test_attempt_ascend_grants_insight_and_increments_count);
    RUN_TEST(test_attempt_ascend_fails_and_leaves_state_unchanged_when_not_eligible);
    RUN_TEST(test_repeated_ascensions_require_growing_qi_each_time);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_ascension`
Expected: FAIL to build — `ascension.h` doesn't exist yet.

- [ ] **Step 3: Create `lib/core/ascension.h`**

```cpp
#pragma once
#include <cstdint>
#include "economy.h"

// Prestige state: permanent across every ascension, never reset by attemptAscend(). Persisted
// via save.h's SaveData (see the save-format task).
struct AscensionState {
    uint32_t ascensionCount = 0;
    double insight = 0.0;
};

// Linear, modest bonus (2% per point) so it compounds ascension over ascension without
// exploding - a first-pass constant, meant to be simulation-tuned later like every other
// numeric balance guess in this project.
constexpr double kInsightBonusPerPoint = 0.02;

// sqrt-scales Qi banked at ascension into insight, so very large late-game Qi numbers convert
// into modest, sane insight gains instead of a linear runaway.
constexpr double kInsightQiDivisor = 1.0e16;

// First ascension requires banking this much Qi while already at the realm cap - roughly the
// same order of magnitude as REALM_QI_THRESHOLD[NUM_REALMS - 1] itself.
constexpr double kAscensionBaseQiThreshold = 1.0e17;

// Each successive ascension requires this many times more Qi than the last.
constexpr double kAscensionThresholdGrowth = 3.0;

// Permanent Qi/sec multiplier from accumulated insight.
double qiMultiplierForInsight(double insight);

// Qi banked at the moment of ascension -> insight gained. Never negative; 0 for qi <= 0.
double insightGainForQi(double qiAtAscension);

// Qi threshold required to ascend for the ascensionCount-th time (0-indexed: the very first
// ascension uses ascensionQiThreshold(0)). Grows by kAscensionThresholdGrowth each time.
double ascensionQiThreshold(uint32_t ascensionCount);

// True once realmIndex is at the cap (NUM_REALMS - 1) AND qi has crossed
// ascensionQiThreshold(ascension.ascensionCount).
bool canAscend(const GameState& state, const AscensionState& ascension);

// If canAscend(state, ascension): converts state.qi into insight (added into
// ascension.insight), increments ascension.ascensionCount, resets state to a fresh GameState
// (qi=0, starting generator, realmIndex=0), and returns true. Otherwise leaves both arguments
// completely unchanged and returns false.
bool attemptAscend(GameState& state, AscensionState& ascension);
```

- [ ] **Step 4: Create `lib/core/ascension.cpp`**

```cpp
#include "ascension.h"
#include <cmath>

double qiMultiplierForInsight(double insight) {
    return 1.0 + kInsightBonusPerPoint * insight;
}

double insightGainForQi(double qiAtAscension) {
    if (qiAtAscension <= 0.0) return 0.0;
    return std::floor(std::sqrt(qiAtAscension / kInsightQiDivisor));
}

double ascensionQiThreshold(uint32_t ascensionCount) {
    return kAscensionBaseQiThreshold * std::pow(kAscensionThresholdGrowth, static_cast<double>(ascensionCount));
}

bool canAscend(const GameState& state, const AscensionState& ascension) {
    if (state.realmIndex < NUM_REALMS - 1) return false;
    return state.qi >= ascensionQiThreshold(ascension.ascensionCount);
}

bool attemptAscend(GameState& state, AscensionState& ascension) {
    if (!canAscend(state, ascension)) return false;
    double gained = insightGainForQi(state.qi);
    ascension.insight += gained;
    ascension.ascensionCount += 1;
    state = GameState();
    return true;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_ascension`
Expected: PASS, all 12 cases.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS, all existing suites unaffected (this task added a new module, touched nothing else).

- [ ] **Step 7: Commit**

```bash
git add lib/core/ascension.h lib/core/ascension.cpp test/test_ascension/test_ascension.cpp
git commit -m "feat: add ascension prestige core module"
```

---

### Task 2: Wire the ascension multiplier into the economy

**Files:**
- Modify: `lib/core/economy.h`
- Modify: `lib/core/economy.cpp`
- Test: `test/test_economy/test_economy.cpp`

**Interfaces:**
- Consumes: nothing new from Task 1 (the multiplier is passed in as a plain `double`, no `AscensionState` dependency here — keeps `economy.h` decoupled from `ascension.h`).
- Produces: `double qiPerSecond(const GameState&, double ascensionMultiplier = 1.0)`; `void tick(GameState&, double dtSeconds, double ascensionMultiplier = 1.0)`. Task 7 (main loop) consumes both with a live multiplier.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_economy/test_economy.cpp` (before the `int main` function):

```cpp
void test_qi_per_second_applies_ascension_multiplier(void) {
    GameState state;
    state.generatorCounts[0] = 1;
    double expected = GENERATORS[0].baseQiPerSecond * realmMultiplier(0) * 1.5;
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state, 1.5));
}

void test_qi_per_second_default_multiplier_is_one(void) {
    GameState state;
    state.generatorCounts[0] = 1;
    double expected = GENERATORS[0].baseQiPerSecond * realmMultiplier(0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state));
}

void test_tick_applies_ascension_multiplier(void) {
    GameState state;
    state.generatorCounts[0] = 4;
    double before = state.qi;
    double rate = qiPerSecond(state, 2.0);
    tick(state, 2.0, 2.0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, before + rate * 2.0, state.qi);
}
```

Add the three `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_economy`
Expected: FAIL to build — `qiPerSecond`/`tick` don't accept a second argument yet.

- [ ] **Step 3: Update `lib/core/economy.h`'s declarations**

Change:
```cpp
double qiPerSecond(const GameState& state);
```
```cpp
void tick(GameState& state, double dtSeconds);
```
to:
```cpp
double qiPerSecond(const GameState& state, double ascensionMultiplier = 1.0);
```
```cpp
void tick(GameState& state, double dtSeconds, double ascensionMultiplier = 1.0);
```

- [ ] **Step 4: Update `lib/core/economy.cpp`'s definitions**

Change:
```cpp
double qiPerSecond(const GameState& state) {
    double total = 0.0;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        if (!isGeneratorUnlocked(state, i)) continue;
        total += state.generatorCounts[i] * GENERATORS[i].baseQiPerSecond;
    }
    return total * realmMultiplier(state.realmIndex);
}

void tick(GameState& state, double dtSeconds) {
    if (dtSeconds <= 0.0) return;
    state.qi += qiPerSecond(state) * dtSeconds;
}
```
to:
```cpp
double qiPerSecond(const GameState& state, double ascensionMultiplier) {
    double total = 0.0;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        if (!isGeneratorUnlocked(state, i)) continue;
        total += state.generatorCounts[i] * GENERATORS[i].baseQiPerSecond;
    }
    return total * realmMultiplier(state.realmIndex) * ascensionMultiplier;
}

void tick(GameState& state, double dtSeconds, double ascensionMultiplier) {
    if (dtSeconds <= 0.0) return;
    state.qi += qiPerSecond(state, ascensionMultiplier) * dtSeconds;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_economy`
Expected: PASS, all cases including the 3 new ones.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS. `test_save`'s `test_fresh_state_from_default_save_makes_progress_under_automation` calls `tick(s, 0.05)` with only 2 arguments — this still compiles unchanged via the default.

- [ ] **Step 7: Commit**

```bash
git add lib/core/economy.h lib/core/economy.cpp test/test_economy/test_economy.cpp
git commit -m "feat: apply the ascension multiplier to qiPerSecond/tick"
```

---

### Task 3: Save format v3 — persist ascension state

**Files:**
- Modify: `lib/core/save.h`
- Modify: `lib/core/save.cpp`
- Test: `test/test_save/test_save.cpp`

**Interfaces:**
- Consumes: `AscensionState` from Task 1's `ascension.h`.
- Produces: `SaveData` gains `uint32_t ascensionCount`, `double ascensionInsight`; `SAVE_VERSION == 3`; `SaveData toSaveData(const GameState&, int64_t, uint8_t brightness = 200, uint8_t volume = 128, const AscensionState& ascension = AscensionState())`; `AscensionState toAscensionState(const SaveData&)`. Task 7 (main loop) consumes both.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_save/test_save.cpp`, in the anonymous namespace alongside `LegacySaveDataV1` (add `#include "ascension.h"` to the top-of-file includes too):

```cpp
// Byte-for-byte the v2 (pre-ascension) SaveData layout (see save.cpp's private SaveDataV2).
struct LegacySaveDataV2 {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = 2;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {1, 0, 0, 0, 0, 0};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
    uint8_t brightness = 200;
    uint8_t volume = 128;
};
```

Add these test functions (before `int main`):

```cpp
void test_deserialize_migrates_legacy_v2_save(void) {
    LegacySaveDataV2 legacy;
    legacy.qi = 777.7;
    legacy.generatorCounts[2] = 6;
    legacy.realmIndex = 5;
    legacy.lastSaveEpochSeconds = 1650000000;
    legacy.brightness = 150;
    legacy.volume = 90;

    uint8_t buffer[sizeof(LegacySaveDataV2) + sizeof(uint32_t)];
    std::memcpy(buffer, &legacy, sizeof(LegacySaveDataV2));
    uint32_t checksum = fnv1aChecksumForTest(buffer, sizeof(LegacySaveDataV2));
    std::memcpy(buffer + sizeof(LegacySaveDataV2), &checksum, sizeof(uint32_t));

    SaveData migrated;
    bool ok = deserializeSave(buffer, sizeof(buffer), migrated);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(777.7, migrated.qi);
    TEST_ASSERT_EQUAL(6, migrated.generatorCounts[2]);
    TEST_ASSERT_EQUAL(5, migrated.realmIndex);
    TEST_ASSERT_EQUAL_INT64(1650000000, migrated.lastSaveEpochSeconds);
    TEST_ASSERT_EQUAL_UINT8(150, migrated.brightness);
    TEST_ASSERT_EQUAL_UINT8(90, migrated.volume);
    TEST_ASSERT_EQUAL_UINT32(0, migrated.ascensionCount); // fresh-game default, v2 never had this
    TEST_ASSERT_EQUAL_DOUBLE(0.0, migrated.ascensionInsight);
}

void test_round_trip_preserves_ascension_fields(void) {
    SaveData original;
    original.qi = 10.0;
    original.ascensionCount = 4;
    original.ascensionInsight = 12.5;

    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));

    SaveData restored;
    TEST_ASSERT_TRUE(deserializeSave(buffer, sizeof(buffer), restored));
    TEST_ASSERT_EQUAL_UINT32(4, restored.ascensionCount);
    TEST_ASSERT_EQUAL_DOUBLE(12.5, restored.ascensionInsight);
}

void test_to_save_data_carries_ascension_state(void) {
    GameState state;
    AscensionState ascension;
    ascension.ascensionCount = 2;
    ascension.insight = 7.0;
    SaveData saved = toSaveData(state, 42, 200, 128, ascension);
    TEST_ASSERT_EQUAL_UINT32(2, saved.ascensionCount);
    TEST_ASSERT_EQUAL_DOUBLE(7.0, saved.ascensionInsight);
}

void test_to_save_data_defaults_ascension_state_when_unspecified(void) {
    GameState state;
    SaveData saved = toSaveData(state, 42);
    TEST_ASSERT_EQUAL_UINT32(0, saved.ascensionCount);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, saved.ascensionInsight);
}

void test_to_ascension_state_round_trip(void) {
    SaveData data;
    data.ascensionCount = 9;
    data.ascensionInsight = 33.0;
    AscensionState ascension = toAscensionState(data);
    TEST_ASSERT_EQUAL_UINT32(9, ascension.ascensionCount);
    TEST_ASSERT_EQUAL_DOUBLE(33.0, ascension.insight);
}
```

Add all 5 `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_save`
Expected: FAIL to build — `SaveData` has no `ascensionCount`/`ascensionInsight` fields, `toAscensionState` doesn't exist.

- [ ] **Step 3: Update `lib/core/save.h`**

Change the top include and version constant:
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "economy.h"
```
to:
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "economy.h"
#include "ascension.h"
```

Change:
```cpp
constexpr uint16_t SAVE_VERSION = 2; // v2 added brightness/volume; see deserializeSave's v1 migration
```
to:
```cpp
constexpr uint16_t SAVE_VERSION = 3; // v3 added ascension count/insight; see v1, v2 migrations in deserializeSave
```

Add two fields at the end of `SaveData` (after `volume`):
```cpp
    uint8_t brightness = 200;
    uint8_t volume = 128;
    uint32_t ascensionCount = 0;
    double ascensionInsight = 0.0;
};
```

Change the `toSaveData`/add `toAscensionState` declarations:
```cpp
SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness = 200,
                     uint8_t volume = 128, const AscensionState& ascension = AscensionState());
GameState toGameState(const SaveData& data);
AscensionState toAscensionState(const SaveData& data);
```

- [ ] **Step 4: Update `lib/core/save.cpp`**

Change `toSaveData`'s definition:
```cpp
SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness,
                     uint8_t volume) {
    SaveData d;
    d.qi = state.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        d.generatorCounts[i] = static_cast<uint32_t>(state.generatorCounts[i]);
    }
    d.realmIndex = static_cast<uint8_t>(state.realmIndex);
    d.lastSaveEpochSeconds = epochSeconds;
    d.brightness = brightness;
    d.volume = volume;
    return d;
}
```
to:
```cpp
SaveData toSaveData(const GameState& state, int64_t epochSeconds, uint8_t brightness,
                     uint8_t volume, const AscensionState& ascension) {
    SaveData d;
    d.qi = state.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        d.generatorCounts[i] = static_cast<uint32_t>(state.generatorCounts[i]);
    }
    d.realmIndex = static_cast<uint8_t>(state.realmIndex);
    d.lastSaveEpochSeconds = epochSeconds;
    d.brightness = brightness;
    d.volume = volume;
    d.ascensionCount = ascension.ascensionCount;
    d.ascensionInsight = ascension.insight;
    return d;
}
```

Add a new function right after `toGameState`:
```cpp
AscensionState toAscensionState(const SaveData& data) {
    AscensionState a;
    a.ascensionCount = data.ascensionCount;
    a.insight = data.ascensionInsight;
    return a;
}
```

Add a `SaveDataV2` fallback struct in the anonymous namespace, right after the existing `SaveDataV1`/`SAVE_V1_BUFFER_SIZE`:
```cpp
// Byte-for-byte the v2 (pre-ascension) SaveData layout, kept only so deserializeSave() can
// still read a save written before schema v3 existed and migrate it forward instead of
// failing validation and silently resetting all progress back to a fresh game. Never write
// this format - only ever read it, once, for migration.
struct SaveDataV2 {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = 2;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {1, 0, 0, 0, 0, 0};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
    uint8_t brightness = 200;
    uint8_t volume = 128;
};
constexpr size_t SAVE_V2_BUFFER_SIZE = sizeof(SaveDataV2) + sizeof(uint32_t);
```

Add a new fallback branch in `deserializeSave`, between the existing v1 fallback block and the final `return false;`:

```cpp
    // Fall back to the v2 (pre-ascension) layout: a save written before this schema change
    // would otherwise fail every check above and silently reset all progress back to a fresh
    // game on next boot - migrate it forward instead, same posture as the v1 fallback above.
    if (bufferLen >= SAVE_V2_BUFFER_SIZE) {
        SaveDataV2 legacy;
        std::memcpy(&legacy, buffer, sizeof(SaveDataV2));

        uint32_t storedChecksum;
        std::memcpy(&storedChecksum, buffer + sizeof(SaveDataV2), sizeof(uint32_t));

        if (fnv1aChecksum(buffer, sizeof(SaveDataV2)) == storedChecksum &&
            legacy.magic == SAVE_MAGIC && legacy.version == 2) {
            SaveData migrated; // ascensionCount/ascensionInsight take SaveData's fresh-game defaults
            migrated.qi = legacy.qi;
            for (int i = 0; i < NUM_GENERATORS; ++i) {
                migrated.generatorCounts[i] = legacy.generatorCounts[i];
            }
            migrated.realmIndex = legacy.realmIndex;
            migrated.lastSaveEpochSeconds = legacy.lastSaveEpochSeconds;
            migrated.brightness = legacy.brightness;
            migrated.volume = legacy.volume;
            if (migrated.realmIndex >= NUM_REALMS) migrated.realmIndex = NUM_REALMS - 1;
            outData = migrated;
            return true;
        }
    }

    return false;
}
```

(This replaces just the final `return false;` line — the existing v1 fallback block immediately above it is unchanged.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_save`
Expected: PASS, all cases including the 5 new ones.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS. The existing `test_deserialize_migrates_legacy_v1_save` must still pass unmodified — a genuine v1 buffer is shorter than `SAVE_V2_BUFFER_SIZE`, so it never reaches the new v2 branch.

- [ ] **Step 7: Commit**

```bash
git add lib/core/save.h lib/core/save.cpp test/test_save/test_save.cpp
git commit -m "feat: bump save format to v3 and persist ascension state"
```

---

### Task 4: Traits core module

**Files:**
- Create: `lib/core/traits.h`
- Create: `lib/core/traits.cpp`
- Test: `test/test_traits/test_traits.cpp`

**Interfaces:**
- Consumes: nothing (pure functions of `int realmIndex`, no dependency on `economy.h`/`ascension.h`).
- Produces: `struct TraitDef { const char* name; int unlockRealmIndex; const char* description; }`; `constexpr int NUM_TRAITS = 8`; `extern const TraitDef TRAITS[NUM_TRAITS]`; gates `hasIronSkin`/`hasSteadyBreath`/`hasSoulEcho`/`hasExecution`/`hasSwiftFeet`/`hasRadiantAura`/`hasUndyingWill`/`hasEmpyreanRadiance(int realmIndex)`; multipliers `incomingDamageMultiplier`/`movementSpeedMultiplier`/`skillDamageMultiplier(int realmIndex)` and `regenPerSecond(int realmIndex, int playerMaxHp)`; public constants `kSoulEchoInterval`, `kSoulEchoBonusMultiplier`, `kExecutionHpFraction`, `kExecutionBonusMultiplier`, `kRadiantAuraIntervalSeconds`, `kRadiantAuraDamageMultiplier`. Task 6 (zone_state integration) consumes every one of these exact names.

- [ ] **Step 1: Write the failing tests**

Create `test/test_traits/test_traits.cpp`:

```cpp
// test/test_traits/test_traits.cpp
#include <unity.h>
#include "traits.h"

void setUp(void) {}
void tearDown(void) {}

void test_trait_table_has_one_entry_per_odd_realm(void) {
    TEST_ASSERT_EQUAL_INT(8, NUM_TRAITS);
    int expectedRealms[8] = {1, 3, 5, 7, 9, 11, 13, 15};
    for (int i = 0; i < NUM_TRAITS; ++i) {
        TEST_ASSERT_EQUAL_INT(expectedRealms[i], TRAITS[i].unlockRealmIndex);
    }
}

void test_trait_unlock_realms_strictly_increase(void) {
    for (int i = 1; i < NUM_TRAITS; ++i) {
        TEST_ASSERT_TRUE(TRAITS[i].unlockRealmIndex > TRAITS[i - 1].unlockRealmIndex);
    }
}

void test_has_iron_skin_gate(void) {
    TEST_ASSERT_FALSE(hasIronSkin(0));
    TEST_ASSERT_TRUE(hasIronSkin(1));
    TEST_ASSERT_TRUE(hasIronSkin(15));
}

void test_has_steady_breath_gate(void) {
    TEST_ASSERT_FALSE(hasSteadyBreath(2));
    TEST_ASSERT_TRUE(hasSteadyBreath(3));
}

void test_has_soul_echo_gate(void) {
    TEST_ASSERT_FALSE(hasSoulEcho(4));
    TEST_ASSERT_TRUE(hasSoulEcho(5));
}

void test_has_execution_gate(void) {
    TEST_ASSERT_FALSE(hasExecution(6));
    TEST_ASSERT_TRUE(hasExecution(7));
}

void test_has_swift_feet_gate(void) {
    TEST_ASSERT_FALSE(hasSwiftFeet(8));
    TEST_ASSERT_TRUE(hasSwiftFeet(9));
}

void test_has_radiant_aura_gate(void) {
    TEST_ASSERT_FALSE(hasRadiantAura(10));
    TEST_ASSERT_TRUE(hasRadiantAura(11));
}

void test_has_undying_will_gate(void) {
    TEST_ASSERT_FALSE(hasUndyingWill(12));
    TEST_ASSERT_TRUE(hasUndyingWill(13));
}

void test_has_empyrean_radiance_gate(void) {
    TEST_ASSERT_FALSE(hasEmpyreanRadiance(14));
    TEST_ASSERT_TRUE(hasEmpyreanRadiance(15));
}

void test_incoming_damage_multiplier_reduces_only_once_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, incomingDamageMultiplier(0));
    TEST_ASSERT_TRUE(incomingDamageMultiplier(1) < 1.0f);
}

void test_regen_per_second_is_zero_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, regenPerSecond(2, 100));
    TEST_ASSERT_TRUE(regenPerSecond(3, 100) > 0.0f);
}

void test_regen_per_second_scales_with_max_hp(void) {
    float regenSmall = regenPerSecond(3, 100);
    float regenLarge = regenPerSecond(3, 500);
    TEST_ASSERT_TRUE(regenLarge > regenSmall);
}

void test_movement_speed_multiplier_is_one_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, movementSpeedMultiplier(8));
    TEST_ASSERT_TRUE(movementSpeedMultiplier(9) > 1.0f);
}

void test_skill_damage_multiplier_is_one_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, skillDamageMultiplier(14));
    TEST_ASSERT_TRUE(skillDamageMultiplier(15) > 1.0f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_trait_table_has_one_entry_per_odd_realm);
    RUN_TEST(test_trait_unlock_realms_strictly_increase);
    RUN_TEST(test_has_iron_skin_gate);
    RUN_TEST(test_has_steady_breath_gate);
    RUN_TEST(test_has_soul_echo_gate);
    RUN_TEST(test_has_execution_gate);
    RUN_TEST(test_has_swift_feet_gate);
    RUN_TEST(test_has_radiant_aura_gate);
    RUN_TEST(test_has_undying_will_gate);
    RUN_TEST(test_has_empyrean_radiance_gate);
    RUN_TEST(test_incoming_damage_multiplier_reduces_only_once_unlocked);
    RUN_TEST(test_regen_per_second_is_zero_until_unlocked);
    RUN_TEST(test_regen_per_second_scales_with_max_hp);
    RUN_TEST(test_movement_speed_multiplier_is_one_until_unlocked);
    RUN_TEST(test_skill_damage_multiplier_is_one_until_unlocked);
    return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_traits`
Expected: FAIL to build — `traits.h` doesn't exist yet.

- [ ] **Step 3: Create `lib/core/traits.h`**

```cpp
#pragma once

// A player's automatic, passive Realm Identity trait kit - one per *odd* cultivation realm,
// filling the gap SKILLS[] (skills.h) leaves there (skills unlock only on even realms). Unlike
// skills, every unlocked trait is always "on" - there's no shared rotation slot to compete for.
// No manual activation, same reasoning as skills.h.
struct TraitDef {
    const char* name;
    int unlockRealmIndex; // always odd: 1, 3, 5, 7, 9, 11, 13, 15
    const char* description;
};

constexpr int NUM_TRAITS = 8;
extern const TraitDef TRAITS[NUM_TRAITS];

// TRAITS[i].unlockRealmIndex is the single source of truth each of these reads from - no realm
// number is hardcoded a second time here.
bool hasIronSkin(int realmIndex);          // realm >= 1
bool hasSteadyBreath(int realmIndex);      // realm >= 3
bool hasSoulEcho(int realmIndex);          // realm >= 5
bool hasExecution(int realmIndex);         // realm >= 7
bool hasSwiftFeet(int realmIndex);         // realm >= 9
bool hasRadiantAura(int realmIndex);       // realm >= 11
bool hasUndyingWill(int realmIndex);       // realm >= 13
bool hasEmpyreanRadiance(int realmIndex);  // realm >= 15

// 1.0f unless hasIronSkin(); then a flat reduction. Does not scale further with higher realms -
// one trait, one fixed effect, same posture as an unlocked skill.
float incomingDamageMultiplier(int realmIndex);

// 0.0f unless hasSteadyBreath(); then a flat HP/sec regen amount, proportional to playerMaxHp.
float regenPerSecond(int realmIndex, int playerMaxHp);

// 1.0f unless hasSwiftFeet(); then a flat walk/jump speed multiplier.
float movementSpeedMultiplier(int realmIndex);

// 1.0f unless hasEmpyreanRadiance(); then a flat skill-damage multiplier - the capstone trait.
float skillDamageMultiplier(int realmIndex);

// Soul Echo / Execution / Radiant Aura / Undying Will have no standalone multiplier accessor -
// their thresholds/magnitudes are named constants, applied directly in zone_state.cpp guarded
// by their has*() gate above, mirroring how kBossEnrageCooldownMultiplier (zone_state.h) is a
// named constant applied directly in zone_state.cpp rather than routed through an accessor.
constexpr int kSoulEchoInterval = 4;               // every 4th landed player autoattack echoes
constexpr float kSoulEchoBonusMultiplier = 1.0f;   // bonus damage = player.attackDamage * this
constexpr float kExecutionHpFraction = 0.2f;       // execute bonus triggers at/below 20% enemy HP
constexpr float kExecutionBonusMultiplier = 0.5f;  // bonus damage = player.attackDamage * this
constexpr float kRadiantAuraIntervalSeconds = 2.0f;      // aura ticks once every 2s while Fighting
constexpr float kRadiantAuraDamageMultiplier = 0.3f;     // tick damage = player.attackDamage * this
```

- [ ] **Step 4: Create `lib/core/traits.cpp`**

```cpp
#include "traits.h"

const TraitDef TRAITS[NUM_TRAITS] = {
    {"Iron Skin",         1,  "Reduces incoming damage"},
    {"Steady Breath",     3,  "Regenerates HP while fighting"},
    {"Soul Echo",         5,  "Every 4th strike echoes for bonus damage"},
    {"Execution",         7,  "Bonus damage finishing off a weakened foe"},
    {"Swift Feet",        9,  "Faster movement between platforms"},
    {"Radiant Aura",      11, "Periodic aura damage to the current foe"},
    {"Undying Will",      13, "Survives one fatal blow per zone run"},
    {"Empyrean Radiance", 15, "Amplifies all skill damage"},
};

bool hasIronSkin(int realmIndex)         { return realmIndex >= TRAITS[0].unlockRealmIndex; }
bool hasSteadyBreath(int realmIndex)     { return realmIndex >= TRAITS[1].unlockRealmIndex; }
bool hasSoulEcho(int realmIndex)         { return realmIndex >= TRAITS[2].unlockRealmIndex; }
bool hasExecution(int realmIndex)        { return realmIndex >= TRAITS[3].unlockRealmIndex; }
bool hasSwiftFeet(int realmIndex)        { return realmIndex >= TRAITS[4].unlockRealmIndex; }
bool hasRadiantAura(int realmIndex)      { return realmIndex >= TRAITS[5].unlockRealmIndex; }
bool hasUndyingWill(int realmIndex)      { return realmIndex >= TRAITS[6].unlockRealmIndex; }
bool hasEmpyreanRadiance(int realmIndex) { return realmIndex >= TRAITS[7].unlockRealmIndex; }

namespace {
constexpr float kIronSkinDamageMultiplier = 0.9f;        // -10% incoming damage
constexpr float kSteadyBreathRegenFraction = 0.05f;      // 5% of max HP per second
constexpr float kSwiftFeetSpeedMultiplier = 1.3f;        // +30% movement speed
constexpr float kEmpyreanRadianceSkillMultiplier = 1.2f; // +20% skill damage
}

float incomingDamageMultiplier(int realmIndex) {
    return hasIronSkin(realmIndex) ? kIronSkinDamageMultiplier : 1.0f;
}

float regenPerSecond(int realmIndex, int playerMaxHp) {
    if (!hasSteadyBreath(realmIndex)) return 0.0f;
    return static_cast<float>(playerMaxHp) * kSteadyBreathRegenFraction;
}

float movementSpeedMultiplier(int realmIndex) {
    return hasSwiftFeet(realmIndex) ? kSwiftFeetSpeedMultiplier : 1.0f;
}

float skillDamageMultiplier(int realmIndex) {
    return hasEmpyreanRadiance(realmIndex) ? kEmpyreanRadianceSkillMultiplier : 1.0f;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_traits`
Expected: PASS, all 15 cases.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS (new module only, touches nothing else yet).

- [ ] **Step 7: Commit**

```bash
git add lib/core/traits.h lib/core/traits.cpp test/test_traits/test_traits.cpp
git commit -m "feat: add realm identity traits core module"
```

---

### Task 5: `tickCombat` incoming-damage-multiplier parameter

**Files:**
- Modify: `lib/core/zone_combat.h`
- Modify: `lib/core/zone_combat.cpp`
- Test: `test/test_zone_combat/test_zone_combat.cpp`

**Interfaces:**
- Consumes: nothing new (plain `float` parameter, no dependency on `traits.h` — the caller in Task 6 computes the multiplier and passes it in).
- Produces: `bool tickCombat(CombatantState&, CombatantState&, double dtSeconds, float incomingDamageMultiplier = 1.0f)` — same return type as today, so every existing caller keeps compiling.

- [ ] **Step 1: Write the failing tests**

Add to `test/test_zone_combat/test_zone_combat.cpp` (before `int main`):

```cpp
void test_tick_combat_incoming_damage_multiplier_reduces_damage_to_player(void) {
    CombatantState player = makePlayerCombatant(0);
    CombatantState enemy = makeEnemyCombatant(30, 20);
    tickCombat(player, enemy, 1.2, 0.5f); // enemy's 1.2s cooldown elapses; 20 * 0.5 = 10 damage
    TEST_ASSERT_EQUAL_INT(90, player.hp);
}

void test_tick_combat_default_multiplier_matches_full_damage(void) {
    CombatantState player = makePlayerCombatant(0);
    CombatantState enemy = makeEnemyCombatant(30, 20);
    tickCombat(player, enemy, 1.2); // no multiplier argument -> defaults to 1.0, unchanged behavior
    TEST_ASSERT_EQUAL_INT(80, player.hp);
}

void test_tick_combat_incoming_damage_multiplier_does_not_affect_players_own_damage(void) {
    CombatantState player = makePlayerCombatant(0); // damage 10
    CombatantState enemy = makeEnemyCombatant(30, 20);
    tickCombat(player, enemy, 1.0, 0.5f); // player's 1.0s cooldown elapses; enemy's 1.2s doesn't
    TEST_ASSERT_EQUAL_INT(20, enemy.hp); // full 10 damage, unaffected by incomingDamageMultiplier
}
```

Add the 3 `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_zone_combat`
Expected: FAIL to build — `tickCombat` doesn't accept a 4th argument yet.

- [ ] **Step 3: Update `lib/core/zone_combat.h`**

Change:
```cpp
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds);
```
to:
```cpp
// incomingDamageMultiplier scales only the damage `enemy` deals to `player` (never the
// player's own damage output) - defaults to 1.0f so every existing call site is unaffected;
// zone_state.cpp passes the Iron Skin trait's live multiplier here.
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds,
                 float incomingDamageMultiplier = 1.0f);
```

- [ ] **Step 4: Update `lib/core/zone_combat.cpp`**

Change:
```cpp
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
```
to:
```cpp
namespace {
bool tickOne(CombatantState& attacker, CombatantState& defender, double dtSeconds,
             float damageMultiplier) {
    attacker.attackTimer += static_cast<float>(dtSeconds);
    if (attacker.attackTimer < attacker.attackCooldownSeconds) return false;
    attacker.attackTimer = 0.0f;
    int damage = static_cast<int>(static_cast<float>(attacker.attackDamage) * damageMultiplier);
    defender.hp -= damage;
    if (defender.hp < 0) defender.hp = 0;
    return true;
}
} // namespace

bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds,
                 float incomingDamageMultiplier) {
    bool playerLanded = tickOne(player, enemy, dtSeconds, 1.0f);
    bool enemyLanded = tickOne(enemy, player, dtSeconds, incomingDamageMultiplier);
    return playerLanded || enemyLanded;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_zone_combat`
Expected: PASS, all cases including the 3 new ones.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS. Every existing `tickCombat(...)` call (3-argument form, in `test_zone_combat.cpp` and `zone_state.cpp`) keeps compiling via the default and produces byte-identical results (multiplier 1.0f is exact).

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_combat.h lib/core/zone_combat.cpp test/test_zone_combat/test_zone_combat.cpp
git commit -m "feat: add an incoming-damage multiplier parameter to tickCombat"
```

---

### Task 6: Wire all 8 traits into `zone_state`

**Files:**
- Modify: `lib/core/zone_state.h`
- Modify: `lib/core/zone_state.cpp`
- Test: `test/test_zone_state/test_zone_state.cpp`

**Interfaces:**
- Consumes: everything from Task 4 (`traits.h`) and Task 5's updated `tickCombat`.
- Produces: `ZoneState` gains `int playerAutoAttackCount`, `float radiantAuraTimerSeconds`, `bool undyingWillUsedThisRun` (all reset in `startZone()`); `JumpArc makeJumpArc(float, float, float, float, float speedMultiplier = 1.0f)`. Task 7 (main loop) doesn't need to know about any of this — `tickZone`'s public signature is unchanged.

- [ ] **Step 1: Write the failing tests**

Add `#include "traits.h"` to the top of `test/test_zone_state/test_zone_state.cpp` (alongside the existing `#include "zone_state.h"`). Add these test functions (before `int main`):

```cpp
// --- Iron Skin ---
// Reuses one ZoneState/enemy across two tickZone() calls, varying only the currentRealmIndex
// argument each call - this isolates the trait's effect from realm-based enemy/player stat
// scaling (which is fixed once at zone start), the same technique the existing boss tests use
// to force specific HP values instead of waiting out real damage.
void test_iron_skin_reduces_damage_taken_in_zone(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    s.enemy.attackDamage = 20; // fixed, known value
    s.player.hp = s.player.maxHp;
    tickZone(s, 1.2, 10.0, 0); // realm 0 -> no Iron Skin -> full 20 damage
    int damageWithoutTrait = s.player.maxHp - s.player.hp;
    TEST_ASSERT_EQUAL_INT(20, damageWithoutTrait);

    s.player.hp = s.player.maxHp; // reset for a clean second hit
    tickZone(s, 1.2, 10.0, 1); // realm 1 -> Iron Skin active -> reduced damage
    int damageWithTrait = s.player.maxHp - s.player.hp;
    TEST_ASSERT_TRUE(damageWithTrait < damageWithoutTrait);
}

// --- Steady Breath ---
// dt (0.9s) is kept below both combatants' attack cooldowns (1.0s/1.2s), and both timers are
// freshly zeroed at the Walking->Fighting transition this loop just stopped on - so no
// autoattack lands in this single tick on either side, isolating regen-only HP change.
void test_steady_breath_regenerates_hp_while_fighting(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.player.hp = s.player.maxHp - 50;
    int hpBefore = s.player.hp;
    tickZone(s, 0.9, 10.0, 3); // realm 3 -> Steady Breath active
    TEST_ASSERT_TRUE(s.player.hp > hpBefore);
}

void test_without_steady_breath_hp_does_not_regenerate(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.player.hp = s.player.maxHp - 50;
    int hpBefore = s.player.hp;
    tickZone(s, 0.9, 10.0, 0); // realm 0 -> no Steady Breath
    TEST_ASSERT_EQUAL_INT(hpBefore, s.player.hp);
}

// --- Soul Echo --- (isolated at unlockRealmIndex-1 vs unlockRealmIndex, so every
// already-active lower trait is held constant between the two branches and only Soul Echo
// itself differs)
// The enemy's 1.2s cooldown doesn't divide evenly into this test's 1.0s-per-tick loop, so its
// attack timer accumulates across calls and it lands a hit on the player partway through (at
// call 2, and again at call 4) - if that were allowed to defeat the player, isDefeated() would
// call restartZone() and silently swap in a brand new map/enemy mid-loop, invalidating the
// "same enemy, 4 landed autoattacks" measurement below. Both player and enemy get an enormous
// HP buffer so neither can die mid-test; only the *enemy's* HP delta is actually measured.
void test_soul_echo_adds_bonus_damage_on_the_fourth_landed_autoattack(void) {
    ZoneMap m1 = makeZoneMap(0);
    ZoneState s4 = startZone(m1, 0);
    for (int i = 0; i < 500 && s4.phase != ZonePhase::Fighting; ++i) tickZone(s4, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s4.phase == ZonePhase::Fighting);
    s4.player.hp = 1000000; s4.player.maxHp = 1000000; // invincible for this measurement
    s4.enemy.hp = 100000; s4.enemy.maxHp = 100000;      // never dies mid-test either
    for (int i = 0; i < 4; ++i) tickZone(s4, 1.0, 10.0, 4); // realm 4: no Soul Echo yet
    int damageAtRealm4 = 100000 - s4.enemy.hp;
    TEST_ASSERT_TRUE(s4.phase == ZonePhase::Fighting); // confirms it's still the same encounter

    ZoneMap m2 = makeZoneMap(0);
    ZoneState s5 = startZone(m2, 0);
    for (int i = 0; i < 500 && s5.phase != ZonePhase::Fighting; ++i) tickZone(s5, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s5.phase == ZonePhase::Fighting);
    s5.player.hp = 1000000; s5.player.maxHp = 1000000;
    s5.enemy.hp = 100000; s5.enemy.maxHp = 100000;
    for (int i = 0; i < 4; ++i) tickZone(s5, 1.0, 10.0, 5); // realm 5: Soul Echo active
    int damageAtRealm5 = 100000 - s5.enemy.hp;
    TEST_ASSERT_TRUE(s5.phase == ZonePhase::Fighting);

    TEST_ASSERT_TRUE(damageAtRealm5 > damageAtRealm4);
    TEST_ASSERT_EQUAL_INT(4, s5.playerAutoAttackCount);
}

// --- Execution ---
void test_execution_adds_bonus_damage_finishing_a_weakened_enemy(void) {
    ZoneMap m1 = makeZoneMap(0);
    ZoneState s6 = startZone(m1, 0);
    for (int i = 0; i < 500 && s6.phase != ZonePhase::Fighting; ++i) tickZone(s6, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s6.phase == ZonePhase::Fighting);
    s6.enemy.maxHp = 1000;
    s6.enemy.hp = static_cast<int>(s6.enemy.maxHp * 0.15); // already below the 20% threshold
    tickZone(s6, 1.0, 10.0, 6); // realm 6: no Execution yet
    int hpAtRealm6 = s6.enemy.hp;

    ZoneMap m2 = makeZoneMap(0);
    ZoneState s7 = startZone(m2, 0);
    for (int i = 0; i < 500 && s7.phase != ZonePhase::Fighting; ++i) tickZone(s7, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s7.phase == ZonePhase::Fighting);
    s7.enemy.maxHp = 1000;
    s7.enemy.hp = static_cast<int>(s7.enemy.maxHp * 0.15);
    tickZone(s7, 1.0, 10.0, 7); // realm 7: Execution active
    int hpAtRealm7 = s7.enemy.hp;

    TEST_ASSERT_TRUE(hpAtRealm7 < hpAtRealm6);
}

// --- Swift Feet ---
void test_swift_feet_increases_walking_speed(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s0 = startZone(m, 0);
    tickZone(s0, 0.1, 10.0, 0); // realm 0: no Swift Feet

    ZoneState s9 = startZone(m, 0); // fresh state, same map
    tickZone(s9, 0.1, 10.0, 9); // realm 9: Swift Feet active

    TEST_ASSERT_TRUE(s9.posX > s0.posX);
}

void test_swift_feet_shortens_jump_duration(void) {
    JumpArc normal = makeJumpArc(0.0f, 0.0f, 2.0f, 0.0f);
    JumpArc fast = makeJumpArc(0.0f, 0.0f, 2.0f, 0.0f, 1.3f);
    TEST_ASSERT_TRUE(fast.duration < normal.duration);
}

// --- Radiant Aura ---
void test_radiant_aura_ticks_extra_damage_independent_of_autoattack(void) {
    ZoneMap m1 = makeZoneMap(0);
    ZoneState s10 = startZone(m1, 0);
    for (int i = 0; i < 500 && s10.phase != ZonePhase::Fighting; ++i) tickZone(s10, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s10.phase == ZonePhase::Fighting);
    s10.enemy.hp = s10.enemy.maxHp;
    tickZone(s10, 2.0, 10.0, 10); // realm 10: no Radiant Aura yet
    int damageAtRealm10 = s10.enemy.maxHp - s10.enemy.hp;

    ZoneMap m2 = makeZoneMap(0);
    ZoneState s11 = startZone(m2, 0);
    for (int i = 0; i < 500 && s11.phase != ZonePhase::Fighting; ++i) tickZone(s11, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s11.phase == ZonePhase::Fighting);
    s11.enemy.hp = s11.enemy.maxHp;
    tickZone(s11, 2.0, 10.0, 11); // realm 11: Radiant Aura active, ticks once at t=2.0s
    int damageAtRealm11 = s11.enemy.maxHp - s11.enemy.hp;

    TEST_ASSERT_TRUE(damageAtRealm11 > damageAtRealm10);
}

// --- Undying Will ---
void test_undying_will_survives_one_fatal_hit_per_run(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    s.player.hp = 1;
    s.enemy.attackTimer = s.enemy.attackCooldownSeconds; // guarantees the enemy's attack lands
    int zoneRunIndexBefore = s.zoneRunIndex;
    tickZone(s, 0.01, 10.0, 13); // realm 13: Undying Will saves the player instead of restarting
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    TEST_ASSERT_EQUAL_INT(1, s.player.hp);
    TEST_ASSERT_TRUE(s.undyingWillUsedThisRun);
    TEST_ASSERT_EQUAL_INT(zoneRunIndexBefore, s.zoneRunIndex); // restartZone() would have bumped this

    // A second would-be-fatal hit in the same run must NOT be saved again.
    s.player.hp = 1;
    s.enemy.attackTimer = s.enemy.attackCooldownSeconds;
    tickZone(s, 0.01, 10.0, 13);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking); // restartZone() reset it this time
}

void test_without_undying_will_a_fatal_hit_restarts_the_zone(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.player.hp = 1;
    s.enemy.attackTimer = s.enemy.attackCooldownSeconds;
    tickZone(s, 0.01, 10.0, 12); // realm 12: no Undying Will yet
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp); // startZone() gave a fresh, full-HP player
}

// --- Empyrean Radiance ---
void test_empyrean_radiance_amplifies_skill_damage(void) {
    ZoneMap m1 = makeZoneMap(15);
    ZoneState s14 = startZone(m1, 15);
    for (int i = 0; i < 500 && s14.phase != ZonePhase::Fighting; ++i) tickZone(s14, 0.1, 10.0, 15);
    TEST_ASSERT_TRUE(s14.phase == ZonePhase::Fighting);
    s14.enemy.hp = 100000; s14.enemy.maxHp = 100000;
    s14.skill.timer = SKILLS[0].cooldownSeconds; // guarantees a skill fires this tick
    tickZone(s14, 0.01, 10.0, 14); // realm 14: no Empyrean Radiance yet
    int damageAtRealm14 = 100000 - s14.enemy.hp;

    ZoneMap m2 = makeZoneMap(15);
    ZoneState s15 = startZone(m2, 15);
    for (int i = 0; i < 500 && s15.phase != ZonePhase::Fighting; ++i) tickZone(s15, 0.1, 10.0, 15);
    TEST_ASSERT_TRUE(s15.phase == ZonePhase::Fighting);
    s15.enemy.hp = 100000; s15.enemy.maxHp = 100000;
    s15.skill.timer = SKILLS[0].cooldownSeconds;
    tickZone(s15, 0.01, 10.0, 15); // realm 15: Empyrean Radiance active
    int damageAtRealm15 = 100000 - s15.enemy.hp;

    TEST_ASSERT_TRUE(damageAtRealm15 > damageAtRealm14);
}
```

Add all 12 new `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_zone_state`
Expected: FAIL to build — `ZoneState` has no `playerAutoAttackCount`/`radiantAuraTimerSeconds`/`undyingWillUsedThisRun` fields yet, `makeJumpArc` doesn't accept a 5th argument.

- [ ] **Step 3: Add the new `ZoneState` fields to `lib/core/zone_state.h`**

Add three fields at the end of the `ZoneState` struct (after `bossJustDefeated`):
```cpp
    bool bossJustDefeated = false;       // pulses true on the single tickZone() call a boss dies;
                                          // reset every call
    int playerAutoAttackCount = 0;   // total landed player autoattacks this zone run (Soul Echo cadence)
    float radiantAuraTimerSeconds = 0.0f; // advances only while Fighting; reset in startZone()
    bool undyingWillUsedThisRun = false;  // latches true the first time it saves the player from
                                           // a fatal hit; reset in startZone()
};
```

Change `makeJumpArc`'s declaration:
```cpp
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY);
```
to:
```cpp
// speedMultiplier scales the effective travel speed used to compute the arc's duration
// (defaults to 1.0f, matching kWalkSpeedUnitsPerSec exactly) - zone_state.cpp passes the Swift
// Feet trait's live multiplier here so jump duration scales consistently with walk speed.
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY, float speedMultiplier = 1.0f);
```

- [ ] **Step 4: Update `lib/core/zone_state.cpp`**

Add `#include "traits.h"` to the top includes (alongside `#include "zone_state.h"` and `#include <cmath>`).

Change `makeJumpArc`'s definition:
```cpp
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
```
to:
```cpp
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY, float speedMultiplier) {
    JumpArc arc;
    arc.fromX = fromX;
    arc.fromY = fromY;
    arc.toX = toX;
    arc.toY = toY;
    arc.elapsed = 0.0f;
    float horizontalDistance = std::fabs(toX - fromX);
    float duration = horizontalDistance / (kWalkSpeedUnitsPerSec * speedMultiplier);
    arc.duration = duration > kMinJumpDuration ? duration : kMinJumpDuration;
    return arc;
}
```

In `startZone`, add the three new field resets right after `s.bossJustDefeated = false;`:
```cpp
    s.bossJustDefeated = false;
    s.playerAutoAttackCount = 0;
    s.radiantAuraTimerSeconds = 0.0f;
    s.undyingWillUsedThisRun = false;
    return s;
```

In the Walking phase branch of `tickZone`, change the step calculation:
```cpp
        float step = kWalkSpeedUnitsPerSec * static_cast<float>(dtSeconds);
```
to:
```cpp
        float step = kWalkSpeedUnitsPerSec * movementSpeedMultiplier(currentRealmIndex) *
                     static_cast<float>(dtSeconds);
```

And the jump-arc construction at the end of the Walking branch:
```cpp
                float landingX = next.x0 + kLandingMargin;
                state.jump = makeJumpArc(state.posX, platform.y, landingX, next.y);
                state.phase = ZonePhase::Jumping;
```
to:
```cpp
                float landingX = next.x0 + kLandingMargin;
                state.jump = makeJumpArc(state.posX, platform.y, landingX, next.y,
                                          movementSpeedMultiplier(currentRealmIndex));
                state.phase = ZonePhase::Jumping;
```

Replace the entire `// Fighting` section (from the `// Fighting` comment through the end of the function) with:
```cpp
    // Fighting
    if (hasSteadyBreath(currentRealmIndex)) {
        float newHp = static_cast<float>(state.player.hp) +
                       regenPerSecond(currentRealmIndex, state.player.maxHp) * static_cast<float>(dtSeconds);
        state.player.hp = newHp < static_cast<float>(state.player.maxHp)
                               ? static_cast<int>(newHp)
                               : state.player.maxHp;
    }

    int enemyHpBeforeAutoAttack = state.enemy.hp;
    tickCombat(state.player, state.enemy, dtSeconds, incomingDamageMultiplier(currentRealmIndex));
    bool playerAutoAttackLanded = state.enemy.hp < enemyHpBeforeAutoAttack;
    if (playerAutoAttackLanded) {
        state.playerAutoAttackCount += 1;
        if (hasSoulEcho(currentRealmIndex) && state.playerAutoAttackCount % kSoulEchoInterval == 0) {
            int bonus = static_cast<int>(state.player.attackDamage * kSoulEchoBonusMultiplier);
            state.enemy.hp -= bonus;
            if (state.enemy.hp < 0) state.enemy.hp = 0;
        }
        if (hasExecution(currentRealmIndex) && state.enemy.hp > 0 &&
            state.enemy.hp <= static_cast<int>(state.enemy.maxHp * kExecutionHpFraction)) {
            int bonus = static_cast<int>(state.player.attackDamage * kExecutionBonusMultiplier);
            state.enemy.hp -= bonus;
            if (state.enemy.hp < 0) state.enemy.hp = 0;
        }
    }

    if (hasRadiantAura(currentRealmIndex)) {
        state.radiantAuraTimerSeconds += static_cast<float>(dtSeconds);
        if (state.radiantAuraTimerSeconds >= kRadiantAuraIntervalSeconds) {
            state.radiantAuraTimerSeconds -= kRadiantAuraIntervalSeconds;
            if (state.enemy.hp > 0) {
                int auraDamage = static_cast<int>(state.player.attackDamage * kRadiantAuraDamageMultiplier);
                state.enemy.hp -= auraDamage;
                if (state.enemy.hp < 0) state.enemy.hp = 0;
            }
        }
    }

    int firedSkill = tickSkill(state.skill, dtSeconds, currentRealmIndex);
    if (firedSkill >= 0) {
        state.skillFiredThisTick = firedSkill;
        int skillDamage = static_cast<int>(state.player.attackDamage * SKILLS[firedSkill].damageMultiplier *
                                            skillDamageMultiplier(currentRealmIndex));
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

    // Undying Will: a would-be-fatal hit is intercepted here, once per zone run, before the
    // defeat check below ever sees a non-positive HP value.
    if (state.player.hp <= 0 && hasUndyingWill(currentRealmIndex) && !state.undyingWillUsedThisRun) {
        state.player.hp = 1;
        state.undyingWillUsedThisRun = true;
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

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_zone_state`
Expected: PASS, all cases including the 12 new ones.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS. Every existing `test_zone_state`/`test_zone_map`/`test_skills` case must still pass unmodified — all new trait behavior is realm-gated `false` at realm 0-and-below-each-threshold, which every existing test uses.

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_state.h lib/core/zone_state.cpp test/test_zone_state/test_zone_state.cpp
git commit -m "feat: wire all 8 realm identity traits into the zone combat/movement loop"
```

---

### Task 7: Main loop wiring — ascension auto-trigger and persistence

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `AscensionState`/`qiMultiplierForInsight`/`canAscend`/`attemptAscend` (Task 1), `qiPerSecond`/`tick`'s multiplier parameter (Task 2), `toSaveData`/`toAscensionState`'s ascension parameter (Task 3), `triggerAscensionFx`/`playAscensionSfx` (Task 8, declared but not yet defined — this task will not build standalone; Tasks 7 and 8 must land together or in this order with Task 8 immediately following, matching how the boss-encounters plan sequenced its FX task after its state-machine task).

This task has no automated test (`main.cpp` is Arduino glue) — verified by the embedded build in Step 3 below, run only after Task 8 provides the FX/SFX functions this task calls. Do Task 8 immediately before building.

- [ ] **Step 1: Add the ascension include and state variable**

In `src/main.cpp`, add to the includes (alongside the existing `#include "economy.h"`):
```cpp
#include "ascension.h"
```

In the anonymous namespace, add a new file-scope variable right after `GameState gState;`:
```cpp
GameState gState;
AscensionState gAscensionState;
```

- [ ] **Step 2: Wire ascension into `saveNow()`, boot, and the tick loop**

Change `saveNow()`:
```cpp
void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch, gBrightness, gVolume));
}
```
to:
```cpp
void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch, gBrightness, gVolume, gAscensionState));
}
```

In `setup()`, change the offline-earnings rate calculation:
```cpp
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState);
```
to:
```cpp
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState, qiMultiplierForInsight(toAscensionState(save).insight));
```

Right after `gState = toGameState(save);`, add:
```cpp
    gState = toGameState(save);
    gAscensionState = toAscensionState(save);
```

In `loop()`, change the tick call:
```cpp
        double dt = (now - gLastTickMs) / 1000.0;
        tick(gState, dt);
        gLastTickMs = now;
```
to:
```cpp
        double dt = (now - gLastTickMs) / 1000.0;
        tick(gState, dt, qiMultiplierForInsight(gAscensionState.insight));
        gLastTickMs = now;
```

- [ ] **Step 3: Add the ascension auto-trigger, immediately after the breakthrough loop**

Change:
```cpp
            while (canBreakthrough(gState)) {
                attemptBreakthrough(gState);
            }
            triggerRealmBreakthroughFx();
            playBreakthroughSfx();
        }
        // One purchase attempt per generator per tick (not loop-until-can't-afford) —
```
to:
```cpp
            while (canBreakthrough(gState)) {
                attemptBreakthrough(gState);
            }
            triggerRealmBreakthroughFx();
            playBreakthroughSfx();
        }
        // Ascension: checked once, after breakthroughs resolve (not interleaved) - a large
        // offline-earnings injection should finish climbing to realm 15 first, then ascend, in
        // the same tick. A plain `if`, not `while`: attemptAscend() resets qi to 0, and the next
        // ascension threshold is strictly positive, so a second ascension can never fire in the
        // same tick.
        if (canAscend(gState, gAscensionState)) {
            attemptAscend(gState, gAscensionState);
            triggerAscensionFx();
            playAscensionSfx();
        }
        // One purchase attempt per generator per tick (not loop-until-can't-afford) —
```

- [ ] **Step 4: Thread `gAscensionState` through both `drawHud` call sites**

Change both occurrences of:
```cpp
        drawHud(M5.Display, gState, gZoneState);
```
to:
```cpp
        drawHud(M5.Display, gState, gZoneState, gAscensionState);
```

(This will not compile until Task 9 updates `drawHud`'s signature — do Task 9 before building. Note this in the commit if built standalone; otherwise fold this step's verification into Task 9's build check.)

- [ ] **Step 5: Commit**

This task's changes only build successfully once Tasks 8 and 9 land (they add the FX/SFX functions and the `drawHud` parameter this task's code calls). Commit this task's diff together with Tasks 8 and 9 in Task 9's final commit step, OR commit here with a WIP note and verify the build at the end of Task 9. This plan uses the latter: proceed directly to Task 8 now without a standalone commit for this task.

---

### Task 8: Ascension FX/SFX

**Files:**
- Modify: `src/zone_view.h`
- Modify: `src/zone_view.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `void triggerAscensionFx()`, `void playAscensionSfx()` — called from Task 7's `main.cpp` code.

No native test (hardware glue) — verified by the embedded build in Task 9's final step.

- [ ] **Step 1: Declare the new functions in `src/zone_view.h`**

Add after the existing boss declarations:
```cpp
void triggerBossDefeatFx();
void playBossDefeatSfx();

// Ascension events: main.cpp fires these once per automatic ascension (gated on
// canAscend()/attemptAscend() in the main tick loop) - the biggest celebration in the game,
// distinct from a regular realm breakthrough.
void triggerAscensionFx();
void playAscensionSfx();
```

- [ ] **Step 2: Add ascension FX state to `src/zone_view.cpp`**

Add to the anonymous namespace, right after the existing boss-defeat state block (`kBossDefeatMaxRadiusPx`):
```cpp
// Ascension celebration burst state - the biggest event in the game, fires each time the
// player's entire cultivation life resets for a permanent Qi/sec bonus - a longer duration,
// bigger radius, more rays, and a bigger shake than any other celebration here, so it visibly
// reads as more significant than a regular realm breakthrough.
bool gAscensionFxActive = false;
uint32_t gAscensionFxStartMs = 0;
constexpr uint32_t kAscensionFxDurationMs = 1200; // longer than kBossDefeatFxDurationMs's 900ms
constexpr int kAscensionRays = 12;
constexpr float kAscensionMaxRadiusPx = 95.0f;
constexpr float kAscensionShakeAmplitudePx = 7.0f; // more than kBossEnrageShakeAmplitudePx's 5.0f
```

Add the drawing function right after `drawBossDefeatFx` (before the closing `} // namespace`):
```cpp
// An expanding violet/gold ring plus radiating rays centered on the character, bigger, longer-
// lived, and with its own shake contribution, unlike drawBreakthroughFx's - ascension is a much
// rarer, bigger milestone than a single realm breakthrough.
void drawAscensionFx(M5Canvas& canvas, int charX, int charY, uint32_t nowMs, float& shakeX, float& shakeY) {
    if (!gAscensionFxActive) return;
    uint32_t elapsed = nowMs - gAscensionFxStartMs;
    if (elapsed >= kAscensionFxDurationMs) { gAscensionFxActive = false; return; }

    float t = static_cast<float>(elapsed) / static_cast<float>(kAscensionFxDurationMs);
    float envelope = pulseEnvelope(t);
    int cy = charY - 20;
    int ringRadius = static_cast<int>(kAscensionMaxRadiusPx * t);

    canvas.drawCircle(charX, cy, ringRadius, TFT_VIOLET);
    if (ringRadius > 3) canvas.drawCircle(charX, cy, ringRadius - 3, TFT_GOLD);

    int rayLen = ringRadius + static_cast<int>(kAscensionMaxRadiusPx * 0.4f * envelope);
    for (int i = 0; i < kAscensionRays; ++i) {
        float angle = (2.0f * kPi / static_cast<float>(kAscensionRays)) * static_cast<float>(i);
        int ex = charX + static_cast<int>(std::cos(angle) * rayLen);
        int ey = cy + static_cast<int>(std::sin(angle) * rayLen);
        canvas.drawLine(charX, cy, ex, ey, TFT_GOLD);
    }

    shakeX += shakeOffset(t, kAscensionShakeAmplitudePx, 0.0f);
    shakeY += shakeOffset(t, kAscensionShakeAmplitudePx, kPi / 2.0f);
}
```

- [ ] **Step 3: Wire the drawing call into `renderZoneView`**

Change:
```cpp
    drawBossEnrageFx(canvas, nowMs, shakeX, shakeY);
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
```
to:
```cpp
    drawBossEnrageFx(canvas, nowMs, shakeX, shakeY);
    drawAscensionFx(canvas, charX, charY, nowMs, shakeX, shakeY);
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
```

- [ ] **Step 4: Add the trigger/SFX functions**

Add right after `playBossDefeatSfx`'s definition:
```cpp
void triggerAscensionFx() {
    gAscensionFxActive = true;
    gAscensionFxStartMs = millis();
}

void playAscensionSfx() {
    // The grandest fanfare in the game - a full ascending run ending on a sustained high note,
    // distinct from playBreakthroughSfx()'s shorter triad arpeggio, since ascension is a much
    // bigger, rarer milestone than a single realm breakthrough.
    M5.Speaker.tone(392.0f, 90);
    delay(90);
    M5.Speaker.tone(523.0f, 90);
    delay(90);
    M5.Speaker.tone(659.0f, 90);
    delay(90);
    M5.Speaker.tone(784.0f, 90);
    delay(90);
    M5.Speaker.tone(1568.0f, 320);
}
```

- [ ] **Step 5: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: PASS (this task touches only `src/`, which native tests don't compile).

- [ ] **Step 6: Commit**

```bash
git add src/zone_view.h src/zone_view.cpp
git commit -m "feat: add ascension celebration FX and fanfare"
```

Proceed directly to Task 9 — Task 7's `main.cpp` changes still won't build until it lands.

---

### Task 9: HUD ascension readout, and final wiring/build verification

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`

**Interfaces:**
- Consumes: `AscensionState`/`qiMultiplierForInsight` (Task 1).
- Produces: `drawHud`'s signature gains a trailing `const AscensionState&` parameter — this is the last piece Task 7's `main.cpp` call sites need.

- [ ] **Step 1: Update `src/ui.h`**

Add to the includes:
```cpp
#include "ascension.h"
```

Change:
```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone);
```
to:
```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone,
             const AscensionState& ascension);
```

- [ ] **Step 2: Update `src/ui.cpp`'s `drawHeader` and `drawHud`**

Change the (anonymous-namespace, internal) `drawHeader` signature and its left-label formatting:
```cpp
void drawHeader(M5GFX& display, const GameState& state) {
```
to:
```cpp
void drawHeader(M5GFX& display, const GameState& state, const AscensionState& ascension) {
```

Change:
```cpp
    char qiRateStr[24];
    formatQi(qiPerSecond(state), qiRateStr, sizeof(qiRateStr));
    char leftBuf[64];
    snprintf(leftBuf, sizeof(leftBuf), "%s  Qi/s %s", REALM_NAMES[state.realmIndex], qiRateStr);
```
to:
```cpp
    char qiRateStr[24];
    formatQi(qiPerSecond(state, qiMultiplierForInsight(ascension.insight)), qiRateStr, sizeof(qiRateStr));
    // Only shown once the player has ascended at least once, so a fresh game's header (the
    // common case for a first-time player) stays exactly as compact as it was before this
    // feature existed.
    char leftBuf[80];
    if (ascension.ascensionCount > 0) {
        char multBuf[16];
        snprintf(multBuf, sizeof(multBuf), "%.2f", qiMultiplierForInsight(ascension.insight));
        snprintf(leftBuf, sizeof(leftBuf), "%s  Qi/s %s  Asc %u (x%s)",
                 REALM_NAMES[state.realmIndex], qiRateStr, static_cast<unsigned>(ascension.ascensionCount),
                 multBuf);
    } else {
        snprintf(leftBuf, sizeof(leftBuf), "%s  Qi/s %s", REALM_NAMES[state.realmIndex], qiRateStr);
    }
```

Change `drawHud`'s signature and its internal call to `drawHeader`:
```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);
```
to:
```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone,
             const AscensionState& ascension) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state, ascension);
```

- [ ] **Step 3: Build the embedded target to verify everything compiles together**

Run: `pio run -e esp32p4_pioarduino`
Expected: `SUCCESS`. This is the first point Tasks 7, 8, and 9 all compile together — if it fails, check that Task 7's `main.cpp` edits (Steps 1-4) and Task 8's `zone_view.h`/`.cpp` edits both landed exactly as specified above.

- [ ] **Step 4: Run the full native suite one more time**

Run: `pio test -e native`
Expected: PASS, all suites (this task and Task 7 touch only `src/`, but this confirms nothing in `lib/core/` regressed across the whole feature).

- [ ] **Step 5: Commit Tasks 7, 8's remaining main.cpp wiring, and this task together**

```bash
git add src/main.cpp src/ui.h src/ui.cpp
git commit -m "feat: wire ascension into the main loop and HUD"
```

---

### Task 10: README documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a new section after "Boss Encounters" and before "Settings: brightness & volume"**

Insert (matching the existing section's heading level and citation style):

```markdown
### Ascension & Realm Identity

Cultivation used to dead-end at realm 15 (Empyrean Realm): `canBreakthrough()` returns `false`
forever once there, and Qi kept accumulating from the player's generators with nothing left to
spend it on. A new automated **ascension** system (`lib/core/ascension.{h,cpp}`) fixes this: once
the player reaches realm 15 and banks enough Qi past that point (a threshold that itself grows
with each successive ascension, mirroring how `REALM_QI_THRESHOLD` grows realm to realm), the
game automatically "ascends" - qi, generator counts, and realm index hard-reset to a fresh
game's starting values, and the Qi spent converts (`sqrt`-scaled, so very large late-game Qi
numbers yield sane, modest gains) into permanent **insight**, a prestige currency that never
resets and compounds into an ever-growing Qi/sec multiplier for every future run. No manual
trigger of any kind - it fires from the same automated tick loop that already drives
breakthroughs and generator purchases, checked once per tick immediately after breakthroughs
resolve. The header's stats readout gains an "Asc N (xM.MM)" suffix once the player has
ascended at least once, and stays exactly as compact as before for a fresh game that hasn't.

Separately, realm growth used to be lopsided: `SKILLS[]` only unlocks a new combat skill every
*other* realm (0, 2, 4, ... 14), leaving odd realms granting nothing beyond the existing linear
HP/damage scaling formula. A parallel **Realm Identity** trait table
(`lib/core/traits.{h,cpp}`) fills exactly those odd realms (1, 3, 5, 7, 9, 11, 13, 15) with one
new passive, always-on, fully automatic trait each - Iron Skin (damage reduction), Steady
Breath (HP regen while fighting), Soul Echo (every 4th landed autoattack echoes for bonus
damage), Execution (bonus damage finishing a weakened foe), Swift Feet (faster platform
movement), Radiant Aura (a periodic damage tick independent of autoattack/skill cooldowns),
Undying Will (survives one fatal hit per zone run), and the capstone Empyrean Radiance
(amplifies all skill damage). Combined with the existing skill table, every single realm from 0
to 15 now grants something new. All eight are deterministic (no RNG, matching this project's
combat philosophy throughout) and implemented as small, targeted additions inside `tickZone()`'s
existing Walking/Fighting branches - no new `ZonePhase`, and `tickCombat()`'s only change is one
defaulted `incomingDamageMultiplier` parameter (Iron Skin), following the same
backward-compatible-defaulted-parameter pattern `makeZoneMap`'s `seed`/`isBossZone` established.

Since ascension resets `realmIndex` to 0, both systems compound together across repeat runs: a
higher `insight` multiplier means a faster subsequent climb back through all 16 realms and all
16 unlocks (8 skills, 8 traits) each time.

Both are unit-tested end to end in `test/test_ascension/` and `test/test_traits/`, plus new
integration cases in `test/test_zone_state/` and `test/test_zone_combat/` for how the traits
hook into live combat/movement - no device required.

**Known limitation — not yet validated on real hardware.** The ascension threshold-growth and
insight-to-multiplier constants, and all eight trait magnitudes (damage reduction/regen
rate/echo interval/execution threshold/movement speed/aura interval/skill multiplier), are
first-pass numbers set by inspection here, not yet run through a simulation sweep the way boss
stats were - a follow-up tuning pass, not a defect in the mechanics themselves. The ascension
fanfare/FX and the HUD's new "Asc" readout are likewise unflashed, same caveat every prior
spec in this project has carried.

Design spec: `docs/superpowers/specs/2026-08-29-ascension-and-realm-identity-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-29-ascension-and-realm-identity.md`
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document ascension and realm identity traits"
```

---

## Final Verification

- [ ] **Run the complete native suite one last time**

Run: `pio test -e native`
Expected: PASS, every suite (the original 15 plus the 2 new ones - `test_ascension`, `test_traits`).

- [ ] **Run the complete embedded build one last time**

Run: `pio run -e esp32p4_pioarduino`
Expected: `SUCCESS`.

- [ ] **Review the full diff for this feature**

Run: `git log --oneline -10` and `git diff origin/main --stat` (if `origin/main` is behind) to confirm every task's commit landed and nothing was left uncommitted.
