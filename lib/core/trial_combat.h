#pragma once

struct CombatantState {
    int hp = 0;
    int maxHp = 0;
    int attackDamage = 0;
    float attackCooldownSeconds = 1.0f;
    float attackTimer = 0.0f; // counts up to attackCooldownSeconds, then fires and resets to 0
};

constexpr float kPlayerAttackCooldownSeconds = 1.0f;
constexpr float kEnemyAttackCooldownSeconds = 1.2f;

// Player combat stats derived from cultivation progress: maxHp = 100 + 40 * realmIndex,
// attackDamage = 10 + 6 * realmIndex.
CombatantState makePlayerCombatant(int realmIndex);

// Enemy combat stats from a TrialMap::EnemySpawn's maxHp/damage.
CombatantState makeEnemyCombatant(int maxHp, int damage);

// Advances both combatants' attack timers by dtSeconds; whichever timer(s) reach their
// cooldown deal their attackDamage to the other (hp clamped at 0) and reset to 0. Both can
// land in the same call if both cooldowns elapse within dtSeconds. Returns true if at least
// one attack landed this call.
bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds);

bool isDefeated(const CombatantState& c);
