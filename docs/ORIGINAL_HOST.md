# Original host integration

The DLL build is a compatibility interface for the original 32-bit Space Rangers 2 host, which loads it to run planetary battles. This page records the host contract, the automated evidence for it and a bounded original-host validation procedure. It needs an owned game installation; original-host execution stays deferred under [the native ledger](testing/native-validation.md) until the maintainer resumes it. Procedure preparation, synthetic-host coverage and a passing original-host run are different evidence.

## Contract

The host contract is x86-only. Pointers, `long` and callback slots are four bytes and structures use natural alignment. `wchar` strings are NUL-terminated UTF-16. The declarations are in [MatrixGameDll.hpp](../MatrixGame/src/MatrixGameDll.hpp).

| Element | Contract | Automated evidence |
| --- | --- | --- |
| Export | `GetRobotInterface`, `extern "C"` and `__cdecl`, exported under that undecorated name; returns one static entry table | `game.dll.native_entry_table`, `game.dll.entry_table` |
| Entry table | `SMGDRobotInterface`, 16 bytes: `Init`, `Deinit`, `Support` and `Run`, all `__stdcall`. Each query refills the same table | `game.dll.host_layout`, `game.dll.entry_table` |
| Callback table | `SMGDRangersInterface`, 64 bytes of 16 `__stdcall` pointers in declaration order. `Init` borrows it until `Deinit`; a later `Init` replaces it and `Deinit` may repeat | `game.dll.host_layout`, `game.dll.entry_table`, `game.dll.native_entry_table` |
| Settings | `SRobotsSettings`, 56 bytes, copied by `CGame::Init` after the game configuration loads; its two leading 32-bit values carry borrowed Direct3D and device pointers | `game.dll.host_layout`, `game.host.settings_image` |
| Result | `SRobotGameState`, 24 bytes: time, robots built, robots killed, turrets built, turrets killed and buildings killed, each 32-bit. Written whenever the loop returns, including errors it catches and reports as `100` to `104`; initialization and form-construction failures, and exceptions that reach the entry boundary later, leave it untouched | `game.host.result_statistics`, `game.dll.*_failure` |
| Arguments | Instance, window, map, settings, language, start/win/loss texts, planet and result pointers are borrowed for the call and forwarded without copying | `game.dll.borrowed_arguments`, `game.dll.native_language_configuration` |
| Return codes | `0` exit to Windows, `1` main menu, `2` loss, `3` win, `4` cancel; `100` to `104` are the error variants. Caught entry failures return `100`. A loop that ends without the exit flag returns `0` | `game.dll.success_values`, `game.dll.*_failure`, `game.dll.native_rejected_configuration` |
| Configuration | `CFG/<lang>/robots.dat` when the host supplies a language, otherwise `CFG/robots.dat`; without that file the text configuration is used | `game.dll.native_language_configuration`, `game.dll.native_rejected_configuration` |
| Support | `0` supported, `1` DirectX 9 unavailable, `2` driver too old, `3` hardware unsupported | Native only; the result depends on the graphics adapter |
| Text descriptor | `SMGDRangersInterfaceText`, 20 bytes: image, pixel pointer, pitch, width and height | `game.dll.host_layout` |

`SRobotsSettings` offsets: `FDirect3D` 0, `FD3DDevice` 4, stencil shadows 8, projected shadows 9, alternative selection 10, land gloss 11, object gloss 12, software cursor 13, sky box 14, robot shadow 15, bits per pixel 16, width 20, height 24, refresh rate 28, brightness 32, contrast 36, multisampling 40, anisotropy 44, maximum distance 48 and VSync 52, followed by three padding bytes. Boolean and byte fields are one byte; integers are signed 32-bit and the remaining values are binary32.

## Callback use

| Callback | Engine use | Automated coverage |
| --- | --- | --- |
| `m_Sound`, `m_SoundGetPan` | Not called | None needed |
| `m_SoundCreate`, `m_SoundDestroy`, `m_SoundPlay`, `m_SoundIsPlay`, `m_SoundVolume`, `m_SoundPan`, `m_SoundGetVolume` | Sound playback through [SoundBridge.cpp](../MatrixGame/src/SoundBridge.cpp). Creation receives a borrowed path, a group counter and a loop flag; a zero identifier means failure. Playback sets pan, then volume, then starts the sound | `game.host.sound_arguments`, `game.audio.host_routing`, `game.audio.host_layer_lifetime` |
| `m_ProgressBar` | Map loading progress in strictly increasing hundredths, reaching 1.0 when the final loading phase completes | `game.host.progress_callback` |
| `m_Begin` | Once near the end of initialization, after the map and interface load | Prepared opt-in synthetic-host session checks count the callback; original-host entry is NV-07-1 |
| `m_Takt`, `m_MusicVolumeGet`, `m_MusicVolumeSet` | `m_Takt` on every logic tick. The music callbacks fade the host music out when the special Terron object is destroyed, and the developer console can fade it out or restore it | Prepared opt-in synthetic-host sessions observe ticks; `game.audio.host_music_routing` covers the bridge. Audible original-host fade is NV-07-2 |
| `m_RangersText`, `m_RangersTextClear` | Host-rendered interface and hint text | Prepared opt-in synthetic-host sessions supply text buffers and check matching release; original-host fonts and hints are NV-07-1/2 |

Debug and trace builds convert C++ exceptions raised inside some sound callbacks into engine errors; Release builds have no such containment. A host callback must not throw across the boundary.

The entry/layout/sound/progress contracts use synthetic callbacks. The loaded-library `game.dll.native_*` cases in [dll_host_tests.cpp](../tests/dll_host_tests.cpp) use the real exported DLL and a message-only window; they deliberately fail before loading a map or creating a graphics device. Their `native` name does not mean the original host application ran.

The later [synthetic-host session fixture](../tests/native_session_fixture.cpp) calls the production entry with generated resources, a contained child process and a real Direct3D 9 device. Its `game.session.native_run_lifetimes` and `native_packed_lifetimes` contracts are registered only in a DLL build with `MATRIXGAME_NATIVE_SESSION_TESTS=ON`, which defaults to OFF. The prepared assertions cover successful initialization, progress/begin/tick/text callbacks, movement, failure/reentry and borrowed-device/window cleanup. They do not establish original-host compatibility, visible fonts, native input or audible sound. This guide records their available preparation, not a new execution result; use the ledger for any reviewed execution or reuse record.

## Native smoke procedure

Procedure: **NV-07 v1**. Execution remains **Pending**. The source contract and steps below are prepared; the installation, input scenes and required profiles must still be qualified in a private per-case record before a case is Ready. No original-host pass is established by this document.

### Qualify the execution profile

Use the [existing native record](testing/native-validation.md#per-case-record), with one record per case and required build/resource profile. Record the immutable candidate revision, DLL SHA-256, pinned compiler/dependencies, x86 DLL role, Debug/Release, cheats/assertion/trace options, host version/language and actual loaded configuration. The host initializes ParkMiller from the current time; a normal host run is not a fixed-seed replay. Record that policy rather than claiming a controlled seed or changing it for this check.

Use the existing no-cheats Release profile as the first qualification. Record the required Debug/Packed, Debug/Text, Release/Packed and Release/Text variants from the accepted catalog; do not infer one from another. A Text variant needs a separately qualified complete text configuration for this host and language. Removing its packed file does not create those inputs. Missing configurations, localization, saves, devices or sound resources leave the corresponding record Pending, with the missing input named for the baseline owner; they are not a pass or an automatic waiver.

Before execution, complete this input sheet using local aliases and hashes. Keep installation paths, game files, saves and proprietary captures private.

| Input | Required qualification |
| --- | --- |
| Host | An owned 32-bit installation that can enter planetary battles with its original library; exact executable/version/language and the menu or save-loading route to each scene |
| Library and configuration | Identify the installed DLL exporting `GetRobotInterface`, its hash and original filename. Identify the host-selected `CFG/<lang>/robots.dat`, or `CFG/robots.dat` when no language is supplied; record the actual language/configuration, not a filename guess |
| A: ordinary battle | A locally named map or save, deterministic entry instructions, friendly mobile robot, available enemy/objective, a usable factory/base and a free turret site; record resources sufficient for one legal robot and turret build |
| W/L: outcomes | Locally named states and bounded commands that produce one natural win and one natural loss. Record the expected result/statistics and host destination; surrender is not a substitute for natural defeat |
| T: Terron | A locally named state with the special Terron object, audible host music and a bounded way to destroy it; an ordinary robot called Terron is insufficient |
| Device and audio | Windows/adapter/driver, supported resolution/mode/scaling, audio endpoint and host sound/music settings, plus package/configuration/map hashes and effective difficulty. Record precision/render policy where required by the catalog |
| Protection | Original DLL/configuration hashes, private backup destinations outside the installation, host log starting length, a way to restore all changed files and enough free space for retained failure evidence |

If the ordinary installation cannot provide one of these scenes within the bounds below, record a preparation gap before launching that case. Choosing or supplying a suitable owned save/map is qualification work; this procedure does not distribute assets, create an AI API or modify gameplay to force an outcome.

### Build and stage one candidate

These commands build and test without launching the host. Run from the repository root; use the same explicit configuration for both commands. They reconfigure cheats OFF rather than trusting an earlier DLL build.

```powershell
$hostConfiguration = 'Release' # Use Debug for a separately required Debug record.
.\tools\build.ps1 -Configuration $hostConfiguration -DLL -WithoutResources
if ($LASTEXITCODE -ne 0) { throw 'Host candidate build failed.' }
.\tools\test.ps1 -Configuration $hostConfiguration -DLL
if ($LASTEXITCODE -ne 0) { throw 'Host candidate tests failed.' }
```

For MinGW, the candidate is `build/mingw-<configuration>-dll/MatrixGame/MatrixGame.dll`; MSVC uses its documented configuration subdirectory. Inspect the actual candidate PE architecture, exact undecorated `GetRobotInterface` export and runtime imports with the pinned toolchain before staging. Keep the build helper's imported runtime files together when the selected binary needs them; a Debug or MSVC library may have different prerequisites from statically linked MinGW Release. The host needs the x86 `d3dx9_43.dll` runtime. See [Windows builds](BUILD_WINDOWS.md#dll).

Staging is a future interactive test step, not permission to change a running installation. Close the owned host completely. Record hashes and back up the original planetary-battle library and selected configuration outside the installation. Preserve any existing runtime file before replacing it; do not overwrite an unrelated DLL merely because its name matches an import. Install the candidate under the original library's filename, verify its hash and verify the full runtime layout. Do not replace the host resources with the standalone build's configuration. Prepare restoration before the first launch.

### Bounds and failure handling

Run one owned host process at a time. Allow 120 seconds for host startup or battle loading, 30 seconds for a requested menu/focus transition, and 120 seconds for each qualified command/production/outcome sequence. Qualify the save/resources/actions so these limits are realistic before execution. Record a timeout as Failed or blocked by its precise missing input; do not extend a deadline after observing failure to obtain a pass. A different machine/input may use a reviewed bound fixed before its run.

Stop on an unexpected process exit, diagnostic, corrupt/missing host state, stuck input, wrong result or timeout. Keep the case's identities, observations and newly appended diagnostics before recovery. Close the owned host normally where possible; record forced termination as failure containment, never successful completion. Restore protected files with the host stopped and verify their original hashes before another case. Do not modify unrelated running processes or delete shared caches/saves.

A completed battle normally returns to its host while that process stays alive. The expected engine `Run` values below come from the documented interface and automated contracts; the original host UI does not necessarily expose those numeric values. Record the actual visible host result and continuation, and a numeric value only if the selected observation method exposes it. The host application's eventual process exit code is a separate observation, not the engine callback return value.

### Case selection and observations

| Case | Steps | Required observation |
| --- | --- | --- |
| NV-07-1: entry and ordinary play | Enter qualified scene A. Dismiss the introduction, select a friendly robot, issue a Move to the recorded reachable point, attack the recorded objective, build one affordable legal robot and construct one turret at the qualified free site. | Loading reaches completion; the battlefield, readable text and hints appear. Selection/order feedback matches the selected unit, movement reaches its point, combat changes the intended target and both builds complete without duplicate/partial state. No unexpected host exit or diagnostic. Record initial/final resources and observed units/statistics; merely opening a window is insufficient. |
| NV-07-2: host text, sound and Terron music | In A, observe UI, movement and battle sounds while changing camera distance; check a documented hint. In a fresh qualified T entry, destroy the special object with the recorded actions while host music is audible. | Host-rendered text/hints remain readable, sounds are audible and respond to the scene/camera, and the Terron event fades host music. A missing/inaudible qualified sound profile is not credited from a callback counter. Leave through the normal battle menu and confirm host responsiveness. |
| NV-07-3-W/L/M: result return | Run W and L independently using their recorded commands; confirm each natural result and complete its statistics/dialog route. For M, enter A and use its normal battle-menu Exit route, confirm statistics and return. | The host receives the appropriate win, loss or main-menu outcome and remains usable. Expected engine values are 3, 2 and 1 respectively. Compare build/kill counters with the qualified actions and record the reported elapsed time; that time is not an exact replay oracle for this wall-clock-seeded run. Do not infer raw result-buffer bytes from a screenshot. Record all three variants separately. |
| NV-07-4: reentry and focus | In one host process, enter A, complete a Move and leave via M. Enter A again; check fresh starting state and another Move. Hold a camera arrow briefly, switch focus away, release the key and return. Issue a new selection/Move and leave normally. | Both entries load and play, the second has no previous selection/order/entity carryover, the cursor is released while unfocused, and returning permits normal fresh input without a stuck held key. The host remains alive and responsive between battles; a new host process does not prove reentry. |
| NV-07-5: rejected configuration and recovery | With the host stopped, use the protected invalid packed configuration below at its exact selected path. Launch the host and request A once. Capture the diagnostic/visible host response, close the host, restore the original configuration and verify its hash. Start a fresh host and enter A again, then complete a Move and leave normally. | The rejected entry does not crash the host. The newly appended `matrixgame-dll-errors.log` reports `Invalid packed configuration:` followed by the selected configuration path. The tested entry-failure contract is 100 with result statistics untouched; record only what the original-host observation actually exposes. The restored entry is playable with no repeated rejection. This Packed rejection does not establish a missing-Text or late-interface failure case. |
| NV-07-6: restore the installation | With the host stopped, restore the original library/configuration and every replaced runtime file, remove only files created by the case, and verify the protected hashes. Launch the original host, enter its qualified ordinary battle, complete a Move and leave normally. | The original installation's protected bytes match their pre-test identities and its ordinary battle still works. Record any intentionally retained private evidence; an unverified copy or launcher-only start is insufficient restoration evidence. |

The camera and move actions above are native input, with the actual keys/menu labels recorded for the selected host language/configuration. Do not apply standalone selector/settings shortcuts to the host by assumption. These cases do not compare frame-exact trajectories; historical recording/comparison remains with the baseline's reference campaign.

### Exact rejected packed input

The rejected input is the same eight bytes used by [the loaded-DLL rejection test](../tests/dll_host_tests.cpp): ASCII `STRG`, followed by little-endian version 2. It is deliberately not a playable packed configuration. Do not edit an assumed version offset inside a compressed archive. Apply it only to the protected host-selected packed path for NV-07-5 with the host stopped; preserve the entire original file, including its exact length, before replacement.

```powershell
# Payload definition only; this block writes no file and launches nothing.
[byte[]]$rejectedHostConfiguration = 0x53, 0x54, 0x52, 0x47, 0x02, 0x00, 0x00, 0x00
```

Use a case-owned binary write when execution is authorized, verify all eight output bytes, and restore the original file in the case's cleanup even when entry fails or times out. `CGame::Init` selects the configuration relative to the host working directory; confirm that directory and selected language first. Capture only the log bytes appended after the recorded starting length so an earlier diagnostic cannot make the case pass.

### Evidence and outstanding qualification

Complete the ledger record with the exact steps and deadlines used, observed host destinations/statistics/input/audio, source/binary/resource identities, actual diagnostics and host process exit when it is finally closed. Keep proprietary assets, saves and private paths out of public reports. An exported symbol, successful compilation, callback-only assertion or synthetic-host result cannot substitute for these original-host observations.

The remaining preparation is qualifying the selected installation and A/W/L/T inputs, language-specific Packed/Text profiles, expected statistics, supported display/audio profile and restoration record. These are explicit Pending inputs until supplied and reviewed. Executing the cases remains deferred under the native validation task; preparing this guide does not reopen the completed format/host implementation package or accept the complete baseline.
