#pragma once
#include <M5Unified.h>
#include "trial_state.h"

// Allocates the offscreen canvas used for the raycast view. Call once from setup().
void initTrialView(M5GFX& display);

// Renders one frame of the Secret Realm trial (raycast walls/floor/ceiling, billboarded
// sprites for every undefeated enemy depth-tested against the wall raycast, and a slim HUD
// strip showing either the enemy HP bar while Fighting or route progress otherwise) into an
// internal offscreen canvas, then displays it scaled up to cover most of `display` via
// pushRotateZoom. Does not advance `state` - call tickTrial() separately in the game loop.
void renderTrialView(M5GFX& display, const TrialState& state);

// Simple procedural SFX (no imported audio assets), played by main.cpp at the relevant
// combat/clear transitions.
void playAttackSfx();
void playHitSfx();
void playVictorySfx();
