# Raycasting-Only Revamp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the buggy idle-game screen (crystal rasterizer + shop HUD) and make the already-autoplaying Secret Realm raycasting trial the app's only screen, shown from first boot, forever — plus fix two real bugs surfaced while tracing the trial code: instant-snap camera turning and a frozen realm index on restart.

**Architecture:** `lib/core/` (hardware-agnostic, unit-tested game logic) loses the crystal rasterizer pipeline entirely; `trial_state.cpp`'s tick/restart logic gains turn-rate-limited camera facing and a live (not frozen) realm index. `src/` (Arduino glue) collapses from a two-screen `ViewMode` switch to a single always-on raycast screen: thin header → raycast viewport (top half of the space below the header) → a read-only stats/settings panel (bottom half).

**Tech Stack:** C++17, PlatformIO, Arduino framework, M5Unified/M5GFX (ESP32-P4 target `esp32p4_pioarduino`); Unity test framework on the `native` PlatformIO environment for `lib/core` unit tests (no device required).

**Spec:** `docs/superpowers/specs/2026-08-27-raycasting-only-revamp-design.md`

## Global Constraints

- Display is 720×1280 in M5GFX's portrait logical coordinates (confirmed against the M5GFX source; this is *not* the physical landscape orientation the "tablet" name suggests) — all layout math is computed at runtime from `M5.Display.width()`/`.height()`, never hardcoded.
- Autoplay only — no manual movement/navigation controls are added. The only tappable UI left anywhere is the brightness/volume rows.
- No imported image/audio assets anywhere in this codebase (procedural only) — unaffected by this plan, but don't introduce any.
- No physical Tab5 is available in this environment. Verification ceiling is: `pio test -e native` (full `lib/core` unit suite, confirmed passing at 87/87 test cases before this plan starts) and `pio run -e esp32p4_pioarduino` (compiles/links the real target, confirmed succeeding before this plan starts — RAM 5.5%, Flash 49.2% at baseline). Flashing and on-device FPS/visual verification (including whether `kTrialZoom` needs retuning for the new half-height viewport) is explicitly out of scope, left for whoever has the device.

---

## Task 1: Delete the crystal rasterizer pipeline

**Files:**
- Delete: `lib/core/mesh.h`, `lib/core/mesh.cpp`, `lib/core/rasterizer.h`, `lib/core/rasterizer.cpp`, `lib/core/framebuffer.h`
- Delete: `test/test_mesh/test_mesh.cpp` (and the now-empty `test/test_mesh/` directory)
- Delete: `test/test_rasterizer/test_rasterizer.cpp` (and the now-empty `test/test_rasterizer/` directory)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new. This task only removes code; nothing later in this plan depends on anything being deleted here beyond "it's gone." `lib/core/color.h` (the `RGB` struct) is explicitly **not** deleted — `trial_textures.h` and the Task 3 rewrite of `trial_view.cpp` both still need it.

`src/main.cpp`, `src/ui.h`, and `src/trial_view.cpp` still `#include` these headers at the end of this task — the ESP32 target (`esp32p4_pioarduino`) will **not** compile until Task 3 removes those references. This is expected and fine: this task's verification is the `native` test environment only, which never builds `src/` at all (`platformio.ini`'s `[env:native]` sets `build_src_filter = -<*>`, excluding it).

- [ ] **Step 1: Delete the files**

```bash
git rm lib/core/mesh.h lib/core/mesh.cpp lib/core/rasterizer.h lib/core/rasterizer.cpp lib/core/framebuffer.h
git rm test/test_mesh/test_mesh.cpp test/test_rasterizer/test_rasterizer.cpp
rmdir test/test_mesh test/test_rasterizer
```

- [ ] **Step 2: Run the native test suite to confirm nothing else broke**

Run: `pio test -e native`
Expected: `test_mesh` and `test_rasterizer` no longer appear in the summary; all remaining suites still pass. Baseline before this task was 87 test cases across 14 suites (confirmed passing); `test_mesh` contributed 7 of those and `test_rasterizer` 5, so the summary line should now read `75 test cases: 75 succeeded` across 12 suites — zero failures, zero unexpected suite disappearances beyond those two.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "Remove the crystal rasterizer pipeline

The idle screen it rendered is going away in favor of making the
raycasting trial the app's only screen; mesh/rasterizer/framebuffer
have no other callers once that screen is deleted."
```

---

## Task 2: Fix trial_state's camera turning and live-realm-index restart

**Files:**
- Modify: `lib/core/trial_state.h`
- Modify: `lib/core/trial_state.cpp`
- Modify: `test/test_trial_state/test_trial_state.cpp`

**Interfaces:**
- Consumes: `TrialMap`/`Waypoint`/`EnemySpawn` from `trial_map.h`, `CombatantState`/`makePlayerCombatant`/`makeEnemyCombatant`/`tickCombat`/`isDefeated` from `trial_combat.h` (all unchanged).
- Produces (for Task 3's `main.cpp`/`ui.cpp` rewrite to consume):
  - `TrialState startTrial(const TrialMap& map, int realmIndex)` — **signature unchanged**.
  - `void tickTrial(TrialState& state, double dtSeconds, double proposedReward, int currentRealmIndex)` — **gains a trailing `int currentRealmIndex` parameter.**
  - `void restartTrial(TrialState& state, int currentRealmIndex)` — **gains a trailing `int currentRealmIndex` parameter.**
  - `TrialState.realmIndexAtStart` field is **removed** — nothing outside `trial_state.cpp` ever read it (confirmed by repo-wide grep), and it's dead once restart no longer reuses it.
  - `constexpr float kTurnRateRadiansPerSec` — new, exported from `trial_state.h` in case a later tuning pass wants to reference it (mirrors how `kTravelSpeed`/`kEncounterRadius` are already exported).

- [ ] **Step 1: Update `trial_state.h`**

Replace the whole file:

```cpp
#pragma once
#include <vector>
#include "trial_map.h"
#include "trial_combat.h"

enum class TrialPhase { Traveling, Fighting, Cleared };

constexpr float kTravelSpeed = 1.5f;      // grid units per second
constexpr float kEncounterRadius = 0.3f;  // distance at which a live enemy engages the player
constexpr float kTurnRateRadiansPerSec = 3.14159265f; // 180 deg/sec -> a 90-degree corner turn
                                                        // (the only kind this maze has) takes
                                                        // about half a second to complete.

struct TrialState {
    TrialMap map;
    int currentWaypointIndex = 0;
    float posX = 0.0f;
    float posY = 0.0f;
    float facingRadians = 0.0f;
    TrialPhase phase = TrialPhase::Traveling;
    CombatantState player;
    int currentEnemyIndex = -1;
    CombatantState enemy;
    std::vector<bool> enemiesDefeated;
    double qiRewardPending = 0.0;
};

// Fresh trial at the route's start, already facing toward the first waypoint; player combat
// stats derive from realmIndex.
TrialState startTrial(const TrialMap& map, int realmIndex);

// Advances the trial by dtSeconds. While Traveling: checks for a live, undefeated enemy within
// kEncounterRadius (entering Fighting if found), otherwise eases facingRadians toward the
// current waypoint's direction at kTurnRateRadiansPerSec (never snapping instantly) while
// moving straight toward it, advancing to the next waypoint on arrival, or to Cleared (setting
// qiRewardPending = proposedReward) if the final waypoint is reached with no enemies left
// undefeated. While Fighting: resolves one combat tick; on enemy defeat, marks it defeated and
// returns to Traveling; on player defeat, calls restartTrial(state, currentRealmIndex). No-op
// once Cleared (call restartTrial to loop again). `currentRealmIndex` should be the caller's
// *live* realm index (e.g. GameState.realmIndex), not whatever realm the trial originally
// started at — it's only consulted at a restart boundary (player defeat here), so it can't
// change player stats mid-fight.
void tickTrial(TrialState& state, double dtSeconds, double proposedReward, int currentRealmIndex);

// Resets to the route's start with full player HP (recomputed from currentRealmIndex, not
// whatever realm the trial last started at), no enemies defeated, and qiRewardPending == 0.0,
// keeping `map`.
void restartTrial(TrialState& state, int currentRealmIndex);
```

- [ ] **Step 2: Update `trial_state.cpp`**

Replace the whole file:

```cpp
#include "trial_state.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

// Signed difference `to - from`, wrapped to (-pi, pi] so a heading always turns the short way
// around the +-pi wrap boundary instead of spinning the long way around. This maze only ever
// needs 90-degree turns, but the math is general.
float shortestAngleDiff(float from, float to) {
    float diff = std::fmod(to - from, kTwoPi);
    if (diff > kPi) diff -= kTwoPi;
    if (diff < -kPi) diff += kTwoPi;
    return diff;
}
} // namespace

TrialState startTrial(const TrialMap& map, int realmIndex) {
    TrialState s;
    s.map = map;
    s.posX = map.route[0].x;
    s.posY = map.route[0].y;
    // Start already standing at route[0], so head toward route[1] immediately rather than
    // toward route[0] itself - targeting index 0 would make the very first tick see "already
    // arrived" (distance ~0) and skip movement for a step instead of heading anywhere.
    s.currentWaypointIndex = (map.route.size() > 1) ? 1 : 0;
    // Face the first waypoint immediately instead of leaving facingRadians at its 0.0f default -
    // that default only happens to already match this specific map's first segment (due east),
    // which isn't something a differently-shaped map could rely on.
    if (map.route.size() > 1) {
        s.facingRadians = std::atan2(map.route[1].y - s.posY, map.route[1].x - s.posX);
    }
    s.phase = TrialPhase::Traveling;
    s.player = makePlayerCombatant(realmIndex);
    s.currentEnemyIndex = -1;
    s.enemiesDefeated.assign(map.enemies.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartTrial(TrialState& state, int currentRealmIndex) {
    TrialMap map = state.map; // preserve across reassignment below
    state = startTrial(map, currentRealmIndex);
}

namespace {
int findUndefeatedEnemyInRange(const TrialState& state) {
    for (size_t i = 0; i < state.map.enemies.size(); ++i) {
        if (state.enemiesDefeated[i]) continue;
        const EnemySpawn& e = state.map.enemies[i];
        float dx = e.x - state.posX;
        float dy = e.y - state.posY;
        if (std::sqrt(dx * dx + dy * dy) <= kEncounterRadius) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool allEnemiesDefeated(const TrialState& state) {
    for (bool defeated : state.enemiesDefeated) {
        if (!defeated) return false;
    }
    return true;
}
} // namespace

void tickTrial(TrialState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == TrialPhase::Cleared) return;

    if (state.phase == TrialPhase::Traveling) {
        int engaged = findUndefeatedEnemyInRange(state);
        if (engaged >= 0) {
            state.phase = TrialPhase::Fighting;
            state.currentEnemyIndex = engaged;
            const EnemySpawn& spawn = state.map.enemies[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        const Waypoint& target = state.map.route[static_cast<size_t>(state.currentWaypointIndex)];
        float dx = target.x - state.posX;
        float dy = target.y - state.posY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 0.05f) {
            bool isLastWaypoint =
                state.currentWaypointIndex == static_cast<int>(state.map.route.size()) - 1;
            if (isLastWaypoint && allEnemiesDefeated(state)) {
                state.phase = TrialPhase::Cleared;
                state.qiRewardPending = proposedReward;
            } else if (!isLastWaypoint) {
                state.currentWaypointIndex++;
            }
            return;
        }

        // Ease facingRadians toward the travel direction at a fixed turn rate instead of
        // snapping instantly to it in one tick - an instant same-tick jump reads as a hard cut
        // once rendered at any real frame rate, not as a turn. Movement below is unaffected:
        // position always steps straight toward the target regardless of how far the camera
        // has turned to face it yet.
        float desiredFacing = std::atan2(dy, dx);
        float diff = shortestAngleDiff(state.facingRadians, desiredFacing);
        float maxStep = kTurnRateRadiansPerSec * static_cast<float>(dtSeconds);
        if (diff > maxStep) diff = maxStep;
        if (diff < -maxStep) diff = -maxStep;
        state.facingRadians += diff;

        float step = kTravelSpeed * static_cast<float>(dtSeconds);
        if (step > dist) step = dist;
        state.posX += (dx / dist) * step;
        state.posY += (dy / dist) * step;
        return;
    }

    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    if (isDefeated(state.enemy)) {
        state.enemiesDefeated[static_cast<size_t>(state.currentEnemyIndex)] = true;
        state.currentEnemyIndex = -1;
        state.phase = TrialPhase::Traveling;
    } else if (isDefeated(state.player)) {
        restartTrial(state, currentRealmIndex);
    }
}
```

- [ ] **Step 3: Update `test/test_trial_state/test_trial_state.cpp`**

Replace the whole file:

```cpp
#include <unity.h>
#include "trial_state.h"

void setUp(void) {}
void tearDown(void) {}

void test_start_trial_begins_at_route_start_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].x, s.posX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].y, s.posY);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_EQUAL_INT(100, s.player.maxHp); // realmIndex 0
}

void test_tick_moves_toward_next_waypoint(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    float startX = s.posX;
    tickTrial(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.posX > startX); // route[1] is to the right of route[0]
}

void test_reaching_enemy_enters_fighting(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    // Drive many ticks toward the first enemy at (4.5, 1.5); travel speed and encounter
    // radius are internal, so tick generously and assert the phase transition happened.
    for (int i = 0; i < 200 && s.phase == TrialPhase::Traveling; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentEnemyIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_enemy_resumes_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    for (int i = 0; i < 500 && s.phase == TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_TRUE(s.enemiesDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack, well before it could win
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);

    // Stop at the exact tick the defeat-triggered reset happens, rather than running a fixed
    // number of further ticks - after the reset the (now full-HP) player immediately walks
    // back toward the same still-undefeated enemy and may re-engage it before an arbitrary
    // fixed tick budget elapses, which would assert on that unrelated rematch instead of the
    // reset this test is actually checking.
    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
        if (s.phase == TrialPhase::Traveling) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].x, s.posX);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
}

void test_clearing_all_enemies_and_reaching_goal_sets_reward(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // strong enough to one-shot-ish every enemy
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_trial_resets_state(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6);
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0, 6);
    }
    restartTrial(s, 6);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.enemiesDefeated[0]);
}

// A small synthetic map (not the real Secret Realm maze) isolating exactly one 90-degree turn,
// with no enemies, so the turning behavior below can be asserted without depending on the real
// maze's encounter timing.
TrialMap makeStraightThenTurnMap() {
    TrialMap m;
    m.grid.width = 10;
    m.grid.height = 10;
    m.grid.cells.assign(100, 0); // all open floor - tickTrial never checks wall collision
    m.route = {
        {1.0f, 1.0f}, // start
        {5.0f, 1.0f}, // due east of start
        {5.0f, 5.0f}, // due south of the previous waypoint: a clean 90-degree turn
    };
    m.enemies = {};
    return m;
}

void test_turn_is_gradual_not_instant(void) {
    TrialMap m = makeStraightThenTurnMap();
    TrialState s = startTrial(m, 0);
    // Walk the first (east) leg to completion; facing stays ~0 rad (east) the whole way, since
    // that leg needs no turning.
    for (int i = 0; i < 200 && s.currentWaypointIndex < 2; ++i) {
        tickTrial(s, 0.05, 10.0, 0);
    }
    TEST_ASSERT_EQUAL_INT(2, s.currentWaypointIndex); // now targeting (5,5): due south - a 90-degree turn
    float facingAtTurnStart = s.facingRadians;

    tickTrial(s, 0.05, 10.0, 0); // one small tick into the turn
    float facingAfterOneTick = s.facingRadians;
    float target = 1.57079633f; // south: atan2(+dy, 0) = pi/2

    TEST_ASSERT_TRUE(facingAfterOneTick > facingAtTurnStart); // it started turning...
    TEST_ASSERT_TRUE((target - facingAfterOneTick) > 0.1f);   // ...but hasn't snapped straight to the target

    // Keep ticking; the turn should fully converge well before the character reaches the
    // second waypoint (a 4-unit leg at kTravelSpeed=1.5 units/s takes ~2.7s, far more than the
    // ~0.5s this 90-degree turn needs at kTurnRateRadiansPerSec = pi rad/s).
    for (int i = 0; i < 40; ++i) {
        tickTrial(s, 0.05, 10.0, 0);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target, s.facingRadians);
}

void test_restart_uses_current_realm_index_not_frozen_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0); // started weak (realm 0)
    // Simulate the hidden cultivation economy having advanced to realm 4 by the time this
    // restart happens.
    restartTrial(s, 4);
    CombatantState expected = makePlayerCombatant(4);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
    TEST_ASSERT_EQUAL_INT(expected.attackDamage, s.player.attackDamage);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.hp); // full HP at the new cap after restart
}

void test_tick_trial_restart_on_defeat_uses_passed_in_realm_index(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0, 5); // economy has since advanced to realm 5 by the time of defeat
        if (s.phase == TrialPhase::Traveling) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    CombatantState expected = makePlayerCombatant(5);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_start_trial_begins_at_route_start_traveling);
    RUN_TEST(test_tick_moves_toward_next_waypoint);
    RUN_TEST(test_reaching_enemy_enters_fighting);
    RUN_TEST(test_defeating_enemy_resumes_traveling);
    RUN_TEST(test_player_defeat_resets_to_start);
    RUN_TEST(test_clearing_all_enemies_and_reaching_goal_sets_reward);
    RUN_TEST(test_restart_trial_resets_state);
    RUN_TEST(test_turn_is_gradual_not_instant);
    RUN_TEST(test_restart_uses_current_realm_index_not_frozen_start);
    RUN_TEST(test_tick_trial_restart_on_defeat_uses_passed_in_realm_index);
    return UNITY_END();
}
```

- [ ] **Step 4: Run the test suite and confirm it's green**

Run: `pio test -e native -f test_trial_state`
Expected: all 10 tests PASS. (If `test_turn_is_gradual_not_instant` fails here, re-check Step 2 was applied — the turn-rate limiting must already be in place before this step, since Steps 2 and 3 were written together in this task rather than as a separate red/green cycle.)

- [ ] **Step 5: Run the full native suite**

Run: `pio test -e native`
Expected: all suites pass, zero failures.

- [ ] **Step 6: Commit**

```bash
git add lib/core/trial_state.h lib/core/trial_state.cpp test/test_trial_state/test_trial_state.cpp
git commit -m "Fix instant-snap camera turning and frozen restart realm index

Traced via a native repro: the trial's camera reaches the correct
heading at each maze corner but jumps there in a single tick, which
reads as a hard cut rather than a turn once rendered. Also,
restartTrial() reused whatever realm the trial first started at
forever, so the player's combat stats could never improve even as
the hidden cultivation economy advanced - both matter once this mode
runs unattended indefinitely instead of being enterable/exitable."
```

---

## Task 3: Single-screen layout — rewrite `ui.h`/`ui.cpp`, `trial_view.h`/`trial_view.cpp`, and `main.cpp`

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/trial_view.h`
- Modify: `src/trial_view.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `TrialState`/`TrialPhase`/`tickTrial`/`restartTrial`/`startTrial` from Task 2's `trial_state.h` (new signatures), `GameState`/`REALM_NAMES`/`REALM_QI_THRESHOLD`/`qiPerSecond`/`tick`/`canBreakthrough`/`attemptBreakthrough`/`purchaseGenerator`/`NUM_GENERATORS` from `economy.h` (unchanged), `SaveData`/`toSaveData`/`toGameState` from `save.h` (unchanged), `nvsLoadSave`/`nvsWriteSave` from `nvs_store.h` (unchanged), `readRtcEpochSeconds`/`writeRtcFromEpochSeconds` from `rtc_store.h` (unchanged), `computeOfflineEarnings` from `offline_earnings.h` (unchanged), `clampBrightness`/`clampVolume`/`kMinBrightness`/`kMaxBrightness`/`kMaxVolume`/`kSettingsStep` from `settings.h` (unchanged), `makeSecretRealmMap` from `trial_map.h` (unchanged), `Rect`/`rectContains` from `hittest.h` (unchanged).
- Produces: `int raycastViewportBottom(int screenH)` (new, in `ui.h`) — the y-coordinate where the raycast viewport ends and the stats panel begins; consumed by both `ui.cpp` and `trial_view.cpp` so they can't disagree about the split. `void drawHud(M5GFX&, const GameState&, const TrialState&, uint8_t brightness, uint8_t volume)` (signature changed — now takes a `const TrialState&`). `int hitTestHud(int touchX, int touchY)` (signature changed — the `inTrialMode` parameter is gone, there's only one mode). `HudButton` enum now only has `HUD_BUTTON_NONE`/`_BRIGHTNESS_DOWN`/`_BRIGHTNESS_UP`/`_VOLUME_DOWN`/`_VOLUME_UP`.

None of these three files can be independently verified — `ui.cpp` and `trial_view.cpp` only compile as part of a full firmware link (the `native` PlatformIO environment excludes all of `src/`), and `main.cpp` calls into both. This task's steps are ordered file-by-file for reviewability, but the only real checkpoint is the full build at the end.

- [ ] **Step 1: Rewrite `src/ui.h`**

```cpp
#pragma once
#include <cstddef>
#include <M5Unified.h>
#include "economy.h"
#include "trial_state.h"
#include "hittest.h"

// Button ids returned by hitTestHud(); -1 means "no button at that point." The brightness/
// volume rows are the only tappable elements left anywhere on screen - every other stat is
// read-only, driven entirely by automation.
enum HudButton {
    HUD_BUTTON_NONE = -1,
    HUD_BUTTON_BRIGHTNESS_DOWN = 103,
    HUD_BUTTON_BRIGHTNESS_UP = 104,
    HUD_BUTTON_VOLUME_DOWN = 105,
    HUD_BUTTON_VOLUME_UP = 106,
};

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
int raycastViewportBottom(int screenH);

// Must be called once (e.g. from setup()) before the first drawHud() call — allocates the
// offscreen header/panel sprites sized to `display`.
void initHud(M5GFX& display);

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

// Hit-tests a touch point against the brightness/volume rows - the only tappable elements left.
int hitTestHud(int touchX, int touchY);

// Compact K/M/B display formatting for Qi-scale numbers (e.g. "2.2M" instead of
// "2200000"); exposed so main.cpp's welcome-back screen can format consistently
// with the rest of the HUD. `outLen` must cover the worst case (sign + digits +
// suffix + NUL); 24 bytes is comfortably enough for any value this game reaches.
void formatQi(double v, char* out, size_t outLen);
```

- [ ] **Step 2: Rewrite `src/ui.cpp`**

```cpp
#include "ui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Compact display formatting for Qi-scale numbers, which grow into the tens of
// millions (REALM_QI_THRESHOLD tops out at 27,000,000) — a raw "%.0f" would
// overflow a bar's label at any reasonable text size. Declared in ui.h (not
// anonymous-namespace-local) so main.cpp's welcome-back screen can reuse it.
void formatQi(double v, char* out, size_t outLen) {
    double av = v < 0 ? -v : v;
    const char* sign = v < 0 ? "-" : "";
    if (av < 1000.0) {
        snprintf(out, outLen, "%s%.1f", sign, av);
    } else if (av < 1e6) {
        snprintf(out, outLen, "%s%.1fK", sign, av / 1e3);
    } else if (av < 1e9) {
        snprintf(out, outLen, "%s%.1fM", sign, av / 1e6);
    } else {
        snprintf(out, outLen, "%s%.1fB", sign, av / 1e9);
    }
}

int raycastViewportBottom(int screenH) {
    return kHeaderHeight + (screenH - kHeaderHeight) / 2;
}

namespace {

// ---- Layout tuning ----
constexpr int kPanelTopPad = 12;
constexpr int kSectionGap = 10;
constexpr int kBreakthroughBarHeight = 40;
constexpr int kHpBarHeight = 36;
constexpr int kRouteBarHeight = 28;
constexpr int kSettingsRowHeight = 48; // compact: one row each for brightness and volume

struct Layout {
    int screenW = 0;
    int screenH = 0;
    int panelY0 = 0;      // absolute y where the stats panel (below the raycast viewport) starts
    int panelH = 0;
    int breakthroughY = 0;
    int playerHpY = 0;
    int enemyHpY = 0;
    int routeY = 0;
    int brightnessY = 0;
    int volumeY = 0;
};
Layout gLayout;

void computeLayout(int screenW, int screenH) {
    gLayout.screenW = screenW;
    gLayout.screenH = screenH;
    gLayout.panelY0 = raycastViewportBottom(screenH);
    gLayout.panelH = screenH - gLayout.panelY0;

    int y = gLayout.panelY0 + kPanelTopPad;
    gLayout.breakthroughY = y; y += kBreakthroughBarHeight + kSectionGap;
    gLayout.playerHpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.enemyHpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.routeY = y; y += kRouteBarHeight + kSectionGap;
    gLayout.brightnessY = y; y += kSettingsRowHeight + kSectionGap;
    gLayout.volumeY = y;
}

Rect breakthroughRect() { return Rect{0, gLayout.breakthroughY, gLayout.screenW, kBreakthroughBarHeight}; }
Rect playerHpRect() { return Rect{0, gLayout.playerHpY, gLayout.screenW, kHpBarHeight}; }
Rect enemyHpRect() { return Rect{0, gLayout.enemyHpY, gLayout.screenW, kHpBarHeight}; }
Rect routeRect() { return Rect{0, gLayout.routeY, gLayout.screenW, kRouteBarHeight}; }
Rect brightnessRowRect() { return Rect{0, gLayout.brightnessY, gLayout.screenW, kSettingsRowHeight}; }
Rect volumeRowRect() { return Rect{0, gLayout.volumeY, gLayout.screenW, kSettingsRowHeight}; }

// Each settings row is one tappable strip split into a left ("-") and right ("+") half,
// rather than four separate button rects, to keep the panel footprint compact.
Rect leftHalf(const Rect& r) { return Rect{r.x, r.y, r.w / 2, r.h}; }
Rect rightHalf(const Rect& r) { return Rect{r.x + r.w / 2, r.y, r.w - r.w / 2, r.h}; }

M5Canvas* gHeaderCanvas = nullptr;
M5Canvas* gPanelCanvas = nullptr;

// Picks the largest text size in [1, startSize] at which `text` fits within
// maxWidth (measured with textWidth(), not assumed), sets it on `canvas`, and
// returns it. Falls back to size 1 (smallest supported) if nothing fits.
int fitTextSize(M5Canvas& canvas, const char* text, int maxWidth, int startSize) {
    for (int size = startSize; size >= 1; --size) {
        canvas.setTextSize(size);
        if (canvas.textWidth(text) <= maxWidth) return size;
    }
    canvas.setTextSize(1);
    return 1;
}

void drawLeftAligned(M5Canvas& canvas, const char* text, int x, int yCenter, int maxWidth, int startSize,
                      uint16_t fg, uint16_t bg) {
    fitTextSize(canvas, text, maxWidth, startSize);
    canvas.setTextColor(fg, bg);
    canvas.setCursor(x, yCenter - canvas.fontHeight() / 2);
    canvas.print(text);
}

void drawRightAligned(M5Canvas& canvas, const char* text, int xRight, int yCenter, int maxWidth, int startSize,
                       uint16_t fg, uint16_t bg) {
    fitTextSize(canvas, text, maxWidth, startSize);
    int w = canvas.textWidth(text);
    canvas.setTextColor(fg, bg);
    canvas.setCursor(xRight - w, yCenter - canvas.fontHeight() / 2);
    canvas.print(text);
}

// Draws a two-tone progress bar (filled portion in `fillColor`, unfilled in dark grey) with a
// left-aligned label overlaid in transparent white text - a solid fg/bg color pair would only
// match one of the bar's two background colors, so this uses the single-argument
// setTextColor() (transparent background, only glyph pixels drawn) instead. `fraction` is
// clamped to [0,1] so a caller passing a raw ratio can't overflow the bar.
void drawBar(M5Canvas& canvas, const Rect& r, float fraction, uint16_t fillColor, const char* label) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    int ly = r.y - gLayout.panelY0;
    canvas.fillRect(0, ly, r.w, r.h, TFT_DARKGREY);
    int fillW = static_cast<int>(r.w * fraction);
    if (fillW > 0) canvas.fillRect(0, ly, fillW, r.h, fillColor);
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(12, ly + (r.h - canvas.fontHeight()) / 2);
    canvas.print(label);
}

void drawHeader(M5GFX& display, const GameState& state) {
    M5Canvas& hdr = *gHeaderCanvas;
    constexpr uint16_t kHeaderBg = 0x18E3; // dark navy-grey, distinct from the panel's black
    hdr.fillScreen(kHeaderBg);

    char qiRateStr[24];
    formatQi(qiPerSecond(state), qiRateStr, sizeof(qiRateStr));
    char leftBuf[64];
    snprintf(leftBuf, sizeof(leftBuf), "%s  Qi/s %s", REALM_NAMES[state.realmIndex], qiRateStr);

    // No clock/time-of-day display: without NTP/internet sync (out of scope for this
    // project), a displayed clock would silently drift from real time, which is worse
    // than not showing one. The RTC is still seeded/used for offline-earnings elapsed-
    // time math (see rtc_store.h) — that's unaffected by not displaying it.
    int32_t batteryLevel = M5.Power.getBatteryLevel(); // -1 if unavailable (see M5Unified Power_Class)
    auto chargeState = M5.Power.isCharging();           // is_charging_t, NOT a bool
    bool charging = (chargeState == decltype(chargeState)::is_charging);

    char rightBuf[16];
    if (batteryLevel < 0) {
        snprintf(rightBuf, sizeof(rightBuf), "Batt --");
    } else {
        snprintf(rightBuf, sizeof(rightBuf), "%ld%%%s", static_cast<long>(batteryLevel), charging ? "+" : "");
    }

    int rightMaxWidth = gLayout.screenW / 4;
    int leftMaxWidth = gLayout.screenW - rightMaxWidth - 24;
    int headerCenterY = kHeaderHeight / 2;

    drawLeftAligned(hdr, leftBuf, 12, headerCenterY, leftMaxWidth, 2, TFT_WHITE, kHeaderBg);
    drawRightAligned(hdr, rightBuf, gLayout.screenW - 12, headerCenterY, rightMaxWidth, 2, TFT_WHITE, kHeaderBg);

    hdr.pushSprite(0, 0);
}

} // namespace

void initHud(M5GFX& display) {
    if (gPanelCanvas) return; // already initialized; safe to call more than once
    computeLayout(display.width(), display.height());

    gHeaderCanvas = new M5Canvas(&display);
    gHeaderCanvas->createSprite(gLayout.screenW, kHeaderHeight);

    gPanelCanvas = new M5Canvas(&display);
    gPanelCanvas->createSprite(gLayout.screenW, gLayout.panelH);
}

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

    Rect brRow = brightnessRowRect();
    int brly = brRow.y - gLayout.panelY0;
    panel.fillRect(0, brly, brRow.w, brRow.h, TFT_DARKGREY);
    char brLine[32];
    snprintf(brLine, sizeof(brLine), "-  Brightness %d%%  +", (brightness * 100) / 255);
    drawLeftAligned(panel, brLine, 12, brly + brRow.h / 2, brRow.w - 24, 2, TFT_WHITE, TFT_DARKGREY);

    Rect volRow = volumeRowRect();
    int voly = volRow.y - gLayout.panelY0;
    panel.fillRect(0, voly, volRow.w, volRow.h, TFT_DARKGREY);
    char volLine[32];
    snprintf(volLine, sizeof(volLine), "-  Volume %d%%  +", (volume * 100) / 255);
    drawLeftAligned(panel, volLine, 12, voly + volRow.h / 2, volRow.w - 24, 2, TFT_WHITE, TFT_DARKGREY);

    panel.pushSprite(0, gLayout.panelY0);
}

int hitTestHud(int touchX, int touchY) {
    Rect brRow = brightnessRowRect();
    if (rectContains(leftHalf(brRow), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_DOWN;
    if (rectContains(rightHalf(brRow), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_UP;
    Rect volRow = volumeRowRect();
    if (rectContains(leftHalf(volRow), touchX, touchY)) return HUD_BUTTON_VOLUME_DOWN;
    if (rectContains(rightHalf(volRow), touchX, touchY)) return HUD_BUTTON_VOLUME_UP;
    return HUD_BUTTON_NONE;
}
```

- [ ] **Step 3: Rewrite `src/trial_view.h`**

```cpp
#pragma once
#include <M5Unified.h>
#include "trial_state.h"

// Allocates the offscreen canvas used for the raycast view. Call once from setup().
void initTrialView(M5GFX& display);

// Renders one frame of the Secret Realm trial (raycast walls/floor/ceiling, billboarded
// sprites for every undefeated enemy depth-tested against the wall raycast) into an internal
// offscreen canvas, then displays it scaled up to fill the raycast viewport - the top half of
// the screen below the header, see ui.h's raycastViewportBottom() - via pushRotateZoom.
// Player/enemy HP and route progress are drawn by ui.cpp's drawHud() instead, not here.
// Does not advance `state` - call tickTrial() separately in the game loop.
void renderTrialView(M5GFX& display, const TrialState& state);

// Simple procedural SFX (no imported audio assets), played by main.cpp at the relevant
// combat/clear transitions.
void playAttackSfx();
void playHitSfx();
void playVictorySfx();
```

- [ ] **Step 4: Rewrite `src/trial_view.cpp`**

```cpp
#include "trial_view.h"
#include "raycast.h"
#include "trial_textures.h"
#include "color.h"
#include "ui.h" // kHeaderHeight, raycastViewportBottom() - shared viewport bounds
#include <cmath>
#include <vector>

namespace {
// The raycaster computes at this resolution - deliberately close to the crystal's old
// hardware-proven 240x240 pixel-fill cost (240x320 = ~33% more pixels, same order of
// magnitude), then displayed scaled up via pushRotateZoom (kTrialZoom) to fill the raycast
// viewport without paying full-resolution compute cost.
constexpr int kTrialViewWidth = 240;
constexpr int kTrialViewHeight = 320;
// May need retuning on real hardware: this value was tuned for the old "between header and
// return-button strip" region (roughly the whole screen minus the header); the viewport is
// now deliberately half that height. See the design spec's Open Risk note - unverified without
// a physical Tab5.
constexpr float kTrialZoom = 2.5f;
constexpr float kFovRadians = 1.02f;  // ~60 degrees
constexpr float kMaxRayDistance = 20.0f;

M5Canvas* gTrialCanvas = nullptr;
std::vector<WallHit> gColumnHits;
std::vector<uint16_t> gPixelBuffer; // RGB565, row-major, for pushImage

RGB wallBaseColorFor(int wallType) {
    if (wallType == 1) return RGB{90, 90, 110};  // boundary stone
    return RGB{110, 70, 150};                     // inner spirit-veined stone
}

// Projects one enemy as a camera-facing billboard into gPixelBuffer, depth-testing each
// column against that column's already-computed wall distance (gColumnHits) so a nearer
// wall correctly hides the enemy - the standard Doom/Wolfenstein sprite-occlusion technique.
// Must run after the wall pass has filled gColumnHits/gPixelBuffer for this frame and before
// pushImage(). No-ops (returns without drawing) if the enemy is out of range or outside the
// field of view.
void drawEnemyBillboard(float camX, float camY, float facingRadians, float enemyX, float enemyY,
                         bool isCurrentEncounter) {
    float dx = enemyX - camX;
    float dy = enemyY - camY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.2f || dist > kMaxRayDistance) return;

    float relAngle = std::atan2(dy, dx) - facingRadians;
    while (relAngle > 3.14159265f) relAngle -= 6.28318531f;
    while (relAngle < -3.14159265f) relAngle += 6.28318531f;
    if (std::fabs(relAngle) > kFovRadians * 0.5f + 0.15f) return; // outside view (+ small margin)

    float correctedDist = dist * std::cos(relAngle);
    if (correctedDist < 0.1f) return;

    float t = 0.5f + relAngle / kFovRadians;
    int centerCol = static_cast<int>(t * (kTrialViewWidth - 1));

    int spriteHeight = wallSliceHeight(correctedDist, kTrialViewHeight);
    if (spriteHeight > kTrialViewHeight * 2) spriteHeight = kTrialViewHeight * 2;
    int spriteWidth = static_cast<int>(spriteHeight * 0.6f);
    int top = (kTrialViewHeight - spriteHeight) / 2;
    int bottom = top + spriteHeight;

    RGB color = isCurrentEncounter ? RGB{220, 60, 60} : RGB{160, 40, 90};
    int left = centerCol - spriteWidth / 2;
    int right = centerCol + spriteWidth / 2;

    for (int col = left; col <= right; ++col) {
        if (col < 0 || col >= kTrialViewWidth) continue;
        if (correctedDist >= gColumnHits[static_cast<size_t>(col)].distance) continue; // behind a wall
        for (int y = top; y < bottom; ++y) {
            if (y < 0 || y >= kTrialViewHeight) continue;
            gPixelBuffer[static_cast<size_t>(y) * kTrialViewWidth + col] =
                gTrialCanvas->color565(color.r, color.g, color.b);
        }
    }
}
} // namespace

void initTrialView(M5GFX& display) {
    gTrialCanvas = new M5Canvas(&display);
    gTrialCanvas->createSprite(kTrialViewWidth, kTrialViewHeight);
    gColumnHits.reserve(static_cast<size_t>(kTrialViewWidth));
    gPixelBuffer.assign(static_cast<size_t>(kTrialViewWidth) * kTrialViewHeight, 0);
}

void renderTrialView(M5GFX& display, const TrialState& state) {
    if (!gTrialCanvas) return;

    castColumns(state.map.grid, state.posX, state.posY, state.facingRadians, kFovRadians,
                kTrialViewWidth, kMaxRayDistance, gColumnHits);

    for (int col = 0; col < kTrialViewWidth; ++col) {
        const WallHit& hit = gColumnHits[static_cast<size_t>(col)];
        int sliceHeight = wallSliceHeight(hit.distance, kTrialViewHeight);
        if (sliceHeight > kTrialViewHeight) sliceHeight = kTrialViewHeight;
        int top = (kTrialViewHeight - sliceHeight) / 2;
        int bottom = top + sliceHeight;

        RGB base = wallBaseColorFor(hit.wallType);
        // Shade the darker of the two DDA hit orientations to fake directional lighting,
        // matching the crystal renderer's old cheap-but-effective shading philosophy.
        if (!hit.hitVertical) {
            base.r = static_cast<uint8_t>(base.r * 0.75f);
            base.g = static_cast<uint8_t>(base.g * 0.75f);
            base.b = static_cast<uint8_t>(base.b * 0.75f);
        }

        for (int y = 0; y < kTrialViewHeight; ++y) {
            RGB pixel;
            if (y < top) {
                pixel = RGB{20, 20, 30}; // ceiling
            } else if (y >= bottom) {
                pixel = RGB{35, 30, 25}; // floor
            } else {
                float v = static_cast<float>(y - top) / static_cast<float>(sliceHeight);
                pixel = sampleWallTexture(hit.wallType, hit.wallX, v, base);
            }
            gPixelBuffer[static_cast<size_t>(y) * kTrialViewWidth + col] =
                gTrialCanvas->color565(pixel.r, pixel.g, pixel.b);
        }
    }

    for (size_t i = 0; i < state.map.enemies.size(); ++i) {
        if (state.enemiesDefeated[i]) continue;
        bool isCurrent = (state.phase == TrialPhase::Fighting &&
                           state.currentEnemyIndex == static_cast<int>(i));
        drawEnemyBillboard(state.posX, state.posY, state.facingRadians,
                            state.map.enemies[i].x, state.map.enemies[i].y, isCurrent);
    }

    gTrialCanvas->pushImage(0, 0, kTrialViewWidth, kTrialViewHeight, gPixelBuffer.data());

    // Scaled push: displays the small internal buffer stretched to fill the raycast viewport,
    // centered horizontally and vertically within it. Default sprite pivot is its own center,
    // so (centerX, centerY) here is where that center lands on the physical display.
    float availableTop = kHeaderHeight;
    float availableBottom = raycastViewportBottom(display.height());
    float centerX = display.width() / 2.0f;
    float centerY = availableTop + (availableBottom - availableTop) / 2.0f;
    gTrialCanvas->pushRotateZoom(centerX, centerY, 0.0f, kTrialZoom, kTrialZoom);
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

- [ ] **Step 5: Rewrite `src/main.cpp`**

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

namespace {
constexpr uint32_t kTickIntervalMs = 50;   // 20Hz economy tick
constexpr uint32_t kAutosaveIntervalMs = 15000;
// The HUD (header + stats panel) is pushed as two offscreen-sprite blits covering most of a
// 720x1280 portrait screen, which costs much more per call than the raycast viewport's own
// blit. It's also just text/bars with no motion of its own, so it doesn't need to redraw at
// full render-loop rate — throttling it keeps the raycast view's own redraw (every loop
// iteration, see below) unaffected, while still forcing an immediate redraw right after any
// touch that actually changes state, so brightness/volume taps still feel responsive.
constexpr uint32_t kHudRedrawIntervalMs = 300; // ~3Hz when idle

// readRtcEpochSeconds() returns exactly 0 only for a genuinely never-seeded RTC chip
// (see rtc_store.h). If that happens, seed it once with a reasonable recent-ish
// timestamp so future elapsed-time deltas (offline earnings) work from this point
// forward. Absolute accuracy doesn't matter here, only that it's nonzero.
constexpr int64_t kRtcFallbackEpochSeconds = 1787844399;

GameState gState;
TrialState gTrialState;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;
uint32_t gLastTrialTickMs = 0;

uint8_t gBrightness = kMaxBrightness;
uint8_t gVolume = kMaxVolume / 2;

void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch, gBrightness, gVolume));
}
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Secret Realm starting");

    M5.Display.fillScreen(TFT_BLACK);

    SaveData save = nvsLoadSave();
    int64_t nowEpoch = readRtcEpochSeconds();

    if (nowEpoch == 0) {
        writeRtcFromEpochSeconds(kRtcFallbackEpochSeconds);
        nowEpoch = kRtcFallbackEpochSeconds;
        Serial.println("[RTC] Chip was never seeded (epoch==0); wrote fallback timestamp");
    }

    if (save.lastSaveEpochSeconds != 0) {
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState);
        double offlineQi = computeOfflineEarnings(nowEpoch, save.lastSaveEpochSeconds,
                                                    rateAtSave, 24 * 3600);
        save.qi += offlineQi;
        Serial.printf("[OFFLINE] Gained %.2f Qi while away\n", offlineQi);

        // Only for a meaningfully nonzero amount — a returning player who was away for only a
        // few seconds (or a fresh device with no prior rate) has nothing worth interrupting
        // boot to report.
        if (offlineQi >= 0.1) {
            char qiBuf[24];
            formatQi(offlineQi, qiBuf, sizeof(qiBuf));
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(20, 20);
            M5.Display.printf("While you cultivated in seclusion,\nyou gained %s Qi", qiBuf);
            delay(2000);
        }
    } else {
        Serial.println("[OFFLINE] First-ever boot, no offline bonus");
    }

    gState = toGameState(save);

    gBrightness = clampBrightness(save.brightness);
    gVolume = clampVolume(save.volume);
    M5.Display.setBrightness(gBrightness);
    M5.Speaker.setVolume(gVolume);

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

    Serial.println("[BOOT] Ready");
}

void loop() {
    M5.update();

    uint32_t now = millis();

    if (now - gLastTickMs >= kTickIntervalMs) {
        double dt = (now - gLastTickMs) / 1000.0;
        tick(gState, dt);
        gLastTickMs = now;

        // Automation: breakthrough first, then auto-buy — deliberately in that order.
        // Breakthrough thresholds are much larger than generator costs; if auto-buy ran
        // first and greedily spent Qi every tick, it could perpetually keep Qi below the
        // breakthrough threshold. Checking breakthrough first avoids that trap.
        if (canBreakthrough(gState)) {
            attemptBreakthrough(gState);
        }
        // One purchase attempt per generator per tick (not loop-until-can't-afford) —
        // natural accumulation across many ticks at 20Hz is plenty responsive, and index
        // order (0..NUM_GENERATORS-1) naturally prioritizes the cheapest/earliest-unlocked
        // generators first.
        for (int i = 0; i < NUM_GENERATORS; ++i) {
            purchaseGenerator(gState, i);
        }
        // Deliberately no saveNow() here: automated purchases/breakthroughs can fire many
        // times per second, and forcing an NVS write on every one would hammer flash write
        // endurance for no real benefit. The periodic autosave below (kAutosaveIntervalMs)
        // is the only thing that persists automated progress.
    }

    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        bool stateChanged = false;
        if (button == HUD_BUTTON_BRIGHTNESS_DOWN) {
            gBrightness = clampBrightness(static_cast<int>(gBrightness) - kSettingsStep);
            M5.Display.setBrightness(gBrightness);
            stateChanged = true;
        } else if (button == HUD_BUTTON_BRIGHTNESS_UP) {
            gBrightness = clampBrightness(static_cast<int>(gBrightness) + kSettingsStep);
            M5.Display.setBrightness(gBrightness);
            stateChanged = true;
        } else if (button == HUD_BUTTON_VOLUME_DOWN) {
            gVolume = clampVolume(static_cast<int>(gVolume) - kSettingsStep);
            M5.Speaker.setVolume(gVolume);
            stateChanged = true;
        } else if (button == HUD_BUTTON_VOLUME_UP) {
            gVolume = clampVolume(static_cast<int>(gVolume) + kSettingsStep);
            M5.Speaker.setVolume(gVolume);
            stateChanged = true;
        }
        if (stateChanged) {
            saveNow();
            gLastHudDrawMs = 0; // force an immediate (unthrottled) HUD redraw this frame
        }
    }

    if (now - gLastAutosaveMs >= kAutosaveIntervalMs) {
        saveNow();
        gLastAutosaveMs = now;
    }

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
        renderTrialView(M5.Display, gTrialState); // show the "Cleared!" frame before pausing
        delay(1500);
        restartTrial(gTrialState, gState.realmIndex); // resets qiRewardPending to 0.0 and loops back
    } else {
        renderTrialView(M5.Display, gTrialState);
    }

    if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
        drawHud(M5.Display, gState, gTrialState, gBrightness, gVolume);
        gLastHudDrawMs = now;
    }
}
```

- [ ] **Step 6: Build the ESP32 firmware target**

Run: `pio run -e esp32p4_pioarduino`
Expected: `SUCCESS`. If it fails, the error will point at one of the five files above — common mistakes to check first: a stale `#include "framebuffer.h"`/`"mesh.h"`/`"rasterizer.h"` left somewhere, a call site still using the old 4-argument-less `tickTrial`/1-argument `restartTrial`, or a leftover reference to a deleted `HudButton` value (`HUD_BUTTON_BREAKTHROUGH` etc.) or deleted `ui.h` constant (`kRenderSize`, `kReturnButtonHeight`, `kSecretRealmUnlockRealmIndex`).

- [ ] **Step 7: Re-run the full native suite as a final sanity check**

Run: `pio test -e native`
Expected: unchanged from Task 2's Step 5 — this task touched nothing `lib/core` depends on, so this should already be green, but confirming costs nothing.

- [ ] **Step 8: Commit**

```bash
git add src/ui.h src/ui.cpp src/trial_view.h src/trial_view.cpp src/main.cpp
git commit -m "Make the raycasting trial the app's only screen

Deletes the idle-game screen's ViewMode switch, generator/breakthrough
shop panel, and Enter/Return buttons entirely. The trial now starts
immediately on boot with no realm-based unlock gate. New layout:
status header, raycast viewport filling the top half of the remaining
screen, and a read-only stats/settings panel (breakthrough progress,
player/enemy HP, route progress, brightness/volume) in the bottom
half. The cultivation economy keeps ticking and saving exactly as
before, just without a shop UI to display it."
```

---

## Task 4: Update the README

**Files:**
- Modify: `README.md`

**Interfaces:** None — documentation only.

- [ ] **Step 1: Rewrite the "Xianxia Idle Game" and "Secret Realm" sections**

Read the current `README.md` first (it will have shifted slightly from what's quoted in this plan if anything upstream changed it), then replace its `## Xianxia Idle Game` section and `### Secret Realm (raycasting trial mode)` subsection with content covering:
- The crystal rasterizer and the idle shop screen are gone; the raycasting trial is now the app's entire UI, autoplaying from first boot with no unlock gate.
- The cultivation economy (Qi/generators/realm breakthroughs) still runs exactly as before, invisibly — it drives the trial's combat difficulty and Qi rewards, and is still visible as read-only stats (not a shop) in the new bottom panel.
- The new screen layout: header, raycast viewport (top half of the remaining screen), stats/settings panel (bottom half: breakthrough progress, player/enemy HP, route progress, brightness/volume — the only remaining touch controls).
- The two fixes made in this revamp: smooth turn-rate-limited camera facing (previously snapped instantly in one tick) and the restart path now pulling the *live* realm index instead of freezing it at the trial's first-ever start.
- Point at this plan's spec (`docs/superpowers/specs/2026-08-27-raycasting-only-revamp-design.md`) and this plan (`docs/superpowers/plans/2026-08-27-raycasting-only-revamp.md`) the same way the existing README already links its other specs/plans.
- Carry forward the still-true caveat that `kTrialZoom`/on-device FPS have not been validated on real hardware, and flag that this revamp makes it *more* likely `kTrialZoom` needs retuning (the viewport is now deliberately half the screen height it was tuned against).

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Document the raycasting-only revamp"
```
