# Diagnostic result shortcuts

`game.outcome.trace_result_callbacks` covers O5's diagnostic result continuation through the production form and hint callbacks. It is registered only with `MATRIXGAME_TRACE=ON`. Left Shift+Escape requests defeat (result 2); right Shift+Escape requests victory (result 3). These legacy shortcuts are independent of the `CHEATS_ON` parser and remain absent from normal builds.

## Run

```powershell
.\tools\test.ps1 -Configuration Debug -Trace -Filter '^game\.outcome\.trace_result_callbacks$'
.\tools\test.ps1 -Configuration Release -Trace -Filter '^game\.outcome\.trace_result_callbacks$'
```

Omit the filter to run the full engine suite in this configuration. `-NoBuild` reuses the corresponding trace directory and rejects a cache without tracing enabled. The build and test helpers support the same option for MinGW/MSVC and EXE/DLL configurations. CI includes MinGW Debug EXE and MSVC Release DLL trace jobs, alongside the ordinary build matrix.

## Observations

Each result is exercised with both RNG modes and 10/16 ms scheduling. Two robots begin a real Move before the form receives Shift, Escape and key releases. The test checks the selected result dialog, released shortcut keys and pause state, advances ten paused frames, confirms into statistics, and confirms again into the exit request. Both robots retain their positions, groups, orders and destinations while paused. Game time, draw count and both sides' deliberately different score times remain unchanged across the callbacks.

These shortcuts leave the normal win flag unset and the player's side active, even when they open the victory dialog. They do not perform ordinary outcome time finalization. The assertions preserve that measured diagnostic contract separately from normal win/loss rules and IAMLOOSER.

The synthetic hint fixture uses production dialog callbacks without native presentation or the standalone next-map selector. The final assertion observes the exit request and cleared dialog; it does not execute the message loop, prove process termination or establish cleanup/reentry. Those remain O7/O8 work. No original game assets are required.
