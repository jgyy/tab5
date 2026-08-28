#include "ui.h"
#include "zone_textures.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Compact display formatting for Qi-scale numbers (REALM_QI_THRESHOLD now tops out at
// 150,000,000,000,000,000 across 16 realms) — a raw "%.0f" would overflow a bar's label at
// any reasonable text size. Declared in ui.h (not anonymous-namespace-local) so main.cpp's
// welcome-back screen can reuse it.
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
// The panel is a fixed-height strip anchored to the bottom of the screen (not a fraction of
// it) so the zone view above it gets to be the screen, not a chunk of it: all four stats -
// player HP, enemy HP, route progress, breakthrough progress - share a single row of equal
// columns instead of stacking into multiple rows, keeping the panel's footprint (and its bite
// out of the zone viewport above) as small as a single bar's height.
//
// Styled after MapleStory's status window: a beveled gold/bronze frame around individually
// bordered, glossy mini-bars, each with its own small corner rivets and a pixel-art icon
// (heart/skull/flag/star) ahead of its label so all four stay legible at this narrower width.
constexpr int kFrameBorder = 3;      // outer bronze border thickness
constexpr int kFrameRadius = 10;     // outer frame corner radius
constexpr int kInnerPad = 5;         // gap between the frame's inner edge and the bar row
constexpr int kSideInset = kFrameBorder + kInnerPad; // left/right margin for the full-width row
constexpr int kColumnGap = 8;        // gap between adjacent columns in the stat row
constexpr int kNumColumns = 4;       // player HP, enemy HP, route progress, breakthrough
constexpr int kBarHeight = 32;
constexpr int kBarRadius = 6;        // corner radius for each individual bar's frame
constexpr int kBarBorder = 2;        // gold border thickness around each individual bar
constexpr int kPanelHeight = kFrameBorder + kInnerPad + kBarHeight + kInnerPad + kFrameBorder;

// ---- Maple-inspired palette ----
// Named from real RGB triples via the graphics stack's constexpr color565() rather than
// hand-computed RGB565 hex, so the intended color is legible directly from the value.
constexpr uint16_t kBronze = lgfx::color565(120, 85, 35);       // outer frame border
constexpr uint16_t kGold = lgfx::color565(210, 170, 60);        // per-bar frame border, rivets
constexpr uint16_t kGoldBright = lgfx::color565(250, 225, 140); // inner bevel highlight line
constexpr uint16_t kFrameBg = lgfx::color565(24, 18, 12);       // warm near-black frame interior
constexpr uint16_t kTrackColor = lgfx::color565(35, 28, 20);    // recessed (unfilled) bar track
constexpr uint16_t kOutlineColor = TFT_BLACK;                   // text drop-shadow/outline

constexpr uint16_t kPlayerHpColor = lgfx::color565(215, 30, 30);   // authentic Maple HP red
constexpr uint16_t kPlayerHpGloss = lgfx::color565(255, 140, 130);
constexpr uint16_t kEnemyHpColor = lgfx::color565(130, 15, 60);    // darker maroon so it reads
constexpr uint16_t kEnemyHpGloss = lgfx::color565(210, 90, 140);   // distinctly from player HP
constexpr uint16_t kExpColor = lgfx::color565(210, 180, 60);       // Maple EXP-bar yellow
constexpr uint16_t kExpGloss = lgfx::color565(240, 220, 140);
constexpr uint16_t kQuestColor = lgfx::color565(50, 160, 165);     // monsters/route progress
constexpr uint16_t kQuestGloss = lgfx::color565(150, 220, 220);
} // namespace

int sceneViewportBottom(int screenH) {
    return screenH - kPanelHeight;
}

namespace {

struct Layout {
    int screenW = 0;
    int screenH = 0;
    int panelY0 = 0;      // absolute y where the stats panel (below the zone viewport) starts
    int panelH = 0;
    int rowY = 0;         // the single row all four stat columns share
};
Layout gLayout;

void computeLayout(int screenW, int screenH) {
    gLayout.screenW = screenW;
    gLayout.screenH = screenH;
    gLayout.panelY0 = sceneViewportBottom(screenH);
    gLayout.panelH = screenH - gLayout.panelY0;
    gLayout.rowY = gLayout.panelY0 + kFrameBorder + kInnerPad;
}

Rect statRowRect() { return Rect{kSideInset, gLayout.rowY, gLayout.screenW - 2 * kSideInset, kBarHeight}; }

// The index-th of kNumColumns equal-width columns across `row`, separated by kColumnGap. The
// last column absorbs the width left over from integer division so the columns exactly tile
// the row with no rounding gap at the right edge.
Rect columnRect(const Rect& row, int index) {
    int gapTotal = kColumnGap * (kNumColumns - 1);
    int colW = (row.w - gapTotal) / kNumColumns;
    int x = row.x + index * (colW + kColumnGap);
    int w = (index == kNumColumns - 1) ? (row.x + row.w - x) : colW;
    return Rect{x, row.y, w, row.h};
}

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

// Draws `text` with a 1px black outline (four cardinal offsets in the outline color, then the
// real text in white on top) instead of a flat fill - the outline uses single-argument
// setTextColor() (transparent background, only glyph pixels drawn) so each pass only adds
// pixels rather than blanking the ones before it. This is the pixel-font drop-shadow look
// MapleStory's UI uses to keep text readable over busy, colorful bar fills.
void printOutlined(M5Canvas& canvas, const char* text, int x, int y) {
    canvas.setTextColor(kOutlineColor);
    canvas.setCursor(x - 1, y); canvas.print(text);
    canvas.setCursor(x + 1, y); canvas.print(text);
    canvas.setCursor(x, y - 1); canvas.print(text);
    canvas.setCursor(x, y + 1); canvas.print(text);
    canvas.setTextColor(TFT_WHITE);
    canvas.setCursor(x, y); canvas.print(text);
}

// Draws the ornate gold/bronze window frame the stats panel sits inside: a bronze outer
// border, a 1px bright-gold bevel line just inside it, a warm near-black interior, and four
// small gold corner rivets - a classic MapleStory dialog/status-window treatment. `canvas`
// is the panel sprite, so coordinates here are panel-local (origin at the panel's own top-left,
// not the screen's), matching how the rest of this file already draws into it.
void drawPanelFrame(M5Canvas& canvas) {
    int w = gLayout.screenW;
    int h = gLayout.panelH;
    // fillRoundRect leaves the four corner wedges outside its curve untouched; since this
    // sprite is reused every frame without otherwise being cleared, skipping this fillScreen
    // would let stale pixels from whatever was drawn before persist there indefinitely instead
    // of being deterministically black like the old full-panel fillScreen(TFT_BLACK) guaranteed.
    canvas.fillScreen(TFT_BLACK);
    canvas.fillRoundRect(0, 0, w, h, kFrameRadius, kBronze);
    canvas.drawRoundRect(1, 1, w - 2, h - 2, kFrameRadius > 1 ? kFrameRadius - 1 : 0, kGoldBright);
    int innerRadius = kFrameRadius > kFrameBorder ? kFrameRadius - kFrameBorder : 0;
    canvas.fillRoundRect(kFrameBorder, kFrameBorder, w - 2 * kFrameBorder, h - 2 * kFrameBorder,
                          innerRadius, kFrameBg);

    constexpr int kRivetInset = 7;
    constexpr int kRivetRadius = 3;
    canvas.fillCircle(kRivetInset, kRivetInset, kRivetRadius, kGold);
    canvas.fillCircle(w - kRivetInset, kRivetInset, kRivetRadius, kGold);
    canvas.fillCircle(kRivetInset, h - kRivetInset, kRivetRadius, kGold);
    canvas.fillCircle(w - kRivetInset, h - kRivetInset, kRivetRadius, kGold);
}

// Which pixel-art glyph drawBar() draws ahead of its label, so all four single-row columns
// stay identifiable at a glance without relying on reading the (now narrower) text alone.
enum class IconKind { Heart, Skull, Flag, Star };

// Draws a small ~2*size-wide glyph centered at (cx, cy), entirely with fill primitives - matching
// how the rest of this codebase draws icon-scale shapes (monster silhouettes, header battery/realm
// text) rather than depending on a font glyph or imported image being available.
void drawBarIcon(M5Canvas& canvas, IconKind kind, int cx, int cy, int size) {
    switch (kind) {
        case IconKind::Heart: {
            int lobeR = size / 2;
            if (lobeR < 1) lobeR = 1;
            canvas.fillCircle(cx - lobeR, cy - lobeR / 2, lobeR, TFT_WHITE);
            canvas.fillCircle(cx + lobeR, cy - lobeR / 2, lobeR, TFT_WHITE);
            canvas.fillTriangle(cx - size, cy - lobeR / 2, cx + size, cy - lobeR / 2, cx, cy + size, TFT_WHITE);
            break;
        }
        case IconKind::Skull: {
            canvas.fillCircle(cx, cy, size, TFT_WHITE);
            int eyeOffset = size / 2;
            if (eyeOffset < 1) eyeOffset = 1;
            canvas.fillCircle(cx - eyeOffset, cy, 1, TFT_BLACK);
            canvas.fillCircle(cx + eyeOffset, cy, 1, TFT_BLACK);
            canvas.fillRect(cx - eyeOffset, cy + eyeOffset, 2 * eyeOffset, 1, TFT_BLACK);
            break;
        }
        case IconKind::Flag: {
            canvas.drawLine(cx, cy - size, cx, cy + size, TFT_WHITE);
            canvas.fillTriangle(cx, cy - size, cx + size, cy - size / 2, cx, cy, TFT_WHITE);
            break;
        }
        case IconKind::Star: {
            int half = size > 1 ? size / 2 : 1;
            canvas.fillTriangle(cx, cy - size, cx - half, cy, cx + half, cy, TFT_WHITE);
            canvas.fillTriangle(cx, cy + size, cx - half, cy, cx + half, cy, TFT_WHITE);
            break;
        }
    }
}

// Width reserved for drawBarIcon() before a bar's label text starts.
constexpr int kIconAreaWidth = 22;

// Small gold dots at a mini-bar's own four corners, echoing the outer panel frame's rivets at a
// scale that fits a bar as short as kBarHeight - purely decorative, drawn last so nothing else
// overdraws them.
void drawBarRivets(M5Canvas& canvas, int x, int ly, int w, int h) {
    constexpr int kMiniRivetInset = 3;
    constexpr int kMiniRivetRadius = 1;
    canvas.fillCircle(x + kMiniRivetInset, ly + kMiniRivetInset, kMiniRivetRadius, kGoldBright);
    canvas.fillCircle(x + w - kMiniRivetInset, ly + kMiniRivetInset, kMiniRivetRadius, kGoldBright);
    canvas.fillCircle(x + kMiniRivetInset, ly + h - kMiniRivetInset, kMiniRivetRadius, kGoldBright);
    canvas.fillCircle(x + w - kMiniRivetInset, ly + h - kMiniRivetInset, kMiniRivetRadius, kGoldBright);
}

// Draws one MapleStory-style stat bar: a gold-bordered rounded frame around a dark recessed
// track, a glossy colored fill (a lighter highlight band across the top third fakes the
// classic HP/MP/EXP sheen), an icon identifying the stat, and a black-outlined label overlaid
// on top. `fraction` is clamped to [0,1] so a caller passing a raw ratio can't overflow the bar.
void drawBar(M5Canvas& canvas, const Rect& r, float fraction, uint16_t fillColor, uint16_t glossColor,
             const char* label, int maxTextSize, IconKind icon) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    int ly = r.y - gLayout.panelY0;

    canvas.fillRoundRect(r.x, ly, r.w, r.h, kBarRadius, kGold);
    int tx = r.x + kBarBorder;
    int ty = ly + kBarBorder;
    int tw = r.w - 2 * kBarBorder;
    int th = r.h - 2 * kBarBorder;
    int fillRadius = kBarRadius > kBarBorder ? kBarRadius - kBarBorder : 0;
    canvas.fillRoundRect(tx, ty, tw, th, fillRadius, kTrackColor);

    int fillW = static_cast<int>(tw * fraction);
    if (fillW > 0) {
        canvas.fillRoundRect(tx, ty, fillW, th, fillRadius, fillColor);
        int glossH = th / 3;
        if (glossH > 0) canvas.fillRoundRect(tx, ty, fillW, glossH, fillRadius, glossColor);
    }

    drawBarIcon(canvas, icon, tx + kIconAreaWidth / 2, ty + th / 2, th / 4);

    int labelX = tx + kIconAreaWidth;
    int labelMaxWidth = tw - kIconAreaWidth - 8;
    fitTextSize(canvas, label, labelMaxWidth, maxTextSize);
    printOutlined(canvas, label, labelX, ty + (th - canvas.fontHeight()) / 2);

    drawBarRivets(canvas, r.x, ly, r.w, r.h);
}

// Radius of the header's circular portrait badge (see drawHeader) - sized to sit comfortably
// inside kHeaderHeight (64px) with margin above and below.
constexpr int kPortraitRadius = 22;
constexpr int kPortraitMargin = 12;

void drawHeader(M5GFX& display, const GameState& state) {
    M5Canvas& hdr = *gHeaderCanvas;
    constexpr uint16_t kHeaderBg = 0x18E3; // dark navy-grey, distinct from the panel's black
    hdr.fillScreen(kHeaderBg);

    int headerCenterY = kHeaderHeight / 2;
    int portraitCx = kPortraitMargin + kPortraitRadius;

    // Small MapleStory-style character-portrait badge: filled with the same per-realm aura
    // color the character sprite wears in the zone view (see characterAuraColor()), so the
    // header visibly reflects "what realm's power am I channeling" at a glance, with a gold
    // ring to match the panel's frame styling and a bright core to read as a gem/eye, not a
    // flat dot.
    RGB aura = characterAuraColor(state.realmIndex);
    uint16_t auraColor = hdr.color565(aura.r, aura.g, aura.b);
    hdr.fillCircle(portraitCx, headerCenterY, kPortraitRadius, auraColor);
    hdr.drawCircle(portraitCx, headerCenterY, kPortraitRadius, kGold);
    hdr.drawCircle(portraitCx, headerCenterY, kPortraitRadius - 1, kGoldBright);
    hdr.fillCircle(portraitCx, headerCenterY, kPortraitRadius / 3, TFT_WHITE);

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

    int leftTextX = kPortraitMargin + 2 * kPortraitRadius + 10;
    int rightMaxWidth = gLayout.screenW / 4;
    int leftMaxWidth = gLayout.screenW - rightMaxWidth - leftTextX - 12;

    drawLeftAligned(hdr, leftBuf, leftTextX, headerCenterY, leftMaxWidth, 2, TFT_WHITE, kHeaderBg);
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

void drawHud(M5GFX& display, const GameState& state, const ZoneState& zone) {
    if (!gPanelCanvas) initHud(display);
    drawHeader(display, state);

    M5Canvas& panel = *gPanelCanvas;
    drawPanelFrame(panel);

    // All four stats now share one row of equal columns instead of stacking into multiple
    // rows - order left to right still mirrors MapleStory's HP/MP/EXP priority: player HP,
    // enemy HP, route (monsters-defeated) progress, then Breakthrough.
    Rect row = statRowRect();

    float playerFraction = zone.player.maxHp > 0
        ? static_cast<float>(zone.player.hp) / static_cast<float>(zone.player.maxHp)
        : 0.0f;
    char playerLabel[32];
    snprintf(playerLabel, sizeof(playerLabel), "Player HP %d/%d", zone.player.hp, zone.player.maxHp);
    drawBar(panel, columnRect(row, 0), playerFraction, kPlayerHpColor, kPlayerHpGloss, playerLabel, 2,
            IconKind::Heart);

    bool fighting = (zone.phase == ZonePhase::Fighting);
    float enemyFraction = (fighting && zone.enemy.maxHp > 0)
        ? static_cast<float>(zone.enemy.hp) / static_cast<float>(zone.enemy.maxHp)
        : 0.0f;
    char enemyLabel[32];
    if (fighting) {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP %d/%d", zone.enemy.hp, zone.enemy.maxHp);
    } else {
        snprintf(enemyLabel, sizeof(enemyLabel), "Enemy HP --");
    }
    drawBar(panel, columnRect(row, 1), enemyFraction, kEnemyHpColor, kEnemyHpGloss, enemyLabel, 2,
            IconKind::Skull);

    int totalMonsters = static_cast<int>(zone.map.monsters.size());
    int defeatedCount = 0;
    for (bool d : zone.monstersDefeated) { if (d) defeatedCount++; }
    float monstersFraction = totalMonsters > 0
        ? static_cast<float>(defeatedCount) / static_cast<float>(totalMonsters)
        : 0.0f;
    char monstersLabel[32];
    if (zone.phase == ZonePhase::Cleared) {
        monstersFraction = 1.0f;
        snprintf(monstersLabel, sizeof(monstersLabel), "Cleared!");
    } else {
        snprintf(monstersLabel, sizeof(monstersLabel), "Monsters %d/%d", defeatedCount, totalMonsters);
    }
    drawBar(panel, columnRect(row, 2), monstersFraction, kQuestColor, kQuestGloss, monstersLabel, 2,
            IconKind::Flag);

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
    drawBar(panel, columnRect(row, 3), breakthroughFraction, kExpColor, kExpGloss, btLabel, 2,
            IconKind::Star);

    panel.pushSprite(0, gLayout.panelY0);
}

