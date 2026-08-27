#include "raycast.h"
#include <cmath>

RayHit castRay(const RaycastMap& map, float originX, float originY, float dirX, float dirY,
               float maxDistance) {
    float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 1e-6f) return RayHit{maxDistance, 0.0f, 0, false};
    dirX /= len;
    dirY /= len;

    int mapX = static_cast<int>(std::floor(originX));
    int mapY = static_cast<int>(std::floor(originY));

    float deltaDistX = (dirX == 0.0f) ? 1e30f : std::fabs(1.0f / dirX);
    float deltaDistY = (dirY == 0.0f) ? 1e30f : std::fabs(1.0f / dirY);

    int stepX = (dirX < 0.0f) ? -1 : 1;
    int stepY = (dirY < 0.0f) ? -1 : 1;

    float sideDistX = (dirX < 0.0f) ? (originX - mapX) * deltaDistX
                                     : (mapX + 1.0f - originX) * deltaDistX;
    float sideDistY = (dirY < 0.0f) ? (originY - mapY) * deltaDistY
                                     : (mapY + 1.0f - originY) * deltaDistY;

    bool hitVertical = false;
    float traveled = 0.0f;

    while (traveled < maxDistance) {
        if (sideDistX < sideDistY) {
            traveled = sideDistX;
            sideDistX += deltaDistX;
            mapX += stepX;
            hitVertical = true;
        } else {
            traveled = sideDistY;
            sideDistY += deltaDistY;
            mapY += stepY;
            hitVertical = false;
        }

        if (traveled >= maxDistance) break;

        int cellValue = map.at(mapX, mapY);
        if (cellValue > 0) {
            RayHit hit;
            hit.distance = traveled;
            hit.wallType = cellValue;
            hit.hitVertical = hitVertical;
            if (hitVertical) {
                float hitY = originY + traveled * dirY;
                hit.wallX = hitY - std::floor(hitY);
            } else {
                float hitX = originX + traveled * dirX;
                hit.wallX = hitX - std::floor(hitX);
            }
            return hit;
        }
    }

    return RayHit{maxDistance, 0.0f, 0, hitVertical};
}

void castColumns(const RaycastMap& map, float camX, float camY, float facingRadians,
                  float fovRadians, int screenWidth, float maxDistance,
                  std::vector<WallHit>& outHits) {
    outHits.assign(static_cast<size_t>(screenWidth), WallHit{});
    for (int col = 0; col < screenWidth; ++col) {
        float t = (screenWidth == 1) ? 0.5f : static_cast<float>(col) / (screenWidth - 1);
        float rayAngle = facingRadians + (t - 0.5f) * fovRadians;
        RayHit raw = castRay(map, camX, camY, std::cos(rayAngle), std::sin(rayAngle), maxDistance);

        WallHit wh;
        wh.wallType = raw.wallType;
        wh.wallX = raw.wallX;
        wh.hitVertical = raw.hitVertical;
        float angleDiff = rayAngle - facingRadians;
        wh.distance = raw.distance * std::cos(angleDiff);
        outHits[static_cast<size_t>(col)] = wh;
    }
}

int wallSliceHeight(float correctedDistance, int viewportHeight) {
    float d = correctedDistance;
    if (d < 0.0001f) d = 0.0001f;
    return static_cast<int>(static_cast<float>(viewportHeight) / d);
}
