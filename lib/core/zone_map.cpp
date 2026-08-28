#include "zone_map.h"
#include "hash.h"

namespace {
constexpr int kMinElevatedPlatforms = 3;
constexpr int kMaxElevatedPlatforms = 5; // inclusive

constexpr float kTierHpStep = 20.0f;     // hp bonus per platform tier above the first
constexpr float kTierDamageStep = 6.0f;  // damage bonus per platform tier above the first

// The original fixed-3-elevated-platform design only ever produced tiers 0/1/2 (platforms 1-3)
// and every realm's difficulty curve was tuned and validated against that ceiling. Platforms 4
// and 5 exist purely to add more monsters, not tougher ones: a monster's *difficulty* tier is
// capped here at kMaxDifficultyTier even though its platformIndex can go higher, so a low-realm
// character can never roll a solo monster stronger than the toughest one the original design
// already proved beatable (a platform-4/5 monster ties platform 3's stats instead of exceeding
// them). Visual variety is unaffected - zone_view.cpp's silhouette/color selection cycles by
// platformIndex independently of this cap, so platforms 4/5 still look distinct even though
// they fight the same as platform 3.
constexpr int kMaxDifficultyTier = 2;

// Boss stats: a single much-tougher monster in lieu of the platform-tiered roster (see
// makeZoneMap's isBossZone branch) - deliberately not derived from the tier formula above,
// since a boss isn't "one more tier," it's its own fight. Tuned in Task 2 against zone_state's
// boss-clear-rate simulation test: both the player's and the enemy's autoattack cadence are
// fixed (1.0s / 1.2s, zone_combat.h) regardless of realm, so only the flat per-hit numbers here
// control the fight - the first-pass values (300/120 hp, 18/9 damage) put the boss's sustained
// dps far above what any realm's player maxHp budget could ever absorb over the much longer
// (bulkier-HP) fight, making it unwinnable at every realm, not just an edge case. This revision
// keeps the boss a genuine endurance test - HP alone roughly 4x a matched-realm top-tier
// platform monster's, so the fight runs ~12-22s instead of a regular fight's ~3-5s - while
// dropping the per-hit damage well below a regular monster's so total damage absorbed over that
// much longer fight still lands the player around 15-50% HP remaining (deterministic per
// realm - see the sim notes in the Task 2 report). Verified across realm 0-15 by the
// scratch simulation in the Task 2 report, and pinned at realm 0/10 by
// test_boss_stats_match_the_boss_formula in test_zone_map.
constexpr int kBossBaseHp = 300, kBossHpPerRealm = 90;
constexpr int kBossBaseDamage = 4, kBossDamagePerRealm = 2;

int bossMaxHp(int realmIndex) { return kBossBaseHp + kBossHpPerRealm * realmIndex; }
int bossDamage(int realmIndex) { return kBossBaseDamage + kBossDamagePerRealm * realmIndex; }

// A platform's spawnable interior keeps clear of its own edges by kSpawnEdgeMargin (declared in
// zone_map.h so tests can check the invariant directly - zone_state.h's separate kPatrolMargin
// matches its value so a patrolling monster's clamp lines up with where it could have spawned);
// when two monsters share a platform they're also kept at least kSpawnMinGap apart. Both are
// well inside the smallest possible elevated-platform interior (min width 1.5 - 2*0.3 = 0.9), so
// neither margin can ever invert the [lo, hi] range it's applied to.
constexpr float kSpawnMinGap = 0.25f;

// Salts distinguish which quantity is drawn from the same (genSeed, platformIndex) pair, so a
// platform's gap/width/height-delta/monster-count/monster-position don't collide with each
// other. Terrain salts stay small (<=17); monster-generation salts use a separate, much larger
// range so the two families can never accidentally coincide.
constexpr int kGapSalt = 0;
constexpr int kWidthSalt = 1;
constexpr int kHeightSalt = 2;
constexpr int kGroundWidthSalt = -1;
constexpr int kElevatedCountSalt = -2;
constexpr int kMonsterCountSaltBase = 1000;
constexpr int kMonsterPosSaltBase = 2000;

// Combines realmIndex and the caller's per-loop seed into one hash salt used for all
// *structural* generation (platform/monster layout). The realm's color palette (zone_textures.h)
// stays keyed to realmIndex alone, so re-rolling `seed` reshuffles the terrain/monsters every
// zone loop without changing the realm's "look".
int combineSeed(int realmIndex, int seed) {
    return realmIndex * 1000003 + seed;
}

int numElevatedPlatforms(int genSeed) {
    constexpr float kSpan = static_cast<float>(kMaxElevatedPlatforms - kMinElevatedPlatforms + 1);
    return kMinElevatedPlatforms + static_cast<int>(hashRange(genSeed, kElevatedCountSalt, 0.0f, kSpan));
}

float platformGap(int genSeed, int platformIndex) {
    return hashRange(genSeed, platformIndex * 3 + kGapSalt, 0.5f, kMaxJumpGap);
}

float platformWidth(int genSeed, int platformIndex) {
    return hashRange(genSeed, platformIndex * 3 + kWidthSalt, 1.5f, 3.0f);
}

float platformHeightDelta(int genSeed, int platformIndex) {
    if (platformIndex == 1) {
        // The first elevated platform always starts from the ground (prevY == 0 exactly, since
        // platform 0 is always the ground baseline), so a symmetric delta here had ~50% odds of
        // clamping straight back to 0 - and if the rest also drew small/negative deltas, the
        // whole realm ended up completely flat. Forcing a strictly-positive delta for platform 1
        // guarantees every layout has at least one genuinely elevated platform, deterministically
        // - later platforms keep the full symmetric range for terrain variety.
        return hashRange(genSeed, platformIndex * 3 + kHeightSalt, 0.6f, kMaxJumpRise);
    }
    return hashRange(genSeed, platformIndex * 3 + kHeightSalt, -kMaxJumpRise, kMaxJumpRise);
}

float groundWidth(int genSeed) {
    return hashRange(genSeed, kGroundWidthSalt, 2.5f, 4.0f);
}

float clampHeight(float y) {
    if (y < 0.0f) return 0.0f;
    if (y > kMaxPlatformHeight) return kMaxPlatformHeight;
    return y;
}

// 1 or 2 monsters per elevated platform - roughly a third of platforms get a second spawn, so a
// zone typically ends up busier than the old fixed "exactly one per platform" without every
// platform doubling up.
int monsterCountForPlatform(int genSeed, int platformIndex) {
    float roll = hashUnitFloat(genSeed, kMonsterCountSaltBase + platformIndex * 10);
    return roll < 0.35f ? 2 : 1;
}

// Deterministically places `count` (1 or 2) monsters along a platform's interior, clear of its
// edges by kSpawnEdgeMargin and, when there are two, clear of each other by kSpawnMinGap - by
// splitting the interior into `count` slots and hash-jittering within each, so a spawn is no
// longer pinned to the exact platform midpoint.
void placeMonstersOnPlatform(const Platform& platform, int genSeed, int platformIndex, int count,
                              float outX[2]) {
    float lo = platform.x0 + kSpawnEdgeMargin;
    float hi = platform.x1 - kSpawnEdgeMargin;
    if (count == 1) {
        outX[0] = hashRange(genSeed, kMonsterPosSaltBase + platformIndex * 10, lo, hi);
        return;
    }
    float mid = (lo + hi) / 2.0f;
    float halfGap = kSpawnMinGap / 2.0f;
    outX[0] = hashRange(genSeed, kMonsterPosSaltBase + platformIndex * 10 + 1, lo, mid - halfGap);
    outX[1] = hashRange(genSeed, kMonsterPosSaltBase + platformIndex * 10 + 2, mid + halfGap, hi);
}
} // namespace

ZoneMap makeZoneMap(int realmIndex, int seed, bool isBossZone) {
    ZoneMap m;
    m.realmIndex = realmIndex;
    int genSeed = combineSeed(realmIndex, seed);

    int numElevated = numElevatedPlatforms(genSeed);
    m.platforms.resize(1 + static_cast<size_t>(numElevated));
    m.platforms[0] = Platform{0.0f, groundWidth(genSeed), 0.0f};

    float prevY = 0.0f;
    for (int i = 1; i <= numElevated; ++i) {
        float gap = platformGap(genSeed, i);
        float width = platformWidth(genSeed, i);
        float y = clampHeight(prevY + platformHeightDelta(genSeed, i));
        float x0 = m.platforms[static_cast<size_t>(i - 1)].x1 + gap;
        m.platforms[static_cast<size_t>(i)] = Platform{x0, x0 + width, y};
        prevY = y;
    }
    m.arenaWidth = m.platforms.back().x1;

    if (isBossZone) {
        int bossPlatformIndex = numElevated;
        const Platform& p = m.platforms[static_cast<size_t>(bossPlatformIndex)];
        MonsterSpawn boss;
        boss.x = (p.x0 + p.x1) / 2.0f; // plain midpoint - a boss is one deliberate encounter,
                                        // not part of the jittered multi-monster placement system
        boss.platformIndex = bossPlatformIndex;
        boss.maxHp = bossMaxHp(realmIndex);
        boss.damage = bossDamage(realmIndex);
        boss.isBoss = true;
        m.monsters.push_back(boss);
        return m;
    }

    int baseHp = 30 + 20 * realmIndex;
    int baseDamage = 8 + 3 * realmIndex;

    for (int i = 1; i <= numElevated; ++i) {
        const Platform& p = m.platforms[static_cast<size_t>(i)];
        int count = monsterCountForPlatform(genSeed, i);
        float xs[2];
        placeMonstersOnPlatform(p, genSeed, i, count, xs);
        int tier = (i - 1) < kMaxDifficultyTier ? (i - 1) : kMaxDifficultyTier;
        int tierHp = baseHp + static_cast<int>(kTierHpStep * static_cast<float>(tier));
        int tierDamage = baseDamage + static_cast<int>(kTierDamageStep * static_cast<float>(tier));
        for (int slot = 0; slot < count; ++slot) {
            MonsterSpawn spawn;
            spawn.x = xs[slot];
            spawn.platformIndex = i;
            spawn.maxHp = tierHp;
            spawn.damage = tierDamage;
            m.monsters.push_back(spawn);
        }
    }
    return m;
}
