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
