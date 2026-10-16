# Capture timing profiles

This supplement records the scheduled capture cases extended to both 10 ms and 16 ms in O2 of the [outcome completion plan](85-outcome-completion.md). Each case runs ParkMiller and LegacyCRT separately. The fixtures use synthetic resources and production map, building, robot and side logic; they do not load an original map or render a native window. Existing 10 ms observations remain unchanged.

## Cases and observations

`game.outcome.registered_base_capture` runs neutral and enemy bases through the context command. `game.player.capture_keyboard_completion` and `game.player.capture_button_completion` reuse the same base journey through their respective input routes, alongside their existing factory journeys. The test observes opened/closing base states and ownership transfer on every map tick. Robot entry is a phase precondition at 2000 ms in the interruption and damage cases, not a separately measured transition in the completion case. Enemy bases contain two queued robots: the queue and old owner's balances remain intact until transfer, then both queued items are removed and all four resource costs refunded. The capturer is consumed, its companion survives, and the base remains closed under its new owner for another 100 ticks. The fixture uses seed 79 and a maximum of 3000 frames.

`game.outcome.base_capture_interruption` uses seed 85 and dispatches Stop while the base is opening, while the robot is inside the opened base, and after closing begins. The 16 ms inputs run at the first scheduled tick at or after each original 200/2000/4500 ms command. The intended phase and manual-input restriction are asserted before the command. Opening can be canceled without killing the capturer; the two later cancellations mark it for death without kill credit. The base remains neutral and is closed by the 10000 ms observation deadline. Each RNG/timestep/command profile is replayed in a fresh synthetic session and its complete post-command snapshot sequence compared.

`game.outcome.capture_combat_death` delivers lethal laser damage directly to a capturer after its first painted point, then advances the production death and capture logic. It records replacement assignment, ownership transfer and retirement of the original robot's retained core. The surviving robot completes capture, the enemy receives exactly one robot kill, and the side remains active. This is a damage-receiver/lifetime check; it does not establish a rival firing encounter. Seed 85 is used, with observations through the first tick at or after 11000 ms; each RNG/timestep profile is replayed and its full snapshot sequence compared.

`game.outcome.capture_target_damage` directly delivers lethal damage to a factory after painting starts or to a base during robot entry. Production damage processing detaches capture orders immediately; subsequent map ticks clear the group target, create ruins and retire the building. Base destruction also kills the trapped capturer without adding a robot kill; factory destruction leaves it alive. Blast damage is disabled to isolate this lifetime contract. Seed 85 is used, with observations through 10000 ms and a complete replay comparison for each RNG/timestep/building profile.

`game.outcome.capture_target_removal` deletes a registered factory at 200 ms or 208 ms, after verifying the live capturer/target link. It checks immediate release of the robot's capture target and order, then checks the first player-group update that releases it: 210 ms or 240 ms. The stale pointer is compared without dereferencing it at each preceding tick, including 200 ms or 224 ms, and the Stop/null-target state is checked again at 400 ms. Both robots remain alive. `game.outcome.capture_candidate_lifetime` observes candidate publication after the first tick, checks that publication at 1020 ms or 1024 ms has not duplicated core references, deletes the building, and advances another tick to verify that only the test's retained core remains. These cases use seed 79 and a 1000-frame safety limit.

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
| Factory deleted / group target cleared | 200 / 210 | 208 / 240 |

The timing follows several separate production clocks. [Map logic](../../MatrixGame/src/MatrixLogic.cpp) divides a 16 ms tick into 10 ms and 6 ms object-logic portions, with buildings ahead of robots in this fixture's logic list. The base floor moves by `BASE_FLOOR_SPEED * cms`, or 0.0128 over a full 16 ms tick, and changes state only after passing its endpoint. Whether a robot opens or closes the floor in the first or second portion changes the remaining travel by one portion; this produces the 78/79-tick journeys in [building logic](../../MatrixGame/src/MatrixObjectBuilding.cpp). Movement and rotation before closing still require their measured schedule.

[Player-group logic](../../MatrixGame/src/MatrixSide.cpp), `TaktPL`, waits at least 100 ms between updates. Its 16 ms schedule therefore runs every 112 ms, at 16, 128, 240 ms and so on, rather than at every object-logic portion. Target release and replacement follow the next player-group update. After the 2000 ms base hit, the stale target survives the 2016 ms tick and is cleared at 2032 ms. Building retirement follows another clock: each object-logic portion checks its hit-point countdown before subtracting that portion's elapsed time, giving 5008 ms rather than 5010 ms in the base case.

Painting and candidate publication use absolute map-time deadlines in [building logic](../../MatrixGame/src/MatrixObjectBuilding.cpp); debris lifetime uses the time checks in [robot logic](../../MatrixGame/src/MatrixRobot.cpp). Their next eligible tick is independent of the floor and player-group gates. These mechanisms explain why copying or rounding a 10 ms transition does not reliably describe the 16 ms profile.

## Component schedules, reassignment and income

The remaining component cases now run seed 79 with both RNG modes and 10/16 ms map clocks: `neutral_capture`, `enemy_capture`, `capture_rejection`, `competing_capturers`, `competing_color`, `capture_rollback`, `enemy_capture_rollback`, `acquisition_after_removal` and `capture_queue_refund`. Their building is deliberately outside the map's scheduled logic list. Each case explicitly invokes `Capture` or `LogicTakt`; map ticks supply the production clock and live robots. These checks preserve the original injected income and rollback thresholds (999/1, 1249/1/0 and 1250 ms). The 16 ms profile tests the last map tick at or before each absolute paint/erase deadline, then the following tick. It is not a substitute for the registered-building journeys.

`capture_move_reassignment` uses a registered neutral factory at (160, 200). After its first paint point, the selected group receives Move to (120, 500) at 1000/1008 ms. The capture references and robot order are released immediately, while the periodic nearest-robot scanner can still paint during departure. Rollback reaches zero at 7810/7856 ms. A new context capture order at 10000 ms replaces Move, and ownership transfers at 35290/35248 ms after the return journey. The tests check the preceding tick's paint/owner state, both robots' survival and capture-order release. This case observes a replacement command; it does not assert that a different robot must become capturer. It allows 6000 frames and uses unavailable synthetic audio.

`capture_income_continuity` runs one enemy titanium factory continuously through erase, neutral and new-owner phases. After each map tick it calls `Capture` until transfer and then `LogicTakt(step)` on the same component building. All four resource balances are checked throughout: the old owner keeps earning until neutralization, neither side earns while neutral, and only the new owner earns afterward. Starting balances are 100 for each enemy resource and 200 for each player resource. At 20000 ms, enemy titanium is 200 and player titanium is 220; all other balances are unchanged. The resource period pauses while neutral and carries its remainder into the new owner's period. This is continuous component income coverage, with no income artwork or registered-map income presentation.

| Component transition | 10 ms profile | 16 ms profile |
| --- | --- | --- |
| Enemy factory neutralized / captured | 10510 / 18020 | 10512 / 18016 |
| First new-owner income | 18510 | 18592 |
| Move cancellation: paint rolled back / replacement capture completed | 7810 / 35290 | 7856 / 35248 |

## Queue scope and reachable actions

Production ordinary construction adds robots through [the constructor](../../MatrixGame/src/Interface/CConstructor.cpp) and cannons through [player placement or AI construction](../../MatrixGame/src/MatrixSide.cpp). The robot constructor is selected for bases; cannon placement uses the active building. `CBuildStack::AddItem` accepts these items up to its capacity. Player cannon placement can append to an existing queue, while AI `BuildCannon` refuses a nonempty queue. Flyer branches in queue deletion and commented construction code do not establish an ordinary flyer enqueue action.

`capture_turret_refund` isolates factory ownership cancellation with two under-construction cannons. It constructs their queue entries directly, with the old owner and parent building recorded, then calls production `Capture` through neutralization. Both entries remain until that boundary at 10510/10512 ms. `ClearStack` deletes them, decrements the parent's turret count and refunds each cannon's configured costs (17, 18, 19, 20) to its old owner exactly once; later player capture grants no second refund. The fixture does not run placement eligibility, turret AI or construction progress. The existing `game.production.turret_progress` remains the separate construction-progress/cancellation check.

Mixed robot/cannon queues require a separate reachable timing scenario. A base with an existing cannon diverts a new capture order into defence, even while that cannon is under construction. A cannon added after the robot has entered the base may instead remain queued until transfer. Establishing that path requires the real player placement action with an eligible slot, followed through the active capture; a direct queue insertion is insufficient to establish it. This variant remains open. No mixed queue or flyer path is declared unreachable on the basis of this component fixture.

## Validation and limits

Run the affected cases from the repository root with the pinned toolchain:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(outcome\.(registered_base_capture|base_capture_interruption|capture_combat_death|capture_target_damage|capture_target_removal|capture_candidate_lifetime)|player\.capture_(keyboard|button)_completion)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(outcome\.(registered_base_capture|base_capture_interruption|capture_combat_death|capture_target_damage|capture_target_removal|capture_candidate_lifetime)|player\.capture_(keyboard|button)_completion)$'
```

The additional component and reassignment checks can be selected with `^game\.outcome\.(neutral_capture|enemy_capture|capture_rejection|competing_capturers|competing_color|capture_rollback|enemy_capture_rollback|acquisition_after_removal|capture_queue_refund|capture_income_continuity|capture_turret_refund|capture_move_reassignment)$` in the same Debug and Release commands.

A local negative control omitted the energy refund in `CBuildStack::ReturnTurretResources`; `capture_turret_refund` failed at the all-resource refund assertion. The production source was restored and the affected suite rerun.

All assertions use `MG_CHECK`, including replay equality, so they remain active in Release. Each profile prints its seed, timestep, RNG and effective build configuration before running. Compiler and x86 DLL validation belong to the supported CI matrix; this supplement does not establish historical persisted replay or whole-baseline acceptance.

O2 remains open for the reachable player-placement and mixed-queue transfer scenario described above. The component scheduling extensions, Move reassignment and continuous component income are implemented; component turret refunds do not establish ordinary placement eligibility. Armed rival/guard encounters remain O1. These scheduled fixtures require no human playtest, but they do not replace the outcome package's separately defined native validation.
