# Xianxia Idle Game for Tab5 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a xianxia-themed idle game for the M5Stack Tab5 (ESP32-P4) with a procedurally-evolving, software-rasterized 3D crystal centerpiece, persistent save state, and RTC-based offline earnings.

**Architecture:** Hardware-agnostic game logic (3D math, procedural mesh growth, software rasterizer, economy, save serialization, offline-earnings math, hit-testing) lives in `lib/core/` as plain C++ with zero Arduino/M5GFX dependencies, so it can be unit-tested on the host machine via PlatformIO's `native` platform + Unity. Hardware glue (display/touch/RTC/NVS, the Arduino `setup()`/`loop()`) lives in `src/` and is validated by flashing to the connected Tab5 and checking serial output / the physical screen.

**Tech Stack:** PlatformIO, Arduino framework via the `pioarduino` `platform-espressif32` fork, M5Unified + M5GFX libraries, C++17, PlatformIO's bundled Unity test framework for native unit tests.

**Spec:** `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`

## Global Constraints

- Two PlatformIO environments: `esp32p4_pioarduino` (real hardware, exact config from `docs/Tab5.pdf`'s PlatformIO section) and `native` (host-only unit tests, `build_src_filter = -<*>` so `src/` is excluded and only `lib/core/` + `test/` compile).
- The device is connected and enumerates at `/dev/ttyACM0` (Espressif USB-JTAG/serial). All flash/monitor commands target this port explicitly.
- No LVGL — HUD is hand-rolled with M5GFX primitives.
- No Wi-Fi/NTP dependency for v1 — offline earnings only need *elapsed* RTC time between two readings, never absolute wall-clock correctness.
- No tap-to-earn on the 3D model — taps are for HUD buttons only (confirmed with the user).
- **Resolved ambiguity:** "Attempt Breakthrough" **spends** the next realm's Qi threshold (subtracts it from `qi`) on success — this is the concrete interpretation of the spec's breakthrough mechanic.
- `NUM_REALMS = 7`, `NUM_GENERATORS = 6` are fixed for v1 (see Task 3 / Task 5 for the exact realm and generator tables).
- All Bash commands in this plan invoke PlatformIO as `python3 -m platformio ...` rather than the bare `pio` command, because PlatformIO is not yet installed and a bare `pio` may not be on `PATH` in a fresh shell.
- Every hardware task that has a checkable outcome prints a clear pass/fail marker to `Serial` (baud 115200) so it can be verified via `python3 -m platformio device monitor`, since the agent cannot see the physical screen directly. Genuinely visual checks (does the crystal look right, is text legible) are called out explicitly as "ask the user to look at the device."
- Two items the spec flagged as open risks are deliberately **not** investigated by this plan, because the spec itself already names a sufficient fallback: (1) whether M5Unified exposes a pre-shutdown hook to force a final save — periodic autosave (Task 11) is used unconditionally instead; (2) a compile-time "fast-forward simulated time" debug flag for testing long-run progression — plain waiting/`sleep` is used instead where a real-time check is needed (Task 10 Step 5). Both are reasonable v2 enhancements, not v1 blockers.

---

## Task 1: Toolchain & Project Scaffold

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `test/test_smoke/test_smoke.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: a buildable/flashable `esp32p4_pioarduino` environment and a runnable `native` test environment, which every later task relies on.

- [ ] **Step 1: Install PlatformIO Core**

Run:
```bash
python3 -m pip install --user platformio
python3 -m platformio --version
```
Expected: prints something like `PlatformIO Core, version 6.x.x`.

- [ ] **Step 2: Create the project skeleton directories**

```bash
mkdir -p src lib/core test/test_smoke
```

- [ ] **Step 3: Write `platformio.ini`**

```ini
; PlatformIO Project Configuration File

[env:esp32p4_pioarduino]
platform = https://github.com/pioarduino/platform-espressif32.git#54.03.21
upload_speed = 1500000
monitor_speed = 115200
upload_port = /dev/ttyACM0
monitor_port = /dev/ttyACM0
build_type = debug
framework = arduino
board = esp32p4-evboard
board_build.mcu = esp32p4
board_build.flash_mode = qio
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    https://github.com/M5Stack/M5Unified.git
    https://github.com/M5Stack/M5GFX.git

[env:native]
platform = native
build_src_filter = -<*>
```

This is the exact hardware-env block from `docs/Tab5.pdf`'s PlatformIO section, plus explicit `upload_port`/`monitor_port` for the detected device, plus a `native` env that excludes `src/` (so Arduino-only files never get pulled into host-side test builds) while still auto-discovering `lib/core/`.

- [ ] **Step 4: Write the minimal scaffold `src/main.cpp`**

```cpp
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Tab5 idle game scaffold booting");

    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(20, 20);
    M5.Display.println("Hello Tab5");

    Serial.println("[BOOT] Display initialized, showing Hello Tab5");
}

void loop() {
    M5.update();
    delay(10);
}
```

- [ ] **Step 5: Build for the hardware environment**

```bash
python3 -m platformio run -e esp32p4_pioarduino
```
Expected: ends with `[SUCCESS]`. First run downloads the pioarduino platform, toolchain, and M5Unified/M5GFX from GitHub — this needs network access and can take several minutes.

- [ ] **Step 6: Flash to the connected device**

```bash
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```
Expected: ends with `[SUCCESS]`; the device resets.

- [ ] **Step 7: Verify over serial**

```bash
timeout 8 python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200 | tee /tmp/task1_boot_log.txt
grep -q "Display initialized" /tmp/task1_boot_log.txt && echo "PASS" || echo "FAIL"
```
Expected: `PASS`. Also ask the user to glance at the Tab5 screen and confirm it shows white "Hello Tab5" text on a black background — this is the first real confirmation the toolchain, board config, and display bring-up all work end-to-end.

- [ ] **Step 8: Set up the native test scaffold**

```cpp
// test/test_smoke/test_smoke.cpp
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_smoke() {
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
```

- [ ] **Step 9: Run the native test suite**

```bash
python3 -m platformio test -e native
```
Expected: output reports the `test_smoke` suite passing with 0 failures.

- [ ] **Step 10: Ignore PlatformIO build artifacts and commit**

Add to `.gitignore`:
```
# PlatformIO
.pio/
.vscode/
```

```bash
git add platformio.ini src/main.cpp test/test_smoke/test_smoke.cpp .gitignore
git commit -m "Add PlatformIO scaffold for Tab5 idle game (hardware + native test envs)"
```

---

## Task 2: 3D Math Primitives (`math3d`)

**Files:**
- Create: `lib/core/math3d.h`
- Test: `test/test_math3d/test_math3d.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `Vec3` (fields `x,y,z`; `operator+`, `operator-`, `operator*(float)`, `dot`, `cross`, `length`, `normalized`), `Mat4` (`identity()`, `rotationY(float radians)`, `translation(float,float,float)`, `multiply(const Mat4&)`, `transformPoint(const Vec3&)`), `ProjectedPoint{screenX, screenY, depth}`, `project(const Vec3&, float focalLength, int screenWidth, int screenHeight)` — all used by Task 3 (mesh) and Task 4 (rasterizer).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_math3d/test_math3d.cpp
#include <unity.h>
#include "math3d.h"

void setUp(void) {}
void tearDown(void) {}

void test_vec3_add() {
    Vec3 a{1, 2, 3};
    Vec3 b{4, 5, 6};
    Vec3 r = a + b;
    TEST_ASSERT_EQUAL_FLOAT(5.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, r.z);
}

void test_vec3_dot() {
    Vec3 a{1, 0, 0};
    Vec3 b{0, 1, 0};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.dot(b));
    Vec3 c{2, 3, 4};
    TEST_ASSERT_EQUAL_FLOAT(4.0f + 9.0f + 16.0f, c.dot(c));
}

void test_vec3_cross_of_axes() {
    Vec3 x{1, 0, 0};
    Vec3 y{0, 1, 0};
    Vec3 r = x.cross(y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.z);
}

void test_vec3_normalized() {
    Vec3 v{3, 4, 0};
    Vec3 n = v.normalized();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, n.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, n.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, n.length());
}

void test_mat4_rotationY_quarter_turn() {
    Mat4 r = Mat4::rotationY(static_cast<float>(M_PI) / 2.0f);
    Vec3 p{1, 0, 0};
    Vec3 rotated = r.transformPoint(p);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rotated.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rotated.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, rotated.z);
}

void test_mat4_translation() {
    Mat4 t = Mat4::translation(1, 2, 3);
    Vec3 p{0, 0, 0};
    Vec3 r = t.transformPoint(p);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, r.y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, r.z);
}

void test_project_center_point() {
    Vec3 p{0, 0, 5};
    ProjectedPoint proj = project(p, 1.5f, 320, 240);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 160.0f, proj.screenX);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, proj.screenY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, proj.depth);
}

void test_project_point_offset_right_and_up() {
    Vec3 p{1, 1, 5};
    ProjectedPoint proj = project(p, 1.5f, 320, 240);
    TEST_ASSERT_TRUE(proj.screenX > 160.0f);
    TEST_ASSERT_TRUE(proj.screenY < 120.0f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_vec3_add);
    RUN_TEST(test_vec3_dot);
    RUN_TEST(test_vec3_cross_of_axes);
    RUN_TEST(test_vec3_normalized);
    RUN_TEST(test_mat4_rotationY_quarter_turn);
    RUN_TEST(test_mat4_translation);
    RUN_TEST(test_project_center_point);
    RUN_TEST(test_project_point_offset_right_and_up);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_math3d
```
Expected: build FAILS — `math3d.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/math3d.h`**

```cpp
#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        float len = length();
        if (len < 1e-6f) return {0, 0, 0};
        return { x / len, y / len, z / len };
    }
};

// Minimal 4x4, used only to rotate/translate Vec3 points (no full matrix stack needed).
struct Mat4 {
    float m[4][4] = {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1}
    };

    static Mat4 identity() { return Mat4{}; }

    static Mat4 rotationY(float radians) {
        Mat4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r = identity();
        r.m[0][3] = x; r.m[1][3] = y; r.m[2][3] = z;
        return r;
    }

    Mat4 multiply(const Mat4& o) const {
        Mat4 r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += m[row][k] * o.m[k][col];
                r.m[row][col] = sum;
            }
        }
        return r;
    }

    Vec3 transformPoint(const Vec3& v) const {
        float x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3];
        float y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3];
        float z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3];
        return {x, y, z};
    }
};

struct ProjectedPoint {
    float screenX;
    float screenY;
    float depth; // camera-space Z; used for backface/depth-sort elsewhere
};

// Perspective projection: camera looks down +Z from the origin. Point must be in front
// (z > 0). Maps NDC [-1,1] to pixel space with (0,0) at top-left.
inline ProjectedPoint project(const Vec3& cameraSpacePoint, float focalLength,
                               int screenWidth, int screenHeight) {
    float z = cameraSpacePoint.z;
    if (z < 0.01f) z = 0.01f;
    float ndcX = (cameraSpacePoint.x * focalLength) / z;
    float ndcY = (cameraSpacePoint.y * focalLength) / z;
    ProjectedPoint p;
    p.screenX = (ndcX + 1.0f) * 0.5f * screenWidth;
    p.screenY = (1.0f - (ndcY + 1.0f) * 0.5f) * screenHeight;
    p.depth = z;
    return p;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_math3d
```
Expected: all 8 tests pass.

- [ ] **Step 5: Commit**

```bash
git add lib/core/math3d.h test/test_math3d/test_math3d.cpp
git commit -m "Add Vec3/Mat4/project math primitives with native unit tests"
```

---

## Task 3: Procedural Crystal Mesh (`mesh`)

**Files:**
- Create: `lib/core/color.h`
- Create: `lib/core/realms.h`
- Create: `lib/core/mesh.h`
- Create: `lib/core/mesh.cpp`
- Test: `test/test_mesh/test_mesh.cpp`

**Interfaces:**
- Consumes: `Vec3` from `math3d.h` (Task 2).
- Produces: `RGB{r,g,b}` (`color.h`), `NUM_REALMS` (`realms.h`), `Face{a,b,c}`, `Mesh{vertices, faces}`, `makeIcosahedron()`, `subdivide(const Mesh&)`, `RealmVisual{mesh, baseColor, rimColor}`, `growForRealm(const Mesh& base, int realmIndex)`, `hashJaggedness(int,int)` — consumed by Task 4 (rasterizer takes `Mesh`/`Face`) and by Task 8 (rendering integration).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_mesh/test_mesh.cpp
#include <unity.h>
#include "mesh.h"

void setUp(void) {}
void tearDown(void) {}

void test_icosahedron_has_12_vertices_and_20_faces() {
    Mesh m = makeIcosahedron();
    TEST_ASSERT_EQUAL(12, m.vertices.size());
    TEST_ASSERT_EQUAL(20, m.faces.size());
}

void test_icosahedron_vertices_are_unit_length() {
    Mesh m = makeIcosahedron();
    for (const auto& v : m.vertices) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.length());
    }
}

void test_subdivide_quadruples_face_count() {
    Mesh m = makeIcosahedron();
    Mesh sub = subdivide(m);
    TEST_ASSERT_EQUAL(80, sub.faces.size());
}

void test_subdivide_new_vertices_stay_unit_length() {
    Mesh m = makeIcosahedron();
    Mesh sub = subdivide(m);
    for (const auto& v : sub.vertices) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.length());
    }
}

void test_growForRealm_is_deterministic() {
    Mesh base = makeIcosahedron();
    RealmVisual a = growForRealm(base, 3);
    RealmVisual b = growForRealm(base, 3);
    TEST_ASSERT_EQUAL(a.mesh.vertices.size(), b.mesh.vertices.size());
    for (size_t i = 0; i < a.mesh.vertices.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].x, b.mesh.vertices[i].x);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].y, b.mesh.vertices[i].y);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.mesh.vertices[i].z, b.mesh.vertices[i].z);
    }
}

void test_growForRealm_jaggedness_increases_with_realm() {
    Mesh base = makeIcosahedron();
    RealmVisual low = growForRealm(base, 0);
    RealmVisual high = growForRealm(base, NUM_REALMS - 1);

    float lowTotalDisplacement = 0.0f;
    float highTotalDisplacement = 0.0f;
    for (size_t i = 0; i < base.vertices.size(); ++i) {
        lowTotalDisplacement += (low.mesh.vertices[i] - base.vertices[i]).length();
        highTotalDisplacement += (high.mesh.vertices[i] - base.vertices[i]).length();
    }
    TEST_ASSERT_TRUE(highTotalDisplacement > lowTotalDisplacement);
}

void test_growForRealm_returns_expected_palette_per_realm() {
    Mesh base = makeIcosahedron();
    RealmVisual mortal = growForRealm(base, 0);
    RealmVisual voidRefinement = growForRealm(base, NUM_REALMS - 1);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.r);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.g);
    TEST_ASSERT_EQUAL_UINT8(220, mortal.baseColor.b);
    TEST_ASSERT_EQUAL_UINT8(255, voidRefinement.baseColor.r);
    TEST_ASSERT_EQUAL_UINT8(245, voidRefinement.baseColor.g);
    TEST_ASSERT_EQUAL_UINT8(210, voidRefinement.baseColor.b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_icosahedron_has_12_vertices_and_20_faces);
    RUN_TEST(test_icosahedron_vertices_are_unit_length);
    RUN_TEST(test_subdivide_quadruples_face_count);
    RUN_TEST(test_subdivide_new_vertices_stay_unit_length);
    RUN_TEST(test_growForRealm_is_deterministic);
    RUN_TEST(test_growForRealm_jaggedness_increases_with_realm);
    RUN_TEST(test_growForRealm_returns_expected_palette_per_realm);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_mesh
```
Expected: build FAILS — `mesh.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/color.h`**

```cpp
#pragma once
#include <cstdint>

struct RGB { uint8_t r, g, b; };
```

- [ ] **Step 4: Write `lib/core/realms.h`**

```cpp
#pragma once

// Shared by mesh.h (visual growth/palette tables) and economy.h (names/thresholds),
// kept as its own tiny header so neither module depends on the other for this count.
constexpr int NUM_REALMS = 7;
```

- [ ] **Step 5: Write `lib/core/mesh.h`**

```cpp
#pragma once
#include <vector>
#include "math3d.h"
#include "color.h"
#include "realms.h"

struct Face { int a, b, c; };

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
};

Mesh makeIcosahedron();
Mesh subdivide(const Mesh& mesh); // one level of subdivision; new vertices land on the unit sphere

struct RealmVisual {
    Mesh mesh;
    RGB baseColor;
    RGB rimColor;
};

// Deterministic per-(vertex,realm) pseudo-random value in [0,1) — no RNG state, so the
// same inputs always produce the same output (see growForRealm's determinism test).
float hashJaggedness(int vertexIndex, int realmIndex);

// Displaces `base`'s vertices outward along their own direction from the mesh center by
// an amount that grows with realmIndex, and returns that mesh plus the realm's base/rim
// color palette. realmIndex is clamped into [0, NUM_REALMS - 1].
RealmVisual growForRealm(const Mesh& base, int realmIndex);
```

- [ ] **Step 6: Write `lib/core/mesh.cpp`**

```cpp
#include "mesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

Mesh makeIcosahedron() {
    float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    Mesh mesh;
    mesh.vertices = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };
    for (auto& v : mesh.vertices) v = v.normalized();

    mesh.faces = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1}
    };
    return mesh;
}

namespace {
struct EdgeKey {
    int lo, hi;
    bool operator==(const EdgeKey& o) const { return lo == o.lo && hi == o.hi; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        return static_cast<size_t>(k.lo) * 100003u + static_cast<size_t>(k.hi);
    }
};
}

Mesh subdivide(const Mesh& mesh) {
    Mesh out;
    out.vertices = mesh.vertices;
    std::unordered_map<EdgeKey, int, EdgeKeyHash> midpointCache;

    auto midpoint = [&](int i0, int i1) -> int {
        EdgeKey key{std::min(i0, i1), std::max(i0, i1)};
        auto found = midpointCache.find(key);
        if (found != midpointCache.end()) return found->second;
        Vec3 mid = (out.vertices[i0] + out.vertices[i1]) * 0.5f;
        mid = mid.normalized();
        out.vertices.push_back(mid);
        int newIndex = static_cast<int>(out.vertices.size()) - 1;
        midpointCache[key] = newIndex;
        return newIndex;
    };

    for (const auto& f : mesh.faces) {
        int ab = midpoint(f.a, f.b);
        int bc = midpoint(f.b, f.c);
        int ca = midpoint(f.c, f.a);
        out.faces.push_back({f.a, ab, ca});
        out.faces.push_back({f.b, bc, ab});
        out.faces.push_back({f.c, ca, bc});
        out.faces.push_back({ab, bc, ca});
    }
    return out;
}

float hashJaggedness(int vertexIndex, int realmIndex) {
    uint32_t h = static_cast<uint32_t>(vertexIndex) * 374761393u
                + static_cast<uint32_t>(realmIndex) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h % 10000) / 10000.0f;
}

namespace {
// Base color, then rim (glow) color, per realm — pale/clear through radiant white-gold,
// following the xianxia realm-color progression from the design spec.
const RGB kBaseColors[NUM_REALMS] = {
    {220, 220, 220}, { 90, 160, 240}, { 90, 220, 140}, {230, 190,  70},
    {170, 100, 230}, {210,  60,  60}, {255, 245, 210},
};
const RGB kRimColors[NUM_REALMS] = {
    {255, 255, 255}, {140, 200, 255}, {160, 255, 200}, {255, 230, 140},
    {220, 170, 255}, {255, 140, 140}, {255, 255, 255},
};
}

RealmVisual growForRealm(const Mesh& base, int realmIndex) {
    int clampedRealm = realmIndex;
    if (clampedRealm < 0) clampedRealm = 0;
    if (clampedRealm >= NUM_REALMS) clampedRealm = NUM_REALMS - 1;

    RealmVisual result;
    result.mesh = base;
    float jaggedness = 0.05f + 0.05f * static_cast<float>(clampedRealm);
    const float kMaxDisplacement = 0.5f;

    for (size_t i = 0; i < result.mesh.vertices.size(); ++i) {
        Vec3 dir = base.vertices[i].normalized();
        float offset = hashJaggedness(static_cast<int>(i), clampedRealm) * jaggedness;
        if (offset > kMaxDisplacement) offset = kMaxDisplacement;
        result.mesh.vertices[i] = base.vertices[i] + dir * offset;
    }

    result.baseColor = kBaseColors[clampedRealm];
    result.rimColor = kRimColors[clampedRealm];
    return result;
}
```

- [ ] **Step 7: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_mesh
```
Expected: all 7 tests pass.

- [ ] **Step 8: Commit**

```bash
git add lib/core/color.h lib/core/realms.h lib/core/mesh.h lib/core/mesh.cpp test/test_mesh/test_mesh.cpp
git commit -m "Add procedural icosahedron crystal mesh with per-realm growth and palette"
```

> **Hands-on note for whoever runs this task:** `growForRealm`'s displacement/palette logic is a creative call (how jagged/ornate should each realm look?), not boilerplate — if you want to shape this yourself rather than use the values above, this is the spot.

---

## Task 4: Software Rasterizer (`framebuffer` + `rasterizer`)

**Files:**
- Create: `lib/core/framebuffer.h`
- Create: `lib/core/rasterizer.h`
- Create: `lib/core/rasterizer.cpp`
- Test: `test/test_rasterizer/test_rasterizer.cpp`

**Interfaces:**
- Consumes: `Vec3`, `Mat4`, `ProjectedPoint`, `project()` (Task 2); `Mesh`, `Face`, `RGB` (Task 3).
- Produces: `Framebuffer{width, height, pixels, clear(), setPixel(), getPixel()}`, `RasterParams{transform, cameraDistance, focalLength, lightDir, viewDir, baseColor, rimColor}`, `rasterizeMesh(const Mesh&, const RasterParams&, Framebuffer&)` — consumed by Task 8 (renders into an offscreen buffer that gets pushed to the display).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_rasterizer/test_rasterizer.cpp
#include <unity.h>
#include "framebuffer.h"
#include "rasterizer.h"
#include "mesh.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
Mesh singleTriangle(bool frontFacing) {
    Mesh m;
    if (frontFacing) {
        m.vertices = { {-1, -1, 0}, {0, 1, 0}, {1, -1, 0} };
    } else {
        m.vertices = { {-1, -1, 0}, {1, -1, 0}, {0, 1, 0} };
    }
    m.faces = { {0, 1, 2} };
    return m;
}

RasterParams defaultParams() {
    RasterParams p;
    p.transform = Mat4::identity();
    p.cameraDistance = 5.0f;
    p.focalLength = 1.5f;
    p.lightDir = Vec3{0, 0, -1}.normalized();
    p.viewDir = Vec3{0, 0, -1}.normalized();
    p.baseColor = RGB{200, 50, 50};
    p.rimColor = RGB{255, 255, 255};
    return p;
}

RGB clearColor() { return RGB{10, 10, 10}; }

bool pixelChangedFromClear(const Framebuffer& fb, int x, int y) {
    RGB p = fb.getPixel(x, y);
    RGB c = clearColor();
    return p.r != c.r || p.g != c.g || p.b != c.b;
}
}

void test_front_facing_triangle_draws_pixels() {
    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), defaultParams(), fb);
    TEST_ASSERT_TRUE(pixelChangedFromClear(fb, 32, 32));
}

void test_back_facing_triangle_is_culled() {
    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(singleTriangle(false), defaultParams(), fb);
    TEST_ASSERT_FALSE(pixelChangedFromClear(fb, 32, 32));
}

void test_depth_sort_picks_nearer_face_within_one_mesh() {
    Mesh m;
    // Far triangle: flat, dead-on to the camera, at local z = 0 (lambert == 1.0 exactly).
    Vec3 farA{-1, -1, 0}, farB{0, 1, 0}, farC{1, -1, 0};
    // Near triangle: same footprint, pulled toward the camera and tilted (non-uniform
    // local z among its 3 vertices), so its normal isn't parallel to the light and its
    // shading is measurably dimmer than the far triangle's.
    Vec3 nearA{-1, -1, -1.2f}, nearB{0, 1, -0.6f}, nearC{1, -1, -1.2f};

    m.vertices = { farA, farB, farC, nearA, nearB, nearC };
    m.faces = { {0, 1, 2}, {3, 4, 5} };

    Framebuffer fb(64, 64);
    fb.clear(clearColor());
    rasterizeMesh(m, defaultParams(), fb);

    RGB center = fb.getPixel(32, 32);
    // A dead-on-lit far face alone would read exactly baseColor.r (200). If the nearer,
    // tilted face correctly wins the depth sort, the visible pixel is dimmer than that.
    TEST_ASSERT_TRUE(center.r < 200);
    TEST_ASSERT_TRUE(center.r > 0);
}

void test_directly_lit_face_is_brighter_than_grazing_face() {
    RasterParams litParams = defaultParams();
    litParams.lightDir = Vec3{0, 0, -1}.normalized();

    RasterParams grazingParams = defaultParams();
    grazingParams.lightDir = Vec3{1, 0, 0}.normalized();

    Framebuffer litFb(64, 64);
    litFb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), litParams, litFb);

    Framebuffer grazingFb(64, 64);
    grazingFb.clear(clearColor());
    rasterizeMesh(singleTriangle(true), grazingParams, grazingFb);

    TEST_ASSERT_TRUE(litFb.getPixel(32, 32).r > grazingFb.getPixel(32, 32).r);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_front_facing_triangle_draws_pixels);
    RUN_TEST(test_back_facing_triangle_is_culled);
    RUN_TEST(test_depth_sort_picks_nearer_face_within_one_mesh);
    RUN_TEST(test_directly_lit_face_is_brighter_than_grazing_face);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_rasterizer
```
Expected: build FAILS — `framebuffer.h`/`rasterizer.h` don't exist yet.

- [ ] **Step 3: Write `lib/core/framebuffer.h`**

```cpp
#pragma once
#include <algorithm>
#include <vector>
#include "color.h"

struct Framebuffer {
    int width;
    int height;
    std::vector<RGB> pixels;

    Framebuffer(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h) {}

    void clear(RGB color) {
        std::fill(pixels.begin(), pixels.end(), color);
    }

    void setPixel(int x, int y, RGB color) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        pixels[static_cast<size_t>(y) * width + x] = color;
    }

    RGB getPixel(int x, int y) const {
        return pixels[static_cast<size_t>(y) * width + x];
    }
};
```

- [ ] **Step 4: Write `lib/core/rasterizer.h`**

```cpp
#pragma once
#include "framebuffer.h"
#include "math3d.h"
#include "mesh.h"

struct RasterParams {
    Mat4 transform;       // model transform (e.g. rotationY for auto-spin)
    float cameraDistance; // added to every vertex's local Z to push the mesh in front of the camera
    float focalLength;
    Vec3 lightDir;        // normalized; direction FROM a surface TOWARD the light
    Vec3 viewDir;         // normalized; direction FROM a surface TOWARD the camera (rim term)
    RGB baseColor;
    RGB rimColor;
};

// Pipeline: rotate -> transform to camera space -> perspective-project -> backface cull
// -> depth-sort back-to-front (painter's algorithm) -> flat/Lambert + rim shade -> fill.
// Does NOT clear `fb` first — the caller clears with the desired background color.
void rasterizeMesh(const Mesh& mesh, const RasterParams& params, Framebuffer& fb);
```

- [ ] **Step 5: Write `lib/core/rasterizer.cpp`**

```cpp
#include "rasterizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    return ab.cross(ac).normalized();
}

RGB shade(const Vec3& normal, const RasterParams& params) {
    float lambert = normal.dot(params.lightDir);
    if (lambert < 0.0f) lambert = 0.0f;
    float rim = 1.0f - normal.dot(params.viewDir);
    if (rim < 0.0f) rim = 0.0f;
    rim = rim * rim * rim; // sharpen the falloff (cheap fake-fresnel "aura" glow)

    auto mix = [](uint8_t base, uint8_t rimC, float lambertT, float rimT) -> uint8_t {
        float intensity = 0.25f + 0.75f * lambertT; // ambient floor so unlit faces aren't pure black
        float v = base * intensity + rimC * rimT;
        if (v > 255.0f) v = 255.0f;
        return static_cast<uint8_t>(v);
    };

    return RGB{
        mix(params.baseColor.r, params.rimColor.r, lambert, rim),
        mix(params.baseColor.g, params.rimColor.g, lambert, rim),
        mix(params.baseColor.b, params.rimColor.b, lambert, rim)
    };
}

void fillTriangle(Framebuffer& fb, float x0, float y0, float x1, float y1,
                   float x2, float y2, RGB color) {
    int minX = static_cast<int>(std::floor(std::min({x0, x1, x2})));
    int maxX = static_cast<int>(std::ceil(std::max({x0, x1, x2})));
    int minY = static_cast<int>(std::floor(std::min({y0, y1, y2})));
    int maxY = static_cast<int>(std::ceil(std::max({y0, y1, y2})));
    minX = std::max(minX, 0); minY = std::max(minY, 0);
    maxX = std::min(maxX, fb.width - 1); maxY = std::min(maxY, fb.height - 1);

    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-6f) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
            float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                fb.setPixel(x, y, color);
            }
        }
    }
}

} // namespace

void rasterizeMesh(const Mesh& mesh, const RasterParams& params, Framebuffer& fb) {
    std::vector<Vec3> camSpace(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec3 rotated = params.transform.transformPoint(mesh.vertices[i]);
        camSpace[i] = { rotated.x, rotated.y, rotated.z + params.cameraDistance };
    }

    struct DrawFace { int faceIndex; float avgZ; };
    std::vector<DrawFace> visible;
    visible.reserve(mesh.faces.size());

    for (size_t i = 0; i < mesh.faces.size(); ++i) {
        const Face& f = mesh.faces[i];
        const Vec3& a = camSpace[f.a];
        const Vec3& b = camSpace[f.b];
        const Vec3& c = camSpace[f.c];
        Vec3 normal = faceNormal(a, b, c);
        // Camera sits at the origin looking down +Z, so "toward camera" from a point p
        // is -p (normalized). Cull if the face points away from the camera.
        Vec3 towardCamera = (a * -1.0f).normalized();
        if (normal.dot(towardCamera) <= 0.0f) continue;

        float avgZ = (a.z + b.z + c.z) / 3.0f;
        visible.push_back({static_cast<int>(i), avgZ});
    }

    std::sort(visible.begin(), visible.end(), [](const DrawFace& lhs, const DrawFace& rhs) {
        return lhs.avgZ > rhs.avgZ; // farthest (largest Z) first -> painter's algorithm
    });

    for (const auto& df : visible) {
        const Face& f = mesh.faces[df.faceIndex];
        const Vec3& a = camSpace[f.a];
        const Vec3& b = camSpace[f.b];
        const Vec3& c = camSpace[f.c];
        RGB color = shade(faceNormal(a, b, c), params);

        ProjectedPoint pa = project(a, params.focalLength, fb.width, fb.height);
        ProjectedPoint pb = project(b, params.focalLength, fb.width, fb.height);
        ProjectedPoint pc = project(c, params.focalLength, fb.width, fb.height);
        fillTriangle(fb, pa.screenX, pa.screenY, pb.screenX, pb.screenY, pc.screenX, pc.screenY, color);
    }
}
```

- [ ] **Step 6: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_rasterizer
```
Expected: all 4 tests pass.

> If `test_front_facing_triangle_draws_pixels`/`test_back_facing_triangle_is_culled` don't match your intuition about which triangle should be visible, the issue is almost always the backface-cull sign convention or the hand-picked winding order — re-derive with the exact `faceNormal`/`towardCamera` formulas above before changing anything else.

- [ ] **Step 7: Commit**

```bash
git add lib/core/framebuffer.h lib/core/rasterizer.h lib/core/rasterizer.cpp test/test_rasterizer/test_rasterizer.cpp
git commit -m "Add software 3D rasterizer: project, backface-cull, depth-sort, shade, fill"
```

---

## Task 5: Idle Game Economy (`economy`)

**Files:**
- Create: `lib/core/economy.h`
- Create: `lib/core/economy.cpp`
- Test: `test/test_economy/test_economy.cpp`

**Interfaces:**
- Consumes: `NUM_REALMS` from `realms.h` (Task 3).
- Produces: `GeneratorDef{name, baseCost, growthRate, baseQiPerSecond, unlockRealmIndex}`, `NUM_GENERATORS = 6`, `GENERATORS[NUM_GENERATORS]`, `GameState{qi, generatorCounts[NUM_GENERATORS], realmIndex}`, `REALM_NAMES[NUM_REALMS]`, `REALM_QI_THRESHOLD[NUM_REALMS]`, `costForGenerator()`, `qiPerSecond()`, `realmMultiplier()`, `tick()`, `canBreakthrough()`, `attemptBreakthrough()`, `isGeneratorUnlocked()`, `purchaseGenerator()` — consumed by Task 6 (save conversion), Task 7 (offline earnings), Task 10/11 (game loop + HUD).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_economy/test_economy.cpp
#include <unity.h>
#include "economy.h"

void setUp(void) {}
void tearDown(void) {}

void test_realm_thresholds_increase_monotonically() {
    for (int i = 1; i < NUM_REALMS; ++i) {
        TEST_ASSERT_TRUE(REALM_QI_THRESHOLD[i] > REALM_QI_THRESHOLD[i - 1]);
    }
}

void test_cost_for_generator_grows_by_growth_rate() {
    double cost0 = costForGenerator(0, 0);
    double cost1 = costForGenerator(0, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001, GENERATORS[0].baseCost, cost0);
    TEST_ASSERT_FLOAT_WITHIN(0.001, GENERATORS[0].baseCost * GENERATORS[0].growthRate, cost1);
}

void test_qi_per_second_ignores_locked_generators() {
    GameState state;
    state.realmIndex = 0;
    state.generatorCounts[0] = 2;
    state.generatorCounts[1] = 5; // locked at realm 0, must not count
    double expected = 2 * GENERATORS[0].baseQiPerSecond * realmMultiplier(0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state));
}

void test_qi_per_second_applies_realm_multiplier() {
    GameState state;
    state.realmIndex = 2;
    state.generatorCounts[0] = 1;
    double expected = GENERATORS[0].baseQiPerSecond * realmMultiplier(2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state));
}

void test_tick_adds_correct_amount() {
    GameState state;
    state.generatorCounts[0] = 4;
    double before = state.qi;
    double rate = qiPerSecond(state);
    tick(state, 2.0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, before + rate * 2.0, state.qi);
}

void test_tick_ignores_non_positive_dt() {
    GameState state;
    state.generatorCounts[0] = 4;
    state.qi = 5.0;
    tick(state, 0.0);
    tick(state, -1.0);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, state.qi);
}

void test_cannot_breakthrough_below_threshold() {
    GameState state;
    state.qi = REALM_QI_THRESHOLD[1] - 1.0;
    TEST_ASSERT_FALSE(canBreakthrough(state));
    TEST_ASSERT_FALSE(attemptBreakthrough(state));
    TEST_ASSERT_EQUAL(0, state.realmIndex);
}

void test_breakthrough_spends_threshold_and_advances_realm() {
    GameState state;
    state.qi = REALM_QI_THRESHOLD[1] + 50.0;
    bool ok = attemptBreakthrough(state);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, state.realmIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 50.0, state.qi);
}

void test_cannot_breakthrough_past_final_realm() {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = 1e12;
    TEST_ASSERT_FALSE(canBreakthrough(state));
}

void test_purchase_generator_requires_unlock() {
    GameState state;
    state.qi = 1e9;
    TEST_ASSERT_FALSE(purchaseGenerator(state, 1)); // generator 1 needs realmIndex >= 1
    TEST_ASSERT_EQUAL(0, state.generatorCounts[1]);
}

void test_purchase_generator_requires_affordability() {
    GameState state;
    state.qi = 1.0;
    TEST_ASSERT_FALSE(purchaseGenerator(state, 0));
    TEST_ASSERT_EQUAL(0, state.generatorCounts[0]);
}

void test_purchase_generator_deducts_cost_and_increments_count() {
    GameState state;
    state.qi = 1000.0;
    double costBefore = costForGenerator(0, 0);
    bool ok = purchaseGenerator(state, 0);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, state.generatorCounts[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1000.0 - costBefore, state.qi);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_thresholds_increase_monotonically);
    RUN_TEST(test_cost_for_generator_grows_by_growth_rate);
    RUN_TEST(test_qi_per_second_ignores_locked_generators);
    RUN_TEST(test_qi_per_second_applies_realm_multiplier);
    RUN_TEST(test_tick_adds_correct_amount);
    RUN_TEST(test_tick_ignores_non_positive_dt);
    RUN_TEST(test_cannot_breakthrough_below_threshold);
    RUN_TEST(test_breakthrough_spends_threshold_and_advances_realm);
    RUN_TEST(test_cannot_breakthrough_past_final_realm);
    RUN_TEST(test_purchase_generator_requires_unlock);
    RUN_TEST(test_purchase_generator_requires_affordability);
    RUN_TEST(test_purchase_generator_deducts_cost_and_increments_count);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_economy
```
Expected: build FAILS — `economy.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/economy.h`**

```cpp
#pragma once
#include "realms.h"

struct GeneratorDef {
    const char* name;
    double baseCost;
    double growthRate;
    double baseQiPerSecond;
    int unlockRealmIndex; // purchasable once GameState.realmIndex >= this
};

constexpr int NUM_GENERATORS = 6;
extern const GeneratorDef GENERATORS[NUM_GENERATORS];

extern const char* const REALM_NAMES[NUM_REALMS];
extern const double REALM_QI_THRESHOLD[NUM_REALMS]; // Qi needed to break INTO realm i

struct GameState {
    double qi = 0.0;
    int generatorCounts[NUM_GENERATORS] = {0, 0, 0, 0, 0, 0};
    int realmIndex = 0;
};

double costForGenerator(int genIndex, int ownedCount);
double realmMultiplier(int realmIndex);
bool isGeneratorUnlocked(const GameState& state, int genIndex);
double qiPerSecond(const GameState& state);
void tick(GameState& state, double dtSeconds);
bool canBreakthrough(const GameState& state);

// If canBreakthrough(state): spends REALM_QI_THRESHOLD[state.realmIndex + 1] from
// state.qi, advances state.realmIndex, and returns true. Otherwise leaves state
// unchanged and returns false.
bool attemptBreakthrough(GameState& state);

// If unlocked and affordable: deducts costForGenerator(genIndex, current count) and
// increments the owned count, returning true. Otherwise leaves state unchanged and
// returns false.
bool purchaseGenerator(GameState& state, int genIndex);
```

- [ ] **Step 4: Write `lib/core/economy.cpp`**

```cpp
#include "economy.h"
#include <cmath>

const GeneratorDef GENERATORS[NUM_GENERATORS] = {
    {"Breathing Technique",     10.0,      1.12, 0.5,     0},
    {"Spirit Herb Garden",      100.0,     1.13, 4.0,     1},
    {"Meditation Formation",    1200.0,    1.13, 30.0,    2},
    {"Disciple Cultivators",    15000.0,   1.14, 220.0,   3},
    {"Spirit Vein Tap",         180000.0,  1.14, 1600.0,  4},
    {"Ancient Formation Array", 2200000.0, 1.15, 12000.0, 5},
};

const char* const REALM_NAMES[NUM_REALMS] = {
    "Mortal Body", "Qi Condensation", "Foundation Establishment",
    "Core Formation", "Nascent Soul", "Soul Transformation", "Void Refinement"
};

const double REALM_QI_THRESHOLD[NUM_REALMS] = {
    0.0, 100.0, 1200.0, 15000.0, 180000.0, 2200000.0, 27000000.0
};

double costForGenerator(int genIndex, int ownedCount) {
    const GeneratorDef& def = GENERATORS[genIndex];
    return def.baseCost * std::pow(def.growthRate, static_cast<double>(ownedCount));
}

double realmMultiplier(int realmIndex) {
    return std::pow(1.15, static_cast<double>(realmIndex));
}

bool isGeneratorUnlocked(const GameState& state, int genIndex) {
    return state.realmIndex >= GENERATORS[genIndex].unlockRealmIndex;
}

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

bool canBreakthrough(const GameState& state) {
    if (state.realmIndex >= NUM_REALMS - 1) return false;
    return state.qi >= REALM_QI_THRESHOLD[state.realmIndex + 1];
}

bool attemptBreakthrough(GameState& state) {
    if (!canBreakthrough(state)) return false;
    state.qi -= REALM_QI_THRESHOLD[state.realmIndex + 1];
    state.realmIndex += 1;
    return true;
}

bool purchaseGenerator(GameState& state, int genIndex) {
    if (!isGeneratorUnlocked(state, genIndex)) return false;
    double cost = costForGenerator(genIndex, state.generatorCounts[genIndex]);
    if (state.qi < cost) return false;
    state.qi -= cost;
    state.generatorCounts[genIndex] += 1;
    return true;
}
```

- [ ] **Step 5: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_economy
```
Expected: all 12 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/core/economy.h lib/core/economy.cpp test/test_economy/test_economy.cpp
git commit -m "Add idle-game economy: realms, generators, cost curve, breakthrough"
```

---

## Task 6: Save Serialization (`save`)

**Files:**
- Create: `lib/core/save.h`
- Create: `lib/core/save.cpp`
- Test: `test/test_save/test_save.cpp`

**Interfaces:**
- Consumes: `GameState`, `NUM_GENERATORS` (Task 5).
- Produces: `SAVE_MAGIC`, `SAVE_VERSION`, `SaveData{magic, version, qi, generatorCounts[NUM_GENERATORS], realmIndex, lastSaveEpochSeconds}`, `SAVE_BUFFER_SIZE`, `defaultSaveData()`, `toSaveData(const GameState&, int64_t epochSeconds)`, `toGameState(const SaveData&)`, `serializeSave(const SaveData&, uint8_t*, size_t) -> size_t`, `deserializeSave(const uint8_t*, size_t, SaveData&) -> bool` — consumed by Task 9 (NVS wrapper) and Task 10/11 (boot flow, autosave).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_save/test_save.cpp
#include <unity.h>
#include "save.h"

void setUp(void) {}
void tearDown(void) {}

void test_round_trip_preserves_data() {
    SaveData original;
    original.qi = 12345.678;
    original.generatorCounts[0] = 3;
    original.generatorCounts[5] = 7;
    original.realmIndex = 4;
    original.lastSaveEpochSeconds = 1700000000;

    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t written = serializeSave(original, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(SAVE_BUFFER_SIZE, written);

    SaveData restored;
    bool ok = deserializeSave(buffer, sizeof(buffer), restored);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(original.qi, restored.qi);
    TEST_ASSERT_EQUAL(3, restored.generatorCounts[0]);
    TEST_ASSERT_EQUAL(7, restored.generatorCounts[5]);
    TEST_ASSERT_EQUAL(4, restored.realmIndex);
    TEST_ASSERT_EQUAL_INT64(1700000000, restored.lastSaveEpochSeconds);
}

void test_corrupted_byte_fails_checksum() {
    SaveData original;
    original.qi = 99.0;
    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));
    buffer[0] ^= 0xFF;

    SaveData restored;
    TEST_ASSERT_FALSE(deserializeSave(buffer, sizeof(buffer), restored));
}

void test_truncated_buffer_fails() {
    SaveData original;
    uint8_t buffer[SAVE_BUFFER_SIZE];
    serializeSave(original, buffer, sizeof(buffer));

    SaveData restored;
    TEST_ASSERT_FALSE(deserializeSave(buffer, SAVE_BUFFER_SIZE - 1, restored));
}

void test_default_save_data_is_fresh_game() {
    SaveData d = defaultSaveData();
    TEST_ASSERT_EQUAL_DOUBLE(0.0, d.qi);
    TEST_ASSERT_EQUAL(0, d.realmIndex);
}

void test_game_state_round_trip_via_save_data() {
    GameState state;
    state.qi = 500.0;
    state.generatorCounts[2] = 9;
    state.realmIndex = 2;

    SaveData saved = toSaveData(state, 42);
    GameState restored = toGameState(saved);

    TEST_ASSERT_EQUAL_DOUBLE(state.qi, restored.qi);
    TEST_ASSERT_EQUAL(state.generatorCounts[2], restored.generatorCounts[2]);
    TEST_ASSERT_EQUAL(state.realmIndex, restored.realmIndex);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_data);
    RUN_TEST(test_corrupted_byte_fails_checksum);
    RUN_TEST(test_truncated_buffer_fails);
    RUN_TEST(test_default_save_data_is_fresh_game);
    RUN_TEST(test_game_state_round_trip_via_save_data);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_save
```
Expected: build FAILS — `save.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/save.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "economy.h"

constexpr uint32_t SAVE_MAGIC = 0x51494732; // 'QIG2'; bump on breaking format changes
constexpr uint16_t SAVE_VERSION = 1;

struct SaveData {
    uint32_t magic = SAVE_MAGIC;
    uint16_t version = SAVE_VERSION;
    double qi = 0.0;
    uint32_t generatorCounts[NUM_GENERATORS] = {};
    uint8_t realmIndex = 0;
    int64_t lastSaveEpochSeconds = 0;
};

constexpr size_t SAVE_BUFFER_SIZE = sizeof(SaveData) + sizeof(uint32_t); // payload + checksum

SaveData defaultSaveData();
SaveData toSaveData(const GameState& state, int64_t epochSeconds);
GameState toGameState(const SaveData& data);

// Serializes `data` plus a trailing FNV-1a checksum into `outBuffer` (must be at least
// SAVE_BUFFER_SIZE bytes). Returns bytes written, or 0 if the buffer is too small.
size_t serializeSave(const SaveData& data, uint8_t* outBuffer, size_t bufferLen);

// Validates length, checksum, magic, and version. On success fills `outData` and returns
// true; on any failure leaves `outData` untouched and returns false.
bool deserializeSave(const uint8_t* buffer, size_t bufferLen, SaveData& outData);
```

- [ ] **Step 4: Write `lib/core/save.cpp`**

```cpp
#include "save.h"
#include <cstring>

SaveData defaultSaveData() {
    return SaveData{};
}

SaveData toSaveData(const GameState& state, int64_t epochSeconds) {
    SaveData d;
    d.qi = state.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        d.generatorCounts[i] = static_cast<uint32_t>(state.generatorCounts[i]);
    }
    d.realmIndex = static_cast<uint8_t>(state.realmIndex);
    d.lastSaveEpochSeconds = epochSeconds;
    return d;
}

GameState toGameState(const SaveData& data) {
    GameState s;
    s.qi = data.qi;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        s.generatorCounts[i] = static_cast<int>(data.generatorCounts[i]);
    }
    s.realmIndex = data.realmIndex;
    return s;
}

namespace {
uint32_t fnv1aChecksum(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}
}

size_t serializeSave(const SaveData& data, uint8_t* outBuffer, size_t bufferLen) {
    if (bufferLen < SAVE_BUFFER_SIZE) return 0;
    std::memcpy(outBuffer, &data, sizeof(SaveData));
    uint32_t checksum = fnv1aChecksum(outBuffer, sizeof(SaveData));
    std::memcpy(outBuffer + sizeof(SaveData), &checksum, sizeof(uint32_t));
    return SAVE_BUFFER_SIZE;
}

bool deserializeSave(const uint8_t* buffer, size_t bufferLen, SaveData& outData) {
    if (bufferLen < SAVE_BUFFER_SIZE) return false;

    SaveData candidate;
    std::memcpy(&candidate, buffer, sizeof(SaveData));

    uint32_t storedChecksum;
    std::memcpy(&storedChecksum, buffer + sizeof(SaveData), sizeof(uint32_t));

    if (fnv1aChecksum(buffer, sizeof(SaveData)) != storedChecksum) return false;
    if (candidate.magic != SAVE_MAGIC) return false;
    if (candidate.version != SAVE_VERSION) return false;

    outData = candidate;
    return true;
}
```

- [ ] **Step 5: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_save
```
Expected: all 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/core/save.h lib/core/save.cpp test/test_save/test_save.cpp
git commit -m "Add versioned, checksummed save serialization"
```

---

## Task 7: Offline Earnings Calculation (`offline_earnings`)

**Files:**
- Create: `lib/core/offline_earnings.h`
- Create: `lib/core/offline_earnings.cpp`
- Test: `test/test_offline_earnings/test_offline_earnings.cpp`

**Interfaces:**
- Consumes: nothing beyond `<cstdint>`.
- Produces: `computeOfflineEarnings(int64_t rtcNowEpochSeconds, int64_t lastSaveEpochSeconds, double qiPerSecondAtSave, int64_t maxOfflineSeconds) -> double` — consumed by Task 10 (RTC + offline-earnings integration).

- [ ] **Step 1: Write the failing tests**

```cpp
// test/test_offline_earnings/test_offline_earnings.cpp
#include <unity.h>
#include "offline_earnings.h"

void setUp(void) {}
void tearDown(void) {}

void test_normal_elapsed_time_grants_expected_qi() {
    double earned = computeOfflineEarnings(1000, 900, 2.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, earned);
}

void test_clock_moved_backward_grants_zero() {
    double earned = computeOfflineEarnings(500, 900, 2.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, earned);
}

void test_elapsed_time_is_capped() {
    double earned = computeOfflineEarnings(100000, 0, 1.0, 3600);
    TEST_ASSERT_EQUAL_DOUBLE(3600.0, earned);
}

void test_zero_elapsed_grants_zero() {
    double earned = computeOfflineEarnings(1000, 1000, 5.0, 86400);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, earned);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_elapsed_time_grants_expected_qi);
    RUN_TEST(test_clock_moved_backward_grants_zero);
    RUN_TEST(test_elapsed_time_is_capped);
    RUN_TEST(test_zero_elapsed_grants_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_offline_earnings
```
Expected: build FAILS — `offline_earnings.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/offline_earnings.h`**

```cpp
#pragma once
#include <cstdint>

// Qi earned while the device was off, based on elapsed RTC time. Negative elapsed time
// (clock moved backward / RTC unset) clamps to zero; elapsed time is capped at
// maxOfflineSeconds to avoid runaway first-boot numbers.
double computeOfflineEarnings(int64_t rtcNowEpochSeconds, int64_t lastSaveEpochSeconds,
                               double qiPerSecondAtSave, int64_t maxOfflineSeconds);
```

- [ ] **Step 4: Write `lib/core/offline_earnings.cpp`**

```cpp
#include "offline_earnings.h"

double computeOfflineEarnings(int64_t rtcNowEpochSeconds, int64_t lastSaveEpochSeconds,
                               double qiPerSecondAtSave, int64_t maxOfflineSeconds) {
    int64_t elapsed = rtcNowEpochSeconds - lastSaveEpochSeconds;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > maxOfflineSeconds) elapsed = maxOfflineSeconds;
    return static_cast<double>(elapsed) * qiPerSecondAtSave;
}
```

- [ ] **Step 5: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_offline_earnings
```
Expected: all 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/core/offline_earnings.h lib/core/offline_earnings.cpp test/test_offline_earnings/test_offline_earnings.cpp
git commit -m "Add capped offline-earnings calculation"
```

---

## Task 8: On-Device Rendering Spike

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Mesh`, `makeIcosahedron()`, `growForRealm()` (Task 3); `Framebuffer`, `RasterParams`, `rasterizeMesh()` (Task 4); `Mat4`, `Vec3` (Task 2).
- Produces: a validated offscreen-render-then-scale-blit approach and a measured (viewport size, FPS) baseline that Task 11 builds the full game loop on top of.

This is the design spec's flagged highest-risk milestone: confirm the rasterizer actually produces a recognizable, reasonably fast rotating shape on real hardware before building the rest of the game on top of it.

- [ ] **Step 1: Replace `src/main.cpp`'s `setup()`/`loop()` with the rendering spike**

```cpp
#include <M5Unified.h>
#include "math3d.h"
#include "mesh.h"
#include "framebuffer.h"
#include "rasterizer.h"

namespace {
constexpr int kRenderSize = 320; // offscreen 3D viewport, square; tune based on measured FPS below

Mesh gBaseMesh;
RealmVisual gRealmVisual;
Framebuffer* gFramebuffer = nullptr;
M5Canvas* gCanvas = nullptr;
float gRotation = 0.0f;
uint32_t gFrameCount = 0;
uint32_t gFpsWindowStartMs = 0;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Rendering spike starting");

    M5.Display.fillScreen(TFT_BLACK);

    gBaseMesh = makeIcosahedron();
    gRealmVisual = growForRealm(gBaseMesh, 0);

    gFramebuffer = new Framebuffer(kRenderSize, kRenderSize);
    gCanvas = new M5Canvas(&M5.Display);
    gCanvas->createSprite(kRenderSize, kRenderSize);

    gFpsWindowStartMs = millis();
    Serial.println("[BOOT] Rendering spike ready");
}

void loop() {
    M5.update();

    RasterParams params;
    params.transform = Mat4::rotationY(gRotation);
    params.cameraDistance = 4.0f;
    params.focalLength = 1.6f;
    params.lightDir = Vec3{0.4f, 0.6f, -1.0f}.normalized();
    params.viewDir = Vec3{0, 0, -1};
    params.baseColor = gRealmVisual.baseColor;
    params.rimColor = gRealmVisual.rimColor;

    gFramebuffer->clear(RGB{15, 15, 25});
    rasterizeMesh(gRealmVisual.mesh, params, *gFramebuffer);

    for (int y = 0; y < kRenderSize; ++y) {
        for (int x = 0; x < kRenderSize; ++x) {
            RGB p = gFramebuffer->getPixel(x, y);
            gCanvas->drawPixel(x, y, gCanvas->color565(p.r, p.g, p.b));
        }
    }
    gCanvas->pushSprite((M5.Display.width() - kRenderSize) / 2,
                         (M5.Display.height() - kRenderSize) / 2);

    gRotation += 0.02f;
    gFrameCount++;

    uint32_t now = millis();
    if (now - gFpsWindowStartMs >= 1000) {
        Serial.printf("[FPS] %u frames/sec\n", gFrameCount);
        gFrameCount = 0;
        gFpsWindowStartMs = now;
    }
}
```

- [ ] **Step 2: Build and flash**

```bash
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```
Expected: `[SUCCESS]`.

- [ ] **Step 3: Measure FPS over serial**

```bash
timeout 8 python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200 | tee /tmp/task8_render_log.txt
grep "\[FPS\]" /tmp/task8_render_log.txt
```
Expected: one or more `[FPS] N frames/sec` lines. If `N` is low (single digits), reduce `kRenderSize` (try `240`, then `160`), reflash, and re-measure until it's comfortably in the target 20-30 FPS range from the spec — this is the resolution/FPS trade-off the design spec flagged as an open risk; whatever value is chosen here becomes the baseline for Task 11.

If instead there's clear headroom above the target (e.g. comfortably above 30 FPS at the chosen `kRenderSize`), try swapping `gBaseMesh = makeIcosahedron();` for `gBaseMesh = subdivide(makeIcosahedron());` (80 faces instead of 20, for a more faceted crystal look) and re-measure. Keep whichever version (plain or subdivided) still hits the target FPS — this is the triangle-budget-ceiling decision the design spec's "Open Risks" section flagged as pending profiling, resolved here with real measurements rather than assumed.

- [ ] **Step 4: Ask the user to visually confirm**

Ask the user to look at the Tab5 screen and confirm they see a rotating, pale/white, faceted crystal-like shape centered on a dark background. If instead the screen looks empty, flat/uniformly shaded, or "inside out," the likely cause is the icosahedron's face-winding order not matching the rasterizer's backface-cull convention: in `lib/core/mesh.cpp`, swap the last two indices of every face tuple in `makeIcosahedron()` (e.g. `{a,b,c}` becomes `{a,c,b}`), rebuild, reflash, and recheck.

- [ ] **Step 5: Record the chosen viewport size and commit**

Update `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`'s "Open Risks" section to note the measured `kRenderSize`, the achieved FPS, and whether the mesh is subdivided (resolving both open risks with real data), then:

```bash
git add src/main.cpp docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md
git commit -m "Wire rasterizer into a rendering spike; measure and lock in viewport size"
```

---

## Task 9: NVS Persistence Wrapper

**Files:**
- Create: `src/nvs_store.h`
- Create: `src/nvs_store.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `SaveData`, `SAVE_BUFFER_SIZE`, `serializeSave()`, `deserializeSave()`, `defaultSaveData()` (Task 6).
- Produces: `nvsLoadSave() -> SaveData`, `nvsWriteSave(const SaveData&)` — consumed by Task 10 (offline-earnings boot flow) and Task 11 (autosave).

This module is Arduino-only (uses `Preferences.h`), so it has no native test; correctness is confirmed on-device.

- [ ] **Step 1: Write `src/nvs_store.h`**

```cpp
#pragma once
#include "save.h"

// Loads the save from NVS. Returns defaultSaveData() if nothing is stored yet, or if
// what's stored fails deserializeSave()'s checksum/magic/version validation.
SaveData nvsLoadSave();

void nvsWriteSave(const SaveData& data);
```

- [ ] **Step 2: Write `src/nvs_store.cpp`**

```cpp
#include "nvs_store.h"
#include <Preferences.h>

namespace {
const char* kNamespace = "xianxia";
const char* kKey = "save";
}

SaveData nvsLoadSave() {
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/true);
    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t got = prefs.getBytes(kKey, buffer, sizeof(buffer));
    prefs.end();

    SaveData data;
    if (got == SAVE_BUFFER_SIZE && deserializeSave(buffer, got, data)) {
        return data;
    }
    return defaultSaveData();
}

void nvsWriteSave(const SaveData& data) {
    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t written = serializeSave(data, buffer, sizeof(buffer));

    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putBytes(kKey, buffer, written);
    prefs.end();
}
```

- [ ] **Step 3: Add a temporary round-trip diagnostic to `src/main.cpp`'s `setup()`**

Add this right after `Serial.begin(115200); delay(200);` (before the rendering spike setup from Task 8):

```cpp
    {
        SaveData probe = defaultSaveData();
        probe.qi = 777.0;
        probe.realmIndex = 2;
        nvsWriteSave(probe);

        SaveData readBack = nvsLoadSave();
        bool ok = (readBack.qi == 777.0) && (readBack.realmIndex == 2);
        Serial.println(ok ? "[NVS] round-trip PASS" : "[NVS] round-trip FAIL");
    }
```

Also add `#include "nvs_store.h"` near the top of `src/main.cpp`.

- [ ] **Step 4: Build, flash, and verify the round-trip over serial**

```bash
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
timeout 8 python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200 | tee /tmp/task9_nvs_log.txt
grep -q "round-trip PASS" /tmp/task9_nvs_log.txt && echo "PASS" || echo "FAIL"
```
Expected: `PASS`.

- [ ] **Step 5: Verify persistence survives a power cycle**

Ask the user to power the device off (double-press the power button per the datasheet) and back on. Then:

```bash
timeout 8 python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200 | tee /tmp/task9_nvs_reboot_log.txt
grep -q "round-trip PASS" /tmp/task9_nvs_reboot_log.txt && echo "PASS" || echo "FAIL"
```
Expected: still `PASS` — the probe write from before the reboot, followed by this boot's own write/read of the same values, both succeed, confirming NVS survives power cycles as expected.

- [ ] **Step 6: Remove the temporary diagnostic block and commit**

Delete the `{ ... }` probe block added in Step 3 (its job — proving NVS round-trips and survives power cycles — is done; Task 10/11 wire real save/load into the boot flow).

```bash
git add src/nvs_store.h src/nvs_store.cpp src/main.cpp
git commit -m "Add NVS-backed save persistence, verified round-trip across power cycles"
```

---

## Task 10: RTC Wrapper & Offline-Earnings Integration

**Files:**
- Create: `src/rtc_store.h`
- Create: `src/rtc_store.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `computeOfflineEarnings()` (Task 7); `SaveData`, `nvsLoadSave()`, `nvsWriteSave()` (Task 9); `GameState`, `toGameState()`, `qiPerSecond()` (Task 5/6).
- Produces: `readRtcEpochSeconds() -> int64_t`, `writeRtcFromEpochSeconds(int64_t)` — consumed by Task 11 (autosave stamps `lastSaveEpochSeconds` with this on every save).

- [ ] **Step 1: Confirm the exact M5Unified RTC API for Tab5**

The RX8130CE RTC wrapper's exact struct/method names in M5Unified need confirming against the version fetched by Task 1 before writing real code against it:

```bash
grep -rn "getDateTime\|setDateTime\|rtc_date_t\|rtc_time_t\|RX8130\|class.*Rtc" \
  .pio/libdeps/esp32p4_pioarduino/M5Unified/src/ 2>/dev/null | head -50
```

Read whichever header this turns up in full. If the struct/method names match the template in Step 3 below (`m5::rtc_datetime_t` with `.date.year/.month/.date` and `.time.hours/.minutes/.seconds`, accessed via `M5.Rtc.getDateTime(&dt)` / `M5.Rtc.setDateTime(dt)`), use it as-is. If the names differ, adjust Step 3's field accesses to match what the grep actually found — the surrounding structure (convert to/from `struct tm`, call `timegm`/`gmtime_r`) stays the same either way.

- [ ] **Step 2: Write `src/rtc_store.h`**

```cpp
#pragma once
#include <cstdint>

// Returns the Tab5's RX8130CE RTC time as Unix epoch seconds, or 0 if it can't be read.
// Only the *elapsed* difference between two calls to this function matters for offline
// earnings — the RTC's absolute wall-clock accuracy is irrelevant (no NTP sync in v1).
int64_t readRtcEpochSeconds();

void writeRtcFromEpochSeconds(int64_t epoch);
```

- [ ] **Step 3: Write `src/rtc_store.cpp`**

```cpp
#include "rtc_store.h"
#include <M5Unified.h>
#include <ctime>

// Field/method names below match M5Unified's common RTC wrapper shape. If Step 1's grep
// found different names for the Tab5/RX8130CE driver, update the accesses below to match.

int64_t readRtcEpochSeconds() {
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt)) return 0;

    struct tm timeinfo = {};
    timeinfo.tm_year = dt.date.year - 1900;
    timeinfo.tm_mon  = dt.date.month - 1;
    timeinfo.tm_mday = dt.date.date;
    timeinfo.tm_hour = dt.time.hours;
    timeinfo.tm_min  = dt.time.minutes;
    timeinfo.tm_sec  = dt.time.seconds;

    return static_cast<int64_t>(timegm(&timeinfo));
}

void writeRtcFromEpochSeconds(int64_t epoch) {
    time_t t = static_cast<time_t>(epoch);
    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);

    m5::rtc_datetime_t dt;
    dt.date.year  = timeinfo.tm_year + 1900;
    dt.date.month = timeinfo.tm_mon + 1;
    dt.date.date  = timeinfo.tm_mday;
    dt.time.hours   = timeinfo.tm_hour;
    dt.time.minutes = timeinfo.tm_min;
    dt.time.seconds = timeinfo.tm_sec;

    M5.Rtc.setDateTime(dt);
}
```

Note: `writeRtcFromEpochSeconds` is provided but not required for v1's offline-earnings correctness — every save stamps `lastSaveEpochSeconds` with whatever `readRtcEpochSeconds()` currently returns, so only the delta between two of the device's own readings ever matters, regardless of whether the RTC's absolute time was ever set correctly.

- [ ] **Step 4: Wire the offline-earnings boot flow into `src/main.cpp`**

Add `#include "rtc_store.h"` and `#include "offline_earnings.h"` and `#include "economy.h"` near the top. By this point `setup()` contains Task 8's rendering-spike code (Task 9's temporary probe block was already removed in Task 9 Step 6). Insert the following right after the `Serial.println("[BOOT] Rendering spike starting");` line and before the mesh/framebuffer/canvas setup that follows it:

```cpp
    SaveData save = nvsLoadSave();
    int64_t nowEpoch = readRtcEpochSeconds();

    if (save.lastSaveEpochSeconds != 0) { // 0 means "no prior save" (see defaultSaveData())
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState);
        double offlineQi = computeOfflineEarnings(nowEpoch, save.lastSaveEpochSeconds,
                                                    rateAtSave, /*maxOfflineSeconds=*/24 * 3600);
        save.qi += offlineQi;
        Serial.printf("[OFFLINE] Away for up to 24h capped, gained %.2f Qi\n", offlineQi);
    } else {
        Serial.println("[OFFLINE] First-ever boot, no offline bonus");
    }

    save.lastSaveEpochSeconds = nowEpoch;
    nvsWriteSave(save);
```

This depends on Task 11's `GameState gGameState` not existing yet — for this task, keep it self-contained (local variables as shown) and print the result to serial; Task 11 folds this same logic into the real game-state initialization instead of a standalone local block.

- [ ] **Step 5: Build, flash, and do a short real-time check**

```bash
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```

Then wait roughly 30 real seconds with the device powered and running (this *is* the test — offline earnings need real elapsed time to observe):

```bash
sleep 30
```

Ask the user to reboot the device (reset button or power-cycle), then:

```bash
timeout 8 python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200 | tee /tmp/task10_offline_log.txt
grep "\[OFFLINE\]" /tmp/task10_offline_log.txt
```
Expected: an `[OFFLINE] Away for up to 24h capped, gained N Qi` line where `N` is roughly consistent with ~30 seconds' worth of Qi at whatever rate the probe state implies (with the default fresh `GameState`, `qiPerSecond` is 0 unless generators are owned — for this check it's enough to confirm the line prints a non-negative number and doesn't crash; a nonzero value will only appear once Task 11 gives the player actual generators).

- [ ] **Step 6: Commit**

```bash
git add src/rtc_store.h src/rtc_store.cpp src/main.cpp
git commit -m "Add RTC epoch read/write and RTC-based offline-earnings boot flow"
```

---

## Task 11: HUD, Touch, and the Full Game Loop

**Files:**
- Create: `lib/core/hittest.h`
- Test: `test/test_hittest/test_hittest.cpp`
- Create: `src/ui.h`
- Create: `src/ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2-10 (`Mesh`/`growForRealm`, `Framebuffer`/`rasterizeMesh`, `GameState`/`tick`/`purchaseGenerator`/`attemptBreakthrough`/`GENERATORS`/`REALM_NAMES`, `SaveData`/`nvsLoadSave`/`nvsWriteSave`, `readRtcEpochSeconds`).
- Produces: `Rect{x,y,w,h}`, `rectContains(const Rect&, int, int)`; `drawHud(M5GFX& display, const GameState&)`, `hitTestHud(int touchX, int touchY) -> int` (returns a button id or -1) — the final integration; nothing downstream depends on this task's output within this plan (Task 12 is documentation only).

- [ ] **Step 1: Write the failing hit-test tests**

```cpp
// test/test_hittest/test_hittest.cpp
#include <unity.h>
#include "hittest.h"

void setUp(void) {}
void tearDown(void) {}

void test_point_inside_rect() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_TRUE(rectContains(r, 20, 15));
}

void test_point_outside_rect() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_FALSE(rectContains(r, 5, 15));
    TEST_ASSERT_FALSE(rectContains(r, 20, 35));
}

void test_left_top_edge_is_inclusive() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_TRUE(rectContains(r, 10, 10));
}

void test_right_bottom_edge_is_exclusive() {
    Rect r{10, 10, 50, 20};
    TEST_ASSERT_FALSE(rectContains(r, 60, 10));
    TEST_ASSERT_FALSE(rectContains(r, 10, 30));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_point_inside_rect);
    RUN_TEST(test_point_outside_rect);
    RUN_TEST(test_left_top_edge_is_inclusive);
    RUN_TEST(test_right_bottom_edge_is_exclusive);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
python3 -m platformio test -e native -f test_hittest
```
Expected: build FAILS — `hittest.h` doesn't exist yet.

- [ ] **Step 3: Write `lib/core/hittest.h`**

```cpp
#pragma once

struct Rect { int x, y, w, h; };

// Half-open bounds [x, x+w) x [y, y+h) — a point on the right/bottom edge is NOT
// contained, so adjacent buttons sharing an edge never both claim the same touch.
inline bool rectContains(const Rect& r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
python3 -m platformio test -e native -f test_hittest
```
Expected: all 4 tests pass. Then run the full native suite to confirm every module still passes together:

```bash
python3 -m platformio test -e native
```
Expected: all suites (`test_smoke`, `test_math3d`, `test_mesh`, `test_rasterizer`, `test_economy`, `test_save`, `test_offline_earnings`, `test_hittest`) pass.

- [ ] **Step 5: Confirm the M5Unified touch API**

```bash
grep -rn "class.*Touch\|wasClicked\|getDetail\|getCount" \
  .pio/libdeps/esp32p4_pioarduino/M5Unified/src/ 2>/dev/null | head -30
```
Read the matching header to confirm the exact call shape. The template in Step 6 assumes `auto t = M5.Touch.getDetail(); if (t.wasClicked()) { int x = t.x; int y = t.y; }` — adjust field/method names in Step 6 if the grep shows otherwise.

- [ ] **Step 6: Write `src/ui.h`**

```cpp
#pragma once
#include <M5Unified.h>
#include "economy.h"
#include "hittest.h"

// Button ids returned by hitTestHud(); -1 means "no button at that point."
enum HudButton {
    HUD_BUTTON_NONE = -1,
    HUD_BUTTON_BREAKTHROUGH = 100,
    // Generator buy buttons use HUD_BUTTON_GENERATOR_BASE + genIndex (0..NUM_GENERATORS-1).
    HUD_BUTTON_GENERATOR_BASE = 0,
};

void drawHud(M5GFX& display, const GameState& state);
int hitTestHud(int touchX, int touchY);
```

- [ ] **Step 7: Write `src/ui.cpp`**

```cpp
#include "ui.h"

namespace {
constexpr int kPanelX = 340; // just right of the 320px 3D viewport
constexpr int kGeneratorRowHeight = 44;
constexpr int kGeneratorRowY0 = 140;
constexpr int kButtonW = 900;

Rect generatorRowRect(int genIndex) {
    return Rect{kPanelX, kGeneratorRowY0 + genIndex * kGeneratorRowHeight, kButtonW, kGeneratorRowHeight - 4};
}

Rect breakthroughRect() {
    return Rect{kPanelX, kGeneratorRowY0 + NUM_GENERATORS * kGeneratorRowHeight + 20, kButtonW, 60};
}
}

void drawHud(M5GFX& display, const GameState& state) {
    display.fillRect(kPanelX, 0, display.width() - kPanelX, display.height(), TFT_BLACK);

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(kPanelX, 20);
    display.printf("Realm: %s", REALM_NAMES[state.realmIndex]);
    display.setCursor(kPanelX, 50);
    display.printf("Qi: %.1f", state.qi);
    display.setCursor(kPanelX, 80);
    display.printf("Qi/sec: %.2f", qiPerSecond(state));

    for (int i = 0; i < NUM_GENERATORS; ++i) {
        Rect r = generatorRowRect(i);
        bool unlocked = isGeneratorUnlocked(state, i);
        uint16_t bg = unlocked ? TFT_DARKGREY : TFT_BLACK;
        display.fillRect(r.x, r.y, r.w, r.h, bg);
        display.setCursor(r.x + 8, r.y + 10);
        if (unlocked) {
            display.printf("%s x%d - cost %.0f", GENERATORS[i].name, state.generatorCounts[i],
                            costForGenerator(i, state.generatorCounts[i]));
        } else {
            display.printf("%s (locked)", GENERATORS[i].name);
        }
    }

    Rect bt = breakthroughRect();
    bool canBt = canBreakthrough(state);
    display.fillRect(bt.x, bt.y, bt.w, bt.h, canBt ? TFT_ORANGE : TFT_DARKGREY);
    display.setCursor(bt.x + 8, bt.y + 20);
    if (state.realmIndex < NUM_REALMS - 1) {
        display.printf("Attempt Breakthrough (%.0f Qi)", REALM_QI_THRESHOLD[state.realmIndex + 1]);
    } else {
        display.print("Max Realm Reached");
    }
}

int hitTestHud(int touchX, int touchY) {
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        if (rectContains(generatorRowRect(i), touchX, touchY)) {
            return HUD_BUTTON_GENERATOR_BASE + i;
        }
    }
    if (rectContains(breakthroughRect(), touchX, touchY)) {
        return HUD_BUTTON_BREAKTHROUGH;
    }
    return HUD_BUTTON_NONE;
}
```

- [ ] **Step 8: Rewrite `src/main.cpp` to wire the full game loop**

```cpp
#include <M5Unified.h>
#include "math3d.h"
#include "mesh.h"
#include "framebuffer.h"
#include "rasterizer.h"
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"

namespace {
constexpr int kRenderSize = 320; // use the value locked in during Task 8
constexpr uint32_t kTickIntervalMs = 50;   // 20Hz economy tick
constexpr uint32_t kAutosaveIntervalMs = 15000;

GameState gState;
Mesh gBaseMesh;
RealmVisual gRealmVisual;
Framebuffer* gFramebuffer = nullptr;
M5Canvas* gCanvas = nullptr;
float gRotation = 0.0f;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;

void refreshRealmVisual() {
    gRealmVisual = growForRealm(gBaseMesh, gState.realmIndex);
}

void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch));
}
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Xianxia idle game starting");

    M5.Display.fillScreen(TFT_BLACK);

    SaveData save = nvsLoadSave();
    int64_t nowEpoch = readRtcEpochSeconds();

    if (save.lastSaveEpochSeconds != 0) {
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState);
        double offlineQi = computeOfflineEarnings(nowEpoch, save.lastSaveEpochSeconds,
                                                    rateAtSave, 24 * 3600);
        save.qi += offlineQi;
        Serial.printf("[OFFLINE] Gained %.2f Qi while away\n", offlineQi);
    } else {
        Serial.println("[OFFLINE] First-ever boot, no offline bonus");
    }

    gState = toGameState(save);
    gBaseMesh = makeIcosahedron();
    refreshRealmVisual();

    gFramebuffer = new Framebuffer(kRenderSize, kRenderSize);
    gCanvas = new M5Canvas(&M5.Display);
    gCanvas->createSprite(kRenderSize, kRenderSize);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
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
    }

    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        bool stateChanged = false;
        if (button == HUD_BUTTON_BREAKTHROUGH) {
            if (attemptBreakthrough(gState)) {
                refreshRealmVisual();
                stateChanged = true;
            }
        } else if (button >= HUD_BUTTON_GENERATOR_BASE && button < HUD_BUTTON_GENERATOR_BASE + NUM_GENERATORS) {
            if (purchaseGenerator(gState, button - HUD_BUTTON_GENERATOR_BASE)) {
                stateChanged = true;
            }
        }
        if (stateChanged) saveNow();
    }

    if (now - gLastAutosaveMs >= kAutosaveIntervalMs) {
        saveNow();
        gLastAutosaveMs = now;
    }

    RasterParams params;
    params.transform = Mat4::rotationY(gRotation);
    params.cameraDistance = 4.0f;
    params.focalLength = 1.6f;
    params.lightDir = Vec3{0.4f, 0.6f, -1.0f}.normalized();
    params.viewDir = Vec3{0, 0, -1};
    params.baseColor = gRealmVisual.baseColor;
    params.rimColor = gRealmVisual.rimColor;

    gFramebuffer->clear(RGB{15, 15, 25});
    rasterizeMesh(gRealmVisual.mesh, params, *gFramebuffer);

    for (int y = 0; y < kRenderSize; ++y) {
        for (int x = 0; x < kRenderSize; ++x) {
            RGB p = gFramebuffer->getPixel(x, y);
            gCanvas->drawPixel(x, y, gCanvas->color565(p.r, p.g, p.b));
        }
    }
    gCanvas->pushSprite(0, 0);

    drawHud(M5.Display, gState);

    gRotation += 0.02f;
}
```

- [ ] **Step 9: Build and flash**

```bash
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```
Expected: `[SUCCESS]`.

- [ ] **Step 10: Full manual gameplay verification**

Ask the user to check, on the physical device:
- The rotating crystal renders on the left, the HUD (realm name, Qi, Qi/sec, generator rows, breakthrough button) renders on the right.
- Tapping the "Breathing Technique" row buys it (Qi decreases by its cost, the row's owned count increases, Qi/sec increases).
- Qi visibly climbs on its own over time (idle accrual).
- Once Qi crosses the Qi Condensation threshold, the breakthrough button visually changes (e.g. turns orange) and tapping it advances the realm, changes the crystal's color/shape, and unlocks the next generator row.
- Power-cycling the device and turning it back on restores the same Qi/realm/generator state (confirms the autosave + boot-load path from Tasks 9-10 is fully wired through the real game state, not just the earlier diagnostic probe).

- [ ] **Step 11: Commit**

```bash
git add lib/core/hittest.h test/test_hittest/test_hittest.cpp src/ui.h src/ui.cpp src/main.cpp
git commit -m "Wire HUD, touch input, and full game loop into the rendering/economy/save pipeline"
```

---

## Task 12: README Update

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: the final `kRenderSize`/FPS baseline recorded in Task 8, the realm/generator names from Task 5, the build/flash commands used throughout.
- Produces: nothing consumed elsewhere — this is the last task.

- [ ] **Step 1: Rewrite `README.md`**

Keep the existing hardware description paragraph at the top, then append a project section. Use the actual `kRenderSize`/FPS values recorded during Task 8 (replace the placeholders below with the real measured numbers):

```markdown
# tab5
The Tab5 is a highly expandable, portable smart IoT terminal designed for developers, integrating a dual-core architecture and rich hardware resources. It is built around the ESP32-P4 SoC based on the RISC-V architecture, featuring 16MB Flash and 32MB PSRAM for high-performance application development.

Full hardware datasheet: `docs/Tab5.pdf`.

## Xianxia Idle Game

A cultivation-themed idle game running natively on the Tab5. Cultivate **Qi**, buy
passive **Cultivation Methods**, and tap **Attempt Breakthrough** to advance through
seven **Cultivation Realms** (Mortal Body through Void Refinement). The centerpiece is
a procedurally-grown, rotating low-poly crystal rendered with a custom software 3D
rasterizer — the ESP32-P4 has no GPU, so the crystal is rendered into a <RENDER_SIZE>x<RENDER_SIZE>
offscreen buffer at roughly <MEASURED_FPS> FPS and scaled onto the full 1280x720 panel,
with a hand-drawn 2D HUD alongside it. Progress persists across power cycles (NVS), and
the Tab5's battery-backed RTC grants offline earnings on boot.

Design spec: `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-xianxia-idle-game.md`

### Building & Flashing

Requires [PlatformIO](https://platformio.org/):

```bash
python3 -m pip install --user platformio
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```

Serial console (115200 baud):
```bash
python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200
```

### Running Tests

Game logic (3D math, procedural mesh growth, the software rasterizer, the idle-game
economy, save serialization, offline-earnings math, HUD hit-testing) is hardware-agnostic
C++ under `lib/core/`, unit-tested on the host machine — no device required:

```bash
python3 -m platformio test -e native
```

### Project Layout

- `lib/core/` — hardware-agnostic game logic, unit-tested via the `native` PlatformIO environment.
- `src/` — Arduino/M5Unified/M5GFX glue: display, touch, RTC, NVS persistence, and `setup()`/`loop()`.
- `test/` — one PlatformIO test suite per `lib/core/` module.
- `docs/superpowers/specs/`, `docs/superpowers/plans/` — design spec and implementation plan for this feature.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Document the xianxia idle game: build/flash/test instructions and layout"
```
