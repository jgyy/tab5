#pragma once

// A player's automatic, passive Realm Identity trait kit - one per *odd* cultivation realm,
// filling the gap SKILLS[] (skills.h) leaves there (skills unlock only on even realms). Unlike
// skills, every unlocked trait is always "on" - there's no shared rotation slot to compete for.
// No manual activation, same reasoning as skills.h.
struct TraitDef {
    const char* name;
    int unlockRealmIndex; // always odd: 1, 3, 5, 7, 9, 11, 13, 15
    const char* description;
};

constexpr int NUM_TRAITS = 8;
extern const TraitDef TRAITS[NUM_TRAITS];

// TRAITS[i].unlockRealmIndex is the single source of truth each of these reads from - no realm
// number is hardcoded a second time here.
bool hasIronSkin(int realmIndex);          // realm >= 1
bool hasSteadyBreath(int realmIndex);      // realm >= 3
bool hasSoulEcho(int realmIndex);          // realm >= 5
bool hasExecution(int realmIndex);         // realm >= 7
bool hasSwiftFeet(int realmIndex);         // realm >= 9
bool hasRadiantAura(int realmIndex);       // realm >= 11
bool hasUndyingWill(int realmIndex);       // realm >= 13
bool hasEmpyreanRadiance(int realmIndex);  // realm >= 15

// 1.0f unless hasIronSkin(); then a flat reduction. Does not scale further with higher realms -
// one trait, one fixed effect, same posture as an unlocked skill.
float incomingDamageMultiplier(int realmIndex);

// 0.0f unless hasSteadyBreath(); then a flat HP/sec regen amount, proportional to playerMaxHp.
float regenPerSecond(int realmIndex, int playerMaxHp);

// 1.0f unless hasSwiftFeet(); then a flat walk/jump speed multiplier.
float movementSpeedMultiplier(int realmIndex);

// 1.0f unless hasEmpyreanRadiance(); then a flat skill-damage multiplier - the capstone trait.
float skillDamageMultiplier(int realmIndex);

// Soul Echo / Execution / Radiant Aura / Undying Will have no standalone multiplier accessor -
// their thresholds/magnitudes are named constants, applied directly in zone_state.cpp guarded
// by their has*() gate above, mirroring how kBossEnrageCooldownMultiplier (zone_state.h) is a
// named constant applied directly in zone_state.cpp rather than routed through an accessor.
constexpr int kSoulEchoInterval = 4;               // every 4th landed player autoattack echoes
constexpr float kSoulEchoBonusMultiplier = 1.0f;   // bonus damage = player.attackDamage * this
constexpr float kExecutionHpFraction = 0.2f;       // execute bonus triggers at/below 20% enemy HP
constexpr float kExecutionBonusMultiplier = 0.5f;  // bonus damage = player.attackDamage * this
constexpr float kRadiantAuraIntervalSeconds = 2.0f;      // aura ticks once every 2s while Fighting
constexpr float kRadiantAuraDamageMultiplier = 0.3f;     // tick damage = player.attackDamage * this
