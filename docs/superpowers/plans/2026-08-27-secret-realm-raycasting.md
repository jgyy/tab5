# Secret Realm Raycasting Trial Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an autoplaying, raycasted "Secret Realm" trial mode to the xianxia idle game — a small fixed maze the cultivator auto-navigates and auto-fights through, reachable from the idle view, with a Qi reward on each clear.

**Architecture:** New hardware-agnostic `lib/core/` modules (`raycast`, `trial_map`, `trial_combat`, `trial_state`) unit-tested on `native`, exactly mirroring how `math3d`/`mesh`/`rasterizer`/`economy` already work. Thin Arduino glue in a new `src/trial_view.*` renders the raycast output and wires SFX, gated behind a `ViewMode` switch added to `main.cpp`'s existing `loop()`. The existing crystal renderer and economy tick are untouched.

**Tech Stack:** C++17, PlatformIO (`native` env for unit tests via Unity, `esp32p4_pioarduino` env for the real device), M5Unified/M5GFX (pinned versions already in `platformio.ini`).

**Spec:** `docs/superpowers/specs/2026-08-27-secret-realm-raycasting-design.md`

## Global Constraints

- No RNG anywhere in `lib/core/` — combat, navigation, and textures must be deterministic (same inputs → same outputs), matching this codebase's existing convention (`hashJaggedness` in `mesh.cpp`).
- No external asset files (images, audio) — textures and SFX are procedurally generated in code, matching the project's zero-external-asset convention.
- `lib/core/` code must compile and be tested in the `native` PlatformIO environment with **no** Arduino/M5Unified/M5GFX includes. Only `src/*.cpp` may include those.
- Every new `lib/core/` module gets a `test/test_<module>/test_<module>.cpp` suite, following the existing Unity-based test files' structure (see `test/test_economy/test_economy.cpp` for the pattern: `#include <unity.h>`, one `test_*` function per case, `setUp`/`tearDown` no-ops, all cases registered in `main()`/`process()`).
- Player combat stats: `playerMaxHP = 100 + 40 * realmIndex`, `playerAttackDamage = 10 + 6 * realmIndex` (from the spec's Combat & Progression Tie-in section).
- On-device FPS/resolution tuning (the spec's "hardware spike") **cannot be performed in this environment** — no physical Tab5 is connected. Task 9 documents this explicitly as an open item for the user to validate on real hardware rather than claiming an unverified number.

---

### Task 1: Raycast core — single-ray DDA intersection

**Files:**
- Create: `lib/core/raycast.h`
- Create: `lib/core/raycast.cpp`
- Test: `test/test_raycast/test_raycast.cpp`

**Interfaces:**
- Produces: `struct RaycastMap { int width; int height; std::vector<int> cells; int at(int x, int y) const; };` (out-of-bounds `at()` returns `1`, treated as solid, so rays never escape the grid). `struct RayHit { float distance; float wallX; int wallType; bool hitVertical; };` `RayHit castRay(const RaycastMap& map, float originX, float originY, float dirX, float dirY, float maxDistance);`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_raycast/test_raycast.cpp
#include <unity.h>
#include "raycast.h"

void setUp(void) {}
void tearDown(void) {}

RaycastMap makeTestMap() {
    // 5x5, walls (type 1) around the border, open floor inside.
    RaycastMap m;
    m.width = 5;
    m.height = 5;
    m.cells = {
        1,1,1,1,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,0,0,0,1,
        1,1,1,1,1,
    };
    return m;
}

void test_ray_hits_wall_straight_ahead(void) {
    RaycastMap m = makeTestMap();
    // Origin at cell center (2.5, 2.5), firing straight in +X: hits the wall at x=4
    // (the wall cell spans x in [4,5)), so distance should be 4.0 - 2.5 = 1.5.
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, hit.distance);
    TEST_ASSERT_EQUAL_INT(1, hit.wallType);
    TEST_ASSERT_TRUE(hit.hitVertical);
}

void test_ray_hits_wall_straight_down(void) {
    RaycastMap m = makeTestMap();
    RayHit hit = castRay(m, 2.5f, 2.5f, 0.0f, 1.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, hit.distance);
    TEST_ASSERT_FALSE(hit.hitVertical);
}

void test_ray_wallx_is_fractional_hit_position(void) {
    RaycastMap m = makeTestMap();
    // Same straight-ahead ray as above hits the wall face at y=2.5 exactly -> wallX = 0.5
    // (the fractional position along the hit cell's edge).
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, hit.wallX);
}

void test_ray_beyond_max_distance_returns_no_hit(void) {
    RaycastMap m = makeTestMap();
    RayHit hit = castRay(m, 2.5f, 2.5f, 1.0f, 0.0f, 1.0f); // wall is 1.5 away, cap at 1.0
    TEST_ASSERT_EQUAL_INT(0, hit.wallType);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hit.distance);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ray_hits_wall_straight_ahead);
    RUN_TEST(test_ray_hits_wall_straight_down);
    RUN_TEST(test_ray_wallx_is_fractional_hit_position);
    RUN_TEST(test_ray_beyond_max_distance_returns_no_hit);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_raycast`
Expected: FAIL to compile — `raycast.h` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// lib/core/raycast.h
#pragma once
#include <vector>

struct RaycastMap {
    int width = 0;
    int height = 0;
    std::vector<int> cells; // row-major: cells[y * width + x]. 0 = open floor, >0 = wall type id.

    int at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 1; // out of bounds reads as solid
        return cells[static_cast<size_t>(y) * width + x];
    }
};

struct RayHit {
    float distance = 0.0f;  // raw Euclidean distance from the ray origin to the hit point
    float wallX = 0.0f;     // fractional position along the hit wall face, in [0,1)
    int wallType = 0;       // cell value at the hit; 0 means no hit within maxDistance
    bool hitVertical = false; // true if a vertical grid line (a north/south-facing wall) was hit
};

// Casts one ray from (originX, originY) in grid space, direction (dirX, dirY) (need not be
// normalized), through `map` via DDA grid stepping. Returns the nearest wall hit, or a RayHit
// with wallType == 0 and distance == maxDistance if nothing is hit within maxDistance.
RayHit castRay(const RaycastMap& map, float originX, float originY, float dirX, float dirY,
               float maxDistance);
```

```cpp
// lib/core/raycast.cpp
#include "raycast.h"
#include <cmath>

RayHit castRay(const RaycastMap& map, float originX, float originY, float dirX, float dirY,
               float maxDistance) {
    float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 1e-6f) return RayHit{maxDistance, 0.0f, 0, false};
    dirX /= len;
    dirY /= len;

    int mapX = static_cast<int>(std::floor(originX));
    int mapY = static_cast<int>(std::floor(originY));

    float deltaDistX = (dirX == 0.0f) ? 1e30f : std::fabs(1.0f / dirX);
    float deltaDistY = (dirY == 0.0f) ? 1e30f : std::fabs(1.0f / dirY);

    int stepX = (dirX < 0.0f) ? -1 : 1;
    int stepY = (dirY < 0.0f) ? -1 : 1;

    float sideDistX = (dirX < 0.0f) ? (originX - mapX) * deltaDistX
                                     : (mapX + 1.0f - originX) * deltaDistX;
    float sideDistY = (dirY < 0.0f) ? (originY - mapY) * deltaDistY
                                     : (mapY + 1.0f - originY) * deltaDistY;

    bool hitVertical = false;
    float traveled = 0.0f;

    while (traveled < maxDistance) {
        if (sideDistX < sideDistY) {
            traveled = sideDistX;
            sideDistX += deltaDistX;
            mapX += stepX;
            hitVertical = true;
        } else {
            traveled = sideDistY;
            sideDistY += deltaDistY;
            mapY += stepY;
            hitVertical = false;
        }

        if (traveled >= maxDistance) break;

        int cellValue = map.at(mapX, mapY);
        if (cellValue > 0) {
            RayHit hit;
            hit.distance = traveled;
            hit.wallType = cellValue;
            hit.hitVertical = hitVertical;
            if (hitVertical) {
                float hitY = originY + traveled * dirY;
                hit.wallX = hitY - std::floor(hitY);
            } else {
                float hitX = originX + traveled * dirX;
                hit.wallX = hitX - std::floor(hitX);
            }
            return hit;
        }
    }

    return RayHit{maxDistance, 0.0f, 0, hitVertical};
}
```

- [ ] **Step 4: Register the new native test suite**

PlatformIO's `native` env auto-discovers `test/test_*/` directories — no registration file to edit. Confirm by listing: `ls test/test_raycast/`.

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_raycast`
Expected: PASS, all 4 cases.

- [ ] **Step 6: Commit**

```bash
git add lib/core/raycast.h lib/core/raycast.cpp test/test_raycast/test_raycast.cpp
git commit -m "Add DDA raycasting core (single-ray grid intersection)"
```

---

### Task 2: Raycast core — full-screen column casting with fisheye correction

**Files:**
- Modify: `lib/core/raycast.h`
- Modify: `lib/core/raycast.cpp`
- Test: `test/test_raycast/test_raycast.cpp`

**Interfaces:**
- Consumes: `RaycastMap`, `RayHit`, `castRay(...)` from Task 1.
- Produces: `struct WallHit { float distance; float wallX; int wallType; bool hitVertical; };` `void castColumns(const RaycastMap& map, float camX, float camY, float facingRadians, float fovRadians, int screenWidth, float maxDistance, std::vector<WallHit>& outHits);` `int wallSliceHeight(float correctedDistance, int viewportHeight);`

- [ ] **Step 1: Write the failing test**

```cpp
// Append to test/test_raycast/test_raycast.cpp, before main()

void test_cast_columns_fills_one_hit_per_column(void) {
    RaycastMap m = makeTestMap();
    std::vector<WallHit> hits;
    castColumns(m, 2.5f, 2.5f, 0.0f, 1.0f, 7, 20.0f, hits);
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(hits.size()));
    for (const auto& h : hits) {
        TEST_ASSERT_TRUE(h.wallType > 0);
    }
}

void test_cast_columns_center_ray_matches_facing_direction(void) {
    RaycastMap m = makeTestMap();
    std::vector<WallHit> hits;
    // facing straight in +X (radians 0), odd screenWidth so the middle column is the exact
    // camera-forward ray with no fisheye correction needed (correction factor cos(0) == 1).
    castColumns(m, 2.5f, 2.5f, 0.0f, 1.0f, 5, 20.0f, hits);
    WallHit center = hits[2];
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, center.distance);
}

void test_wall_slice_height_shrinks_with_distance(void) {
    int near = wallSliceHeight(1.0f, 240);
    int far = wallSliceHeight(4.0f, 240);
    TEST_ASSERT_TRUE(near > far);
    TEST_ASSERT_EQUAL_INT(240, near);  // distance 1.0 -> full viewport height
    TEST_ASSERT_EQUAL_INT(60, far);    // distance 4.0 -> quarter height
}
```

Add these three `RUN_TEST(...)` calls to `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_raycast`
Expected: FAIL to compile — `WallHit`/`castColumns`/`wallSliceHeight` don't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// Add to lib/core/raycast.h, after castRay's declaration

struct WallHit {
    float distance = 0.0f;  // fisheye-corrected perpendicular distance; use directly as depth
    float wallX = 0.0f;
    int wallType = 0;
    bool hitVertical = false;
};

// Casts `screenWidth` rays fanned evenly across `fovRadians`, centered on `facingRadians`, from
// camera position (camX, camY). Fills `outHits` (resized to screenWidth) with one WallHit per
// column, left to right, with `distance` corrected for fisheye distortion (multiplied by
// cos(rayAngle - facingRadians)) so it can be used directly as a per-column depth value.
void castColumns(const RaycastMap& map, float camX, float camY, float facingRadians,
                  float fovRadians, int screenWidth, float maxDistance,
                  std::vector<WallHit>& outHits);

// Projected on-screen pixel height of a wall slice at the given fisheye-corrected perpendicular
// distance, for a viewport of `viewportHeight` pixels. Distance is clamped to a small minimum
// to avoid division blowup.
int wallSliceHeight(float correctedDistance, int viewportHeight);
```

```cpp
// Add to lib/core/raycast.cpp

void castColumns(const RaycastMap& map, float camX, float camY, float facingRadians,
                  float fovRadians, int screenWidth, float maxDistance,
                  std::vector<WallHit>& outHits) {
    outHits.assign(static_cast<size_t>(screenWidth), WallHit{});
    for (int col = 0; col < screenWidth; ++col) {
        float t = (screenWidth == 1) ? 0.5f : static_cast<float>(col) / (screenWidth - 1);
        float rayAngle = facingRadians + (t - 0.5f) * fovRadians;
        RayHit raw = castRay(map, camX, camY, std::cos(rayAngle), std::sin(rayAngle), maxDistance);

        WallHit wh;
        wh.wallType = raw.wallType;
        wh.wallX = raw.wallX;
        wh.hitVertical = raw.hitVertical;
        float angleDiff = rayAngle - facingRadians;
        wh.distance = raw.distance * std::cos(angleDiff);
        outHits[static_cast<size_t>(col)] = wh;
    }
}

int wallSliceHeight(float correctedDistance, int viewportHeight) {
    float d = correctedDistance;
    if (d < 0.0001f) d = 0.0001f;
    return static_cast<int>(static_cast<float>(viewportHeight) / d);
}
```

Note: `#include <cmath>` is already present in `raycast.cpp` from Task 1.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_raycast`
Expected: PASS, all 7 cases.

- [ ] **Step 5: Commit**

```bash
git add lib/core/raycast.h lib/core/raycast.cpp test/test_raycast/test_raycast.cpp
git commit -m "Add full-screen column raycasting with fisheye correction"
```

---

### Task 3: Fixed Secret Realm map, route, and enemy spawns

**Files:**
- Create: `lib/core/trial_map.h`
- Create: `lib/core/trial_map.cpp`
- Test: `test/test_trial_map/test_trial_map.cpp`

**Interfaces:**
- Consumes: `RaycastMap` from Task 1 (`lib/core/raycast.h`).
- Produces: `struct Waypoint { float x, y; };` `struct EnemySpawn { float x, y; int maxHp; int damage; };` `struct TrialMap { RaycastMap grid; std::vector<Waypoint> route; std::vector<EnemySpawn> enemies; };` `TrialMap makeSecretRealmMap();`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_trial_map/test_trial_map.cpp
#include <unity.h>
#include "trial_map.h"

void setUp(void) {}
void tearDown(void) {}

void test_map_grid_is_10_wide_8_tall(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(10, m.grid.width);
    TEST_ASSERT_EQUAL_INT(8, m.grid.height);
}

void test_map_border_is_solid(void) {
    TrialMap m = makeSecretRealmMap();
    for (int x = 0; x < m.grid.width; ++x) {
        TEST_ASSERT_TRUE(m.grid.at(x, 0) > 0);
        TEST_ASSERT_TRUE(m.grid.at(x, 7) > 0);
    }
    for (int y = 0; y < m.grid.height; ++y) {
        TEST_ASSERT_TRUE(m.grid.at(0, y) > 0);
        TEST_ASSERT_TRUE(m.grid.at(9, y) > 0);
    }
}

void test_map_has_three_enemies_and_seven_waypoints(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(m.enemies.size()));
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(m.route.size()));
}

void test_route_waypoints_sit_on_open_floor(void) {
    TrialMap m = makeSecretRealmMap();
    for (const auto& wp : m.route) {
        int cellX = static_cast<int>(wp.x);
        int cellY = static_cast<int>(wp.y);
        TEST_ASSERT_EQUAL_INT(0, m.grid.at(cellX, cellY));
    }
}

void test_enemy_spawns_sit_on_open_floor(void) {
    TrialMap m = makeSecretRealmMap();
    for (const auto& e : m.enemies) {
        int cellX = static_cast<int>(e.x);
        int cellY = static_cast<int>(e.y);
        TEST_ASSERT_EQUAL_INT(0, m.grid.at(cellX, cellY));
    }
}

void test_enemies_get_progressively_stronger(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_TRUE(m.enemies[0].maxHp < m.enemies[1].maxHp);
    TEST_ASSERT_TRUE(m.enemies[1].maxHp < m.enemies[2].maxHp);
}

void test_make_secret_realm_map_is_deterministic(void) {
    TrialMap a = makeSecretRealmMap();
    TrialMap b = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(a.grid.width, b.grid.width);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.route.size()), static_cast<int>(b.route.size()));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.route[0].x, b.route[0].x);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_map_grid_is_10_wide_8_tall);
    RUN_TEST(test_map_border_is_solid);
    RUN_TEST(test_map_has_three_enemies_and_seven_waypoints);
    RUN_TEST(test_route_waypoints_sit_on_open_floor);
    RUN_TEST(test_enemy_spawns_sit_on_open_floor);
    RUN_TEST(test_enemies_get_progressively_stronger);
    RUN_TEST(test_make_secret_realm_map_is_deterministic);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_trial_map`
Expected: FAIL to compile — `trial_map.h` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// lib/core/trial_map.h
#pragma once
#include <vector>
#include "raycast.h"

struct Waypoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct EnemySpawn {
    float x = 0.0f;
    float y = 0.0f;
    int maxHp = 0;
    int damage = 0; // damage dealt to the player per attack landed
};

struct TrialMap {
    RaycastMap grid;
    std::vector<Waypoint> route;      // ordered start-to-goal waypoints, in grid-space coordinates
    std::vector<EnemySpawn> enemies;  // encountered along the route in array order
};

// The fixed Phase-1 Secret Realm: a 10x8 ring-corridor maze, a scripted waypoint route walking
// the corridor from the top-left entrance clockwise to the bottom-left goal, and three enemy
// spawns of increasing difficulty placed along that route. Deterministic - identical every call.
TrialMap makeSecretRealmMap();
```

```cpp
// lib/core/trial_map.cpp
#include "trial_map.h"

namespace {
// 10 wide x 8 tall. '#' = outer boundary wall (type 1), 'X' = inner block wall (type 2),
// '.' = open floor (0). Matches the design spec's illustrative sketch, simplified to a single
// ring corridor around a solid inner block for easy hand-verification.
const char* kRows[8] = {
    "##########",
    "#........#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#........#",
    "##########",
};

RaycastMap buildGrid() {
    RaycastMap grid;
    grid.width = 10;
    grid.height = 8;
    grid.cells.resize(80);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            char c = kRows[y][x];
            int value = 0;
            if (c == '#') value = 1;
            else if (c == 'X') value = 2;
            grid.cells[static_cast<size_t>(y) * 10 + x] = value;
        }
    }
    return grid;
}
} // namespace

TrialMap makeSecretRealmMap() {
    TrialMap m;
    m.grid = buildGrid();

    // Clockwise loop: entrance -> across the top -> down the right column -> across the
    // bottom to the goal. Cell-center coordinates (e.g. x=1.5 is the center of column 1).
    m.route = {
        {1.5f, 1.5f}, // start / entrance
        {4.5f, 1.5f}, // enemy 1
        {8.5f, 1.5f}, // top-right corner
        {8.5f, 3.5f}, // enemy 2
        {8.5f, 6.5f}, // bottom-right corner
        {4.5f, 6.5f}, // enemy 3
        {1.5f, 6.5f}, // goal
    };

    m.enemies = {
        {4.5f, 1.5f, 30, 8},
        {8.5f, 3.5f, 50, 14},
        {4.5f, 6.5f, 80, 22},
    };

    return m;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_trial_map`
Expected: PASS, all 7 cases.

- [ ] **Step 5: Commit**

```bash
git add lib/core/trial_map.h lib/core/trial_map.cpp test/test_trial_map/test_trial_map.cpp
git commit -m "Add the fixed Secret Realm maze, route, and enemy spawns"
```

---

### Task 4: Deterministic combat resolution

**Files:**
- Create: `lib/core/trial_combat.h`
- Create: `lib/core/trial_combat.cpp`
- Test: `test/test_trial_combat/test_trial_combat.cpp`

**Interfaces:**
- Produces: `struct CombatantState { int hp; int maxHp; int attackDamage; float attackCooldownSeconds; float attackTimer; };` `CombatantState makePlayerCombatant(int realmIndex);` `CombatantState makeEnemyCombatant(int maxHp, int damage);` `bool tickCombat(CombatantState& a, CombatantState& b, double dtSeconds);` `bool isDefeated(const CombatantState& c);`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_trial_combat/test_trial_combat.cpp
#include <unity.h>
#include "trial_combat.h"

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

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_trial_combat`
Expected: FAIL to compile — `trial_combat.h` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// lib/core/trial_combat.h
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

// Player combat stats derived from cultivation progress (Global Constraints in the plan doc).
CombatantState makePlayerCombatant(int realmIndex);

// Enemy combat stats from a TrialMap::EnemySpawn's maxHp/damage.
CombatantState makeEnemyCombatant(int maxHp, int damage);

// Advances both combatants' attack timers by dtSeconds; whichever timer(s) reach their
// cooldown deal their attackDamage to the other (hp clamped at 0) and reset to 0. Both can
// land in the same call if both cooldowns elapse within dtSeconds. Returns true if at least
// one attack landed this call.
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds);

bool isDefeated(const CombatantState& c);
```

```cpp
// lib/core/trial_combat.cpp
#include "trial_combat.h"

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

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_trial_combat`
Expected: PASS, all 6 cases.

- [ ] **Step 5: Commit**

```bash
git add lib/core/trial_combat.h lib/core/trial_combat.cpp test/test_trial_combat/test_trial_combat.cpp
git commit -m "Add deterministic tick-based combat resolution"
```

---

### Task 5: Trial orchestration — autoplay navigation, combat engagement, clear/retry loop

**Files:**
- Create: `lib/core/trial_state.h`
- Create: `lib/core/trial_state.cpp`
- Test: `test/test_trial_state/test_trial_state.cpp`

**Interfaces:**
- Consumes: `TrialMap`/`Waypoint`/`EnemySpawn` from Task 3 (`trial_map.h`); `CombatantState`/`makePlayerCombatant`/`makeEnemyCombatant`/`tickCombat`/`isDefeated` from Task 4 (`trial_combat.h`).
- Produces: `enum class TrialPhase { Traveling, Fighting, Cleared };` `struct TrialState { TrialMap map; int realmIndexAtStart; int currentWaypointIndex; float posX, posY; float facingRadians; TrialPhase phase; CombatantState player; int currentEnemyIndex; CombatantState enemy; std::vector<bool> enemiesDefeated; double qiRewardPending; };` `TrialState startTrial(const TrialMap& map, int realmIndex);` `void tickTrial(TrialState& state, double dtSeconds, double proposedReward);` `void restartTrial(TrialState& state);`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_trial_state/test_trial_state.cpp
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
    tickTrial(s, 0.1, 10.0);
    TEST_ASSERT_TRUE(s.posX > startX); // route[1] is to the right of route[0]
}

void test_reaching_enemy_enters_fighting(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    // Drive many ticks toward the first enemy at (4.5, 1.5); travel speed and encounter
    // radius are internal, so tick generously and assert the phase transition happened.
    for (int i = 0; i < 200 && s.phase == TrialPhase::Traveling; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentEnemyIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_enemy_resumes_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    for (int i = 0; i < 500 && s.phase == TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_TRUE(s.enemiesDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // about to die on the first enemy hit
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].x, s.posX);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
}

void test_clearing_all_enemies_and_reaching_goal_sets_reward(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // strong enough to one-shot-ish every enemy
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_trial_resets_state(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6);
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0);
    }
    restartTrial(s);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.enemiesDefeated[0]);
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
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_trial_state`
Expected: FAIL to compile — `trial_state.h` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// lib/core/trial_state.h
#pragma once
#include <vector>
#include "trial_map.h"
#include "trial_combat.h"

enum class TrialPhase { Traveling, Fighting, Cleared };

constexpr float kTravelSpeed = 1.5f;      // grid units per second
constexpr float kEncounterRadius = 0.3f;  // distance at which a live enemy engages the player

struct TrialState {
    TrialMap map;
    int realmIndexAtStart = 0;
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

// Fresh trial at the route's start; player combat stats derive from realmIndex.
TrialState startTrial(const TrialMap& map, int realmIndex);

// Advances the trial by dtSeconds. While Traveling: checks for a live, undefeated enemy within
// kEncounterRadius (entering Fighting if found), otherwise moves toward the current waypoint,
// advancing to the next waypoint on arrival, or to Cleared (setting qiRewardPending =
// proposedReward) if the final waypoint is reached with no enemies left undefeated. While
// Fighting: resolves one combat tick; on enemy defeat, marks it defeated and returns to
// Traveling; on player defeat, calls restartTrial. No-op once Cleared (call restartTrial to
// loop again).
void tickTrial(TrialState& state, double dtSeconds, double proposedReward);

// Resets to the route's start with full player HP, no enemies defeated, and
// qiRewardPending == 0.0, keeping `map` and `realmIndexAtStart`.
void restartTrial(TrialState& state);
```

```cpp
// lib/core/trial_state.cpp
#include "trial_state.h"
#include <cmath>

TrialState startTrial(const TrialMap& map, int realmIndex) {
    TrialState s;
    s.map = map;
    s.realmIndexAtStart = realmIndex;
    s.posX = map.route[0].x;
    s.posY = map.route[0].y;
    s.currentWaypointIndex = 0;
    s.phase = TrialPhase::Traveling;
    s.player = makePlayerCombatant(realmIndex);
    s.currentEnemyIndex = -1;
    s.enemiesDefeated.assign(map.enemies.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartTrial(TrialState& state) {
    TrialMap map = state.map; // preserve across reassignment below
    int realmIndex = state.realmIndexAtStart;
    state = startTrial(map, realmIndex);
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

void tickTrial(TrialState& state, double dtSeconds, double proposedReward) {
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

        state.facingRadians = std::atan2(dy, dx);
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
        restartTrial(state);
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_trial_state`
Expected: PASS, all 7 cases.

- [ ] **Step 5: Commit**

```bash
git add lib/core/trial_state.h lib/core/trial_state.cpp test/test_trial_state/test_trial_state.cpp
git commit -m "Add trial orchestration: autoplay navigation, combat engagement, clear/retry"
```

---

### Task 6: Procedural wall textures

**Files:**
- Create: `lib/core/trial_textures.h`
- Create: `lib/core/trial_textures.cpp`
- Test: `test/test_trial_textures/test_trial_textures.cpp`

**Interfaces:**
- Consumes: `RGB` from `lib/core/color.h`.
- Produces: `constexpr int kWallTextureSize = 32;` `RGB sampleWallTexture(int wallType, float u, float v, RGB baseColor);`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_trial_textures/test_trial_textures.cpp
#include <unity.h>
#include "trial_textures.h"

void setUp(void) {}
void tearDown(void) {}

void test_sample_is_deterministic(void) {
    RGB base{120, 60, 200};
    RGB a = sampleWallTexture(1, 0.3f, 0.7f, base);
    RGB b = sampleWallTexture(1, 0.3f, 0.7f, base);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_different_wall_types_look_different(void) {
    RGB base{120, 60, 200};
    RGB type1 = sampleWallTexture(1, 0.5f, 0.5f, base);
    RGB type2 = sampleWallTexture(2, 0.5f, 0.5f, base);
    TEST_ASSERT_TRUE(type1.r != type2.r || type1.g != type2.g || type1.b != type2.b);
}

void test_sample_stays_within_uv_bounds_at_edges(void) {
    RGB base{120, 60, 200};
    // Should not crash or produce garbage at u/v exactly 0.0 or just under 1.0.
    RGB corner = sampleWallTexture(1, 0.0f, 0.0f, base);
    RGB farCorner = sampleWallTexture(1, 0.999f, 0.999f, base);
    (void)corner;
    (void)farCorner;
    TEST_ASSERT_TRUE(true); // reaching here without a crash is the assertion
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sample_is_deterministic);
    RUN_TEST(test_different_wall_types_look_different);
    RUN_TEST(test_sample_stays_within_uv_bounds_at_edges);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_trial_textures`
Expected: FAIL to compile — `trial_textures.h` doesn't exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// lib/core/trial_textures.h
#pragma once
#include "color.h"

constexpr int kWallTextureSize = 32; // logical texel grid per wall face, sampled procedurally

// Procedurally shades a wall texel at fractional (u, v) in [0,1) for the given wall type,
// tinting `baseColor` with a deterministic brick/vein-style pattern (a grid of darker mortar
// lines for wallType 1, a marbled hash-based vein pattern for wallType 2 and above). Same
// (wallType, u, v, baseColor) always produces the same result - no RNG.
RGB sampleWallTexture(int wallType, float u, float v, RGB baseColor);
```

```cpp
// lib/core/trial_textures.cpp
#include "trial_textures.h"
#include <cmath>
#include <cstdint>

namespace {
uint8_t scaleChannel(uint8_t c, float factor) {
    float v = static_cast<float>(c) * factor;
    if (v > 255.0f) v = 255.0f;
    if (v < 0.0f) v = 0.0f;
    return static_cast<uint8_t>(v);
}

// Deterministic pseudo-random value in [0,1) from integer texel coordinates - same technique
// as mesh.cpp's hashJaggedness, no RNG state.
float hashTexel(int tx, int ty, int salt) {
    uint32_t h = static_cast<uint32_t>(tx) * 374761393u + static_cast<uint32_t>(ty) * 668265263u +
                 static_cast<uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}
} // namespace

RGB sampleWallTexture(int wallType, float u, float v, RGB baseColor) {
    int tx = static_cast<int>(u * kWallTextureSize) % kWallTextureSize;
    int ty = static_cast<int>(v * kWallTextureSize) % kWallTextureSize;
    if (tx < 0) tx += kWallTextureSize;
    if (ty < 0) ty += kWallTextureSize;

    float factor = 1.0f;
    if (wallType == 1) {
        // Brick-like grid: darker "mortar" lines every 8 texels.
        bool mortarLine = (tx % 8 == 0) || (ty % 8 == 0);
        factor = mortarLine ? 0.6f : 0.95f + 0.1f * hashTexel(tx, ty, wallType);
    } else {
        // Marbled vein pattern for inner/other wall types.
        float n = hashTexel(tx / 3, ty / 3, wallType);
        factor = 0.7f + 0.4f * n;
    }

    return RGB{
        scaleChannel(baseColor.r, factor),
        scaleChannel(baseColor.g, factor),
        scaleChannel(baseColor.b, factor),
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_trial_textures`
Expected: PASS, all 3 cases.

- [ ] **Step 5: Commit**

```bash
git add lib/core/trial_textures.h lib/core/trial_textures.cpp test/test_trial_textures/test_trial_textures.cpp
git commit -m "Add procedural wall texture sampling"
```

---

### Task 7: Device rendering glue — `src/trial_view`

**Files:**
- Create: `src/trial_view.h`
- Create: `src/trial_view.cpp`

**Interfaces:**
- Consumes: `TrialState`/`TrialPhase`/`EnemySpawn`/`tickTrial`/`startTrial`/`restartTrial` (`lib/core/trial_state.h`/`trial_map.h`), `castColumns`/`WallHit`/`wallSliceHeight` (`lib/core/raycast.h`), `sampleWallTexture` (`lib/core/trial_textures.h`), `RGB` (`lib/core/color.h`), `Framebuffer` (`lib/core/framebuffer.h`).
- Produces: `void initTrialView(M5GFX& display);` `void renderTrialView(M5GFX& display, TrialState& state);` (draws one frame: raycast walls/floor/ceiling, billboarded sprites for every undefeated enemy (depth-tested per-column against the wall raycast so nearer walls correctly hide them), and the HUD strip, all into an offscreen `M5Canvas`, then pushes it scaled up).

This task has no `native` unit test — it's Arduino/M5GFX glue, validated on-device only, matching this codebase's existing convention (`ui.cpp`/`main.cpp` have no native tests either).

- [ ] **Step 1: Write the implementation**

```cpp
// src/trial_view.h
#pragma once
#include <M5Unified.h>
#include "trial_state.h"

// Allocates the offscreen canvas used for the raycast view. Call once from setup().
void initTrialView(M5GFX& display);

// Renders one frame of the Secret Realm trial (raycast scene + a slim HUD strip showing either
// the enemy HP bar while Fighting or route progress while Traveling/Cleared) into an internal
// offscreen canvas, then displays it scaled up to cover most of `display` via pushRotateZoom
// (see the zoom-scaling note below). Does not advance `state` - call tickTrial() separately in
// the game loop.
void renderTrialView(M5GFX& display, const TrialState& state);
```

```cpp
// src/trial_view.cpp
#include "trial_view.h"
#include "raycast.h"
#include "trial_textures.h"
#include "framebuffer.h"
#include "ui.h" // kHeaderHeight, for vertical centering below the header bar
#include <cmath>
#include <vector>

namespace {
// The raycaster computes at this resolution - deliberately close to the crystal's
// hardware-proven 240x240 pixel-fill cost (240x320 = ~33% more pixels, same order of
// magnitude) rather than guessing a full native 720x1280 buffer with no way to benchmark it
// in this environment. It's then displayed scaled up via pushRotateZoom (kTrialZoom) to
// cover most of the screen without paying full-resolution compute cost - "near-full-screen"
// per the spec's own hedge on this point. If on-device FPS testing (see README) shows
// headroom, kTrialViewWidth/Height and kTrialZoom are the two knobs to raise; if it's too
// slow, lower kTrialZoom first (cheap - it only affects the display scale, not raycasting
// cost), then kTrialViewWidth/Height (which does affect cost).
constexpr int kTrialViewWidth = 240;
constexpr int kTrialViewHeight = 320;
constexpr float kTrialZoom = 2.5f; // -> displayed at ~600x800 pixels on screen
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
        // matching the crystal renderer's cheap-but-effective shading philosophy.
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

    // Slim HUD strip along the top of the canvas: enemy HP bar while fighting, otherwise
    // route progress. Drawn directly on the canvas before pushing to the display.
    gTrialCanvas->fillRect(0, 0, kTrialViewWidth, 12, TFT_BLACK);
    if (state.phase == TrialPhase::Fighting) {
        float hpFraction = static_cast<float>(state.enemy.hp) /
                            static_cast<float>(state.enemy.maxHp > 0 ? state.enemy.maxHp : 1);
        int barWidth = static_cast<int>((kTrialViewWidth - 4) * hpFraction);
        gTrialCanvas->fillRect(2, 2, barWidth, 8, TFT_RED);
    } else if (state.phase == TrialPhase::Cleared) {
        gTrialCanvas->setTextColor(TFT_GOLD, TFT_BLACK);
        gTrialCanvas->setCursor(2, 2);
        gTrialCanvas->print("Secret Realm Cleared!");
    } else {
        float progress = static_cast<float>(state.currentWaypointIndex) /
                          static_cast<float>(state.map.route.size() > 1
                                                  ? state.map.route.size() - 1
                                                  : 1);
        int barWidth = static_cast<int>((kTrialViewWidth - 4) * progress);
        gTrialCanvas->fillRect(2, 2, barWidth, 8, TFT_GREEN);
    }

    // Scaled push: displays the small internal buffer stretched to cover most of the screen,
    // centered horizontally and vertically within the space below the header bar. Default
    // sprite pivot is its own center, so (centerX, centerY) here is where that center lands
    // on the physical display.
    float centerX = display.width() / 2.0f;
    float centerY = kHeaderHeight + (display.height() - kHeaderHeight) / 2.0f;
    gTrialCanvas->pushRotateZoom(centerX, centerY, 0.0f, kTrialZoom, kTrialZoom);
}
```

- [ ] **Step 2: Verify it compiles against the real target**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: compiles without errors. (This does not upload — no device is required. If a
toolchain download is needed and this environment has no network access, note the failure
reason rather than guessing; this is a build-only check, not an on-device test.)

- [ ] **Step 3: Commit**

```bash
git add src/trial_view.h src/trial_view.cpp
git commit -m "Add device-side raycast rendering for the Secret Realm trial view"
```

---

### Task 8: Wire the mode switch, entry/return buttons, and reward payout into `main.cpp`/`ui.cpp`

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: everything from Tasks 5 and 7, plus existing `GameState`/`hitTestHud` (`economy.h`/`hittest.h`) and `HUD_BUTTON_*` constants (`ui.h`).
- Produces: `HUD_BUTTON_ENTER_SECRET_REALM` and `HUD_BUTTON_RETURN_TO_CULTIVATION` added to the `HudButton` enum in `ui.h`; a `ViewMode` enum and mode switch in `main.cpp`.

- [ ] **Step 1: Add new button ids to `ui.h`**

In `src/ui.h`, extend the existing enum (leaving prior values unchanged, matching how
`HUD_BUTTON_GENERATOR_BASE` already reserves a range):

```cpp
enum HudButton {
    HUD_BUTTON_NONE = -1,
    HUD_BUTTON_BREAKTHROUGH = 100,
    HUD_BUTTON_ENTER_SECRET_REALM = 101,
    HUD_BUTTON_RETURN_TO_CULTIVATION = 102,
    // Generator buy buttons use HUD_BUTTON_GENERATOR_BASE + genIndex (0..NUM_GENERATORS-1).
    HUD_BUTTON_GENERATOR_BASE = 0,
};
```

- [ ] **Step 2: Draw and hit-test the "Enter Secret Realm" button in `ui.cpp`**

Read `src/ui.cpp` first to match its existing button-drawing/hit-testing pattern for
`HUD_BUTTON_BREAKTHROUGH` exactly (same rectangle-tracking approach, same
enabled/disabled styling based on a gating condition), then add a same-shaped button
beneath it, gated on `state.realmIndex >= 2`, returning `HUD_BUTTON_ENTER_SECRET_REALM`
from `hitTestHud` when tapped inside its rect while enabled.

- [ ] **Step 3: Add the new includes and the mode switch to `main.cpp`**

```cpp
// Add alongside main.cpp's existing #include block (near "ui.h")
#include "trial_map.h"
#include "trial_state.h"
#include "trial_view.h"
```

```cpp
// Add near the other anonymous-namespace globals in main.cpp
enum class ViewMode { Idle, TrialGround };
ViewMode gViewMode = ViewMode::Idle;
TrialState gTrialState;
bool gTrialStarted = false;
uint32_t gLastTrialTickMs = 0;
```

In `setup()`, after `initHud(M5.Display);`:

```cpp
initTrialView(M5.Display);
```

In `loop()`, replace the unconditional crystal-render block (the section from
`RasterParams params;` through `gCanvas->pushSprite(gCrystalX, gCrystalY);`) with a
branch on `gViewMode`, and extend the touch-handling block to switch modes:

```cpp
if (touch.wasClicked()) {
    int button = hitTestHud(touch.x, touch.y);
    bool stateChanged = false;
    if (button == HUD_BUTTON_ENTER_SECRET_REALM && gViewMode == ViewMode::Idle) {
        gViewMode = ViewMode::TrialGround;
        if (!gTrialStarted) {
            gTrialState = startTrial(makeSecretRealmMap(), gState.realmIndex);
            gTrialStarted = true;
        }
        // Reset the trial's frame clock on every entry (not just the first), otherwise the
        // next tick's dt is computed against a stale gLastTrialTickMs from whenever the mode
        // was last active - potentially seconds or minutes ago - causing one huge simulated
        // dt step that skips the player through most of the route/combat in a single tick.
        gLastTrialTickMs = millis();
    } else if (button == HUD_BUTTON_RETURN_TO_CULTIVATION) {
        gViewMode = ViewMode::Idle;
    } else if (button == HUD_BUTTON_BREAKTHROUGH) {
        // ...existing breakthrough handling...
    } else if (button >= HUD_BUTTON_GENERATOR_BASE && button < HUD_BUTTON_GENERATOR_BASE + NUM_GENERATORS) {
        // ...existing generator purchase handling...
    }
    // ...existing stateChanged/save block...
}

if (gViewMode == ViewMode::Idle) {
    RasterParams params;
    // ...existing crystal RasterParams setup and rasterizeMesh/blit/pushSprite, unchanged...
} else {
    uint32_t nowTrial = millis();
    double dt = (nowTrial - gLastTrialTickMs) / 1000.0;
    gLastTrialTickMs = nowTrial;

    // Reward scales with the Qi needed for the player's *next* breakthrough (or stays at the
    // final realm's own threshold once there's no next realm), so clearing the trial is
    // always worth a meaningful fraction of "how far you have left to go," matching the
    // spec's proposed scaling.
    int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
    double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
    TrialPhase phaseBefore = gTrialState.phase;

    tickTrial(gTrialState, dt, reward);

    if (phaseBefore != TrialPhase::Cleared && gTrialState.phase == TrialPhase::Cleared) {
        // Apply the reward exactly once, on the single tick this transition happens (checking
        // qiRewardPending > 0 every frame instead would re-apply it every frame after, since
        // tickTrial() leaves it set while parked in Cleared - a real bug caught during the
        // plan's self-review, not a hypothetical).
        gState.qi += gTrialState.qiRewardPending;
        saveNow();
        renderTrialView(M5.Display, gTrialState); // show the "Cleared!" frame before pausing
        delay(1500);
        restartTrial(gTrialState); // resets qiRewardPending to 0.0 and loops back to the start
    } else {
        renderTrialView(M5.Display, gTrialState);
    }
}
```

Also skip the existing HUD panel redraw's generator/breakthrough content while in
`TrialGround` mode (it has nothing to show there) but keep the header bar's battery/Qi
display logic intact so the economy readout is still visible when the player returns —
read the current `drawHud`/`initHud` split in `ui.cpp` before changing this, since the
right seam depends on how the header and panel sprites are currently separated.

- [ ] **Step 4: Build for the real target**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "Wire the Secret Realm mode switch, entry/return buttons, and reward payout"
```

---

### Task 9: Sound effects

**Files:**
- Modify: `src/trial_view.cpp`
- Modify: `src/trial_view.h`

**Interfaces:**
- Produces: `void playAttackSfx();` `void playHitSfx();` `void playVictorySfx();` (declared in `trial_view.h`, called from `main.cpp`'s trial-mode branch when `tickTrial`'s return value / state transitions indicate an attack landed or the trial just cleared — `tickTrial` itself stays UI-agnostic per its existing signature, so detect these edges in `main.cpp` by comparing `gTrialState.phase`/HP before and after each `tickTrial` call).

- [ ] **Step 1: Add tone-based SFX functions to `trial_view.cpp`/`.h`**

```cpp
// Add to src/trial_view.h
void playAttackSfx();
void playHitSfx();
void playVictorySfx();
```

```cpp
// Add to src/trial_view.cpp
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

**Note for the implementer:** confirm `M5.Speaker.tone(float frequency, uint32_t duration_ms)`
against the exact M5Unified version pinned in `platformio.ini` before this step — the spec
flags this API shape as an open risk. If the signature differs, adjust the calls accordingly;
the behavior (three distinct short tones for attack/hit/victory) is what matters, not these
exact literals.

- [ ] **Step 2: Call them from `main.cpp` at the right transitions**

Modify the `TrialGround` branch from Task 8 to capture both combatants' HP before calling
`tickTrial`, then compare after — this directly detects "who got hit," rather than
inferring it from a phase transition — and to play the victory jingle at the same
already-correct once-only reward-transition check Task 8 established:

```cpp
} else {
    uint32_t nowTrial = millis();
    double dt = (nowTrial - gLastTrialTickMs) / 1000.0;
    gLastTrialTickMs = nowTrial;

    int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
    double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
    TrialPhase phaseBefore = gTrialState.phase;
    bool wasFighting = (phaseBefore == TrialPhase::Fighting);
    int enemyHpBefore = gTrialState.enemy.hp;
    int playerHpBefore = gTrialState.player.hp;

    tickTrial(gTrialState, dt, reward);

    if (wasFighting && gTrialState.enemy.hp < enemyHpBefore) playAttackSfx();
    if (wasFighting && gTrialState.player.hp < playerHpBefore) playHitSfx();

    if (phaseBefore != TrialPhase::Cleared && gTrialState.phase == TrialPhase::Cleared) {
        playVictorySfx();
        gState.qi += gTrialState.qiRewardPending;
        saveNow();
        renderTrialView(M5.Display, gTrialState);
        delay(1500);
        restartTrial(gTrialState);
    } else {
        renderTrialView(M5.Display, gTrialState);
    }
}
```

- [ ] **Step 3: Build for the real target**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/trial_view.h src/trial_view.cpp src/main.cpp
git commit -m "Add procedural attack/hit/victory sound effects to the Secret Realm trial"
```

---

### Task 10: README update and full test run

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the Secret Realm mode**

Add a section to `README.md` (after the existing Xianxia Idle Game description)
describing: the raycasting technique used, that it autoplays (navigation + combat) the
same way the rest of the game does, the Foundation Establishment unlock gate, the
procedural (non-imported) textures and SFX, and — explicitly, matching this README's
existing honesty about what has/hasn't been hardware-validated — that the raycast view's
real on-device FPS and screen-coverage sizing have **not** been benchmarked on physical
hardware as part of this change, unlike the crystal's `kRenderSize = 240` which was
empirically tuned; flag this as the immediate next validation step for whoever has the
physical Tab5.

- [ ] **Step 2: Run the full native test suite**

Run: `python3 -m platformio test -e native`
Expected: all suites pass, including the five new ones (`test_raycast`, `test_trial_map`,
`test_trial_combat`, `test_trial_state`, `test_trial_textures`).

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Document the Secret Realm raycasting trial mode"
```
