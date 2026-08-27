// test/test_economy/test_economy.cpp
#include <unity.h>
#include "economy.h"

void setUp(void) {}
void tearDown(void) {}

void test_realm_thresholds_increase_monotonically() {
    for (int i = 1; i < NUM_REALMS; ++i) {
        TEST_ASSERT_TRUE(REALM_QI_THRESHOLD[i] > REALM_QI_THRESHOLD[i - 1]);
    }
}

void test_cost_for_generator_grows_by_growth_rate() {
    double cost0 = costForGenerator(0, 0);
    double cost1 = costForGenerator(0, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001, GENERATORS[0].baseCost, cost0);
    TEST_ASSERT_FLOAT_WITHIN(0.001, GENERATORS[0].baseCost * GENERATORS[0].growthRate, cost1);
}

void test_qi_per_second_ignores_locked_generators() {
    GameState state;
    state.realmIndex = 0;
    state.generatorCounts[0] = 2;
    state.generatorCounts[1] = 5; // locked at realm 0, must not count
    double expected = 2 * GENERATORS[0].baseQiPerSecond * realmMultiplier(0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state));
}

void test_qi_per_second_applies_realm_multiplier() {
    GameState state;
    state.realmIndex = 2;
    state.generatorCounts[0] = 1;
    double expected = GENERATORS[0].baseQiPerSecond * realmMultiplier(2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, expected, qiPerSecond(state));
}

void test_tick_adds_correct_amount() {
    GameState state;
    state.generatorCounts[0] = 4;
    double before = state.qi;
    double rate = qiPerSecond(state);
    tick(state, 2.0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, before + rate * 2.0, state.qi);
}

void test_tick_ignores_non_positive_dt() {
    GameState state;
    state.generatorCounts[0] = 4;
    state.qi = 5.0;
    tick(state, 0.0);
    tick(state, -1.0);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, state.qi);
}

void test_cannot_breakthrough_below_threshold() {
    GameState state;
    state.qi = REALM_QI_THRESHOLD[1] - 1.0;
    TEST_ASSERT_FALSE(canBreakthrough(state));
    TEST_ASSERT_FALSE(attemptBreakthrough(state));
    TEST_ASSERT_EQUAL(0, state.realmIndex);
}

void test_breakthrough_spends_threshold_and_advances_realm() {
    GameState state;
    state.qi = REALM_QI_THRESHOLD[1] + 50.0;
    bool ok = attemptBreakthrough(state);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, state.realmIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 50.0, state.qi);
}

void test_cannot_breakthrough_past_final_realm() {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = 1e12;
    TEST_ASSERT_FALSE(canBreakthrough(state));
}

void test_purchase_generator_requires_unlock() {
    GameState state;
    state.qi = 1e9;
    TEST_ASSERT_FALSE(purchaseGenerator(state, 1)); // generator 1 needs realmIndex >= 1
    TEST_ASSERT_EQUAL(0, state.generatorCounts[1]);
}

void test_purchase_generator_requires_affordability() {
    GameState state;
    // Explicit zero baseline: GameState's default now owns 1 unit of generator 0 (a
    // fresh game starts with Breathing Technique), so this test sets the count itself
    // rather than relying on the default to isolate the afford-check being tested.
    state.generatorCounts[0] = 0;
    state.qi = 1.0;
    TEST_ASSERT_FALSE(purchaseGenerator(state, 0));
    TEST_ASSERT_EQUAL(0, state.generatorCounts[0]);
}

void test_purchase_generator_deducts_cost_and_increments_count() {
    GameState state;
    // Explicit zero baseline; see comment above.
    state.generatorCounts[0] = 0;
    state.qi = 1000.0;
    double costBefore = costForGenerator(0, 0);
    bool ok = purchaseGenerator(state, 0);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, state.generatorCounts[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1000.0 - costBefore, state.qi);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_thresholds_increase_monotonically);
    RUN_TEST(test_cost_for_generator_grows_by_growth_rate);
    RUN_TEST(test_qi_per_second_ignores_locked_generators);
    RUN_TEST(test_qi_per_second_applies_realm_multiplier);
    RUN_TEST(test_tick_adds_correct_amount);
    RUN_TEST(test_tick_ignores_non_positive_dt);
    RUN_TEST(test_cannot_breakthrough_below_threshold);
    RUN_TEST(test_breakthrough_spends_threshold_and_advances_realm);
    RUN_TEST(test_cannot_breakthrough_past_final_realm);
    RUN_TEST(test_purchase_generator_requires_unlock);
    RUN_TEST(test_purchase_generator_requires_affordability);
    RUN_TEST(test_purchase_generator_deducts_cost_and_increments_count);
    return UNITY_END();
}
