#include <M5Unified.h>
#include "economy.h"
#include "save.h"
#include "nvs_store.h"
#include "rtc_store.h"
#include "offline_earnings.h"
#include "ui.h"
#include "zone_map.h"
#include "zone_state.h"
#include "zone_events.h"
#include "zone_view.h"
#include "settings.h"

namespace {
constexpr uint32_t kTickIntervalMs = 50;   // 20Hz economy tick
constexpr uint32_t kAutosaveIntervalMs = 15000;
// The HUD (header + stats panel) is pushed as two offscreen-sprite blits covering most of a
// 1280x720 landscape screen (the panel's native hardware orientation is portrait 720x1280;
// setup() rotates it to landscape for the wide MapleStory-style zone view), which costs much
// more per call than the raycast viewport's own blit used to. It's also just text/bars with no
// motion of its own, so it doesn't need to redraw at full render-loop rate — throttling it
// keeps the zone view's own redraw (every loop iteration, see below) unaffected. There are no
// touch controls left to force an immediate redraw for anymore (see ui.h) — every redraw here
// just waits out the throttle.
constexpr uint32_t kHudRedrawIntervalMs = 300; // ~3Hz when idle

// The zone canvas now covers most of the screen (see sceneViewportBottom() in ui.cpp) rather
// than half of it, so redrawing/pushing it on every single loop() iteration - previously cheap
// enough not to matter - now moves a lot more pixels than the display can usefully show more
// often than this. tickZone() below still runs every loop iteration (using the real elapsed
// dt) so simulation speed/smoothness is unaffected; only how often the result gets pushed to
// the physical screen is capped.
constexpr uint32_t kZoneFrameIntervalMs = 33; // ~30fps cap

// readRtcEpochSeconds() returns exactly 0 only for a genuinely never-seeded RTC chip
// (see rtc_store.h). If that happens, seed it once with a reasonable recent-ish
// timestamp so future elapsed-time deltas (offline earnings) work from this point
// forward. Absolute accuracy doesn't matter here, only that it's nonzero.
constexpr int64_t kRtcFallbackEpochSeconds = 1787844399;

GameState gState;
ZoneState gZoneState;
// The pre-tick copy of gZoneState that deriveZoneTickEvents() compares against. Kept at file
// scope and reassigned in place (rather than declared as a fresh local in loop()) because
// ZoneState owns three small heap vectors: copy-assigning into an already-sized buffer reuses
// their storage, so snapshotting costs no heap traffic once the loop is warm.
ZoneState gZoneStateBefore;
uint32_t gLastTickMs = 0;
uint32_t gLastAutosaveMs = 0;
uint32_t gLastHudDrawMs = 0;
uint32_t gLastZoneTickMs = 0;
uint32_t gLastZoneRenderMs = 0;

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

    // Rotates the panel from its native portrait orientation to landscape (width() becomes
    // 1280, height() becomes 720) for the MapleStory-style wide side-view zone. Rotation 3
    // (rather than 1, the other landscape option 180 degrees apart) so the image reads right
    // side up with the device's keyboard attachment docked below the screen.
    M5.Display.setRotation(3);

    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Zone starting");

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
    initZoneView(M5.Display);

    // The zone starts immediately and runs forever - there's no other screen to enter it
    // from anymore, and no unlock gate: even a fresh realm-0 character autoplays from boot,
    // consistent with "a weak cultivator can genuinely lose" already being the intended design.
    // isBossZoneForRunIndex(0) is always false (see zone_state.h) - spelled out explicitly
    // rather than relied upon, so this boot call stays correct if kBossZoneInterval is ever
    // retuned to 1.
    gZoneState = startZone(makeZoneMap(gState.realmIndex, 0, isBossZoneForRunIndex(0)), gState.realmIndex);

    gLastTickMs = millis();
    gLastAutosaveMs = millis();
    gLastZoneTickMs = millis();
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
            // Loops rather than one attempt per tick: a large Qi injection (e.g. offline
            // earnings after a long AFK stretch) can clear several realm thresholds at once,
            // and playBreakthroughSfx() below blocks for a few hundred ms - spreading that
            // across many ticks would freeze rendering/input for seconds as it re-enters this
            // branch tick after tick. Resolving the whole cascade here and celebrating once
            // keeps the blocking cost bounded to a single fanfare regardless of how many
            // realms were actually gained.
            while (canBreakthrough(gState)) {
                attemptBreakthrough(gState);
            }
            triggerRealmBreakthroughFx();
            playBreakthroughSfx();
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


    if (now - gLastAutosaveMs >= kAutosaveIntervalMs) {
        saveNow();
        gLastAutosaveMs = now;
    }

    uint32_t nowZone = millis();
    double dt = (nowZone - gLastZoneTickMs) / 1000.0;
    gLastZoneTickMs = nowZone;

    // Reward scales with the Qi needed for the player's *next* breakthrough (or stays
    // at the final realm's own threshold once there's no next realm), so clearing the
    // zone is always worth a meaningful fraction of "how far you have left to go."
    int nextRealm = (gState.realmIndex < NUM_REALMS - 1) ? gState.realmIndex + 1 : gState.realmIndex;
    double reward = REALM_QI_THRESHOLD[nextRealm] * 0.05;
    // Snapshot the whole pre-tick state (plus the two HP values, which the FX below also need as
    // plain ints for their damage-number amounts) so deriveZoneTickEvents() can diff it against
    // the post-tick state. That derivation lives in lib/core/zone_events.cpp rather than here so
    // it's reachable by `pio test -e native` - see zone_events.h.
    gZoneStateBefore = gZoneState;
    bool wasFighting = (gZoneStateBefore.phase == ZonePhase::Fighting);
    int enemyHpBefore = gZoneStateBefore.enemy.hp;
    int playerHpBefore = gZoneStateBefore.player.hp;

    tickZone(gZoneState, dt, reward, gState.realmIndex);

    ZoneTickEvents events = deriveZoneTickEvents(gZoneStateBefore, gZoneState, wasFighting,
                                                  enemyHpBefore, playerHpBefore);
    if (events.enemyHit) {
        // skip the plain-hit tone when the skill's own SFX will play this tick
        if (!events.skillFired) playAttackSfx();
        triggerAttackFlash();
        spawnDamageNumber(false, enemyHpBefore - gZoneState.enemy.hp,
                          events.skillFired ? gZoneState.skillFiredThisTick : -1);
    }
    if (events.bossEnrageTriggered) {
        triggerBossEnrageFx();
        playBossEnrageSfx();
    }
    if (events.monsterDefeated) {
        triggerLootPop();
        playLootSfx();
        if (events.bossDefeated) {
            triggerBossDefeatFx();
            playBossDefeatSfx();
        }
    }
    if (events.skillFired) {
        triggerSkillFx(gZoneState.skillFiredThisTick);
        playSkillSfx(gZoneState.skillFiredThisTick);
    }
    if (events.playerHit) {
        playHitSfx();
        triggerHitFlash();
        spawnDamageNumber(true, playerHpBefore - gZoneState.player.hp, -1);
    }

    if (gZoneStateBefore.phase != ZonePhase::Cleared && gZoneState.phase == ZonePhase::Cleared) {
        // Apply the reward exactly once, on the single tick this transition happens
        // (checking qiRewardPending > 0 every frame instead would re-apply it every
        // frame after, since tickZone() leaves it set while parked in Cleared).
        playVictorySfx();
        gState.qi += gZoneState.qiRewardPending;
        saveNow();
        renderZoneView(M5.Display, gZoneState); // show the cleared frame...
        drawHud(M5.Display, gState, gZoneState); // ...with "Cleared!" in the monsters bar, before pausing
        gLastHudDrawMs = now; // this was an explicit/forced draw; keep the throttle in sync
        gLastZoneRenderMs = now; // ditto for the zone-frame throttle below
        delay(1500);
        restartZone(gZoneState, gState.realmIndex); // rebuilds the map for the current realm and loops back
        gLastZoneTickMs = millis(); // avoid a huge simulated dt on the next tick from the ~1.7s of
                                     // delay() above (SFX + the pause) that gLastZoneTickMs doesn't
                                     // otherwise account for
    } else if (nowZone - gLastZoneRenderMs >= kZoneFrameIntervalMs) {
        renderZoneView(M5.Display, gZoneState);
        gLastZoneRenderMs = nowZone;
    }

    if (now - gLastHudDrawMs >= kHudRedrawIntervalMs) {
        drawHud(M5.Display, gState, gZoneState);
        gLastHudDrawMs = now;
    }
}
