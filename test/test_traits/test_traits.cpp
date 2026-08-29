// test/test_traits/test_traits.cpp
#include <unity.h>
#include "traits.h"

void setUp(void) {}
void tearDown(void) {}

void test_trait_table_has_one_entry_per_odd_realm(void) {
    TEST_ASSERT_EQUAL_INT(8, NUM_TRAITS);
    int expectedRealms[8] = {1, 3, 5, 7, 9, 11, 13, 15};
    for (int i = 0; i < NUM_TRAITS; ++i) {
        TEST_ASSERT_EQUAL_INT(expectedRealms[i], TRAITS[i].unlockRealmIndex);
    }
}

void test_trait_unlock_realms_strictly_increase(void) {
    for (int i = 1; i < NUM_TRAITS; ++i) {
        TEST_ASSERT_TRUE(TRAITS[i].unlockRealmIndex > TRAITS[i - 1].unlockRealmIndex);
    }
}

void test_has_iron_skin_gate(void) {
    TEST_ASSERT_FALSE(hasIronSkin(0));
    TEST_ASSERT_TRUE(hasIronSkin(1));
    TEST_ASSERT_TRUE(hasIronSkin(15));
}

void test_has_steady_breath_gate(void) {
    TEST_ASSERT_FALSE(hasSteadyBreath(2));
    TEST_ASSERT_TRUE(hasSteadyBreath(3));
}

void test_has_soul_echo_gate(void) {
    TEST_ASSERT_FALSE(hasSoulEcho(4));
    TEST_ASSERT_TRUE(hasSoulEcho(5));
}

void test_has_execution_gate(void) {
    TEST_ASSERT_FALSE(hasExecution(6));
    TEST_ASSERT_TRUE(hasExecution(7));
}

void test_has_swift_feet_gate(void) {
    TEST_ASSERT_FALSE(hasSwiftFeet(8));
    TEST_ASSERT_TRUE(hasSwiftFeet(9));
}

void test_has_radiant_aura_gate(void) {
    TEST_ASSERT_FALSE(hasRadiantAura(10));
    TEST_ASSERT_TRUE(hasRadiantAura(11));
}

void test_has_undying_will_gate(void) {
    TEST_ASSERT_FALSE(hasUndyingWill(12));
    TEST_ASSERT_TRUE(hasUndyingWill(13));
}

void test_has_empyrean_radiance_gate(void) {
    TEST_ASSERT_FALSE(hasEmpyreanRadiance(14));
    TEST_ASSERT_TRUE(hasEmpyreanRadiance(15));
}

void test_incoming_damage_multiplier_reduces_only_once_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, incomingDamageMultiplier(0));
    TEST_ASSERT_TRUE(incomingDamageMultiplier(1) < 1.0f);
}

void test_regen_per_second_is_zero_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, regenPerSecond(2, 100));
    TEST_ASSERT_TRUE(regenPerSecond(3, 100) > 0.0f);
}

void test_regen_per_second_scales_with_max_hp(void) {
    float regenSmall = regenPerSecond(3, 100);
    float regenLarge = regenPerSecond(3, 500);
    TEST_ASSERT_TRUE(regenLarge > regenSmall);
}

void test_movement_speed_multiplier_is_one_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, movementSpeedMultiplier(8));
    TEST_ASSERT_TRUE(movementSpeedMultiplier(9) > 1.0f);
}

void test_skill_damage_multiplier_is_one_until_unlocked(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, skillDamageMultiplier(14));
    TEST_ASSERT_TRUE(skillDamageMultiplier(15) > 1.0f);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_trait_table_has_one_entry_per_odd_realm);
    RUN_TEST(test_trait_unlock_realms_strictly_increase);
    RUN_TEST(test_has_iron_skin_gate);
    RUN_TEST(test_has_steady_breath_gate);
    RUN_TEST(test_has_soul_echo_gate);
    RUN_TEST(test_has_execution_gate);
    RUN_TEST(test_has_swift_feet_gate);
    RUN_TEST(test_has_radiant_aura_gate);
    RUN_TEST(test_has_undying_will_gate);
    RUN_TEST(test_has_empyrean_radiance_gate);
    RUN_TEST(test_incoming_damage_multiplier_reduces_only_once_unlocked);
    RUN_TEST(test_regen_per_second_is_zero_until_unlocked);
    RUN_TEST(test_regen_per_second_scales_with_max_hp);
    RUN_TEST(test_movement_speed_multiplier_is_one_until_unlocked);
    RUN_TEST(test_skill_damage_multiplier_is_one_until_unlocked);
    return UNITY_END();
}
