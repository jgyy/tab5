#include "ui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Compact display formatting for Qi-scale numbers, which grow into the tens of
// millions (REALM_QI_THRESHOLD tops out at 27,000,000) — a raw "%.0f" would
// overflow a generator row or the header at any reasonable text size. Declared in
// ui.h (not anonymous-namespace-local) so main.cpp's welcome-back screen can reuse
// it for the same K/M/B formatting the rest of the HUD uses.
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

namespace {

// ---- Layout tuning ----
// The real M5Tab5 panel is 720x1280 (portrait) — confirmed by reading it
// straight from the fetched M5GFX source and from a live [DISPLAY] serial
// print, NOT the 1280x720 landscape shape a "tablet" name suggests. So the
// layout here is a vertical stack (header -> crystal -> panel), each spanning
// the screen's full width, rather than a landscape side-by-side split.
constexpr int kPanelTopPad = 12;
constexpr int kPanelBottomPad = 16;
constexpr int kSectionGap = 8;
constexpr int kQiReadoutHeight = 70;
constexpr int kBreakthroughHeight = 72;
constexpr int kRowMinHeight = 40; // floor so rows never collapse to nothing on a tiny display

struct Layout {
    int screenW = 0;
    int screenH = 0;
    int crystalX = 0;
    int crystalY = 0;
    int panelY0 = 0;      // absolute y where the HUD panel (below the crystal) starts
    int panelH = 0;
    int rowY0 = 0;        // absolute y of the first generator row
    int rowHeight = 0;
    int breakthroughY = 0; // absolute y of the breakthrough button
};
Layout gLayout;

void computeLayout(int screenW, int screenH) {
    gLayout.screenW = screenW;
    gLayout.screenH = screenH;
    gLayout.crystalX = (screenW - kRenderSize) / 2;
    gLayout.crystalY = kHeaderHeight + kCrystalTopGap;
    gLayout.panelY0 = gLayout.crystalY + kRenderSize + kCrystalBottomGap;
    gLayout.panelH = screenH - gLayout.panelY0;

    int afterQi = kPanelTopPad + kQiReadoutHeight + kSectionGap; // panel-local y where rows start
    int reservedBottom = kSectionGap + kBreakthroughHeight + kPanelBottomPad;
    int rowsArea = gLayout.panelH - afterQi - reservedBottom;
    int minRowsArea = NUM_GENERATORS * kRowMinHeight;
    if (rowsArea < minRowsArea) rowsArea = minRowsArea;

    gLayout.rowHeight = rowsArea / NUM_GENERATORS;
    gLayout.rowY0 = gLayout.panelY0 + afterQi;
    gLayout.breakthroughY = gLayout.rowY0 + gLayout.rowHeight * NUM_GENERATORS + kSectionGap;
}

Rect qiReadoutRect() {
    return Rect{0, gLayout.panelY0 + kPanelTopPad, gLayout.screenW, kQiReadoutHeight};
}

Rect generatorRowRect(int genIndex) {
    return Rect{0, gLayout.rowY0 + genIndex * gLayout.rowHeight, gLayout.screenW, gLayout.rowHeight - 4};
}

Rect breakthroughRect() {
    return Rect{0, gLayout.breakthroughY, gLayout.screenW, kBreakthroughHeight};
}

M5Canvas* gHeaderCanvas = nullptr;
M5Canvas* gHudCanvas = nullptr;

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

} // namespace

void initHud(M5GFX& display) {
    if (gHudCanvas) return; // already initialized; safe to call more than once
    computeLayout(display.width(), display.height());

    gHeaderCanvas = new M5Canvas(&display);
    gHeaderCanvas->createSprite(gLayout.screenW, kHeaderHeight);

    gHudCanvas = new M5Canvas(&display);
    gHudCanvas->createSprite(gLayout.screenW, gLayout.panelH);
}

void drawHud(M5GFX& display, const GameState& state) {
    if (!gHudCanvas) initHud(display);

    // ---------------- Header bar: realm/Qi-per-sec (left), battery (right) ----------------
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

    int rightMaxWidth = gLayout.screenW / 4; // battery-only now; clock removed, reclaim the space for the left side
    int leftMaxWidth = gLayout.screenW - rightMaxWidth - 24;
    int headerCenterY = kHeaderHeight / 2;

    drawLeftAligned(hdr, leftBuf, 12, headerCenterY, leftMaxWidth, 2, TFT_WHITE, kHeaderBg);
    drawRightAligned(hdr, rightBuf, gLayout.screenW - 12, headerCenterY, rightMaxWidth, 2, TFT_WHITE, kHeaderBg);

    hdr.pushSprite(0, 0);

    // ---------------- Body panel: Qi total, generator rows, breakthrough button ----------------
    M5Canvas& hud = *gHudCanvas;
    hud.fillScreen(TFT_BLACK);

    Rect qiR = qiReadoutRect();
    int qiLy = qiR.y - gLayout.panelY0;
    char qiValue[24];
    formatQi(state.qi, qiValue, sizeof(qiValue));
    char qiLine[32];
    snprintf(qiLine, sizeof(qiLine), "Qi: %s", qiValue);
    drawLeftAligned(hud, qiLine, 12, qiLy + kQiReadoutHeight / 2, qiR.w - 24, 4, TFT_WHITE, TFT_BLACK);

    for (int i = 0; i < NUM_GENERATORS; ++i) {
        Rect r = generatorRowRect(i);
        int ly = r.y - gLayout.panelY0;
        bool unlocked = isGeneratorUnlocked(state, i);
        uint16_t bg = unlocked ? TFT_DARKGREY : TFT_BLACK;
        hud.fillRect(0, ly, r.w, r.h, bg);

        char line[64];
        if (unlocked) {
            char costBuf[24];
            formatQi(costForGenerator(i, state.generatorCounts[i]), costBuf, sizeof(costBuf));
            snprintf(line, sizeof(line), "%s x%d - %s", GENERATORS[i].name, state.generatorCounts[i], costBuf);
        } else {
            snprintf(line, sizeof(line), "%s (locked)", GENERATORS[i].name);
        }
        drawLeftAligned(hud, line, 12, ly + r.h / 2, r.w - 24, 2, TFT_WHITE, bg);
    }

    Rect bt = breakthroughRect();
    int btly = bt.y - gLayout.panelY0;
    bool canBt = canBreakthrough(state);
    uint16_t btBg = canBt ? TFT_ORANGE : TFT_DARKGREY;
    hud.fillRect(0, btly, bt.w, bt.h, btBg);

    char btLine[40];
    if (state.realmIndex < NUM_REALMS - 1) {
        char threshBuf[24];
        formatQi(REALM_QI_THRESHOLD[state.realmIndex + 1], threshBuf, sizeof(threshBuf));
        snprintf(btLine, sizeof(btLine), "Attempt Breakthrough (%s Qi)", threshBuf);
    } else {
        snprintf(btLine, sizeof(btLine), "Max Realm Reached");
    }
    uint16_t btFg = canBt ? TFT_BLACK : TFT_WHITE; // black-on-orange reads better than white-on-orange
    drawLeftAligned(hud, btLine, 12, btly + bt.h / 2, bt.w - 24, 2, btFg, btBg);

    hud.pushSprite(0, gLayout.panelY0);
}

int hitTestHud(int touchX, int touchY) {
    for (int i = 0; i < NUM_GENERATORS; ++i) {
        if (rectContains(generatorRowRect(i), touchX, touchY)) {
            return HUD_BUTTON_GENERATOR_BASE + i;
        }
    }
    if (rectContains(breakthroughRect(), touchX, touchY)) {
        return HUD_BUTTON_BREAKTHROUGH;
    }
    return HUD_BUTTON_NONE;
}
