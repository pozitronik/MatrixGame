# Autonomous targeting during robot delivery

`game.outcome.transport_ai_acquisition` completes the selected O3 acquisition/aiming boundary alongside the [transport collision cases](85-transport-collisions.md). It advances the production map, robot perception, enemy-side tactical AI, aiming, weapon ticks and damage receivers. It never submits a target, fire order, launch ray or damage call after setup. Production gameplay is unchanged.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.transport_ai_acquisition$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.transport_ai_acquisition$'
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game\.outcome\.transport_ai_acquisition$'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^game\.outcome\.transport_ai_acquisition$'
```

## Declared scenes

Every scene uses seed 85, both supported RNG modes and independent 10/16 ms schedules. Synthetic storage-backed meshes, unavailable audio and the explicit unavailable-icon-renderer profile require no original assets or native window. The enemy observer begins at `(80,100)` with one laser; a distant unarmed player robot at `(600,600)` keeps the player active in the empty-carrier control. Initial maximum movement speeds are zero; the production collision/position corrections still run and are included in replay observations.

The laser has range 500, a 100 ms cooldown and damage 1; the enemy-to-player damage multiplier is 1. Armor rotation speed is 0.04. One heat unit per emission, zero cooling and a 100 ms cooling period keep heat monotonic and below the overheat limit for the bounded scene. These are explicit synthetic fixture settings, not proposed balance changes. All moving carrier, cargo and target states after setup come from ordinary map ticks.

| Scene | Setup and duration | Required behavior |
| --- | --- | --- |
| Delivery | A player-owned body-plus-weapon transport created through `OrderFlyer` carries its generated unarmed robot to `(200,300)`; advance 12 seconds | The enemy acquires and aims at the cargo while it is carried, then emits laser fire and damages it after landing; the carrier eventually retires |
| Empty carrier | Remove cargo through map-owned deletion before the first tick; advance 2 seconds | The enemy acquires no target, issues no fire order and emits no laser fire despite the hostile carrier being present |
| Ground robot | Replace cargo before the first tick with an ordinary chassis at `(200,300)`, initialized with 1500 internal health units; advance 2 seconds | The same observer acquires, aims at and damages the ground robot, demonstrating that the empty-carrier rejection is not a disabled-AI fixture |

The ground control uses the ordinary chassis fixture rather than the generated cargo's armor-bearing model. It establishes functioning perception and combat, not a comparison of otherwise identical model geometry. The transport's mounted weapon is loaded normally, but the scene does not call the unused combat-flyer firing path.

## Acquisition, intention and emission

`GatherInfo(0)` considers live hostile robots and cannons. A carried robot remains a live robot in the logic list; a flyer is outside that candidate set. The observer must never remember or target the carrier, and it must not acquire the distant sentinel. At every boundary its enemy count must equal the presence of the one eligible target.

The first scheduled perception update occurs strictly after 100 ms: at 110 ms with 10 ms steps and 112 ms with 16 ms steps. The first tactical selection and `ROT_FIRE` order follow one map step later, at 120/128 ms. Every observed fire order must target the intended live robot and contain exactly its current geometric center plus five world units in Z. Both target models have no aim-protection deviation; no tolerance is applied to these coordinates.

An order to fire does not prove that the weapon has emitted. Robot firing still applies its ordinary range, barrel-direction, rotation and cooldown behavior. `IsFireWas()` consumes a flag used by production and is deliberately not read by the test. Instead, the configured monotonic heat records actual emission through the existing production firing path; health loss independently establishes a hit. No observation counter is added to the engine.

In the selected delivery scene, acquisition and the first fire order precede cargo release, while the first observed emission and first health loss coincide after landing, within one second of it. The ground control must fire and damage its target within the first second. This freezes those bounded relationships without claiming airborne immunity or pinning a universal firing delay. Other launch geometry can hit attached cargo, as the separate projectile contracts demonstrate.

For reference, the MinGW Debug and Release runs measured the following delivery events. These measurements describe this fixture and compiler profile; exact replay compares each profile with itself.

| RNG / step | Release | Landing | First emission and damage |
| --- | ---: | ---: | ---: |
| ParkMiller / 10 ms | 6100 | 6420 | 7100 |
| ParkMiller / 16 ms | 6112 | 6432 | 6944 |
| LegacyCRT / 10 ms | 4940 | 5260 | 5900 |
| LegacyCRT / 16 ms | 4944 | 5264 | 5824 |

## Lifetime and replay

Retained object cores are checked before reading target, observer, sentinel or carrier state. Cargo progresses through carrying, falling and successful landing; a live carrier's carrying link must agree with cargo state. Once landed, cargo must stay active through the continuation, even after carrier retirement. The sentinel remains undamaged, neither side receives robot-kill credit, and every observed frame keeps the player active with no result dialog or pause.

Each of the twelve scene/RNG/schedule profiles runs twice in a fresh session. Ordered snapshots include the existing registered-robot and RNG observations, normalized target roles, enemy count, heat, fire-order aim, target health/state and carrier position, health, carrying link and lifetime. Floating-point observations use raw bits; pointer addresses and process-global IDs are not compared. The extra observations are compared in memory; this case does not add a persisted trace format or an accepted historical reference artifact.

## Validation and limits

The actual standalone game, x86 DLL and affected test target build with pinned MinGW in Debug and Release, with cheats disabled. The new case plus the four existing armed-transport collision, survival and context controls pass in all four configurations. Sensitivity controls reject omission of carried-robot perception, a changed aiming coordinate and suppressed laser damage; exact production sources are restored and the positive case rebuilt and rerun after each control.

Native rendering, physical obstacle approaches, original-host loading and the remaining baseline integration evidence are separate obligations. The synthetic schedules establish automatic target eligibility and the selected delivery combat continuation, not all possible carrier geometry or autonomous firing by a carrier. Supported MSVC validation remains for CI. Issue #85 and the complete-baseline acceptance gate remain open.
