# Diagnostic result input

`game.outcome.trace_result_callbacks` covers O5's diagnostic result continuation through the production form and hint callbacks. It is registered only with `MATRIXGAME_TRACE=ON`. The window procedure forwards `WM_KEYDOWN`/`WM_KEYUP` virtual keys unchanged: either physical Shift key arrives as `VK_SHIFT`. Generic Shift+Escape therefore opens the ordinary menu and leaves the result unset. Direct synthetic injection of `VK_LSHIFT` with Escape requests defeat (result 2); `VK_RSHIFT` requests victory (result 3). Those side-specific codes are not produced by the window procedure. Their diagnostic branches are independent of the `CHEATS_ON` parser and remain absent from normal builds. Making these branches reachable from native keyboard input would require a separate behavior decision; this test does not change input translation.

## Run

```powershell
.\tools\test.ps1 -Configuration Debug -Trace -Filter '^game\.outcome\.trace_result_callbacks$'
.\tools\test.ps1 -Configuration Release -Trace -Filter '^game\.outcome\.trace_result_callbacks$'
```

Omit the filter to run the full engine suite in this configuration. `-NoBuild` reuses the corresponding trace directory and rejects a cache without tracing enabled. The build and test helpers support the same option for MinGW/MSVC and EXE/DLL configurations. CI includes MinGW Debug EXE and MSVC Release DLL trace jobs, alongside the ordinary build matrix.

## Observations

The generic-key menu path and both synthetic result paths run with both RNG modes and 10/16 ms scheduling, for twelve profiles. Two robots begin a real Move before the form receives the declared Shift code, Escape and key releases. The generic-key variant requires the menu dialog, no statistics request, an unchanged zero result code, no exit request and ten paused frames; Escape then dismisses the menu without changing robot or score state. The synthetic variants check the selected result dialog, released injected keys and pause state, advance ten paused frames, confirm into statistics, and confirm again into the exit request. Both robots retain their positions, groups, orders and destinations while paused. Game time, draw count and both sides' deliberately different score times remain unchanged across the callbacks.

The synthetically reached result branches leave the normal win flag unset and the player's side active, even when they open the victory dialog. They do not perform ordinary outcome time finalization. The assertions preserve that measured diagnostic contract separately from normal win/loss rules and IAMLOOSER.

The synthetic hint fixture uses production dialog callbacks without native presentation or the standalone next-map selector. The final assertion observes the exit request and cleared dialog; it does not execute the message loop, prove process termination or establish cleanup/reentry. Those remain O7/O8 work. No original game assets are required.
