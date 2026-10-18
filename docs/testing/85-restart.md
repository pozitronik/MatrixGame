# Storage-backed battle restart tests

The `game.restart.movement`, `game.restart.pending_win` and `game.restart.pending_loss` cases in [battle_restart_scenarios.cpp](../../tests/battle_restart_scenarios.cpp) exercise the existing battle-menu Restart command. They supplement O6 in the [outcome completion plan](85-outcome-completion.md); they do not initialize another standalone process or enter a second DLL battle.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.(movement|pending_win|pending_loss)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.(movement|pending_win|pending_loss)$'
```

## Fixture and entry path

The fixture starts with the existing synthetic terrain/device session, then writes a real `CStorage` map file containing two unarmed robots and distinct four-resource balances for each side. Synthetic storage meshes, cached texture pixels, robot names and bitmap-only dialog templates satisfy the production loaders without original game assets. The robot model has a chassis and no weapons. The temporary map file is removed on fixture teardown; it is not a substitute implementation of Restart.

Selection uses `CMatrixSideUnit::CreateGroupFromCurrent`. Move enters the real ground-context command handler with supplied world coordinates. Escape, R and Enter pass through `CFormMatrixGame::Keyboard`, production menu/confirmation hints and `OkResetHandler`. `CMatrixMap::Restart` deletes the old objects, effects and queues, loads the saved storage, constructs robots through `SSpecialBot::GetRobot`, restores resource balances and opens the introduction. The surrogate device reports unavailable robot icon targets, exercising their supported failure path; these cases do not render icons or other frames.

## Registered cases

Every case runs seed 79 independently in ParkMiller and LegacyCRT with 10 ms and 16 ms simulation steps. Each profile restarts twice in the same map instance, dismisses each introduction and submits a fresh Move. The suite records profile metadata and retains its bounded frame/wall-time limits.

| Case | Trigger and assertions |
| --- | --- |
| `game.restart.movement` | Confirm Restart after movement has begun. An additional registered base owns a queued robot in the old battle; neither exists in the saved scene. Restart retires both, clears its terrain footprint, reconstructs the saved robots and replaces all resource balances. |
| `game.restart.pending_win` | Remove the opposing army, wait for production to classify elimination and latch victory, then confirm Restart before its delayed result dialog. No old result appears during more than two seconds of subsequent simulation. |
| `game.restart.pending_loss` | Remove the player's army, wait for the pending defeat, then confirm Restart. Both saved sides return active and a new player robot accepts movement; the pending defeat does not reappear. |

All three cases check that the map instance, terrain allocation and simulation time survive the reload. They check fresh robot UIDs and initial positions, reset statistics, both sides' resource balances and active status, the introduction's pause ownership, empty current/scratch selection, unchanged RNG mode/draw count and no exit request. A live smoke effect is installed while confirmation is paused; Restart clears it and disconnects its handler. The movement case also checks disposal of the old build queue through the normal owning building destruction and Debug allocation accounting.

Reload does not reset a held arrow key. The tests record that retained input and release it explicitly before further simulation. The cases print the surviving status-check schedule: the recorded second pending-result trigger is 4030 ms in the 10 ms profile and 4048 ms in the 16 ms profile. Those elapsed trigger times are observations in the output, not exact timing assertions. The pending-result countdown itself is asserted zero immediately after Restart, before dismissing the introduction; the subsequent 2.2-second observation protects against a stale result appearing. These observations describe a reload of an existing map, not a fresh `CGame::Init` lifetime.

The pending-result assertions were checked with a temporary negative control that omitted the production countdown reset in `Restart`: both pending-result cases failed when the old dialog appeared after reload. The production reset was restored and the tests rebuilt before final validation.

## Retired selection-group probe

The opt-in `probe.restart.retired_group` case is not registered in CTest. Run it directly with the already built test executable:

```powershell
& '.\build\mingw-debug-exe\tests\matrixgame_battle_restart_scenarios.exe' probe.restart.retired_group
& '.\build\mingw-release-exe\tests\matrixgame_battle_restart_scenarios.exe' probe.restart.retired_group
```

The probe currently fails: an inactive selection group can retain the address of a deleted robot across Restart. It compares group entries against recorded identities without dereferencing deleted objects. Allocation reuse can make the old entry alias a new robot in either build. One observed Release run left an entry with no live replacement; that observation does not exclude address reuse in other Release runs. Current/scratch selection being empty does not establish retirement of this separate group list. A later unpaused `PumpGroups` removes inactive groups, but input-side group consumers can dereference entries before that tick. This lifetime defect requires separate correction and regression coverage; retaining the bad reference is not an accepted compatibility contract.

## Remaining O6 work

The registered cases cover successful reload of robot dynamics on synthetic terrain. They do not cover a missing/corrupt reload through the real enclosing error/cleanup boundary, complete `CGame::Init`/DLL lifetimes, saved-versus-effective settings, or reloading a populated building/map-object/effect-spawner catalog. The old battle's queued base and smoke establish deletion paths, not reconstruction of those asset families. Native dialog rendering and interactive input remain separate validation. The group-retirement defect also prevents declaring the O6 lifetime contract complete.
