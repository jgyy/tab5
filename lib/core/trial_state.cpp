#include "trial_state.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

// Signed difference `to - from`, wrapped to (-pi, pi] so a heading always turns the short way
// around the +-pi wrap boundary instead of spinning the long way around. This maze only ever
// needs 90-degree turns, but the math is general.
float shortestAngleDiff(float from, float to) {
    float diff = std::fmod(to - from, kTwoPi);
    if (diff > kPi) diff -= kTwoPi;
    if (diff < -kPi) diff += kTwoPi;
    return diff;
}
} // namespace

TrialState startTrial(const TrialMap& map, int realmIndex) {
    TrialState s;
    s.map = map;
    s.posX = map.route[0].x;
    s.posY = map.route[0].y;
    // Start already standing at route[0], so head toward route[1] immediately rather than
    // toward route[0] itself - targeting index 0 would make the very first tick see "already
    // arrived" (distance ~0) and skip movement for a step instead of heading anywhere.
    s.currentWaypointIndex = (map.route.size() > 1) ? 1 : 0;
    // Face the first waypoint immediately instead of leaving facingRadians at its 0.0f default -
    // that default only happens to already match this specific map's first segment (due east),
    // which isn't something a differently-shaped map could rely on.
    if (map.route.size() > 1) {
        s.facingRadians = std::atan2(map.route[1].y - s.posY, map.route[1].x - s.posX);
    }
    s.phase = TrialPhase::Traveling;
    s.player = makePlayerCombatant(realmIndex);
    s.currentEnemyIndex = -1;
    s.enemiesDefeated.assign(map.enemies.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartTrial(TrialState& state, int currentRealmIndex) {
    TrialMap map = state.map; // preserve across reassignment below
    state = startTrial(map, currentRealmIndex);
}

namespace {
int findUndefeatedEnemyInRange(const TrialState& state) {
    for (size_t i = 0; i < state.map.enemies.size(); ++i) {
        if (state.enemiesDefeated[i]) continue;
        const EnemySpawn& e = state.map.enemies[i];
        float dx = e.x - state.posX;
        float dy = e.y - state.posY;
        if (std::sqrt(dx * dx + dy * dy) <= kEncounterRadius) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool allEnemiesDefeated(const TrialState& state) {
    for (bool defeated : state.enemiesDefeated) {
        if (!defeated) return false;
    }
    return true;
}
} // namespace

void tickTrial(TrialState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == TrialPhase::Cleared) return;

    if (state.phase == TrialPhase::Traveling) {
        int engaged = findUndefeatedEnemyInRange(state);
        if (engaged >= 0) {
            state.phase = TrialPhase::Fighting;
            state.currentEnemyIndex = engaged;
            const EnemySpawn& spawn = state.map.enemies[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        const Waypoint& target = state.map.route[static_cast<size_t>(state.currentWaypointIndex)];
        float dx = target.x - state.posX;
        float dy = target.y - state.posY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 0.05f) {
            bool isLastWaypoint =
                state.currentWaypointIndex == static_cast<int>(state.map.route.size()) - 1;
            if (isLastWaypoint && allEnemiesDefeated(state)) {
                state.phase = TrialPhase::Cleared;
                state.qiRewardPending = proposedReward;
            } else if (!isLastWaypoint) {
                state.currentWaypointIndex++;
            }
            return;
        }

        // Ease facingRadians toward the travel direction at a fixed turn rate instead of
        // snapping instantly to it in one tick - an instant same-tick jump reads as a hard cut
        // once rendered at any real frame rate, not as a turn. Movement below is unaffected:
        // position always steps straight toward the target regardless of how far the camera
        // has turned to face it yet.
        float desiredFacing = std::atan2(dy, dx);
        float diff = shortestAngleDiff(state.facingRadians, desiredFacing);
        float maxStep = kTurnRateRadiansPerSec * static_cast<float>(dtSeconds);
        if (diff > maxStep) diff = maxStep;
        if (diff < -maxStep) diff = -maxStep;
        state.facingRadians += diff;

        float step = kTravelSpeed * static_cast<float>(dtSeconds);
        if (step > dist) step = dist;
        state.posX += (dx / dist) * step;
        state.posY += (dy / dist) * step;
        return;
    }

    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    if (isDefeated(state.enemy)) {
        state.enemiesDefeated[static_cast<size_t>(state.currentEnemyIndex)] = true;
        state.currentEnemyIndex = -1;
        state.phase = TrialPhase::Traveling;
    } else if (isDefeated(state.player)) {
        restartTrial(state, currentRealmIndex);
    }
}
