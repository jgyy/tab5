#include "zone_view.h"
#include "zone_textures.h"
#include "ui.h" // kHeaderHeight, sceneViewportBottom() - shared viewport bounds
#include "skills.h"
#include "fx.h"
#include "hash.h"
#include <cstdio>
#include <cmath>

namespace {
M5Canvas* gZoneCanvas = nullptr;
int gViewportW = 0;
int gViewportH = 0;

uint32_t gAttackFlashUntilMs = 0; // flash on the monster - player's attack landed
uint32_t gHitFlashUntilMs = 0;    // flash on the character - enemy's attack landed
constexpr uint32_t kFlashDurationMs = 150;

// Last-known screen position of the current (or most recently current) enemy - a module-static
// cache, NOT a per-call local, because a skill FX or damage number can still be animating for
// up to kSkillFxTotalMs/kDamageNumberDurationMs after the enemy that triggered it is defeated
// (monstersDefeated[i] becomes true and currentMonsterIndex resets to -1 on the very same
// tickZone() call that lands the killing blow, so no monster is ever isCurrent again for that
// enemy). A per-call local reset to (0,0) every render would make any FX/number still in flight
// at the moment of a kill snap to the screen's top-left corner instead of the enemy's last
// position - this cache is what keeps it pinned there until a new monster becomes current.
int gLastEnemyScreenX = 0;
int gLastEnemyScreenY = 0;

// Skill projectile/impact/shake state - triggerSkillFx() latches these, renderZoneView()
// re-derives the current animation frame from elapsed time every call (position is static
// during Fighting, same reasoning the pre-existing attack/hit flash already relies on).
int gSkillFxIndex = -1;
uint32_t gSkillFxStartMs = 0;
constexpr uint32_t kSkillTravelMs = 220;
constexpr uint32_t kSkillImpactMs = 160;
constexpr uint32_t kSkillFxTotalMs = kSkillTravelMs + kSkillImpactMs;
constexpr uint32_t kShakeDurationMs = 140;
constexpr float kShakeAmplitudePx = 5.0f;
constexpr float kPi = 3.14159265358979323846f;

struct DamageNumber {
    bool active = false;
    bool onPlayer = false;
    int amount = 0;
    int skillIndex = -1; // -1 = plain autoattack hit
    uint32_t spawnMs = 0;
};
constexpr int kMaxDamageNumbers = 8;
DamageNumber gDamageNumbers[kMaxDamageNumbers];
constexpr uint32_t kDamageNumberDurationMs = 700;
constexpr float kDamageNumberRisePxPerSec = 40.0f;

// Fill/ring color pair for a skill's projectile and impact burst - color-only differentiation
// (not per-skill unique geometry) keeps this tractable across 8 skill kinds while still
// visually distinguishing which skill just fired.
void skillColors(SkillVisual visual, uint16_t& fillColor, uint16_t& ringColor) {
    switch (visual) {
        case SkillVisual::Slash:         fillColor = TFT_WHITE;    ringColor = TFT_LIGHTGREY; break;
        case SkillVisual::Fireball:      fillColor = TFT_ORANGE;   ringColor = TFT_RED;       break;
        case SkillVisual::FrostShard:    fillColor = TFT_CYAN;     ringColor = TFT_WHITE;     break;
        case SkillVisual::LightningBolt: fillColor = TFT_YELLOW;   ringColor = TFT_PURPLE;    break;
        case SkillVisual::VoidSpike:     fillColor = TFT_PURPLE;   ringColor = TFT_MAGENTA;   break;
        case SkillVisual::PhoenixNova:   fillColor = TFT_ORANGE;   ringColor = TFT_GOLD;      break;
        case SkillVisual::Earthquake:    fillColor = TFT_BROWN;    ringColor = TFT_OLIVE;     break;
        case SkillVisual::Starfall:      fillColor = TFT_SKYBLUE;  ringColor = TFT_WHITE;     break;
        default:                         fillColor = TFT_YELLOW;   ringColor = TFT_ORANGE;    break;
    }
}

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

constexpr int kNumParallaxElements = 6;
struct ParallaxElement { float seedX; float speedPxPerSec; };
ParallaxElement gParallax[kNumParallaxElements];
int gParallaxSeededForRealm = -1;

// (Re)seeds gParallax only when realmIndex changes - mirrors the existing background-color
// cache immediately above, so this isn't recomputed every frame.
void seedParallaxIfNeeded(int realmIndex) {
    if (realmIndex == gParallaxSeededForRealm) return;
    for (int i = 0; i < kNumParallaxElements; ++i) {
        gParallax[i].seedX = hashRange(realmIndex, 100 + i, 0.0f, static_cast<float>(gViewportW));
        gParallax[i].speedPxPerSec = hashRange(realmIndex, 200 + i, 4.0f, 12.0f);
    }
    gParallaxSeededForRealm = realmIndex;
}

// Drifting background dressing behind the platforms: clouds for lower realms, embers for
// mid realms, tiny stars for the highest realms - three visually distinct bands across the
// 16 realms, all driven by the same hash-based determinism this project already uses for
// terrain generation.
void drawParallax(M5Canvas& canvas, int realmIndex, uint32_t nowMs) {
    seedParallaxIfNeeded(realmIndex);
    float elapsedSeconds = static_cast<float>(nowMs) / 1000.0f;
    for (int i = 0; i < kNumParallaxElements; ++i) {
        int x = static_cast<int>(parallaxWrapX(gParallax[i].seedX, gParallax[i].speedPxPerSec,
                                                 elapsedSeconds, static_cast<float>(gViewportW)));
        int y = 20 + (i % 3) * 14; // a few staggered heights near the top of the sky band
        if (realmIndex < 6) {
            canvas.fillEllipse(x, y, 14, 6, TFT_WHITE);
        } else if (realmIndex < 12) {
            canvas.fillCircle(x, y, 3, TFT_ORANGE);
        } else {
            canvas.drawLine(x - 4, y, x + 4, y, TFT_WHITE);
            canvas.drawLine(x, y - 4, x, y + 4, TFT_WHITE);
        }
    }
}

// Deterministic ground tick-marks so the ground band isn't a flat color fill - reuses the
// same 0.75f split drawBackground() uses for where the ground color starts.
void drawGroundTexture(M5Canvas& canvas, int realmIndex) {
    constexpr int kNumTufts = 8;
    int groundTop = static_cast<int>(gViewportH * 0.75f);
    for (int i = 0; i < kNumTufts; ++i) {
        int x = static_cast<int>(hashRange(realmIndex, 300 + i, 0.0f, static_cast<float>(gViewportW)));
        canvas.drawLine(x, groundTop + 2, x, groundTop + 6, TFT_BLACK);
    }
}

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

void drawMonster(M5Canvas& canvas, int screenX, int standY, int maxHp, RGB color, bool isCurrent, int tierIndex) {
    int radius = 10 + maxHp / 15; // bigger monsters read as tougher
    if (radius > 40) radius = 40;
    uint16_t fill = canvas.color565(color.r, color.g, color.b);

    if (tierIndex <= 0) {
        // Tier 0: round "slime" body - today's original silhouette.
        canvas.fillCircle(screenX, standY - radius, radius, fill);
    } else if (tierIndex == 1) {
        // Tier 1: diamond body with 4 spikes around the rim - reads sharper/angrier.
        int cy = standY - radius;
        canvas.fillTriangle(screenX, cy - radius, screenX - radius, cy, screenX, cy + radius, fill);
        canvas.fillTriangle(screenX, cy - radius, screenX + radius, cy, screenX, cy + radius, fill);
        constexpr int kSpikes = 4;
        for (int s = 0; s < kSpikes; ++s) {
            float angle = (6.2831853f / kSpikes) * static_cast<float>(s);
            int tipX = screenX + static_cast<int>((radius + 6) * std::cos(angle));
            int tipY = cy + static_cast<int>((radius + 6) * std::sin(angle));
            int baseX1 = screenX + static_cast<int>(radius * std::cos(angle - 0.2f));
            int baseY1 = cy + static_cast<int>(radius * std::sin(angle - 0.2f));
            int baseX2 = screenX + static_cast<int>(radius * std::cos(angle + 0.2f));
            int baseY2 = cy + static_cast<int>(radius * std::sin(angle + 0.2f));
            canvas.fillTriangle(tipX, tipY, baseX1, baseY1, baseX2, baseY2, fill);
        }
    } else {
        // Tier 2: biggest body plus wing/horn shapes - reads as the toughest silhouette.
        int bigRadius = radius + 6;
        int cy = standY - bigRadius;
        canvas.fillCircle(screenX, cy, bigRadius, fill);
        canvas.fillTriangle(screenX - bigRadius, cy, screenX - bigRadius - 10, cy - 8, screenX - bigRadius - 10, cy + 8, fill);
        canvas.fillTriangle(screenX + bigRadius, cy, screenX + bigRadius + 10, cy - 8, screenX + bigRadius + 10, cy + 8, fill);
        radius = bigRadius; // so the eyes/current-ring below sit correctly on the enlarged body
    }

    int eyeY = standY - radius;
    canvas.fillCircle(screenX - radius / 3, eyeY, 2, TFT_BLACK);
    canvas.fillCircle(screenX + radius / 3, eyeY, 2, TFT_BLACK);
    if (isCurrent) {
        canvas.drawCircle(screenX, eyeY, radius + 3, TFT_YELLOW);
    }
}

void drawCharacter(M5Canvas& canvas, int screenX, int standY, ZonePhase phase, uint32_t nowMs, int realmIndex) {
    constexpr int kBodyHeight = 26;
    constexpr int kHeadRadius = 7;
    constexpr int kAirborneLegTuck = 6; // airborne pose: legs tucked up higher than walk/idle
    constexpr int kArmLength = 10;
    constexpr uint32_t kCastingPoseMs = 200;
    bool walking = (phase == ZonePhase::Walking);
    bool jumping = (phase == ZonePhase::Jumping);
    bool casting = gSkillFxIndex >= 0 && (nowMs - gSkillFxStartMs) < kCastingPoseMs;

    constexpr int kWalkBobFrames[4] = {0, 1, 2, 1}; // 4-frame walk cycle (was 2-frame)
    int bob = walking ? kWalkBobFrames[(nowMs / 100) % 4] : 0;
    int legTuck = jumping ? kAirborneLegTuck : 0;
    int headY = standY - kBodyHeight - kHeadRadius + bob;
    int bodyTop = standY - kBodyHeight + bob;
    int shoulderY = bodyTop + 4;

    RGB aura = characterAuraColor(realmIndex);
    uint16_t auraColor = canvas.color565(aura.r, aura.g, aura.b);
    canvas.drawCircle(screenX, (headY + standY) / 2, kBodyHeight, auraColor);

    canvas.fillCircle(screenX, headY, kHeadRadius, TFT_WHITE);
    canvas.fillRect(screenX - 5, bodyTop, 10, kBodyHeight, TFT_BLUE);

    if (casting) {
        // Arms raised overhead, synced to a fired skill's opening frames.
        canvas.drawLine(screenX - 5, shoulderY, screenX - kArmLength, shoulderY - kArmLength, TFT_WHITE);
        canvas.drawLine(screenX + 5, shoulderY, screenX + kArmLength, shoulderY - kArmLength, TFT_WHITE);
    } else {
        canvas.drawLine(screenX - 5, shoulderY, screenX - kArmLength, shoulderY + kArmLength / 2, TFT_WHITE);
        canvas.drawLine(screenX + 5, shoulderY, screenX + kArmLength, shoulderY + kArmLength / 2, TFT_WHITE);
    }

    canvas.fillRect(screenX - 5, standY - 4 + bob - legTuck, 4, 4, TFT_NAVY); // left leg
    canvas.fillRect(screenX + 1, standY - 4 - bob - legTuck, 4, 4, TFT_NAVY); // right leg - moves opposite the left for a scissor gait
}

void drawFlash(M5Canvas& canvas, int screenX, int standY, uint32_t nowMs, uint32_t untilMs,
                int fillRadius = 6, int ringRadius = 10,
                uint16_t fillColor = TFT_YELLOW, uint16_t ringColor = TFT_ORANGE) {
    if (nowMs >= untilMs) return;
    canvas.fillCircle(screenX, standY - 20, fillRadius, fillColor);
    canvas.drawCircle(screenX, standY - 20, ringRadius, ringColor);
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
    drawParallax(canvas, state.map.realmIndex, nowMs);
    drawGroundTexture(canvas, state.map.realmIndex);
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
        drawMonster(canvas, mx, my, spawn.maxHp, color, isCurrent, static_cast<int>(i));
        if (isCurrent) {
            drawFlash(canvas, mx, my, nowMs, gAttackFlashUntilMs);
            gLastEnemyScreenX = mx;
            gLastEnemyScreenY = my;
        }
    }

    int charX = screenXFor(state.posX, state.map.arenaWidth);
    int charY = screenYFor(state.posY, groundY);
    drawCharacter(canvas, charX, charY, state.phase, nowMs, state.map.realmIndex);
    if (state.phase == ZonePhase::Fighting) drawFlash(canvas, charX, charY, nowMs, gHitFlashUntilMs);

    uint32_t skillElapsed = nowMs - gSkillFxStartMs;
    float shakeX = 0.0f;
    float shakeY = 0.0f;
    if (gSkillFxIndex >= 0 && skillElapsed < kSkillFxTotalMs) {
        uint16_t fillColor, ringColor;
        skillColors(SKILLS[gSkillFxIndex].visual, fillColor, ringColor);
        if (skillElapsed < kSkillTravelMs) {
            float t = static_cast<float>(skillElapsed) / static_cast<float>(kSkillTravelMs);
            int px = charX + static_cast<int>((gLastEnemyScreenX - charX) * t);
            int py = charY + static_cast<int>((gLastEnemyScreenY - charY) * t);
            canvas.fillCircle(px, py - 20, 5, fillColor); // -20 keeps it roughly chest-height
        } else {
            drawFlash(canvas, gLastEnemyScreenX, gLastEnemyScreenY, nowMs,
                      gSkillFxStartMs + kSkillFxTotalMs, 10, 16, fillColor, ringColor);
        }
        if (skillElapsed < kShakeDurationMs) {
            float shakeT = static_cast<float>(skillElapsed) / static_cast<float>(kShakeDurationMs);
            shakeX = shakeOffset(shakeT, kShakeAmplitudePx, 0.0f);
            shakeY = shakeOffset(shakeT, kShakeAmplitudePx, kPi / 2.0f);
        }
    }

    for (int i = 0; i < kMaxDamageNumbers; ++i) {
        DamageNumber& dn = gDamageNumbers[i];
        if (!dn.active) continue;
        uint32_t elapsed = nowMs - dn.spawnMs;
        if (elapsed >= kDamageNumberDurationMs) { dn.active = false; continue; }
        int baseX = dn.onPlayer ? charX : gLastEnemyScreenX;
        int baseY = dn.onPlayer ? charY : gLastEnemyScreenY;
        float rise = damageNumberRiseOffsetPx(static_cast<float>(elapsed) / 1000.0f, kDamageNumberRisePxPerSec);
        int drawY = baseY - 30 + static_cast<int>(rise); // -30 starts above the head, not the feet
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", dn.amount);
        uint16_t color = TFT_WHITE;
        if (dn.skillIndex >= 0) {
            uint16_t unusedRing;
            skillColors(SKILLS[dn.skillIndex].visual, color, unusedRing);
        }
        canvas.setTextSize(dn.skillIndex >= 0 ? 3 : 2);
        canvas.setTextColor(color);
        canvas.setCursor(baseX - canvas.textWidth(buf) / 2, drawY);
        canvas.print(buf);
    }

    canvas.pushSprite(static_cast<int>(shakeX), kHeaderHeight + static_cast<int>(shakeY));
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

void spawnDamageNumber(bool onPlayer, int amount, int skillIndex) {
    int slot = -1;
    for (int i = 0; i < kMaxDamageNumbers; ++i) {
        if (!gDamageNumbers[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < kMaxDamageNumbers; ++i) {
            if (gDamageNumbers[i].spawnMs < gDamageNumbers[slot].spawnMs) slot = i;
        }
    }
    gDamageNumbers[slot] = DamageNumber{true, onPlayer, amount, skillIndex, millis()};
}

void triggerSkillFx(int skillIndex) {
    gSkillFxIndex = skillIndex;
    gSkillFxStartMs = millis();
}

void playSkillSfx(int skillIndex) {
    float base = 440.0f + 60.0f * static_cast<float>(skillIndex);
    M5.Speaker.tone(base, 50);
    delay(40);
    M5.Speaker.tone(base * 1.5f, 70);
}
