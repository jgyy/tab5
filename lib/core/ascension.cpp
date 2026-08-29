#include "ascension.h"
#include <cmath>

double qiMultiplierForInsight(double insight) {
    return 1.0 + kInsightBonusPerPoint * insight;
}

double insightGainForQi(double qiAtAscension) {
    if (qiAtAscension <= 0.0) return 0.0;
    return std::floor(std::sqrt(qiAtAscension / kInsightQiDivisor));
}

double ascensionQiThreshold(uint32_t ascensionCount) {
    return kAscensionBaseQiThreshold * std::pow(kAscensionThresholdGrowth, static_cast<double>(ascensionCount));
}

bool canAscend(const GameState& state, const AscensionState& ascension) {
    if (state.realmIndex < NUM_REALMS - 1) return false;
    return state.qi >= ascensionQiThreshold(ascension.ascensionCount);
}

bool attemptAscend(GameState& state, AscensionState& ascension) {
    if (!canAscend(state, ascension)) return false;
    double gained = insightGainForQi(state.qi);
    ascension.insight += gained;
    ascension.ascensionCount += 1;
    state = GameState();
    return true;
}
