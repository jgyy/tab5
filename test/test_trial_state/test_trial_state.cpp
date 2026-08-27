#include <unity.h>
#include "trial_state.h"

void setUp(void) {}
void tearDown(void) {}

void test_start_trial_begins_at_route_start_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].x, s.posX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].y, s.posY);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_EQUAL_INT(100, s.player.maxHp); // realmIndex 0
}

void test_tick_moves_toward_next_waypoint(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    float startX = s.posX;
    tickTrial(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.posX > startX); // route[1] is to the right of route[0]
}

void test_reaching_enemy_enters_fighting(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    // Drive many ticks toward the first enemy at (4.5, 1.5); travel speed and encounter
    // radius are internal, so tick generously and assert the phase transition happened.
    for (int i = 0; i < 200 && s.phase == TrialPhase::Traveling; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentEnemyIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_enemy_resumes_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    for (int i = 0; i < 500 && s.phase == TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_TRUE(s.enemiesDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack, well before it could win
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);

    // Stop at the exact tick the defeat-triggered reset happens, rather than running a fixed
    // number of further ticks - after the reset the (now full-HP) player immediately walks
    // back toward the same still-undefeated enemy and may re-engage it before an arbitrary
    // fixed tick budget elapses, which would assert on that unrelated rematch instead of the
    // reset this test is actually checking.
    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
        if (s.phase == TrialPhase::Traveling) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, m.route[0].x, s.posX);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
}

void test_clearing_all_enemies_and_reaching_goal_sets_reward(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // strong enough to one-shot-ish every enemy
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0, 6);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_trial_resets_state(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6);
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0, 6);
    }
    restartTrial(s, 6);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.enemiesDefeated[0]);
}

// A small synthetic map (not the real Secret Realm maze) isolating exactly one 90-degree turn,
// with no enemies, so the turning behavior below can be asserted without depending on the real
// maze's encounter timing.
TrialMap makeStraightThenTurnMap() {
    TrialMap m;
    m.grid.width = 10;
    m.grid.height = 10;
    m.grid.cells.assign(100, 0); // all open floor - tickTrial never checks wall collision
    m.route = {
        {1.0f, 1.0f}, // start
        {5.0f, 1.0f}, // due east of start
        {5.0f, 5.0f}, // due south of the previous waypoint: a clean 90-degree turn
    };
    m.enemies = {};
    return m;
}

void test_turn_is_gradual_not_instant(void) {
    TrialMap m = makeStraightThenTurnMap();
    TrialState s = startTrial(m, 0);
    // Walk the first (east) leg to completion; facing stays ~0 rad (east) the whole way, since
    // that leg needs no turning.
    for (int i = 0; i < 200 && s.currentWaypointIndex < 2; ++i) {
        tickTrial(s, 0.05, 10.0, 0);
    }
    TEST_ASSERT_EQUAL_INT(2, s.currentWaypointIndex); // now targeting (5,5): due south - a 90-degree turn
    float facingAtTurnStart = s.facingRadians;

    tickTrial(s, 0.05, 10.0, 0); // one small tick into the turn
    float facingAfterOneTick = s.facingRadians;
    float target = 1.57079633f; // south: atan2(+dy, 0) = pi/2

    TEST_ASSERT_TRUE(facingAfterOneTick > facingAtTurnStart); // it started turning...
    TEST_ASSERT_TRUE((target - facingAfterOneTick) > 0.1f);   // ...but hasn't snapped straight to the target

    // Keep ticking; the turn should fully converge well before the character reaches the
    // second waypoint (a 4-unit leg at kTravelSpeed=1.5 units/s takes ~2.7s, far more than the
    // ~0.5s this 90-degree turn needs at kTurnRateRadiansPerSec = pi rad/s).
    for (int i = 0; i < 40; ++i) {
        tickTrial(s, 0.05, 10.0, 0);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target, s.facingRadians);
}

void test_restart_uses_current_realm_index_not_frozen_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0); // started weak (realm 0)
    // Simulate the hidden cultivation economy having advanced to realm 4 by the time this
    // restart happens.
    restartTrial(s, 4);
    CombatantState expected = makePlayerCombatant(4);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
    TEST_ASSERT_EQUAL_INT(expected.attackDamage, s.player.attackDamage);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.hp); // full HP at the new cap after restart
}

void test_tick_trial_restart_on_defeat_uses_passed_in_realm_index(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0, 5); // economy has since advanced to realm 5 by the time of defeat
        if (s.phase == TrialPhase::Traveling) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    CombatantState expected = makePlayerCombatant(5);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_start_trial_begins_at_route_start_traveling);
    RUN_TEST(test_tick_moves_toward_next_waypoint);
    RUN_TEST(test_reaching_enemy_enters_fighting);
    RUN_TEST(test_defeating_enemy_resumes_traveling);
    RUN_TEST(test_player_defeat_resets_to_start);
    RUN_TEST(test_clearing_all_enemies_and_reaching_goal_sets_reward);
    RUN_TEST(test_restart_trial_resets_state);
    RUN_TEST(test_turn_is_gradual_not_instant);
    RUN_TEST(test_restart_uses_current_realm_index_not_frozen_start);
    RUN_TEST(test_tick_trial_restart_on_defeat_uses_passed_in_realm_index);
    return UNITY_END();
}
