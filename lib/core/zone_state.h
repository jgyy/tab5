#pragma once
#include <vector>
#include "zone_map.h"
#include "zone_combat.h"
#include "skills.h"

enum class ZonePhase { Walking, Jumping, Fighting, Cleared };

constexpr float kWalkSpeedUnitsPerSec = 1.5f;  // == old trial's kTravelSpeed
constexpr float kEncounterDistance = 0.3f;     // == old trial's kEncounterRadius

constexpr float kMinJumpDuration = 0.3f;   // seconds - floor so even a zero-distance jump reads as a hop
constexpr float kJumpArcHeight = 0.6f;     // world units - cosmetic up-then-down hump added on top of the linear height interpolation
constexpr float kLandingMargin = 0.2f;     // world units - land this far past a platform's x0, not exactly on the edge

constexpr float kMaxPatrolRange = 0.8f;    // world units - largest half-width a monster can patrol
constexpr float kPatrolMargin = 0.3f;      // world units - keeps a patrolling monster clear of its platform's edges
constexpr float kPatrolSpeed = 0.6f;       // world units/sec - full back-and-forth traversal speed

struct JumpArc {
    float fromX = 0.0f, fromY = 0.0f, toX = 0.0f, toY = 0.0f;
    float elapsed = 0.0f;
    float duration = 0.0f; // seconds
};

// Builds a JumpArc from (fromX,fromY) to (toX,toY): duration is however long the horizontal
// distance takes at kWalkSpeedUnitsPerSec, floored at kMinJumpDuration so even a same-x hop off
// a ledge still visibly reads as a jump.
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY);

// Position along the arc at jump-elapsed-time `elapsed` (clamped to [0, arc.duration]): a
// straight-line interpolation between the two endpoints, with a sin(pi*t) hump added to the
// height so the arc reads as "up then down" whether the destination is higher, lower, or level
// with the start. Pure function of `arc` and `elapsed` - does not mutate `arc`.
void jumpArcPosition(const JumpArc& arc, float elapsed, float& outX, float& outY);

// Monster patrol position at zone-elapsed-walking-time `t` (always >= 0): a triangle wave of
// amplitude `patrolRange` centered on `spawnX`, period 4*patrolRange/kPatrolSpeed. Returns
// exactly spawnX at t=0. Pure function - patrol motion has no state beyond elapsed time.
float patrolPositionX(float spawnX, float patrolRange, float t);

// The patrol half-width for a monster on `platform`, clamped so it never reaches the
// platform's edges: min(kMaxPatrolRange, platformWidth/2 - kPatrolMargin), floored at 0 (a
// platform narrower than 2*kPatrolMargin degenerates to no patrol motion rather than a
// negative range).
float patrolRangeForPlatform(const Platform& platform);

struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;                    // height above ground baseline, world units
    float posY = 0.0f;
    int currentPlatformIndex = 0;         // which platform posX/posY sit on while Walking
    ZonePhase phase = ZonePhase::Walking;
    JumpArc jump;                         // only meaningful while phase == Jumping
    float walkingElapsedSeconds = 0.0f;   // drives monster patrol position; frozen outside Walking
    CombatantState player;
    int currentMonsterIndex = -1;
    CombatantState enemy;
    std::vector<bool> monstersDefeated;
    double qiRewardPending = 0.0;
    SkillState skill;             // fires only while Fighting; frozen otherwise, like walkingElapsedSeconds
    int skillFiredThisTick = -1;  // SKILLS[] index fired on the most recent tickZone() call, or -1
};

// Fresh zone at the arena's start (posX = 0), Walking, player stats derived from realmIndex.
ZoneState startZone(const ZoneMap& map, int realmIndex);

// Advances the zone by dtSeconds. While Walking: checks for a live, undefeated monster on the
// current platform within kEncounterDistance of posX (entering Fighting if found), otherwise
// steps posX toward the current platform's far edge at kWalkSpeedUnitsPerSec; reaching that edge
// starts a JumpArc to the next platform (Jumping) or, on the last platform with every monster
// defeated, sets Cleared (qiRewardPending = proposedReward). While Jumping: advances the arc and
// updates posX/posY from it; landing (elapsed >= duration) switches to Walking on the
// destination platform. While Fighting: resolves one combat tick; on monster defeat, marks it
// defeated and returns to Walking; on player defeat, calls restartZone(state,
// currentRealmIndex). No-op once Cleared (call restartZone to loop again). `currentRealmIndex`
// should be the caller's *live* realm index - only consulted at a restart boundary (player
// defeat here), so it can't change stats mid-fight.
void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex);

// Resets to a fresh zone for currentRealmIndex - rebuilds the map (via makeZoneMap) too, not
// just player stats, so a restart after a realm breakthrough actually shows the new realm's
// zone (background palette + monster stats), not the zone it started in.
void restartZone(ZoneState& state, int currentRealmIndex);
