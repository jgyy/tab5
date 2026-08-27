#pragma once
#include <cstddef>
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

// Crystal viewport geometry — shared between main.cpp (which pushes the crystal
// sprite) and ui.cpp (which lays out the HUD around it), so the two can never
// disagree about where the viewport sits. The real M5Tab5 panel is portrait
// (720x1280, confirmed against the fetched M5GFX source — NOT the 1280x720
// landscape shape it's often assumed to be), so the viewport sits centered
// horizontally, below the header, and the HUD panel fills the rest of the
// screen beneath it.
constexpr int kRenderSize = 240; // value locked in during Task 8's FPS tuning
constexpr int kHeaderHeight = 64;
constexpr int kCrystalTopGap = 12;
constexpr int kCrystalBottomGap = 16;

// Must be called once (e.g. from setup()) before the first drawHud() call —
// allocates the offscreen header/HUD sprites sized to `display`.
void initHud(M5GFX& display);

// Draws the full HUD (header bar + generator/breakthrough panel) into offscreen
// sprites, then pushes each to `display` in one blit apiece. Keeps every HUD
// redraw atomic on the physical screen — drawing primitives (fillRect/print/
// etc.) directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker.
void drawHud(M5GFX& display, const GameState& state);
int hitTestHud(int touchX, int touchY);

// Compact K/M/B display formatting for Qi-scale numbers (e.g. "2.2M" instead of
// "2200000"); exposed so main.cpp's welcome-back screen can format consistently
// with the rest of the HUD. `outLen` must cover the worst case (sign + digits +
// suffix + NUL); 24 bytes is comfortably enough for any value this game reaches.
void formatQi(double v, char* out, size_t outLen);
