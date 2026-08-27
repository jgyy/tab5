#include "economy.h"
#include <cmath>

const GeneratorDef GENERATORS[NUM_GENERATORS] = {
    {"Breathing Technique",     10.0,      1.12, 0.5,     0},
    {"Spirit Herb Garden",      100.0,     1.13, 4.0,     1},
    {"Meditation Formation",    1200.0,    1.13, 30.0,    2},
    {"Disciple Cultivators",    15000.0,   1.14, 220.0,   3},
    {"Spirit Vein Tap",         180000.0,  1.14, 1600.0,  4},
    {"Ancient Formation Array", 2200000.0, 1.15, 12000.0, 5},
};

const char* const REALM_NAMES[NUM_REALMS] = {
    "Mortal Body", "Qi Condensation", "Foundation Establishment",
    "Core Formation", "Nascent Soul", "Soul Transformation", "Void Refinement"
};

const double REALM_QI_THRESHOLD[NUM_REALMS] = {
    0.0, 100.0, 1200.0, 15000.0, 180000.0, 2200000.0, 27000000.0
};

double costForGenerator(int genIndex, int ownedCount) {
    const GeneratorDef& def = GENERATORS[genIndex];
    return def.baseCost * std::pow(def.growthRate, static_cast<double>(ownedCount));
}

double realmMultiplier(int realmIndex) {
    return std::pow(1.15, static_cast<double>(realmIndex));
}

bool isGeneratorUnlocked(const GameState& state, int genIndex) {
    return state.realmIndex >= GENERATORS[genIndex].unlockRealmIndex;
}

double qiPerSecond(const GameState& state) {
    double total = 0.0;
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        if (!isGeneratorUnlocked(state, i)) continue;
        total += state.generatorCounts[i] * GENERATORS[i].baseQiPerSecond;
    }
    return total * realmMultiplier(state.realmIndex);
}

void tick(GameState& state, double dtSeconds) {
    if (dtSeconds <= 0.0) return;
    state.qi += qiPerSecond(state) * dtSeconds;
}

bool canBreakthrough(const GameState& state) {
    if (state.realmIndex >= NUM_REALMS - 1) return false;
    return state.qi >= REALM_QI_THRESHOLD[state.realmIndex + 1];
}

bool attemptBreakthrough(GameState& state) {
    if (!canBreakthrough(state)) return false;
    state.qi -= REALM_QI_THRESHOLD[state.realmIndex + 1];
    state.realmIndex += 1;
    return true;
}

bool purchaseGenerator(GameState& state, int genIndex) {
    if (!isGeneratorUnlocked(state, genIndex)) return false;
    double cost = costForGenerator(genIndex, state.generatorCounts[genIndex]);
    if (state.qi < cost) return false;
    state.qi -= cost;
    state.generatorCounts[genIndex] += 1;
    return true;
}
