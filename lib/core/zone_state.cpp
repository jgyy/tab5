#include "zone_state.h"
#include <cmath>

ZoneState startZone(const ZoneMap& map, int realmIndex) {
    ZoneState s;
    s.map = map;
    s.posX = 0.0f;
    s.phase = ZonePhase::Walking;
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
int findUndefeatedMonsterInRange(const ZoneState& state) {
    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        float dist = std::fabs(state.map.monsters[i].x - state.posX);
        if (dist <= kEncounterDistance) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool allMonstersDefeated(const ZoneState& state) {
    for (bool defeated : state.monstersDefeated) {
        if (!defeated) return false;
    }
    return true;
}
} // namespace

void tickZone(ZoneState& state, double dtSeconds, double proposedReward, int currentRealmIndex) {
    if (state.phase == ZonePhase::Cleared) return;

    if (state.phase == ZonePhase::Walking) {
        int engaged = findUndefeatedMonsterInRange(state);
        if (engaged >= 0) {
            state.phase = ZonePhase::Fighting;
            state.currentMonsterIndex = engaged;
            const MonsterSpawn& spawn = state.map.monsters[static_cast<size_t>(engaged)];
            state.enemy = makeEnemyCombatant(spawn.maxHp, spawn.damage);
            return;
        }

        float step = kWalkSpeedUnitsPerSec * static_cast<float>(dtSeconds);
        state.posX += step;
        if (state.posX >= kArenaWidth) {
            state.posX = kArenaWidth;
            if (allMonstersDefeated(state)) {
                state.phase = ZonePhase::Cleared;
                state.qiRewardPending = proposedReward;
            }
        }
        return;
    }

    // Fighting
    tickCombat(state.player, state.enemy, dtSeconds);
    if (isDefeated(state.enemy)) {
        state.monstersDefeated[static_cast<size_t>(state.currentMonsterIndex)] = true;
        state.currentMonsterIndex = -1;
        state.phase = ZonePhase::Walking;
    } else if (isDefeated(state.player)) {
        restartZone(state, currentRealmIndex);
    }
}
