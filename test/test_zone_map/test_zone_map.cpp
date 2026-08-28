#include <unity.h>
#include <cmath>
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
        TEST_ASSERT_TRUE(m.monsters[i].x < m.arenaWidth);
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
    for (size_t i = 0; i < a.platforms.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].x0, b.platforms[i].x0);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].y, b.platforms[i].y);
    }
}

void test_always_has_four_platforms_and_three_monsters(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        TEST_ASSERT_EQUAL(4, (int)m.platforms.size());
        TEST_ASSERT_EQUAL(3, (int)m.monsters.size());
    }
}

void test_reachability_invariant_holds_across_realms(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        for (size_t i = 1; i < m.platforms.size(); ++i) {
            float gap = m.platforms[i].x0 - m.platforms[i - 1].x1;
            float riseMagnitude = std::fabs(m.platforms[i].y - m.platforms[i - 1].y);
            TEST_ASSERT_TRUE(gap >= 0.0f);
            TEST_ASSERT_TRUE(gap <= kMaxJumpGap);
            TEST_ASSERT_TRUE(riseMagnitude <= kMaxJumpRise);
        }
    }
}

void test_platform_heights_within_bounds(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        for (const Platform& p : m.platforms) {
            TEST_ASSERT_TRUE(p.y >= 0.0f);
            TEST_ASSERT_TRUE(p.y <= kMaxPlatformHeight);
        }
    }
}

void test_every_realm_has_meaningful_verticality(void) {
    for (int realm = 0; realm < 16; ++realm) {
        ZoneMap m = makeZoneMap(realm);
        float maxElevatedY = 0.0f;
        for (size_t i = 1; i < m.platforms.size(); ++i) {
            if (m.platforms[i].y > maxElevatedY) maxElevatedY = m.platforms[i].y;
        }
        TEST_ASSERT_TRUE(maxElevatedY > 0.5f);
    }
}

void test_monster_platform_index_matches_encounter_order(void) {
    ZoneMap m = makeZoneMap(6);
    TEST_ASSERT_EQUAL(1, m.monsters[0].platformIndex);
    TEST_ASSERT_EQUAL(2, m.monsters[1].platformIndex);
    TEST_ASSERT_EQUAL(3, m.monsters[2].platformIndex);
}

void test_layouts_are_distinct_across_realms(void) {
    ZoneMap a = makeZoneMap(0);
    ZoneMap b = makeZoneMap(8);
    bool anyDifference = false;
    for (size_t i = 0; i < a.platforms.size(); ++i) {
        if (a.platforms[i].y != b.platforms[i].y ||
            (a.platforms[i].x1 - a.platforms[i].x0) != (b.platforms[i].x1 - b.platforms[i].x0)) {
            anyDifference = true;
        }
    }
    TEST_ASSERT_TRUE(anyDifference);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_zero_matches_original_secret_realm_numbers);
    RUN_TEST(test_stats_increase_with_realm_index);
    RUN_TEST(test_monster_positions_are_increasing_and_within_arena);
    RUN_TEST(test_realm_index_is_recorded);
    RUN_TEST(test_deterministic);
    RUN_TEST(test_always_has_four_platforms_and_three_monsters);
    RUN_TEST(test_reachability_invariant_holds_across_realms);
    RUN_TEST(test_platform_heights_within_bounds);
    RUN_TEST(test_every_realm_has_meaningful_verticality);
    RUN_TEST(test_monster_platform_index_matches_encounter_order);
    RUN_TEST(test_layouts_are_distinct_across_realms);
    return UNITY_END();
}
