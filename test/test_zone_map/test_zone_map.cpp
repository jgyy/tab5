#include <unity.h>
#include <cmath>
#include <set>
#include "zone_map.h"

void setUp(void) {}
void tearDown(void) {}

// Platform 1 (the first elevated platform) always carries zero tier bonus, and every zone has
// one, so its first monster (index 0, since monsters are generated platform-by-platform in
// increasing order) always reproduces the original flat-arena numbers at realm 0.
void test_realm_zero_first_monster_matches_original_secret_realm_numbers(void) {
    ZoneMap m = makeZoneMap(0);
    TEST_ASSERT_TRUE(m.monsters.size() >= 1);
    TEST_ASSERT_EQUAL(1, m.monsters[0].platformIndex);
    TEST_ASSERT_EQUAL(30, m.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(8, m.monsters[0].damage);
}

void test_stats_increase_with_realm_index(void) {
    ZoneMap low = makeZoneMap(0);
    ZoneMap high = makeZoneMap(5);
    // Both always have a platform-1 monster at index 0 - comparing it isolates the realm-scaling
    // formula from the now-variable platform/monster count.
    TEST_ASSERT_TRUE(high.monsters[0].maxHp > low.monsters[0].maxHp);
    TEST_ASSERT_TRUE(high.monsters[0].damage > low.monsters[0].damage);
}

void test_monster_positions_are_increasing_and_within_arena(void) {
    for (int seed = 0; seed < 20; ++seed) {
        ZoneMap m = makeZoneMap(3, seed);
        for (size_t i = 0; i < m.monsters.size(); ++i) {
            TEST_ASSERT_TRUE(m.monsters[i].x >= 0.0f);
            TEST_ASSERT_TRUE(m.monsters[i].x < m.arenaWidth);
            if (i > 0) TEST_ASSERT_TRUE(m.monsters[i].x > m.monsters[i - 1].x);
        }
    }
}

void test_realm_index_is_recorded(void) {
    ZoneMap m = makeZoneMap(7);
    TEST_ASSERT_EQUAL(7, m.realmIndex);
}

void test_deterministic_for_same_realm_and_seed(void) {
    ZoneMap a = makeZoneMap(4, 7);
    ZoneMap b = makeZoneMap(4, 7);
    TEST_ASSERT_EQUAL((int)a.platforms.size(), (int)b.platforms.size());
    TEST_ASSERT_EQUAL((int)a.monsters.size(), (int)b.monsters.size());
    for (size_t i = 0; i < a.platforms.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].x0, b.platforms[i].x0);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.platforms[i].y, b.platforms[i].y);
    }
    for (size_t i = 0; i < a.monsters.size(); ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, a.monsters[i].x, b.monsters[i].x);
        TEST_ASSERT_EQUAL(a.monsters[i].maxHp, b.monsters[i].maxHp);
    }
}

// The core fix this feature is for: looping the same realm must NOT always hand back the exact
// same layout - a different seed has to change *something* about the terrain or the monsters.
void test_different_seeds_produce_different_layouts(void) {
    bool anyPlatformDifference = false;
    bool anyMonsterDifference = false;
    ZoneMap base = makeZoneMap(2, 0);
    for (int seed = 1; seed < 30; ++seed) {
        ZoneMap m = makeZoneMap(2, seed);
        if (m.platforms.size() != base.platforms.size()) anyPlatformDifference = true;
        for (size_t i = 0; i < m.platforms.size() && i < base.platforms.size(); ++i) {
            if (std::fabs(m.platforms[i].y - base.platforms[i].y) > 0.001f) anyPlatformDifference = true;
        }
        if (m.monsters.size() != base.monsters.size()) anyMonsterDifference = true;
        for (size_t i = 0; i < m.monsters.size() && i < base.monsters.size(); ++i) {
            if (std::fabs(m.monsters[i].x - base.monsters[i].x) > 0.001f) anyMonsterDifference = true;
        }
    }
    TEST_ASSERT_TRUE(anyPlatformDifference);
    TEST_ASSERT_TRUE(anyMonsterDifference);
}

void test_platform_count_is_one_ground_plus_three_to_five_elevated(void) {
    for (int realm = 0; realm < 16; ++realm) {
        for (int seed = 0; seed < 10; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed);
            TEST_ASSERT_TRUE(m.platforms.size() >= 4);
            TEST_ASSERT_TRUE(m.platforms.size() <= 6);
        }
    }
}

void test_every_elevated_platform_has_one_or_two_monsters(void) {
    for (int seed = 0; seed < 30; ++seed) {
        ZoneMap m = makeZoneMap(1, seed);
        int numElevated = (int)m.platforms.size() - 1;
        int countPerPlatform[6] = {0, 0, 0, 0, 0, 0};
        for (const MonsterSpawn& spawn : m.monsters) {
            TEST_ASSERT_TRUE(spawn.platformIndex >= 1);
            TEST_ASSERT_TRUE(spawn.platformIndex <= numElevated);
            countPerPlatform[spawn.platformIndex]++;
        }
        for (int p = 1; p <= numElevated; ++p) {
            TEST_ASSERT_TRUE(countPerPlatform[p] >= 1);
            TEST_ASSERT_TRUE(countPerPlatform[p] <= 2);
        }
    }
}

// Proves the "up to 2 per platform" mechanic actually fires somewhere in a reasonable sweep,
// not just dead code that always rolls 1.
void test_some_platform_gets_two_monsters_across_seeds(void) {
    bool sawDoubleSpawn = false;
    for (int seed = 0; seed < 40 && !sawDoubleSpawn; ++seed) {
        ZoneMap m = makeZoneMap(1, seed);
        int numElevated = (int)m.platforms.size() - 1;
        if ((int)m.monsters.size() > numElevated) sawDoubleSpawn = true;
    }
    TEST_ASSERT_TRUE(sawDoubleSpawn);
}

// Proves monsters are no longer pinned to the exact platform midpoint.
void test_monster_position_is_not_always_platform_midpoint(void) {
    bool sawOffCenter = false;
    for (int seed = 0; seed < 20 && !sawOffCenter; ++seed) {
        ZoneMap m = makeZoneMap(2, seed);
        for (const MonsterSpawn& spawn : m.monsters) {
            const Platform& p = m.platforms[static_cast<size_t>(spawn.platformIndex)];
            float mid = (p.x0 + p.x1) / 2.0f;
            if (std::fabs(spawn.x - mid) > 0.05f) sawOffCenter = true;
        }
    }
    TEST_ASSERT_TRUE(sawOffCenter);
}

// The real invariant is the margin-inset interior, not just "somewhere on the platform" - a
// spawn right on the platform's edge is exactly the placement this feature was meant to stop.
void test_monster_positions_stay_within_their_platforms_edge_margin(void) {
    for (int seed = 0; seed < 20; ++seed) {
        ZoneMap m = makeZoneMap(3, seed);
        for (const MonsterSpawn& spawn : m.monsters) {
            const Platform& p = m.platforms[static_cast<size_t>(spawn.platformIndex)];
            TEST_ASSERT_TRUE(spawn.x >= p.x0 + kSpawnEdgeMargin - 0.001f);
            TEST_ASSERT_TRUE(spawn.x <= p.x1 - kSpawnEdgeMargin + 0.001f);
        }
    }
}

// Two monsters sharing a platform share that platform's difficulty tier.
void test_monsters_on_same_platform_have_equal_stats(void) {
    bool checkedAPair = false;
    for (int seed = 0; seed < 40; ++seed) {
        ZoneMap m = makeZoneMap(1, seed);
        for (size_t i = 0; i + 1 < m.monsters.size(); ++i) {
            if (m.monsters[i].platformIndex == m.monsters[i + 1].platformIndex) {
                TEST_ASSERT_EQUAL(m.monsters[i].maxHp, m.monsters[i + 1].maxHp);
                TEST_ASSERT_EQUAL(m.monsters[i].damage, m.monsters[i + 1].damage);
                checkedAPair = true;
            }
        }
    }
    TEST_ASSERT_TRUE(checkedAPair); // sanity: the sweep above actually exercised a shared platform
}

// Difficulty still climbs *strictly* with which platform a monster is on, up through tier 2
// (platform 3) - the highest tier the original fixed-3-elevated-platform design ever produced
// and validated as beatable. Platforms 4 and 5 exist now purely for more monsters, not tougher
// ones: their tier is capped at 2, so consecutive platforms beyond the cap tie rather than climb
// further. Uses ">" (not ">=") below the cap deliberately: a ">=" version would also pass a
// build with the tier-bonus formula deleted entirely (every monster flat), which is exactly the
// regression this guards against.
void test_monster_difficulty_climbs_with_platform_tier_then_caps(void) {
    constexpr int kMaxDifficultyTier = 2;
    for (int seed = 0; seed < 20; ++seed) {
        ZoneMap m = makeZoneMap(2, seed);
        for (size_t i = 0; i + 1 < m.monsters.size(); ++i) {
            if (m.monsters[i + 1].platformIndex > m.monsters[i].platformIndex) {
                int tierA = m.monsters[i].platformIndex - 1;
                if (tierA < kMaxDifficultyTier) {
                    TEST_ASSERT_TRUE(m.monsters[i + 1].maxHp > m.monsters[i].maxHp);
                    TEST_ASSERT_TRUE(m.monsters[i + 1].damage > m.monsters[i].damage);
                } else {
                    TEST_ASSERT_EQUAL(m.monsters[i].maxHp, m.monsters[i + 1].maxHp);
                    TEST_ASSERT_EQUAL(m.monsters[i].damage, m.monsters[i + 1].damage);
                }
            }
        }
    }
}

// Pins the exact per-tier bonus formula (not just "increases") at a realm with enough elevated
// platforms to exercise every tier this layout rolls, including the tier-2 cap - kills a mutant
// that flattens, shrinks, or uncaps the tier step without breaking the checks above.
void test_monster_tier_bonus_matches_exact_step_formula(void) {
    constexpr int kMaxDifficultyTier = 2;
    ZoneMap m = makeZoneMap(9, 3);
    int baseHp = 30 + 20 * 9;
    int baseDamage = 8 + 3 * 9;
    for (const MonsterSpawn& spawn : m.monsters) {
        int tier = spawn.platformIndex - 1;
        if (tier > kMaxDifficultyTier) tier = kMaxDifficultyTier;
        TEST_ASSERT_EQUAL(baseHp + 20 * tier, spawn.maxHp);
        TEST_ASSERT_EQUAL(baseDamage + 6 * tier, spawn.damage);
    }
}

// The actual bug this cap fixes: without it, a realm-1 character (weak, early-game stats) could
// roll a 5-elevated-platform zone whose platform-4 monster (110 hp / 29 dmg with the old
// uncapped formula) is flatly unbeatable solo, no matter how much healing happens between
// fights - see test_matched_realm_zone_is_clearable_at_low_realm in test_zone_state for the
// end-to-end version of this guard.
void test_monster_stats_never_exceed_the_capped_tier_ceiling(void) {
    constexpr int kMaxDifficultyTier = 2;
    for (int realm = 0; realm < 16; ++realm) {
        for (int seed = 0; seed < 10; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed);
            int baseHp = 30 + 20 * realm;
            int baseDamage = 8 + 3 * realm;
            int maxAllowedHp = baseHp + 20 * kMaxDifficultyTier;
            int maxAllowedDamage = baseDamage + 6 * kMaxDifficultyTier;
            for (const MonsterSpawn& spawn : m.monsters) {
                TEST_ASSERT_TRUE(spawn.maxHp <= maxAllowedHp);
                TEST_ASSERT_TRUE(spawn.damage <= maxAllowedDamage);
            }
        }
    }
}

void test_reachability_invariant_holds_across_realms_and_seeds(void) {
    for (int realm = 0; realm < 16; ++realm) {
        for (int seed = 0; seed < 5; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed);
            for (size_t i = 1; i < m.platforms.size(); ++i) {
                float gap = m.platforms[i].x0 - m.platforms[i - 1].x1;
                float riseMagnitude = std::fabs(m.platforms[i].y - m.platforms[i - 1].y);
                TEST_ASSERT_TRUE(gap >= 0.0f);
                TEST_ASSERT_TRUE(gap <= kMaxJumpGap);
                TEST_ASSERT_TRUE(riseMagnitude <= kMaxJumpRise);
            }
        }
    }
}

void test_platform_heights_within_bounds(void) {
    for (int realm = 0; realm < 16; ++realm) {
        for (int seed = 0; seed < 5; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed);
            for (const Platform& p : m.platforms) {
                TEST_ASSERT_TRUE(p.y >= 0.0f);
                TEST_ASSERT_TRUE(p.y <= kMaxPlatformHeight);
            }
        }
    }
}

void test_every_realm_has_meaningful_verticality(void) {
    for (int realm = 0; realm < 16; ++realm) {
        for (int seed = 0; seed < 5; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed);
            float maxElevatedY = 0.0f;
            for (size_t i = 1; i < m.platforms.size(); ++i) {
                if (m.platforms[i].y > maxElevatedY) maxElevatedY = m.platforms[i].y;
            }
            TEST_ASSERT_TRUE(maxElevatedY > 0.5f);
        }
    }
}

// Monsters are generated platform-by-platform in increasing order, so their platformIndex must
// never decrease walking through the vector - generalizes the old fixed "index i -> platform
// i+1" guarantee to a variable per-platform count.
void test_monster_platform_index_is_non_decreasing(void) {
    for (int seed = 0; seed < 20; ++seed) {
        ZoneMap m = makeZoneMap(6, seed);
        for (size_t i = 1; i < m.monsters.size(); ++i) {
            TEST_ASSERT_TRUE(m.monsters[i].platformIndex >= m.monsters[i - 1].platformIndex);
        }
    }
}

void test_layouts_are_distinct_across_realms(void) {
    ZoneMap a = makeZoneMap(0);
    ZoneMap b = makeZoneMap(8);
    bool anyDifference = a.platforms.size() != b.platforms.size();
    for (size_t i = 0; i < a.platforms.size() && i < b.platforms.size(); ++i) {
        if (a.platforms[i].y != b.platforms[i].y ||
            (a.platforms[i].x1 - a.platforms[i].x0) != (b.platforms[i].x1 - b.platforms[i].x0)) {
            anyDifference = true;
        }
    }
    TEST_ASSERT_TRUE(anyDifference);
}

void test_boss_zone_has_exactly_one_boss_monster_on_last_platform(void) {
    for (int realm = 0; realm < 16; realm += 3) {
        for (int seed = 0; seed < 10; ++seed) {
            ZoneMap m = makeZoneMap(realm, seed, /*isBossZone=*/true);
            TEST_ASSERT_EQUAL(1, (int)m.monsters.size());
            TEST_ASSERT_TRUE(m.monsters[0].isBoss);
            int lastPlatformIndex = (int)m.platforms.size() - 1;
            TEST_ASSERT_EQUAL(lastPlatformIndex, m.monsters[0].platformIndex);
        }
    }
}

void test_boss_monster_sits_at_its_platforms_midpoint(void) {
    ZoneMap m = makeZoneMap(3, 7, true);
    const Platform& p = m.platforms.back();
    float mid = (p.x0 + p.x1) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, mid, m.monsters[0].x);
}

// Pins the Task 2 retuned boss formula (kBossBaseHp/kBossHpPerRealm/kBossBaseDamage/
// kBossDamagePerRealm in zone_map.cpp) - see that file's comment and the Task 2 report for why
// the first-pass numbers (300/120 hp, 18/9 damage) made the boss unwinnable at every realm.
void test_boss_stats_match_the_boss_formula(void) {
    ZoneMap low = makeZoneMap(0, 0, true);
    ZoneMap high = makeZoneMap(10, 0, true);
    TEST_ASSERT_EQUAL(300, low.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(4, low.monsters[0].damage);
    TEST_ASSERT_EQUAL(300 + 90 * 10, high.monsters[0].maxHp);
    TEST_ASSERT_EQUAL(4 + 2 * 10, high.monsters[0].damage);
}

// Regression: the isBossZone==false path (including its default) must be completely untouched.
void test_non_boss_zone_is_unaffected_by_the_boss_parameter(void) {
    ZoneMap withDefault = makeZoneMap(4, 2);
    ZoneMap withExplicitFalse = makeZoneMap(4, 2, false);
    TEST_ASSERT_EQUAL((int)withDefault.monsters.size(), (int)withExplicitFalse.monsters.size());
    for (size_t i = 0; i < withDefault.monsters.size(); ++i) {
        TEST_ASSERT_FALSE(withDefault.monsters[i].isBoss);
        TEST_ASSERT_FALSE(withExplicitFalse.monsters[i].isBoss);
        TEST_ASSERT_EQUAL(withDefault.monsters[i].maxHp, withExplicitFalse.monsters[i].maxHp);
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_realm_zero_first_monster_matches_original_secret_realm_numbers);
    RUN_TEST(test_stats_increase_with_realm_index);
    RUN_TEST(test_monster_positions_are_increasing_and_within_arena);
    RUN_TEST(test_realm_index_is_recorded);
    RUN_TEST(test_deterministic_for_same_realm_and_seed);
    RUN_TEST(test_different_seeds_produce_different_layouts);
    RUN_TEST(test_platform_count_is_one_ground_plus_three_to_five_elevated);
    RUN_TEST(test_every_elevated_platform_has_one_or_two_monsters);
    RUN_TEST(test_some_platform_gets_two_monsters_across_seeds);
    RUN_TEST(test_monster_position_is_not_always_platform_midpoint);
    RUN_TEST(test_monster_positions_stay_within_their_platforms_edge_margin);
    RUN_TEST(test_monsters_on_same_platform_have_equal_stats);
    RUN_TEST(test_monster_difficulty_climbs_with_platform_tier_then_caps);
    RUN_TEST(test_monster_tier_bonus_matches_exact_step_formula);
    RUN_TEST(test_monster_stats_never_exceed_the_capped_tier_ceiling);
    RUN_TEST(test_reachability_invariant_holds_across_realms_and_seeds);
    RUN_TEST(test_platform_heights_within_bounds);
    RUN_TEST(test_every_realm_has_meaningful_verticality);
    RUN_TEST(test_monster_platform_index_is_non_decreasing);
    RUN_TEST(test_layouts_are_distinct_across_realms);
    RUN_TEST(test_boss_zone_has_exactly_one_boss_monster_on_last_platform);
    RUN_TEST(test_boss_monster_sits_at_its_platforms_midpoint);
    RUN_TEST(test_boss_stats_match_the_boss_formula);
    RUN_TEST(test_non_boss_zone_is_unaffected_by_the_boss_parameter);
    return UNITY_END();
}
