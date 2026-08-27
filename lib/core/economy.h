#pragma once
#include "realms.h"

struct GeneratorDef {
    const char* name;
    double baseCost;
    double growthRate;
    double baseQiPerSecond;
    int unlockRealmIndex; // purchasable once GameState.realmIndex >= this
};

constexpr int NUM_GENERATORS = 6;
extern const GeneratorDef GENERATORS[NUM_GENERATORS];

extern const char* const REALM_NAMES[NUM_REALMS];
extern const double REALM_QI_THRESHOLD[NUM_REALMS]; // Qi needed to break INTO realm i

struct GameState {
    double qi = 0.0;
    int generatorCounts[NUM_GENERATORS] = {0, 0, 0, 0, 0, 0};
    int realmIndex = 0;
};

double costForGenerator(int genIndex, int ownedCount);
double realmMultiplier(int realmIndex);
bool isGeneratorUnlocked(const GameState& state, int genIndex);
double qiPerSecond(const GameState& state);
void tick(GameState& state, double dtSeconds);
bool canBreakthrough(const GameState& state);

// If canBreakthrough(state): spends REALM_QI_THRESHOLD[state.realmIndex + 1] from
// state.qi, advances state.realmIndex, and returns true. Otherwise leaves state
// unchanged and returns false.
bool attemptBreakthrough(GameState& state);

// If unlocked and affordable: deducts costForGenerator(genIndex, current count) and
// increments the owned count, returning true. Otherwise leaves state unchanged and
// returns false.
bool purchaseGenerator(GameState& state, int genIndex);
