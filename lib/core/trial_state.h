#pragma once
#include <vector>
#include "trial_map.h"
#include "trial_combat.h"

enum class TrialPhase { Traveling, Fighting, Cleared };

constexpr float kTravelSpeed = 1.5f;      // grid units per second
constexpr float kEncounterRadius = 0.3f;  // distance at which a live enemy engages the player

struct TrialState {
    TrialMap map;
    int realmIndexAtStart = 0;
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

// Fresh trial at the route's start; player combat stats derive from realmIndex.
TrialState startTrial(const TrialMap& map, int realmIndex);

// Advances the trial by dtSeconds. While Traveling: checks for a live, undefeated enemy within
// kEncounterRadius (entering Fighting if found), otherwise moves toward the current waypoint,
// advancing to the next waypoint on arrival, or to Cleared (setting qiRewardPending =
// proposedReward) if the final waypoint is reached with no enemies left undefeated. While
// Fighting: resolves one combat tick; on enemy defeat, marks it defeated and returns to
// Traveling; on player defeat, calls restartTrial. No-op once Cleared (call restartTrial to
// loop again).
void tickTrial(TrialState& state, double dtSeconds, double proposedReward);

// Resets to the route's start with full player HP, no enemies defeated, and
// qiRewardPending == 0.0, keeping `map` and `realmIndexAtStart`.
void restartTrial(TrialState& state);
