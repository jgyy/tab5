#include <unity.h>
#include "zone_textures.h"

void setUp(void) {}
void tearDown(void) {}

void test_sky_color_is_deterministic(void) {
    RGB a = zoneSkyColor(3);
    RGB b = zoneSkyColor(3);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_different_realms_have_different_sky_colors(void) {
    RGB a = zoneSkyColor(0);
    RGB b = zoneSkyColor(8);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_sky_and_ground_colors_differ(void) {
    RGB sky = zoneSkyColor(2);
    RGB ground = zoneGroundColor(2);
    TEST_ASSERT_TRUE(sky.r != ground.r || sky.g != ground.g || sky.b != ground.b);
}

void test_monster_color_darkens_with_tier(void) {
    RGB tier0 = monsterColor(5, 0);
    RGB tier2 = monsterColor(5, 2);
    // Tier 2 (toughest) is darker overall than tier 0 (weakest) - sum of channels is lower.
    int sum0 = tier0.r + tier0.g + tier0.b;
    int sum2 = tier2.r + tier2.g + tier2.b;
    TEST_ASSERT_TRUE(sum2 < sum0);
}

void test_monster_color_is_deterministic(void) {
    RGB a = monsterColor(6, 1);
    RGB b = monsterColor(6, 1);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_platform_color_is_deterministic(void) {
    RGB a = platformColor(4);
    RGB b = platformColor(4);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_platform_color_differs_from_sky_and_ground(void) {
    RGB platform = platformColor(2);
    RGB sky = zoneSkyColor(2);
    RGB ground = zoneGroundColor(2);
    TEST_ASSERT_TRUE(platform.r != sky.r || platform.g != sky.g || platform.b != sky.b);
    TEST_ASSERT_TRUE(platform.r != ground.r || platform.g != ground.g || platform.b != ground.b);
}

void test_platform_color_differs_across_realms(void) {
    RGB a = platformColor(0);
    RGB b = platformColor(9);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_character_aura_color_is_deterministic(void) {
    RGB a = characterAuraColor(6);
    RGB b = characterAuraColor(6);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_character_aura_color_differs_across_realms(void) {
    RGB a = characterAuraColor(0);
    RGB b = characterAuraColor(10);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_boss_color_is_deterministic(void) {
    RGB a = bossColor(5);
    RGB b = bossColor(5);
    TEST_ASSERT_EQUAL_UINT8(a.r, b.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, b.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, b.b);
}

void test_boss_color_differs_across_realms(void) {
    RGB a = bossColor(0);
    RGB b = bossColor(8);
    TEST_ASSERT_TRUE(a.r != b.r || a.g != b.g || a.b != b.b);
}

void test_boss_color_is_darker_than_the_toughest_regular_tier(void) {
    RGB boss = bossColor(5);
    RGB tier2 = monsterColor(5, 2);
    int bossSum = boss.r + boss.g + boss.b;
    int tier2Sum = tier2.r + tier2.g + tier2.b;
    TEST_ASSERT_TRUE(bossSum < tier2Sum);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sky_color_is_deterministic);
    RUN_TEST(test_different_realms_have_different_sky_colors);
    RUN_TEST(test_sky_and_ground_colors_differ);
    RUN_TEST(test_monster_color_darkens_with_tier);
    RUN_TEST(test_monster_color_is_deterministic);
    RUN_TEST(test_platform_color_is_deterministic);
    RUN_TEST(test_platform_color_differs_from_sky_and_ground);
    RUN_TEST(test_platform_color_differs_across_realms);
    RUN_TEST(test_character_aura_color_is_deterministic);
    RUN_TEST(test_character_aura_color_differs_across_realms);
    RUN_TEST(test_boss_color_is_deterministic);
    RUN_TEST(test_boss_color_differs_across_realms);
    RUN_TEST(test_boss_color_is_darker_than_the_toughest_regular_tier);
    return UNITY_END();
}
