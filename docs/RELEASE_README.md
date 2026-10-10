# MatrixGame

MatrixGame is the planetary battle game from Space Rangers 2, developed as a standalone Windows game. This package is an x86 Release build with developer cheats disabled.

## Start the game

1. Extract the ZIP into a writable folder.
2. Install the [Microsoft DirectX End-User Runtimes, June 2010](https://www.microsoft.com/en-us/download/details.aspx?id=8109). The game needs the 32-bit `d3dx9_43.dll` component even on 64-bit Windows; a newer DirectX version alone does not supply it.
3. Create a `DATA` folder beside `MatrixGame.exe` and place your separately supplied `robots.pkg` at `DATA/robots.pkg`.
4. Run `MatrixGame.exe` without arguments.

Windows 10 or 11 is the supported baseline. The package includes the game's text configuration. No compiler, build tools or original game installation is needed to launch it once the runtime and resource package are supplied.

Without `robots.pkg`, the game displays "Place robots.pkg at DATA/robots.pkg." and exits. It does not download game resources.

## Optional sound and configuration

`DATA/sound.pkg` and `DATA/voices.pkg` are optional. With neither present the game runs silently. Sound playback also needs a separately prepared `CFG/sounds.txt` mapping; see [Standalone sound](https://github.com/pozitronik/MatrixGame/blob/dev/docs/AUDIO.md). Keep optional archives intact; invalid archives can prevent startup.

`CFG/robots.dat` is optional. When absent, the game loads `CFG/robots/data.txt` and `CFG/robots/iface.txt`. `CFG/standalone.txt` selects the random generator; `ParkMiller` is the default, with `LegacyCRT` available for corrected legacy behavior.

## Controls

Dismiss the introductory checkmark, then select a friendly robot. Press `M` and click ground to move, `A` and click an enemy to attack, `S` to stop, or `X` to cancel an order. Arrow keys scroll the tactical camera. Enter toggles manual robot control; `W/A/S/D` move the robot in that mode. Escape opens the menu or leaves manual control. Button hints describe available commands.

## Source and notices

`SOURCE.json` identifies the exact source revision, build configuration and file hashes. The matching source ZIP includes the engine source, build helpers and thread-runtime source. Engine sources are available under GPLv2 or any later version; see `LICENSE`. Compiler-runtime and third-party notices are in `THIRD_PARTY_NOTICES.md` and `licenses/`.
