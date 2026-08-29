#include "zone_combat.h"

CombatantState makePlayerCombatant(int realmIndex) {
    CombatantState c;
    c.maxHp = 100 + 40 * realmIndex;
    c.hp = c.maxHp;
    c.attackDamage = 10 + 6 * realmIndex;
    c.attackCooldownSeconds = kPlayerAttackCooldownSeconds;
    c.attackTimer = 0.0f;
    return c;
}

CombatantState makeEnemyCombatant(int maxHp, int damage) {
    CombatantState c;
    c.maxHp = maxHp;
    c.hp = maxHp;
    c.attackDamage = damage;
    c.attackCooldownSeconds = kEnemyAttackCooldownSeconds;
    c.attackTimer = 0.0f;
    return c;
}

namespace {
bool tickOne(CombatantState& attacker, CombatantState& defender, double dtSeconds,
             float damageMultiplier) {
    attacker.attackTimer += static_cast<float>(dtSeconds);
    if (attacker.attackTimer < attacker.attackCooldownSeconds) return false;
    attacker.attackTimer = 0.0f;
    int damage = static_cast<int>(static_cast<float>(attacker.attackDamage) * damageMultiplier);
    defender.hp -= damage;
    if (defender.hp < 0) defender.hp = 0;
    return true;
}
} // namespace

bool tickCombat(CombatantState& player, CombatantState& enemy, double dtSeconds,
                 float incomingDamageMultiplier) {
    bool playerLanded = tickOne(player, enemy, dtSeconds, 1.0f);
    bool enemyLanded = tickOne(enemy, player, dtSeconds, incomingDamageMultiplier);
    return playerLanded || enemyLanded;
}

bool isDefeated(const CombatantState& c) {
    return c.hp <= 0;
}
