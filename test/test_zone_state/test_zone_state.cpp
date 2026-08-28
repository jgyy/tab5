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

// A zone can now roll several monsters per platform (up to ~10 total), so without a heal
// between individual fights, chip damage would accumulate across the whole run instead of each
// fight being its own "can I beat this one enemy" test - the design intent stated in this
// module's header. Full-healing on every kill keeps that intent true regardless of monster count.
void test_defeating_monster_heals_player_to_full(void) {
    ZoneMap m = makeZoneMap(0);
    ZoneState s = startZone(m, 0);
    for (int i = 0; i < 200 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.player.hp = 50;  // took damage earlier in the fight, comfortably survives a stray hit
    s.enemy.hp = 1;     // dies on the very next landed autoattack
    for (int i = 0; i < 50 && s.phase == ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_EQUAL_INT(s.player.maxHp, s.player.hp);
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
    // First monster (always platform 1, zero tier bonus) reflects realm 4's base formula
    // regardless of which seed restartZone() happened to roll.
    TEST_ASSERT_EQUAL_INT(30 + 20 * 4, s.map.monsters[0].maxHp);
    TEST_ASSERT_EQUAL_INT(8 + 3 * 4, s.map.monsters[0].damage);
}

// How many of `trials` freshly-seeded matched-realm zones (layout and player both built from
// `realm`) clear on their first attempt, with no retries. Stops each trial as soon as its
// attempt ends, one way or the other: Cleared, or restartZone() firing on defeat (detected via
// zoneRunIndex changing from the seed that trial started at) - without this, tickZone's
// built-in auto-restart would just keep rerolling fresh layouts forever and never distinguish
// "this seed's layout cleared" from "some later reroll eventually cleared".
int clearedOnFirstAttemptCount(int realm, int trials) {
    int cleared = 0;
    for (int seed = 0; seed < trials; ++seed) {
        ZoneState s = startZone(makeZoneMap(realm, seed), realm, seed);
        for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared && s.zoneRunIndex == seed; ++i) {
            tickZone(s, 0.1, 42.0, realm);
        }
        if (s.phase == ZonePhase::Cleared && s.zoneRunIndex == seed) cleared++;
    }
    return cleared;
}

// Regression guard for the balance retune: a matched-realm zone must actually be clearable on
// its first attempt, even at the highest realm where monster stats are largest, regardless of
// which layout a given seed happens to roll (elevated platform count and monster count/spacing
// per platform both vary now - see zone_map.h). Sweeps many seeds rather than pinning a single
// one: a single fixed seed only proves that seed's particular layout is beatable, not the
// underlying difficulty curve - exactly how the previous version of this test kept passing
// while most other seeds' layouts had become unclearable (see the heal-on-kill fix in
// tickZone() this test now exercises).
void test_matched_realm_zone_is_clearable_at_high_realm(void) {
    constexpr int kTrials = 30;
    int cleared = clearedOnFirstAttemptCount(15, kTrials);
    TEST_ASSERT_TRUE(cleared >= kTrials * 9 / 10); // >=90%, matching the original design's ~100% baseline
}

// Same guard at the *low* end of the realm range: a low-realm character has the weakest combat
// stats, so it's the case most exposed by the difficulty-tier cap in zone_map.cpp (without that
// cap, a 5-elevated-platform zone's platform-4 monster is flatly unbeatable solo at low realms,
// no matter how much healing happens between fights - this isn't cumulative fatigue, it's a
// single encounter stronger than the player can ever be at that realm).
void test_matched_realm_zone_is_clearable_at_low_realm(void) {
    constexpr int kTrials = 30;
    int cleared = clearedOnFirstAttemptCount(1, kTrials);
    TEST_ASSERT_TRUE(cleared >= kTrials * 9 / 10);
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
        // triggering an encounter (the original bug this regression guards against). Checked
        // generically over however many monsters this layout rolled, since the count now varies.
        const Platform& lastPlatform = s.map.platforms.back();
        bool onLastPlatform =
            s.currentPlatformIndex == static_cast<int>(s.map.platforms.size()) - 1;
        bool anyUndefeated = false;
        for (bool defeated : s.monstersDefeated) {
            if (!defeated) { anyUndefeated = true; break; }
        }
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
    float range = patrolRangeForPlatform(narrow, 0.5f); // spawn at the midpoint
    TEST_ASSERT_TRUE(range >= 0.0f);
    TEST_ASSERT_TRUE(range <= (narrow.x1 - narrow.x0) / 2.0f);
}

void test_patrol_range_for_platform_is_capped(void) {
    Platform wide{0.0f, 10.0f, 0.0f}; // wide enough that the cap, not the platform, binds
    float range = patrolRangeForPlatform(wide, 5.0f); // spawn at the midpoint
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kMaxPatrolRange, range);
}

// Regression guard: a spawn is no longer always at its platform's exact midpoint (zone_map.cpp
// now places monsters anywhere in a platform's interior), so the range must clamp off the
// *nearer* edge to the actual spawn point, not off platform width alone - otherwise an
// off-center spawn patrols straight past the near edge and reads as floating off the ledge.
void test_patrol_range_for_platform_clamps_to_nearer_edge_of_off_center_spawn(void) {
    Platform p{0.0f, 3.0f, 0.0f}; // width 3.0, well past 2*kMaxPatrolRange so the cap never binds
    float spawnNearLeftEdge = 0.5f; // only 0.5 - kPatrolMargin of interior to its left
    float range = patrolRangeForPlatform(p, spawnNearLeftEdge);
    TEST_ASSERT_TRUE(spawnNearLeftEdge - range >= p.x0 + kPatrolMargin - 0.001f);
    TEST_ASSERT_TRUE(spawnNearLeftEdge + range <= p.x1 - kPatrolMargin + 0.001f);
    // The near (left) edge is what actually binds here, not the symmetric platform-width formula
    // the old implementation used (which would have returned 3.0/2 - kPatrolMargin, clamped to
    // kMaxPatrolRange - both larger than what's actually safe on the left).
    TEST_ASSERT_TRUE(range < spawnNearLeftEdge - (p.x0 + kPatrolMargin) + 0.01f);
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
    TEST_ASSERT_EQUAL_INT(4, s.map.realmIndex);
    TEST_ASSERT_TRUE(s.map.platforms.size() >= 4);
    TEST_ASSERT_TRUE(s.map.platforms.size() <= 6);
}

// The core fix this feature is for, exercised through the ZoneState API (not just raw
// makeZoneMap): looping the same realm over and over must NOT keep handing back the exact same
// platform/monster layout.
void test_restart_zone_reshuffles_layout_across_successive_loops(void) {
    ZoneState s = startZone(makeZoneMap(2), 2);
    bool anyDifference = false;
    float firstMonsterX = s.map.monsters[0].x;
    size_t firstMonsterCount = s.map.monsters.size();
    for (int i = 0; i < 30; ++i) {
        restartZone(s, 2);
        if (s.map.monsters.size() != firstMonsterCount) anyDifference = true;
        if (std::fabs(s.map.monsters[0].x - firstMonsterX) > 0.001f) anyDifference = true;
    }
    TEST_ASSERT_TRUE(anyDifference);
}

void test_start_zone_defaults_zone_run_index_to_zero(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    TEST_ASSERT_EQUAL_INT(0, s.zoneRunIndex);
}

void test_start_zone_records_explicit_zone_run_index(void) {
    ZoneState s = startZone(makeZoneMap(0, 5), 0, 5);
    TEST_ASSERT_EQUAL_INT(5, s.zoneRunIndex);
}

void test_restart_zone_increments_zone_run_index(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    restartZone(s, 0);
    TEST_ASSERT_EQUAL_INT(1, s.zoneRunIndex);
    restartZone(s, 0);
    TEST_ASSERT_EQUAL_INT(2, s.zoneRunIndex);
}

void test_start_zone_resets_skill_state(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.skill.timer);
    TEST_ASSERT_EQUAL_INT(0, s.skill.cycleIndex);
    TEST_ASSERT_EQUAL_INT(-1, s.skillFiredThisTick);
}

void test_skill_timer_frozen_while_walking(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    float timerAtStart = s.skill.timer;
    tickZone(s, 0.1, 10.0, 0); // still Walking on the first tick of a fresh zone
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, timerAtStart, s.skill.timer);
}

void test_skill_fires_after_cooldown_while_fighting(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000; // keep the fight alive long enough to observe the skill firing
    bool fired = false;
    int firedIndex = -1;
    for (int i = 0; i < 35; ++i) { // 3.5s, past skill 0's 3.0s cooldown
        tickZone(s, 0.1, 10.0, 0);
        if (s.skillFiredThisTick >= 0) { fired = true; firedIndex = s.skillFiredThisTick; break; }
    }
    TEST_ASSERT_TRUE(fired);
    TEST_ASSERT_EQUAL_INT(0, firedIndex); // only skill 0 is unlocked at realm 0
}

void test_skill_does_not_fire_before_cooldown_elapses(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    for (int i = 0; i < 20; ++i) { // 2.0s, under skill 0's 3.0s cooldown
        tickZone(s, 0.1, 10.0, 0);
        TEST_ASSERT_EQUAL_INT(-1, s.skillFiredThisTick);
    }
}

void test_skill_bonus_damage_exceeds_plain_attack_damage(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    int hpBefore = 0;
    int hpAfter = 0;
    for (int i = 0; i < 35; ++i) {
        hpBefore = s.enemy.hp;
        tickZone(s, 0.1, 10.0, 0);
        if (s.skillFiredThisTick >= 0) { hpAfter = s.enemy.hp; break; }
    }
    int dropped = hpBefore - hpAfter;
    TEST_ASSERT_TRUE(dropped > s.player.attackDamage); // more than a plain autoattack alone
}

void test_skill_round_robins_among_unlocked_skills_in_zone(void) {
    ZoneState s = startZone(makeZoneMap(4), 4); // realm 4 -> 3 unlocked skills (indices 0,1,2)
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 4);
    }
    s.enemy.maxHp = 1000000;
    s.enemy.hp = 1000000;
    int fired[6] = {-1, -1, -1, -1, -1, -1};
    int count = 0;
    for (int i = 0; i < 300 && count < 6; ++i) { // 30s of simulated fighting, generous headroom
        s.player.hp = s.player.maxHp; // top off every tick - only the skill cycle is under test
                                       // here, and a realm-4 enemy's autoattacks (20 dmg every
                                       // 1.2s) would otherwise kill a 260-hp player well before
                                       // 30s elapse, triggering restartZone() and resetting the
                                       // cycle mid-test
        tickZone(s, 0.1, 10.0, 4);
        if (s.skillFiredThisTick >= 0) { fired[count++] = s.skillFiredThisTick; }
    }
    TEST_ASSERT_EQUAL_INT(6, count);
    TEST_ASSERT_EQUAL_INT(0, fired[0]);
    TEST_ASSERT_EQUAL_INT(1, fired[1]);
    TEST_ASSERT_EQUAL_INT(2, fired[2]);
    TEST_ASSERT_EQUAL_INT(0, fired[3]);
    TEST_ASSERT_EQUAL_INT(1, fired[4]);
    TEST_ASSERT_EQUAL_INT(2, fired[5]);
}

void test_skill_and_defeat_on_same_tick_transitions_to_walking(void) {
    ZoneState s = startZone(makeZoneMap(0), 0);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 0);
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Fighting);
    s.enemy.maxHp = 1000000;
    s.enemy.hp = 1000000; // stays alive until deliberately dropped right before a skill fires
    int skillFiredOnLastTick = -1;
    for (int i = 0; i < 100 && s.phase == ZonePhase::Fighting; ++i) {
        // If this tick's dt will cross the currently-cycled skill's cooldown (the same formula
        // tickSkill itself uses), force the enemy down to a sliver of HP first - so whichever
        // damage source lands this tick (autoattack, skill, or both) also defeats it, exercising
        // the same-tick ordering the spec calls out without depending on fragile
        // float-accumulation timing landing on a pre-computed tick count (30 additions of 0.1f
        // sum to just under 3.0f, not exactly 3.0f, so a fixed-tick-count approach mis-predicts
        // which tick the skill actually fires on).
        if (s.skill.timer + 0.1f >= SKILLS[s.skill.cycleIndex].cooldownSeconds) {
            s.enemy.hp = 1;
        }
        tickZone(s, 0.1, 10.0, 0);
        skillFiredOnLastTick = s.skillFiredThisTick; // always the most recently executed tick's value
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Walking);
    TEST_ASSERT_TRUE(s.monstersDefeated[0]);
    TEST_ASSERT_EQUAL_INT(0, skillFiredOnLastTick); // confirms a skill fired on the exact tick that ended the fight
}

void test_restart_zone_resets_skill_state(void) {
    ZoneState s = startZone(makeZoneMap(4), 4);
    for (int i = 0; i < 500 && s.phase != ZonePhase::Fighting; ++i) {
        tickZone(s, 0.1, 10.0, 4);
    }
    s.enemy.maxHp = 100000;
    s.enemy.hp = 100000;
    for (int i = 0; i < 40; ++i) tickZone(s, 0.1, 10.0, 4); // let the skill timer/cycle advance
    restartZone(s, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s.skill.timer);
    TEST_ASSERT_EQUAL_INT(0, s.skill.cycleIndex);
    TEST_ASSERT_EQUAL_INT(-1, s.skillFiredThisTick);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_start_zone_begins_walking_at_arena_start);
    RUN_TEST(test_tick_moves_toward_far_end);
    RUN_TEST(test_reaching_monster_enters_fighting);
    RUN_TEST(test_defeating_monster_resumes_walking);
    RUN_TEST(test_defeating_monster_heals_player_to_full);
    RUN_TEST(test_player_defeat_resets_to_start);
    RUN_TEST(test_clearing_all_monsters_and_reaching_end_sets_reward);
    RUN_TEST(test_restart_zone_resets_state);
    RUN_TEST(test_restart_uses_current_realm_index_not_frozen_start);
    RUN_TEST(test_tick_zone_restart_on_defeat_uses_passed_in_realm_index);
    RUN_TEST(test_restart_zone_rebuilds_map_for_current_realm);
    RUN_TEST(test_matched_realm_zone_is_clearable_at_high_realm);
    RUN_TEST(test_matched_realm_zone_is_clearable_at_low_realm);
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
    RUN_TEST(test_patrol_range_for_platform_clamps_to_nearer_edge_of_off_center_spawn);
    RUN_TEST(test_reaching_non_final_platform_edge_triggers_jumping);
    RUN_TEST(test_jumping_lands_on_destination_platform);
    RUN_TEST(test_walking_elapsed_seconds_freezes_while_fighting);
    RUN_TEST(test_restart_zone_rebuilds_platform_and_position_state);
    RUN_TEST(test_restart_zone_reshuffles_layout_across_successive_loops);
    RUN_TEST(test_start_zone_defaults_zone_run_index_to_zero);
    RUN_TEST(test_start_zone_records_explicit_zone_run_index);
    RUN_TEST(test_restart_zone_increments_zone_run_index);
    RUN_TEST(test_start_zone_resets_skill_state);
    RUN_TEST(test_skill_timer_frozen_while_walking);
    RUN_TEST(test_skill_fires_after_cooldown_while_fighting);
    RUN_TEST(test_skill_does_not_fire_before_cooldown_elapses);
    RUN_TEST(test_skill_bonus_damage_exceeds_plain_attack_damage);
    RUN_TEST(test_skill_round_robins_among_unlocked_skills_in_zone);
    RUN_TEST(test_skill_and_defeat_on_same_tick_transitions_to_walking);
    RUN_TEST(test_restart_zone_resets_skill_state);
    return UNITY_END();
}
