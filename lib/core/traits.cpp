#include "traits.h"

const TraitDef TRAITS[NUM_TRAITS] = {
    {"Iron Skin",         1,  "Reduces incoming damage"},
    {"Steady Breath",     3,  "Regenerates HP while fighting"},
    {"Soul Echo",         5,  "Every 4th strike echoes for bonus damage"},
    {"Execution",         7,  "Bonus damage finishing off a weakened foe"},
    {"Swift Feet",        9,  "Faster movement between platforms"},
    {"Radiant Aura",      11, "Periodic aura damage to the current foe"},
    {"Undying Will",      13, "Survives one fatal blow per zone run"},
    {"Empyrean Radiance", 15, "Amplifies all skill damage"},
};

bool hasIronSkin(int realmIndex)         { return realmIndex >= TRAITS[0].unlockRealmIndex; }
bool hasSteadyBreath(int realmIndex)     { return realmIndex >= TRAITS[1].unlockRealmIndex; }
bool hasSoulEcho(int realmIndex)         { return realmIndex >= TRAITS[2].unlockRealmIndex; }
bool hasExecution(int realmIndex)        { return realmIndex >= TRAITS[3].unlockRealmIndex; }
bool hasSwiftFeet(int realmIndex)        { return realmIndex >= TRAITS[4].unlockRealmIndex; }
bool hasRadiantAura(int realmIndex)      { return realmIndex >= TRAITS[5].unlockRealmIndex; }
bool hasUndyingWill(int realmIndex)      { return realmIndex >= TRAITS[6].unlockRealmIndex; }
bool hasEmpyreanRadiance(int realmIndex) { return realmIndex >= TRAITS[7].unlockRealmIndex; }

namespace {
constexpr float kIronSkinDamageMultiplier = 0.9f;        // -10% incoming damage
constexpr float kSteadyBreathRegenFraction = 0.05f;      // 5% of max HP per second
constexpr float kSwiftFeetSpeedMultiplier = 1.3f;        // +30% movement speed
constexpr float kEmpyreanRadianceSkillMultiplier = 1.2f; // +20% skill damage
}

float incomingDamageMultiplier(int realmIndex) {
    return hasIronSkin(realmIndex) ? kIronSkinDamageMultiplier : 1.0f;
}

float regenPerSecond(int realmIndex, int playerMaxHp) {
    if (!hasSteadyBreath(realmIndex)) return 0.0f;
    return static_cast<float>(playerMaxHp) * kSteadyBreathRegenFraction;
}

float movementSpeedMultiplier(int realmIndex) {
    return hasSwiftFeet(realmIndex) ? kSwiftFeetSpeedMultiplier : 1.0f;
}

float skillDamageMultiplier(int realmIndex) {
    return hasEmpyreanRadiance(realmIndex) ? kEmpyreanRadianceSkillMultiplier : 1.0f;
}
