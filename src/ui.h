#pragma once
#include <cstddef>
#include <M5Unified.h>
#include "economy.h"
#include "hittest.h"

// Forward-declared rather than #include "trial_state.h": the only use below is a const
// reference, and pulling in the full definition here (specifically trial_combat.h's
// CombatantState) collides with zone_combat.h's CombatantState of the same name whenever a
// single translation unit needs both this header and the new zone_* stack - as zone_view.cpp
// does. ui.cpp includes trial_state.h directly for the full definition it actually needs.
struct TrialState;

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
int sceneViewportBottom(int screenH);

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
