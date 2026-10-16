# Restart while loop settings are active

`game.loop.restart_toggles` combines the production game loop, form keyboard handling and storage-backed Restart. It extends the [loop scheduler contracts](85-loop-scheduler.md) with the [Restart fixture](85-restart.md). No gameplay tick is submitted directly by the test: `L3GRun` delivers each delta to the real form and map.

Run the related contracts in Debug and Release, with cheats disabled and enabled:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Debug -Cheats -Filter '^game\.(loop|restart)\.'
.\tools\test.ps1 -Configuration Release -Cheats -Filter '^game\.(loop|restart)\.'
```

The helper changes the configuration of its ordinary build directory. Rebuild without `-Cheats` before using that directory as a normal playable candidate.

## Sequence and observations

Each run uses seed 85, one of the two supported RNG modes, and a 10 or 16 ms scripted loop interval. It types `NEED4SPEED` and `KEEPALIVE` through the window procedure and real form, issues a fresh Move, then enters Escape, R and Enter to restart the map. Restart is repeated in the same session. After each introduction is dismissed, the newly loaded robot receives another Move. The words are typed again to switch both settings off before `WM_CLOSE` ends the loop. Ordinary shortcuts also consume the letters of each word; fresh orders isolate the following movement interval.

The assertions cover these boundaries:

- Each moving robot has left its initial position before Restart. A rebuilt robot has a new identity and the stored initial position; the retained old actor core is retired. The map and terrain allocation remain in the existing session. Current, scratch and inactive selections are empty, the new player robot starts with Stop, statistics are cleared and all eight side/resource balances are restored from storage.
- Restart retains the simulation clock and both developer settings, opens the paused introduction, and leaves the held Up key set. The test explicitly releases that key after dismissal. The input/reload batch preserves the preceding draw count and RNG mode. Each paused loop iteration consumes neither simulation time nor RNG draws; every active form tick advances the clock by its delivered delta.
- Cheats-enabled builds receive 93 four-speed form deltas followed by five ordinary deltas. Cheats-disabled builds receive ordinary deltas throughout. Two paused iterations produce total map time of 369 or 96 base intervals, respectively. Every one of the 98 iterations has a draw boundary; the final close message produces no additional tick or state change before return.
- Both settings are off at close, the map is unpaused and a rebuilt robot has moved. Two fresh runs must reproduce all per-draw observations of map time, RNG draw count, pause/input/selection state, robot position bits, player order/destination and resources. Process-global object identities are checked for replacement within a run and are excluded from replay comparison.

Movement is tested as displacement, not as a monotonic increase of the x coordinate: a freshly loaded robot can initially turn and move to the side while following its route. This test does not replace navigation's destination and obstacle contracts.

## Negative controls

Local controls reset the developer flags during Restart and reseed the shared RNG. The combined case must reject the first in a cheats-enabled build at the retained-flag assertion and the second at the preserved draw-count assertion. Sources are restored before rebuilding and running the passing related contracts.

## Boundary

The test uses synthetic terrain and serialized dynamics, a controlled loop clock/message source and a draw override that updates only the CPU camera. It does not establish native message delivery, GPU presentation, full `CGame::Init`, game-owned shutdown or clearing held keys between separate sessions. The registered retired-group regression separately exercises form-driven factory selection before the first resumed tick; this combined sequence retains the corrected current Restart fixture and cleanup behavior. Full lifecycle cleanup and native execution retain their separate O7/O10 requirements.
