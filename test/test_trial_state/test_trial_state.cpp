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
    tickTrial(s, 0.1, 10.0);
    TEST_ASSERT_TRUE(s.posX > startX); // route[1] is to the right of route[0]
}

void test_reaching_enemy_enters_fighting(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    // Drive many ticks toward the first enemy at (4.5, 1.5); travel speed and encounter
    // radius are internal, so tick generously and assert the phase transition happened.
    for (int i = 0; i < 200 && s.phase == TrialPhase::Traveling; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentEnemyIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_enemy_resumes_traveling(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);
    for (int i = 0; i < 500 && s.phase == TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_TRUE(s.enemiesDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack, well before it could win
    for (int i = 0; i < 500 && s.phase != TrialPhase::Fighting; ++i) {
        tickTrial(s, 0.1, 10.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Fighting);

    // Stop at the exact tick the defeat-triggered reset happens, rather than running a fixed
    // number of further ticks - after the reset the (now full-HP) player immediately walks
    // back toward the same still-undefeated enemy and may re-engage it before an arbitrary
    // fixed tick budget elapses, which would assert on that unrelated rematch instead of the
    // reset this test is actually checking.
    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickTrial(s, 0.1, 10.0);
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
        tickTrial(s, 0.1, 42.0);
    }
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_trial_resets_state(void) {
    TrialMap m = makeSecretRealmMap();
    TrialState s = startTrial(m, 6);
    for (int i = 0; i < 5000 && s.phase != TrialPhase::Cleared; ++i) {
        tickTrial(s, 0.1, 42.0);
    }
    restartTrial(s);
    TEST_ASSERT_TRUE(s.phase == TrialPhase::Traveling);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.enemiesDefeated[0]);
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
    return UNITY_END();
}
