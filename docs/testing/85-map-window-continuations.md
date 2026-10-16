# Battle continuations through the standalone map selector

The `game.selector.*` cases cover the O5/O7 boundary from a running battle or its statistics dialog into the real standalone map window. They enable the production standalone display state and use the real form, dialog callbacks, selector, catalog, settings persistence and relaunch-request logic. They do not start another process. The separate EXE lifecycle fixture must verify cleanup, process creation and the readiness/failure handshake.

## Cases

The synthetic battlefield contains two robots on each side. A normal context Move advances the player robots before opening the menu or triggering an outcome. Win and loss are produced by removing the last robots of the selected side and advancing full map ticks until the natural result dialog appears; this is an elimination trigger, not a combat-damage test. Surrender and Exit use Escape, their menu shortcut and confirmation. Enter confirms the real result and statistics callbacks.

| Route | Asserted continuation |
| --- | --- |
| Menu New battle, Cancel | The selector pauses the battle. Navigation and a changed bonus are discarded on Cancel; the battle menu remains paused. Closing that menu resumes the same Move. No saved value, relaunch request or exit state changes. |
| Menu New battle, accept | Enter saves the selected current or different map and Air Support bonus, preserves unrelated saved options, closes the selector and requests loop exit and relaunch. |
| Win, loss or surrender statistics, Cancel | Confirming statistics opens the selector without exiting the loop. Cancel closes it and requests exit, preserving the result and existing saved values without a relaunch request. |
| Win, loss or surrender statistics, accept | Current-map and different-map choices both save the selected map/bonus and request relaunch. The underlying win/loss/surrender result remains 3/2/4. |
| Menu Exit and statistics | Confirmation requests exit with state 1, never opens a selector and never requests relaunch or saves another battle. |

Every route runs with both supported RNG modes and 10/16 ms map steps. Additional paused map ticks verify that an open selector freezes simulation time and RNG draws. The menu-Cancel case also checks robot position/order retention and resumed movement. Initial camera speed and sound volume differ from defaults so the saved-value assertions detect accidental loss of unrelated settings. The runtime settings of the retiring battle remain unchanged when a new choice is saved for the following process.

The relaunch request intentionally has process lifetime. Each acceptance profile is therefore registered as its own CTest invocation; no test-only reset is added to production. There are 49 registered cases, including a menu-Cancel case that runs all four RNG/step profiles.

## Fixture boundaries

Two synthetic `.cmap` files contain only the property-table schema required by the production catalog's readability check. They are catalog fixtures and cannot initialize a battlefield. Their purpose is to distinguish same-map and different-map selection and persisted paths. Full map loading uses separate complete synthetic resources.

Generated pixels replace hint artwork and text. A test-link font adapter provides bounded, fixed text metrics and rejects native font drawing; the production selector layout, hint composition, input and callbacks still execute. No preview image is supplied. These cases establish no native font quality, preview decoding, GPU presentation, cursor behavior or OS input delivery. The loading-screen code follows its normal hidden/null-window path. No interactive game window is opened.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(selector|outcome)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(selector|outcome)\.'
```

Actual EXE startup, parent/child identities, initialization failure recovery, loading continuity and native close delivery remain separate O7/O10 requirements. These deterministic callback tests do not complete those lifecycle obligations.
