#include "zone_events.h"

ZoneTickEvents deriveZoneTickEvents(const ZoneState& before, const ZoneState& after,
                                     bool wasFighting, int enemyHpBefore, int playerHpBefore) {
    ZoneTickEvents e;

    // restartZone() is the only thing in the whole module that bumps zoneRunIndex, and tickZone()
    // calls it on exactly one path: the player was defeated. So an increment across a single tick
    // is an exact, unambiguous "the zone was just rebuilt" signal.
    //
    // This deliberately does NOT infer a restart from player.hp going up. That older heuristic
    // ("nothing else ever raises player.hp during ordinary combat") was wrong: zone_state.cpp
    // full-heals the player on *every* monster kill, so any kill in which the player had taken
    // damage - which is every boss kill, since a boss fight runs 11-21 real seconds - looked
    // identical to a restart and suppressed enemyHit/monsterDefeated/bossDefeated for that tick.
    e.zoneRestarted = after.zoneRunIndex != before.zoneRunIndex;

    // Everything below is gated on wasFighting because these are all combat events, and on
    // !zoneRestarted because a rebuilt zone's fresh enemy/phase values would otherwise read as
    // "the enemy took damage" / "a kill happened" purely from the reset.
    e.enemyHit = wasFighting && !e.zoneRestarted && after.enemy.hp < enemyHpBefore;
    e.skillFired = wasFighting && after.skillFiredThisTick >= 0;
    e.bossEnrageTriggered = wasFighting && after.bossJustEnraged;
    e.bossDefeated = wasFighting && !e.zoneRestarted && after.bossJustDefeated;

    // tickZone() only ever leaves Fighting for Walking via one of two paths: the enemy was just
    // defeated, or (zoneRestarted) the player was - excluding the latter leaves exactly "a kill
    // happened this tick", the same way the enemyHit/skillFired checks above lean on exact
    // before/after comparisons instead of a dedicated event flag.
    e.monsterDefeated = wasFighting && !e.zoneRestarted && after.phase == ZonePhase::Walking;

    // No !zoneRestarted guard needed: a restart reassigns the player to full HP, so a drop can
    // never be observed on a restart tick anyway.
    e.playerHit = wasFighting && after.player.hp < playerHpBefore;

    return e;
}
