#pragma once
#include "zone_state.h"

// The discrete, one-shot events a single tickZone() call produced. Each is "did this happen on
// this exact tick", not "is this true right now" - a caller (src/main.cpp's loop()) fires one
// FX/SFX per true flag and then discards the struct.
struct ZoneTickEvents {
    bool zoneRestarted = false;       // the player was defeated and the zone was rebuilt from scratch
    bool enemyHit = false;            // the enemy lost HP (autoattack and/or skill damage)
    bool skillFired = false;          // a skill fired (index is ZoneState::skillFiredThisTick)
    bool monsterDefeated = false;     // a monster (boss or regular) was killed
    bool bossEnrageTriggered = false; // a boss crossed its half-HP enrage threshold
    bool bossDefeated = false;        // the monster killed this tick was a boss
    bool playerHit = false;           // the player lost HP
};

// Derives this tick's discrete events from a before/after ZoneState pair (captured immediately
// around a tickZone() call) plus the enemy/player HP snapshots taken before that call. Pure
// function - no hardware, no timing, fully unit-testable. `wasFighting` is whether `before.phase
// == ZonePhase::Fighting` (passed explicitly since the caller already computes it before
// mutating state).
//
// This lives in lib/core rather than inline in main.cpp's loop() specifically so it can be
// tested: it used to be a block of local booleans in src/, which `pio test -e native` cannot
// reach (build_src_filter = -<*>), and a bug in one of them - a spurious "the zone restarted"
// verdict that silently suppressed every boss-kill FX/SFX - shipped invisibly as a result.
ZoneTickEvents deriveZoneTickEvents(const ZoneState& before, const ZoneState& after,
                                     bool wasFighting, int enemyHpBefore, int playerHpBefore);
