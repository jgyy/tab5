#pragma once
#include <cstddef>
#include <M5Unified.h>
#include "economy.h"
#include "hittest.h"

// Button ids returned by hitTestHud(); -1 means "no button at that point."
enum HudButton {
    HUD_BUTTON_NONE = -1,
    HUD_BUTTON_BREAKTHROUGH = 100,
    HUD_BUTTON_ENTER_SECRET_REALM = 101,
    HUD_BUTTON_RETURN_TO_CULTIVATION = 102,
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

// Fixed strip reserved at the bottom of the screen for the "Return to Cultivation" button
// while the Secret Realm trial view is showing. Shared between ui.cpp (which draws/hit-tests
// the button there) and trial_view.cpp (which centers the raycast view in the remaining space
// above it), so the two can never disagree about where the reserved area sits — the same
// reasoning kRenderSize/kHeaderHeight already apply to the crystal viewport.
constexpr int kReturnButtonHeight = 90;

// Realm milestone that unlocks the "Enter Secret Realm" button on the idle view.
constexpr int kSecretRealmUnlockRealmIndex = 2; // Foundation Establishment

// Must be called once (e.g. from setup()) before the first drawHud() call —
// allocates the offscreen header/HUD sprites sized to `display`.
void initHud(M5GFX& display);

// Draws the full HUD (header bar + generator/breakthrough panel) into offscreen
// sprites, then pushes each to `display` in one blit apiece. Keeps every HUD
// redraw atomic on the physical screen — drawing primitives (fillRect/print/
// etc.) directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker.
// Draws just the header bar (realm/Qi-per-sec, battery) and pushes it. Split out from
// drawHud() so the Secret Realm trial view (which has nothing to show in the generator/
// breakthrough panel) can still keep the economy readout live while showing.
void drawHeader(M5GFX& display, const GameState& state);

void drawHud(M5GFX& display, const GameState& state);

// Hit-tests a touch point against the currently-relevant button set: the idle view's
// generator/breakthrough/enter-secret-realm buttons when `inTrialMode` is false, or just the
// trial view's "Return to Cultivation" button (kReturnButtonHeight strip at the bottom of the
// screen) when true. Matches how drawHud()/renderTrialView() only draw their own mode's
// buttons - the two view modes never share on-screen button real estate.
int hitTestHud(int touchX, int touchY, bool inTrialMode = false);

// Compact K/M/B display formatting for Qi-scale numbers (e.g. "2.2M" instead of
// "2200000"); exposed so main.cpp's welcome-back screen can format consistently
// with the rest of the HUD. `outLen` must cover the worst case (sign + digits +
// suffix + NUL); 24 bytes is comfortably enough for any value this game reaches.
void formatQi(double v, char* out, size_t outLen);
