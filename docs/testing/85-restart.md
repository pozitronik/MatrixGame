# Storage-backed battle restart tests

The `game.restart.movement`, `game.restart.pending_win`, `game.restart.pending_loss` and `game.restart.retired_group` cases in [battle_restart_scenarios.cpp](../../tests/battle_restart_scenarios.cpp) exercise the existing battle-menu Restart command. They supplement O6 in the [outcome completion plan](85-outcome-completion.md); they do not initialize another standalone process or enter a second DLL battle.

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
| `game.restart.retired_group` | Create two distinct player robots and select each after movement without an intervening tick. Require two nonempty inactive groups at menu entry and an empty group list immediately after Restart. Select a friendly factory through form mouse input and actual mesh picking before the first resumed simulation tick; fresh robot commands must still work afterward. |

All four cases check that the map instance, terrain allocation and simulation time survive the reload. They check fresh robot UIDs and initial positions, reset statistics, both sides' resource balances and active status, the introduction's pause ownership, empty current/scratch selection, unchanged RNG mode/draw count and no exit request. A live smoke effect is installed while confirmation is paused; Restart clears it and disconnects its handler. The movement case also checks disposal of the old build queue through the normal owning building destruction and Debug allocation accounting.

Reload does not reset a held arrow key. The tests record that retained input and release it explicitly before further simulation. The cases print the surviving status-check schedule: the recorded second pending-result trigger is 4030 ms in the 10 ms profile and 4048 ms in the 16 ms profile. Those elapsed trigger times are observations in the output, not exact timing assertions. The pending-result countdown itself is asserted zero immediately after Restart, before dismissing the introduction; the subsequent 2.2-second observation protects against a stale result appearing. These observations describe a reload of an existing map, not a fresh `CGame::Init` lifetime.

The pending-result assertions were checked with a temporary negative control that omitted the production countdown reset in `Restart`: both pending-result cases failed when the old dialog appeared after reload. The production reset was restored and the tests rebuilt before final validation.

## Retired selection-group regression

`game.restart.retired_group` is a registered regression. Run it separately when changing selection ownership or map teardown:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.retired_group$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.retired_group$'
```

Before the correction, an inactive selection group could retain the address of a deleted robot across Restart. Allocation reuse can make the old entry alias a new robot in either build. One observed Release run left an entry with no live replacement; that observation does not guarantee nonreuse in other runs. Current/scratch selection being empty did not establish retirement of this separate group list. `Restart` now clears current/scratch selection and removes inactive groups before deleting the old dynamics, using the existing selection cleanup operations.

The regression creates two distinct player actors before each reload, completes movement, then creates two one-member selections without advancing simulation between them. After Escape both groups must remain nonempty and inactive; immediately after Restart the group list must be empty. Recorded identities remain diagnostic data and are never dereferenced after retirement. It then supplies a synthetic friendly factory as a visible input target and performs real mouse selection before any resumed simulation tick. Production `GroupsUnselectSoft` runs through the building-selection branch. The fixture supplies visibility order; `InRect`, mesh projection, form dispatch and selection remain production behavior. The target is fixture setup, not a claim that building assets have been reloaded from the map file.

A negative control without the cleanup fails the safe identity assertion. Bypassing only that assertion also reproduces a Release segmentation fault in the form-driven building-selection sequence. The corrected case runs twice in each RNG/timestep profile and then accepts fresh movement.

## Related O6 fixtures

The registered cases cover successful reload of robot dynamics on synthetic terrain. The additional [saved-settings case](85-restart-settings.md) checks immediate camera speed and pending skybox/RNG options across two map reloads. The [Restart-after-toggles case](85-loop-restart.md) performs the reloads through the real loop, checks four-speed scheduling, retained KEEPALIVE and held-key state, and resumes movement after each introduction; the [focus continuation](85-loop-restart.md#focus-after-restart) exercises KEEPALIVE input reset, inactive waits and resumed scheduling after the reloads. The [populated object/spawner case](85-restart-population.md) separately checks reconstruction of map objects and smoke/fire spawners. The [restart-failure guide](85-restart-failures.md) describes the cleanup control and the registered missing-file and malformed-storage regressions. These CPU storage-device cases do not reconstruct a populated building catalog: the old battle's queued base establishes deletion only. The separate [opt-in native building/cannon cases](85-native-population-restart.md) supply that selected O6 path through actual Restart with a borrowed Direct3D device, including restored ownership/resources, old queue retirement and stored slot occupancy. Their DLL loop is ParkMiller/time-seeded, with bounded native timing; it is not a fixed 10/16 ms or both-RNG replay. Complete initialization/reentry is documented in the [synthetic host guide](85-native-sessions.md); startup application of saved options and executable relaunch retain O7 ownership. Native dialog appearance, original-host execution and integrated baseline acceptance remain separate. Native DLL/building/cannon cases require `MATRIXGAME_NATIVE_SESSION_TESTS=ON`, and executable lifecycle cases require `MATRIXGAME_NATIVE_EXE_TESTS=ON`; both default to OFF and default CI does not execute them. Closure requires the combined selected evidence, not expanding any one fixture's claims.
