#include <unity.h>
#include <cmath>
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
    for (int i = 0; i < 200 && s.phase != ZonePhase::Fighting; ++i) {
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
// posX past an undefeated monster's encounter window, or off a platform's edge without
// triggering the jump/Cleared transition. Without the clamp, this repeatedly overshoots
// all 3 monsters and permanently soft-locks at the last platform's edge in Walking.
void test_large_dt_does_not_skip_monsters(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    bool reachedFirstMonster = false;
    for (int i = 0; i < 2000; ++i) {
        tickZone(s, 1.4, 10.0, 0);
        if (s.phase == ZonePhase::Fighting && s.currentMonsterIndex == 0) {
            reachedFirstMonster = true;
            break;
        }
        // Must never be sitting at the end of the arena, on the last platform, while any
        // monster remains undefeated - that would mean a walk step skipped past one without
        // triggering an encounter (the original bug this regression guards against).
        const Platform& lastPlatform = s.map.platforms.back();
        bool onLastPlatform =
            s.currentPlatformIndex == static_cast<int>(s.map.platforms.size()) - 1;
        bool anyUndefeated =
            !s.monstersDefeated[0] || !s.monstersDefeated[1] || !s.monstersDefeated[2];
        bool softLocked = onLastPlatform && s.posX >= lastPlatform.x1 && anyUndefeated;
        TEST_ASSERT_FALSE(softLocked);
    }
    TEST_ASSERT_TRUE(reachedFirstMonster);
}

void test_jump_arc_starts_at_from_point(void) {
    JumpArc arc = makeJumpArc(1.0f, 0.0f, 4.0f, 2.0f);
    float x, y;
    jumpArcPosition(arc, 0.0f, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, y);
}

void test_jump_arc_ends_at_to_point(void) {
    JumpArc arc = makeJumpArc(1.0f, 0.0f, 4.0f, 2.0f);
    float x, y;
    jumpArcPosition(arc, arc.duration, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, y);
}

void test_jump_arc_midpoint_is_raised_above_linear_interpolation(void) {
    JumpArc arc = makeJumpArc(0.0f, 1.0f, 2.0f, 1.0f); // level start/end
    float x, y;
    jumpArcPosition(arc, arc.duration / 2.0f, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, x); // halfway horizontally
    TEST_ASSERT_TRUE(y > 1.0f); // above the level 1.0 baseline - the cosmetic hump
}

void test_jump_arc_duration_has_a_floor_for_zero_distance(void) {
    JumpArc arc = makeJumpArc(2.0f, 0.0f, 2.0f, 0.0f); // same point (a straight-down/up hop)
    TEST_ASSERT_TRUE(arc.duration >= kMinJumpDuration);
}

void test_patrol_position_returns_spawn_at_time_zero(void) {
    float x = patrolPositionX(5.0f, 0.5f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, x);
}

void test_patrol_position_stays_within_range(void) {
    for (int i = 0; i < 200; ++i) {
        float t = static_cast<float>(i) * 0.05f;
        float x = patrolPositionX(3.0f, 0.5f, t);
        TEST_ASSERT_TRUE(x >= 3.0f - 0.5f - 0.001f);
        TEST_ASSERT_TRUE(x <= 3.0f + 0.5f + 0.001f);
    }
}

void test_patrol_position_is_deterministic(void) {
    float a = patrolPositionX(2.0f, 0.6f, 1.3f);
    float b = patrolPositionX(2.0f, 0.6f, 1.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, a, b);
}

void test_patrol_range_for_platform_stays_within_platform(void) {
    Platform narrow{0.0f, 1.0f, 0.0f}; // width 1.0
    float range = patrolRangeForPlatform(narrow);
    TEST_ASSERT_TRUE(range >= 0.0f);
    TEST_ASSERT_TRUE(range <= (narrow.x1 - narrow.x0) / 2.0f);
}

void test_patrol_range_for_platform_is_capped(void) {
    Platform wide{0.0f, 10.0f, 0.0f}; // wide enough that the cap, not the platform, binds
    float range = patrolRangeForPlatform(wide);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxPatrolRange, range);
}

void test_reaching_non_final_platform_edge_triggers_jumping(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    TEST_ASSERT_EQUAL_INT(0, s.currentPlatformIndex); // still on the "from" platform until landing
}

void test_jumping_lands_on_destination_platform(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase != ZonePhase::Jumping; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    for (int i = 0; i < 50 && s.phase == ZonePhase::Jumping; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(1, s.currentPlatformIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, s.map.platforms[1].y, s.posY);
}

void test_walking_elapsed_seconds_freezes_while_fighting(void) {
    ZoneState s = startZone(makeZoneMap(15), 15); // strong player, quick fights
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    float elapsedAtEngage = s.walkingElapsedSeconds;
    for (int i = 0; i < 5 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 15);
        if (s.phase == ZonePhase::Fighting) {
            TEST_ASSERT_FLOAT_WITHIN(0.0001f, elapsedAtEngage, s.walkingElapsedSeconds);
        }
    }
}

void test_restart_zone_rebuilds_platform_and_position_state(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 200 && s.phase == ZonePhase::Walking; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Jumping);
    restartZone(s, 4);
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(0, s.currentPlatformIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.posY);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.walkingElapsedSeconds);
    ZoneMap expectedMap = makeZoneMap(4);
    TEST_ASSERT_EQUAL_INT((int)expectedMap.platforms.size(), (int)s.map.platforms.size());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expectedMap.platforms[1].y, s.map.platforms[1].y);
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
    RUN_TEST(test_jump_arc_starts_at_from_point);
    RUN_TEST(test_jump_arc_ends_at_to_point);
    RUN_TEST(test_jump_arc_midpoint_is_raised_above_linear_interpolation);
    RUN_TEST(test_jump_arc_duration_has_a_floor_for_zero_distance);
    RUN_TEST(test_patrol_position_returns_spawn_at_time_zero);
    RUN_TEST(test_patrol_position_stays_within_range);
    RUN_TEST(test_patrol_position_is_deterministic);
    RUN_TEST(test_patrol_range_for_platform_stays_within_platform);
    RUN_TEST(test_patrol_range_for_platform_is_capped);
    RUN_TEST(test_reaching_non_final_platform_edge_triggers_jumping);
    RUN_TEST(test_jumping_lands_on_destination_platform);
    RUN_TEST(test_walking_elapsed_seconds_freezes_while_fighting);
    RUN_TEST(test_restart_zone_rebuilds_platform_and_position_state);
    return UNITY_END();
}
