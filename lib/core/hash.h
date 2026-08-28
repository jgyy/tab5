#pragma once

// Deterministic pseudo-random value in [0,1) from two integers - an integer hash, no RNG
// state, so the same (a,b) always returns the same value. Shared by any module that needs
// reproducible per-index "randomness" (zone_textures' color jitter, zone_map's platform-terrain
// generation).
float hashUnitFloat(int a, int b);

// Maps hashUnitFloat(a,b) into [lo, hi). Convenience wrapper for the common "pick a bounded
// value from a hash" pattern.
float hashRange(int a, int b, float lo, float hi);
