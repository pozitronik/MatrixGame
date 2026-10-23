# Restart while loop settings are active

`game.loop.restart_toggles` combines the production game loop, form keyboard handling and storage-backed Restart. `game.loop.restart_focus` adds focus loss/return after each reload and after the settings are switched off. They extend the [loop scheduler contracts](85-loop-scheduler.md) with the [Restart fixture](85-restart.md). No gameplay tick is submitted directly by the test: `L3GRun` delivers each delta to the real form and map.

Run the related contracts in Debug and Release, with cheats disabled and enabled:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Debug -Cheats -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Release -Cheats -Filter '^game\.(loop|restart)\.'
```

The helper changes the configuration of its ordinary build directory. Rebuild without `-Cheats` before using that directory as a normal playable candidate.

For a focused check of the combined Restart executable, use `'^game\.(loop\.restart_(toggles|focus)|restart\.(movement|pending_win|pending_loss|retired_group|objects_spawners|saved_settings))$'`. This selects eight cases in an EXE build; `saved_settings` is excluded from DLL registration. The focus case has the `current-desktop` label because its test-only import adapter creates an invisible parent; it does not create a visible game window. The broader filter above also includes scheduler, moving-loop, result-loop and Restart-failure executables; inspect their guides before selecting desktop-dependent or diagnostic failure checks.

## Sequence and observations

Each run uses seed 85, one of the two supported RNG modes, and a 10 or 16 ms scripted loop interval. It types `NEED4SPEED` and `KEEPALIVE` through the window procedure and real form, issues a fresh Move, then enters Escape, R and Enter to restart the map. Restart is repeated in the same session. After each introduction is dismissed, the newly loaded robot receives another Move. The words are typed again to switch both settings off before `WM_CLOSE` ends the loop. Ordinary shortcuts also consume the letters of each word; fresh orders isolate the following movement interval.

The assertions cover these boundaries:

- Each moving robot has left its initial position before Restart. A rebuilt robot has a new identity and the stored initial position; the retained old actor core is retired. The map and terrain allocation remain in the existing session. Current, scratch and inactive selections are empty, the new player robot starts with Stop, statistics are cleared and all eight side/resource balances are restored from storage.
- Restart retains the simulation clock and both developer flags, opens the paused introduction, and leaves the held Up key set. Four-speed's scheduling effect is measured after reload. The original toggle case sends no focus messages and observes KEEPALIVE as a retained flag; the additional focus case below exercises its effect after reload. The test explicitly releases the held key after dismissal. The input/reload batch preserves the preceding draw count and RNG mode. Each paused loop iteration consumes neither simulation time nor RNG draws; every active form tick advances the clock by its delivered delta.
- Cheats-enabled builds receive 93 four-speed form deltas followed by five ordinary deltas. Cheats-disabled builds receive ordinary deltas throughout. Two paused iterations produce total map time of 369 or 96 base intervals, respectively. Every one of the 98 iterations has a draw boundary; the final close message produces no additional tick or state change before return.
- Both settings are off at close, the map is unpaused and a rebuilt robot has moved. Two fresh runs must reproduce all per-draw observations of map time, RNG draw count, pause/input/selection state, robot position bits, player order/destination and resources. Process-global object identities are checked for replacement within a run and are excluded from replay comparison.

Movement is tested as displacement, not as a monotonic increase of the x coordinate: a freshly loaded robot can initially turn and move to the side while following its route. This test does not replace navigation's destination and obstacle contracts.

## Focus after Restart

The additional case reuses the complete two-Restart sequence and its reconstruction assertions. After each new battle has resumed, it holds Up, dispatches `WM_ACTIVATEAPP(FALSE)`, leaves two inactive rounds, then dispatches `WM_ACTIVATEAPP(TRUE)`. A third loss/return occurs after typing both developer words again to switch the settings off. All messages go through the production window procedure and loop platform; camera, input and timing assertions are synthetic rather than OS focus delivery.

In cheats-enabled builds, KEEPALIVE remains active through both reloads. The first two losses deliver exactly one `SYSEV_INPUT_RESET` each, clearing both the held key and the form's key record while keeping application activity and simulation. After the setting is switched off, the last loss delivers `SYSEV_DEACTIVATING`, suspends simulation for two rounds and is followed by `SYSEV_ACTIVATED`. Cheats-disabled builds ignore the developer words, so all three losses take the deactivate/reactivate route and produce six inactive waits. The test checks the exact event sequence and held-key state before and after every event.

At each inactive boundary, the captured simulation time, RNG draws, unit positions, orders, selection and resources must remain identical through both waits. Reactivation must preserve the live robot's Move intent and leave no stuck key. The scheduler retains its previous active timestamp, so its first resumed ordinary tick spans three base intervals; each delivered delta is checked in order. There are 96 tick/draw pairs with cheats enabled and 92 with cheats disabled. The final simulation time remains 369 or 96 base intervals, respectively, because resumption accounts for the inactive elapsed time. Two introductions remain paused, and close must still return without a further tick. Both RNG modes and both schedules repeat the complete sequence and compare every draw observation.

The [test-window containment adapter](native-window-containment.md) is installed before the battle fixture and restored after it. Its strict cursor-import checks apply, and the case explicitly retains and verifies the `ShowCursor` import used by that contract. Desktop cursor clipping, handles and visibility counters are adapted within the test executable; no global hook or physical input is used. A hidden parent is verified invisible. Since the CPU scene has no default-pool presentation resources, its form override skips their release/recreation while retaining production input reset. This does not test native device focus, minimization or visible continuity.

## Negative controls

Local controls reset the developer flags during Restart and reseed the shared RNG. The combined case must reject the first in a cheats-enabled build at the retained-flag assertion and the second at the preserved draw-count assertion. Sources are restored before rebuilding and running the passing related contracts.

For the new focus continuation, omitting KEEPALIVE's input-reset event must fail the cleared-held-key assertion. Omitting ordinary deactivation's active-flag reset must fail the inactive-state assertion after the settings are switched off. Both Release/cheats-enabled mutations are rejected, followed by byte-exact source restoration, actual game/test rebuilding and passing toggle/focus controls. The new case, original toggle, existing speed/focus movement and six Restart controls pass with pinned MinGW Debug/Release standalone cheats disabled/enabled; the matching x86 DLL checks pass with cheats disabled, where saved-settings registration is excluded. This is 58 registered-case executions across six configurations. Standalone builds are restored to cheats disabled after validation; supported MSVC and DLL/cheats-enabled coverage remains for CI.

## Boundary

The test uses synthetic terrain and serialized dynamics, a controlled loop clock/message source and a draw override that updates only the CPU camera. It does not establish native message delivery, GPU presentation, full `CGame::Init`, game-owned shutdown or clearing held keys between separate sessions. The registered retired-group regression separately exercises form-driven factory selection before the first resumed tick; this combined sequence retains the corrected current Restart fixture and cleanup behavior. Full lifecycle cleanup and native execution retain their separate O7/O10 requirements.
