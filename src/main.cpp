#include <M5Unified.h>
#include "math3d.h"
#include "mesh.h"
#include "framebuffer.h"
#include "rasterizer.h"
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"
#include "trial_map.h"
#include "trial_state.h"
#include "trial_view.h"

namespace {
// kRenderSize/kHeaderHeight/kCrystalTopGap live in ui.h — shared with ui.cpp so the
// crystal viewport and the HUD layout built around it can never disagree about
// where the viewport sits.
constexpr uint32_t kTickIntervalMs = 50;   // 20Hz economy tick
constexpr uint32_t kAutosaveIntervalMs = 15000;
// The HUD (header + generator/breakthrough panel) is pushed as two offscreen-sprite
// blits covering most of a 720x1280 portrait screen, which costs much more per call
// than the small 240x240 crystal viewport. It's also just text/numbers with no
// motion, so it doesn't need to redraw at full render-loop rate — throttling it
// keeps the crystal's rotation smooth without a bigger rendering rewrite.
// A full HUD redraw measured ~150ms on real hardware (debug build, ~700K+ pixels
// across the header + panel sprites) - at that cost, throttling to a fixed interval
// still saturates the loop, so the redraw is (a) rate-limited to ~3Hz for the
// idle/no-interaction case (plenty for a ticking-number display) and (b) forced
// immediately after any touch that actually changes state, so button taps still
// feel responsive. See loop()'s touch-handling block.
constexpr uint32_t kHudRedrawIntervalMs = 300; // ~3Hz when idle

// readRtcEpochSeconds() returns exactly 0 only for a genuinely never-seeded RTC chip
// (see rtc_store.h). If that happens, seed it once with a reasonable recent-ish
// timestamp so future elapsed-time deltas (offline earnings) work from this point
// forward. Absolute accuracy doesn't matter here, only that it's nonzero — this
// value happens to be real GMT+8 wall-clock time (2026-08-27 15:26:39, captured
// mid-Task-11 and used to reseed this device's already-seeded-but-inaccurate RTC),
// but nothing in this codebase currently displays or otherwise depends on that
// accuracy (there is no clock UI); only the elapsed difference between reads matters.
constexpr int64_t kRtcFallbackEpochSeconds = 1787844399;

GameState gState;
Mesh gBaseMesh;
RealmVisual gRealmVisual;
Framebuffer* gFramebuffer = nullptr;
M5Canvas* gCanvas = nullptr;
int gCrystalX = 0;
int gCrystalY = 0;
float gRotation = 0.0f;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;

enum class ViewMode { Idle, TrialGround };
ViewMode gViewMode = ViewMode::Idle;
TrialState gTrialState;
bool gTrialStarted = false;
uint32_t gLastTrialTickMs = 0;

void refreshRealmVisual() {
    gRealmVisual = growForRealm(gBaseMesh, gState.realmIndex);
}

void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch));
}
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Xianxia idle game starting");

    M5.Display.fillScreen(TFT_BLACK);

    SaveData save = nvsLoadSave();
    int64_t nowEpoch = readRtcEpochSeconds();

    if (nowEpoch == 0) {
        writeRtcFromEpochSeconds(kRtcFallbackEpochSeconds);
        nowEpoch = kRtcFallbackEpochSeconds;
        Serial.println("[RTC] Chip was never seeded (epoch==0); wrote fallback timestamp");
    }

    if (save.lastSaveEpochSeconds != 0) {
        GameState priorState = toGameState(save);
        double rateAtSave = qiPerSecond(priorState);
        double offlineQi = computeOfflineEarnings(nowEpoch, save.lastSaveEpochSeconds,
                                                    rateAtSave, 24 * 3600);
        save.qi += offlineQi;
        Serial.printf("[OFFLINE] Gained %.2f Qi while away\n", offlineQi);

        // Design spec (Persistence & Offline Earnings): show a "While you cultivated in
        // seclusion..." screen before entering the main view. Only for a meaningfully
        // nonzero amount — a returning player who was away for only a few seconds (or a
        // fresh device with no prior rate) has nothing worth interrupting boot to report.
        if (offlineQi >= 0.1) {
            char qiBuf[24];
            formatQi(offlineQi, qiBuf, sizeof(qiBuf));
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(20, 20);
            M5.Display.printf("While you cultivated in seclusion,\nyou gained %s Qi", qiBuf);
            delay(2000);
        }
    } else {
        Serial.println("[OFFLINE] First-ever boot, no offline bonus");
    }

    gState = toGameState(save);
    gBaseMesh = makeIcosahedron();
    refreshRealmVisual();

    gCrystalX = (M5.Display.width() - kRenderSize) / 2; // centered; see ui.h's layout comment
    gCrystalY = kHeaderHeight + kCrystalTopGap;

    gFramebuffer = new Framebuffer(kRenderSize, kRenderSize);
    gCanvas = new M5Canvas(&M5.Display);
    gCanvas->createSprite(kRenderSize, kRenderSize);
    initHud(M5.Display);
    initTrialView(M5.Display);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
    saveNow();

    Serial.println("[BOOT] Ready");
}

void loop() {
    M5.update();

    uint32_t now = millis();

    if (now - gLastTickMs >= kTickIntervalMs) {
        double dt = (now - gLastTickMs) / 1000.0;
        tick(gState, dt);
        gLastTickMs = now;

        // Automation: breakthrough first, then auto-buy — deliberately in that order.
        // Breakthrough thresholds are much larger than generator costs; if auto-buy ran
        // first and greedily spent Qi every tick, it could perpetually keep Qi below the
        // breakthrough threshold. Checking breakthrough first avoids that trap.
        if (canBreakthrough(gState)) {
            if (attemptBreakthrough(gState)) {
                refreshRealmVisual();
            }
        }
        // One purchase attempt per generator per tick (not loop-until-can't-afford) —
        // natural accumulation across many ticks at 20Hz is plenty responsive, and index
        // order (0..NUM_GENERATORS-1) naturally prioritizes the cheapest/earliest-unlocked
        // generators first.
        for (int i = 0; i < NUM_GENERATORS; ++i) {
            purchaseGenerator(gState, i);
        }
        // Deliberately no saveNow() here: automated purchases/breakthroughs can fire many
        // times per second, and forcing an NVS write on every one would hammer flash write
        // endurance for no real benefit. The periodic autosave below (kAutosaveIntervalMs)
        // is the only thing that persists automated progress — worst case ~15s of it is
        // lost on an unexpected power loss, an acceptable tradeoff here. Manual taps (below)
        // keep their own immediate saveNow() — a deliberate user action should still feel
        // instantly saved.
    }

    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y, gViewMode == ViewMode::TrialGround);
        bool stateChanged = false;
        if (button == HUD_BUTTON_ENTER_SECRET_REALM && gViewMode == ViewMode::Idle) {
            if (gState.realmIndex >= kSecretRealmUnlockRealmIndex) {
                gViewMode = ViewMode::TrialGround;
                if (!gTrialStarted) {
                    gTrialState = startTrial(makeSecretRealmMap(), gState.realmIndex);
                    gTrialStarted = true;
                }
                // Reset the trial's frame clock on every entry (not just the first),
                // otherwise the next tick's dt is computed against a stale
                // gLastTrialTickMs from whenever the mode was last active - potentially
                // seconds or minutes ago - causing one huge simulated dt step that skips
                // the player through most of the route/combat in a single tick.
                gLastTrialTickMs = millis();
            }
        } else if (button == HUD_BUTTON_RETURN_TO_CULTIVATION) {
            gViewMode = ViewMode::Idle;
            gLastHudDrawMs = 0; // force an immediate HUD redraw on returning
        } else if (button == HUD_BUTTON_BREAKTHROUGH) {
            if (attemptBreakthrough(gState)) {
                refreshRealmVisual();
                stateChanged = true;
            }
        } else if (button >= HUD_BUTTON_GENERATOR_BASE && button < HUD_BUTTON_GENERATOR_BASE + NUM_GENERATORS) {
            if (purchaseGenerator(gState, button - HUD_BUTTON_GENERATOR_BASE)) {
                stateChanged = true;
            }
        }
        if (stateChanged) {
            saveNow();
            gLastHudDrawMs = 0; // force an immediate (unthrottled) HUD redraw this frame
        }
    }

    if (now - gLastAutosaveMs >= kAutosaveIntervalMs) {
        saveNow();
        gLastAutosaveMs = now;
    }

    if (gViewMode == ViewMode::Idle) {
        RasterParams params;
        params.transform = Mat4::rotationY(gRotation);
        params.cameraDistance = 4.0f;
        params.focalLength = 1.6f;
        params.lightDir = Vec3{0.4f, 0.6f, -1.0f}.normalized();
        params.viewDir = Vec3{0, 0, -1};
        params.baseColor = gRealmVisual.baseColor;
        params.rimColor = gRealmVisual.rimColor;

        gFramebuffer->clear(RGB{15, 15, 25});
        rasterizeMesh(gRealmVisual.mesh, params, *gFramebuffer);

        for (int y = 0; y < kRenderSize; ++y) {
            for (int x = 0; x < kRenderSize; ++x) {
                RGB p = gFramebuffer->getPixel(x, y);
                gCanvas->drawPixel(x, y, gCanvas->color565(p.r, p.g, p.b));
            }
        }
        gCanvas->pushSprite(gCrystalX, gCrystalY);

        if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
            drawHud(M5.Display, gState);
            gLastHudDrawMs = now;
        }

        gRotation += 0.02f;
    } else {
        uint32_t nowTrial = millis();
        double dt = (nowTrial - gLastTrialTickMs) / 1000.0;
        gLastTrialTickMs = nowTrial;

        // Reward scales with the Qi needed for the player's *next* breakthrough (or stays
        // at the final realm's own threshold once there's no next realm), so clearing the
        // trial is always worth a meaningful fraction of "how far you have left to go."
        int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
        double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
        TrialPhase phaseBefore = gTrialState.phase;
        bool wasFighting = (phaseBefore == TrialPhase::Fighting);
        int enemyHpBefore = gTrialState.enemy.hp;
        int playerHpBefore = gTrialState.player.hp;

        tickTrial(gTrialState, dt, reward);

        if (wasFighting && gTrialState.enemy.hp < enemyHpBefore) playAttackSfx();
        if (wasFighting && gTrialState.player.hp < playerHpBefore) playHitSfx();

        if (phaseBefore != TrialPhase::Cleared && gTrialState.phase == TrialPhase::Cleared) {
            // Apply the reward exactly once, on the single tick this transition happens
            // (checking qiRewardPending > 0 every frame instead would re-apply it every
            // frame after, since tickTrial() leaves it set while parked in Cleared).
            playVictorySfx();
            gState.qi += gTrialState.qiRewardPending;
            saveNow();
            renderTrialView(M5.Display, gTrialState); // show the "Cleared!" frame before pausing
            delay(1500);
            restartTrial(gTrialState); // resets qiRewardPending to 0.0 and loops back to the start
        } else {
            renderTrialView(M5.Display, gTrialState);
        }

        // Keep the header (realm/Qi-per-sec/battery) live while in the trial view too, on
        // the same throttle cadence the idle panel uses, so the economy readout doesn't
        // visibly freeze just because the generator/breakthrough panel has nothing to show
        // here.
        if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
            drawHeader(M5.Display, gState);
            gLastHudDrawMs = now;
        }
    }
}
