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
    bool isBoss = false; // true for the single monster in a boss zone (see makeZoneMap)
};

constexpr float kMaxJumpGap = 2.5f;        // largest horizontal gap a single jump can cross
constexpr float kMaxJumpRise = 1.8f;       // largest height change (up or down) a single jump can cross
constexpr float kMaxPlatformHeight = 4.0f; // hard ceiling on any platform's height
constexpr float kSpawnEdgeMargin = 0.3f;   // a monster spawn stays this far clear of its platform's edges

struct ZoneMap {
    int realmIndex = 0;                  // which realm's zone this is - drives background/ledge palette
    std::vector<Platform> platforms;     // 1 ground + 3-5 elevated, varies per (realmIndex, seed)
    std::vector<MonsterSpawn> monsters;  // 1-2 per elevated platform, varies per (realmIndex, seed)
    float arenaWidth = 0.0f;             // == platforms.back().x1
};

// Builds a zone for a realm and a per-loop `seed`: platform 0 is the ground baseline (x0=0);
// 3-5 elevated platforms follow, each placed at a deterministic (hash-based) gap/width/height-
// delta from the previous one, with height clamped into [0, kMaxPlatformHeight] and
// gap/height-delta bounded by kMaxJumpGap/kMaxJumpRise - so every generated layout is reachable
// by a single fixed jump at every platform boundary, same rule as the original fixed-3-platform
// version. Each elevated platform spawns 1 or 2 monsters (also hash-based) at a randomized
// interior position - no longer always its platform's exact midpoint - with maxHp/damage
// scaling by which platform they're on (baseHp = 30 + 20*realmIndex, +20 per platform tier
// above the first; baseDamage = 8 + 3*realmIndex, +6 per platform tier), so two monsters
// sharing a platform share its difficulty. realmIndex == 0's first monster (always platform 1)
// still reproduces the original 30 hp / 8 damage, since platform 1 always carries zero tier
// bonus. `seed` only affects *structure* (platform/monster layout) - the realm's color palette
// stays keyed to realmIndex alone (zone_textures.h), so re-rolling `seed` every zone loop
// reshuffles the terrain/monsters without changing the realm's "look". Deterministic - identical
// every call for the same (realmIndex, seed) pair; `seed` defaults to 0 so existing single-arg
// call sites keep compiling unchanged.
// `isBossZone`: when true, skips the normal per-platform monster loop entirely and places one
// much-tougher MonsterSpawn (isBoss = true) on the last elevated platform instead — every other
// platform in the zone has no monsters. Terrain generation (platform count/gaps/widths/heights)
// is completely unaffected by this flag. Deciding *which* zoneRunIndex loops are boss zones is
// the caller's job (see zone_state.h's isBossZoneForRunIndex()), not this function's.
ZoneMap makeZoneMap(int realmIndex, int seed = 0, bool isBossZone = false);
