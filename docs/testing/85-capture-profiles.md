# Capture timing profiles

This supplement records the scheduled capture cases extended to both 10 ms and 16 ms in O2 of the [outcome completion plan](85-outcome-completion.md). Each case runs ParkMiller and LegacyCRT separately. The fixtures use synthetic resources and production map, building, robot and side logic; they do not load an original map or render a native window. Existing 10 ms observations remain unchanged.

## Cases and observations

`game.outcome.registered_base_capture` runs neutral and enemy bases through the context command. `game.player.capture_keyboard_completion` and `game.player.capture_button_completion` reuse the same base journey through their respective input routes, alongside their existing factory journeys. The test observes opening, entry, closing and ownership transfer on every map tick. Enemy bases contain two queued robots: the queue and old owner's balances remain intact until transfer, then both queued items are removed and all four resource costs refunded. The capturer is consumed, its companion survives, and the base remains closed under its new owner for another 100 ticks. The fixture uses seed 79 and a maximum of 3000 frames.

`game.outcome.base_capture_interruption` uses seed 85 and dispatches Stop while the base is opening, while the robot is inside the opened base, and after closing begins. The 16 ms inputs run at the first scheduled tick at or after each original 200/2000/4500 ms command. The intended phase and manual-input restriction are asserted before the command. Opening can be canceled without killing the capturer; the two later cancellations mark it for death without kill credit. The base remains neutral and is closed by the 10000 ms observation deadline. Each RNG/timestep/command profile is replayed in a fresh synthetic session and its complete post-command snapshot sequence compared.

`game.outcome.capture_combat_death` delivers lethal laser damage directly to a capturer after its first painted point, then advances the production death and capture logic. It records replacement assignment, ownership transfer and retirement of the original robot's retained core. The surviving robot completes capture, the enemy receives exactly one robot kill, and the side remains active. This is a damage-receiver/lifetime check; it does not establish a rival firing encounter. Seed 85 is used, with observations through the first tick at or after 11000 ms; each RNG/timestep profile is replayed and its full snapshot sequence compared.

`game.outcome.capture_target_damage` directly delivers lethal damage to a factory after painting starts or to a base during robot entry. Production damage processing detaches capture orders immediately; subsequent map ticks clear the group target, create ruins and retire the building. Base destruction also kills the trapped capturer without adding a robot kill; factory destruction leaves it alive. Blast damage is disabled to isolate this lifetime contract. Seed 85 is used, with observations through 10000 ms and a complete replay comparison for each RNG/timestep/building profile.

`game.outcome.capture_target_removal` deletes a registered factory at 200 ms or 208 ms, after verifying the live capturer/target link. It checks immediate release of the robot's capture target and order, then checks the group's Stop/null-target state at 400 ms. Both robots remain alive. `game.outcome.capture_candidate_lifetime` observes candidate publication after the first tick, checks that publication at 1020 ms or 1024 ms has not duplicated core references, deletes the building, and advances another tick to verify that only the test's retained core remains. These cases use seed 79 and a 1000-frame safety limit.

## Measured transition times

Times below are elapsed simulation milliseconds. They are independently observed schedules, not rounded copies of the 10 ms expectations. Except for the explicitly listed robot debris retirement times, both RNG modes produce the same boundaries. The tests retain the old 10 ms assertions, check the first observed transition tick, and assert the preceding tick's state for base phases, capture replacement/transfer, object retirement and delayed group-target release.

| Transition | 10 ms profile | 16 ms profile |
| --- | --- | --- |
| Base opened / closing / ownership transferred | 1280 / 4330 / 5580 | 1280 / 4336 / 5600 |
| Stop during opening: command / closed | 200 / 370 | 208 / 400 |
| Stop during entry: command / robot no longer live / closed | 2000 / 2010 / 3260 | 2000 / 2016 / 3264 |
| Stop during closing: command / robot no longer live / closed | 4500 / 4510 / 5580 | 4512 / 4528 / 5600 |
| Capturer lethal damage / replacement assigned / factory captured | 1000 / 1020 / 7530 | 1008 / 1040 / 7536 |
| Capturer core retired, ParkMiller / LegacyCRT | 5430 / 4110 | 5440 / 4112 |
| Factory lethal damage / group target cleared / building retired | 1000 / 1010 / 2010 | 1008 / 1024 / 2016 |
| Base lethal damage / group target cleared / building retired | 2000 / 2010 / 5010 | 2000 / 2032 / 5008 |

The base-destruction case illustrates why each schedule needs measurement: the 16 ms profile retires the building at 5008 ms, but retains the group target for two ticks after the hit. A timing rule derived by rounding each 10 ms timestamp upward would describe neither transition correctly.

## Validation and limits

Run the affected cases from the repository root with the pinned toolchain:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(outcome\.(registered_base_capture|base_capture_interruption|capture_combat_death|capture_target_damage|capture_target_removal|capture_candidate_lifetime)|player\.capture_(keyboard|button)_completion)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(outcome\.(registered_base_capture|base_capture_interruption|capture_combat_death|capture_target_damage|capture_target_removal|capture_candidate_lifetime)|player\.capture_(keyboard|button)_completion)$'
```

All assertions use `MG_CHECK`, including replay equality, so they remain active in Release. Each profile prints its seed, timestep, RNG and effective build configuration before running. Compiler and x86 DLL validation belong to the supported CI matrix; this supplement does not establish historical persisted replay or whole-baseline acceptance.

The rest of O2 remains open: other component cases still need 16 ms profiles, and Move reassignment, continuous income across enemy/neutral/new-owner phases, and reachable turret/mixed queue variants are not added here. Armed rival/guard encounters remain O1. These scheduled fixtures require no human playtest, but they do not replace the outcome package's separately defined native validation.
