# MatrixGame

MatrixGame is the planetary battle engine from Space Rangers 2, developed here
as a standalone game for Windows.

![Planetary battle](docs/image.png)

## Building and running

The game targets Windows x86 and uses C++20 and DirectX 9. Install 7-Zip and make
`7z.exe` available on PATH, then run these commands from the repository root:

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

Game resources are supplied separately and are not included in the repository. For local builds, place `robots.pkg` at the repository root, along with `robots.dat` if available. The build script stages them beside the executable.

For a standalone build, place the required package at `DATA/robots.pkg`, where `DATA` is beside `MatrixGame.exe`. Without it, startup shows "Place robots.pkg at DATA/robots.pkg." and exits with status 1 before initializing a battle. No game packages are needed to display this dialog.

`DATA/sound.pkg`, `DATA/voices.pkg` and `DATA/music.pkg` are optional. Missing audio packages do not prevent startup: the game runs silently without them, and missing effects, speech or battle music remain silent when only some are supplied. Sound mappings are built in, so `sound.pkg` is sufficient for effects; `CFG/sounds.txt` is an optional override. See [Standalone sound](docs/AUDIO.md) for speech and customization. `CFG/robots.dat` is optional because the build includes text configuration under `CFG/robots`.

Run `build/mingw-debug-exe/MatrixGame/MatrixGame.exe` without arguments to play.
See the [Windows build guide](docs/BUILD_WINDOWS.md) for prerequisites, MSVC
instructions and configuration options.

The [packaging guide](docs/RELEASES.md) describes the minimal standalone ZIP, source archive, checksums and release checklist.

Resolution, windowed/borderless/exclusive modes, volume, shadows, camera speed and the random generator can be changed in the in-game [Settings window](docs/SETTINGS.md) (battle menu, `C`) or in `CFG/standalone.txt`; see also [Display settings](docs/DISPLAY.md). The Settings window's Map row, and the end of a battle, open the [Map window](docs/MAPS.md) for choosing the next battle map.

## Development

[Contributing](CONTRIBUTING.md) covers reporting bugs and submitting changes.
The [architecture guide](docs/ARCHITECTURE.md) explains subsystem ownership, the [data-format guide](docs/DATA_FORMATS.md) records resource contracts, and the [debugging guide](docs/DEBUGGING.md) provides failure-investigation procedures.
The [testing guide](docs/TESTING.md) describes verification, and the
[roadmap](docs/ROADMAP.md) outlines the project's direction.

## License and origins

The copyright holders released the engine sources in `MatrixGame` and `MatrixLib` under **GPLv2 or any later version**; see [LICENSE](LICENSE). Preserve their existing copyright notices.

The engine uses libpng, zlib, libogg and libvorbis under their own licenses. See the [libpng license](https://www.libpng.org/pub/png/src/libpng-LICENSE.txt), [zlib copyright notice and license](https://github.com/madler/zlib/blob/v1.3.2/LICENSE), [libogg license](https://github.com/xiph/ogg/blob/v1.3.6/COPYING) and [libvorbis license](https://github.com/xiph/vorbis/blob/v1.3.7/COPYING). Their original authors retain the rights and distribution terms for these libraries. The [dependency manifest](ThirdParty/dependencies.json) pins their source releases and checksums.

This project builds on [vladislavrv/MatrixGame](https://github.com/vladislavrv/MatrixGame)
and [murgesku/MatrixGame](https://github.com/murgesku/MatrixGame).
An [original source mirror](https://github.com/twoweeks/MatrixGame) is also available.
