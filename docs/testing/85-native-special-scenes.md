# Mixed-objective native scenes

Procedure version: **NV-06-4 v3**. This handoff supplies synthetic mixed Break/Anim and Terron maps, a resource exporter, a native loading and weapon check and exact standalone outcome steps. Preparation and execution are separate: the hidden native initialization check does not establish the manual attack, collateral, result-dialog or next-battle observations below. Record those observations in the [native validation ledger](native-validation.md#per-case-record).

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
.\tools\build.ps1 -Configuration Release -WithoutResources -Jobs 2
.\tools\setup-directx-runtime.ps1
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination '.\build\mingw-release-exe\tests'
& '.\build\mingw-release-exe\tests\matrixgame_native_special_fixture.exe' --export '.\build\native-special-release'
if ($LASTEXITCODE -ne 0) { throw 'Special-scene export failed.' }
$fixture = (Resolve-Path '.\build\native-special-release').Path
Copy-Item -LiteralPath '.\build\mingw-release-exe\MatrixGame\MatrixGame.exe' -Destination $fixture
Get-ChildItem -LiteralPath '.\build\mingw-release-exe\MatrixGame' -Filter '*.dll' -File | Copy-Item -Destination $fixture
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination $fixture
```

Copy only the executable and its runtime DLLs. Do not copy the normal build's CFG, DATA or Matrix directories: the export already supplies those inputs. The minimal Windows/legacy DirectX requirements remain those in the [build guide](../BUILD_WINDOWS.md). For Debug, replace `Release`/`release` with `Debug`/`debug` throughout and use a different new destination.

Record the source revision, binary SHA-256 and the hashes of both maps and selected configuration. The exported text profile is Windowed 1280×720 with shadows/sky disabled, ParkMiller RNG, default difficulty/bonuses and no optional audio packages. For the LegacyCRT variant, change only `RandomGenerator=ParkMiller` to `RandomGenerator=LegacyCRT` in the exported `CFG/standalone.txt`. The fresh export starts in Text mode. Use a new export for each recorded configuration/RNG profile. For Packed mode, close all processes from that export, then run the following inside its directory; parking both loose trees prevents a silent text fallback. Do not substitute an original `robots.dat`.

```powershell
Set-Location -LiteralPath $fixture
if (Test-Path -LiteralPath 'CFG/robots.dat') { throw 'Expected a fresh Text export.' }
if (Test-Path -LiteralPath 'parked-text-config') { throw 'Text configuration is already parked.' }
Copy-Item -LiteralPath 'CFG/robots.fixture-packed' -Destination 'CFG/robots.dat'
Move-Item -LiteralPath 'CFG/robots' -Destination 'parked-text-config'
if ((Test-Path -LiteralPath 'CFG/robots/data.txt') -or (Test-Path -LiteralPath 'CFG/robots/iface.txt')) { throw 'Packed configuration is not isolated.' }
```

Text and Packed inputs are generated from the same trees; the automated layout check compares every parameter and block in both. To return a generated Packed export to Text after its processes exit, remove only its generated `CFG/robots.dat` and move `parked-text-config` back to `CFG/robots`. Its standalone options remain outside that Packed archive.

Record reproducible input identities after choosing the configuration mode:

```powershell
git rev-parse HEAD
Set-Location -LiteralPath $fixture
$inputs = @('MatrixGame.exe', 'CFG/standalone.txt', 'Matrix/Map/mixed-break-anim.cmap', 'Matrix/Map/mixed-boss-break.cmap')
if (Test-Path -LiteralPath 'CFG/robots.dat') { $inputs += 'CFG/robots.dat' }
else { $inputs += @('CFG/robots/data.txt', 'CFG/robots/iface.txt') }
$inputs | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $fixture $_) -Algorithm SHA256 }
```

The integrated post-conversion Terron correction and its registered `game.outcome.terron_blast_after_conversion` regression remain prerequisites for interpreting a collateral result. A crash is a failure, not an accepted objective outcome.

## Launch and observe exits

Launch the exported game without positional arguments. A positional map path selects visibility calculation in `WinMain`, so it cannot be used for these interactive battles:

```powershell
Set-Location -LiteralPath $fixture
$game = (Resolve-Path '.\MatrixGame.exe').Path
$initial = Start-Process -FilePath $game -WorkingDirectory $fixture -PassThru
$null = $initial.Handle
```

Within 60 seconds the startup menu must appear. Choose New battle, select `mixed-break-anim` for cases A/B or `mixed-boss-break` for C, keep bonus None and press Start. Require the battlefield and introduction within 60 seconds of Start. Confirm the loaded map path in `test.log`; a saved preference alone does not identify a successful load. Dismiss the introduction with Enter and release the key. Select only the laser robot above the colored objectives; it is the unit with a weapon attached. Press Enter to enter direct robot control, point at the colored target and hold the left mouse button to fire. Release the button as soon as the target retires. Both colored objectives are inside the laser's configured 250-unit range from the starting position. Tactical right-click/A orders are not suitable for these synthetic map objects: the observed robot approached the target without firing. The optional native check below exercises that direct-control route; its execution needs a separate record. Press Enter again to leave direct control before issuing M and clicking ground to move. Use arrow keys to move the camera outside direct control. Escape opens the battle menu; R then Enter restarts the same map and restores all starting objects. None of these steps needs cheats.

Allow at most 30 seconds of aimed firing to retire each selected objective or start Terron's destruction sequence. A target that does not take damage, a wrong target or a timeout is a failed observation; do not continue indefinitely. Allow 30 seconds for a menu, confirmation, restart or normal exit, and 60 seconds for a replacement battlefield. Preserve the shorter result/countdown deadlines in A/B/C below. On failure, record the diagnostic and process state, then close the owned game before restoring or removing its export.

After the requested victory, press and release Enter to acknowledge the result and do the same when the statistics dialog appears. The standalone map-selection dialog must open within 30 seconds. Select the other exported map by its basename, keep None and press Enter. When the loading interval is observable, its screen must cover the transition until the new battle appears; record an unobservable fast interval as incomplete presentation evidence. Require the replacement battlefield within 60 seconds, dismiss its introduction and give its laser robot a short Move. Movement must begin within two seconds. This is a standalone relaunch, distinct from the in-process Restart used for the reversed objective order.

Keep the new battle open while returning to PowerShell, then capture its process and the first process's exit:

```powershell
if (-not $initial.WaitForExit(30000)) { throw 'Previous battle did not exit within 30 seconds.' }
$initial.Refresh()
$initial.ExitCode # expected 0 for the completed relaunch
$next = @(Get-Process MatrixGame | Where-Object { $_.Path -eq $game })
if ($next.Count -ne 1) { throw 'Expected one battle process in the exported directory.' }
$null = $next[0].Handle # Retain a handle before the process exits.
```

Return to the new battle and close it with Alt+F4, then run:

```powershell
if (-not $next[0].WaitForExit(30000)) { throw 'Replacement battle did not exit within 30 seconds.' }
$next[0].Refresh()
$next[0].ExitCode # expected 0
```

An unexpected exit, error dialog, stuck loader or missing second process is a failure, even if the first battle's result looked correct. This visible procedure retains the observed parent and child handles; it does not replace the automated controller's complete short-lived descendant tracing.

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

The near robot starts 60 world units from Terron; the distant control and green objective are outside the configured 250-unit blast radius. Robot damage is 2000 internal points versus the observed initial 1900 internal health. These are source-derived expectations for this exported configuration, not a completed collateral playtest. Enemy-side blast attribution and the player's last-robot-loss orderings remain in the component outcome tests; these native scenes do not claim those variants.

## Automated check, evidence and cleanup

The default asset-free `game.session.special_export_layout` and `game.session.special_export_ownership` cases validate generation without opening a window or creating a native device. They load the maps and animation storage, verify coordinates/descriptors/armed-control identities, compare all Text/Packed configuration trees, open the generated empty package through its production parser, check Packed isolation, reject existing/missing-parent destinations and remove incomplete output. The negative case deliberately removes the required special mesh before validation; export must fail and clean only its new destination.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.session\.special_export_(layout|ownership)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.session\.special_export_(layout|ownership)$'
```

Generation checks do not execute gameplay or satisfy a visual case. Preparation status: A, B and C have generated inputs and exact procedures; their native observations remain pending in the ledger until performed. The optional hidden check below is also a separate execution record.

The optional `game.session.native_special_scenes` check opens only children of an invisible host window and uses a real Direct3D 9 device. Through the actual DLL entry point it loads both exported scenes and then the ordinary synthetic map, in Text and Packed modes. It verifies objective registration, descriptors, hit points, geometry/animations, weapon effects, loop entry and cleanup. In each special map it selects the armed robot, enters direct control through the form, holds fire and supplies resolved world aim at the host callback boundary. Full native frames must produce a nonfatal weapon hit on each colored objective within ten seconds of game time, preserving the objective count. This verifies generated weapon geometry, actual weapon traces and damage; it does not test screen picking. The ordinary following session runs three ticks. No objective destruction or result transition is injected or claimed. The DLL startup RNG remains its real ParkMiller/time-derived configuration; no fixed-step or cross-run image determinism is claimed.

The invisible parent contains presentation but does not isolate the calling desktop's cursor state. Production cursor selection calls `ShowCursor` even for the software cursor, and DLL teardown calls `ClipCursor(nullptr)`; initialization also calls `ShowWindow` on the borrowed child. Run this opt-in case only on an explicitly chosen test desktop where those operations are acceptable. Asset-generation checks above do not invoke any of these game paths.

To register it, set `MATRIXGAME_NATIVE_SESSION_TESTS=ON` in the DLL build, then run:

```powershell
.\tools\build.ps1 -Configuration Release -DLL -TestsOnly -WithoutResources -Jobs 2
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B '.\build\mingw-release-dll' -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' --build '.\build\mingw-release-dll' --target matrixgame_native_special_fixture --parallel 2
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination '.\build\mingw-release-dll\tests'
.\tools\test.ps1 -Configuration Release -DLL -NoBuild -Filter '^game.session.native_special_scenes$'
```

Keep separate execution records for A, B and C, Debug/Release, Text/Packed and both standalone RNG modes, or record an explicit maintainer disposition of a variant. Optional original audio packages and original artwork are absent from these fixtures; audible or original-resource presentation validation remains with its respective cases. Native execution of these outcome procedures is pending until recorded. A successful hidden initialization check must not be entered as a pass for A/B/C.

All changes made by the manual procedure are inside the new export directory. Close its game processes before deleting that directory when validation is complete. No supplied resource package or normal configuration needs restoring. Retain only the agreed evidence and hashes; generated assets are reproducible and should not enter the repository or release.
