# Mixed-objective native scenes

Procedure version: **NV-06-4 v6**. This handoff supplies synthetic mixed Break/Anim and Terron maps, a resource exporter, a native loading and weapon check and exact standalone outcome steps. Preparation and execution are separate: the hidden native initialization check does not establish the manual attack, collateral, result-dialog or next-battle observations below. Record those observations in the [native validation ledger](native-validation.md#per-case-record).

## What the export contains

`matrixgame_native_special_fixture --export <new-directory>` generates complete loose synthetic resources from the tracked text configuration. It writes an independently encoded empty `DATA/robots.pkg` accepted by the package loader, text configuration, an equivalent packed configuration named `CFG/robots.fixture-packed`, two `.cmap` files and their colored box meshes. No original game package or extracted asset is used. The destination must be a new directory inside an existing parent directory. Export refuses existing destinations, including empty ones, retains output only after layout validation and removes its own incomplete output after failure; it does not modify a normal game installation. Unicode and spaces in destination names are supported.

The shared flat-map generator provides terrain, navigation, two robot meshes and interface resources. The special-scene builder adds actual Static, Break, Anim and Terron descriptors, object coordinates and a mesh with persisted Main, Pain and Death animation names. These animations use synthetic box geometry, not original character animation. Generic interface images remain gray; use the keyboard actions below and identify targets by color. This fixture tests production logic and loading, not the original artwork.

| Map | Targets and units |
| --- | --- |
| `Matrix/Map/mixed-break-anim.cmap` | Red special Break at `(220,300)`, green special Anim at `(420,300)`, blue ordinary Break at `(320,440)`. A laser-equipped friendly robot starts at `(320,100)` and an unarmed friendly control at `(500,500)`. |
| `Matrix/Map/mixed-boss-break.cmap` | Large red Terron at `(320,320)`, green surviving special Break at `(540,100)`, blue ordinary Break at `(100,320)`. The laser robot starts at `(420,160)`, an unarmed collateral victim at `(260,320)` and a distant unarmed control at `(560,560)`. |

Coordinates are engine world coordinates, not screen pixels; the initial camera centers on `(320,320)`. All robots in the special scenes are friendly, so unselected controls stay idle instead of relocating under enemy AI. Keep the near and distant controls unselected. Two special objectives prevent an empty enemy army from causing an early victory. A gray Static marker at `(100,540)` is present in both maps; it is not an objective. Each red, green and blue object starts with 300 internal hit points. The configured wheel chassis and monostack armor total 1,900 internal hit points, displayed as 190. Converted Break objects use gray ruins; Anim retirement changes animation state and removes its objective progress bar without changing its green material.

## Build and export

Use the pinned toolchain. The console exporter is built in both EXE and DLL configurations. For this standalone procedure, build the EXE and export from its matching test output; this command only generates files:

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Release -WithoutResources -Jobs 2
if ($LASTEXITCODE -ne 0) { throw 'Candidate build failed.' }
.\tools\setup-directx-runtime.ps1
if ($LASTEXITCODE -ne 0) { throw 'Runtime preparation failed.' }
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination '.\build\mingw-release-exe\tests'
& '.\build\mingw-release-exe\tests\matrixgame_native_special_fixture.exe' --export '.\build\native-special-release'
if ($LASTEXITCODE -ne 0) { throw 'Special-scene export failed.' }
$fixture = (Resolve-Path '.\build\native-special-release').Path
Copy-Item -LiteralPath '.\build\mingw-release-exe\MatrixGame\MatrixGame.exe' -Destination $fixture
Get-ChildItem -LiteralPath '.\build\mingw-release-exe\MatrixGame' -Filter '*.dll' -File | Copy-Item -Destination $fixture
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination $fixture
}
```

Copy only the executable and its runtime DLLs. Do not copy the normal build's CFG, DATA or Matrix directories: the export already supplies those inputs. The minimal Windows/legacy DirectX requirements remain those in the [build guide](../BUILD_WINDOWS.md). For Debug, replace `Release`/`release` with `Debug`/`debug` throughout and use a different new destination.

Record the source revision, binary SHA-256 and the hashes of both maps and selected configuration. The exported text profile is Windowed 1280×720 with shadows/sky disabled, ParkMiller RNG, default difficulty/bonuses and no optional audio packages. For the LegacyCRT variant, change only `RandomGenerator=ParkMiller` to `RandomGenerator=LegacyCRT` in the exported `CFG/standalone.txt`. The fresh export starts in Text mode. Use a new export for each recorded configuration/RNG profile. For Packed mode, close all processes from that export, then run the following inside its directory; parking both loose trees prevents a silent text fallback. Do not substitute an original `robots.dat`.

```powershell
. {
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $fixture -ErrorAction Stop
try {
if (Test-Path -LiteralPath 'CFG/robots.dat') { throw 'Expected a fresh Text export.' }
if (Test-Path -LiteralPath 'parked-text-config') { throw 'Text configuration is already parked.' }
$packedHash = (Get-FileHash -LiteralPath 'CFG/robots.fixture-packed' -Algorithm SHA256 -ErrorAction Stop).Hash
Copy-Item -LiteralPath 'CFG/robots.fixture-packed' -Destination 'CFG/robots.dat' -ErrorAction Stop
if ((Get-FileHash -LiteralPath 'CFG/robots.dat' -Algorithm SHA256 -ErrorAction Stop).Hash -ne $packedHash) { throw 'Packed copy differs; do not launch.' }
Move-Item -LiteralPath 'CFG/robots' -Destination 'parked-text-config' -ErrorAction Stop
if ((Test-Path -LiteralPath 'CFG/robots/data.txt') -or (Test-Path -LiteralPath 'CFG/robots/iface.txt')) { throw 'Packed configuration is not isolated.' }
} finally { Pop-Location }
}
```

Text and Packed inputs are generated from the same trees; the automated layout check compares every parameter and block in both. Use a fresh validated Text export for a later Text record rather than manually deleting/moving configuration back inside a reused or partial Packed record. A conversion error is an incomplete setup: preserve that export, do not launch it and reconcile it only after any active external cache/settings record is restored and all game processes have exited. Standalone options remain outside the Packed archive.

Record reproducible input identities after choosing the configuration mode:

```powershell
. {
$ErrorActionPreference = 'Stop'
git rev-parse HEAD
$inputs = @('MatrixGame.exe', 'CFG/standalone.txt', 'Matrix/Map/mixed-break-anim.cmap', 'Matrix/Map/mixed-boss-break.cmap')
if (Test-Path -LiteralPath 'CFG/robots.dat') { $inputs += 'CFG/robots.dat' }
else { $inputs += @('CFG/robots/data.txt', 'CFG/robots/iface.txt') }
$inputs | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $fixture $_) -Algorithm SHA256 }
}
```

The integrated post-conversion Terron correction and its registered `game.outcome.terron_blast_after_conversion` regression remain prerequisites for interpreting a collateral result. A crash is a failure, not an accepted objective outcome.

## Preserve and restore the minimap cache

The exporter itself does not load a game or write Documents. The standalone executable does: its production `PathToOutputFiles` creates or uses `Documents/SpaceRangersHD`, shared with the original game. Loading these generated maps reads or writes `Cache/mixed-break-anim.cmap.0` and `Cache/mixed-boss-break.cmap.0` there, because neither map declares a nonzero `DATA_UNIQID`. A cache left by an older export can therefore supply an obsolete minimap. Screenshots also use the Documents output directory. These side effects are separate from the synthetic files in the export.

Before the first launch, close every game sharing the output roots and prepare a new owned record using [the shared settings/cache protection](85-native-outcomes.md#protect-each-record-before-launch). Set `$game` to the absolute `$fixture` directory, `$exe` to its copied `MatrixGame.exe` and `$configuration` to the actual build. Use the common path/settings blocks unchanged; their verified backup remains active until all processes and cache/settings restoration are complete. Do not move or delete whole cache trees.

These two unchanged generated maps omit `DATA_UNIQID`; their reviewed exact keys are known without an initial game run. Run this input block first, then the shared path/settings and known-key protection blocks. Supply both keys because the result route loads the other map in a replacement process:

```powershell
. {
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $fixture -ErrorAction Stop).Path
$exe = (Resolve-Path -LiteralPath (Join-Path $game 'MatrixGame.exe') -ErrorAction Stop).Path
$configuration = 'Release' # Use Debug only with the matching built export.
$cacheNames = @('mixed-break-anim.cmap.0', 'mixed-boss-break.cmap.0')
}
```

Run the shared [known-key parking/restoration](87-native-procedures.md#preserve-the-selected-minimap-cache) blocks with this array: they protect each key at both Documents and fallback roots and verify each original's presence, hash and attributes before replacement. If map records/resources changed, qualify their actual keys with the [protected Debug procedure](87-cache-qualification.md#copy-the-pre-launch-state) first; a guessed `.0` is not acceptable for changed input. Preserve the map hashes and qualified key records. A failed/partial preparation keeps its active backup and cannot authorize launch or cleanup.

Immediately before the launch, capture [this record's log boundary](85-native-outcomes.md#capture-the-log-boundary). After all retained processes exit, use the [completion/evidence guard](85-native-outcomes.md#restoration-and-evidence), shared cache restoration, shared settings restoration and guarded record cleanup, in that order. Never delete a cache entry merely because an old backup directory exists. The guard refuses live games or a stale/replaced record. Retain screenshots separately; shared cache directories and unrelated entries remain untouched.

For a changed map without reviewed keys, qualify each map separately with the matching Debug export and the prepared qualification route. Load only the intended map and quit normally without selecting the other one; completion and shared qualification must finish before preparing another record. The two-map A/B/C journey below requires both confirmed keys and the known-key route.

## Launch and observe exits

Launch the exported game without positional arguments. A positional map path selects visibility calculation in `WinMain`, so it cannot be used for these interactive battles:

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($launchAttempted) { throw 'One launch attempt per prepared record; resolve and restore it before preparing another.' }
if (-not $settingsReady -or ($cacheReady -eq $cacheQualificationReady) -or -not $logBoundaryReady) { throw 'Prepare this record, both keys and log boundary first.' }
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
$initial = $process
}
```

Within 60 seconds the startup menu must appear. Choose New battle, select `mixed-break-anim` for cases A/B or `mixed-boss-break` for C, keep bonus None and press Start. Require the battlefield and introduction within 60 seconds of Start. After exit, confirm the selected map using only the appended log excerpt and its actual battlefield, following the shared evidence guard. Release lacks the Debug-only startup delimiter; a saved preference alone does not identify a successful load. Dismiss the introduction with Enter and release the key. Select only the laser robot above the colored objectives; it is the unit with a weapon attached. Press Enter to enter direct robot control, point at the colored target and hold the left mouse button to fire. Release the button as soon as the target retires. Both colored objectives are inside the laser's configured 250-unit range from the starting position. This procedure uses direct control; tactical right-click/A orders on these synthetic map objects remain unverified. The optional native check below exercises that direct-control route; its execution needs a separate record. Press Enter again to leave direct control before issuing M and clicking ground to move. Use arrow keys to move the camera outside direct control. Escape opens the battle menu; R then Enter restarts the same map and restores all starting objects. None of these steps needs cheats.

Allow at most 30 seconds of aimed firing to retire each selected objective or start Terron's destruction sequence. A target that does not take damage, a wrong target or a timeout is a failed observation; do not continue indefinitely. Allow 30 seconds for a menu, confirmation, restart or normal exit, and 60 seconds for a replacement battlefield. Preserve the shorter result/countdown deadlines in A/B/C below. On failure, record the diagnostic and process state, then close the owned game before restoring or removing its export.

After the requested victory, press and release Enter to acknowledge the result and do the same when the statistics dialog appears. The standalone map-selection dialog must open within 30 seconds. Select the other exported map by its basename, keep None and press Enter. When the loading interval is observable, its screen must cover the transition until the new battle appears; record an unobservable fast interval as incomplete presentation evidence. Require the replacement battlefield within 60 seconds, dismiss its introduction and give its laser robot a short Move. Movement must begin within two seconds. This is a standalone relaunch, distinct from the in-process Restart used for the reversed objective order.

Keep the new battle open while returning to PowerShell, then capture its process and the first process's exit:

```powershell
. {
$ErrorActionPreference = 'Stop'
if (-not $initial.WaitForExit(30000)) { throw 'Previous battle is still running; do not restore files.' }
$initial.Refresh()
if ($initial.StartTime.ToUniversalTime() -ne $retainedProcessStart -or $initial.ExitCode -ne 0) { throw 'Previous battle lifetime/exit differs.' }
$next = @(Get-Process -ErrorAction Stop | Where-Object { $_.ProcessName -eq 'MatrixGame' -and $_.Path -eq $exe -and $_.Id -ne $initial.Id })
if ($next.Count -ne 1) { throw 'Expected one observed replacement; do not choose or terminate an unknown process.' }
$null = $next[0].Handle
$replacementStarted = $next[0].StartTime.ToUniversalTime()
if ($replacementStarted -lt $retainedProcessStart) { throw 'This is a stale process, not the recorded replacement.' }
$next[0].Id
}
```

Return to the new battle and close it with Alt+F4, then run:

```powershell
. {
$ErrorActionPreference = 'Stop'
if (-not $next[0].WaitForExit(30000)) { throw 'Replacement is still running; retain backups.' }
$next[0].Refresh()
if ($next[0].StartTime.ToUniversalTime() -ne $replacementStarted -or $next[0].ExitCode -ne 0) { throw 'Replacement lifetime/exit differs.' }
$next[0].ExitCode
}
```

An unexpected exit, error dialog, stuck loader or missing second process is a failure, even if the first battle's result looked correct. This visible procedure retains the initial process and observed replacement handle/start time. A same-path observation does not prove parentage; a missing, extra or short-lived process leaves the record incomplete and does not authorize terminating it. This does not replace the automated controller's complete descendant tracing. Once the declared exits are verified, run the completion/evidence guard before any cache/settings restoration.

## NV-06-4A: Break followed by Anim

1. Launch `mixed-break-anim.cmap`. Leave the blue ordinary object and unarmed robot untouched. Use direct control to fire at the red target until it becomes gray. Release fire and wait ten seconds. The green objective must remain active and no result may appear.
2. Fire at the green target until its objective progress bar disappears. A victory must appear within ten seconds of that retirement. The statistics must credit two destroyed objectives/buildings; the blue ordinary object must still be present. The unit without weapons should remain alive.
3. Follow the result/statistics/next-battle sequence above, choosing `mixed-boss-break.cmap`. Confirm the fresh red Terron, green objective, blue ordinary object and starting units, issue a Move, then exit. Record both process exits.

## NV-06-4B: Anim followed by Break

Launch `mixed-break-anim.cmap` again, or use Escape, R, Enter before completing the previous battle. Dismiss the restored introduction. Use direct control to fire at the green objective first, release fire and wait ten seconds: the red objective must remain and no result may appear. Then retire red and require victory within ten seconds, with exactly two objective/building kills. Repeat the next-battle and exit checks. This verifies the other order; a first-order pass does not cover it.

## NV-06-4C: Terron, collateral and a surviving objective

1. Launch `mixed-boss-break.cmap`. Leave the green objective and the two unarmed robots untouched. Select only the laser robot, enter direct control and fire at the large red Terron. When its health is exhausted and the destruction explosions begin, release fire, press Enter to leave direct control, then M and click near an empty outer corner to retreat. Do not move either unarmed control.
2. Observe the delayed explosion sequence. The nearby unarmed robot must be destroyed; the distant control must retain its health. The blue ordinary object lies inside the blast radius and should convert to gray. The shooter can also be hit if it remains inside the blast radius; its survival is not the collateral control. At least the distant control must survive.
3. Terron converts after its approximately five-second destruction countdown. Victory must appear within ten seconds of the countdown starting, while the distant green special Break remains green and intact. This preserves the current rule that Terron requests victory even with another objective still present. Statistics must credit one destroyed objective/building; the ordinary blue conversion does not add objective credit.
4. Acknowledge result and statistics, choose `mixed-break-anim.cmap`, verify its intact red/green/blue starting targets and fresh units, issue a Move and close. Record both exits as above.

The near robot starts 60 world units from Terron; the distant control and green objective are outside the configured 250-unit blast radius. Robot damage is 2000 internal points versus the configured initial 1900 internal health. These are source-derived expectations for this exported configuration, not a completed collateral playtest. Enemy-side blast attribution is covered by the [post-conversion component regression](186-terron-owner.md). The [mixed-objective component cases](85-mixed-objectives.md) cover the player's last-robot-loss orderings, including loss observed by a status scan before the decisive hit and the resulting pending-defeat Q. These native scenes do not exercise those variants or establish native presentation of that quirk.

## Automated check, evidence and cleanup

The default asset-free `game.session.special_export_layout` and `game.session.special_export_ownership` cases validate generation without opening a window or creating a native device. They load the maps and animation storage, verify coordinates/descriptors/armed-control identities, compare all Text/Packed configuration trees, open the generated empty package through its production parser, check Packed isolation, reject both non-empty and empty existing directories and missing-parent destinations, and remove incomplete output. The negative case deliberately removes the required special mesh before validation; export must fail and clean only its new destination.

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\test.ps1 -Configuration Debug -Filter '^game\.session\.special_export_(layout|ownership)$'
if ($LASTEXITCODE -ne 0) { throw 'Debug special export checks failed.' }
.\tools\test.ps1 -Configuration Release -Filter '^game\.session\.special_export_(layout|ownership)$'
if ($LASTEXITCODE -ne 0) { throw 'Release special export checks failed.' }
}
```

Generation checks do not execute gameplay or satisfy a visual case. Preparation status: A, B and C have generated inputs and exact procedures; their native observations remain pending in the ledger until performed. The optional hidden check below is also a separate execution record.

The optional `game.session.native_special_scenes` check opens only children of an invisible host window and uses a real Direct3D 9 device. Through the actual DLL entry point it loads both exported scenes and then the ordinary synthetic map, in Text and Packed modes. It verifies objective registration, descriptors, hit points, geometry/animations, weapon effects, loop entry and cleanup. In each special map it selects the armed robot, enters direct control through the form, holds fire and supplies resolved world aim at the host callback boundary. Full native frames must produce a nonfatal weapon hit on each colored objective within ten seconds of game time, preserving the objective count. This verifies generated weapon geometry, actual weapon traces and damage; it does not test screen picking. The ordinary following session runs three ticks. No objective destruction or result transition is injected or claimed. The DLL startup RNG remains its real ParkMiller/time-derived configuration; no fixed-step or cross-run image determinism is claimed.

The native fixture installs the shared `NativeWindowContainment` helper before creating its device or entering the DLL. It keeps the fixture-owned `MatrixGame_wc` child under an invisible parent without activation and adapts imported cursor selection, visibility and clipping to fixture-owned state. Required cursor adaptations are checked before the game runs. Native message-box diagnostics are captured and cause failure; they are never accepted as a passing initialization. The fixture sends no desktop input and uses no global hooks. Asset-generation checks above do not invoke any of these game paths.

To register it, set `MATRIXGAME_NATIVE_SESSION_TESTS=ON` in the DLL build, then run:

Run the complete fenced block as one statement, including `. {` and the final `}`. It stops on a failed preparation/configure/build/test step and restores ordinary native registration in `finally`, then rebuilds the DLL and fixture even after failure. A failed restoration leaves the build unverified; do not continue to ordinary tests. Finish each native process before changing build options.

```powershell
. {
$ErrorActionPreference = 'Stop'
$configuration = 'Release'
$cheats = $false
$build = "build/mingw-$($configuration.ToLowerInvariant())-dll"
$cmake = '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe'
try {
    .\tools\build.ps1 -Configuration $configuration -DLL -WithoutMusic -TestsOnly -WithoutResources -Jobs 2 -Cheats:$cheats
    if ($LASTEXITCODE -ne 0) { throw 'Native test build preparation failed.' }
    & $cmake -S . -B $build -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw 'Native test registration failed.' }
    & $cmake --build $build --target MatrixGame matrixgame_native_special_fixture --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Native fixture build failed.' }
    .\tools\setup-directx-runtime.ps1
    if ($LASTEXITCODE -ne 0) { throw 'DirectX runtime preparation failed.' }
    Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination (Join-Path $build 'tests') -ErrorAction Stop
    .\tools\test.ps1 -Configuration $configuration -DLL -WithoutMusic -NoBuild -Jobs 1 -Cheats:$cheats -Filter '^game\.session\.native_special_scenes$'
    if ($LASTEXITCODE -ne 0) { throw 'Native fixture checks failed.' }
} finally {
    & $cmake -S . -B $build -DMATRIXGAME_NATIVE_SESSION_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Restoration failed: native registration remains unverified; do not run ordinary tests.' }
    & $cmake --build $build --target MatrixGame matrixgame_native_special_fixture --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Restoration failed: default fixture binaries remain unverified; rebuild before ordinary tests.' }
}
}
```

Keep separate execution records for A, B and C, Debug/Release, Text/Packed and both standalone RNG modes, or record an explicit maintainer disposition of a variant. Optional original audio packages and original artwork are absent from these fixtures; audible or original-resource presentation validation remains with its respective cases. Native execution of these outcome procedures is pending until recorded. A successful hidden initialization check must not be entered as a pass for A/B/C.

The exporter writes only inside its new directory. Running the standalone executable also uses the production Documents output path; follow the cache preservation and restoration steps above after all exported game processes exit, before deleting the export. Preserve any screenshots separately. No supplied resource package or normal configuration needs restoring. Retain only the agreed evidence and hashes; generated assets are reproducible and should not enter the repository or release.

The separate [native outcome cases](85-native-special-outcomes.md) extend automation through actual destruction, DLL victory/statistics callbacks and a following ordinary session. They retain a supplied world-aim boundary and do not replace the standalone visual/relaunch observations in this procedure.
