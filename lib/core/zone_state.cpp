#include "zone_state.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

JumpArc makeJumpArc(float fromX, float fromY, float toX, float toY) {
    JumpArc arc;
    arc.fromX = fromX;
    arc.fromY = fromY;
    arc.toX = toX;
    arc.toY = toY;
    arc.elapsed = 0.0f;
    float horizontalDistance = std::fabs(toX - fromX);
    float duration = horizontalDistance / kWalkSpeedUnitsPerSec;
    arc.duration = duration > kMinJumpDuration ? duration : kMinJumpDuration;
    return arc;
}

void jumpArcPosition(const JumpArc& arc, float elapsed, float& outX, float& outY) {
    float t = arc.duration > 0.0f ? elapsed / arc.duration : 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    outX = arc.fromX + (arc.toX - arc.fromX) * t;
    float baseY = arc.fromY + (arc.toY - arc.fromY) * t;
    outY = baseY + std::sin(kPi * t) * kJumpArcHeight;
}

float patrolPositionX(float spawnX, float patrolRange, float t) {
    if (patrolRange <= 0.0f) return spawnX;
    float period = 4.0f * patrolRange / kPatrolSpeed;
    float phase = std::fmod(t, period) / period; // t is always >= 0 (an elapsed-time accumulator)
    float tri;
    if (phase < 0.25f)      tri = 4.0f * phase;
    else if (phase < 0.75f) tri = 2.0f - 4.0f * phase;
    else                    tri = 4.0f * phase - 4.0f;
    return spawnX + tri * patrolRange;
}

float patrolRangeForPlatform(const Platform& platform) {
    float half = (platform.x1 - platform.x0) / 2.0f - kPatrolMargin;
    if (half < 0.0f) half = 0.0f;
    return half < kMaxPatrolRange ? half : kMaxPatrolRange;
}

ZoneState startZone(const ZoneMap& map, int realmIndex) {
    ZoneState s;
    s.map = map;
    s.posX = 0.0f;
    s.posY = 0.0f;
    s.currentPlatformIndex = 0;
    s.phase = ZonePhase::Walking;
    s.jump = JumpArc{};
    s.walkingElapsedSeconds = 0.0f;
    s.player = makePlayerCombatant(realmIndex);
    s.currentMonsterIndex = -1;
    s.monstersDefeated.assign(map.monsters.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartZone(ZoneState& state, int currentRealmIndex) {
    state = startZone(makeZoneMap(currentRealmIndex), currentRealmIndex);
}

namespace {
// The nearest undefeated monster on `platformIndex` within encounter range of posX, or -1.
int findUndefeatedMonsterInRangeOnPlatform(const ZoneState& state, int platformIndex) {
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        const MonsterSpawn& spawn = state.map.monsters[i];
        if (spawn.platformIndex != platformIndex) continue;
        float dist = std::fabs(spawn.x - state.posX);
        if (dist <= kEncounterDistance) return static_cast<int>(i);
    }
    return -1;
}

bool allMonstersDefeated(const ZoneState& state) {
    for (bool defeated : state.monstersDefeated) {
        if (!defeated) return false;
    }
    return true;
}

// The nearest undefeated monster's x position at or ahead of posX on the current platform, or
// that platform's x1 if none remain ahead on it. Used to clamp a single tick's walk step so a
// large dt can never carry posX past an undefeated monster, or off the platform's edge, without
// triggering the appropriate transition (encounter or jump).
float walkTargetXOnCurrentPlatform(const ZoneState& state) {
    const Platform& platform = state.map.platforms[static_cast<size_t>(state.currentPlatformIndex)];
    float nearest = platform.x1;
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        const MonsterSpawn& spawn = state.map.monsters[i];
        if (spawn.platformIndex != state.currentPlatformIndex) continue;
        if (spawn.x >= state.posX && spawn.x < nearest) nearest = spawn.x;
    }
    return nearest;
}
} // namespace

void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    state.skillFiredThisTick = -1; // reset every call - a caller inspects this immediately after
    if (state.phase == ZonePhase::Cleared) return;

    if (state.phase == ZonePhase::Walking) {
        state.walkingElapsedSeconds += static_cast<float>(dtSeconds);

        int engaged = findUndefeatedMonsterInRangeOnPlatform(state, state.currentPlatformIndex);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        float step = kWalkSpeedUnitsPerSec * static_cast<float>(dtSeconds);
        float maxStep = walkTargetXOnCurrentPlatform(state) - state.posX;
        if (maxStep < 0.0f) maxStep = 0.0f;
        if (step > maxStep) step = maxStep;
        state.posX += step;

        const Platform& platform =
            state.map.platforms[static_cast<size_t>(state.currentPlatformIndex)];
        if (state.posX >= platform.x1) {
            state.posX = platform.x1;
            bool isLastPlatform =
                (state.currentPlatformIndex == static_cast<int>(state.map.platforms.size()) - 1);
            if (isLastPlatform) {
                if (allMonstersDefeated(state)) {
                    state.phase = ZonePhase::Cleared;
                    state.qiRewardPending = proposedReward;
                }
                // Otherwise: nothing left to walk into and no next platform - the skip-clamp
                // above already prevents reaching here with an undefeated monster still ahead
                // on this platform, so this branch only fires once every monster is defeated.
            } else {
                const Platform& next =
                    state.map.platforms[static_cast<size_t>(state.currentPlatformIndex + 1)];
                float landingX = next.x0 + kLandingMargin;
                state.jump = makeJumpArc(state.posX, platform.y, landingX, next.y);
                state.phase = ZonePhase::Jumping;
            }
        }
        return;
    }

    if (state.phase == ZonePhase::Jumping) {
        state.jump.elapsed += static_cast<float>(dtSeconds);
        jumpArcPosition(state.jump, state.jump.elapsed, state.posX, state.posY);
        if (state.jump.elapsed >= state.jump.duration) {
            state.currentPlatformIndex += 1;
            state.posX = state.jump.toX;
            state.posY = state.jump.toY;
            state.phase = ZonePhase::Walking;
        }
        return;
    }

    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    int firedSkill = tickSkill(state.skill, dtSeconds, currentRealmIndex);
    if (firedSkill >= 0) {
        state.skillFiredThisTick = firedSkill;
        int skillDamage = static_cast<int>(state.player.attackDamage * SKILLS[firedSkill].damageMultiplier);
        state.enemy.hp -= skillDamage;
        if (state.enemy.hp < 0) state.enemy.hp = 0;
    }
    // Player defeat is checked FIRST: tickCombat() can land both attacks in the same call
    // whenever dtSeconds is large enough to cross both combatants' attack cooldowns at once (a
    // real occurrence here - see main.cpp's SFX delay() calls, which inflate the next loop()
    // iteration's dt). If both happened to drop to 0 HP on the same tick, checking the enemy
    // first would silently credit a win and leave the player's own defeat unhandled - the
    // character would keep walking/fighting at 0 HP until the next encounter's combat happens
    // to re-check isDefeated(player) on its own. Checking the player first means a simultaneous
    // double-KO is always a loss (and restarts the zone), never a masked win.
    if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    } else if (isDefeated(state.enemy)) {
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.phase = ZonePhase::Walking;
    }
}
