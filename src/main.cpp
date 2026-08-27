#include <M5Unified.h>
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"
#include "trial_map.h"
#include "trial_state.h"
#include "trial_view.h"
#include "settings.h"

namespace {
constexpr uint32_t kTickIntervalMs = 50;   // 20Hz economy tick
constexpr uint32_t kAutosaveIntervalMs = 15000;
// The HUD (header + stats panel) is pushed as two offscreen-sprite blits covering most of a
// 720x1280 portrait screen, which costs much more per call than the raycast viewport's own
// blit. It's also just text/bars with no motion of its own, so it doesn't need to redraw at
// full render-loop rate — throttling it keeps the raycast view's own redraw (every loop
// iteration, see below) unaffected, while still forcing an immediate redraw right after any
// touch that actually changes state, so brightness/volume taps still feel responsive.
constexpr uint32_t kHudRedrawIntervalMs = 300; // ~3Hz when idle

// readRtcEpochSeconds() returns exactly 0 only for a genuinely never-seeded RTC chip
// (see rtc_store.h). If that happens, seed it once with a reasonable recent-ish
// timestamp so future elapsed-time deltas (offline earnings) work from this point
// forward. Absolute accuracy doesn't matter here, only that it's nonzero.
constexpr int64_t kRtcFallbackEpochSeconds = 1787844399;

GameState gState;
TrialState gTrialState;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;
uint32_t gLastTrialTickMs = 0;

uint8_t gBrightness = kMaxBrightness;
uint8_t gVolume = kMaxVolume / 2;

void saveNow() {
    int64_t nowEpoch = readRtcEpochSeconds();
    nvsWriteSave(toSaveData(gState, nowEpoch, gBrightness, gVolume));
}
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Secret Realm starting");

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

        // Only for a meaningfully nonzero amount — a returning player who was away for only a
        // few seconds (or a fresh device with no prior rate) has nothing worth interrupting
        // boot to report.
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

    gBrightness = clampBrightness(save.brightness);
    gVolume = clampVolume(save.volume);
    M5.Display.setBrightness(gBrightness);
    M5.Speaker.setVolume(gVolume);

    initHud(M5.Display);
    initTrialView(M5.Display);

    // The trial starts immediately and runs forever - there's no other screen to enter it
    // from anymore, and no unlock gate: even a fresh realm-0 character autoplays from boot,
    // consistent with "a weak cultivator can genuinely lose" already being the intended design.
    gTrialState = startTrial(makeSecretRealmMap(), gState.realmIndex);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
    gLastTrialTickMs = millis();
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
            attemptBreakthrough(gState);
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
        // is the only thing that persists automated progress.
    }

    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        int button = hitTestHud(touch.x, touch.y);
        bool stateChanged = false;
        if (button == HUD_BUTTON_BRIGHTNESS_DOWN) {
            gBrightness = clampBrightness(static_cast<int>(gBrightness) - kSettingsStep);
            M5.Display.setBrightness(gBrightness);
            stateChanged = true;
        } else if (button == HUD_BUTTON_BRIGHTNESS_UP) {
            gBrightness = clampBrightness(static_cast<int>(gBrightness) + kSettingsStep);
            M5.Display.setBrightness(gBrightness);
            stateChanged = true;
        } else if (button == HUD_BUTTON_VOLUME_DOWN) {
            gVolume = clampVolume(static_cast<int>(gVolume) - kSettingsStep);
            M5.Speaker.setVolume(gVolume);
            stateChanged = true;
        } else if (button == HUD_BUTTON_VOLUME_UP) {
            gVolume = clampVolume(static_cast<int>(gVolume) + kSettingsStep);
            M5.Speaker.setVolume(gVolume);
            stateChanged = true;
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

    tickTrial(gTrialState, dt, reward, gState.realmIndex);

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
        restartTrial(gTrialState, gState.realmIndex); // resets qiRewardPending to 0.0 and loops back
    } else {
        renderTrialView(M5.Display, gTrialState);
    }

    if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
        drawHud(M5.Display, gState, gTrialState, gBrightness, gVolume);
        gLastHudDrawMs = now;
    }
}
