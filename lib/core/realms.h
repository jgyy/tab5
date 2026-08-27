#pragma once

// Shared by mesh.h (visual growth/palette tables) and economy.h (names/thresholds),
// kept as its own tiny header so neither module depends on the other for this count.
constexpr int NUM_REALMS = 16;
