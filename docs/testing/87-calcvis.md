# Bounded CalcVis preparation and observations

Procedure version: **NV-03-CV v1**. This is the remaining PRES-03/P5 setup for [the presentation package](87-presentation.md), with actual native execution owned by [NV-03](native-validation.md). It changes no production algorithm, game resource or host interface. A generated input, CPU test or successful process exit is not a native visual pass.

## Safe input boundary

Use only the `calcvis-flat` and `calcvis-ridge` exports described here. The legacy CalcVis edge pass reads every cell and its neighbors even though plane construction skips non-land cells. All 1,024 cells in these exports are land, all 1,089 points have finite positive heights, and all sixteen terrain groups and their triangle/vertex records are checked. The ridge has a 138-unit peak; the flat control is at height 10. This is a restricted safe input, not a correction or support claim for arbitrary original/custom maps. Never substitute a normal map into these commands.

Both inputs fix the strategy target at `(120,320)`, camera distance 70, angle 45 degrees and camera height 70, with movement/rotation/wheel configuration disabled. The actual runtime guard additionally checks all live cells/points/groups and the finite camera eye's coordinate/group bounds before CalcVis. The CPU fixture measures eye `(120,369.497,119.497)` and an initialized all-land edge pass. Its flat control creates no Debug helpers; the ridge creates a positive number, and a repeated call replaces its previous diagnostics. Exact helper counts and native pixels are not cross-compiler goldens.

CalcVis changes temporary edge/shadow state and replaces a zero-filled group-visibility array. Its local storage record is never saved. The map must remain byte-identical, and no newly generated visibility map is expected. Startup with a positional map argument appends to `calcvis.log` and exits before the game loop; that path cannot display its Debug helpers. The in-battle F7 observation is separate.

## Build and prepare four independent exports

Use the pinned [Windows tools](../BUILD_WINDOWS.md). Build the standalone game and test helpers in Debug and Release, with cheats disabled; no private packages are needed. The commands below compile and generate files, without starting a game:

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Debug -WithoutResources -Jobs 4
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
.\tools\build.ps1 -Configuration Release -WithoutResources -Jobs 4
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
.\tools\setup-directx-runtime.ps1
if ($LASTEXITCODE -ne 0) { throw 'Runtime preparation failed.' }
$repository = (Get-Location).Path
foreach ($configuration in @('Debug','Release')) {
    foreach ($mode in @('Text','Packed')) {
        $destination = Join-Path $repository ('build/calcvis-{0}-{1}' -f $configuration.ToLowerInvariant(), $mode.ToLowerInvariant())
        .\tools\prepare-calcvis-playtest.ps1 -Destination $destination -Configuration $configuration -ConfigurationMode $mode -Terrain ridge
    }
}
}
```

Each destination must be absent and have an existing parent. The preparer runs the existing synthetic exporter, copies the actual game, the opt-in observer, exporter and imported x86 runtime DLLs, then writes `calcvis-preparation.json` with source revision, profile and file hashes. No installation, Documents directory or existing export is overwritten. A staging failure retains its new output for diagnosis; an absent receipt is not a runnable preparation. Use another fresh root for the flat control or a repeated launch. Do not copy normal CFG/DATA/Matrix directories into an export.

`-VerifyOnly` checks the retained file hashes, actual map/configuration safety and absence of optional audio without launching a window. It does not prove that a binary came from a reviewed revision: record the actual build revision/options and match it to the reviewed candidate. Preserve the preparation receipt and process evidence privately; no external assets are present in these exports.

```powershell
. {
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath '.\build/calcvis-debug-text').Path
.\tools\prepare-calcvis-playtest.ps1 -Destination $game -VerifyOnly
if ($LASTEXITCODE -ne 0) { throw 'Prepared input changed.' }
}
```

## Positional-map startup, log and exit

Repeat this case in Debug/Text, Debug/Packed, Release/Text and Release/Packed. Select the corresponding prepared root and set `$configuration` accordingly. Before the first launch, reuse only the shared [path-resolution block](87-native-procedures.md#prepare-and-bound-each-record) after selecting this synthetic `$game` (skip that section's ordinary-game build/path selection), then run [settings preservation](87-native-procedures.md#preserve-settings-for-each-record) and [known-key cache preservation](87-native-procedures.md#preserve-the-selected-minimap-cache) with `$exe = Join-Path $game 'MatrixGame.exe'` and `$cacheNames = @('calcvis-ridge.cmap.0')`. The generated map omits DATA_UNIQID, so this exact basename/suffix is known from the writer. The flat control uses `calcvis-flat.cmap.0`. Preserve both Documents and fallback entries; no unprotected first run is permitted. Do not change the frozen camera, settings, map, configuration mode or resources after verification.

Use one launch attempt per protected record, from the prepared game directory. This block deliberately opens the real game and waits at most 60 seconds. An unexpected dialog, crash, extra game process/window or deadline is a failure. Do not dismiss an unexpected diagnostic and call the case passed. Run this block only when ready to observe it:

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($launchAttempted) { throw 'One launch attempt per record; restore the preceding record first.' }
if (-not $settingsReady -or -not $cacheReady -or $cacheQualificationReady) { throw 'Prepare the known-key settings/cache record first.' }
if ($process -and -not $process.HasExited) { throw 'The previous retained process is running.' }
if (@(Get-Process -Name MatrixGame -ErrorAction SilentlyContinue).Count) { throw 'Close other games first.' }
$currentGame = (Resolve-Path -LiteralPath $game).Path
if ($settings -ine (Join-Path $currentGame 'CFG/standalone.txt') -or $exe -ine (Join-Path $currentGame 'MatrixGame.exe')) { throw 'The protected record belongs to another game.' }
& (Join-Path $repository 'tools/prepare-calcvis-playtest.ps1') -Destination $currentGame -VerifyOnly
$map = Join-Path $currentGame 'Matrix/Map/calcvis-ridge.cmap'
$mapHash = (Get-FileHash -LiteralPath $map -Algorithm SHA256).Hash
$logPath = Join-Path $currentGame 'test.log'
$logBeforeLength = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0L }
$visibilityLog = Join-Path $currentGame 'calcvis.log'
if (Test-Path -LiteralPath $visibilityLog) { throw 'Use a fresh export; do not accept an earlier CalcVis completion.' }
$launchSucceeded = $false
$process = $null
$launchAttempted = $true
$clock = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $exe -WorkingDirectory $currentGame -ArgumentList 'Matrix\Map\calcvis-ridge.cmap' -PassThru -ErrorAction Stop
if ($null -eq $process) { throw 'No retained process handle; preserve the record.' }
$null = $process.Handle
$launchSucceeded = $true
if (-not $process.WaitForExit(60000)) { throw 'CalcVis exceeded 60 seconds; record failure and resolve the owned PID before restoration.' }
$clock.Stop()
$process.Refresh()
if ($process.ExitCode -ne 0) { throw "CalcVis exit: $($process.ExitCode)" }
$line = [IO.File]::ReadAllText($visibilityLog)
if ($line -notmatch '^(.+) \.\.\.done\r?\n$') { throw 'Missing or unexpected CalcVis completion line.' }
$loggedMap = [IO.Path]::GetFullPath((Join-Path $currentGame $Matches[1]))
if ($loggedMap -ine [IO.Path]::GetFullPath($map)) { throw 'The completed calculation names another map.' }
if ((Get-FileHash -LiteralPath $map -Algorithm SHA256).Hash -cne $mapHash) { throw 'CalcVis changed its input map.' }
[pscustomobject]@{ProcessId=$process.Id; ExitCode=$process.ExitCode; RuntimeMs=$clock.ElapsedMilliseconds; Completion=$line.Trim()}
}
```

Expect startup followed by automatic exit 0, exactly the selected map's ` ...done` line, an unchanged map, and a finite measured runtime. The window may be too brief to inspect. No menu interaction is needed and no playable battle or diagnostic image is claimed. Runtime includes initialization/teardown; it is not a benchmark of CalcVis alone. Preserve any failure and the newly appended `test.log` excerpt. Finish the shared [exit/evidence and settings/cache restoration](87-native-procedures.md#complete-a-record) for this retained process, even after a failed observation. An early failure may create no cache entry; a successful initialized map should create its known entry. A timeout needs explicit owned-process resolution: request that PID's normal close, wait five seconds, then terminate only that retained process if necessary and wait for its exit. Forced termination remains a failed case; never restore while it is alive. Keep backups if handle/ownership is uncertain.

After successful restoration, run `-VerifyOnly` again and retain the receipt, map hash, duration, exact binary/configuration identity, completion line and exit status. Repeat independently for the other three profiles and the flat control. New exports avoid mixing append logs between runs. For the flat control, replace `calcvis-ridge` with `calcvis-flat` in every map path, launch argument and cache key. Positional launch arguments use backslashes: the legacy initializer treats an argument without a backslash as a basename and prefixes `Matrix\Map\`.

## Debug diagnostics and flyer input observer

Ordinary selection excludes flyers, while the Debug horizontal-plane consumer requires an active selected flyer and the configured automatic-action key (the normal configuration uses the right mouse button). The opt-in `CalcVisObserver.exe` supplies that otherwise inaccessible precondition directly in a test executable. It initializes the production standalone scene, removes the introduction, pauses simulation and preselects one parked body-only synthetic flyer. Its native window, normal form input, `CMatrixMap::BeforeDraw`, F7 handler, plane query and flyer `SetTarget` remain production code. It overrides only its own output-folder resolver to `native-view-output/` inside the export, so it does not read or write Documents caches. It is not evidence that normal gameplay supports selecting a flyer, nor that this paused flyer physically navigates or fights.

Use a separate fresh Debug/ridge export in Text, then Packed. Verify its receipt first. The observer refuses missing explicit window consent, unknown modes and Release builds. Do not pass a different map or resume the battle. Its body-only flyer intentionally has no base/spawn lifecycle; simulation stays paused throughout. Do not open settings, select units, scroll, rotate, switch display modes or issue other commands during this bounded diagnostic case.

The following is an explicit interactive launch. Before running it, close other game/observer processes. The observer has a 120-second observation deadline; keep its returned PID for cleanup. Output files must be absent so a previous result cannot be mistaken for this launch:

```powershell
. {
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath '.\build/calcvis-debug-text').Path
& (Join-Path $repository 'tools/prepare-calcvis-playtest.ps1') -Destination $game -VerifyOnly
if (@(Get-Process -Name MatrixGame,CalcVisObserver -ErrorAction SilentlyContinue).Count) { throw 'Close other game/observer processes first.' }
$observerLog = Join-Path $game 'observer-output.txt'
$observerError = Join-Path $game 'observer-error.txt'
if ((Test-Path -LiteralPath $observerLog) -or (Test-Path -LiteralPath $observerError)) { throw 'Use a fresh observation export.' }
$observer = Start-Process -FilePath (Join-Path $game 'CalcVisObserver.exe') -WorkingDirectory $game -ArgumentList ('--view "' + $game + '" Text --allow-visible-window') -RedirectStandardOutput $observerLog -RedirectStandardError $observerError -PassThru -ErrorAction Stop
if ($null -eq $observer) { throw 'No retained observer handle.' }
$null = $observer.Handle
$observer.Id
}
```

Require a visible paused synthetic scene within 60 seconds. The left portion is low flat land and the ridge rises near world x = 320. Press F7 once: green diagnostic geometry must appear, the image must continue refreshing and the output file must report `F7 returned; Debug CalcVis helpers=` with a positive value. Do not require a particular marker count or screenshot. Keep the pointer in the central battlefield, outside UI elements and away from the flyer. Hold the right mouse button for one second at one clear location, then move at least 300 screen pixels to a second clear location and hold another second. The observer must report at least two distinct `Production flyer target=` coordinates, each with a verified horizontal-plane intersection. The flyer remains parked because simulation is paused; target assignment is the observation. Release the button and press Escape to end the observer.

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($null -eq $observer) { throw 'No retained observer handle.' }
if (-not $observer.WaitForExit(30000)) { throw 'Observer exit timed out; resolve only its owned PID before cleanup.' }
$observer.Refresh()
Get-Content -LiteralPath $observerLog
Get-Content -LiteralPath $observerError
if ($observer.ExitCode -ne 0) { throw "Observer failed with exit $($observer.ExitCode)" }
& (Join-Path $repository 'tools/prepare-calcvis-playtest.ps1') -Destination $game -VerifyOnly
}
```

Code 0 requires returned F7, positive Debug helper creation, at least two verified target transitions and unchanged source map. Capture the visible results separately: the observer cannot judge its rendered pixels. Code 1, missing/incorrect output, lost selected flyer, resumed simulation, diagnostic or deadline is a failure. The internal timer cannot bound a stalled initialization/driver call: if the scene is not ready by 60 seconds or the process remains after 150 seconds overall, record failure and use the retained-PID close/terminate/wait procedure above. Do not leave it running indefinitely. Restore nothing in Documents for this observer; all of its output is local to the disposable synthetic export. After process exit and manifest verification, retain evidence and remove the owned export through the normal workspace cleanup procedure when no longer needed. For Packed, use a fresh Packed export and replace the launch argument `Text` with `Packed`.

## Automated evidence and limits

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.presentation\.calcvis_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.presentation\.calcvis_'
```

The exporter/layout/CLI cases exercise both Text/Packed inputs, no-overwrite ownership and refusal of changed camera/map/mode inputs. Guard cases reject non-land/nonfinite points, missing groups, invalid camera positions, wrong group coordinates/indices and truncated records before executing CalcVis. The production-linked CPU cases call CalcVis twice in both RNG modes on safe flat/ridge inputs, assert no simulation time/RNG draws/input-file changes, zero saved group contents and distinct Debug diagnostics. The observer's default argument/transition tests open no native window and reject timing or input-state assumptions; queued input alone cannot establish a new target. Its visible mode is never registered with CTest. Debug source-omission controls remove the live-land check or target/plane equality check: the corresponding guard test fails, then passes after exact source restoration. These controls never run CalcVis on a non-land map or open the observer. The native observer is built only with standalone configurations; safe CPU/export contracts also compile for the original x86 DLL profile, without changing that interface.

The CPU fixture supplies a storage/state-only Direct3D surrogate and authored terrain. It does not run full standalone initialization, draw pixels, send OS input or establish driver/host behavior. All four native startup records, actual Debug diagnostics and native flyer-input observations remain **Pending** under #191 until performed and reviewed. This preparation does not accept #69 or authorize structural changes.
