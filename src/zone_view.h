#pragma once
#include <M5Unified.h>
#include "zone_state.h"

// Allocates the offscreen canvas used for the zone scene. Call once from setup().
void initZoneView(M5GFX& display);

// Renders one frame of the MapleStory-style zone (background, ground, monsters, character)
// into an internal offscreen canvas sized to the actual viewport, then pushes it directly -
// no internal low-res buffer or scaling, since 2D primitive fills are cheap enough to draw at
// native resolution. Player/enemy HP and progress are drawn by ui.cpp's drawHud() instead, not
// here. Does not advance `state` - call tickZone() separately in the game loop.
void renderZoneView(M5GFX& display, const ZoneState& state);

// Short-lived visual flashes at the character/monster position, triggered by main.cpp
// alongside the existing SFX calls at the same combat events. Purely cosmetic timing state
// local to this file - not part of ZoneState, not unit-tested.
void triggerAttackFlash();
void triggerHitFlash();

// Simple procedural SFX (no imported audio assets) - unchanged from the deleted trial_view.
void playAttackSfx();
void playHitSfx();
void playVictorySfx();

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
