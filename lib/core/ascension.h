#pragma once
#include <cstdint>
#include "economy.h"

// Prestige state: permanent across every ascension, never reset by attemptAscend(). Persisted
// via save.h's SaveData (see the save-format task).
struct AscensionState {
    uint32_t ascensionCount = 0;
    double insight = 0.0;
};

// Linear, modest bonus (2% per point) so it compounds ascension over ascension without
// exploding - a first-pass constant, meant to be simulation-tuned later like every other
// numeric balance guess in this project.
constexpr double kInsightBonusPerPoint = 0.02;

// sqrt-scales Qi banked at ascension into insight, so very large late-game Qi numbers convert
// into modest, sane insight gains instead of a linear runaway.
constexpr double kInsightQiDivisor = 1.0e16;

// First ascension requires banking this much Qi while already at the realm cap - roughly the
// same order of magnitude as REALM_QI_THRESHOLD[NUM_REALMS - 1] itself.
constexpr double kAscensionBaseQiThreshold = 1.0e17;

// Each successive ascension requires this many times more Qi than the last.
constexpr double kAscensionThresholdGrowth = 3.0;

// Permanent Qi/sec multiplier from accumulated insight.
double qiMultiplierForInsight(double insight);

// Qi banked at the moment of ascension -> insight gained. Never negative; 0 for qi <= 0.
double insightGainForQi(double qiAtAscension);

// Qi threshold required to ascend for the ascensionCount-th time (0-indexed: the very first
// ascension uses ascensionQiThreshold(0)). Grows by kAscensionThresholdGrowth each time.
double ascensionQiThreshold(uint32_t ascensionCount);

// True once realmIndex is at the cap (NUM_REALMS - 1) AND qi has crossed
// ascensionQiThreshold(ascension.ascensionCount).
bool canAscend(const GameState& state, const AscensionState& ascension);

// If canAscend(state, ascension): converts state.qi into insight (added into
// ascension.insight), increments ascension.ascensionCount, resets state to a fresh GameState
// (qi=0, starting generator, realmIndex=0), and returns true. Otherwise leaves both arguments
// completely unchanged and returns false.
bool attemptAscend(GameState& state, AscensionState& ascension);
