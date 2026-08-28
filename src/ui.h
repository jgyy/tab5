#pragma once
#include <cstddef>
#include <M5Unified.h>
#include "economy.h"
#include "zone_state.h"
#include "hittest.h"


// The real M5Tab5 panel's native hardware orientation is portrait (720x1280, confirmed against
// the fetched M5GFX source - NOT the 1280x720 landscape shape it's often assumed to be).
// main.cpp's setup() rotates it to landscape (setRotation(1)) for the MapleStory-style wide
// zone view, giving width()==1280/height()==720 from here on - the layout below is still a
// vertical stack (header -> zone viewport -> stats panel), computed at runtime from
// M5.Display.width()/.height() rather than hardcoded, so it adapts to whichever rotation is
// actually active.
constexpr int kHeaderHeight = 64;

// The y-coordinate, in absolute screen space, where the zone viewport ends and the stats
// panel begins: screen height minus a fixed, compact panel height (see kPanelHeight in
// ui.cpp), not a 50/50 split - the zone view is the game, so it gets the screen; the panel
// is a thin strip of read-only stats. A function (not a constant) because it depends on the
// live display height - shared between zone_view.cpp (which sizes its canvas to this range)
// and ui.cpp (which draws the panel starting here), so the two can never disagree about
// where the split sits.
int sceneViewportBottom(int screenH);

// Must be called once (e.g. from setup()) before the first drawHud() call — allocates the
// offscreen header/panel sprites sized to `display`.
void initHud(M5GFX& display);

// Draws the full HUD (header bar + stats panel) into offscreen sprites, then pushes each to
// `display` in one blit apiece. Keeps every redraw atomic on the physical screen - drawing
// primitives directly to the live display, one at a time, is visible to the eye as
// partial-redraw flicker. The panel is a MapleStory-styled gold/bronze window frame around
// individually bordered, glossy bars - top to bottom: player HP / enemy HP (empty when not
// currently fighting) sharing a row, monsters-defeated progress, and breakthrough progress as
// the thinnest sliver flush against the bottom (this game's analog to Maple's EXP bar) - all
// read-only, driven entirely by automation. There is nothing left to tap anywhere on screen.
void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone);

// Compact K/M/B display formatting for Qi-scale numbers (e.g. "2.2M" instead of
// "2200000"); exposed so main.cpp's welcome-back screen can format consistently
// with the rest of the HUD. `outLen` must cover the worst case (sign + digits +
// suffix + NUL); 24 bytes is comfortably enough for any value this game reaches.
void formatQi(double v, char* out, size_t outLen);
