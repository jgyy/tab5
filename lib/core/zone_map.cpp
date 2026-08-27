#include "zone_map.h"

ZoneMap makeZoneMap(int realmIndex) {
    ZoneMap m;
    m.realmIndex = realmIndex;

    int baseHp = 30 + 40 * realmIndex;
    int baseDamage = 8 + 6 * realmIndex;
    constexpr int kTierHpBonus[3] = {0, 20, 50};
    constexpr int kTierDamageBonus[3] = {0, 6, 14};
    constexpr float kSpawnX[3] = {2.5f, 5.0f, 7.5f};

    m.monsters.resize(3);
    for (int i = 0; i < 3; ++i) {
        m.monsters[i].x = kSpawnX[i];
        m.monsters[i].maxHp = baseHp + kTierHpBonus[i];
        m.monsters[i].damage = baseDamage + kTierDamageBonus[i];
    }
    return m;
}
