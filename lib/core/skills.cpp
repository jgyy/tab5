#include "skills.h"

// Cooldown and multiplier both climb monotonically - early skills fire often for a small
// bonus, late skills are rare but hit hard, so the rotation never trivializes the existing
// HP-scaling combat balance in zone_combat.
const SkillDef SKILLS[NUM_SKILLS] = {
    {"Sword Qi Slash",    0,  3.0f, 1.5f, SkillVisual::Slash},
    {"Flame Palm",        2,  3.5f, 1.8f, SkillVisual::Fireball},
    {"Frost Needle",      4,  4.0f, 2.2f, SkillVisual::FrostShard},
    {"Thunderclap Fist",  6,  4.5f, 2.6f, SkillVisual::LightningBolt},
    {"Void Piercer",      8,  5.0f, 3.0f, SkillVisual::VoidSpike},
    {"Phoenix Nova",      10, 5.5f, 3.4f, SkillVisual::PhoenixNova},
    {"Earthquake Palm",   12, 6.0f, 3.8f, SkillVisual::Earthquake},
    {"Starfall Judgment", 14, 6.5f, 4.2f, SkillVisual::Starfall},
};

int countUnlockedSkills(int realmIndex) {
    int count = 0;
    for (int i = 0; i < NUM_SKILLS; ++i) {
        if (SKILLS[i].unlockRealmIndex <= realmIndex) count++;
    }
    return count;
}

int tickSkill(SkillState& state, double dtSeconds, int realmIndex) {
    int unlocked = countUnlockedSkills(realmIndex);
    if (unlocked <= 0) return -1; // defensive; unreachable in practice (SKILLS[0].unlockRealmIndex == 0)
    if (state.cycleIndex >= unlocked) state.cycleIndex = 0;
    const SkillDef& current = SKILLS[state.cycleIndex];
    state.timer += static_cast<float>(dtSeconds);
    if (state.timer < current.cooldownSeconds) return -1;
    state.timer = 0.0f;
    int fired = state.cycleIndex;
    state.cycleIndex = (state.cycleIndex + 1) % unlocked;
    return fired;
}
