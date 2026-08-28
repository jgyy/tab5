#include <unity.h>
#include "skills.h"

void setUp(void) {}
void tearDown(void) {}

void test_count_unlocked_skills_at_realm_zero(void) {
    TEST_ASSERT_EQUAL_INT(1, countUnlockedSkills(0));
}

void test_count_unlocked_skills_at_realm_seven(void) {
    TEST_ASSERT_EQUAL_INT(4, countUnlockedSkills(7));
}

void test_count_unlocked_skills_at_max_realm(void) {
    TEST_ASSERT_EQUAL_INT(NUM_SKILLS, countUnlockedSkills(15));
}

void test_first_skill_unlocks_at_realm_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, SKILLS[0].unlockRealmIndex);
}

void test_skill_table_cooldowns_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].cooldownSeconds > SKILLS[i - 1].cooldownSeconds);
    }
}

void test_skill_table_multipliers_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].damageMultiplier > SKILLS[i - 1].damageMultiplier);
    }
}

void test_skill_table_unlock_realms_strictly_increase(void) {
    for (int i = 1; i < NUM_SKILLS; ++i) {
        TEST_ASSERT_TRUE(SKILLS[i].unlockRealmIndex > SKILLS[i - 1].unlockRealmIndex);
    }
}

void test_tick_skill_does_not_fire_before_cooldown(void) {
    SkillState state;
    int fired = tickSkill(state, 1.0, 0); // realm 0 -> only skill 0 (3.0s cooldown) unlocked
    TEST_ASSERT_EQUAL_INT(-1, fired);
}

void test_tick_skill_fires_at_cooldown(void) {
    SkillState state;
    int fired = tickSkill(state, 3.0, 0);
    TEST_ASSERT_EQUAL_INT(0, fired);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, state.timer);
}

void test_tick_skill_does_not_immediately_refire(void) {
    SkillState state;
    tickSkill(state, 3.0, 0);
    int fired = tickSkill(state, 1.0, 0); // only 1.0s since the reset, well under 3.0s again
    TEST_ASSERT_EQUAL_INT(-1, fired);
}

void test_tick_skill_round_robins_among_unlocked_skills(void) {
    SkillState state;
    int first = tickSkill(state, 3.0, 4);  // realm 4 -> skills 0,1,2 unlocked; skill 0 cooldown 3.0s
    TEST_ASSERT_EQUAL_INT(0, first);
    int second = tickSkill(state, 3.5, 4); // skill 1's cooldown is 3.5s
    TEST_ASSERT_EQUAL_INT(1, second);
    int third = tickSkill(state, 4.0, 4);  // skill 2's cooldown is 4.0s
    TEST_ASSERT_EQUAL_INT(2, third);
    int fourth = tickSkill(state, 3.0, 4); // wraps back to skill 0
    TEST_ASSERT_EQUAL_INT(0, fourth);
}

void test_tick_skill_single_unlocked_skill_wraps_to_itself(void) {
    SkillState state;
    int fired = tickSkill(state, 3.0, 0); // realm 0 -> only skill 0 unlocked
    TEST_ASSERT_EQUAL_INT(0, fired);
    TEST_ASSERT_EQUAL_INT(0, state.cycleIndex);
}

void test_tick_skill_is_deterministic(void) {
    SkillState a;
    SkillState b;
    int firedA = tickSkill(a, 3.0, 0);
    int firedB = tickSkill(b, 3.0, 0);
    TEST_ASSERT_EQUAL_INT(firedA, firedB);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_count_unlocked_skills_at_realm_zero);
    RUN_TEST(test_count_unlocked_skills_at_realm_seven);
    RUN_TEST(test_count_unlocked_skills_at_max_realm);
    RUN_TEST(test_first_skill_unlocks_at_realm_zero);
    RUN_TEST(test_skill_table_cooldowns_strictly_increase);
    RUN_TEST(test_skill_table_multipliers_strictly_increase);
    RUN_TEST(test_skill_table_unlock_realms_strictly_increase);
    RUN_TEST(test_tick_skill_does_not_fire_before_cooldown);
    RUN_TEST(test_tick_skill_fires_at_cooldown);
    RUN_TEST(test_tick_skill_does_not_immediately_refire);
    RUN_TEST(test_tick_skill_round_robins_among_unlocked_skills);
    RUN_TEST(test_tick_skill_single_unlocked_skill_wraps_to_itself);
    RUN_TEST(test_tick_skill_is_deterministic);
    return UNITY_END();
}
