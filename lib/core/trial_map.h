#pragma once
#include <vector>
#include "raycast.h"

struct Waypoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct EnemySpawn {
    float x = 0.0f;
    float y = 0.0f;
    int maxHp = 0;
    int damage = 0; // damage dealt to the player per attack landed
};

struct TrialMap {
    RaycastMap grid;
    std::vector<Waypoint> route;      // ordered start-to-goal waypoints, in grid-space coordinates
    std::vector<EnemySpawn> enemies;  // encountered along the route in array order
};

// The fixed Phase-1 Secret Realm: a 10x8 ring-corridor maze, a scripted waypoint route walking
// the corridor from the top-left entrance clockwise to the bottom-left goal, and three enemy
// spawns of increasing difficulty placed along that route. Deterministic - identical every call.
TrialMap makeSecretRealmMap();
