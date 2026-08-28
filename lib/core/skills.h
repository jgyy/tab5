#pragma once

// A player's automatic combat skill kit - unlocked by cultivation realm (mirrors how
// GENERATORS in economy.h unlock by realm), fired round-robin during Fighting on top of the
// existing zone_combat autoattack. No manual activation - this project has no touch controls
// left, and skills don't add any.
enum class SkillVisual { Slash, Fireball, FrostShard, LightningBolt, VoidSpike, PhoenixNova, Earthquake, Starfall };

struct SkillDef {
    const char* name;
    int unlockRealmIndex;   // unlocked once GameState.realmIndex >= this
    float cooldownSeconds;
    float damageMultiplier; // bonus damage dealt = player.attackDamage * damageMultiplier
    SkillVisual visual;
};

constexpr int NUM_SKILLS = 8;
extern const SkillDef SKILLS[NUM_SKILLS];

struct SkillState {
    float timer = 0.0f; // counts up toward SKILLS[cycleIndex]'s cooldown
    int cycleIndex = 0;  // round-robin cursor among currently-unlocked skills
};

// Count of SKILLS[i] with unlockRealmIndex <= realmIndex. Always >= 1 for realmIndex >= 0,
// since SKILLS[0].unlockRealmIndex == 0. Scans the full table rather than assuming it stays
// sorted by unlockRealmIndex.
int countUnlockedSkills(int realmIndex);

// Advances state.timer by dtSeconds against SKILLS[state.cycleIndex]'s cooldown. Below
// cooldown: returns -1, only state.timer changes. At/above cooldown: resets state.timer to 0,
// advances state.cycleIndex to the next currently-unlocked skill (wrapping via modulo the
// live unlocked count, not a fixed NUM_SKILLS, so a just-unlocked skill folds into the
// rotation without a discontinuity), and returns the index of the skill that fired.
int tickSkill(SkillState& state, double dtSeconds, int realmIndex);
