# Result callbacks through the game loop

`game.loop.menu_result_exit` and `game.loop.diagnostic_result_exit` connect O5's result callbacks to actual `L3GRun` termination. They reuse the synthetic hint/dialog fixture from the [outcome cases](85-battle-outcomes.md), the real window procedure and game form, and the shared scripted clock/message source from the [loop contracts](85-loop-scheduler.md). No production implementation or platform seam changes.

## Run

Run the focused cases in Debug and Release, with cheats disabled and enabled:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.loop\.(menu_result_exit|diagnostic_result_exit)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.loop\.(menu_result_exit|diagnostic_result_exit)$'
.\tools\test.ps1 -Configuration Debug -Cheats -Filter '^game\.loop\.(menu_result_exit|diagnostic_result_exit)$'
.\tools\test.ps1 -Configuration Release -Cheats -Filter '^game\.loop\.(menu_result_exit|diagnostic_result_exit)$'
```

Repeat each command with `-Trace` to exercise the compiled diagnostic result branches. Normal builds check their absence. Trace uses the helper's separate trace build directory; cheats selection reconfigures each ordinary or trace directory. Rebuild without `-Cheats` before treating an output directory as a normal playable candidate.

## Selected routes

Each invocation uses seed 85, both RNG modes and scripted 10/16 ms intervals, with an independent fresh-session replay. The two cases execute 16 and 40 fresh journeys respectively. Two robots begin a production context Move and advance through 25 actual form ticks. The fixture deselects them directly before typed words so ordinary shortcuts have no selected group; their existing Move intent remains.

| Input route | Dialog behavior and `g_ExitState` | Control |
| --- | --- | --- |
| Escape, E, Enter | Menu Exit confirmation, statistics and Enter request loop exit with `g_ExitState = 1` | No WM_CLOSE fallback is supplied |
| Escape, S, Enter | Surrender confirmation, statistics and Enter request loop exit with `g_ExitState = 4`; opposing score time changes sign | Actors remain alive while the result is presented |
| Typed IAMLOOSER | Cheats-enabled builds open Win with `g_ExitState = 3`, then statistics and loop exit | Disabled builds open no result and continue movement |
| Typed STATS | Cheats-enabled builds open diagnostic statistics; Enter dismisses them and the original Move continues | Disabled builds open no diagnostic dialog |
| Generic Shift+Escape | Open the ordinary menu, cancel it and resume movement | This remains the native-key meaning in trace builds too |
| Synthetic left/right Shift+Escape | Trace builds open Lose/Win with `g_ExitState = 2/3`, then statistics and loop exit | Normal builds open the ordinary menu and resume after cancellation |

Side-specific Shift codes are supplied explicitly as scripted window messages. Actual Windows keyboard delivery supplies generic Shift, as documented in [diagnostic result input](85-trace-results.md). These assertions do not make the side-specific shortcuts reachable from a physical keyboard.

## Observations

Terminal routes deliver 29 form ticks/draw boundaries: 25 moving ticks and four paused dialog/statistics ticks. The final confirmation sets `GFLAG_EXITLOOP`, clears the dialog and returns from `L3GRun` with its normal result 1 before another tick/draw. The terminal script has no close message that could hide a missing exit. Menu/statistics cancellation controls deliver 55 ticks, including three paused ticks where applicable, then WM_CLOSE; disabled cheat words deliver 55 active ticks. Script bounds reject missing termination. Map time, RNG draws, robot positions/orders/destinations and the captured final state must remain unchanged from terminal confirmation through loop return. Cancellation controls must resume the retained Move and make further physical progress.

IAMLOOSER and synthetic trace results retain their measured diagnostic contract: `MMFLAG_WIN` stays clear, the player's side stays active and normal outcome-time finalization does not occur. Distinct score times are supplied at the paused result boundary and remain unchanged through terminal callbacks, except the opposing sign change on Surrender. Active `CMatrixSideUnit::LogicTakt` updates score time from `timeGetTime()`, independently of the scripted loop clock; those live wall-clock values are excluded from replay equality. Robot positions and orders use the shared raw-bit snapshots. Rendering is replaced only at the draw boundary by CPU camera/state observation.

A local negative control makes the scheduler ignore `GFLAG_EXITLOOP` while still accepting WM_CLOSE. The terminal cases then reject the forbidden post-confirmation draw. The production branch is restored before rebuilding and rerunning passing cases.

## Limits

The cases establish real loop termination and synthetic fixture teardown, including Debug allocation checks. They retain the standalone selector-disabled callback fallback; native-enabled statistics-to-selector routing and parent/child relaunch remain O7's separate cases. They do not initialize a complete game, execute game-owned shutdown, present native GPU frames, deliver physical keyboard input or accept a historical reference. Those initialization/cleanup/native obligations and the whole-baseline gate remain separate.
