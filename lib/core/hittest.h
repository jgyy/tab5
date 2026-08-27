#pragma once

struct Rect { int x, y, w, h; };

// Half-open bounds [x, x+w) x [y, y+h) — a point on the right/bottom edge is NOT
// contained, so adjacent buttons sharing an edge never both claim the same touch.
inline bool rectContains(const Rect& r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}
