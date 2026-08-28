#include "zone_map.h"
#include "hash.h"

namespace {
constexpr int kNumElevatedPlatforms = 3;

// Salts distinguish which quantity is drawn from the same (realmIndex, platformIndex) pair, so
// a platform's gap/width/height-delta don't collide with each other.
constexpr int kGapSalt = 0;
constexpr int kWidthSalt = 1;
constexpr int kHeightSalt = 2;

float platformGap(int realmIndex, int platformIndex) {
    return hashRange(realmIndex, platformIndex * 3 + kGapSalt, 0.5f, kMaxJumpGap);
}

float platformWidth(int realmIndex, int platformIndex) {
    return hashRange(realmIndex, platformIndex * 3 + kWidthSalt, 1.5f, 3.0f);
}

float platformHeightDelta(int realmIndex, int platformIndex) {
    if (platformIndex == 1) {
        // The first elevated platform always starts from the ground (prevY == 0 exactly, since
        // platform 0 is always the ground baseline), so a symmetric delta here had ~50% odds of
        // clamping straight back to 0 - and if platforms 2/3 also drew small/negative deltas,
        // the whole realm ended up completely flat (observed for realmIndex 3, 6, 7). Forcing a
        // strictly-positive delta for platform 1 guarantees every realm has at least one
        // genuinely elevated platform, deterministically (not just empirically for the realms
        // tested) - platforms 2 and 3 keep the full symmetric range for terrain variety.
        return hashRange(realmIndex, platformIndex * 3 + kHeightSalt, 0.6f, kMaxJumpRise);
    }
    return hashRange(realmIndex, platformIndex * 3 + kHeightSalt, -kMaxJumpRise, kMaxJumpRise);
}

float groundWidth(int realmIndex) {
    return hashRange(realmIndex, -1, 2.5f, 4.0f); // salt -1: distinct from any elevated index*3+salt
}

float clampHeight(float y) {
    if (y < 0.0f) return 0.0f;
    if (y > kMaxPlatformHeight) return kMaxPlatformHeight;
    return y;
}
} // namespace

ZoneMap makeZoneMap(int realmIndex) {
    ZoneMap m;
    m.realmIndex = realmIndex;

    m.platforms.resize(1 + kNumElevatedPlatforms);
    m.platforms[0] = Platform{0.0f, groundWidth(realmIndex), 0.0f};

    float prevY = 0.0f;
    for (int i = 1; i <= kNumElevatedPlatforms; ++i) {
        float gap = platformGap(realmIndex, i);
        float width = platformWidth(realmIndex, i);
        float y = clampHeight(prevY + platformHeightDelta(realmIndex, i));
        float x0 = m.platforms[static_cast<size_t>(i - 1)].x1 + gap;
        m.platforms[static_cast<size_t>(i)] = Platform{x0, x0 + width, y};
        prevY = y;
    }

    m.arenaWidth = m.platforms.back().x1;

    int baseHp = 30 + 20 * realmIndex;
    int baseDamage = 8 + 3 * realmIndex;
    constexpr int kTierHpBonus[3] = {0, 20, 50};
    constexpr int kTierDamageBonus[3] = {0, 6, 14};

    m.monsters.resize(kNumElevatedPlatforms);
    for (int i = 0; i < kNumElevatedPlatforms; ++i) {
        const Platform& p = m.platforms[static_cast<size_t>(i + 1)];
        m.monsters[static_cast<size_t>(i)].x = (p.x0 + p.x1) / 2.0f;
        m.monsters[static_cast<size_t>(i)].platformIndex = i + 1;
        m.monsters[static_cast<size_t>(i)].maxHp = baseHp + kTierHpBonus[i];
        m.monsters[static_cast<size_t>(i)].damage = baseDamage + kTierDamageBonus[i];
    }
    return m;
}
