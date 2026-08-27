#pragma once
#include <vector>
#include "zone_map.h"
#include "zone_combat.h"

enum class ZonePhase { Walking, Fighting, Cleared };

constexpr float kWalkSpeedUnitsPerSec = 1.5f;  // == old trial's kTravelSpeed
constexpr float kEncounterDistance = 0.3f;     // == old trial's kEncounterRadius

struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;                 // 0..kArenaWidth
    ZonePhase phase = ZonePhase::Walking;
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
};

// Fresh zone at the arena's start (posX = 0), Walking, player stats derived from realmIndex.
ZoneState startZone(const ZoneMap& map, int realmIndex);

// Advances the zone by dtSeconds. While Walking: checks for a live, undefeated monster within
// kEncounterDistance of posX (entering Fighting if found), otherwise steps posX toward
// kArenaWidth at kWalkSpeedUnitsPerSec; reaching kArenaWidth with every monster defeated sets
// Cleared (qiRewardPending = proposedReward). While Fighting: resolves one combat tick; on
// monster defeat, marks it defeated and returns to Walking; on player defeat, calls
// restartZone(state, currentRealmIndex). No-op once Cleared (call restartZone to loop again).
// `currentRealmIndex` should be the caller's *live* realm index - only consulted at a restart
// boundary (player defeat here), so it can't change stats mid-fight.
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);

// Resets to a fresh zone for currentRealmIndex - rebuilds the map (via makeZoneMap) too, not
// just player stats, so a restart after a realm breakthrough actually shows the new realm's
// zone (background palette + monster stats), not the zone it started in.
void restartZone(ZoneState& state, int currentRealmIndex);
