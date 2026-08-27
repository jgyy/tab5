#pragma once
#include <vector>
#include "trial_map.h"
#include "trial_combat.h"

enum class TrialPhase { Traveling, Fighting, Cleared };

constexpr float kTravelSpeed = 1.5f;      // grid units per second
constexpr float kEncounterRadius = 0.3f;  // distance at which a live enemy engages the player
constexpr float kTurnRateRadiansPerSec = 3.14159265f; // 180 deg/sec -> a 90-degree corner turn
                                                        // (the only kind this maze has) takes
                                                        // about half a second to complete.

struct TrialState {
    TrialMap map;
    int currentWaypointIndex = 0;
    float posX = 0.0f;
    float posY = 0.0f;
    float facingRadians = 0.0f;
    TrialPhase phase = TrialPhase::Traveling;
    CombatantState player;
    int currentEnemyIndex = -1;
    CombatantState enemy;
    std::vector<bool> enemiesDefeated;
    double qiRewardPending = 0.0;
};

// Fresh trial at the route's start, already facing toward the first waypoint; player combat
// stats derive from realmIndex.
TrialState startTrial(const TrialMap& map, int realmIndex);

// Advances the trial by dtSeconds. While Traveling: checks for a live, undefeated enemy within
// kEncounterRadius (entering Fighting if found), otherwise eases facingRadians toward the
// current waypoint's direction at kTurnRateRadiansPerSec (never snapping instantly) while
// moving straight toward it, advancing to the next waypoint on arrival, or to Cleared (setting
// qiRewardPending = proposedReward) if the final waypoint is reached with no enemies left
// undefeated. While Fighting: resolves one combat tick; on enemy defeat, marks it defeated and
// returns to Traveling; on player defeat, calls restartTrial(state, currentRealmIndex). No-op
// once Cleared (call restartTrial to loop again). `currentRealmIndex` should be the caller's
// *live* realm index (e.g. GameState.realmIndex), not whatever realm the trial originally
// started at — it's only consulted at a restart boundary (player defeat here), so it can't
// change player stats mid-fight.
void tickTrial(TrialState& state, double dtSeconds, double proposedReward, int currentRealmIndex);

// Resets to the route's start with full player HP (recomputed from currentRealmIndex, not
// whatever realm the trial last started at), no enemies defeated, and qiRewardPending == 0.0,
// keeping `map`.
void restartTrial(TrialState& state, int currentRealmIndex);
