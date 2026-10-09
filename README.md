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

Standalone play requires the Space Rangers resource package `robots.pkg`.
Place it at the repository root, along with `robots.dat` if available. The build
script copies these files into the executable's resource directories. Game
resources are supplied separately and are not included in the repository.

Run `build/mingw-debug-exe/MatrixGame/MatrixGame.exe` without arguments to play.
See the [Windows build guide](docs/BUILD_WINDOWS.md) for prerequisites, MSVC
instructions and configuration options.

## Development

[Contributing](CONTRIBUTING.md) covers reporting bugs and submitting changes.
The [architecture guide](docs/ARCHITECTURE.md) explains subsystem ownership and data contracts, with practical [debugging procedures](docs/DEBUGGING.md).
The [testing guide](docs/TESTING.md) describes verification, and the
[roadmap](docs/ROADMAP.md) outlines the project's direction.

## License and origins

The copyright holders released the engine sources in `MatrixGame` and `MatrixLib` under **GPLv2 or any later version**; see [LICENSE](LICENSE). Preserve their existing copyright notices.

The engine uses libpng and zlib under their own licenses. See the [libpng license](https://github.com/glennrp/libpng/blob/v1.6.37/LICENSE) and [zlib copyright notice and license](https://github.com/madler/zlib/blob/v1.2.11/README#L77). Their original authors retain the rights and distribution terms for these libraries.

This project builds on [vladislavrv/MatrixGame](https://github.com/vladislavrv/MatrixGame)
and [murgesku/MatrixGame](https://github.com/murgesku/MatrixGame).
An [original source mirror](https://github.com/twoweeks/MatrixGame) is also available.
