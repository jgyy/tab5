#include <unity.h>
#include "trial_map.h"

void setUp(void) {}
void tearDown(void) {}

void test_map_grid_is_10_wide_8_tall(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(10, m.grid.width);
    TEST_ASSERT_EQUAL_INT(8, m.grid.height);
}

void test_map_border_is_solid(void) {
    TrialMap m = makeSecretRealmMap();
    for (int x = 0; x < m.grid.width; ++x) {
        TEST_ASSERT_TRUE(m.grid.at(x, 0) > 0);
        TEST_ASSERT_TRUE(m.grid.at(x, 7) > 0);
    }
    for (int y = 0; y < m.grid.height; ++y) {
        TEST_ASSERT_TRUE(m.grid.at(0, y) > 0);
        TEST_ASSERT_TRUE(m.grid.at(9, y) > 0);
    }
}

void test_map_has_three_enemies_and_seven_waypoints(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(m.enemies.size()));
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(m.route.size()));
}

void test_route_waypoints_sit_on_open_floor(void) {
    TrialMap m = makeSecretRealmMap();
    for (const auto& wp : m.route) {
        int cellX = static_cast<int>(wp.x);
        int cellY = static_cast<int>(wp.y);
        TEST_ASSERT_EQUAL_INT(0, m.grid.at(cellX, cellY));
    }
}

void test_enemy_spawns_sit_on_open_floor(void) {
    TrialMap m = makeSecretRealmMap();
    for (const auto& e : m.enemies) {
        int cellX = static_cast<int>(e.x);
        int cellY = static_cast<int>(e.y);
        TEST_ASSERT_EQUAL_INT(0, m.grid.at(cellX, cellY));
    }
}

void test_enemies_get_progressively_stronger(void) {
    TrialMap m = makeSecretRealmMap();
    TEST_ASSERT_TRUE(m.enemies[0].maxHp < m.enemies[1].maxHp);
    TEST_ASSERT_TRUE(m.enemies[1].maxHp < m.enemies[2].maxHp);
}

void test_make_secret_realm_map_is_deterministic(void) {
    TrialMap a = makeSecretRealmMap();
    TrialMap b = makeSecretRealmMap();
    TEST_ASSERT_EQUAL_INT(a.grid.width, b.grid.width);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.route.size()), static_cast<int>(b.route.size()));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.route[0].x, b.route[0].x);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_map_grid_is_10_wide_8_tall);
    RUN_TEST(test_map_border_is_solid);
    RUN_TEST(test_map_has_three_enemies_and_seven_waypoints);
    RUN_TEST(test_route_waypoints_sit_on_open_floor);
    RUN_TEST(test_enemy_spawns_sit_on_open_floor);
    RUN_TEST(test_enemies_get_progressively_stronger);
    RUN_TEST(test_make_secret_realm_map_is_deterministic);
    return UNITY_END();
}
