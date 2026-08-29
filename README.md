# tab5
The Tab5 is a highly expandable, portable smart IoT terminal designed for developers, integrating a dual-core architecture and rich hardware resources. It is built around the ESP32-P4 SoC based on the RISC-V architecture, featuring 16MB Flash and 32MB PSRAM for high-performance application development.

Full hardware datasheet: `docs/Tab5.pdf`.

## Xianxia Idle Game

A cultivation-themed idle game running natively on the Tab5. Cultivate **Qi**, buy
passive **Cultivation Methods** (generators), and advance through sixteen **Cultivation
Realms** — Mortal Body, Qi Condensation, Foundation Establishment, Core Formation,
Nascent Soul, Soul Transformation, Void Refinement, Spirit Severing, Dao Seeking,
Immortal Ascension, Earth Immortal, Heaven Immortal, Golden Immortal, Daluo Immortal,
Saint Realm, Empyrean Realm — each breakthrough spending the next realm's Qi threshold.

The economy no longer has a screen of its own. A later revamp deleted the rotating-crystal
idle view and its touch-driven generator/breakthrough shop entirely — it was the buggiest,
most complex hand-rolled UI in this codebase, and the raycasting Secret Realm trial (below)
already autoplayed end-to-end without it. Qi/generators/realm breakthroughs still run
exactly as before, just invisibly now: they drive the Secret Realm trial's combat
difficulty and Qi rewards, and surface only as read-only stats — not a shop, nothing here
is tappable — in the app's stats panel.

The game plays itself, idle-game style: generators are auto-bought and realm
breakthroughs auto-triggered every 50ms tick (breakthrough is checked first, then one
purchase attempt per generator in unlock order) as Qi allows. There's no manual
tap-to-buy/tap-to-breakthrough anymore since there's no shop UI left to tap it on;
automation was already winning that race every time before the shop screen was removed,
so nothing playable was lost by deleting the losing path. Automated actions don't force
an immediate save (to spare NVS flash write endurance from very frequent writes) — the
existing 15-second periodic autosave covers them; brightness/volume (no longer
tap-adjustable, see below) still persist across reboots via the save file.

The Secret Realm raycasting trial (below) is now the app's entire UI, autoplaying from
first boot with no unlock gate — there's no other screen left to gate it behind. It's a
hand-rolled M5GFX HUD (no LVGL), composited into offscreen `M5Canvas` sprites and pushed
to the display once per frame to avoid flicker. Despite the panel being physically
landscape, M5GFX reports the Tab5's display as 720x1280 (portrait logical coordinates) —
confirmed by reading the M5GFX source and a live serial print on real hardware — so the
single screen is laid out as a vertical stack: a thin status header at top, the raycast
viewport filling nearly all of the remaining screen, and a compact, fixed-height
read-only stats strip anchored to the bottom — styled as a MapleStory-style gold/bronze
window frame around four individually bordered, glossy mini-bars sharing a single row (not
stacked into multiple rows, to keep the panel's bite out of the viewport above as small as
possible): player HP (authentic Maple red), enemy HP, route progress, and breakthrough
progress, left to right. Each mini-bar carries its own small corner rivets and a pixel-art
icon (heart/skull/flag/star) ahead of its label so all four stay identifiable at this
narrower per-bar width — computed at runtime from `M5.Display.width()`/`.height()`
rather than hardcoded. Held normally in landscape, this reads correctly on the physical
device. The header bar shows a small circular portrait badge (filled with the character's
own per-realm aura color, the same one drawn as a ring around the character in the zone
view, plus a gold ring border matching the panel's frame styling) alongside battery
percentage and charging state (a real reading from `M5.Power`) and the current realm name and
Qi/sec rate; there's deliberately no clock, since without Wi-Fi/NTP there'd be nothing to
keep it from silently drifting. Long realm names in the header and large Qi/sec and
offline-earnings figures are handled with measured `textWidth()`-based sizing and compact
`K`/`M`/`B` number formatting (e.g. `2.2M` instead of `2200000`).

Progress persists across power cycles via NVS (verified to survive real reboots, not
just a same-boot false positive), and the Tab5's battery-backed RTC grants offline
earnings on boot, computed with a custom epoch conversion (since `timegm()` isn't
available in this toolchain) checked against leap-year and century-boundary edge cases.

Design spec: `docs/superpowers/specs/2026-08-27-xianxia-idle-game-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-xianxia-idle-game.md`

### Secret Realm (raycasting trial mode)

The Secret Realm trial is no longer something you reach — it *is* the screen, running from
the moment the device boots, with no realm-based unlock gate. (There's nothing left to gate
it behind: the idle view it used to launch from via a HUD button is gone.) It's a small
fixed maze rendered with classic DDA raycasting (Wolfenstein/Doom-style: one ray per screen
column, textured walls, billboarded enemy sprites depth-tested against the wall raycast so
nearer walls correctly hide them) — raycasting was chosen over the old crystal's triangle
rasterizer because it's the right tool for a full-screen first-person view and sidesteps a
painter's-algorithm rasterizer's inability to correctly sort multiple independent
overlapping objects, a distinction that matters even more now that this is the only screen
in the app.

Like the rest of this game, the trial **autoplays**: the camera follows a scripted waypoint
route through the maze and auto-fights enemies it encounters (tick-based, deterministic, no
RNG), the same "the game plays itself" philosophy the old crystal and the generators already
used. Combat stats derive from cultivation progress (`playerMaxHP = 100 + 40 * realmIndex`,
`playerAttackDamage = 10 + 6 * realmIndex`), so a weak cultivator can genuinely lose to a
later enemy — on defeat, the trial resets to its start with full HP and no permanent penalty
(only time lost); clearing it grants a Qi reward back into the main economy and loops. Wall
textures and the attack/hit/victory sound effects are procedurally generated in code (a
deterministic hash-based pattern) — no imported image or audio assets, consistent with the
rest of this project.

Making this trial the app's only screen surfaced two real bugs while tracing its code for
that change. First, the camera used to snap instantly to a new heading in a single tick at
each of the maze's corners; on real hardware, at whatever FPS this renders at, an
instantaneous heading jump reads as a hard cut, not a turn. `tickTrial` now eases
`facingRadians` toward the desired heading at a fixed turn rate (`kTurnRateRadiansPerSec`,
180 deg/sec — about half a second for the maze's 90-degree corners), always turning the
shorter way around the ±π wrap boundary; movement toward the waypoint keeps stepping
straight ahead regardless of how far the camera has turned to face it yet. Second,
`restartTrial` used to re-derive player combat stats from whichever realm the trial had
first ever started at, frozen for the rest of the run — since the trial now runs unattended
indefinitely with no idle screen to re-enter from, that meant the character could never
visibly grow stronger no matter how long it ran. It now takes the caller's *live* realm
index (`gState.realmIndex`) on every restart instead, so player HP/attack keep pace with
cultivation progress across an indefinite autoplay session.

Design spec: `docs/superpowers/specs/2026-08-27-secret-realm-raycasting-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-secret-realm-raycasting.md`

The screen deletion, the new single-screen layout, and both fixes above are their own
follow-up revamp:
Design spec: `docs/superpowers/specs/2026-08-27-raycasting-only-revamp-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-27-raycasting-only-revamp.md`

**Known limitation — not yet validated on real hardware.** Unlike the old crystal's
`kRenderSize = 240`, which was empirically tuned against measured on-device FPS (Task 8 of
the original idle-game plan), no physical Tab5 was connected while this mode was built, so
its render resolution (240×320 internally) is a conservative starting guess, not a
benchmarked value — raycasting is much cheaper per pixel than the old crystal's triangle
rasterizer, but the actual achievable FPS at this resolution is unconfirmed. Viewport *fit*
is no longer part of this risk: the display scale used to be a fixed constant (`kTrialZoom`)
tuned for the old, much taller "header to return-button strip" region, and it silently
overflowed into the header and stats panel once the raycasting-only revamp halved the
viewport height. That constant is gone — `renderTrialView()` in `src/trial_view.cpp` now
computes the zoom live from the actual viewport (`raycastViewportBottom()`/`kHeaderHeight`),
so it can never overflow regardless of display size or future layout changes. If it runs
slower than expected on real hardware, `kTrialViewWidth`/`kTrialViewHeight` in
`src/trial_view.cpp` are the knobs that affect actual compute cost. The full
esp32p4_pioarduino build does compile and link successfully (verified in this environment),
but flashing and visually/FPS-testing it on the device is the next step for whoever has it.

### Character skills & graphics pass

The character has a growing, fully-automatic skill kit — 8 realm-gated skills
(`lib/core/skills.h`), one unlocking every 2 realms, firing round-robin among whatever's
currently unlocked while `Fighting`, dealing bonus damage on top of the untouched
`zone_combat` autoattack. No manual activation — this app has no touch controls left, and
skills don't add any.

Combat now shows floating damage numbers, a skill projectile that travels from the character
to the current enemy and bursts on impact (color-coded per skill), and a brief screen shake
reserved for skill impacts. The character has arms, a 4-frame walk cycle, and a per-realm aura
ring. Monsters get tier-distinct silhouettes (round/spiky/winged) instead of a uniform colored
circle. The background now drifts with parallax clouds/embers/stars (realm-dependent) and
deterministic ground texture. Platforms are dressed with alternating flickering torches and
hanging cloth banners: the banner is tinted with the same per-realm hue as the ledge itself
(so it never clashes with a realm's palette), while the torch's flame deliberately stays a
fixed warm orange/yellow regardless of realm, so it still reads as fire. Together they make
the zone feel less like a bare set of ledges and more like a decorated MapleStory
town/dungeon.

The character no longer repeats the exact same motion every time: its casting pose now varies
by which skill just fired (`castPoseFor()` in `src/zone_view.cpp`) — a forward lunge for the
melee slash, a low wide-armed wind-up for the two area-effect skills, and an overhead channel
for everything else — instead of always raising both arms the same way. Standing and fighting
between skill casts adds a slow breathing sway instead of freezing solid, and the walk cycle's
cadence is jittered +-15% per platform (hash-seeded, so it's still deterministic) so an
autoplaying character crossing many platforms back to back doesn't read as one perfectly
looping stride forever.

Defeating a monster now pops a brief gold sparkle burst at its last position (reward feedback
for the kill itself, distinct from the per-hit attack flash/damage number), and ranking up a
cultivation realm now triggers an expanding gold/white ring-and-rays celebration centered on
the character plus its own rising fanfare — previously both the kill and the breakthrough were
visually silent beyond a bar updating.

All of this is procedural M5Canvas drawing — no image or audio assets — consistent with the
rest of this project. New deterministic math (skill unlock/cycling, shake/rise/parallax/pulse
curves) lives in `lib/core/skills.{h,cpp}` and `lib/core/fx.{h,cpp}` and is unit-tested; the
drawing itself is hardware glue in `src/zone_view.cpp` and `src/ui.cpp`, unvalidated on real
hardware like everything else in this project.

Design spec: `docs/superpowers/specs/2026-08-28-maplestory-skills-and-graphics-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-28-maplestory-skills-and-graphics.md`

The zone's layout itself used to be completely fixed per realm: always exactly 4 platforms (1
ground + 3 elevated) and always exactly 3 monsters, one per elevated platform, dead-center every
time. Since the zone loops constantly within the same realm (realm only advances on a rare
breakthrough), that meant grinding one realm for a while showed the literal same layout and same
3 monsters in the same spots over and over. `makeZoneMap()` in `lib/core/zone_map.cpp` now takes
a `seed` in addition to `realmIndex`, salting every hash-based structural roll (platform
count/gaps/widths/heights, monster count/position) — elevated platforms now vary 3-5 per zone,
and each spawns 1 or 2 monsters at a randomized interior position instead of always the exact
midpoint, so a typical zone runs busier (roughly 4-7 monsters, occasionally more) than the old
fixed 3. `ZoneState` tracks a `zoneRunIndex` that `restartZone()` bumps on every loop (both the
clear-and-loop-again path and the death-restart path) and feeds back in as the next seed, so
looping the same realm reshuffles the terrain and monsters each time instead of regenerating the
identical layout. The realm's color palette deliberately stays keyed to `realmIndex` alone (not
`seed`) — only structure/placement reshuffles, so a realm still reads as the same "place" even as
its terrain and monster spread vary loop to loop. Difficulty still climbs by which platform a
monster is on (two monsters sharing a platform share its difficulty), now via a smooth per-tier
formula instead of the old fixed 3-entry table - but capped at the old design's highest tier
(platform 3's), since platforms 4 and 5 are new and the difficulty curve was only ever tuned and
validated up through 3. Without that cap a low-realm character could roll a 5-platform zone
whose platform-4/5 monster is flatly unbeatable solo; extra platforms now only add more
monsters, never tougher ones. More monsters per zone also meant more unhealed combat between an
unchanged reward and an unchanged player HP pool, so `tickZone()` now fully heals the player on
every monster kill, not just at zone start - each fight goes back to being its own "can I beat
this one enemy" test instead of chip damage accumulating across a whole run. Together these
restore roughly the original ~100% first-attempt clear rate at every realm from 1 to 15 (measured
by simulation), instead of the 10-57% collapse an early version of this change had introduced.

### Boss Encounters

Every third loop of the zone (`kBossZoneInterval` in `lib/core/zone_state.h`, so `zoneRunIndex`
2, 5, 8, …; the very first zone of a session is never one) is a dedicated **boss zone**. Its
terrain rolls exactly as any other zone's does — still 3-5 elevated platforms, still reshuffled
per loop — but the normal per-platform monster roster is replaced by a single boss standing at
the midpoint of the zone's last elevated platform, and every other platform is empty. The route
to it is therefore the same walk/jump machinery as always; only what waits at the end changes.

The boss has its own silhouette rather than one of the three regular tiers: a body larger than
even the toughest tier's, in its own darker, more saturated per-realm `bossColor()`, wearing a
five-spike gold crown whose spikes are anchored on the body's own circle (`baseY = cy -
sqrt(r² - dx²)`) so they hug the dome instead of floating beside it. The zone view reserves
enough headroom for that full sprite (`kTopMarginPx` is derived from the boss's own maximum
height, 122px, not guessed separately) so a boss on a tall platform can't have its crown clipped
off the top of the viewport. While a boss fight is live the enemy stat bar relabels itself
`BOSS HP n/n` and swaps to its own alarming fill/gloss colors.

Its one mechanic is a **one-time enrage**: the first tick the boss drops to half HP or below, its
attack cooldown is multiplied by `kBossEnrageCooldownMultiplier` (0.7, so ~43% faster swings) and
latches — a boss never heals, so it can never re-trigger. That moment fires a red flash on the
boss, a screen shake more than twice a skill impact's, and a harsh low-to-high snarl. Killing the
boss stages a **bonus Qi reward equal to the zone's own clear reward**, so a boss zone pays out
roughly double a regular zone's Qi (the bonus at the kill, then the normal clear reward moments
later once the character walks to the last ledge), and triggers its own expanding red/gold
ring-and-rays burst plus a short fanfare, deliberately distinct from both the zone-clear jingle
and the realm-breakthrough celebration. Losing to a boss costs nothing but time, same as every
other defeat here: the zone restarts from the beginning with the staged bonus discarded.

Boss HP/damage (`kBossBaseHp`/`kBossHpPerRealm`/`kBossBaseDamage`/`kBossDamagePerRealm` in
`lib/core/zone_map.cpp`) were tuned by simulation, not by inspection — the first-pass numbers made
the fight unwinnable at every realm. The tuned boss is an endurance test rather than a burst
threat: roughly 4x a matched-realm top-tier monster's HP (a ~12-22s fight instead of a regular
~3-5s one) at well below a regular monster's per-hit damage. Note that combat here is fully
deterministic (no RNG anywhere, and stats key off `realmIndex` only), so the seed-sweep clear-rate
tests in `test/test_zone_state/` measure "winnable at this realm" all-or-nothing, not a true
clear-rate distribution — realms 0, 1 and 15 are each pinned as winnable on the first attempt.

Wiring the boss FX into the game loop also turned up a pre-existing bug worth recording. `loop()`
used to infer "the player just died and the zone restarted" from `player.hp` going *up* across a
tick — but `tickZone()` full-heals the player on **every** monster kill (added by the platforms
revamp above), so any kill where the player had taken damage looked identical to a restart and
silently suppressed that kill's own hit/loot/boss-defeat effects. Measured at realm 5, zero of
seven regular kills in a zone were observed; for a boss, whose fight always runs long enough to
take damage, it was 100% of kills at every realm, so the boss defeat FX/SFX above were dead code
as first merged. The per-tick event derivation now lives in `lib/core/zone_events.{h,cpp}` as a
pure `deriveZoneTickEvents(before, after, …)` function keyed off `zoneRunIndex` (which only
`restartZone()` ever bumps) instead of the HP heuristic — and, being in `lib/core/` rather than
`src/`, it is finally unit-testable, which is precisely why the original bug survived six task
reviews unnoticed.

**Known limitation — not yet validated on real hardware.** The boss silhouette, crown geometry,
HUD treatment, enrage/defeat FX, and both new SFX are unflashed: the esp32p4_pioarduino build
compiles and links successfully in this environment, but nothing here has been seen or heard on a
physical Tab5, and the balance numbers come from native simulation rather than play. The boss
difficulty curve is also known to run slightly backward — a realm-0 player finishes at ~20% HP
while a realm-15 player finishes at ~51%, because `kBossHpPerRealm` doesn't keep pace with player
DPS growth — which is a deliberate follow-up tuning pass, not a defect in the mechanic.

Design spec: `docs/superpowers/specs/2026-08-28-boss-encounters-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-28-boss-encounters.md`

### Ascension & Realm Identity

Cultivation used to dead-end at realm 15 (Empyrean Realm): `canBreakthrough()` returns `false`
forever once there, and Qi kept accumulating from the player's generators with nothing left to
spend it on. A new automated **ascension** system (`lib/core/ascension.{h,cpp}`) fixes this: once
the player reaches realm 15 and banks enough Qi past that point (a threshold that itself grows
with each successive ascension, mirroring how `REALM_QI_THRESHOLD` grows realm to realm), the
game automatically "ascends" - qi, generator counts, and realm index hard-reset to a fresh
game's starting values, and the Qi spent converts (`sqrt`-scaled, so very large late-game Qi
numbers yield sane, modest gains) into permanent **insight**, a prestige currency that never
resets and compounds into an ever-growing Qi/sec multiplier for every future run. No manual
trigger of any kind - it fires from the same automated tick loop that already drives
breakthroughs and generator purchases, checked once per tick immediately after breakthroughs
resolve. The header's stats readout gains an "Asc N (xM.MM)" suffix once the player has
ascended at least once, and stays exactly as compact as before for a fresh game that hasn't.

Separately, realm growth used to be lopsided: `SKILLS[]` only unlocks a new combat skill every
*other* realm (0, 2, 4, ... 14), leaving odd realms granting nothing beyond the existing linear
HP/damage scaling formula. A parallel **Realm Identity** trait table
(`lib/core/traits.{h,cpp}`) fills exactly those odd realms (1, 3, 5, 7, 9, 11, 13, 15) with one
new passive, always-on, fully automatic trait each - Iron Skin (damage reduction), Steady
Breath (HP regen while fighting), Soul Echo (every 4th landed autoattack echoes for bonus
damage), Execution (bonus damage finishing a weakened foe), Swift Feet (faster platform
movement), Radiant Aura (a periodic damage tick independent of autoattack/skill cooldowns),
Undying Will (survives one fatal hit per zone run), and the capstone Empyrean Radiance
(amplifies all skill damage). Combined with the existing skill table, every single realm from 0
to 15 now grants something new. All eight are deterministic (no RNG, matching this project's
combat philosophy throughout) and implemented as small, targeted additions inside `tickZone()`'s
existing Walking/Fighting branches - no new `ZonePhase`, and `tickCombat()`'s only change is one
defaulted `incomingDamageMultiplier` parameter (Iron Skin), following the same
backward-compatible-defaulted-parameter pattern `makeZoneMap`'s `seed`/`isBossZone` established.

Since ascension resets `realmIndex` to 0, both systems compound together across repeat runs: a
higher `insight` multiplier means a faster subsequent climb back through all 16 realms and all
16 unlocks (8 skills, 8 traits) each time.

Both are unit-tested end to end in `test/test_ascension/` and `test/test_traits/`, plus new
integration cases in `test/test_zone_state/` and `test/test_zone_combat/` for how the traits
hook into live combat/movement - no device required.

**Known limitation — not yet validated on real hardware.** The ascension threshold-growth and
insight-to-multiplier constants, and all eight trait magnitudes (damage reduction/regen
rate/echo interval/execution threshold/movement speed/aura interval/skill multiplier), are
first-pass numbers set by inspection here, not yet run through a simulation sweep the way boss
stats were - a follow-up tuning pass, not a defect in the mechanics themselves. The ascension
fanfare/FX and the HUD's new "Asc" readout are likewise unflashed, same caveat every prior
spec in this project has carried.

Design spec: `docs/superpowers/specs/2026-08-29-ascension-and-realm-identity-design.md`
Implementation plan: `docs/superpowers/plans/2026-08-29-ascension-and-realm-identity.md`

### Settings: brightness & volume

Two full specs' worth of investigation never found a confirmed root cause for the brightness/
volume rows being unresponsive to touch on real hardware, so the interactive controls were
removed outright rather than continuing to chase it — this app now has **no touch controls at
all**. `gBrightness`/`gVolume` still load from and save to NVS, and still apply via
`M5.Display.setBrightness()`/`M5.Speaker.setVolume()` at boot exactly as before; they're simply
fixed for the session rather than player-adjustable. The freed panel space went to the zone
viewport.

### Building & Flashing

Requires [PlatformIO](https://platformio.org/):

```bash
python3 -m pip install --user platformio
python3 -m platformio run -e esp32p4_pioarduino -t upload --upload-port /dev/ttyACM0
```

Serial console (115200 baud):
```bash
python3 -m platformio device monitor --port /dev/ttyACM0 --baud 115200
```

### Running Tests

Game logic (3D vector/matrix math, the idle-game economy, save serialization and its
v1->v2->v3 migrations, offline-earnings math, HUD hit-testing, the MapleStory-style zone's
terrain generation, jump arc, patrol motion, combat resolution, procedural textures, and
autoplay state machine, per-tick combat event derivation, brightness/volume clamping, the
character skill kit and realm identity traits, the ascension prestige loop, and FX curves) is
hardware-agnostic C++ under `lib/core/`, unit-tested on the host machine — no device required.
226 test cases across 17 suites, all passing:

```bash
python3 -m platformio test -e native
```

`src/` (display setup, the game loop, HUD drawing, NVS/RTC glue) is Arduino/hardware
glue instead, and was validated on the physical Tab5 rather than in `native` tests — 12+
flashes across this project with zero panic/crash/watchdog signatures.

### Project Layout

- `lib/core/` — hardware-agnostic game logic, unit-tested via the `native` PlatformIO
  environment: `math3d` (vector/matrix math left over from the deleted crystal renderer;
  still unit-tested, no longer used by any production code), `economy`/`save`/
  `offline_earnings` (the idle-game loop and persistence, including the v1->v2 save
  migration, plus the v2->v3 ascension migration), `ascension` (the prestige loop: insight
  currency, Qi/sec multiplier, auto-trigger threshold), `zone_map`/`zone_combat`/`zone_state`/
  `zone_textures` (the MapleStory-style zone's terrain generation, combat resolution, autoplay
  state machine, and procedural colors), `zone_events` (the pure before/after diff that turns
  one `tickZone()` call into the discrete hit/kill/enrage/restart events `main.cpp` fires FX and
  SFX off), `settings` (brightness/volume clamping), `skills` (realm-gated automatic combat
  skills, unlocking on even realms), `traits` (realm-gated passive Realm Identity traits,
  unlocking on odd realms), `fx` (pure shake/damage-number/parallax curves for zone_view).
- `src/` — Arduino/M5Unified/M5GFX glue: `main.cpp` (setup/loop, the 50ms game tick,
  automation, and driving the always-on zone view — there's no `ViewMode` switch anymore,
  just the one screen), `ui.h`/`ui.cpp` (header and stats panel layout and drawing; no
  tappable controls remain), `zone_view.h`/`zone_view.cpp` (zone rendering, combat/skill FX,
  and SFX), `nvs_store`/`rtc_store` (persistence and offline-earnings glue).
- `test/` — one PlatformIO test suite per `lib/core/` module.
- `docs/superpowers/specs/`, `docs/superpowers/plans/` — design specs and implementation
  plans for the xianxia idle game, the Secret Realm trial mode, and the raycasting-only
  revamp that made it the app's only screen.
