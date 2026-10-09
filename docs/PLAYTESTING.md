# Standalone playtesting

Use the Windows x86 toolchain and locally supplied resources described in [Windows builds](BUILD_WINDOWS.md). Run the first pass with cheats disabled in both Debug and Release. Automated engine tests establish specific code contracts; a visible battle and usable controls require this separate check.

## Prepare the configuration cases

Build before choosing a configuration case. The build helper refreshes tracked text files and stages a root-level `robots.dat` when supplied, so another build can restore packed configuration into an output directory.

| Case | Files beside the EXE | Purpose |
| --- | --- | --- |
| Packed configuration | `DATA/robots.pkg`, `CFG/robots.dat`, tracked `CFG/robots/` text files | Check the supplied packed configuration and its interface data |
| Text fallback | `DATA/robots.pkg`, tracked `CFG/robots/data.txt` and `iface.txt`; no `CFG/robots.dat` | Check the tracked standalone configuration |
| Missing resources | Temporarily remove only the output copy of `DATA/robots.pkg` | Check the failure diagnostic and cleanup |
| Invalid configuration | Replace only the output copy of `CFG/robots.dat` with a small synthetic invalid file | Check rejection and cleanup |

Change output copies only. Keep supplied files at the repository root unchanged. Rename an output copy to a clearly named backup with a different filename prefix, such as `.playtest-robots.dat`, before a failure/fallback case. Resource lookup also searches `robots.dat.*`, so a suffix-only backup can still be selected. Restore the copy after the game exits and remove any synthetic replacement. Do not extract or publish the resource package. Run one game process at a time.

## Check an interactive battle

Launch `MatrixGame.exe` without arguments. Record the map named by the active configuration. Dismiss the introductory dialog with its confirmation control before testing commands.

1. Confirm map rendering, interface text and the resource display. Record the configured resolution and Windows display scaling; physical client pixels can differ from the configured resolution.
2. Select a robot, issue a move order and verify movement to the selected ground point. Move several robots through a shared route and observe avoidance rather than testing only a single empty route.
3. Issue an attack against a reachable enemy and verify firing, damage and subsequent selection.
4. With a bomb-carrying robot, check detonation through the interface and manual control. Include ground and live-object targets, both while moving and after arrival. Verify the explosion, damage and subsequent selection; repeat developer-only cases separately with cheats enabled.
5. Construct a robot and a turret. Inspect a representative AI construction and head price against the configured resource costs; a head must not be valued using the weapon price table.
6. Observe a flyer reaching its target and repeat a command after completion. Distinguish a range/position defect from a blocked route or unsupported target.
7. Close through the interface, then check the window-close path separately. Confirm process termination and that mouse/keyboard use is restored. Record the process exit status rather than assuming that the absence of a window establishes success.

Check sound, pause/resume, focus transitions, manual control and developer-only paths separately where relevant. Repeat affected cases after a correction. Record any limitation or unperformed step explicitly; do not treat startup, compilation or synthetic tests as proof of successful combat.

## Bomb commands

Use a fresh bomber for each row below and keep the test area away from the base and other units. Start with cheats disabled in Debug and Release. Changing `-Cheats` reuses the same build directory, so finish one option's playtests before rebuilding for the other option.

### Construct a bomber

1. Left-click your main base and press `B` to open the robot constructor.
2. Move the pointer over the hull slot, the large armor icon beside the robot preview, and press `2`. This selects armor kind 1, which supports a mortar/bomb socket in the standard resource set.
3. Move the pointer over the chassis slot below the hull slot and press `1` for pneumatic chassis. Leave the head empty; if a head is installed, hover over its slot and press `0`.
4. Move the pointer over the special weapon slot above and to the right of the hull slot and press `2`. The slot should show the bomb; its weapon label is `бомба` in the tracked Russian interface. This shortcut applies to the special slot, not an ordinary gun slot.
5. Set the build quantity to one and click `Build` (`Построить`). Wait up to 90 seconds for the robot to leave the base, then select it. The bomb-order button should be available. If a required slot is absent, the button is disabled, or construction does not finish, record the blocker and stop rather than substituting another robot.

In a separately built cheats-enabled game, select the base and type `RICHIERICH` before opening the constructor when resources are insufficient. The resource counters should each increase by 9,000. Do not use this preparation in a cheats-disabled run. `SPAWN` creates a laser robot, not a bomber.

### Detonation cases

Use `M`, then left-click a ground point, for a movement order. For UI detonation, hover over the bomb-order button and check its hint (`Взорвать бомбу` in the tracked interface), click the button, then left-click the specified target. Pressing `E` in tactical view selects the same order but does not by itself test the UI button. In manual control, `Enter` enters the selected robot and `E` detonates immediately.

| Case | Actions | Expected result |
| --- | --- | --- |
| UI, stationary, ground | Move the bomber into an open area and wait for it to stop. Click the bomb-order button, then bare terrain about two robot lengths away. | The order is accepted; the bomber approaches if needed and detonates without an exception. |
| UI, moving, ground | Order movement to bare terrain at least ten robot lengths away. While the bomber is moving, click the bomb-order button and left-click a different bare ground point along that route. | The moving robot accepts the replacement bomb order and detonates without an exception. |
| UI, stationary, live object | Stop the bomber in an open area. Click the bomb-order button, then a live robot at least ten robot lengths away. | The command tracks a live object and the bomber detonates when it reaches the existing trigger conditions. |
| UI, moving, live object | Issue the long movement order. While the bomber is moving, click the bomb-order button and then a live robot away from the base. | The replacement command retains its live target and detonates without an exception. |
| Manual, stationary | Select a stopped bomber, press `Enter`, wait two seconds without moving, then press `E`. | Immediate explosion and destruction of the bomber; manual control is released. |
| Manual, moving | Select a bomber, press `Enter`, hold `W`, then press `E` after two seconds while still holding `W`. Release all keys. | Detonation during movement, destruction of the bomber and restored input. |

A friendly robot is a valid live-object command target; record its side and its health before and after the blast. Friendly damage depends on the active configuration, so use an unprotected enemy in blast range to establish damage if friendly damage is disabled. Keep uninvolved units outside the blast area. Allow up to 30 seconds for a command; record an unreachable target as a blocked case rather than a successful detonation.

After each blast, wait five seconds, select another surviving friendly robot and issue a short `M` movement order. Check that the destroyed bomber is no longer selectable, selection and commands still work, and no exception appears. At the end of each run, close the window and record the process exit status; normal close should return zero. Record each row separately, including explosion visibility, target damage and any unperformed check. Repeat the same matrix with cheats enabled after completing the disabled runs.

## Repeated sessions and restart

Use packed configuration and cheats-disabled standalone builds. Run these three launches in Debug, then repeat all three in Release. Dismiss the introductory checkmark after each launch. Select a friendly robot, press `M`, click open ground about ten robot lengths away and wait for movement; this establishes that the battle is running before testing its lifetime. Allow up to 30 seconds for each dialog or restart and record a timeout as a failure rather than waiting indefinitely.

| Launch | Actions | Expected result |
| --- | --- | --- |
| 1: normal close | Move a robot, wait five seconds, then close the window with Alt+F4. | No exception; the process exits with status zero and the desktop cursor can move freely. |
| 2: restart and exit | Move a robot away from its starting point. Press Escape, then `R` to open Restart confirmation. Press Escape to cancel, then Escape again to resume the battle. Open Escape, `R` again and press Enter to confirm. Dismiss the new introduction; verify starting units and positions return, then issue a fresh movement order. Repeat the confirmed restart once more. Finally press Escape, `E`, Enter to confirm exit, and Enter to dismiss statistics. | Cancel leaves the current battle intact. Both confirmed restarts restore the starting situation, clear old selections/orders and leave controls usable. Exit reaches statistics, then terminates with status zero and releases the cursor. |
| 3: surrender and result exit | Move a robot, then press Escape, `S`, Enter to confirm surrender. Inspect statistics and press Enter to close them. | A result/statistics path appears, no exception is shown, the process exits with status zero and cursor confinement ends. |

If a confirmation or statistics dialog does not respond to Enter, click its visible checkmark and record that difference. Check that selecting and commanding a robot after restart does not refer to a removed unit. Record results for each launch and configuration, including any exception and the exit status. Surrender validates a result path; it does not establish natural victory detection or completion of an entire battle.

Use the [missing/invalid-resource cases](#map-arguments-and-failure-cases) to check early failure separately, restoring only the staged output copies afterward. Never rename or change the supplied root-level packages. [Session lifetime](LIFECYCLE.md) distinguishes process-owned state, UI restart and in-process reinitialization.

## Focus and recovery

Use a cheats-disabled windowed standalone build with packed configuration. Complete the following steps in Debug, then repeat in Release. Record the actual client resolution, adapter and driver. The ordinary window releases cursor confinement and minimizes whenever the application loses focus, including Alt+Tab or activating another application's window. Compare camera position before and after rather than trying to watch it while inactive. Camera scrolling uses the arrow keys by default; use the configured camera key if bindings have changed.

1. Dismiss the introduction and inspect resource counters, robot labels and a menu hint for readable text. Select a friendly robot and issue `M` plus a click on nearby open ground.
2. Hold Up Arrow for one second to scroll, Alt+Tab to another application, then release Up Arrow. Leave the game inactive for five seconds. Check that the desktop cursor is unrestricted.
3. Return to the game without holding any key. Within two seconds, check that the scene and UI text return. The camera should stop until a fresh scroll key is pressed. Press and release Up Arrow to verify fresh scrolling; selection and a new movement order should work.
4. Repeat focus loss/regain twice. After each return, open Escape and inspect menu text, then Escape again to resume. Check for a black window, missing text, stale selection, an exception or an unresponsive interface.
5. Close the window and record the process status; normal exit should be zero. A recovery diagnostic must include the failing operation and HRESULT.

These steps cover the exposed windowed focus transition. Border resizing and Alt+Enter mode switching are not implemented. Exclusive fullscreen, display-mode changes, sleep/resume and physical driver loss need separately named scenarios and hardware evidence; do not report the windowed checklist as those tests. See [DirectX device recovery](DEVICE_RECOVERY.md) for automated cases and the explicit native reset check.

## Map arguments and failure cases

A command-line map argument selects visibility calculation rather than the interactive battle loop. A bare filename is prefixed with `Matrix\Map\`; a path containing a backslash is used as supplied. Visibility calculation initializes the game, loads its resources, rebuilds visibility data and records completion in `calcvis.log`. The calculated visibility data remains in memory. This mode requires game resources and does not replace the interactive checklist.

For missing/invalid-resource cases, inspect the diagnostic, dismiss any error dialog, and confirm process termination and restored cursor state. A rejected packed configuration must identify the configuration file; a missing map must identify the failed map path. Text fallback applies when packed configuration is absent. Keep these runs bounded. Record whether failure occurred before or after window/device creation and whether the output resource backups were restored. Preserve the smallest relevant log excerpt and stack; avoid publishing configuration dumps or extracted assets.

## Record the result

Use the source revision, compiler and build configuration, cheats setting, configuration mode, map, resolution/display scaling, steps and observations. Include normal and failure exit statuses. A report should separate verified behavior from remaining checks and describe a reproducible failure with the smallest useful evidence.
