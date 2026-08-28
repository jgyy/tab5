# MapleStory Skills & Graphics Revamp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the confirmed-unresponsive brightness/volume touch controls, and add a fully-automatic realm-gated character skill system plus a graphics pass (combat FX, character animation, monster variety, environmental dressing) to the existing MapleStory-style zone, without touching the cultivation economy, save format, or existing terrain/combat math.

**Architecture:** Two new pure, unit-tested `lib/core` modules (`skills`, `fx`) supply deterministic game-logic math with zero hardware dependency, following this project's existing split between testable `lib/core` and hardware-glue `src/`. `zone_state.cpp` gains a small integration point in its `Fighting` branch to fire skills on top of the untouched `zone_combat` autoattack. All new drawing (skill FX, character/monster/environment upgrades) lives in `src/zone_view.cpp`, reusing the existing trigger-function pattern (`triggerAttackFlash`-style, no coordinates passed — positions are static during `Fighting` and re-derived at render time).

**Tech Stack:** C++ (Arduino framework), M5Unified/M5GFX (M5Canvas offscreen sprites, procedural drawing only — no image/audio assets), PlatformIO (`native` env for Unity-based host tests, `esp32p4_pioarduino` env for the real build), Unity test framework.

**Spec:** `docs/superpowers/specs/2026-08-28-maplestory-skills-and-graphics-design.md`

## Global Constraints

- No manual/touch control of anything — this plan *removes* the app's last touch controls and adds no new ones. Every new mechanic (skills, FX) is fully automatic.
- No changes to `zone_combat.h/.cpp`'s autoattack resolution, `zone_map.{h,cpp}`'s terrain generation, the jump mechanic (`JumpArc`/`jumpArcPosition`/`makeJumpArc`), or patrol motion (`patrolPositionX`/`patrolRangeForPlatform`).
- No changes to the cultivation economy (`economy.h/.cpp`), save format (`save.h/.cpp`, `SAVE_VERSION` stays 2), or offline-earnings math. `SaveData.brightness`/`volume` keep loading/saving exactly as today.
- New math with a defined right answer (skill unlock/cycling, shake/rise/parallax curves) goes in `lib/core` and is unit-tested via `pio test -e native`. All drawing stays in `src/`, verified only by `pio run -e esp32p4_pioarduino` (build-only) — this project has no way to validate on-device visuals/FPS/touch from this environment.
- Per-frame FX collections stay bounded by small fixed constants (8 damage numbers, ~6 parallax elements, ~8 ground tufts, 1 active skill FX) so worst-case draw cost stays predictable.
- No true alpha blending — "fade" effects use a hard cutoff after a fixed duration, not a color ramp (M5Canvas has no real per-pixel alpha).

---

### Task 1: Remove brightness/volume touch controls

**Files:**
- Modify: `src/ui.h`
- Modify: `src/ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone);` (drops the `uint8_t brightness, uint8_t volume` parameters) — every later task that calls `drawHud` (none do; only `main.cpp` calls it) must use this new signature.

This task has no native unit tests to write first — `ui.cpp`/`main.cpp` are hardware glue with no existing test harness (same split this project already uses; see `README.md`'s Testing section). Verification is: the full native suite still passes unmodified, and the real build still compiles.

- [ ] **Step 1: Remove the `HudButton` enum and touch-handling declarations from `ui.h`**

Delete this whole block (currently lines 8-17):

```cpp
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
```

Change the `drawHud` declaration and its comment from:

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

// Hit-tests a touch point against the brightness/volume rows - the only tappable elements left.
int hitTestHud(int touchX, int touchY);

// Briefly highlights the just-tapped brightness/volume quadrant (pass one of the
// HUD_BUTTON_BRIGHTNESS_*/HUD_BUTTON_VOLUME_* ids) on the next drawHud() call or two. Gives
// immediate visual proof a tap was received and routed to the right control, independent of
// whether the brightness/volume value itself visibly/audibly took effect - useful for telling
// apart "touch never registered" from "touch registered but the hardware effect didn't" on
// real hardware.
void flashSettingsButton(int button);
```

to:

```cpp
// Draws the full HUD (header bar + stats panel) into offscreen sprites, then pushes each to
// `display` in one blit apiece. Keeps every redraw atomic on the physical screen - drawing
// primitives directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker. The panel shows: breakthrough progress, player HP, enemy HP (empty
// when not currently fighting), and monsters-defeated progress - all read-only, driven
// entirely by automation. There is nothing left to tap anywhere on screen.
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone);
```

- [ ] **Step 2: Remove the settings row from `ui.cpp`'s layout and drawing**

Change `kPanelHeight` from:

```cpp
constexpr int kSettingsRowHeight = 48; // unchanged from the old standalone rows - these are the
                                        // two controls under active hardware-response investigation,
                                        // so their tap-target height is deliberately not shrunk.
constexpr int kPanelHeight = kPanelTopPad
    + kBreakthroughBarHeight + kSectionGap
    + kHpBarHeight + kSectionGap
    + kRouteBarHeight + kSectionGap
    + kSettingsRowHeight + kPanelTopPad;
```

to:

```cpp
constexpr int kPanelHeight = kPanelTopPad
    + kBreakthroughBarHeight + kSectionGap
    + kHpBarHeight + kSectionGap
    + kRouteBarHeight + kPanelTopPad;
```

Remove `settingsY` from the `Layout` struct and from `computeLayout()`:

```cpp
struct Layout {
    int screenW = 0;
    int screenH = 0;
    int panelY0 = 0;
    int panelH = 0;
    int breakthroughY = 0;
    int hpY = 0;
    int routeY = 0;
    int settingsY = 0; // brightness (left half) and volume (right half) share this row
};
```

becomes (drop the `settingsY` line and its comment):

```cpp
struct Layout {
    int screenW = 0;
    int screenH = 0;
    int panelY0 = 0;
    int panelH = 0;
    int breakthroughY = 0;
    int hpY = 0;
    int routeY = 0;
};
```

and:

```cpp
    int y = gLayout.panelY0 + kPanelTopPad;
    gLayout.breakthroughY = y; y += kBreakthroughBarHeight + kSectionGap;
    gLayout.hpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.routeY = y; y += kRouteBarHeight + kSectionGap;
    gLayout.settingsY = y;
```

becomes:

```cpp
    int y = gLayout.panelY0 + kPanelTopPad;
    gLayout.breakthroughY = y; y += kBreakthroughBarHeight + kSectionGap;
    gLayout.hpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.routeY = y;
```

Delete `settingsRowRect()` entirely:

```cpp
Rect settingsRowRect() { return Rect{0, gLayout.settingsY, gLayout.screenW, kSettingsRowHeight}; }
```

- [ ] **Step 3: Delete the flash-state globals and `drawSettingsHalf`/`hitTestHud`/`flashSettingsButton` from `ui.cpp`**

Delete this whole block:

```cpp
// See flashSettingsButton()/drawSettingsHalf() - highlights whichever settings half (brightness
// or volume) was just tapped, as a tap-was-received confirmation independent of the brightness/
// volume value itself. kSettingsFlashDurationMs is a floor, not the actual on-screen duration:
// drawHud() only runs on its own throttled cadence (immediately after the tap, then every
// kHudRedrawIntervalMs while idle - see main.cpp), so the highlight stays visible through
// whichever of those redraws lands before this expires, which can be up to one redraw interval
// longer than kSettingsFlashDurationMs itself.
int gSettingsFlashButton = HUD_BUTTON_NONE;
uint32_t gSettingsFlashUntilMs = 0;
constexpr uint32_t kSettingsFlashDurationMs = 400;
```

Delete `drawSettingsHalf()` entirely:

```cpp
// Draws one settings control (brightness or volume) as "-" pinned to its left edge, "+" pinned
// to its right edge, and the current value centered between them, so the two tappable halves
// (see hitTestHud()) are visually obvious rather than baked into one run of left-aligned text.
// Flashes yellow briefly after either half is tapped - see flashSettingsButton() and
// kSettingsFlashDurationMs's comment for what "briefly" actually bounds.
void drawSettingsHalf(M5Canvas& canvas, const Rect& r, const char* valueLabel,
                       int downButton, int upButton, uint32_t nowMs) {
    int ly = r.y - gLayout.panelY0;
    bool flashing = nowMs < gSettingsFlashUntilMs
        && (gSettingsFlashButton == downButton || gSettingsFlashButton == upButton);
    uint16_t bg = flashing ? TFT_YELLOW : TFT_DARKGREY;
    canvas.fillRect(r.x, ly, r.w, r.h, bg);
    int yCenter = ly + r.h / 2;
    drawLeftAligned(canvas, "-", r.x + 10, yCenter, 24, 2, TFT_WHITE, bg);
    drawRightAligned(canvas, "+", r.x + r.w - 10, yCenter, 24, 2, TFT_WHITE, bg);
    drawCentered(canvas, valueLabel, r.x + r.w / 2, yCenter, r.w - 80, 2, TFT_WHITE, bg);
}
```

Delete `hitTestHud()` and `flashSettingsButton()` entirely:

```cpp
int hitTestHud(int touchX, int touchY) {
    Rect settingsRow = settingsRowRect();
    Rect brightnessHalf = leftHalf(settingsRow);
    Rect volumeHalf = rightHalf(settingsRow);
    if (rectContains(leftHalf(brightnessHalf), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_DOWN;
    if (rectContains(rightHalf(brightnessHalf), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_UP;
    if (rectContains(leftHalf(volumeHalf), touchX, touchY)) return HUD_BUTTON_VOLUME_DOWN;
    if (rectContains(rightHalf(volumeHalf), touchX, touchY)) return HUD_BUTTON_VOLUME_UP;
    return HUD_BUTTON_NONE;
}

void flashSettingsButton(int button) {
    gSettingsFlashButton = button;
    gSettingsFlashUntilMs = millis() + kSettingsFlashDurationMs;
}
```

- [ ] **Step 4: Update `drawHud()` itself in `ui.cpp`**

Change the signature and delete the settings-row section (the last part of the function body) and the now-unused `nowMs`:

```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone,
             uint8_t brightness, uint8_t volume) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);
```

becomes:

```cpp
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);
```

and delete this trailing block (keep `panel.pushSprite(0, gLayout.panelY0);` — just remove everything between the monsters bar and that line):

```cpp
    uint32_t nowMs = millis();
    Rect settingsRow = settingsRowRect();

    char brLine[16];
    snprintf(brLine, sizeof(brLine), "Bright %d%%", (brightness * 100) / 255);
    drawSettingsHalf(panel, leftHalf(settingsRow), brLine,
                      HUD_BUTTON_BRIGHTNESS_DOWN, HUD_BUTTON_BRIGHTNESS_UP, nowMs);

    char volLine[16];
    snprintf(volLine, sizeof(volLine), "Vol %d%%", (volume * 100) / 255);
    drawSettingsHalf(panel, rightHalf(settingsRow), volLine,
                      HUD_BUTTON_VOLUME_DOWN, HUD_BUTTON_VOLUME_UP, nowMs);

    panel.pushSprite(0, gLayout.panelY0);
```

so `drawHud()` ends with just `drawBar(panel, routeRect(), monstersFraction, TFT_CYAN, monstersLabel);` followed by `panel.pushSprite(0, gLayout.panelY0);`.

- [ ] **Step 5: Remove the touch-handling block from `main.cpp`, and update `drawHud` call sites**

Delete this entire block from `loop()` (everything from `auto touch = ...` through the closing brace of the `if (stateChanged)` block):

```cpp
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        // Diagnostic for the brightness/volume unresponsiveness report: this project's own
        // vendored M5Unified/M5GFX source was read (twice now) to rule out a touch/display
        // rotation mismatch, a missing Tab5 backlight-PWM/speaker-codec wiring, and a click-
        // detection/flick-threshold bug - none found, both the app code and the library's Tab5
        // bring-up look correct. No confirmed root cause survived either reading, so this line
        // stays to get real data on the next hardware flash: does a touch even register (this
        // line printing at all), and if so, is touch.x/touch.y within the row it should have
        // hit (button != -1)? flashSettingsButton() below adds an on-screen counterpart: if the
        // tapped quadrant visibly flashes yellow but the brightness/volume never actually
        // changes, that narrows it from "touch not registering" to "the hardware effect isn't
        // applying" - two very differently-fixed bugs that look identical from the outside.
        Serial.printf("[TOUCH] raw=(%d,%d) hitTestHud=%d\n", touch.x, touch.y, button);
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
            flashSettingsButton(button);
            saveNow();
            gLastHudDrawMs = 0; // force an immediate (unthrottled) HUD redraw this frame
        }
    }
```

Change both `drawHud(...)` call sites from `drawHud(M5.Display, gState, gZoneState, gBrightness, gVolume);` to `drawHud(M5.Display, gState, gZoneState);` (one is inside the `Cleared`-transition block, one is at the bottom of `loop()`).

- [ ] **Step 6: Build and test**

Run: `python3 -m platformio test -e native`
Expected: PASS, same test count as before this task (this task touches no `lib/core` code).

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully with no reference to `HudButton`, `hitTestHud`, `flashSettingsButton`, or `drawSettingsHalf` remaining anywhere.

Run: `grep -rn "HUD_BUTTON\|hitTestHud\|flashSettingsButton\|drawSettingsHalf\|settingsRowRect\|M5.Touch" src/ lib/`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
feat: remove unresponsive brightness/volume touch controls

Two specs' worth of unresolved touch debugging with no confirmed root
cause on real hardware - delete the feature outright rather than keep
chasing it. Device brightness/volume still load/save/apply at boot,
just aren't player-adjustable anymore. The reclaimed panel height
grows the zone viewport.
EOF
)"
```

---

### Task 2: Add the skill system (`lib/core/skills.h/.cpp`)

**Files:**
- Create: `lib/core/skills.h`
- Create: `lib/core/skills.cpp`
- Test: `test/test_skills/test_skills.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `enum class SkillVisual { Slash, Fireball, FrostShard, LightningBolt, VoidSpike, PhoenixNova, Earthquake, Starfall };`, `struct SkillDef { const char* name; int unlockRealmIndex; float cooldownSeconds; float damageMultiplier; SkillVisual visual; };`, `constexpr int NUM_SKILLS = 8;`, `extern const SkillDef SKILLS[NUM_SKILLS];`, `struct SkillState { float timer = 0.0f; int cycleIndex = 0; };`, `int countUnlockedSkills(int realmIndex);`, `int tickSkill(SkillState& state, double dtSeconds, int realmIndex);` — Task 4 (`zone_state` integration) and Task 6 (`zone_view` FX) both consume these exact names.

- [ ] **Step 1: Write the failing tests**

Create `test/test_skills/test_skills.cpp`:

```cpp
#include <unity.h>
#include "skills.h"

void setUp(void) {}
void tearDown(void) {}

void test_count_unlocked_skills_at_realm_zero(void) {
    TEST_ASSERT_EQUAL_INT(1, countUnlockedSkills(0));
}

void test_count_unlocked_skills_at_realm_seven(void) {
    TEST_ASSERT_EQUAL_INT(4, countUnlockedSkills(7));
}

void test_count_unlocked_skills_at_max_realm(void) {
    TEST_ASSERT_EQUAL_INT(NUM_SKILLS, countUnlockedSkills(15));
}

void test_first_skill_unlocks_at_realm_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, SKILLS[0].unlockRealmIndex);
}

void test_skill_table_cooldowns_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].cooldownSeconds > SKILLS[i - 1].cooldownSeconds);
    }
}

void test_skill_table_multipliers_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].damageMultiplier > SKILLS[i - 1].damageMultiplier);
    }
}

void test_skill_table_unlock_realms_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].unlockRealmIndex > SKILLS[i - 1].unlockRealmIndex);
    }
}

void test_tick_skill_does_not_fire_before_cooldown(void) {
    SkillState state;
    int fired = tickSkill(state, 1.0, 0); // realm 0 -> only skill 0 (3.0s cooldown) unlocked
    TEST_ASSERT_EQUAL_INT(-1, fired);
}

void test_tick_skill_fires_at_cooldown(void) {
    SkillState state;
    int fired = tickSkill(state, 3.0, 0);
    TEST_ASSERT_EQUAL_INT(0, fired);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, state.timer);
}

void test_tick_skill_does_not_immediately_refire(void) {
    SkillState state;
    tickSkill(state, 3.0, 0);
    int fired = tickSkill(state, 1.0, 0); // only 1.0s since the reset, well under 3.0s again
    TEST_ASSERT_EQUAL_INT(-1, fired);
}

void test_tick_skill_round_robins_among_unlocked_skills(void) {
    SkillState state;
    int first = tickSkill(state, 3.0, 4);  // realm 4 -> skills 0,1,2 unlocked; skill 0 cooldown 3.0s
    TEST_ASSERT_EQUAL_INT(0, first);
    int second = tickSkill(state, 3.5, 4); // skill 1's cooldown is 3.5s
    TEST_ASSERT_EQUAL_INT(1, second);
    int third = tickSkill(state, 4.0, 4);  // skill 2's cooldown is 4.0s
    TEST_ASSERT_EQUAL_INT(2, third);
    int fourth = tickSkill(state, 3.0, 4); // wraps back to skill 0
    TEST_ASSERT_EQUAL_INT(0, fourth);
}

void test_tick_skill_single_unlocked_skill_wraps_to_itself(void) {
    SkillState state;
    int fired = tickSkill(state, 3.0, 0); // realm 0 -> only skill 0 unlocked
    TEST_ASSERT_EQUAL_INT(0, fired);
    TEST_ASSERT_EQUAL_INT(0, state.cycleIndex);
}

void test_tick_skill_is_deterministic(void) {
    SkillState a;
    SkillState b;
    int firedA = tickSkill(a, 3.0, 0);
    int firedB = tickSkill(b, 3.0, 0);
    TEST_ASSERT_EQUAL_INT(firedA, firedB);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_count_unlocked_skills_at_realm_zero);
    RUN_TEST(test_count_unlocked_skills_at_realm_seven);
    RUN_TEST(test_count_unlocked_skills_at_max_realm);
    RUN_TEST(test_first_skill_unlocks_at_realm_zero);
    RUN_TEST(test_skill_table_cooldowns_strictly_increase);
    RUN_TEST(test_skill_table_multipliers_strictly_increase);
    RUN_TEST(test_skill_table_unlock_realms_strictly_increase);
    RUN_TEST(test_tick_skill_does_not_fire_before_cooldown);
    RUN_TEST(test_tick_skill_fires_at_cooldown);
    RUN_TEST(test_tick_skill_does_not_immediately_refire);
    RUN_TEST(test_tick_skill_round_robins_among_unlocked_skills);
    RUN_TEST(test_tick_skill_single_unlocked_skill_wraps_to_itself);
    RUN_TEST(test_tick_skill_is_deterministic);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_skills`
Expected: FAIL to build (`skills.h` does not exist yet).

- [ ] **Step 3: Create `lib/core/skills.h`**

```cpp
#pragma once

// A player's automatic combat skill kit - unlocked by cultivation realm (mirrors how
// GENERATORS in economy.h unlock by realm), fired round-robin during Fighting on top of the
// existing zone_combat autoattack. No manual activation - this project has no touch controls
// left, and skills don't add any.
enum class SkillVisual { Slash, Fireball, FrostShard, LightningBolt, VoidSpike, PhoenixNova, Earthquake, Starfall };

struct SkillDef {
    const char* name;
    int unlockRealmIndex;   // unlocked once GameState.realmIndex >= this
    float cooldownSeconds;
    float damageMultiplier; // bonus damage dealt = player.attackDamage * damageMultiplier
    SkillVisual visual;
};

constexpr int NUM_SKILLS = 8;
extern const SkillDef SKILLS[NUM_SKILLS];

struct SkillState {
    float timer = 0.0f; // counts up toward SKILLS[cycleIndex]'s cooldown
    int cycleIndex = 0;  // round-robin cursor among currently-unlocked skills
};

// Count of SKILLS[i] with unlockRealmIndex <= realmIndex. Always >= 1 for realmIndex >= 0,
// since SKILLS[0].unlockRealmIndex == 0. Scans the full table rather than assuming it stays
// sorted by unlockRealmIndex.
int countUnlockedSkills(int realmIndex);

// Advances state.timer by dtSeconds against SKILLS[state.cycleIndex]'s cooldown. Below
// cooldown: returns -1, only state.timer changes. At/above cooldown: resets state.timer to 0,
// advances state.cycleIndex to the next currently-unlocked skill (wrapping via modulo the
// live unlocked count, not a fixed NUM_SKILLS, so a just-unlocked skill folds into the
// rotation without a discontinuity), and returns the index of the skill that fired.
int tickSkill(SkillState& state, double dtSeconds, int realmIndex);
```

- [ ] **Step 4: Create `lib/core/skills.cpp`**

```cpp
#include "skills.h"

// Cooldown and multiplier both climb monotonically - early skills fire often for a small
// bonus, late skills are rare but hit hard, so the rotation never trivializes the existing
// HP-scaling combat balance in zone_combat.
const SkillDef SKILLS[NUM_SKILLS] = {
    {"Sword Qi Slash",    0,  3.0f, 1.5f, SkillVisual::Slash},
    {"Flame Palm",        2,  3.5f, 1.8f, SkillVisual::Fireball},
    {"Frost Needle",      4,  4.0f, 2.2f, SkillVisual::FrostShard},
    {"Thunderclap Fist",  6,  4.5f, 2.6f, SkillVisual::LightningBolt},
    {"Void Piercer",      8,  5.0f, 3.0f, SkillVisual::VoidSpike},
    {"Phoenix Nova",      10, 5.5f, 3.4f, SkillVisual::PhoenixNova},
    {"Earthquake Palm",   12, 6.0f, 3.8f, SkillVisual::Earthquake},
    {"Starfall Judgment", 14, 6.5f, 4.2f, SkillVisual::Starfall},
};

int countUnlockedSkills(int realmIndex) {
    int count = 0;
    for (int i = 0; i < NUM_SKILLS; ++i) {
        if (SKILLS[i].unlockRealmIndex <= realmIndex) count++;
    }
    return count;
}

int tickSkill(SkillState& state, double dtSeconds, int realmIndex) {
    int unlocked = countUnlockedSkills(realmIndex);
    if (unlocked <= 0) return -1; // defensive; unreachable in practice (SKILLS[0].unlockRealmIndex == 0)
    if (state.cycleIndex >= unlocked) state.cycleIndex = 0;
    const SkillDef& current = SKILLS[state.cycleIndex];
    state.timer += static_cast<float>(dtSeconds);
    if (state.timer < current.cooldownSeconds) return -1;
    state.timer = 0.0f;
    int fired = state.cycleIndex;
    state.cycleIndex = (state.cycleIndex + 1) % unlocked;
    return fired;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_skills`
Expected: PASS, all 13 test cases green.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS (existing suites unaffected).

- [ ] **Step 7: Commit**

```bash
git add lib/core/skills.h lib/core/skills.cpp test/test_skills/test_skills.cpp
git commit -m "feat: add realm-gated automatic skill system (lib/core/skills)"
```

---

### Task 3: Add the FX math module (`lib/core/fx.h/.cpp`)

**Files:**
- Create: `lib/core/fx.h`
- Create: `lib/core/fx.cpp`
- Test: `test/test_fx/test_fx.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `float shakeOffset(float t, float amplitudePx, float phaseRadians);`, `float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond);`, `float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW);` — Task 6 (skill FX/damage numbers/shake) and Task 9 (parallax) both consume these exact names.

- [ ] **Step 1: Write the failing tests**

Create `test/test_fx/test_fx.cpp`:

```cpp
#include <unity.h>
#include "fx.h"

void setUp(void) {}
void tearDown(void) {}

void test_shake_offset_is_zero_at_start(void) {
    float v = shakeOffset(0.0f, 5.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_shake_offset_is_zero_at_end(void) {
    float v = shakeOffset(1.0f, 5.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_shake_offset_stays_within_amplitude(void) {
    for (int i = 0; i <= 100; ++i) {
        float t = static_cast<float>(i) / 100.0f;
        float v = shakeOffset(t, 5.0f, 0.0f);
        TEST_ASSERT_TRUE(v >= -5.0001f && v <= 5.0001f);
    }
}

void test_shake_offset_phase_shift_differs_from_unshifted(void) {
    float a = shakeOffset(0.3f, 5.0f, 0.0f);
    float b = shakeOffset(0.3f, 5.0f, 1.5707963f); // pi/2
    TEST_ASSERT_TRUE(a != b);
}

void test_damage_number_rise_offset_is_zero_at_spawn(void) {
    float v = damageNumberRiseOffsetPx(0.0f, 40.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

void test_damage_number_rise_offset_grows_more_negative_over_time(void) {
    float early = damageNumberRiseOffsetPx(0.1f, 40.0f);
    float late = damageNumberRiseOffsetPx(0.5f, 40.0f);
    TEST_ASSERT_TRUE(late < early);
}

void test_parallax_wrap_x_stays_within_viewport(void) {
    for (int i = 0; i < 50; ++i) {
        float elapsed = static_cast<float>(i) * 3.7f; // sweep past multiple wraps
        float v = parallaxWrapX(10.0f, 20.0f, elapsed, 200.0f);
        TEST_ASSERT_TRUE(v >= 0.0f && v < 200.0f);
    }
}

void test_parallax_wrap_x_is_deterministic(void) {
    float a = parallaxWrapX(10.0f, 20.0f, 12.5f, 200.0f);
    float b = parallaxWrapX(10.0f, 20.0f, 12.5f, 200.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a, b);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_shake_offset_is_zero_at_start);
    RUN_TEST(test_shake_offset_is_zero_at_end);
    RUN_TEST(test_shake_offset_stays_within_amplitude);
    RUN_TEST(test_shake_offset_phase_shift_differs_from_unshifted);
    RUN_TEST(test_damage_number_rise_offset_is_zero_at_spawn);
    RUN_TEST(test_damage_number_rise_offset_grows_more_negative_over_time);
    RUN_TEST(test_parallax_wrap_x_stays_within_viewport);
    RUN_TEST(test_parallax_wrap_x_is_deterministic);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_fx`
Expected: FAIL to build (`fx.h` does not exist yet).

- [ ] **Step 3: Create `lib/core/fx.h`**

```cpp
#pragma once

// Small, pure, hardware-agnostic math backing zone_view.cpp's combat/environment effects -
// kept here (not in src/) so it's unit-testable, same split this project already uses for
// jump arcs, patrol motion, and procedural coloring.

// Decaying oscillating offset in pixels for t in [0,1] (elapsed/duration since a triggering
// event). 0 at t=0 and t=1, bounded within +-amplitudePx in between. phaseRadians lets two
// axes share the same t without moving in lockstep (pass 0 for one axis, pi/2 for the other).
float shakeOffset(float t, float amplitudePx, float phaseRadians);

// Upward pixel offset (i.e. <= 0) for a floating damage number at elapsedSeconds since it
// spawned - constant speed, no physics. Callers add this to the number's base y each frame.
float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond);

// Wraps a drifting background element's x position at elapsedSeconds, drifting from seedX at
// pxPerSecond, into [0, viewportW). fmod-based so it wraps with no jump at the boundary.
float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW);
```

- [ ] **Step 4: Create `lib/core/fx.cpp`**

```cpp
#include "fx.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

float shakeOffset(float t, float amplitudePx, float phaseRadians) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    // sin(t*pi), not a linear (1-t) decay: the linear form is only 0 at t=1, so at t=0 it
    // reduces to amplitudePx*sin(phaseRadians) - the FULL amplitude when phaseRadians=pi/2,
    // exactly the phase renderZoneView() uses for the Y shake axis. sin(t*pi) is 0 at both
    // t=0 and t=1 for any phaseRadians, matching this function's documented contract.
    float envelope = std::sin(t * kPi);
    return amplitudePx * envelope * std::sin(t * 6.0f * kPi + phaseRadians);
}

float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond) {
    if (elapsedSeconds < 0.0f) elapsedSeconds = 0.0f;
    return -(elapsedSeconds * risePxPerSecond);
}

float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW) {
    if (viewportW <= 0.0f) return seedX;
    float raw = seedX + pxPerSecond * elapsedSeconds;
    float wrapped = std::fmod(raw, viewportW);
    if (wrapped < 0.0f) wrapped += viewportW;
    return wrapped;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_fx`
Expected: PASS, all 8 test cases green.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add lib/core/fx.h lib/core/fx.cpp test/test_fx/test_fx.cpp
git commit -m "feat: add pure FX math module (shake/damage-number/parallax curves)"
```

---

### Task 4: Integrate skills into `zone_state`

**Files:**
- Modify: `lib/core/zone_state.h`
- Modify: `lib/core/zone_state.cpp`
- Test: `test/test_zone_state/test_zone_state.cpp`

**Interfaces:**
- Consumes: `SkillState`, `tickSkill`, `SKILLS` from Task 2's `skills.h`.
- Produces: `ZoneState` gains `SkillState skill;` and `int skillFiredThisTick = -1;` — Task 6 (`main.cpp`/`zone_view.cpp`) reads `gZoneState.skillFiredThisTick` immediately after each `tickZone()` call.

- [ ] **Step 1: Write the failing tests**

Add these test functions to `test/test_zone_state/test_zone_state.cpp`, placed after `test_restart_zone_rebuilds_platform_and_position_state` and before the `int main(...)` runner:

```cpp
void test_start_zone_resets_skill_state(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.skill.timer);
    TEST_ASSERT_EQUAL_INT(0, s.skill.cycleIndex);
    TEST_ASSERT_EQUAL_INT(-1, s.skillFiredThisTick);
}

void test_skill_timer_frozen_while_walking(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    float timerAtStart = s.skill.timer;
    tickZone(s, 0.1, 10.0, 0); // still Walking on the first tick of a fresh zone
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, timerAtStart, s.skill.timer);
}

void test_skill_fires_after_cooldown_while_fighting(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000; // keep the fight alive long enough to observe the skill firing
    bool fired = false;
    int firedIndex = -1;
    for (int i = 0; i < 35; ++i) { // 3.5s, past skill 0's 3.0s cooldown
        tickZone(s, 0.1, 10.0, 0);
        if (s.skillFiredThisTick >= 0) { fired = true; firedIndex = s.skillFiredThisTick; break; }
    }
    TEST_ASSERT_TRUE(fired);
    TEST_ASSERT_EQUAL_INT(0, firedIndex); // only skill 0 is unlocked at realm 0
}

void test_skill_does_not_fire_before_cooldown_elapses(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    for (int i = 0; i < 20; ++i) { // 2.0s, under skill 0's 3.0s cooldown
        tickZone(s, 0.1, 10.0, 0);
        TEST_ASSERT_EQUAL_INT(-1, s.skillFiredThisTick);
    }
}

void test_skill_bonus_damage_exceeds_plain_attack_damage(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    int hpBefore = 0;
    int hpAfter = 0;
    for (int i = 0; i < 35; ++i) {
        hpBefore = s.enemy.hp;
        tickZone(s, 0.1, 10.0, 0);
        if (s.skillFiredThisTick >= 0) { hpAfter = s.enemy.hp; break; }
    }
    int dropped = hpBefore - hpAfter;
    TEST_ASSERT_TRUE(dropped > s.player.attackDamage); // more than a plain autoattack alone
}

void test_skill_round_robins_among_unlocked_skills_in_zone(void) {
    ZoneState s = startZone(makeZoneMap(4), 4); // realm 4 -> 3 unlocked skills (indices 0,1,2)
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 4);
    }
    s.enemy.maxHp = 1000000;
    s.enemy.hp = 1000000;
    int fired[6] = {-1, -1, -1, -1, -1, -1};
    int count = 0;
    for (int i = 0; i < 300 && count < 6; ++i) { // 30s of simulated fighting, generous headroom
        s.player.hp = s.player.maxHp; // top off every tick - only the skill cycle is under test
                                       // here, and a realm-4 enemy's autoattacks (20 dmg every
                                       // 1.2s) would otherwise kill a 260-hp player well before
                                       // 30s elapse, triggering restartZone() and resetting the
                                       // cycle mid-test
        tickZone(s, 0.1, 10.0, 4);
        if (s.skillFiredThisTick >= 0) { fired[count++] = s.skillFiredThisTick; }
    }
    TEST_ASSERT_EQUAL_INT(6, count);
    TEST_ASSERT_EQUAL_INT(0, fired[0]);
    TEST_ASSERT_EQUAL_INT(1, fired[1]);
    TEST_ASSERT_EQUAL_INT(2, fired[2]);
    TEST_ASSERT_EQUAL_INT(0, fired[3]);
    TEST_ASSERT_EQUAL_INT(1, fired[4]);
    TEST_ASSERT_EQUAL_INT(2, fired[5]);
}

void test_restart_zone_resets_skill_state(void) {
    ZoneState s = startZone(makeZoneMap(4), 4);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 4);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    for (int i = 0; i < 40; ++i) tickZone(s, 0.1, 10.0, 4); // let the skill timer/cycle advance
    restartZone(s, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.skill.timer);
    TEST_ASSERT_EQUAL_INT(0, s.skill.cycleIndex);
}
```

Add matching `RUN_TEST(...)` lines to `main()`, right before `return UNITY_END();`:

```cpp
    RUN_TEST(test_start_zone_resets_skill_state);
    RUN_TEST(test_skill_timer_frozen_while_walking);
    RUN_TEST(test_skill_fires_after_cooldown_while_fighting);
    RUN_TEST(test_skill_does_not_fire_before_cooldown_elapses);
    RUN_TEST(test_skill_bonus_damage_exceeds_plain_attack_damage);
    RUN_TEST(test_skill_round_robins_among_unlocked_skills_in_zone);
    RUN_TEST(test_restart_zone_resets_skill_state);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: FAIL to build (`ZoneState` has no `skill`/`skillFiredThisTick` members yet).

- [ ] **Step 3: Add `SkillState`/`skillFiredThisTick` to `ZoneState` in `zone_state.h`**

Add the include:

```cpp
#pragma once
#include <vector>
#include "zone_map.h"
#include "zone_combat.h"
```

becomes:

```cpp
#pragma once
#include <vector>
#include "zone_map.h"
#include "zone_combat.h"
#include "skills.h"
```

Change the `ZoneState` struct from:

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

to:

```cpp
struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;                    // height above ground baseline, world units
    float posY = 0.0f;
    int currentPlatformIndex = 0;         // which platform posX/posY sit on while Walking
    ZonePhase phase = ZonePhase::Walking;
    JumpArc jump;                         // only meaningful while phase == Jumping
    float walkingElapsedSeconds = 0.0f;   // drives monster patrol position; frozen outside Walking
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
    SkillState skill;             // fires only while Fighting; frozen otherwise, like walkingElapsedSeconds
    int skillFiredThisTick = -1;  // SKILLS[] index fired on the most recent tickZone() call, or -1
};
```

(Note: `posX`/`posY` comment placement was already slightly off in the original file — this fixes it in passing since the line is being touched anyway; no functional change.)

- [ ] **Step 4: Integrate `tickSkill` into `tickZone()`'s `Fighting` branch in `zone_state.cpp`**

Add the include:

```cpp
#include "zone_state.h"
#include <cmath>
```

(unchanged — `skills.h` is already pulled in transitively via `zone_state.h`, no new include needed here.)

Change `tickZone()`'s opening line from:

```cpp
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == ZonePhase::Cleared) return;
```

to:

```cpp
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    state.skillFiredThisTick = -1; // reset every call - a caller inspects this immediately after
    if (state.phase == ZonePhase::Cleared) return;
```

Change the `Fighting` branch (the function's last block) from:

```cpp
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
    if (isDefeated(state.enemy)) {
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.phase = ZonePhase::Walking;
    } else if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    }
}
```

`startZone()`/`restartZone()` need no changes — `SkillState`'s default member initializers (`timer = 0.0f`, `cycleIndex = 0`) and `ZoneState`'s own `s.skill` (default-constructed inside `ZoneState s;` at the top of `startZone()`) already produce fresh state, and `skillFiredThisTick` gets reset to `-1` by every `tickZone()` call regardless.

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_state`
Expected: PASS, including all 7 new skill-related cases.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_state.h lib/core/zone_state.cpp test/test_zone_state/test_zone_state.cpp
git commit -m "feat: fire skills during zone combat on top of the existing autoattack"
```

---

### Task 5: Add `characterAuraColor` (`lib/core/zone_textures`)

**Files:**
- Modify: `lib/core/zone_textures.h`
- Modify: `lib/core/zone_textures.cpp`
- Test: `test/test_zone_textures/test_zone_textures.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `RGB characterAuraColor(int realmIndex);` — Task 7 (`zone_view.cpp` character rendering) consumes this.

- [ ] **Step 1: Write the failing tests**

Add these test functions to `test/test_zone_textures/test_zone_textures.cpp`, placed after `test_platform_color_differs_across_realms` and before `int main(...)`:

```cpp
void test_character_aura_color_is_deterministic(void) {
    RGB a = characterAuraColor(6);
    RGB b = characterAuraColor(6);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_character_aura_color_differs_across_realms(void) {
    RGB a = characterAuraColor(0);
    RGB b = characterAuraColor(10);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}
```

Add matching `RUN_TEST(...)` lines to `main()`, right before `return UNITY_END();`:

```cpp
    RUN_TEST(test_character_aura_color_is_deterministic);
    RUN_TEST(test_character_aura_color_differs_across_realms);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: FAIL to build (`characterAuraColor` is not declared yet).

- [ ] **Step 3: Add the declaration to `zone_textures.h`**

Change the end of the file from:

```cpp
// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);
```

to:

```cpp
// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);

// Faint ring color drawn around the character in zone_view - reuses the zone's own per-realm
// hue (not an arbitrary rainbow), saturation climbing with realmIndex for a subtle "aura
// strengthens with cultivation" progression. Deterministic.
RGB characterAuraColor(int realmIndex);
```

- [ ] **Step 4: Implement it in `zone_textures.cpp`**

Add the include:

```cpp
#include "zone_textures.h"
#include "hash.h"
#include <cstdint>
#include <cmath>
```

becomes:

```cpp
#include "zone_textures.h"
#include "hash.h"
#include "realms.h"
#include <cstdint>
#include <cmath>
```

Append at the end of the file:

```cpp

RGB characterAuraColor(int realmIndex) {
    float t = static_cast<float>(realmIndex) / static_cast<float>(NUM_REALMS - 1);
    float saturation = 0.5f + 0.3f * t; // climbs from 0.5 at realm 0 to 0.8 at realm 15
    return hsvToRgb(realmHue(realmIndex), saturation, 0.9f);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `python3 -m platformio test -e native -f test_zone_textures`
Expected: PASS, including both new cases.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `python3 -m platformio test -e native`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add lib/core/zone_textures.h lib/core/zone_textures.cpp test/test_zone_textures/test_zone_textures.cpp
git commit -m "feat: add per-realm character aura color"
```

---

### Task 6: Combat FX — damage numbers, skill projectile/impact, screen shake

**Files:**
- Modify: `src/zone_view.h`
- Modify: `src/zone_view.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `SKILLS`, `SkillVisual` from Task 2; `shakeOffset`, `damageNumberRiseOffsetPx` from Task 3; `ZoneState::skillFiredThisTick` from Task 4.
- Produces: `void spawnDamageNumber(bool onPlayer, int amount, int skillIndex);`, `void triggerSkillFx(int skillIndex);`, `void playSkillSfx(int skillIndex);` in `zone_view.h`. Also produces module-static `int gSkillFxIndex` and `uint32_t gSkillFxStartMs` inside `zone_view.cpp`'s anonymous namespace — Task 7's casting pose reads these two directly (same file, same namespace).

No native tests — `zone_view.cpp`/`main.cpp` are hardware glue with no test harness, same as Task 1. Verification is a successful `pio run -e esp32p4_pioarduino` build plus a full native-suite pass (to confirm this task didn't touch anything `lib/core` depends on).

- [ ] **Step 1: Add new declarations to `zone_view.h`**

Append at the end of the file (after `void playVictorySfx();`):

```cpp

// Spawns a floating combat-text number, drawn rising from the player's or the current enemy's
// position over the next ~700ms then removed outright (no fade - M5Canvas has no real alpha
// blending). skillIndex is the SKILLS[] index if this damage came from a skill (colors/sizes
// the number accordingly), or -1 for a plain autoattack hit.
void spawnDamageNumber(bool onPlayer, int amount, int skillIndex);

// Starts a skill's projectile-travel-then-impact-burst animation (from the character to the
// current enemy) and a short screen shake. Call once per fired skill, immediately after
// tickZone() reports one via ZoneState::skillFiredThisTick - do not wait for the next
// throttled render, for the same reason triggerAttackFlash()/triggerHitFlash() are called
// eagerly today.
void triggerSkillFx(int skillIndex);

// Procedural cast chime, pitch scaled by skillIndex so later (stronger) skills sound more
// dramatic - distinct from the existing single-tone playAttackSfx()/playHitSfx().
void playSkillSfx(int skillIndex);
```

- [ ] **Step 2: Add includes and new module state to `zone_view.cpp`**

Change the top of the file from:

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
```

to:

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include <cstdio>

namespace {
M5Canvas* gZoneCanvas = nullptr;
int gViewportW = 0;
int gViewportH = 0;

uint32_t gAttackFlashUntilMs = 0; // flash on the monster - player's attack landed
uint32_t gHitFlashUntilMs = 0;    // flash on the character - enemy's attack landed
constexpr uint32_t kFlashDurationMs = 150;

// Last-known screen position of the current (or most recently current) enemy - a module-static
// cache, NOT a per-call local, because a skill FX or damage number can still be animating for
// up to kSkillFxTotalMs/kDamageNumberDurationMs after the enemy that triggered it is defeated
// (monstersDefeated[i] becomes true and currentMonsterIndex resets to -1 on the very same
// tickZone() call that lands the killing blow, so no monster is ever isCurrent again for that
// enemy). A per-call local reset to (0,0) every render would make any FX/number still in flight
// at the moment of a kill snap to the screen's top-left corner instead of the enemy's last
// position - this cache is what keeps it pinned there until a new monster becomes current.
int gLastEnemyScreenX = 0;
int gLastEnemyScreenY = 0;

// Skill projectile/impact/shake state - triggerSkillFx() latches these, renderZoneView()
// re-derives the current animation frame from elapsed time every call (position is static
// during Fighting, same reasoning the pre-existing attack/hit flash already relies on).
int gSkillFxIndex = -1;
uint32_t gSkillFxStartMs = 0;
constexpr uint32_t kSkillTravelMs = 220;
constexpr uint32_t kSkillImpactMs = 160;
constexpr uint32_t kSkillFxTotalMs = kSkillTravelMs + kSkillImpactMs;
constexpr uint32_t kShakeDurationMs = 140;
constexpr float kShakeAmplitudePx = 5.0f;
constexpr float kPi = 3.14159265358979323846f;

struct DamageNumber {
    bool active = false;
    bool onPlayer = false;
    int amount = 0;
    int skillIndex = -1; // -1 = plain autoattack hit
    uint32_t spawnMs = 0;
};
constexpr int kMaxDamageNumbers = 8;
DamageNumber gDamageNumbers[kMaxDamageNumbers];
constexpr uint32_t kDamageNumberDurationMs = 700;
constexpr float kDamageNumberRisePxPerSec = 40.0f;

// Fill/ring color pair for a skill's projectile and impact burst - color-only differentiation
// (not per-skill unique geometry) keeps this tractable across 8 skill kinds while still
// visually distinguishing which skill just fired.
void skillColors(SkillVisual visual, uint16_t& fillColor, uint16_t& ringColor) {
    switch (visual) {
        case SkillVisual::Slash:         fillColor = TFT_WHITE;    ringColor = TFT_LIGHTGREY; break;
        case SkillVisual::Fireball:      fillColor = TFT_ORANGE;   ringColor = TFT_RED;       break;
        case SkillVisual::FrostShard:    fillColor = TFT_CYAN;     ringColor = TFT_WHITE;     break;
        case SkillVisual::LightningBolt: fillColor = TFT_YELLOW;   ringColor = TFT_PURPLE;    break;
        case SkillVisual::VoidSpike:     fillColor = TFT_PURPLE;   ringColor = TFT_MAGENTA;   break;
        case SkillVisual::PhoenixNova:   fillColor = TFT_ORANGE;   ringColor = TFT_GOLD;      break;
        case SkillVisual::Earthquake:    fillColor = TFT_BROWN;    ringColor = TFT_OLIVE;     break;
        case SkillVisual::Starfall:      fillColor = TFT_SKYBLUE;  ringColor = TFT_WHITE;     break;
        default:                         fillColor = TFT_YELLOW;   ringColor = TFT_ORANGE;    break;
    }
}
```

- [ ] **Step 3: Extend `drawFlash()` with size/color parameters**

Change:

```cpp
void drawFlash(M5Canvas& canvas, int screenX, int standY, uint32_t nowMs, uint32_t untilMs) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, standY - 20, 6, TFT_YELLOW);
    canvas.drawCircle(screenX, standY - 20, 10, TFT_ORANGE);
}
```

to:

```cpp
void drawFlash(M5Canvas& canvas, int screenX, int standY, uint32_t nowMs, uint32_t untilMs,
                int fillRadius = 6, int ringRadius = 10,
                uint16_t fillColor = TFT_YELLOW, uint16_t ringColor = TFT_ORANGE) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, standY - 20, fillRadius, fillColor);
    canvas.drawCircle(screenX, standY - 20, ringRadius, ringColor);
}
```

The two existing call sites (`drawFlash(canvas, mx, my, nowMs, gAttackFlashUntilMs);` and `drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);`) need no changes — the new parameters default to today's exact look.

- [ ] **Step 4: Track the current enemy's screen position and render skill FX + damage numbers in `renderZoneView()`**

Change the monster-drawing loop from:

```cpp
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
```

to:

```cpp
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
        if (isCurrent) {
            drawFlash(canvas, mx, my, nowMs, gAttackFlashUntilMs);
            gLastEnemyScreenX = mx;
            gLastEnemyScreenY = my;
        }
    }
```

(Uses the module-static `gLastEnemyScreenX`/`gLastEnemyScreenY` added in Step 2 - NOT a per-call
local - so the position survives past the render call where the enemy is defeated instead of
resetting to (0,0).)

Change the end of the function from:

```cpp
    int charX = screenXFor(state.posX, state.map.arenaWidth);
    int charY = screenYFor(state.posY, groundY);
    drawCharacter(canvas, charX, charY, state.phase, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);

    canvas.pushSprite(0, kHeaderHeight);
}
```

to:

```cpp
    int charX = screenXFor(state.posX, state.map.arenaWidth);
    int charY = screenYFor(state.posY, groundY);
    drawCharacter(canvas, charX, charY, state.phase, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);

    uint32_t skillElapsed = nowMs - gSkillFxStartMs;
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
        uint16_t fillColor, ringColor;
        skillColors(SKILLS[gSkillFxIndex].visual, fillColor, ringColor);
        if (skillElapsed < kSkillTravelMs) {
            float t = static_cast<float>(skillElapsed) / static_cast<float>(kSkillTravelMs);
            int px = charX + static_cast<int>((gLastEnemyScreenX - charX) * t);
            int py = charY + static_cast<int>((gLastEnemyScreenY - charY) * t);
            canvas.fillCircle(px, py - 20, 5, fillColor); // -20 keeps it roughly chest-height
        } else {
            drawFlash(canvas, gLastEnemyScreenX, gLastEnemyScreenY, nowMs,
                      gSkillFxStartMs + kSkillFxTotalMs, 10, 16, fillColor, ringColor);
        }
        if (skillElapsed < kShakeDurationMs) {
            float shakeT = static_cast<float>(skillElapsed) / static_cast<float>(kShakeDurationMs);
            shakeX = shakeOffset(shakeT, kShakeAmplitudePx, 0.0f);
            shakeY = shakeOffset(shakeT, kShakeAmplitudePx, kPi / 2.0f);
        }
    }

    for (int i = 0; i < kMaxDamageNumbers; ++i) {
        DamageNumber& dn = gDamageNumbers[i];
        if (!dn.active) continue;
        uint32_t elapsed = nowMs - dn.spawnMs;
        if (elapsed >= kDamageNumberDurationMs) { dn.active = false; continue; }
        int baseX = dn.onPlayer ? charX : gLastEnemyScreenX;
        int baseY = dn.onPlayer ? charY : gLastEnemyScreenY;
        float rise = damageNumberRiseOffsetPx(static_cast<float>(elapsed) / 1000.0f, kDamageNumberRisePxPerSec);
        int drawY = baseY - 30 + static_cast<int>(rise); // -30 starts above the head, not the feet
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", dn.amount);
        uint16_t color = TFT_WHITE;
        if (dn.skillIndex >= 0) {
            uint16_t unusedRing;
            skillColors(SKILLS[dn.skillIndex].visual, color, unusedRing);
        }
        canvas.setTextSize(dn.skillIndex >= 0 ? 3 : 2);
        canvas.setTextColor(color);
        canvas.setCursor(baseX - canvas.textWidth(buf) / 2, drawY);
        canvas.print(buf);
    }

    canvas.pushSprite(static_cast<int>(shakeX), kHeaderHeight + static_cast<int>(shakeY));
}
```

- [ ] **Step 5: Implement `spawnDamageNumber`, `triggerSkillFx`, `playSkillSfx`**

Append after `playVictorySfx()`'s definition at the end of the file:

```cpp

void spawnDamageNumber(bool onPlayer, int amount, int skillIndex) {
    int slot = -1;
    for (int i = 0; i < kMaxDamageNumbers; ++i) {
        if (!gDamageNumbers[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < kMaxDamageNumbers; ++i) {
            if (gDamageNumbers[i].spawnMs < gDamageNumbers[slot].spawnMs) slot = i;
        }
    }
    gDamageNumbers[slot] = DamageNumber{true, onPlayer, amount, skillIndex, millis()};
}

void triggerSkillFx(int skillIndex) {
    gSkillFxIndex = skillIndex;
    gSkillFxStartMs = millis();
}

void playSkillSfx(int skillIndex) {
    float base = 440.0f + 60.0f * static_cast<float>(skillIndex);
    M5.Speaker.tone(base, 50);
    delay(40);
    M5.Speaker.tone(base * 1.5f, 70);
}
```

- [ ] **Step 6: Wire the new triggers into `main.cpp`**

Change:

```cpp
    if (wasFighting && gZoneState.enemy.hp < enemyHpBefore) { playAttackSfx(); triggerAttackFlash(); }
    if (wasFighting && gZoneState.player.hp < playerHpBefore) { playHitSfx(); triggerHitFlash(); }
```

to:

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

- [ ] **Step 7: Build and test**

Run: `python3 -m platformio test -e native`
Expected: PASS (this task touches no `lib/core` code).

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

- [ ] **Step 8: Commit**

```bash
git add src/zone_view.h src/zone_view.cpp src/main.cpp
git commit -m "feat: add skill projectile/impact FX, screen shake, and floating damage numbers"
```

---

### Task 7: Character animation — arms, 4-frame walk, casting pose, aura ring

**Files:**
- Modify: `src/zone_view.cpp`

**Interfaces:**
- Consumes: `gSkillFxIndex`, `gSkillFxStartMs` (module-static, from Task 6, same file/namespace); `characterAuraColor` from Task 5.
- Produces: `drawCharacter(M5Canvas& canvas, int screenX, int standY, ZonePhase phase, uint32_t nowMs, int realmIndex)` — signature gains a trailing `int realmIndex` parameter; its one call site in `renderZoneView()` updates to match.

No native tests (rendering code, no test harness) — verified via `pio run -e esp32p4_pioarduino`.

- [ ] **Step 1: Rewrite `drawCharacter()`**

Change:

```cpp
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
```

to:

```cpp
void drawCharacter(M5Canvas& canvas, int screenX, int standY, ZonePhase phase, uint32_t nowMs, int realmIndex) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    constexpr int kAirborneLegTuck = 6; // airborne pose: legs tucked up higher than walk/idle
    constexpr int kArmLength = 10;
    constexpr uint32_t kCastingPoseMs = 200;
    bool walking = (phase == ZonePhase::Walking);
    bool jumping = (phase == ZonePhase::Jumping);
    bool casting = gSkillFxIndex >= 0 && (nowMs - gSkillFxStartMs) < kCastingPoseMs;

    constexpr int kWalkBobFrames[4] = {0, 1, 2, 1}; // 4-frame walk cycle (was 2-frame)
    int bob = walking ? kWalkBobFrames[(nowMs / 100) % 4] : 0;
    int legTuck = jumping ? kAirborneLegTuck : 0;
    int headY = standY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = standY - kBodyHeight + bob;
    int shoulderY = bodyTop + 4;

    RGB aura = characterAuraColor(realmIndex);
    uint16_t auraColor = canvas.color565(aura.r, aura.g, aura.b);
    canvas.drawCircle(screenX, (headY + standY) / 2, kBodyHeight, auraColor);

    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);

    if (casting) {
        // Arms raised overhead, synced to a fired skill's opening frames.
        canvas.drawLine(screenX - 5, shoulderY, screenX - kArmLength, shoulderY - kArmLength, TFT_WHITE);
        canvas.drawLine(screenX + 5, shoulderY, screenX + kArmLength, shoulderY - kArmLength, TFT_WHITE);
    } else {
        canvas.drawLine(screenX - 5, shoulderY, screenX - kArmLength, shoulderY + kArmLength / 2, TFT_WHITE);
        canvas.drawLine(screenX + 5, shoulderY, screenX + kArmLength, shoulderY + kArmLength / 2, TFT_WHITE);
    }

    canvas.fillRect(screenX - 5, standY - 4 + bob - legTuck, 4, 4, TFT_NAVY); // left leg
    canvas.fillRect(screenX + 1, standY - 4 - bob - legTuck, 4, 4, TFT_NAVY); // right leg - moves opposite the left for a scissor gait
}
```

- [ ] **Step 2: Update the one call site in `renderZoneView()`**

Change:

```cpp
    drawCharacter(canvas, charX, charY, state.phase, nowMs);
```

to:

```cpp
    drawCharacter(canvas, charX, charY, state.phase, nowMs, state.map.realmIndex);
```

- [ ] **Step 3: Build**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

Run: `python3 -m platformio test -e native`
Expected: PASS (no `lib/core` change in this task).

- [ ] **Step 4: Commit**

```bash
git add src/zone_view.cpp
git commit -m "feat: character arms, 4-frame walk cycle, casting pose, per-realm aura ring"
```

---

### Task 8: Monster tier silhouettes

**Files:**
- Modify: `src/zone_view.cpp`

**Interfaces:**
- Consumes: nothing new (uses standard M5Canvas primitives already available via the M5Unified/M5GFX includes pulled in by `zone_view.h`).
- Produces: `drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent, int tierIndex)` — signature gains a trailing `int tierIndex` parameter; its one call site updates to pass the existing loop index.

No native tests (rendering code) — verified via `pio run -e esp32p4_pioarduino`.

- [ ] **Step 1: Add `<cmath>` for the tier-1 spike geometry**

Change the top of `zone_view.cpp` (as left by Task 6's Step 2) from:

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include <cstdio>
```

to:

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include <cstdio>
#include <cmath>
```

- [ ] **Step 2: Rewrite `drawMonster()`**

Change:

```cpp
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
```

to:

```cpp
void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent, int tierIndex) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);

    if (tierIndex <= 0) {
        // Tier 0: round "slime" body - today's original silhouette.
        canvas.fillCircle(screenX, standY - radius, radius, fill);
    } else if (tierIndex == 1) {
        // Tier 1: diamond body with 4 spikes around the rim - reads sharper/angrier.
        int cy = standY - radius;
        canvas.fillTriangle(screenX, cy - radius, screenX - radius, cy, screenX, cy + radius, fill);
        canvas.fillTriangle(screenX, cy - radius, screenX + radius, cy, screenX, cy + radius, fill);
        constexpr int kSpikes = 4;
        for (int s = 0; s < kSpikes; ++s) {
            float angle = (6.2831853f / kSpikes) * static_cast<float>(s);
            int tipX = screenX + static_cast<int>((radius + 6) * std::cos(angle));
            int tipY = cy + static_cast<int>((radius + 6) * std::sin(angle));
            int baseX1 = screenX + static_cast<int>(radius * std::cos(angle - 0.2f));
            int baseY1 = cy + static_cast<int>(radius * std::sin(angle - 0.2f));
            int baseX2 = screenX + static_cast<int>(radius * std::cos(angle + 0.2f));
            int baseY2 = cy + static_cast<int>(radius * std::sin(angle + 0.2f));
            canvas.fillTriangle(tipX, tipY, baseX1, baseY1, baseX2, baseY2, fill);
        }
    } else {
        // Tier 2: biggest body plus wing/horn shapes - reads as the toughest silhouette.
        int bigRadius = radius + 6;
        int cy = standY - bigRadius;
        canvas.fillCircle(screenX, cy, bigRadius, fill);
        canvas.fillTriangle(screenX - bigRadius, cy, screenX - bigRadius - 10, cy - 8, screenX - bigRadius - 10, cy + 8, fill);
        canvas.fillTriangle(screenX + bigRadius, cy, screenX + bigRadius + 10, cy - 8, screenX + bigRadius + 10, cy + 8, fill);
        radius = bigRadius; // so the eyes/current-ring below sit correctly on the enlarged body
    }

    int eyeY = standY - radius;
    canvas.fillCircle(screenX - radius / 3, eyeY, 2, TFT_BLACK);
    canvas.fillCircle(screenX + radius / 3, eyeY, 2, TFT_BLACK);
    if (isCurrent) {
        canvas.drawCircle(screenX, eyeY, radius + 3, TFT_YELLOW);
    }
}
```

- [ ] **Step 3: Update the one call site in `renderZoneView()`**

Change:

```cpp
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent);
```

to:

```cpp
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent, static_cast<int>(i));
```

(`i` is already the monster's tier index — monsters are stored one-per-elevated-platform in increasing-difficulty order, and `monsterColor(state.map.realmIndex, static_cast<int>(i))` on the very next-door line already relies on the same fact.)

- [ ] **Step 4: Build**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

Run: `python3 -m platformio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/zone_view.cpp
git commit -m "feat: give monsters tier-distinct silhouettes instead of a uniform circle"
```

---

### Task 9: Environment richness — parallax elements and ground texture

**Files:**
- Modify: `src/zone_view.cpp`

**Interfaces:**
- Consumes: `hashRange` from `lib/core/hash.h` (already a project dependency, used by `zone_map`/`zone_textures`); `parallaxWrapX` from Task 3's `fx.h` (already included in this file since Task 6).
- Produces: nothing consumed by later tasks (this is the last `zone_view.cpp` task).

No native tests (rendering code) — verified via `pio run -e esp32p4_pioarduino`.

- [ ] **Step 1: Add the `hash.h` include**

Change:

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include <cstdio>
#include <cmath>
```

to:

```cpp
#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include "hash.h"
#include <cstdio>
#include <cmath>
```

- [ ] **Step 2: Add parallax state and helper functions**

Add this block right after the existing background-caching state (`int gLastBackgroundRealm = -1; uint16_t gSkyColor565 = 0; uint16_t gGroundColor565 = 0;`) and before `void drawBackground(...)`:

```cpp
constexpr int kNumParallaxElements = 6;
struct ParallaxElement { float seedX; float speedPxPerSec; };
ParallaxElement gParallax[kNumParallaxElements];
int gParallaxSeededForRealm = -1;

// (Re)seeds gParallax only when realmIndex changes - mirrors the existing background-color
// cache immediately above, so this isn't recomputed every frame.
void seedParallaxIfNeeded(int realmIndex) {
    if (realmIndex == gParallaxSeededForRealm) return;
    for (int i = 0; i < kNumParallaxElements; ++i) {
        gParallax[i].seedX = hashRange(realmIndex, 100 + i, 0.0f, static_cast<float>(gViewportW));
        gParallax[i].speedPxPerSec = hashRange(realmIndex, 200 + i, 4.0f, 12.0f);
    }
    gParallaxSeededForRealm = realmIndex;
}

// Drifting background dressing behind the platforms: clouds for lower realms, embers for
// mid realms, tiny stars for the highest realms - three visually distinct bands across the
// 16 realms, all driven by the same hash-based determinism this project already uses for
// terrain generation.
void drawParallax(M5Canvas& canvas, int realmIndex, uint32_t nowMs) {
    seedParallaxIfNeeded(realmIndex);
    float elapsedSeconds = static_cast<float>(nowMs) / 1000.0f;
    for (int i = 0; i < kNumParallaxElements; ++i) {
        int x = static_cast<int>(parallaxWrapX(gParallax[i].seedX, gParallax[i].speedPxPerSec,
                                                 elapsedSeconds, static_cast<float>(gViewportW)));
        int y = 20 + (i % 3) * 14; // a few staggered heights near the top of the sky band
        if (realmIndex < 6) {
            canvas.fillEllipse(x, y, 14, 6, TFT_WHITE);
        } else if (realmIndex < 12) {
            canvas.fillCircle(x, y, 3, TFT_ORANGE);
        } else {
            canvas.drawLine(x - 4, y, x + 4, y, TFT_WHITE);
            canvas.drawLine(x, y - 4, x, y + 4, TFT_WHITE);
        }
    }
}

// Deterministic ground tick-marks so the ground band isn't a flat color fill - reuses the
// same 0.75f split drawBackground() uses for where the ground color starts.
void drawGroundTexture(M5Canvas& canvas, int realmIndex) {
    constexpr int kNumTufts = 8;
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    for (int i = 0; i < kNumTufts; ++i) {
        int x = static_cast<int>(hashRange(realmIndex, 300 + i, 0.0f, static_cast<float>(gViewportW)));
        canvas.drawLine(x, groundTop + 2, x, groundTop + 6, TFT_BLACK);
    }
}
```

- [ ] **Step 3: Call the new functions from `renderZoneView()`**

Change:

```cpp
    drawBackground(canvas, state.map.realmIndex);
    int groundY = static_cast<int>(gViewportH * 0.85f);
```

to:

```cpp
    drawBackground(canvas, state.map.realmIndex);
    drawParallax(canvas, state.map.realmIndex, nowMs);
    drawGroundTexture(canvas, state.map.realmIndex);
    int groundY = static_cast<int>(gViewportH * 0.85f);
```

- [ ] **Step 4: Build**

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

Run: `python3 -m platformio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/zone_view.cpp
git commit -m "feat: add parallax background elements and ground texture"
```

---

### Task 10: Update README and final verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: nothing new (documentation only).
- Produces: nothing consumed by other tasks (final task).

- [ ] **Step 1: Update the "Settings: brightness & volume" section**

Change the section (currently titled `### Settings: brightness & volume`, describing the tappable rows) to describe the new boot-only behavior:

```markdown
### Settings: brightness & volume

Two full specs' worth of investigation never found a confirmed root cause for the brightness/
volume rows being unresponsive to touch on real hardware, so the interactive controls were
removed outright rather than continuing to chase it — this app now has **no touch controls at
all**. `gBrightness`/`gVolume` still load from and save to NVS, and still apply via
`M5.Display.setBrightness()`/`M5.Speaker.setVolume()` at boot exactly as before; they're simply
fixed for the session rather than player-adjustable. The freed panel space went to the zone
viewport.
```

- [ ] **Step 2: Add a new section describing skills and the graphics pass**

Insert a new section after the "Secret Realm (raycasting trial mode)" section (or, if that section has since been superseded by the zone/platforms content, after whichever section currently documents the zone's combat) — placement in the file's actual current structure is at the implementer's discretion, but the content must be:

```markdown
### Character skills & graphics pass

The character has a growing, fully-automatic skill kit — 8 realm-gated skills
(`lib/core/skills.h`), one unlocking every 2 realms, firing round-robin among whatever's
currently unlocked while `Fighting`, dealing bonus damage on top of the untouched
`zone_combat` autoattack. No manual activation — this app has no touch controls left, and
skills don't add any.

Combat now shows floating damage numbers, a skill projectile that travels from the character
to the current enemy and bursts on impact (color-coded per skill), and a brief screen shake
reserved for skill impacts. The character has arms, a 4-frame walk cycle, a "casting" pose
synced to skill fires, and a per-realm aura ring. Monsters get tier-distinct silhouettes
(round/spiky/winged) instead of a uniform colored circle. The background now drifts with
parallax clouds/embers/stars (realm-dependent) and deterministic ground texture.

All of this is procedural M5Canvas drawing — no image or audio assets — consistent with the
rest of this project. New deterministic math (skill unlock/cycling, shake/rise/parallax
curves) lives in `lib/core/skills.{h,cpp}` and `lib/core/fx.{h,cpp}` and is unit-tested; the
drawing itself is hardware glue in `src/zone_view.cpp`, unvalidated on real hardware like
everything else in this project.

Design spec: `docs/superpowers/specs/2026-08-28-maplestory-skills-and-graphics-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-28-maplestory-skills-and-graphics.md`
```

- [ ] **Step 3: Update the "Running Tests" section's suite count**

The README's Testing section states a specific suite/case count (`78 test cases across 12 suites` as of the last update). This task adds 2 new test suites (`test_skills`, `test_fx`) and extends 2 existing ones (`test_zone_state` by 7 cases, `test_zone_textures` by 2 cases). Run the actual suite to get the real numbers rather than computing them by hand (the exact starting count may have drifted since the README was last updated):

Run: `python3 -m platformio test -e native -v 2>&1 | tail -30`

Read the final summary line (Unity prints a per-suite and grand total pass count) and update the sentence in `README.md`'s "Running Tests" section to the actual current totals, and add `skills`/`fx` to the module list in that same paragraph (currently: `"...its combat resolution, its autoplay orchestration, its procedural wall textures, and brightness/volume clamping) is hardware-agnostic C++..."`) — append `, the character skill kit, and its FX curves` before the closing parenthesis.

- [ ] **Step 4: Update the "Project Layout" section**

In the `lib/core/` bullet, append after the existing module list: `` `skills` (realm-gated automatic combat skills), `fx` (pure shake/damage-number/parallax curves for zone_view) ``.

In the `src/` bullet, no path changes are needed (`ui.h`/`ui.cpp`/`zone_view.h`/`zone_view.cpp` already listed) — just note in prose that `ui.cpp` no longer has any tappable controls, if the existing sentence there still says otherwise.

- [ ] **Step 5: Final full verification**

Run: `python3 -m platformio test -e native`
Expected: PASS, full suite including all of `test_skills`, `test_fx`, and the extended `test_zone_state`/`test_zone_textures` cases.

Run: `python3 -m platformio run -e esp32p4_pioarduino`
Expected: builds successfully.

Run: `grep -rn "HUD_BUTTON\|hitTestHud\|flashSettingsButton\|drawSettingsHalf\|M5.Touch" src/ lib/`
Expected: no output (confirms Task 1's removal is complete and nothing later reintroduced a reference).

- [ ] **Step 6: Commit**

```bash
git add README.md
git commit -m "docs: document skill system, graphics pass, and touch-control removal"
```
