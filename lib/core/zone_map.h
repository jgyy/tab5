#pragma once
#include <vector>

struct Platform {
    float x0, x1;  // world-space horizontal extent, x0 < x1
    float y;       // height above the ground baseline, world units (0 = ground)
};

struct MonsterSpawn {
    float x;             // patrol-center x, world units [0, arenaWidth)
    int platformIndex;   // which Platform (index into ZoneMap::platforms) this monster patrols on
    int maxHp;
    int damage;          // damage dealt to the player per attack landed
};

constexpr float kMaxJumpGap = 2.5f;        // largest horizontal gap a single jump can cross
constexpr float kMaxJumpRise = 1.8f;       // largest height change (up or down) a single jump can cross
constexpr float kMaxPlatformHeight = 4.0f; // hard ceiling on any platform's height

// Retained temporarily for src/zone_view.cpp's still-1D screen mapping, which isn't rewritten
// until a later task - dead to zone_map.cpp/zone_state.cpp themselves as of this task. Deleted
// once zone_view.cpp is rewritten to use ZoneMap::arenaWidth instead.
constexpr float kArenaWidth = 10.0f;

struct ZoneMap {
    int realmIndex = 0;                  // which realm's zone this is - drives background/ledge palette
    std::vector<Platform> platforms;     // always 4: [0]=ground, [1..3]=elevated
    std::vector<MonsterSpawn> monsters;  // always 3, one per elevated platform, increasing difficulty
    float arenaWidth = 0.0f;             // == platforms.back().x1
};

// Builds a 4-platform zone for a realm: platform 0 is the ground baseline (x0=0); platforms 1-3
// are elevated, each placed at a deterministic (hash-based) gap/width/height-delta from the
// previous one, with height clamped into [0, kMaxPlatformHeight] and gap/height-delta bounded by
// kMaxJumpGap/kMaxJumpRise - so every generated layout is reachable by a single fixed jump at
// every platform boundary. One monster spawns at the midpoint of each elevated platform (in
// platform order, so difficulty tier 0/1/2 still corresponds to encounter order), with
// maxHp/damage from the same unchanged formula this project already used (baseHp = 30 +
// 20*realmIndex, baseDamage = 8 + 3*realmIndex, tier bonuses {0,20,50} hp / {0,6,14} damage;
// realmIndex == 0 reproduces the exact numbers the flat arena used: 30/8, 50/14, 80/22).
// Deterministic - identical every call for the same realmIndex.
ZoneMap makeZoneMap(int realmIndex);
