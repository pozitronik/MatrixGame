# Native ordinary AUTO and demo startup procedures

Procedure version: **1**. This prepares the startup, Escape and following-ordinary-session portions of NV-05-1. It uses the actual filename-based branch in `CGame::Init`, not synthetic assignment of automatic-mode flags. Natural result continuation, controlled reinforcements, changing camera fights and mixed special-object/Terron scenes remain separately identified below. Native execution is pending.

## Build and map input

Follow the [outcome build/profile procedure](85-native-outcomes.md#build-and-preserve-the-candidate). Record source revision, compiler/configuration, binary hash, cheats setting, Text/Packed, RNG and audio profile. Use an explicitly supplied owned ordinary map with friendly robots and an opposing army, such as the extracted original `desert2_2e.cmap`, with its shared resources installed. Do not use a demo, Terron or special-object map as the ordinary control. The map must be playable under its ordinary alias before comparing the demo alias.

From the repository root:

```powershell
$source = Read-Host 'Full path to your owned ordinary desert2_2e.cmap'
.\tools\build.ps1 -Configuration Release
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -AlternateMapFile $source
```

Inspection must report the expected candidate and source-map SHA-256 without changing files or opening the game. The helper stages a copy only with `-Launch`. Its ordinary alias is `Matrix/Map/native-outcome-alternate.cmap`; `-AlternateMapMode Demo` instead stages **the identical payload** as `Matrix/Map/native-outcome-demo.cmap`. The latter name deliberately contains `demo`. `CGame::Init` lowercases the loaded map path and enables `AUTOMATIC_MODE`, `FULLAUTO` and `FLYCAM` when it finds that substring. No special demo payload format or replacement game code is involved.

The alias has no original metadata/preview match; its filename and the No preview placeholder are expected. This comparison controls the payload and changes the startup name. It does not claim that the ordinary map's troop positions, reinforcement conditions or fights become a controlled demo benchmark merely because it is renamed.

Commands below use Release/Text/ParkMiller. Repeat the applicable Debug, Packed and LegacyCRT profiles by changing the corresponding build/helper options, as specified by the native ledger. Do not rebuild while a run or its retained backup is active. Finish and restore one launch before beginning another. Source maps and packages remain unchanged. Explicit recovery with `-Restore` reads the recorded alias mode, so an abandoned demo run can be recovered with the ordinary default parameters; preserve any independent later settings edits before that recovery.

## D0: cheats-disabled ordinary control

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -AlternateMapFile $source -WatchRelaunches -ExpectedProcesses 1 -Launch
```

Within 60 seconds choose startup New battle, **native-outcome-alternate**, bonus None and Start. Expect the normal introduction within 60 seconds. Dismiss the introduction, select a friendly robot, press `M` and click reachable ground. Confirm movement within two seconds and permit up to 30 seconds to reach the destination. If the declared source cannot produce this ordinary playable scene, stop as a setup failure.

Deselect robots, leave the developer console closed, move the pointer to bare terrain away from the screen edges, release all mouse buttons/keys and type `AUTO` using an English keyboard layout. Wait five seconds with the game focused. Cheats are disabled: the Automatic mode indicator must not appear and the tactical camera must not enter a cinematic flight. Press Escape once: the normal battle menu must appear rather than terminating the process. Use Escape to resume; issue another fresh Move, then close through Escape, `E`, Enter, statistics, Enter. Require one observed exit code 0 and restoration within 30 seconds. After exit, confirm the remembered-map path in the latest `test.log` startup section; do not depend on buffered log output becoming available during play.

## D1: ordinary AUTO on and off

After D0 has restored its files, rebuild with enabled codes:

```powershell
.\tools\build.ps1 -Configuration Release -Cheats
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats On -ConfigurationMode Text -AlternateMapFile $source -WatchRelaunches -ExpectedProcesses 1 -Launch
```

Choose the same ordinary alias with None and verify the normal introduction and a fresh Move as in D0. Deselect robots and type `AUTO` over bare terrain with the console closed. Within five focused seconds the **Automatic mode** indicator should appear. This route enables automatic side logic but does not set full-auto exit or the flying-camera flag. Observe the battle for ten seconds without commanding units. Record any visible robot decisions, but do not require a particular destination, reinforcement or kill from this uncontrolled source map.

Press Escape: the ordinary battle menu must still open. Escape again resumes. With the console closed and no units selected, type `AUTO` a second time. Keep the game focused and unpaused for five seconds; the indicator must disappear. The debug-info entry can persist for its three-second lifetime after the flag is cleared, so immediate disappearance is not required. Select a friendly robot, issue a fresh Move and verify movement. Exit through menu Exit/statistics. Require one code-0 process and restoration within 30 seconds.

This checks the real code-entry gate, visible toggle and retained ordinary menu route. It does not prove a particular interrupted AI order, high-level side decision or RNG draw sequence; the controlled A8 fixtures own those logic observations.

## D2: demo startup, flying camera and Escape

First use a fresh cheats-disabled build and the demo alias:

```powershell
.\tools\build.ps1 -Configuration Release
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -AlternateMapFile $source -AlternateMapMode Demo -WatchRelaunches -ExpectedProcesses 1 -Launch
```

At startup New battle select **native-outcome-demo**, bonus None and Start. Verify that the source-map hash is identical to D0/D1. After exit, confirm `native-outcome-demo.cmap` in the run's latest startup log section. Within 60 seconds the battlefield must appear **without the introduction dialog**. Do not dismiss a checkmark automatically: an introduction here is a failure of the selected startup path. Automatic mode should be visible even with developer codes disabled.

Keep the game focused, the pointer away from edges and all inputs released. Observe up to 30 seconds for the camera to move or turn on its own. Record this as visible flying-camera activity only; a moving camera does not identify the selected fight or prove that the fight target changed. If no activity is observable within that interval, record the camera observation as incomplete instead of inventing a selected target. If the battle ends before the Escape step, record that unplanned result and repeat with a source that permits the bounded startup observation; it is not a controlled natural-result case.

Press Escape once while the battle is still running. The process must finish within 30 seconds with code **0**, without an ordinary battle menu, statistics selector or child process. The internal full-auto route sets `g_ExitState=1` and requests loop termination; that internal game result is not the Windows process exit code. Require one observed process, code 0 and configuration/map-copy restoration.

Repeat D2 on a freshly rebuilt `-Cheats` candidate with `-Cheats On`, without typing any developer code. Demo startup is selected by the map path in either build. Do not type `IAMLOOSER`: it directly opens a victory dialog and does not exercise the natural full-auto result scheduler.

## D3: ordinary launch after each demo run

After each D2 launch restores its files, run the matching current build with `-AlternateMapFile $source` and **omit `-AlternateMapMode Demo`**. Keep the same cheats/profile options as that immediately preceding demo. Choose **native-outcome-alternate**, None and Start. The ordinary introduction must return; after dismissing it, there must be no Automatic mode indicator or autonomous cinematic camera flight. Verify arrow-key camera scrolling, select a friendly robot and issue a fresh Move. Escape must now open the normal battle menu. Close via Exit/statistics and require one observed code-0 exit and restoration.

D3 verifies a fresh standalone process after full-auto exit. It is not an in-process restart, host DLL reentry or a natural result-to-next-map transition. The helper restores original settings between these launches deliberately; do not claim a persistent saved demo preference was exercised.

## Remaining scene preparation

| Selected observation | Available input | Required addition |
| --- | --- | --- |
| Ordinary AUTO gate/toggle versus actual demo-name startup, Escape and following ordinary process | D0-D3 with an explicitly owned ordinary map; no asset distribution | Native execution and applicability review |
| Eligible versus ineligible reinforcement after AUTO | [Loaded-demo AI continuations](85-native-demo-ai.md) load declared factory/transport scenes and check actual requests and side actions | Export a complete playable scene/configuration with declared friendly/enemy robots, eligible versus enemy-owned factory, maintenance settings and bounded request observation; an arbitrary original map does not guarantee these inputs |
| Flying camera changes its selected fight during a transition | D2 can observe general camera motion; the [loaded-demo camera journey](85-native-demo-ai.md#changing-fights) checks supplied-hit pair ownership, expiry and replacement | Two controlled fights and a scheduled retirement/replacement, plus an observable selected-target transition; camera movement alone is insufficient |
| Natural demo victory and defeat, then ordinary session | Production full-auto result routing, demo-mode DLL continuations and the shared [native outcome map writer](../../tests/native_outcome_assets.cpp) | A complete standalone export with exact surviving-side inputs and bounded automatic outcomes, including the expected internal result versus process exit; reuse the shared declarations and avoid a human race or developer-dialog substitution |
| NV-06-4 mixed Break/Anim and Terron with collateral blast | The [versioned special-scene exporter and procedures](85-native-special-scenes.md) provide complete playable inputs, ordering, collateral expectations, Text/Packed isolation and following-battle steps; the [contained outcome checks](85-native-special-outcomes.md) cover loaded result/next-session logic | Visible execution and applicability review in native validation; no additional exporter is required for this family |

The loaded-demo materializers already supply terrain, navigation, robots, factory/transport resources and controlled component-camera inputs for their native DLL checks. Reuse those declarations when exporting complete standalone AUTO/demo scenes; saving dynamic entity tables alone does not supply their configuration and rendering dependencies. The special-object family already has its own exporter and versioned procedure. Any further export must declare exact paths, object identities, trigger schedule, hashes, cleanup and profile applicability. No opaque extracted asset or production seam is added by this procedure.

Record procedure version, D0-D3 case/profile, source and binary hashes, ordinary/demo loaded paths, introduction presence, Automatic mode indicator timing, camera observations, Escape behavior, fresh Move, every process exit and restoration. Helper tests verify staging and recovery only; they do not execute these native observations. NV-05-1 and NV-06-4 remain incomplete as whole cases.
