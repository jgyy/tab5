#pragma once

// Small, pure, hardware-agnostic math backing zone_view.cpp's combat/environment effects -
// kept here (not in src/) so it's unit-testable, same split this project already uses for
// jump arcs, patrol motion, and procedural coloring.

// Decaying oscillating offset in pixels for t in [0,1] (elapsed/duration since a triggering
// event). 0 at t=0 and t=1, bounded within +-amplitudePx in between. phaseRadians lets two
// axes share the same t without moving in lockstep (pass 0 for one axis, pi/2 for the other).
float shakeOffset(float t, float amplitudePx, float phaseRadians);

// Upward pixel offset (i.e. <= 0) for a floating damage number at elapsedSeconds since it
// spawned - constant speed, no physics. Callers add this to the number's base y each frame.
float damageNumberRiseOffsetPx(float elapsedSeconds, float risePxPerSecond);

// Wraps a drifting background element's x position at elapsedSeconds, drifting from seedX at
// pxPerSecond, into [0, viewportW). fmod-based so it wraps with no jump at the boundary.
float parallaxWrapX(float seedX, float pxPerSecond, float elapsedSeconds, float viewportW);

// Smooth 0 -> 1 -> 0 intensity envelope for a burst/pulse effect over t in [0,1] (clamped
// outside that range), peaking at t=0.5. Shared by any short-lived celebratory effect that
// should fade in and back out rather than snapping on/off - e.g. the monster-kill loot sparkle
// and the realm-breakthrough celebration ring in zone_view.cpp.
float pulseEnvelope(float t);
