#pragma once
#include <vector>

struct RaycastMap {
    int width = 0;
    int height = 0;
    std::vector<int> cells; // row-major: cells[y * width + x]. 0 = open floor, >0 = wall type id.

    int at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 1; // out of bounds reads as solid
        return cells[static_cast<size_t>(y) * width + x];
    }
};

struct RayHit {
    float distance = 0.0f;  // raw Euclidean distance from the ray origin to the hit point
    float wallX = 0.0f;     // fractional position along the hit wall face, in [0,1)
    int wallType = 0;       // cell value at the hit; 0 means no hit within maxDistance
    bool hitVertical = false; // true if a vertical grid line (a north/south-facing wall) was hit
};

// Casts one ray from (originX, originY) in grid space, direction (dirX, dirY) (need not be
// normalized), through `map` via DDA grid stepping. Returns the nearest wall hit, or a RayHit
// with wallType == 0 and distance == maxDistance if nothing is hit within maxDistance.
RayHit castRay(const RaycastMap& map, float originX, float originY, float dirX, float dirY,
               float maxDistance);

struct WallHit {
    float distance = 0.0f;  // fisheye-corrected perpendicular distance; use directly as depth
    float wallX = 0.0f;
    int wallType = 0;
    bool hitVertical = false;
};

// Casts `screenWidth` rays fanned evenly across `fovRadians`, centered on `facingRadians`, from
// camera position (camX, camY). Fills `outHits` (resized to screenWidth) with one WallHit per
// column, left to right, with `distance` corrected for fisheye distortion (multiplied by
// cos(rayAngle - facingRadians)) so it can be used directly as a per-column depth value.
void castColumns(const RaycastMap& map, float camX, float camY, float facingRadians,
                  float fovRadians, int screenWidth, float maxDistance,
                  std::vector<WallHit>& outHits);

// Projected on-screen pixel height of a wall slice at the given fisheye-corrected perpendicular
// distance, for a viewport of `viewportHeight` pixels. Distance is clamped to a small minimum
// to avoid division blowup.
int wallSliceHeight(float correctedDistance, int viewportHeight);
