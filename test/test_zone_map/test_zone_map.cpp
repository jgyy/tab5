#include <unity.h>
#include "zone_map.h"

void setUp(void) {}
void tearDown(void) {}

void test_realm_zero_matches_original_secret_realm_numbers(void) {
    ZoneMap m = makeZoneMap(0);
    TEST_ASSERT_EQUAL(3, (int)m.monsters.size());
    TEST_ASSERT_EQUAL(30, m.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(8, m.monsters[0].damage);
    TEST_ASSERT_EQUAL(50, m.monsters[1].maxHp);
    TEST_ASSERT_EQUAL(14, m.monsters[1].damage);
    TEST_ASSERT_EQUAL(80, m.monsters[2].maxHp);
    TEST_ASSERT_EQUAL(22, m.monsters[2].damage);
}

void test_stats_increase_with_realm_index(void) {
    ZoneMap low = makeZoneMap(0);
    ZoneMap high = makeZoneMap(5);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(high.monsters[i].maxHp > low.monsters[i].maxHp);
        TEST_ASSERT_TRUE(high.monsters[i].damage > low.monsters[i].damage);
    }
}

void test_monster_positions_are_increasing_and_within_arena(void) {
    ZoneMap m = makeZoneMap(3);
    TEST_ASSERT_TRUE(m.monsters[0].x < m.monsters[1].x);
    TEST_ASSERT_TRUE(m.monsters[1].x < m.monsters[2].x);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(m.monsters[i].x >= 0.0f);
        TEST_ASSERT_TRUE(m.monsters[i].x < kArenaWidth);
    }
}

void test_realm_index_is_recorded(void) {
    ZoneMap m = makeZoneMap(7);
    TEST_ASSERT_EQUAL(7, m.realmIndex);
}

void test_deterministic(void) {
    ZoneMap a = makeZoneMap(4);
    ZoneMap b = makeZoneMap(4);
    TEST_ASSERT_EQUAL(a.monsters[0].maxHp, b.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(a.monsters[2].damage, b.monsters[2].damage);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_zero_matches_original_secret_realm_numbers);
    RUN_TEST(test_stats_increase_with_realm_index);
    RUN_TEST(test_monster_positions_are_increasing_and_within_arena);
    RUN_TEST(test_realm_index_is_recorded);
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
