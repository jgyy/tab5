#include <unity.h>
#include "zone_state.h"

void setUp(void) {}
void tearDown(void) {}

void test_start_zone_begins_walking_at_arena_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(100, s.player.maxHp); // realmIndex 0
}

void test_tick_moves_toward_far_end(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    float startX = s.posX;
    tickZone(s, 0.1, 10.0, 0);
    TEST_ASSERT_TRUE(s.posX > startX);
}

void test_reaching_monster_enters_fighting(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    TEST_ASSERT_EQUAL_INT(0, s.currentMonsterIndex);
    TEST_ASSERT_EQUAL_INT(30, s.enemy.maxHp);
}

void test_defeating_monster_resumes_walking(void) {
    ZoneMap m = makeZoneMap(15);
    ZoneState s = startZone(m, 15); // high realm -> strong player, fast kill
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    for (int i = 0; i < 500 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_TRUE(s.monstersDefeated[0]);
}

void test_player_defeat_resets_to_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack, well before it could win
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickZone(s, 0.1, 10.0, 0);
        if (s.phase == ZonePhase::Walking) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
}

void test_clearing_all_monsters_and_reaching_end_sets_reward(void) {
    ZoneMap m = makeZoneMap(15);
    ZoneState s = startZone(m, 15); // strong enough to one-shot-ish every monster
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared; ++i) {
        tickZone(s, 0.1, 42.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 42.0, s.qiRewardPending);
}

void test_restart_zone_resets_state(void) {
    ZoneMap m = makeZoneMap(15);
    ZoneState s = startZone(m, 15);
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared; ++i) {
        tickZone(s, 0.1, 42.0, 15);
    }
    restartZone(s, 15);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, s.qiRewardPending);
    TEST_ASSERT_FALSE(s.monstersDefeated[0]);
}

void test_restart_uses_current_realm_index_not_frozen_start(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0); // started weak (realm 0)
    // Simulate the hidden cultivation economy having advanced to realm 4 by the time this
    // restart happens.
    restartZone(s, 4);
    CombatantState expected = makePlayerCombatant(4);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
    TEST_ASSERT_EQUAL_INT(expected.attackDamage, s.player.attackDamage);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.hp); // full HP at the new cap after restart
}

void test_tick_zone_restart_on_defeat_uses_passed_in_realm_index(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    s.player.hp = 1; // dies on the enemy's first landed attack
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);

    bool resetDetected = false;
    for (int i = 0; i < 50; ++i) {
        tickZone(s, 0.1, 10.0, 5); // economy has since advanced to realm 5 by the time of defeat
        if (s.phase == ZonePhase::Walking) {
            resetDetected = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(resetDetected);
    CombatantState expected = makePlayerCombatant(5);
    TEST_ASSERT_EQUAL_INT(expected.maxHp, s.player.maxHp);
}

void test_restart_zone_rebuilds_map_for_current_realm(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0); // started weak, realm-0 zone
    restartZone(s, 4); // hidden economy has since advanced to realm 4
    TEST_ASSERT_EQUAL_INT(4, s.map.realmIndex);
    ZoneMap expectedMap = makeZoneMap(4);
    TEST_ASSERT_EQUAL_INT(expectedMap.monsters[0].maxHp, s.map.monsters[0].maxHp);
    TEST_ASSERT_EQUAL_INT(expectedMap.monsters[2].damage, s.map.monsters[2].damage);
}

// Regression guard for the balance retune: a matched-realm zone (the only pairing production
// ever produces - map and player built from the same realmIndex) must actually be clearable,
// even at the highest realm where monster stats are largest.
void test_matched_realm_zone_is_clearable_at_high_realm(void) {
    ZoneState s = startZone(makeZoneMap(15), 15);
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared; ++i) {
        tickZone(s, 0.1, 42.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Cleared);
}

// Regression guard: a large dt (e.g. a frame hitch) must never let the Walking step carry
// posX past an undefeated monster's encounter window. Without the clamp, this repeatedly
// overshoots all 3 monsters and permanently soft-locks at posX == kArenaWidth in Walking.
void test_large_dt_does_not_skip_monsters(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    bool reachedFirstMonster = false;
    for (int i = 0; i < 1000; ++i) {
        tickZone(s, 1.4, 10.0, 0);
        if (s.phase == ZonePhase::Fighting && s.currentMonsterIndex == 0) {
            reachedFirstMonster = true;
            break;
        }
        // Must never park at the end of the arena while a monster remains undefeated -
        // that would mean the walk step skipped past it without triggering an encounter.
        bool softLocked = (s.posX >= kArenaWidth) && !s.monstersDefeated[0];
        TEST_ASSERT_FALSE(softLocked);
    }
    TEST_ASSERT_TRUE(reachedFirstMonster);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_start_zone_begins_walking_at_arena_start);
    RUN_TEST(test_tick_moves_toward_far_end);
    RUN_TEST(test_reaching_monster_enters_fighting);
    RUN_TEST(test_defeating_monster_resumes_walking);
    RUN_TEST(test_player_defeat_resets_to_start);
    RUN_TEST(test_clearing_all_monsters_and_reaching_end_sets_reward);
    RUN_TEST(test_restart_zone_resets_state);
    RUN_TEST(test_restart_uses_current_realm_index_not_frozen_start);
    RUN_TEST(test_tick_zone_restart_on_defeat_uses_passed_in_realm_index);
    RUN_TEST(test_restart_zone_rebuilds_map_for_current_realm);
    RUN_TEST(test_matched_realm_zone_is_clearable_at_high_realm);
    RUN_TEST(test_large_dt_does_not_skip_monsters);
    return UNITY_END();
}
