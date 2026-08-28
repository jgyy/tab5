#pragma once
#include "color.h"

// Vertical-gradient background endpoints for a realm's zone: hue rotates 22.5 degrees per
// realm (360/16) so all 16 zones are visually distinct. Deterministic - same realmIndex
// always returns the same colors.
RGB zoneSkyColor(int realmIndex);
RGB zoneGroundColor(int realmIndex);

// Ledge fill color for a realm's elevated platforms - distinct from both zoneSkyColor and
// zoneGroundColor, tinted by the same per-realm hue. Deterministic.
RGB platformColor(int realmIndex);

// Monster body color for the tierIndex-th spawn (0,1,2 = increasing difficulty) in a realm's
// zone - darkens/intensifies with tier so tougher monsters visibly read as tougher, tinted by
// the same realm hue as the background. Deterministic.
RGB monsterColor(int realmIndex, int tierIndex);

// Boss body color for a realm's zone - darker and more saturated than any regular monsterColor
// tier (even tier 2, the toughest), so a boss reads as visually distinct at a glance. Same
// "opposite the background hue" family as monsterColor. Deterministic.
RGB bossColor(int realmIndex);

// Faint ring color drawn around the character in zone_view - reuses the zone's own per-realm
// hue (not an arbitrary rainbow), saturation climbing with realmIndex for a subtle "aura
// strengthens with cultivation" progression. Deterministic.
RGB characterAuraColor(int realmIndex);
