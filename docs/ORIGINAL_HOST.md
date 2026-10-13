# Original host integration

The DLL build is a compatibility interface for the original 32-bit Space Rangers 2 host, which loads it to run planetary battles. This page records the host contract, the automated evidence for it and a native smoke procedure. The procedure needs a licensed game installation and cannot run in CI; its evidence must be recorded separately before the behavior baseline is accepted.

## Contract

The host contract is x86-only. Pointers, `long` and callback slots are four bytes and structures use natural alignment. `wchar` strings are NUL-terminated UTF-16. The declarations are in [MatrixGameDll.hpp](../MatrixGame/src/MatrixGameDll.hpp).

| Element | Contract | Automated evidence |
| --- | --- | --- |
| Export | `GetRobotInterface`, `extern "C"` and `__cdecl`, exported under that undecorated name; returns one static entry table | `game.dll.native_entry_table`, `game.dll.entry_table` |
| Entry table | `SMGDRobotInterface`, 16 bytes: `Init`, `Deinit`, `Support` and `Run`, all `__stdcall`. Each query refills the same table | `game.dll.host_layout`, `game.dll.entry_table` |
| Callback table | `SMGDRangersInterface`, 64 bytes of 16 `__stdcall` pointers in declaration order. `Init` borrows it until `Deinit`; a later `Init` replaces it and `Deinit` may repeat | `game.dll.host_layout`, `game.dll.entry_table`, `game.dll.native_entry_table` |
| Settings | `SRobotsSettings`, 56 bytes, copied at the start of `Run`; its two leading 32-bit values carry borrowed Direct3D and device pointers | `game.dll.host_layout`, `game.host.settings_image` |
| Result | `SRobotGameState`, 24 bytes: time, robots built, robots killed, turrets built, turrets killed and buildings killed, each 32-bit. Written only after a completed loop | `game.host.result_statistics`, `game.dll.*_failure` |
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
| `m_Begin` | Once near the end of initialization, after the map and interface load | None; needs a loaded map and device |
| `m_Takt`, `m_MusicVolumeGet`, `m_MusicVolumeSet` | `m_Takt` on every logic tick. The music callbacks fade the host music out when the special Terron object is destroyed or the developer console requests it | None; needs a map tick |
| `m_RangersText`, `m_RangersTextClear` | Host-rendered interface and hint text | None; needs the interface and a device |

Debug and trace builds convert C++ exceptions raised inside some sound callbacks into engine errors; Release builds have no such containment. A host callback must not throw across the boundary.

The automated cases use synthetic callbacks, a message-only window and the real exported library for the `native` cases. They do not create a graphics device, load a map or run a battle, so they do not establish successful host integration.

## Native smoke procedure

Status: pending. No original-host evidence has been recorded for the current revision.

Prerequisites:

- Windows 10 or 11 with a working Direct3D 9 adapter and audio endpoint.
- A licensed Space Rangers 2 installation that runs planetary battles, and a backup of its planetary-battle library. Identify that library as the DLL in the installation that exports `GetRobotInterface`, for example with `objdump -p <file>` from the pinned toolchain.
- The x86 `d3dx9_43.dll` runtime available to the host process.

Build the library without cheats and check its exports:

```powershell
.\tools\build.ps1 -Configuration Release -DLL -WithoutResources
.\tools\test.ps1 -Configuration Release -DLL
```

The library is `build/mingw-release-dll/MatrixGame/MatrixGame.dll`. Record its SHA-256 hash and the source revision. Replace the installed planetary-battle library with it under the installed file name, keeping the backup outside the game directory.

Checks:

1. Start a new or saved game and enter a planetary battle. The host loading bar advances to completion and the battle starts without an error dialog.
2. Check that interface text and hints are rendered and that sounds play and change with the camera position. On a map with the Terron, check that destroying it fades the host music out.
3. Exercise robot selection, movement, combat, factory production and turret construction for a few minutes.
4. Finish one battle as a win and one as a loss, and leave one battle through the menu. Check that the host shows the expected result and statistics each time and remains responsive.
5. Enter a second battle in the same host session, then switch away from the game and back during the battle.
6. Make the configuration unreadable: replace `CFG/robots.dat` (or `CFG/<lang>/robots.dat` for a localized installation) with an invalid copy whose version field is `2`. Enter a battle and check that the host receives an error result instead of crashing, and that `matrixgame-dll-errors.log` in the host working directory reports the invalid packed configuration. Restore the original file.
7. Restore the original library and confirm the game starts normally.

Record for each run: source revision, library hash, build configuration, Windows version, graphics adapter and driver, game version and language, steps performed, observed results and any `matrixgame-dll-errors.log` excerpt. Keep game files, saves and screenshots containing proprietary assets out of public reports.
