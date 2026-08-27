#include "trial_state.h"
#include <cmath>

TrialState startTrial(const TrialMap& map, int realmIndex) {
    TrialState s;
    s.map = map;
    s.realmIndexAtStart = realmIndex;
    s.posX = map.route[0].x;
    s.posY = map.route[0].y;
    // Start already standing at route[0], so head toward route[1] immediately rather than
    // toward route[0] itself - targeting index 0 would make the very first tick see "already
    // arrived" (distance ~0) and skip movement for a step instead of heading anywhere.
    s.currentWaypointIndex = (map.route.size() > 1) ? 1 : 0;
    s.phase = TrialPhase::Traveling;
    s.player = makePlayerCombatant(realmIndex);
    s.currentEnemyIndex = -1;
    s.enemiesDefeated.assign(map.enemies.size(), false);
    s.qiRewardPending = 0.0;
    return s;
}

void restartTrial(TrialState& state) {
    TrialMap map = state.map; // preserve across reassignment below
    int realmIndex = state.realmIndexAtStart;
    state = startTrial(map, realmIndex);
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

void tickTrial(TrialState& state, double dtSeconds, double proposedReward) {
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

        state.facingRadians = std::atan2(dy, dx);
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
        restartTrial(state);
    }
}
