#include "ui.h"
#include "trial_state.h" // full CombatantState/TrialState definition - see the forward decl note in ui.h
#include <cstdio>
#include <cstring>
#include <cmath>

// Compact display formatting for Qi-scale numbers, which grow into the tens of
// millions (REALM_QI_THRESHOLD tops out at 27,000,000) — a raw "%.0f" would
// overflow a bar's label at any reasonable text size. Declared in ui.h (not
// anonymous-namespace-local) so main.cpp's welcome-back screen can reuse it.
void formatQi(double v, char* out, size_t outLen) {
    double av = v < 0 ? -v : v;
    const char* sign = v < 0 ? "-" : "";
    if (av < 1000.0) {
        snprintf(out, outLen, "%s%.1f", sign, av);
    } else if (av < 1e6) {
        snprintf(out, outLen, "%s%.1fK", sign, av / 1e3);
    } else if (av < 1e9) {
        snprintf(out, outLen, "%s%.1fM", sign, av / 1e6);
    } else {
        snprintf(out, outLen, "%s%.1fB", sign, av / 1e9);
    }
}

int sceneViewportBottom(int screenH) {
    return kHeaderHeight + (screenH - kHeaderHeight) / 2;
}

namespace {

// ---- Layout tuning ----
constexpr int kPanelTopPad = 12;
constexpr int kSectionGap = 10;
constexpr int kBreakthroughBarHeight = 40;
constexpr int kHpBarHeight = 36;
constexpr int kRouteBarHeight = 28;
constexpr int kSettingsRowHeight = 48; // compact: one row each for brightness and volume

struct Layout {
    int screenW = 0;
    int screenH = 0;
    int panelY0 = 0;      // absolute y where the stats panel (below the raycast viewport) starts
    int panelH = 0;
    int breakthroughY = 0;
    int playerHpY = 0;
    int enemyHpY = 0;
    int routeY = 0;
    int brightnessY = 0;
    int volumeY = 0;
};
Layout gLayout;

void computeLayout(int screenW, int screenH) {
    gLayout.screenW = screenW;
    gLayout.screenH = screenH;
    gLayout.panelY0 = sceneViewportBottom(screenH);
    gLayout.panelH = screenH - gLayout.panelY0;

    int y = gLayout.panelY0 + kPanelTopPad;
    gLayout.breakthroughY = y; y += kBreakthroughBarHeight + kSectionGap;
    gLayout.playerHpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.enemyHpY = y; y += kHpBarHeight + kSectionGap;
    gLayout.routeY = y; y += kRouteBarHeight + kSectionGap;
    gLayout.brightnessY = y; y += kSettingsRowHeight + kSectionGap;
    gLayout.volumeY = y;
}

Rect breakthroughRect() { return Rect{0, gLayout.breakthroughY, gLayout.screenW, kBreakthroughBarHeight}; }
Rect playerHpRect() { return Rect{0, gLayout.playerHpY, gLayout.screenW, kHpBarHeight}; }
Rect enemyHpRect() { return Rect{0, gLayout.enemyHpY, gLayout.screenW, kHpBarHeight}; }
Rect routeRect() { return Rect{0, gLayout.routeY, gLayout.screenW, kRouteBarHeight}; }
Rect brightnessRowRect() { return Rect{0, gLayout.brightnessY, gLayout.screenW, kSettingsRowHeight}; }
Rect volumeRowRect() { return Rect{0, gLayout.volumeY, gLayout.screenW, kSettingsRowHeight}; }

// Each settings row is one tappable strip split into a left ("-") and right ("+") half,
// rather than four separate button rects, to keep the panel footprint compact.
Rect leftHalf(const Rect& r) { return Rect{r.x, r.y, r.w / 2, r.h}; }
Rect rightHalf(const Rect& r) { return Rect{r.x + r.w / 2, r.y, r.w - r.w / 2, r.h}; }

M5Canvas* gHeaderCanvas = nullptr;
M5Canvas* gPanelCanvas = nullptr;

// Picks the largest text size in [1, startSize] at which `text` fits within
// maxWidth (measured with textWidth(), not assumed), sets it on `canvas`, and
// returns it. Falls back to size 1 (smallest supported) if nothing fits.
int fitTextSize(M5Canvas& canvas, const char* text, int maxWidth, int startSize) {
    for (int size = startSize; size >= 1; --size) {
        canvas.setTextSize(size);
        if (canvas.textWidth(text) <= maxWidth) return size;
    }
    canvas.setTextSize(1);
    return 1;
}

void drawLeftAligned(M5Canvas& canvas, const char* text, int x, int yCenter, int maxWidth, int startSize,
                      uint16_t fg, uint16_t bg) {
    fitTextSize(canvas, text, maxWidth, startSize);
    canvas.setTextColor(fg, bg);
    canvas.setCursor(x, yCenter - canvas.fontHeight() / 2);
    canvas.print(text);
}

void drawRightAligned(M5Canvas& canvas, const char* text, int xRight, int yCenter, int maxWidth, int startSize,
                       uint16_t fg, uint16_t bg) {
    fitTextSize(canvas, text, maxWidth, startSize);
    int w = canvas.textWidth(text);
    canvas.setTextColor(fg, bg);
    canvas.setCursor(xRight - w, yCenter - canvas.fontHeight() / 2);
    canvas.print(text);
}

// Draws a two-tone progress bar (filled portion in `fillColor`, unfilled in dark grey) with a
// left-aligned label overlaid in transparent white text - a solid fg/bg color pair would only
// match one of the bar's two background colors, so this uses the single-argument
// setTextColor() (transparent background, only glyph pixels drawn) instead. `fraction` is
// clamped to [0,1] so a caller passing a raw ratio can't overflow the bar.
void drawBar(M5Canvas& canvas, const Rect& r, float fraction, uint16_t fillColor, const char* label) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    int ly = r.y - gLayout.panelY0;
    canvas.fillRect(0, ly, r.w, r.h, TFT_DARKGREY);
    int fillW = static_cast<int>(r.w * fraction);
    if (fillW > 0) canvas.fillRect(0, ly, fillW, r.h, fillColor);
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(12, ly + (r.h - canvas.fontHeight()) / 2);
    canvas.print(label);
}

void drawHeader(M5GFX& display, const GameState& state) {
    M5Canvas& hdr = *gHeaderCanvas;
    constexpr uint16_t kHeaderBg = 0x18E3; // dark navy-grey, distinct from the panel's black
    hdr.fillScreen(kHeaderBg);

    char qiRateStr[24];
    formatQi(qiPerSecond(state), qiRateStr, sizeof(qiRateStr));
    char leftBuf[64];
    snprintf(leftBuf, sizeof(leftBuf), "%s  Qi/s %s", REALM_NAMES[state.realmIndex], qiRateStr);

    // No clock/time-of-day display: without NTP/internet sync (out of scope for this
    // project), a displayed clock would silently drift from real time, which is worse
    // than not showing one. The RTC is still seeded/used for offline-earnings elapsed-
    // time math (see rtc_store.h) — that's unaffected by not displaying it.
    int32_t batteryLevel = M5.Power.getBatteryLevel(); // -1 if unavailable (see M5Unified Power_Class)
    auto chargeState = M5.Power.isCharging();           // is_charging_t, NOT a bool
    bool charging = (chargeState == decltype(chargeState)::is_charging);

    char rightBuf[16];
    if (batteryLevel < 0) {
        snprintf(rightBuf, sizeof(rightBuf), "Batt --");
    } else {
        snprintf(rightBuf, sizeof(rightBuf), "%ld%%%s", static_cast<long>(batteryLevel), charging ? "+" : "");
    }

    int rightMaxWidth = gLayout.screenW / 4;
    int leftMaxWidth = gLayout.screenW - rightMaxWidth - 24;
    int headerCenterY = kHeaderHeight / 2;

    drawLeftAligned(hdr, leftBuf, 12, headerCenterY, leftMaxWidth, 2, TFT_WHITE, kHeaderBg);
    drawRightAligned(hdr, rightBuf, gLayout.screenW - 12, headerCenterY, rightMaxWidth, 2, TFT_WHITE, kHeaderBg);

    hdr.pushSprite(0, 0);
}

} // namespace

void initHud(M5GFX& display) {
    if (gPanelCanvas) return; // already initialized; safe to call more than once
    computeLayout(display.width(), display.height());

    gHeaderCanvas = new M5Canvas(&display);
    gHeaderCanvas->createSprite(gLayout.screenW, kHeaderHeight);

    gPanelCanvas = new M5Canvas(&display);
    gPanelCanvas->createSprite(gLayout.screenW, gLayout.panelH);
}

void drawHud(M5GFX& display, const GameState& state, const TrialState& trial,
             uint8_t brightness, uint8_t volume) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);

    M5Canvas& panel = *gPanelCanvas;
    panel.fillScreen(TFT_BLACK);

    float breakthroughFraction = 1.0f;
    char btLabel[40];
    if (state.realmIndex < NUM_REALMS - 1) {
        breakthroughFraction =
            static_cast<float>(state.qi / REALM_QI_THRESHOLD[state.realmIndex + 1]);
        snprintf(btLabel, sizeof(btLabel), "Breakthrough %d%%",
                 static_cast<int>(breakthroughFraction * 100));
    } else {
        snprintf(btLabel, sizeof(btLabel), "Max Realm Reached");
    }
    drawBar(panel, breakthroughRect(), breakthroughFraction, TFT_ORANGE, btLabel);

    float playerFraction = trial.player.maxHp > 0
        ? static_cast<float>(trial.player.hp) / static_cast<float>(trial.player.maxHp)
        : 0.0f;
    char playerLabel[32];
    snprintf(playerLabel, sizeof(playerLabel), "Player HP %d/%d", trial.player.hp, trial.player.maxHp);
    drawBar(panel, playerHpRect(), playerFraction, TFT_GREEN, playerLabel);

    bool fighting = (trial.phase == TrialPhase::Fighting);
    float enemyFraction = (fighting && trial.enemy.maxHp > 0)
        ? static_cast<float>(trial.enemy.hp) / static_cast<float>(trial.enemy.maxHp)
        : 0.0f;
    char enemyLabel[32];
    if (fighting) {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP %d/%d", trial.enemy.hp, trial.enemy.maxHp);
    } else {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP --");
    }
    drawBar(panel, enemyHpRect(), enemyFraction, TFT_RED, enemyLabel);

    float routeFraction;
    char routeLabel[32];
    if (trial.phase == TrialPhase::Cleared) {
        routeFraction = 1.0f;
        snprintf(routeLabel, sizeof(routeLabel), "Cleared!");
    } else {
        int lastIndex = trial.map.route.size() > 1 ? static_cast<int>(trial.map.route.size()) - 1 : 1;
        routeFraction = static_cast<float>(trial.currentWaypointIndex) / static_cast<float>(lastIndex);
        snprintf(routeLabel, sizeof(routeLabel), "Route %d/%d", trial.currentWaypointIndex, lastIndex);
    }
    drawBar(panel, routeRect(), routeFraction, TFT_CYAN, routeLabel);

    Rect brRow = brightnessRowRect();
    int brly = brRow.y - gLayout.panelY0;
    panel.fillRect(0, brly, brRow.w, brRow.h, TFT_DARKGREY);
    char brLine[32];
    snprintf(brLine, sizeof(brLine), "-  Brightness %d%%  +", (brightness * 100) / 255);
    drawLeftAligned(panel, brLine, 12, brly + brRow.h / 2, brRow.w - 24, 2, TFT_WHITE, TFT_DARKGREY);

    Rect volRow = volumeRowRect();
    int voly = volRow.y - gLayout.panelY0;
    panel.fillRect(0, voly, volRow.w, volRow.h, TFT_DARKGREY);
    char volLine[32];
    snprintf(volLine, sizeof(volLine), "-  Volume %d%%  +", (volume * 100) / 255);
    drawLeftAligned(panel, volLine, 12, voly + volRow.h / 2, volRow.w - 24, 2, TFT_WHITE, TFT_DARKGREY);

    panel.pushSprite(0, gLayout.panelY0);
}

int hitTestHud(int touchX, int touchY) {
    Rect brRow = brightnessRowRect();
    if (rectContains(leftHalf(brRow), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_DOWN;
    if (rectContains(rightHalf(brRow), touchX, touchY)) return HUD_BUTTON_BRIGHTNESS_UP;
    Rect volRow = volumeRowRect();
    if (rectContains(leftHalf(volRow), touchX, touchY)) return HUD_BUTTON_VOLUME_DOWN;
    if (rectContains(rightHalf(volRow), touchX, touchY)) return HUD_BUTTON_VOLUME_UP;
    return HUD_BUTTON_NONE;
}
