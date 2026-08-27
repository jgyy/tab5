#include <unity.h>
#include "zone_combat.h"

void setUp(void) {}
void tearDown(void) {}

void test_player_combatant_scales_with_realm(void) {
    CombatantState r0 = makePlayerCombatant(0);
    CombatantState r3 = makePlayerCombatant(3);
    TEST_ASSERT_EQUAL_INT(100, r0.maxHp);
    TEST_ASSERT_EQUAL_INT(10, r0.attackDamage);
    TEST_ASSERT_EQUAL_INT(220, r3.maxHp);
    TEST_ASSERT_EQUAL_INT(28, r3.attackDamage);
}

void test_is_defeated_when_hp_zero_or_below(void) {
    CombatantState c = makePlayerCombatant(0);
    c.hp = 0;
    TEST_ASSERT_TRUE(isDefeated(c));
    c.hp = 1;
    TEST_ASSERT_FALSE(isDefeated(c));
}

void test_tick_combat_no_attack_before_cooldown_elapses(void) {
    CombatantState player = makePlayerCombatant(0);      // 1.0s cooldown
    CombatantState enemy = makeEnemyCombatant(30, 8);    // 1.2s cooldown
    tickCombat(player, enemy, 0.5);
    TEST_ASSERT_EQUAL_INT(30, enemy.hp);
    TEST_ASSERT_EQUAL_INT(100, player.hp);
}

void test_tick_combat_player_attack_lands_at_cooldown(void) {
    CombatantState player = makePlayerCombatant(0);   // damage 10, cooldown 1.0s
    CombatantState enemy = makeEnemyCombatant(30, 8); // cooldown 1.2s, won't fire yet
    bool landed = tickCombat(player, enemy, 1.0);
    TEST_ASSERT_TRUE(landed);
    TEST_ASSERT_EQUAL_INT(20, enemy.hp);
    TEST_ASSERT_EQUAL_INT(100, player.hp); // enemy hasn't reached its own cooldown yet
}

void test_tick_combat_enemy_damage_clamps_player_hp_at_zero(void) {
    CombatantState player = makePlayerCombatant(0);
    player.hp = 5;
    CombatantState enemy = makeEnemyCombatant(30, 8);
    tickCombat(player, enemy, 1.2); // enemy's cooldown elapses, deals 8 damage
    TEST_ASSERT_EQUAL_INT(0, player.hp);
    TEST_ASSERT_TRUE(isDefeated(player));
}

void test_tick_combat_is_deterministic(void) {
    CombatantState p1 = makePlayerCombatant(1);
    CombatantState e1 = makeEnemyCombatant(50, 14);
    CombatantState p2 = makePlayerCombatant(1);
    CombatantState e2 = makeEnemyCombatant(50, 14);
    for (int i = 0; i < 10; ++i) {
        tickCombat(p1, e1, 0.3);
        tickCombat(p2, e2, 0.3);
    }
    TEST_ASSERT_EQUAL_INT(p1.hp, p2.hp);
    TEST_ASSERT_EQUAL_INT(e1.hp, e2.hp);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_player_combatant_scales_with_realm);
    RUN_TEST(test_is_defeated_when_hp_zero_or_below);
    RUN_TEST(test_tick_combat_no_attack_before_cooldown_elapses);
    RUN_TEST(test_tick_combat_player_attack_lands_at_cooldown);
    RUN_TEST(test_tick_combat_enemy_damage_clamps_player_hp_at_zero);
    RUN_TEST(test_tick_combat_is_deterministic);
    return UNITY_END();
}
