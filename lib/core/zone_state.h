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
// speedMultiplier scales the effective travel speed used to compute the arc's duration
// (defaults to 1.0f, matching kWalkSpeedUnitsPerSec exactly) - zone_state.cpp passes the Swift
// Feet trait's live multiplier here so jump duration scales consistently with walk speed.
JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY, float speedMultiplier = 1.0f);

// Position along the arc at jump-elapsed-time `elapsed` (clamped to [0, arc.duration]): a
// straight-line interpolation between the two endpoints, with a sin(pi*t) hump added to the
// height so the arc reads as "up then down" whether the destination is higher, lower, or level
// with the start. Pure function of `arc` and `elapsed` - does not mutate `arc`.
void jumpArcPosition(const JumpArc& arc, float elapsed, float& outX, float& outY);

// Monster patrol position at zone-elapsed-walking-time `t` (always >= 0): a triangle wave of
// amplitude `patrolRange` centered on `spawnX`, period 4*patrolRange/kPatrolSpeed. Returns
// exactly spawnX at t=0. Pure function - patrol motion has no state beyond elapsed time.
float patrolPositionX(float spawnX, float patrolRange, float t);

// The patrol half-width for a monster spawned at `spawnX` on `platform`, clamped so it never
// reaches the platform's edges on either side: min(kMaxPatrolRange, distance from spawnX to the
// nearer of platform.x0+kPatrolMargin / platform.x1-kPatrolMargin), floored at 0. Takes
// `spawnX` (not just the platform) because a monster is no longer always spawned at its
// platform's exact midpoint - clamping symmetrically off platform width alone would let an
// off-center spawn patrol straight past the near edge.
float patrolRangeForPlatform(const Platform& platform, float spawnX);

struct ZoneState {
    ZoneMap map;
    float posX = 0.0f;
    float posY = 0.0f;                    // height above ground baseline, world units
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
    int zoneRunIndex = 0;         // seed `map` was built with; restartZone() bumps this so looping
                                   // the same realm doesn't keep regenerating the same layout
    bool currentEncounterIsBoss = false; // set at the Walking->Fighting transition from the
                                          // engaged spawn's isBoss; cleared when that encounter ends
    bool bossEnraged = false;            // latches true once, never clears mid-fight (boss never heals)
    bool bossJustEnraged = false;        // pulses true on the single tickZone() call enrage triggers;
                                          // reset every call, same contract as skillFiredThisTick
    bool bossJustDefeated = false;       // pulses true on the single tickZone() call a boss dies;
                                          // reset every call
    int playerAutoAttackCount = 0;   // total landed player autoattacks this zone run (Soul Echo cadence)
    float radiantAuraTimerSeconds = 0.0f; // advances only while Fighting; reset in startZone()
    bool undyingWillUsedThisRun = false;  // latches true the first time it saves the player from
                                           // a fatal hit; reset in startZone()
};

constexpr int kBossZoneInterval = 3; // every Nth zone loop is a boss zone
constexpr float kBossEnrageCooldownMultiplier = 0.7f; // ~43% faster attacks once enraged

// True for the 3rd, 6th, 9th... zone loop (1-indexed) - i.e. zoneRunIndex values 2, 5, 8, ....
// zoneRunIndex 0 (the very first zone of a session) is never a boss zone. Callers building a
// ZoneMap (main.cpp's boot call, restartZone() below) use this to decide the isBossZone argument
// to makeZoneMap() - makeZoneMap() itself has no opinion on which loops are boss zones.
bool isBossZoneForRunIndex(int zoneRunIndex);

// Fresh zone at the arena's start (posX = 0), Walking, player stats derived from realmIndex.
// `zoneRunIndex` is the seed `map` was built with (defaults to 0 for the very first zone of a
// session) - stored on the returned state so a later restartZone() knows what to bump next.
ZoneState startZone(const ZoneMap& map, int realmIndex, int zoneRunIndex = 0);

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
// zone (background palette + monster stats), not the zone it started in. Also bumps
// zoneRunIndex and feeds it to makeZoneMap as the new seed, so looping the same realm over and
// over (the common case - realm only changes on a rare breakthrough) reshuffles the terrain and
// monsters each time instead of regenerating the exact same layout.
void restartZone(ZoneState& state, int currentRealmIndex);
