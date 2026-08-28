#include <unity.h>
#include "zone_events.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
// A ZoneState parked mid-boss-fight (or mid-regular-fight) with the player already chipped down,
// which is the exact situation the old player.hp-based restart heuristic got wrong.
ZoneState fightingState(bool isBoss, int playerDamageTaken, int enemyHp) {
    ZoneState s = startZone(makeZoneMap(0, 0, isBoss), 0);
    s.phase = ZonePhase::Fighting;
    s.currentMonsterIndex = 0;
    s.currentEncounterIsBoss = isBoss;
    s.player.hp = s.player.maxHp - playerDamageTaken;
    s.enemy.maxHp = 400;
    s.enemy.hp = enemyHp;
    return s;
}
} // namespace

// The regression test for the bug this module exists to make testable. On a kill,
// zone_state.cpp full-heals the player, so after.player.hp > before.player.hp for every fight in
// which the player took damage. That must NOT read as a zone restart, and must not suppress the
// kill's own events.
void test_kill_after_taking_damage_still_reports_monster_defeated(void) {
    ZoneState before = fightingState(/*isBoss=*/false, /*playerDamageTaken=*/37, /*enemyHp=*/5);
    int enemyHpBefore = before.enemy.hp;
    int playerHpBefore = before.player.hp;

    ZoneState after = before;
    after.phase = ZonePhase::Walking;     // tickZone() returns to Walking on a kill
    after.enemy.hp = 0;
    after.player.hp = after.player.maxHp; // the full-heal-on-every-kill that broke the old heuristic
    // zoneRunIndex deliberately untouched - no restart happened.

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/true, enemyHpBefore, playerHpBefore);
    TEST_ASSERT_TRUE(after.player.hp > playerHpBefore); // the condition the old heuristic tripped on
    TEST_ASSERT_FALSE(e.zoneRestarted);
    TEST_ASSERT_TRUE(e.monsterDefeated);
    TEST_ASSERT_TRUE(e.enemyHit);
    TEST_ASSERT_FALSE(e.bossDefeated); // not a boss
    TEST_ASSERT_FALSE(e.playerHit);
}

void test_boss_kill_after_taking_damage_still_reports_boss_defeated(void) {
    ZoneState before = fightingState(/*isBoss=*/true, /*playerDamageTaken=*/80, /*enemyHp=*/3);
    int enemyHpBefore = before.enemy.hp;
    int playerHpBefore = before.player.hp;

    ZoneState after = before;
    after.phase = ZonePhase::Walking;
    after.enemy.hp = 0;
    after.player.hp = after.player.maxHp;
    after.bossJustDefeated = true;        // the pulse tickZone() sets on the kill tick
    after.currentEncounterIsBoss = false; // cleared as the encounter ends

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/true, enemyHpBefore, playerHpBefore);
    TEST_ASSERT_FALSE(e.zoneRestarted);
    TEST_ASSERT_TRUE(e.bossDefeated);
    TEST_ASSERT_TRUE(e.monsterDefeated); // a boss kill is still a kill - loot pop fires too
    TEST_ASSERT_TRUE(e.enemyHit);
}

// The other side of the same coin: a genuine player-defeat restart must still be detected, and
// must still suppress the kill/hit events a freshly rebuilt zone would otherwise fake.
void test_player_defeat_restart_is_detected_and_suppresses_kill_events(void) {
    ZoneState before = fightingState(/*isBoss=*/true, /*playerDamageTaken=*/95, /*enemyHp=*/220);
    int enemyHpBefore = before.enemy.hp;
    int playerHpBefore = before.player.hp;

    // Exactly what restartZone() leaves behind: a brand-new zone one run index further on.
    ZoneState after = startZone(makeZoneMap(0, 1), 0, 1);

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/true, enemyHpBefore, playerHpBefore);
    TEST_ASSERT_TRUE(e.zoneRestarted);
    TEST_ASSERT_FALSE(e.monsterDefeated);
    TEST_ASSERT_FALSE(e.bossDefeated);
    TEST_ASSERT_FALSE(e.enemyHit); // the rebuilt zone's enemy.hp of 0 is not "the enemy took damage"
}

void test_mid_fight_signals_derive_from_their_source_fields(void) {
    ZoneState before = fightingState(/*isBoss=*/true, /*playerDamageTaken=*/0, /*enemyHp=*/220);
    int enemyHpBefore = before.enemy.hp;
    int playerHpBefore = before.player.hp;

    ZoneState after = before;
    after.enemy.hp = 150;            // autoattack + skill damage landed
    after.player.hp -= 9;            // the boss hit back
    after.skillFiredThisTick = 2;
    after.bossJustEnraged = true;    // crossed the half-HP threshold on this tick

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/true, enemyHpBefore, playerHpBefore);
    TEST_ASSERT_FALSE(e.zoneRestarted);
    TEST_ASSERT_TRUE(e.enemyHit);
    TEST_ASSERT_TRUE(e.skillFired);
    TEST_ASSERT_TRUE(e.bossEnrageTriggered);
    TEST_ASSERT_TRUE(e.playerHit);
    TEST_ASSERT_FALSE(e.monsterDefeated); // still Fighting
    TEST_ASSERT_FALSE(e.bossDefeated);
}

void test_quiet_fighting_tick_reports_nothing(void) {
    ZoneState before = fightingState(/*isBoss=*/false, /*playerDamageTaken=*/12, /*enemyHp=*/220);
    ZoneState after = before; // neither side's cooldown elapsed this tick

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/true,
                                             before.enemy.hp, before.player.hp);
    TEST_ASSERT_FALSE(e.zoneRestarted);
    TEST_ASSERT_FALSE(e.enemyHit);
    TEST_ASSERT_FALSE(e.skillFired);
    TEST_ASSERT_FALSE(e.monsterDefeated);
    TEST_ASSERT_FALSE(e.bossEnrageTriggered);
    TEST_ASSERT_FALSE(e.bossDefeated);
    TEST_ASSERT_FALSE(e.playerHit);
}

// Walking/Jumping ticks change phase and position constantly; none of that is a combat event.
void test_non_fighting_tick_reports_no_combat_events(void) {
    ZoneState before = startZone(makeZoneMap(0), 0); // Walking
    ZoneState after = before;
    after.phase = ZonePhase::Walking;
    after.enemy.hp = 0; // stale/unused while Walking - must not read as a hit

    ZoneTickEvents e = deriveZoneTickEvents(before, after, /*wasFighting=*/false,
                                             /*enemyHpBefore=*/30, /*playerHpBefore=*/before.player.maxHp);
    TEST_ASSERT_FALSE(e.zoneRestarted);
    TEST_ASSERT_FALSE(e.enemyHit);
    TEST_ASSERT_FALSE(e.monsterDefeated);
    TEST_ASSERT_FALSE(e.skillFired);
    TEST_ASSERT_FALSE(e.bossEnrageTriggered);
    TEST_ASSERT_FALSE(e.bossDefeated);
    TEST_ASSERT_FALSE(e.playerHit);
}

// End-to-end against the real state machine, driven exactly the way main.cpp's loop() drives it.
// This is what actually failed before the fix: a full realm-0 boss zone produced zero observed
// boss-defeat (and zero monster-defeat) events, so triggerBossDefeatFx()/playBossDefeatSfx() and
// triggerLootPop()/playLootSfx() never fired on a boss kill.
void test_a_real_boss_kill_is_observed_end_to_end(void) {
    ZoneState s = startZone(makeZoneMap(0, 0, /*isBossZone=*/true), 0);
    ZoneState before;
    int bossDefeats = 0;
    int monsterDefeats = 0;
    int enrages = 0;
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared && s.zoneRunIndex == 0; ++i) {
        before = s;
        bool wasFighting = (s.phase == ZonePhase::Fighting);
        int enemyHpBefore = s.enemy.hp;
        int playerHpBefore = s.player.hp;
        tickZone(s, 0.1, 42.0, 0);
        ZoneTickEvents e = deriveZoneTickEvents(before, s, wasFighting, enemyHpBefore, playerHpBefore);
        if (e.zoneRestarted) TEST_FAIL_MESSAGE("realm-0 boss zone should be winnable on the first attempt");
        if (e.bossDefeated) bossDefeats++;
        if (e.monsterDefeated) monsterDefeats++;
        if (e.bossEnrageTriggered) enrages++;
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Cleared);
    TEST_ASSERT_EQUAL_INT(1, bossDefeats);   // exactly one boss, exactly one observed defeat
    TEST_ASSERT_EQUAL_INT(1, monsterDefeats); // the same kill also reads as a monster kill
    TEST_ASSERT_EQUAL_INT(1, enrages);        // the one-time half-HP enrage, observed once
}

// The same end-to-end sweep for a regular zone: every kill the state machine performs must be
// observed as exactly one monsterDefeated event, and never as a bossDefeated one.
void test_every_regular_kill_is_observed_end_to_end(void) {
    ZoneState s = startZone(makeZoneMap(5, 3), 5, 3);
    int expectedKills = static_cast<int>(s.map.monsters.size());
    ZoneState before;
    int monsterDefeats = 0;
    int bossDefeats = 0;
    for (int i = 0; i < 5000 && s.phase != ZonePhase::Cleared && s.zoneRunIndex == 3; ++i) {
        before = s;
        bool wasFighting = (s.phase == ZonePhase::Fighting);
        int enemyHpBefore = s.enemy.hp;
        int playerHpBefore = s.player.hp;
        tickZone(s, 0.1, 42.0, 5);
        ZoneTickEvents e = deriveZoneTickEvents(before, s, wasFighting, enemyHpBefore, playerHpBefore);
        if (e.monsterDefeated) monsterDefeats++;
        if (e.bossDefeated) bossDefeats++;
    }
    TEST_ASSERT_TRUE(s.phase == ZonePhase::Cleared);
    TEST_ASSERT_TRUE(expectedKills > 0);
    TEST_ASSERT_EQUAL_INT(expectedKills, monsterDefeats);
    TEST_ASSERT_EQUAL_INT(0, bossDefeats);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_kill_after_taking_damage_still_reports_monster_defeated);
    RUN_TEST(test_boss_kill_after_taking_damage_still_reports_boss_defeated);
    RUN_TEST(test_player_defeat_restart_is_detected_and_suppresses_kill_events);
    RUN_TEST(test_mid_fight_signals_derive_from_their_source_fields);
    RUN_TEST(test_quiet_fighting_tick_reports_nothing);
    RUN_TEST(test_non_fighting_tick_reports_no_combat_events);
    RUN_TEST(test_a_real_boss_kill_is_observed_end_to_end);
    RUN_TEST(test_every_regular_kill_is_observed_end_to_end);
    return UNITY_END();
}
