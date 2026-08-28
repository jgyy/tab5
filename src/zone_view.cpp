#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds

namespace {
M5Canvas* gZoneCanvas = nullptr;
int gViewportW = 0;
int gViewportH = 0;

uint32_t gAttackFlashUntilMs = 0; // flash on the monster - player's attack landed
uint32_t gHitFlashUntilMs = 0;    // flash on the character - enemy's attack landed
constexpr uint32_t kFlashDurationMs = 150;

// Reserve headroom above the tallest possible platform (kMaxPlatformHeight) for its monster's
// sprite (radius up to 40px below) plus margin, so nothing generated at the height ceiling
// clips off the top of the viewport.
constexpr int kTopMarginPx = 60;

int screenXFor(float worldX, float arenaWidth) {
    float frac = arenaWidth > 0.0f ? worldX / arenaWidth : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return static_cast<int>(frac * gViewportW);
}

int screenYFor(float worldY, int groundY) {
    float frac = worldY / kMaxPlatformHeight; // 0 (ground) .. 1 (tallest possible platform)
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int usableRise = groundY - kTopMarginPx;
    if (usableRise < 0) usableRise = 0;
    return groundY - static_cast<int>(frac * usableRise);
}

// realmIndex only changes on a (rare) realm breakthrough, but the background still has to be
// re-filled every frame regardless (platforms/monsters/the character move, so last frame's
// pixels underneath them can't just be left alone) - the viewport now covers most of the
// screen (see sceneViewportBottom() in ui.h), so that's already the single biggest per-frame
// fill here. Caching the two color565() conversions at least avoids redoing that math on every
// one of those fills for a value that's almost always unchanged from the last frame.
int gLastBackgroundRealm = -1;
uint16_t gSkyColor565 = 0;
uint16_t gGroundColor565 = 0;

void drawBackground(M5Canvas& canvas, int realmIndex) {
    if (realmIndex != gLastBackgroundRealm) {
        RGB sky = zoneSkyColor(realmIndex);
        RGB ground = zoneGroundColor(realmIndex);
        gSkyColor565 = canvas.color565(sky.r, sky.g, sky.b);
        gGroundColor565 = canvas.color565(ground.r, ground.g, ground.b);
        gLastBackgroundRealm = realmIndex;
    }
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    canvas.fillRect(0, 0, gViewportW, groundTop, gSkyColor565);
    canvas.fillRect(0, groundTop, gViewportW, gViewportH - groundTop, gGroundColor565);
}

void drawPlatform(M5Canvas& canvas, int screenX0, int screenX1, int screenY, RGB color) {
    constexpr int kLedgeThickness = 10;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillRect(screenX0, screenY, screenX1 - screenX0, kLedgeThickness, fill);
}

void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillCircle(screenX, standY - radius, radius, fill);
    canvas.fillCircle(screenX - radius / 3, standY - radius, 2, TFT_BLACK); // eye
    canvas.fillCircle(screenX + radius / 3, standY - radius, 2, TFT_BLACK); // eye
    if (isCurrent) {
        canvas.drawCircle(screenX, standY - radius, radius + 3, TFT_YELLOW);
    }
}

void drawCharacter(M5Canvas& canvas, int screenX, int standY, ZonePhase phase, uint32_t nowMs) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    constexpr int kAirborneLegTuck = 6; // airborne pose: legs tucked up higher than walk/idle
    bool walking = (phase == ZonePhase::Walking);
    bool jumping = (phase == ZonePhase::Jumping);
    int bob = (walking && ((nowMs / 150) % 2 == 0)) ? 0 : 2; // 2-frame walk cycle
    int legTuck = jumping ? kAirborneLegTuck : 0;
    int headY = standY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = standY - kBodyHeight + bob;
    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);
    canvas.fillRect(screenX - 5, standY - 4 + bob - legTuck, 4, 4, TFT_NAVY);          // left leg
    canvas.fillRect(screenX + 1, standY - (bob == 0 ? 4 : 8) - legTuck, 4, 4, TFT_NAVY); // right leg
}

void drawFlash(M5Canvas& canvas, int screenX, int standY, uint32_t nowMs, uint32_t untilMs) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, standY - 20, 6, TFT_YELLOW);
    canvas.drawCircle(screenX, standY - 20, 10, TFT_ORANGE);
}
} // namespace

void initZoneView(M5GFX& display) {
    if (gZoneCanvas) return;
    gViewportW = display.width();
    gViewportH = sceneViewportBottom(display.height()) - kHeaderHeight;
    gZoneCanvas = new M5Canvas(&display);
    gZoneCanvas->createSprite(gViewportW, gViewportH);
}

void renderZoneView(M5GFX& display, const ZoneState& state) {
    if (!gZoneCanvas) return;
    M5Canvas& canvas = *gZoneCanvas;
    uint32_t nowMs = millis();

    drawBackground(canvas, state.map.realmIndex);
    int groundY = static_cast<int>(gViewportH * 0.85f);

    RGB ledgeColor = platformColor(state.map.realmIndex);
    for (size_t i = 1; i < state.map.platforms.size(); ++i) { // [0] is the ground band above
        const Platform& p = state.map.platforms[i];
        int sx0 = screenXFor(p.x0, state.map.arenaWidth);
        int sx1 = screenXFor(p.x1, state.map.arenaWidth);
        int sy = screenYFor(p.y, groundY);
        drawPlatform(canvas, sx0, sx1, sy, ledgeColor);
    }

    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        bool isCurrent = (state.phase == ZonePhase::Fighting &&
                           state.currentMonsterIndex == static_cast<int>(i));
        const MonsterSpawn& spawn = state.map.monsters[i];
        const Platform& platform = state.map.platforms[static_cast<size_t>(spawn.platformIndex)];
        float liveX = isCurrent
            ? spawn.x
            : patrolPositionX(spawn.x, patrolRangeForPlatform(platform), state.walkingElapsedSeconds);
        int mx = screenXFor(liveX, state.map.arenaWidth);
        int my = screenYFor(platform.y, groundY);
        RGB color = monsterColor(state.map.realmIndex, static_cast<int>(i));
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent);
        if (isCurrent) drawFlash(canvas, mx, my, nowMs, gAttackFlashUntilMs);
    }

    int charX = screenXFor(state.posX, state.map.arenaWidth);
    int charY = screenYFor(state.posY, groundY);
    drawCharacter(canvas, charX, charY, state.phase, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);

    canvas.pushSprite(0, kHeaderHeight);
}

void triggerAttackFlash() {
    gAttackFlashUntilMs = millis() + kFlashDurationMs;
}

void triggerHitFlash() {
    gHitFlashUntilMs = millis() + kFlashDurationMs;
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
