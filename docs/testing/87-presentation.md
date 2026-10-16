# Presentation integration evidence

Presentation coverage has two parts: executable state/event contracts and observations from the native game. The former can verify command routing and resource ownership; only the latter can establish what a player sees and hears. Neither part establishes the other. This document maps PRES-01 through PRES-04 in the [behavior catalog](../research/75-behavior-baseline/README.md). Complete-baseline acceptance requires evidence at the agreed integration revision.

## Automated checks

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(presentation|input|selection|display|device|font|audio|settings|resources|paths)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(presentation|input|selection|display|device|font|audio|settings|resources|paths)\.'
```

The new `game.presentation.*` cases link real `CIFaceButton`, `CSound` and standalone sound services. The existing `CommandWorld` supplies map/configuration lifetime, and the existing fake audio device receives decoded synthetic WAVs. No game package, window, driver or audible playback participates. Most button cases begin in explicitly declared focus states. The separate `textured_button_hit_testing` case uses real interface geometry and texture-alpha picking with synthetic pixels; it does not draw the button or use native window messages.

| Case suffix | Asserted contract |
| --- | --- |
| `push_sounds` | Ordinary, construction and cancel button presses invoke the action after entering the pressed state, then route the appropriate sound. Release invokes the release action and restores focus. In both RNG modes, one audible-path press consumes exactly two raw draws; release consumes none |
| `preset_matching` | A check/push button whose name starts with `conf` selects `bclick`; an internal occurrence or no occurrence selects `preset_click`. Repeated press while unfocused does not repeat the action or sound |
| `disabled` | Disabled buttons consume the mouse-down event without invoking actions or sounds; ordinary unfocused buttons reject it. Neither consumes RNG draws |
| `optional_audio` | Without an audio backend, press/release actions and button state still work, with zero sound draws in both RNG modes |
| `battle_acknowledgements` | Actual context move/attack commands produce chassis and attack acknowledgements; a direct replacement on the same logic group selects an in-progress variant. New order voices retire the previous voice once. Both RNG modes preserve the explicit draw counts and robots advance through real map ticks |
| `silent_battle_commands` | A missing audio backend preserves move/attack orders and actual movement. Chassis-variant choices still consume one raw draw per move, while volume/pan draws are absent |
| `missing_speech` | With an active backend and effects mapping but no speech mapping, moves still execute, missing speech reports once, effects remain playable, and audio-path RNG draws remain present |
| `positional_loop` | Camera-relative playback pans left/right; reusing a live handle updates the same voice, distance attenuates it to zero, and stopping retires it once. Positional updates consume no RNG draws |
| `positional_attenuation_boundaries` | Raw frontend and standalone voice volume/pan at full-volume, attenuated and inaudible distances; see Positional audio boundaries below |
| `positional_loop_reentry` | Inaudible creation, live-loop silence/re-entry, stale handles, cached PCM and single destruction; see Positional audio boundaries below |
| `interface_layer` | The next interface click interrupts the previous interface voice, and clearing sound retires the remaining voice exactly once |
| `textured_button_hit_testing` | A translated interface panel and an atlas-offset button use production geometry and pixel reads. Transparent RGB rejects hover and a focused press; a transparent release dispatches no action and leaves the button pressed (an observation); alpha 1 and 255 accept hover. Interface mouse dispatch produces focus/press/release, leaving the right/bottom edges resets focus, disabled clicks invoke no action, and hidden controls reject input |

The textured-button case uses a 4 by 4 ARGB texture uploaded through `CTextureManaged` to the shared CPU texture fixture. It contains no artwork from the game. `CInterface::OnMouseMove`, `OnMouseLBDown` and `OnMouseLBUp` perform picking and dispatch; no focus or pressed state is supplied for the successful click. The only directly supplied interaction state is the disabled flag for that rejection check. Tooltip construction and rendered overlap/layer ordering remain outside this case.

The click-name behavior corresponds to inventory item D5. It is an observable legacy choice, not a claim that the name matching was intended. Tests preserve it without changing the condition. Seed 87 is explicit for draw-count comparisons; configuration and WAV data are synthetic and supplied by the case. The six battle-audio cases use the existing battle session with a synthetic mesh/map and non-rendering device, seed 87 and 10 ms ticks. They record backend events rather than audible output or private package contents. Context commands rebuild the selected logic group, so a repeated context move uses the chassis acknowledgement again; direct replacement on an existing group uses the in-progress variant. Missing-backend and missing-mapping profiles have different draw counts and must remain separate reference profiles. Other cases assert discrete routing/lifetime behavior without a battle schedule. Checks are active in Release, and tracked-allocation checks run where instrumentation is enabled.

## Camera input through the game form

Three `game.presentation.camera_*` cases send mouse and keyboard input through `CFormMatrixGame` with the synthetic battle fixture. They update the production camera with `Takt(0)` and `BeforeDraw`, without advancing battle time or submitting rendered frames. The strategy-camera defaults, 1280 by 720 viewport, seed 87 and both RNG modes define the profile. The fixture's cursor surrogate records position without loading cursor artwork or claiming OS capture behavior.

`camera_wheel_limits` measures camera-to-link-point distance from the resulting inverse view matrix. One positive wheel step moves from 250 to 209.5 world units; the opposite step restores 250. Repeated steps saturate at 115 and 790, further steps cannot cross either limit, and zero wheel input at the unclamped distance of 250 changes nothing. The reset key preserves wheel distance. Distance checks allow 0.001 world units for matrix reconstruction roundoff.

`camera_drag_release` verifies middle-button ownership, cursor-delta rotation (a 20-pixel move adds 0.2 radians of yaw and sets pitch to 46 degrees), both configured pitch endpoints, release without further rotation, and keyboard restoration of the original orientation. The configured pitch parameter runs from 60 degrees at its zero endpoint to 20 degrees at its one endpoint, so downward pointer movement increases the resulting angle. Wheel and drag/reset routing must consume no RNG draws.

`camera_input_interruption` repeats a drag across deactivation, explicit input reset and pointer leave. Each event cancels mouse-camera mode; deactivation/reset also clear the held button, while pointer leave retains its key state until release. A subsequent move and late release must not rotate the camera. A fresh middle-button press must restore normal rotation. These are form-handler and camera-state contracts; native focus delivery, pointer confinement, image continuity, camera scrolling and rendered picking remain required evidence.

## Interface focus and manual firing

`game.presentation.manual_fire_interface_focus` delivers pointer movement and button transitions through `CFormMatrixGame`, then advances complete production map ticks. A generated 4x4 texture provides opaque and transparent texels for a visible, always-on-top interface button. The normal interface pass performs geometry and alpha hit testing and selects the focused interface/element; the fixture never assigns `m_InFocus` or the focused element directly. The always-on-top setting keeps this independent synthetic panel visible through `CInterface::Init`, as required for repeated-frame input delivery.

A press over an opaque texel puts the button into its pressed state and remains held without creating a fire order or damaging the target. Moving onto a transparent texel in the same element bounds releases interface focus and allows firing at the supplied world target. Moving back over an opaque texel while holding fire retains the existing fire order and continues damage. This is the observed distinction: interface focus blocks starting fire, but does not cancel firing already in progress. Releasing the button over the interface stops firing; leaving the interface without pressing again does not restart it. Direct-control identity persists until the explicit Enter exit.

The case uses a real laser-equipped robot and damage receiver, both RNG modes with seed 87, separate 10/16 ms schedules and unavailable audio. Assertions cover focus/element identity, held input, fire-order transitions, actual health changes and stable health after release. World picking remains a supplied trace; the test does not render pixels, deliver OS focus events or prove native pointer alignment. This is shared PRES-01/CMD-05 hover/fire integration evidence. Settings-window input consumption and the remaining presentation baseline retain their separate requirements.

## Settings input, pause and persistence

`game.presentation.settings_input_pause_cancel` opens the production battle menu with Escape, then opens `SettingsWindow` through C or a click on the menu entry. Both entry paths run independently under ParkMiller and LegacyCRT with seed 87 and 10 ms map ticks. A moving robot assigned to control group 1 supplies an observable battle state. Opening the normal menu clears selection; pressing 1 while settings are open must not recall the assigned group. A separate, explicitly supplied stress precondition then reselects one robot while the modal window remains open, making the remaining command checks actionable. M cannot enter order mode, Ctrl+2 cannot reassign that robot's control group, and the typed `NEED4SPEED` sequence cannot change the speed flag. Pause cannot resume the battle from inside the dialog. Repeated production map ticks preserve simulation time, robot position and its existing order. After Cancel and a second Escape to leave the battle menu, simulation time and movement resume. The real group-1 recall restores both assigned robots, then the same Move and group-assignment inputs take effect. The cheat sequence toggles speed only in cheats-enabled builds, with an unchanged flag required in cheats-disabled builds. The enabled profile is essential to establish cheat interception.

A mouse change to camera speed produces a pending 125% label without changing the effective 100% speed. Cancel returns to the paused battle menu, discards the change and creates no settings file. `game.presentation.settings_apply_save_failure` additionally navigates the selected row with Left/Right, changes volume and a restart-only skybox choice, then attempts Apply against an existing read-only settings file. Save rejection must retain the open, paused dialog, produce the error text, preserve the previous file byte for byte, remove the temporary file and leave runtime settings unchanged. Removing the read-only attribute and applying again saves the pending values, updates camera speed to 125%, retains the running skybox and records the new skybox as restart-pending. Loading the saved file must reproduce the applied settings. Apply returns to the paused menu; the following Escape resumes the battle.

The executable links the production form, settings window/model, hint composition, map/menu lifecycle, runtime settings and file persistence. A test-only `IDirect3D9` adapter enumerates exactly 640x480 and 1280x720; all other adapter operations abort. The existing CPU device stores the composed hint textures and rejects drawing. A link-time `Text::Render` surrogate supplies text-sized opaque rectangles and records the requested text; it does not draw glyphs or call D3DX. The menu template, button-hole bitmap and empty hint-button panel are synthetic. Apply and Cancel use the real Enter/Escape handlers; this fixture does not test artwork-dependent OK/Cancel button picking. Initial selection and the context-command world trace are supplied by the battle fixture; the additional modal re-selection is a declared stress precondition, rather than normal menu behavior. No native window, packages or audio device are used.

Sensitivity checks in Debug and Release with cheats enabled reject three independent changes: allowing M past modal keyboard interception, feeding the cheat parser before settings consumes input, and applying settings after a failed save. These temporary mutations are not part of the source tree.

These contracts establish automated input isolation and the listed pause/save/apply/cancel consequences. They do not establish native focus delivery, glyph rendering, localized/small-window layout, display reset/rollback from the settings dialog, audible volume changes or the full settings baseline. Those requirements remain in P4 and its native procedure.

## Catalog coverage and gaps

The [completion checklist](baseline-completion.md) defines the bounded scenarios referenced below, credits evidence shared with other suites, and separates implementation from final reference/native acceptance. Its proposed scenario selection requires review against the behavior catalog.

| Family | Available executable evidence | Evidence still required |
| --- | --- | --- |
| PRES-01: input, viewport, focus and reset | Existing `game.input.*`, `game.selection.*`, `game.display.*`, `game.device.*`, `game.font.*` exercise dispatch/state/cancellation/reset callbacks. Camera wheel, drag/reset and interruption contracts exercise the actual form handlers and camera matrices; `game.presentation.manual_fire_interface_focus` adds real form/interface alpha hit testing, UI press delivery, manual fire suppression/start/continuation and release. Command-route scenarios provide additional gameplay evidence when integrated | P1: native input/picking/display/reset evidence. P5 retains D1 correction/disposition; the reproducer is not a passing golden. |
| PRES-02: scene, text, effects and audio | New button/action/audio contracts and real command-to-audio routes, positional attenuation boundaries, loop silence/re-entry, stale handles and missing-speech behavior; existing audio decoding, bridge, startup/default-map, lifetime and volume tests; settings shadow-pass checks | P2: actual rendered scene/shadows/text/minimap and positional/looping speech/effects across separate optional-audio profiles. |
| PRES-03: startup and resources | Existing resource/path/startup/package contracts and audio catalog checks | P3: fresh archive/package/configuration/path/exit matrix and CalcVis output provenance. P5 retains D7 correction/disposition. |
| PRES-04: settings | Existing settings parsing, persistence, model, runtime and display checks; production menu/form/window contracts for Move/group/cheat interception, paused/resumed simulation, pending edits, Cancel, Apply, failed-save preservation/retry and effective versus saved restart-only settings | P4: settings-driven display rollback integration and native step 5, including actual button picking, text, small-window/localized layout and audibility. Adapter enumeration and text rasterization are explicit presentation surrogates in the new executable; no production test seam is added. |

The presentation implementation issue owns missing automated contracts, fixtures and exact native procedures/setup. Native execution and reuse of earlier records are tracked in the [validation ledger](native-validation.md) under #191. The implementation issue may close after those implementation deliverables and explicit case handoffs are complete; the complete baseline still requires reviewed native results and its other acceptance evidence.

## Positional audio boundaries

`positional_attenuation_boundaries` uses the existing looping laser mapping with attenuation 0.5 (the frontend scales it to 0.001), volume endpoints 0/1 and pan endpoints -1/1. It checks raw frontend `CalcPanVol` outputs before the standalone service clamp, then actual standalone voice parameters at the camera center and on both sides at distances 199, 200, 201, 700, 1,199, 1,199.9, 1,200, 1,201 and 2,000. Frontend volume must be exactly 1 at distances 0 and 199, and exactly 0 beyond 1,200; the other samples and pan allow 0.00001 for floating-point rounding. At nominal distance 1,200, camera-relative coordinate rounding can leave a tiny positive raw volume (observed 2^-24), which must still be rejected by playback. Volume stays at one through 200, then matches 0.999, 0.5, 0.001 and 0.0001 at the intermediate samples. The 1,199.9 sample must still create a voice, tightening the rejection-threshold check. At and beyond 1,200 a new sound is rejected without allocating a voice. Center pan is zero and lateral samples pan fully to the corresponding side. Each created voice is stopped and destroyed once; the PCM clip loads once and positional calls consume no RNG draws.

`positional_loop_reentry` distinguishes creation from updates. An initially distant sound creates no voice and loads no PCM; bringing its empty handle to the center creates a loop. Moving that active loop out of range makes it silent but retains its handle and voice. Returning it on the opposite side restores attenuated volume and pan without allocation. After an explicit stop, ChangePos rejects the stale handle; Play with that handle creates a different voice using the cached clip. Clear destroys each remaining voice exactly once. Both cases run under both RNG modes with seed 87, without advancing simulation or opening an audio device. They extend PRES-02 routing/lifetime evidence; actual audibility, native spatial perception, camera-driven automatic sound updates and full resource-backed scenes still need separate validation.

## Opt-in defect reproducers

`matrixgame_presentation_defect_probes` is built with the test executables but is deliberately not registered in CTest. Its three probes assert the geometrically correct results and currently return exit code 1 on the reference implementation. They document catalog candidates D1 and D7; they are not accepted baseline goldens or passing regression coverage. After a separately reviewed correction, their assertions can become regular CTest contracts.

After running the test build, invoke each command separately, then repeat with `mingw-release-exe`:

```powershell
& .\build\mingw-debug-exe\tests\matrixgame_presentation_defect_probes.exe probe.presentation.clear_rectangle
& .\build\mingw-debug-exe\tests\matrixgame_presentation_defect_probes.exe probe.presentation.plane_normal
& .\build\mingw-debug-exe\tests\matrixgame_presentation_defect_probes.exe probe.presentation.plane_points
```

D1 is assigned to #89 for reproduction and #87 for correction/disposition by the [legacy inventory](../research/88-legacy-code-inventory/README.md); #63 also identifies this coordinate error. A real interface element at (100, 200) with clear rectangle (3, 11, 20, 30) submits (103, 203, 120, 230); its top should be 211. `CIFaceElement::BeforeRender` uses the left offset for the vertical start. A minimal texture object acknowledges `PreLoad` only, and rejects other device operations; the production coordinate calculation and clear-rectangle buffer are unchanged. No rendered artifact is inferred from this component result.

D7 is assigned to #89 for reproduction and #97 for correction/disposition by the legacy inventory. Both `SPlane` builders correctly populate `dxplane`, while distance/intersection methods read separate `norm`/`dist` fields. The probes initialize those separate fields explicitly, then build the plane z = 2 and query a ray from z = 10 directed downward. They obtain distance 0 and no intersection instead of distance magnitude 8 and intersection parameter 8. Initialization avoids treating an uninitialized read as a golden value. The test establishes the mathematical inconsistency; it does not establish the visibility output or native impact of the map-argument `CalcVis` path.

Both defects still require a separate correction/disposition before accepting the affected baseline entries. The default test suite does not hide them with inverted assertions or a known-failure success status.

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

1. Start windowed at 1280×720 with both audio packages and no loose `CFG/sounds.txt`. In the startup menu choose New battle, select the scenario map and bonus explicitly, and start it. Record `Map`, `BattleBonus` and the effective difficulty multipliers; use None unless the scenario specifies another bonus. Confirm map, robot/building geometry, text, minimap and introductory controls. Dismiss the introduction, select a friendly robot, press M and click nearby open terrain. Verify the selection/move acknowledgement, visible order and arrival. Trigger a reachable attack and observe projectiles, damage and battle audio.
2. Drag a selection rectangle quickly for ten seconds, release, then give a move order. Repeat in the opposite drag direction and with an immediate release. Start another drag, press 1 while holding the mouse button, then release; the canceled rectangle must not add units. Follow the exact [selection procedure](../PLAYTESTING.md#selection-dragging) for expected membership and exit checks.
3. While scrolling with an arrow key, switch away without first releasing it. Return and release all controls; the camera must remain stopped until a fresh input. Repeat during a selection drag and middle-button camera rotation. Cursor release and any mode-dependent minimization must be recorded. Compare camera position before/after return; a minimized window alone cannot demonstrate stopped movement.
4. Press Alt+Enter twice, both during normal play and with the introduction or battle menu open. Verify viewport, cursor/picking alignment, selection, text and continued commands. Repeat the [display procedure](../DISPLAY.md) for borderless/exclusive startup and toggling at supported adapter resolutions. Do not select an exclusive resolution absent from that adapter's modes.
5. Perform the [settings-window procedure](../SETTINGS.md#verification): open by menu click and C, navigate by keyboard/mouse, Cancel, Apply volume/camera/shadow changes, restart-only options and failed-save handling. While the window is open, press M and a group-number key, then cancel and resume: neither key must queue a gameplay order or change the selected group. Record paused unit positions before and after. Recheck at 640×480 and the available English/Russian configuration; unavailable localization remains a gap.
6. Inspect terrain, robot/building shadows, effects, text and minimap before and after display transitions. Change the two shadow switches independently in settings, including restoring shadows on objects already present. Select/move robots and observe a continuing battle to check interface/effects/voice routing and interrupted loops. Listening establishes audible output; passing the synthetic audio tests does not.
7. Quit through the menu and record exit code 0. Relaunch and close with Alt+F4, again expecting 0. Repeat the startup/package cases below in fresh processes, restoring output copies between cases.

For package coverage, repeat the [optional-audio matrix](../PLAYTESTING.md#prepare-the-configuration-cases) with neither package, `sound.pkg` only, `voices.pkg` only and both. Startup must succeed with required `robots.pkg`; missing optional categories remain silent. UI and battle commands must remain usable. No-audio and audio profiles need their own reference records because their RNG draw counts differ. With `DATA/robots.pkg` absent, expect the resource-placement dialog and exit code 1; do not continue into a battle. Use the documented invalid-configuration, space/Unicode-path and map-argument procedures as separate records, retaining diagnostics without publishing assets. An unexpected crash is a failure requiring investigation, not baseline behavior.

## Evidence record

For every native case record: source revision and binary SHA-256; toolchain and build/options including cheats/assertions; map/configuration identity, effective difficulty and RNG mode; package presence and private hashes; packed/text mode; resolution, display mode, adapter and Windows scaling; steps completed, observations, exit code and relevant diagnostics. Screenshots may support visual claims but cannot replace command/state observations. Keep copyrighted packages, extracted assets and private configuration out of public evidence.

Required native profiles: Debug / Packed, Debug / Text, Release / Packed and Release / Text. Record execution status in the contribution evidence.

Earlier playtests from other changes require an applicability check against the selected revision/profile. Record reused, pending, passed and failed cases separately in the [validation ledger](native-validation.md). Native execution may be deferred independently of completed implementation; before accepting the complete baseline, attach the required reviewed records and explicitly dispose of every missing or failing case above.
