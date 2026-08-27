#include "trial_view.h"
#include "raycast.h"
#include "trial_textures.h"
#include "color.h"
#include "ui.h" // kHeaderHeight, raycastViewportBottom() - shared viewport bounds
#include <cmath>
#include <vector>

namespace {
// The raycaster computes at this resolution - deliberately close to the crystal's old
// hardware-proven 240x240 pixel-fill cost (240x320 = ~33% more pixels, same order of
// magnitude), then displayed scaled up via pushRotateZoom (kTrialZoom) to fill the raycast
// viewport without paying full-resolution compute cost.
constexpr int kTrialViewWidth = 240;
constexpr int kTrialViewHeight = 320;
// May need retuning on real hardware: this value was tuned for the old "between header and
// return-button strip" region (roughly the whole screen minus the header); the viewport is
// now deliberately half that height. See the design spec's Open Risk note - unverified without
// a physical Tab5.
constexpr float kTrialZoom = 2.5f;
constexpr float kFovRadians = 1.02f;  // ~60 degrees
constexpr float kMaxRayDistance = 20.0f;

M5Canvas* gTrialCanvas = nullptr;
std::vector<WallHit> gColumnHits;
std::vector<uint16_t> gPixelBuffer; // RGB565, row-major, for pushImage

RGB wallBaseColorFor(int wallType) {
    if (wallType == 1) return RGB{90, 90, 110};  // boundary stone
    return RGB{110, 70, 150};                     // inner spirit-veined stone
}

// Projects one enemy as a camera-facing billboard into gPixelBuffer, depth-testing each
// column against that column's already-computed wall distance (gColumnHits) so a nearer
// wall correctly hides the enemy - the standard Doom/Wolfenstein sprite-occlusion technique.
// Must run after the wall pass has filled gColumnHits/gPixelBuffer for this frame and before
// pushImage(). No-ops (returns without drawing) if the enemy is out of range or outside the
// field of view.
void drawEnemyBillboard(float camX, float camY, float facingRadians, float enemyX, float enemyY,
                         bool isCurrentEncounter) {
    float dx = enemyX - camX;
    float dy = enemyY - camY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.2f || dist > kMaxRayDistance) return;

    float relAngle = std::atan2(dy, dx) - facingRadians;
    while (relAngle > 3.14159265f) relAngle -= 6.28318531f;
    while (relAngle < -3.14159265f) relAngle += 6.28318531f;
    if (std::fabs(relAngle) > kFovRadians * 0.5f + 0.15f) return; // outside view (+ small margin)

    float correctedDist = dist * std::cos(relAngle);
    if (correctedDist < 0.1f) return;

    float t = 0.5f + relAngle / kFovRadians;
    int centerCol = static_cast<int>(t * (kTrialViewWidth - 1));

    int spriteHeight = wallSliceHeight(correctedDist, kTrialViewHeight);
    if (spriteHeight > kTrialViewHeight * 2) spriteHeight = kTrialViewHeight * 2;
    int spriteWidth = static_cast<int>(spriteHeight * 0.6f);
    int top = (kTrialViewHeight - spriteHeight) / 2;
    int bottom = top + spriteHeight;

    RGB color = isCurrentEncounter ? RGB{220, 60, 60} : RGB{160, 40, 90};
    int left = centerCol - spriteWidth / 2;
    int right = centerCol + spriteWidth / 2;

    for (int col = left; col <= right; ++col) {
        if (col < 0 || col >= kTrialViewWidth) continue;
        if (correctedDist >= gColumnHits[static_cast<size_t>(col)].distance) continue; // behind a wall
        for (int y = top; y < bottom; ++y) {
            if (y < 0 || y >= kTrialViewHeight) continue;
            gPixelBuffer[static_cast<size_t>(y) * kTrialViewWidth + col] =
                gTrialCanvas->color565(color.r, color.g, color.b);
        }
    }
}
} // namespace

void initTrialView(M5GFX& display) {
    gTrialCanvas = new M5Canvas(&display);
    gTrialCanvas->createSprite(kTrialViewWidth, kTrialViewHeight);
    gColumnHits.reserve(static_cast<size_t>(kTrialViewWidth));
    gPixelBuffer.assign(static_cast<size_t>(kTrialViewWidth) * kTrialViewHeight, 0);
}

void renderTrialView(M5GFX& display, const TrialState& state) {
    if (!gTrialCanvas) return;

    castColumns(state.map.grid, state.posX, state.posY, state.facingRadians, kFovRadians,
                kTrialViewWidth, kMaxRayDistance, gColumnHits);

    for (int col = 0; col < kTrialViewWidth; ++col) {
        const WallHit& hit = gColumnHits[static_cast<size_t>(col)];
        int sliceHeight = wallSliceHeight(hit.distance, kTrialViewHeight);
        if (sliceHeight > kTrialViewHeight) sliceHeight = kTrialViewHeight;
        int top = (kTrialViewHeight - sliceHeight) / 2;
        int bottom = top + sliceHeight;

        RGB base = wallBaseColorFor(hit.wallType);
        // Shade the darker of the two DDA hit orientations to fake directional lighting,
        // matching the crystal renderer's old cheap-but-effective shading philosophy.
        if (!hit.hitVertical) {
            base.r = static_cast<uint8_t>(base.r * 0.75f);
            base.g = static_cast<uint8_t>(base.g * 0.75f);
            base.b = static_cast<uint8_t>(base.b * 0.75f);
        }

        for (int y = 0; y < kTrialViewHeight; ++y) {
            RGB pixel;
            if (y < top) {
                pixel = RGB{20, 20, 30}; // ceiling
            } else if (y >= bottom) {
                pixel = RGB{35, 30, 25}; // floor
            } else {
                float v = static_cast<float>(y - top) / static_cast<float>(sliceHeight);
                pixel = sampleWallTexture(hit.wallType, hit.wallX, v, base);
            }
            gPixelBuffer[static_cast<size_t>(y) * kTrialViewWidth + col] =
                gTrialCanvas->color565(pixel.r, pixel.g, pixel.b);
        }
    }

    for (size_t i = 0; i < state.map.enemies.size(); ++i) {
        if (state.enemiesDefeated[i]) continue;
        bool isCurrent = (state.phase == TrialPhase::Fighting &&
                           state.currentEnemyIndex == static_cast<int>(i));
        drawEnemyBillboard(state.posX, state.posY, state.facingRadians,
                            state.map.enemies[i].x, state.map.enemies[i].y, isCurrent);
    }

    gTrialCanvas->pushImage(0, 0, kTrialViewWidth, kTrialViewHeight, gPixelBuffer.data());

    // Scaled push: displays the small internal buffer stretched to fill the raycast viewport,
    // centered horizontally and vertically within it. Default sprite pivot is its own center,
    // so (centerX, centerY) here is where that center lands on the physical display.
    float availableTop = kHeaderHeight;
    float availableBottom = raycastViewportBottom(display.height());
    float centerX = display.width() / 2.0f;
    float centerY = availableTop + (availableBottom - availableTop) / 2.0f;
    gTrialCanvas->pushRotateZoom(centerX, centerY, 0.0f, kTrialZoom, kTrialZoom);
}

void playAttackSfx() {
    M5.Speaker.tone(880.0f, 60);
}

void playHitSfx() {
    M5.Speaker.tone(220.0f, 100);
}

void playVictorySfx() {
    M5.Speaker.tone(660.0f, 80);
    delay(90);
    M5.Speaker.tone(880.0f, 80);
    delay(90);
    M5.Speaker.tone(1320.0f, 160);
}
