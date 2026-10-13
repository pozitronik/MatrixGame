# Presentation integration evidence

Presentation coverage has two parts: executable state/event contracts and observations from the native game. The former can verify command routing and resource ownership; only the latter can establish what a player sees and hears. Neither part establishes the other. This document maps PRES-01 through PRES-04 in the [behavior catalog](../research/75-behavior-baseline/README.md). The reference source for the new button tests is `a64e9db4595bf06cc456772cb02c70f6a34b6804`; complete-baseline acceptance still requires evidence at the agreed integration revision.

## Automated checks

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(presentation|input|selection|display|device|font|audio|settings|resources|paths)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(presentation|input|selection|display|device|font|audio|settings|resources|paths)\.'
```

The new `game.presentation.*` cases link real `CIFaceButton`, `CSound` and standalone sound services. The existing `CommandWorld` supplies map/configuration lifetime, and the existing fake audio device receives decoded synthetic WAVs. No game package, window, driver or audible playback participates. Buttons begin in explicitly declared focus states; these tests do not substitute a pixel hit test or claim that a rendered button can be clicked.

| Case suffix | Asserted contract |
| --- | --- |
| `push_sounds` | Ordinary, construction and cancel button presses invoke the action after entering the pressed state, then route the appropriate sound. Release invokes the release action and restores focus. In both RNG modes, one audible-path press consumes exactly two raw draws; release consumes none |
| `preset_matching` | A check/push button whose name starts with `conf` selects `bclick`; an internal occurrence or no occurrence selects `preset_click`. Repeated press while unfocused does not repeat the action or sound |
| `disabled` | Disabled buttons consume the mouse-down event without invoking actions or sounds; ordinary unfocused buttons reject it. Neither consumes RNG draws |
| `optional_audio` | Without an audio backend, press/release actions and button state still work, with zero sound draws in both RNG modes |
| `interface_layer` | The next interface click interrupts the previous interface voice, and clearing sound retires the remaining voice exactly once |

The click-name behavior corresponds to inventory item D5. It is an observable legacy choice, not a claim that the name matching was intended. Tests preserve it without changing the condition. Seed 87 is explicit for draw-count comparisons; configuration and WAV data are synthetic and supplied by the case. Other cases assert discrete routing/lifetime behavior without a battle schedule. Checks are active in Release, and tracked-allocation checks run where instrumentation is enabled.

## Catalog coverage and gaps

| Family | Available executable evidence | Evidence still required |
| --- | --- | --- |
| PRES-01: input, viewport, focus and reset | Existing `game.input.*`, `game.selection.*`, `game.display.*`, `game.device.*`, `game.font.*` exercise dispatch/state/cancellation/reset callbacks. Command-route scenarios provide additional gameplay evidence when integrated | Native picking and visual selection at each resolution/mode, camera movement, cursor release, rendered reset recovery and driver failure behavior. The ClearRect vertical-start candidate D1 still needs a separate reproduction/disposition; it is not an approved golden |
| PRES-02: scene, text, effects and audio | New button/action/audio contracts; existing audio decoding, bridge, startup/default-map, lifetime and volume tests; settings shadow-pass checks | Actual terrain/robots/buildings/effects, independent shadows, text/minimap, positional/looping sounds and speech. Native no-audio/effects-only/speech-only/both profiles have different RNG inputs and must be recorded separately |
| PRES-03: startup and resources | Existing resource/path/startup/package contracts and audio catalog checks | Fresh ZIP launch, required/optional package combinations, Unicode/space paths, invalid configuration, error dialogs and exit codes. Map-argument `CalcVis` has candidate D7; record reproduction and output provenance without blessing visibility data as a golden |
| PRES-04: settings | Existing settings parsing, persistence, model, runtime and display checks | Actual menu/window key consumption, focus, pause, Apply/Cancel, failed save, display rollback and restart-only settings through the rendered form. `SettingsWindow::open` needs adapter enumeration, hint/font rendering and real menu lifecycle; a model-only test cannot prove that route |

Each outstanding entry remains part of the presentation leaf. This document and passing button tests do not satisfy its native acceptance criteria or authorize structural refactoring.

## Native preparation

Use the pinned build helper, then follow [configuration preparation](../PLAYTESTING.md#prepare-the-configuration-cases). Test Debug/Packed, Debug/Text, Release/Packed and Release/Text separately with cheats disabled. Build before removing the output copy of `CFG/robots.dat` for text mode: rebuilding stages it again. Rename backups with a different filename prefix, since suffix-only backups can still be discovered. Leave all root-level supplied packages unchanged and untracked.

For each build, launch from its own executable directory and capture the exit status. For example, after preparing Debug's configuration and package copies:

```powershell
$playtestDirectory = Join-Path (Get-Location) 'build/mingw-debug-exe/MatrixGame'
$playtestExecutable = Join-Path $playtestDirectory 'MatrixGame.exe'
Get-FileHash -Algorithm SHA256 -LiteralPath $playtestExecutable
$playtestProcess = Start-Process -FilePath $playtestExecutable -WorkingDirectory $playtestDirectory -PassThru -Wait
$playtestProcess.Refresh()
$playtestProcess.ExitCode
```

For Release, replace `mingw-debug-exe` with `mingw-release-exe`. Run only one game at a time. This command intentionally opens the interactive game; it is a manual procedure, not part of CTest.

## Native procedures

Run the following in each of the four configuration cases. Use the arrow keys for strategic camera scrolling; W is a direct-control key. Record any unavailable scene/target as unperformed rather than passing it.

1. Start windowed at 1280×720 with both audio packages and no loose `CFG/sounds.txt`. Confirm map, robot/building geometry, text, minimap and introductory controls. Dismiss the introduction, select a friendly robot, press M and click nearby open terrain. Verify the selection/move acknowledgement, visible order and arrival. Trigger a reachable attack and observe projectiles, damage and battle audio.
2. Drag a selection rectangle quickly for ten seconds, release, then give a move order. Repeat in the opposite drag direction and with an immediate release. Start another drag, press 1 while holding the mouse button, then release; the canceled rectangle must not add units. Follow the exact [selection procedure](../PLAYTESTING.md#selection-dragging) for expected membership and exit checks.
3. While scrolling with an arrow key, switch away without first releasing it. Return and release all controls; the camera must remain stopped until a fresh input. Repeat during a selection drag and middle-button camera rotation. Cursor release and any mode-dependent minimization must be recorded. Compare camera position before/after return; a minimized window alone cannot demonstrate stopped movement.
4. Press Alt+Enter twice, both during normal play and with the introduction or battle menu open. Verify viewport, cursor/picking alignment, selection, text and continued commands. Repeat the [display procedure](../DISPLAY.md) for borderless/exclusive startup and toggling at supported adapter resolutions. Do not select an exclusive resolution absent from that adapter's modes.
5. Perform the [settings-window procedure](../SETTINGS.md#verification): open by menu click and C, navigate by keyboard/mouse, Cancel, Apply volume/camera/shadow changes, restart-only options and failed-save handling. While the window is open, press M and a group-number key, then cancel and resume: neither key must queue a gameplay order or change the selected group. Record paused unit positions before and after. Recheck at 640×480 and the available English/Russian configuration; unavailable localization remains a gap.
6. Inspect terrain, robot/building shadows, effects, text and minimap before and after display transitions. Change the two shadow switches independently in settings, including restoring shadows on objects already present. Select/move robots and observe a continuing battle to check interface/effects/voice routing and interrupted loops. Listening establishes audible output; passing the synthetic audio tests does not.
7. Quit through the menu and record exit code 0. Relaunch and close with Alt+F4, again expecting 0. Repeat the startup/package cases below in fresh processes, restoring output copies between cases.

For package coverage, repeat the [optional-audio matrix](../PLAYTESTING.md#prepare-the-configuration-cases) with neither package, `sound.pkg` only, `voices.pkg` only and both. Startup must succeed with required `robots.pkg`; missing optional categories remain silent. UI and battle commands must remain usable. No-audio and audio profiles need their own reference records because their RNG draw counts differ. With `DATA/robots.pkg` absent, expect the resource-placement dialog and exit code 1; do not continue into a battle. Use the documented invalid-configuration, space/Unicode-path and map-argument procedures as separate records, retaining diagnostics without publishing assets. An unexpected crash is a failure requiring investigation, not baseline behavior.

## Evidence record

For every native case record: source revision and binary SHA-256; toolchain and build/options including cheats/assertions; map/configuration identity, effective difficulty and RNG mode; package presence and private hashes; packed/text mode; resolution, display mode, adapter and Windows scaling; steps completed, observations, exit code and relevant diagnostics. Screenshots may support visual claims but cannot replace command/state observations. Keep copyrighted packages, extracted assets and private configuration out of public evidence.

| Required native profile | Status for this contribution |
| --- | --- |
| Debug / Packed | Not run |
| Debug / Text | Not run |
| Release / Packed | Not run |
| Release / Text | Not run |

Earlier playtests from other changes do not automatically establish these cases at the final integration revision. Before accepting the presentation baseline, attach the current records and explicitly dispose of every missing or failing case above.
