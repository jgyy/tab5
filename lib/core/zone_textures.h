#pragma once
#include "color.h"

// Vertical-gradient background endpoints for a realm's zone: hue rotates 22.5 degrees per
// realm (360/16) so all 16 zones are visually distinct. Deterministic - same realmIndex
// always returns the same colors.
RGB zoneSkyColor(int realmIndex);
RGB zoneGroundColor(int realmIndex);

// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);
