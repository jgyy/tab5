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

int screenXFor(float worldX) {
    float frac = worldX / kArenaWidth;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return static_cast<int>(frac * gViewportW);
}

void drawBackground(M5Canvas& canvas, int realmIndex) {
    RGB sky = zoneSkyColor(realmIndex);
    RGB ground = zoneGroundColor(realmIndex);
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    canvas.fillRect(0, 0, gViewportW, groundTop, canvas.color565(sky.r, sky.g, sky.b));
    canvas.fillRect(0, groundTop, gViewportW, gViewportH - groundTop,
                     canvas.color565(ground.r, ground.g, ground.b));
}

void drawMonster(M5Canvas& canvas, int screenX, int groundY, int maxHp, RGB color, bool isCurrent) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);
    canvas.fillCircle(screenX, groundY - radius, radius, fill);
    canvas.fillCircle(screenX - radius / 3, groundY - radius, 2, TFT_BLACK); // eye
    canvas.fillCircle(screenX + radius / 3, groundY - radius, 2, TFT_BLACK); // eye
    if (isCurrent) {
        canvas.drawCircle(screenX, groundY - radius, radius + 3, TFT_YELLOW);
    }
}

void drawCharacter(M5Canvas& canvas, int screenX, int groundY, bool walking, uint32_t nowMs) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    int bob = (walking && ((nowMs / 150) % 2 == 0)) ? 0 : 2; // 2-frame walk cycle
    int headY = groundY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = groundY - kBodyHeight + bob;
    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);
    canvas.fillRect(screenX - 5, groundY - 4 + bob, 4, 4, TFT_NAVY);          // left leg
    canvas.fillRect(screenX + 1, groundY - (bob == 0 ? 4 : 8), 4, 4, TFT_NAVY); // right leg (alternates)
}

void drawFlash(M5Canvas& canvas, int screenX, int groundY, uint32_t nowMs, uint32_t untilMs) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, groundY - 20, 6, TFT_YELLOW);
    canvas.drawCircle(screenX, groundY - 20, 10, TFT_ORANGE);
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

    for (size_t i = 0; i < state.map.monsters.size(); ++i) {
        if (state.monstersDefeated[i]) continue;
        bool isCurrent = (state.phase == ZonePhase::Fighting &&
                           state.currentMonsterIndex == static_cast<int>(i));
        int mx = screenXFor(state.map.monsters[i].x);
        RGB color = monsterColor(state.map.realmIndex, static_cast<int>(i));
        drawMonster(canvas, mx, groundY, state.map.monsters[i].maxHp, color, isCurrent);
        if (isCurrent) drawFlash(canvas, mx, groundY, nowMs, gAttackFlashUntilMs);
    }

    int charX = screenXFor(state.posX);
    drawCharacter(canvas, charX, groundY, state.phase == ZonePhase::Walking, nowMs);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, groundY, nowMs, gHitFlashUntilMs);

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
