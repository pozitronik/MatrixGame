# Controlled standalone demo scenes

Procedure version: **NV-05-1 v3**. These synthetic exports prepare the visible ordinary/AUTO, filename-driven demo, reinforcement, changing-fight and natural-result observations. Generation is automated; running the exported standalone game is a separate operator action. A generated scene or a passing hidden DLL test is not a recorded visual pass. Use the [native validation ledger](native-validation.md#per-case-record) for each executed variant.

## Build and export

Use the pinned Windows tools and a reviewed x86 candidate. The dedicated console exporter is available in both EXE and DLL test configurations. It never initializes a game or creates a graphics device; without arguments it prints usage and exits with code 2. `--export <profile> <new-directory> <Text|Packed>` generates only files. The six supported profiles are `ordinary`, `reinforcement`, `no-reinforcement`, `win`, `loss` and `fights`.

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Release -WithoutResources -Jobs 2
if ($LASTEXITCODE -ne 0) { throw 'Candidate build failed.' }
.\tools\setup-directx-runtime.ps1
if ($LASTEXITCODE -ne 0) { throw 'Runtime preparation failed.' }
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination '.\build\mingw-release-exe\tests'
.\tools\test.ps1 -Configuration Release -NoBuild -Jobs 1 -Filter '^game\.session\.demo_export_'
if ($LASTEXITCODE -ne 0) { throw 'Export regression checks failed.' }
$writer = (Resolve-Path '.\build\mingw-release-exe\tests\matrixgame_native_demo_export.exe').Path
$game = (Resolve-Path '.\build\mingw-release-exe\MatrixGame\MatrixGame.exe').Path
$runtime = Split-Path -Parent $game
$root = Join-Path (Get-Location) 'build/native-demo-release-text'
if (Test-Path -LiteralPath $root) { throw 'Choose a new export root.' }
$null = New-Item -Path $root -ItemType Directory
foreach ($profile in @('ordinary', 'reinforcement', 'no-reinforcement', 'win', 'loss', 'fights')) {
    $target = Join-Path $root $profile
    & $writer --export $profile $target Text
    if ($LASTEXITCODE -ne 0) { throw "Export failed: $profile" }
    Copy-Item -LiteralPath $game -Destination (Join-Path $target 'MatrixGame.exe')
    Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File | Copy-Item -Destination $target
    Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination $target
}
}
```

The destination's parent must already exist. Existing destinations are refused, including empty directories; invalid profiles/modes and missing parents fail without creating an export. Before retaining output, the writer validates declared map records, settings, configuration and package parsing, checks that the shared image, fixture map and package exist and are nonempty, and checks that reinforcement mesh files exist. Image and mesh decoding belongs to the separate loaded-scene fixtures. A generation/validation failure removes only its newly created output and restores the working directory. Windows Unicode paths and spaces are supported. Do not merge output into an installation or retry a failed export over an existing directory.

For Debug, replace `Release`/`release` with `Debug`/`debug` throughout. For Packed, choose another new root and pass `Packed` instead of `Text`. Packing uses the production block-storage format for both configuration trees and parks the loose tree in `parked-text-config`; `CFG/robots/data.txt` and `iface.txt` must be absent. Do not copy CFG, DATA or Matrix directories from the ordinary build. Only its executable and runtime DLLs belong in the export. The [Windows requirements](../BUILD_WINDOWS.md) still apply.

Each export contains generated terrain/navigation, robot meshes, gray interface images, configuration, one installed `.cmap` and an independently encoded empty `DATA/robots.pkg` accepted by the package parser. It uses no original package, extracted artwork or optional audio. `synthetic.map` is supporting fixture data outside the installed map directory. Generated art is deliberately simple; visual fidelity and audible playback require their separate resource profiles.

The default standalone profile is Windowed 1280×720, ParkMiller RNG, bonus None, shadows and skybox disabled. The selected map is saved in both `CFG/standalone.txt` and `Config/Map`. For LegacyCRT, change only `RandomGenerator=ParkMiller` to `RandomGenerator=LegacyCRT` in an independent export's `CFG/standalone.txt`. Keep Debug/Release, Text/Packed and RNG observations separate. Filename-driven demo startup needs no cheat; the ordinary AUTO comparison additionally needs matching cheat-disabled/enabled builds, identified by their actual build configuration and binary hashes. Restore the ordinary cheat-disabled build afterwards.

Record source/binary/input identities before running a profile:

```powershell
. {
$ErrorActionPreference = 'Stop'
git rev-parse HEAD
Get-FileHash -Algorithm SHA256 -LiteralPath $game, $writer
Get-ChildItem -LiteralPath $root -Recurse -File |
    Where-Object { $_.Extension -eq '.cmap' -or $_.Name -in @('standalone.txt', 'robots.dat', 'data.txt', 'iface.txt') } |
    Get-FileHash -Algorithm SHA256
}
```

## Protect the exported scene before launch

Use a new record for each profile and follow [shared settings/cache protection](85-native-outcomes.md#protect-each-record-before-launch), with `$game` set to the absolute profile directory and `$exe` to its copied executable. The unchanged generated maps omit `DATA_UNIQID`: the exact key is `ordinary.cmap.0` for ordinary, or `demo-<profile>.cmap.0` for the other five declared profiles. Set `$cacheNames` to that one known key, then use the common known-key block to protect it at both Documents and fallback roots. If map/resource records changed, perform the protected Debug qualification before treating any suffix as known. No unprotected first run is allowed.

Run this input block first, selecting the intended profile, then the linked shared path/settings/cache blocks and log capture. Keep its values unchanged for that record:

```powershell
. {
$ErrorActionPreference = 'Stop'
$profile = 'ordinary' # Select one of the six exported profiles before preparation.
$game = (Resolve-Path -LiteralPath (Join-Path $root $profile) -ErrorAction Stop).Path
$exe = (Resolve-Path -LiteralPath (Join-Path $game 'MatrixGame.exe') -ErrorAction Stop).Path
$configuration = 'Release' # Match the actual copied executable.
$cacheNames = @(if ($profile -eq 'ordinary') { 'ordinary.cmap.0' } else { 'demo-' + $profile + '.cmap.0' })
}
```

Run the shared settings preparation and [log-boundary capture](85-native-outcomes.md#capture-the-log-boundary) before launching. Keep the verified record until all processes are gone, the [completion/evidence guard](85-native-outcomes.md#restoration-and-evidence) passes, and shared cache/settings restoration and guarded cleanup complete. Do not delete an export that still owns parked originals or an unresolved snapshot. No cache implementation is copied into this guide.

For a changed map without a reviewed key, the direct launch also permits the prepared Debug qualification route. Load only that intended map and quit normally without a result/relaunch journey, then run completion and shared qualification; repeat separately for each changed key before ordinary records.

## Launch, deadlines and exits

Choose the profile, then explicitly launch its copied executable without arguments. Positional map arguments enter visibility calculation rather than this gameplay path.

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($launchAttempted) { throw 'One launch attempt per prepared record; resolve and restore it before preparing another.' }
$target = (Resolve-Path -LiteralPath (Join-Path $root $profile) -ErrorAction Stop).Path
if ($game -ine $target -or -not $settingsReady -or ($cacheReady -eq $cacheQualificationReady) -or -not $logBoundaryReady) { throw 'Prepare this exact profile record, cache key and log boundary first.' }
if ($cacheQualificationReady -and $configuration -ne 'Debug') { throw 'Qualification requires the matching Debug export.' }
if ($process -and -not $process.HasExited) { throw 'The preceding retained process is still running.' }
if (@(Get-Process -ErrorAction Stop | Where-Object ProcessName -eq 'MatrixGame').Count) { throw 'Close all MatrixGame processes first.' }
$currentGame = (Resolve-Path -LiteralPath $game -ErrorAction Stop).Path
if ([IO.Path]::GetFullPath($exe) -ine (Join-Path $currentGame 'MatrixGame.exe')) { throw 'The executable does not belong to the protected game.' }
$fallbackRoots = @($cacheRoots | Where-Object Name -eq 'Fallback')
if ($fallbackRoots.Count -ne 1 -or $fallbackRoots[0].Path -ine (Join-Path $currentGame 'Cache')) { throw 'Run shared path setup for this game before preparing its cache.' }
if ($settings -ine (Join-Path $currentGame 'CFG/standalone.txt') -or $caseBackup -ine (Join-Path $currentGame '.playtest-presentation-backup')) { throw 'The settings record belongs to another game.' }
$boundaryPath = Join-Path $caseBackup 'log-boundary.json'
if (-not $caseBackupOwned -or -not $logBoundaryReady -or -not (Test-Path -LiteralPath $boundaryPath -PathType Leaf) -or $logPath -ine (Join-Path $currentGame 'test.log') -or (Get-Item -LiteralPath $caseBackup -ErrorAction Stop).CreationTimeUtc -ne $logBoundaryOwner) { throw 'The current owned record log boundary is missing or stale.' }
if ((Get-Item -LiteralPath $boundaryPath -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Do not follow a boundary-record link.' }
$boundaryRecord = [IO.File]::ReadAllText($boundaryPath, [Text.Encoding]::UTF8) | ConvertFrom-Json -ErrorAction Stop
if ($boundaryRecord.Record -ine $caseBackup -or $boundaryRecord.OwnerUtc -cne $logBoundaryOwner.ToString('o') -or $boundaryRecord.Executable -ine $exe -or $boundaryRecord.Log -ine $logPath -or $boundaryRecord.Hash -cne $recordedExeHash -or $boundaryRecord.Length -ne $logBeforeLength -or $boundaryRecord.OriginalHash -cne $logHashBefore -or (Get-FileHash -LiteralPath $exe -Algorithm SHA256 -ErrorAction Stop).Hash -cne $recordedExeHash) { throw 'The captured log boundary does not belong to this record and candidate.' }
$launchMode = 'DirectExport'
$newLog = $null
$newLogProcessId = $null
$gameExitCode = $null
$restorationEligible = $false
$caseExitEvidenceMatches = $false
$process = $null
$launchSucceeded = $false
$launchAttempted = $true
$process = Start-Process -FilePath $exe -WorkingDirectory $game -PassThru -ErrorAction Stop
if ($null -eq $process) { throw 'No retained game handle; preserve the record.' }
$null = $process.Handle
$retainedProcessStart = $process.StartTime.ToUniversalTime()
}
```

Require the startup menu within 60 seconds. Choose New battle, select the only installed map and Start with bonus None; require the battlefield within 60 seconds. Use the basename, actual battlefield and only this launch's appended log excerpt to identify the scene; Release has no Debug-only startup-section delimiter. Keep the game focused at normal speed, release all keys and keep the pointer away from window edges while observing autonomous behavior. Profile deadlines start when its battlefield appears. A missing scene, diagnostic, premature exit, wrong outcome or deadline failure must be recorded as a failure. Close the owned game before trying another profile; do not let a failed scene run indefinitely.

After the specified exit, return to PowerShell:

```powershell
. {
$ErrorActionPreference = 'Stop'
if (-not $process.WaitForExit(30000)) { throw 'Owned game is still running; retain all backups.' }
$process.Refresh()
if ($process.StartTime.ToUniversalTime() -ne $retainedProcessStart -or $process.ExitCode -ne 0) { throw 'The retained lifetime/exit differs.' }
$process.ExitCode
}
```

For a natural win/loss, do not press Escape or Alt+F4 to obtain the expected exit. If its 30-second result deadline fails, record the failure, then use Alt+F4 only to clean up. After all game windows close, run the completion/evidence block and the shared restoration blocks before another profile. If the expected-exit check above fails, retain that failed observation and still use completion for verified known-key/settings restoration; restored files never turn it into a passing case. A process code of 0 by itself does not identify victory or defeat; the observed firing, losing side and unit retirement are required. No full-auto case should show Win/Lose/statistics or a next-map selector after its result/Escape exit. A new unexpected game window or error dialog is a failure. This manual observation does not substitute for an automated descendant-process trace.

## NV-05-1D0/D1: ordinary control and AUTO

Use `ordinary`, map `ordinary.cmap`. It contains an unarmed player robot at `(100,100)` and an unarmed opponent at `(500,500)`, with the normal movement configuration and no building. The introduction must appear. Dismiss it with Enter, select the friendly robot, press M and click clear ground about ten robot lengths away. Movement must start within two seconds. Escape must open the ordinary battle menu; cancel it and confirm movement resumes. Close through the menu's Exit and acknowledge statistics; require code 0.

For the AUTO-only comparison, launch a fresh ordinary export. Dismiss the introduction, release every key and type AUTO once at normal speed. In a cheat-enabled build, the player robots enter automatic management; this command alone does not enable the flying camera or full-auto result behavior. Keep the pointer away from edges, observe five seconds, then press Escape: the ordinary menu must open while the process remains alive. Cancel the menu, type AUTO again and issue a new Move; manual movement must work. In a cheat-disabled build, typing AUTO must leave the ordinary camera/menu/manual-command behavior available. Record the compiled gate explicitly; absence of a visible AI decision alone cannot prove that a cheat was rejected. Component [AUTO contracts](84-auto-scenes.md) assert the actual mode flags. Exit normally and require code 0.

## Controlled full-auto profiles

All `demo-*` maps must omit the introduction and start automatic management with the flying camera. Do not type AUTO or give robot orders in these profiles. Map coordinates below are world coordinates, not screen pixels.

| Profile and installed map | Declared scene | Required observation |
| --- | --- | --- |
| `reinforcement`; `demo-reinforcement.cmap` | Two unarmed opposing robots; player-owned titanium factory at `(400,100)`. Maintenance 20,000 ms with a 1% map multiplier, resource intervals 1,000,000 ms. Body-only transport carries an unarmed player robot with 150 displayed health. | Within ten seconds observe at least one carrier with player cargo near the friendly factory. Multiple requests are possible with this intentionally short cooldown; this is not an exact carrier-count assertion. The factory must remain friendly. Once observed, press/release Escape and require code 0 within 30 seconds. Delivery completion is a separate transport contract. |
| `no-reinforcement`; `demo-no-reinforcement.cmap` | Same robots and transport configuration, but the only factory is enemy-owned. | Observe at least five active seconds with no requested carrier or new player cargo; the factory must remain enemy-owned. Autonomous movement toward it is permitted. Press/release Escape and require code 0 within 30 seconds. |
| `win`; `demo-win.cmap` | Player laser robot at `(250,250)`, unarmed enemy at `(350,250)`; laser robot damage 1000 internal points, ordinary chassis/armor and weapon timing. | Without commands observe laser fire, enemy damage and enemy retirement. The battle must exit naturally within 30 seconds with code 0 and no result/statistics/selector dialog. Do not use Escape. |
| `loss`; `demo-loss.cmap` | Same positions and damage declaration, with the laser on the enemy instead. | Observe enemy laser fire and loss of the player robot, followed by the same bounded natural exit with code 0. Do not use Escape. |
| `fights`; `demo-fights.cmap` | Player laser robot at `(250,250)`, unarmed enemy at `(350,250)`, durable enemy at `(400,300)`. Wheel movement and laser heat are zero; laser damage is 1000, Monostack structure 30,000 and Diploid structure 200,000 internal points. | Observe the first fight near midpoint `(300,250)`, retirement of the nearer target and subsequent damage to the durable target. Within 25 seconds the camera must move toward that second fight near `(325,275)` while its target is still alive. Then press/release Escape and require code 0 within 30 seconds. If the selected-fight change cannot be distinguished visually, record it as unresolved; general camera motion alone is insufficient. |

The reinforcement exports alone add 1,000,000 ms capture erase/paint intervals so the visible ownership control remains stable. This is a declared export configuration difference from the hidden helper's shorter observation, not a live timer override or a production rule change. Ordinary and fight exports use independent configurations; never copy a stationary fight configuration over the ordinary control.

The combat profiles use autonomous targeting, real weapon effects, tracing, damage and retirement. No callback kills units or assigns outcome flags. The durable second target gives a bounded observation interval rather than immortality. Leave the controls alone and do not use speed cheats. The ordinary profile following each demo must show its introduction again, accept a fresh Move within two seconds, open its normal menu and exit normally with code 0. Run it as a fresh standalone process using the launch/exit commands above; record both process exits. This checks the independent ordinary installation after a demo, not DLL reentry or in-process Restart.

## Automated boundaries and cleanup

The six `game.session.demo_export_layout_*` cases each validate one profile in Text and Packed formats through production map/configuration/package parsers without initializing a game or graphics device. Together they check all twelve combinations: map identities, actor sides/positions/weapon declarations, factory ownership, maintenance/capture/resource declarations, startup options, explicit editable RNG/window/bonus lines, strict Packed isolation and complete Text/Packed tree equivalence. Each layout case retains the default 30-second timeout. `demo_export_ownership` rejects empty and nonempty existing directories, missing parents and invalid profiles, and deliberately removes a required map before validation; failure must remove only the newly created incomplete export and preserve existing destinations. `demo_export_cli` invokes the actual executable in nonvisible child processes, independently checks its requested configuration format in Unicode/space destinations, proves no-overwrite retention and rejects invalid/missing arguments. Each child has a 15-second deadline; the CLI case has a separate 90-second CTest timeout so its child termination and fixture cleanup can complete. Generation tests do not establish a gameplay observation.

The [loaded-demo AI fixture](85-native-demo-ai.md) separately covers reinforcement and component-fed fight registration under real DLL initialization. Its `game.session.native_demo_natural_win`, `native_demo_natural_loss` and `native_demo_natural_fights` continuations, including their `packed_` variants, observe weapon-driven native outcomes/camera selection and ordinary failure/recovery. The native entry point selects time-seeded ParkMiller; those runs do not establish standalone LegacyCRT or fixed 10/16 ms schedules. Hidden frame/state assertions and human observations retain separate evidence. Unperformed presentation, focus, original-resource/audio and original-host variants remain pending in the ledger; this procedure does not accept the complete behavior baseline or close the outcome package.

Generated resources and saved choices are confined to each new export. After all its owned processes exit, remove that export when its evidence is no longer needed. If export cleanup itself fails, the tool reports the error; retain the exact path for inspection rather than overwriting it. Preserve only agreed source/binary/input hashes, diagnostics and captures. Supplied packages and normal installation settings remain unchanged; generated inputs must not enter the repository or release as original game resources.
