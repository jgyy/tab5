#pragma once
#include <vector>

struct MonsterSpawn {
    float x;      // position along the zone's arena, world units [0, kArenaWidth)
    int maxHp;
    int damage;   // damage dealt to the player per attack landed
};

constexpr float kArenaWidth = 10.0f;

struct ZoneMap {
    int realmIndex = 0;                  // which realm's zone this is - drives background palette
    std::vector<MonsterSpawn> monsters;  // encountered in array order, increasing difficulty
};

// Builds the fixed zone for a realm: 3 monster spawns evenly spaced across the arena
// (x = 2.5, 5.0, 7.5), with maxHp/damage scaling from realmIndex at a slower additive rate
// than makePlayerCombatant uses for the player (+20 maxHp / +3 damage per realmIndex, versus
// the player's +40 maxHp / +6 damage), since a zone pits all 3 monsters' aggregate HP/damage
// against a single player HP pool with no recovery between fights - matching the player's
// per-realm growth rate here would make the zone unclearable, plus a fixed per-tier bonus.
// realmIndex == 0 reproduces the exact numbers the old fixed Secret Realm map used
// (30/8, 50/14, 80/22). Deterministic - identical every call for the same realmIndex.
ZoneMap makeZoneMap(int realmIndex);
