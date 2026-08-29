// test/test_ascension/test_ascension.cpp
#include <unity.h>
#include "ascension.h"
#include "economy.h"

void setUp(void) {}
void tearDown(void) {}

void test_qi_multiplier_for_insight_is_one_at_zero_insight(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.0, qiMultiplierForInsight(0.0));
}

void test_qi_multiplier_for_insight_grows_with_insight(void) {
    double m10 = qiMultiplierForInsight(10.0);
    double m20 = qiMultiplierForInsight(20.0);
    TEST_ASSERT_TRUE(m20 > m10);
    TEST_ASSERT_TRUE(m10 > 1.0);
}

void test_insight_gain_for_zero_or_negative_qi_is_zero(void) {
    TEST_ASSERT_EQUAL_DOUBLE(0.0, insightGainForQi(0.0));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, insightGainForQi(-100.0));
}

void test_insight_gain_grows_with_qi(void) {
    double small = insightGainForQi(1.0e17);
    double large = insightGainForQi(1.0e19);
    TEST_ASSERT_TRUE(large > small);
    TEST_ASSERT_TRUE(small >= 1.0); // the first ascension threshold itself yields a nonzero gain
}

void test_ascension_threshold_grows_with_ascension_count(void) {
    double t0 = ascensionQiThreshold(0);
    double t1 = ascensionQiThreshold(1);
    double t2 = ascensionQiThreshold(2);
    TEST_ASSERT_TRUE(t1 > t0);
    TEST_ASSERT_TRUE(t2 > t1);
}

void test_cannot_ascend_below_max_realm(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 2;
    state.qi = 1.0e30; // absurdly large qi - realm alone must still gate this
    AscensionState ascension;
    TEST_ASSERT_FALSE(canAscend(state, ascension));
}

void test_cannot_ascend_below_qi_threshold(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    // At this magnitude (1e17), subtracting a small constant like 1.0 is swallowed entirely by
    // double precision (a no-op) - use a fractional reduction instead so it's unambiguously below.
    state.qi = ascensionQiThreshold(0) / 2.0;
    AscensionState ascension;
    TEST_ASSERT_FALSE(canAscend(state, ascension));
}

void test_can_ascend_at_max_realm_and_threshold(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = ascensionQiThreshold(0);
    AscensionState ascension;
    TEST_ASSERT_TRUE(canAscend(state, ascension));
}

void test_attempt_ascend_resets_game_state(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    state.qi = ascensionQiThreshold(0) + 500.0;
    state.generatorCounts[0] = 40;
    state.generatorCounts[3] = 12;
    AscensionState ascension;
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, state.qi);
    TEST_ASSERT_EQUAL(0, state.realmIndex);
    TEST_ASSERT_EQUAL(1, state.generatorCounts[0]); // fresh-game default, not zero
    TEST_ASSERT_EQUAL(0, state.generatorCounts[3]);
}

void test_attempt_ascend_grants_insight_and_increments_count(void) {
    GameState state;
    state.realmIndex = NUM_REALMS - 1;
    double qiAtAscension = ascensionQiThreshold(0) + 500.0;
    state.qi = qiAtAscension;
    AscensionState ascension;
    double expectedGain = insightGainForQi(qiAtAscension);
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(expectedGain, ascension.insight);
    TEST_ASSERT_EQUAL_UINT32(1, ascension.ascensionCount);
}

void test_attempt_ascend_fails_and_leaves_state_unchanged_when_not_eligible(void) {
    GameState state;
    state.realmIndex = 5;
    state.qi = 999.0;
    AscensionState ascension;
    bool ok = attemptAscend(state, ascension);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(999.0, state.qi);
    TEST_ASSERT_EQUAL(5, state.realmIndex);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, ascension.insight);
    TEST_ASSERT_EQUAL_UINT32(0, ascension.ascensionCount);
}

void test_repeated_ascensions_require_growing_qi_each_time(void) {
    GameState state;
    AscensionState ascension;
    for (int cycle = 0; cycle < 3; ++cycle) {
        double thresholdBefore = ascensionQiThreshold(ascension.ascensionCount);
        state.realmIndex = NUM_REALMS - 1;
        state.qi = thresholdBefore; // exactly enough to ascend this cycle, not the next
        TEST_ASSERT_TRUE(canAscend(state, ascension));
        bool ok = attemptAscend(state, ascension);
        TEST_ASSERT_TRUE(ok);
        // Immediately re-checking the fresh (reset) state must not allow an instant second
        // ascension - qi is back to 0 and the next threshold only grew.
        TEST_ASSERT_FALSE(canAscend(state, ascension));
    }
    TEST_ASSERT_EQUAL_UINT32(3, ascension.ascensionCount);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_qi_multiplier_for_insight_is_one_at_zero_insight);
    RUN_TEST(test_qi_multiplier_for_insight_grows_with_insight);
    RUN_TEST(test_insight_gain_for_zero_or_negative_qi_is_zero);
    RUN_TEST(test_insight_gain_grows_with_qi);
    RUN_TEST(test_ascension_threshold_grows_with_ascension_count);
    RUN_TEST(test_cannot_ascend_below_max_realm);
    RUN_TEST(test_cannot_ascend_below_qi_threshold);
    RUN_TEST(test_can_ascend_at_max_realm_and_threshold);
    RUN_TEST(test_attempt_ascend_resets_game_state);
    RUN_TEST(test_attempt_ascend_grants_insight_and_increments_count);
    RUN_TEST(test_attempt_ascend_fails_and_leaves_state_unchanged_when_not_eligible);
    RUN_TEST(test_repeated_ascensions_require_growing_qi_each_time);
    return UNITY_END();
}
