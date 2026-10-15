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

`DATA/sound.pkg` and `DATA/voices.pkg` are optional. With neither present the game runs silently. Sound-name mappings are built into the executable: adding `sound.pkg` is sufficient for effects. For speech, copy one original `voicesRus.pkg` or `voicesEng.pkg` as `DATA/voices.pkg`. `CFG/sounds.txt` is an optional per-name override; see [Standalone sound](https://github.com/pozitronik/MatrixGame/blob/dev/docs/AUDIO.md). Keep optional archives intact; invalid archives can prevent startup.

`CFG/robots.dat` is optional. When absent, the game loads `CFG/robots/data.txt` and `CFG/robots/iface.txt`. `CFG/standalone.txt` selects the random generator; `ParkMiller` is the default, with `LegacyCRT` available for corrected legacy behavior.

## Resolution and fullscreen

Press Escape during a battle and open Settings (or press `C` in that menu) to change these options, volume, shadows, camera speed and the random generator; see [Settings window](https://github.com/pozitronik/MatrixGame/blob/dev/docs/SETTINGS.md). Sky, robot shadow and random-generator changes apply after restart; resolution changes apply immediately. The window saves `CFG/standalone.txt`, which can also be edited directly. Set `ResolutionWidth` and `ResolutionHeight` to the rendering size in pixels (for example, `1920` and `1080`). Set `WindowMode` to `Windowed`, `Borderless` or `Exclusive`. Alt+Enter switches between windowed mode and `FullscreenMode` (`Borderless` by default); starting fullscreen selects that starting mode for subsequent toggles. These options also work with `CFG/robots.dat`.

Borderless fills the primary display by scaling the configured image; use the monitor's native resolution or the same aspect ratio to avoid stretching. Exclusive mode requires a supported display resolution and minimizes on focus loss. Windowed and borderless modes pause without forcing minimization. Invalid display settings fall back to 1024x768 windowed and are recorded in `test.log`. Changes made with Alt+Enter last for the current session unless saved from the Settings window.

## Maps

Choose **New battle** in the startup menu before loading a battlefield. During play, open the battle menu with Escape and choose **New battle (N)**, or close the statistics after a battle. The separate selector shows previews, available descriptions and bonuses. Start with its button, Enter or a double-click on a map name. The game remembers the map and bonus in `CFG/standalone.txt` and starts a new process when changing battles. An animated loading screen covers the transition; it can be dismissed without stopping the new process and closes after 120 seconds if the handoff does not complete. A `.cmap` file placed in `Matrix\Map` beside the executable is listed too and replaces a packaged map with the same name. Optional `CFG/Lang.dat` supplies original descriptions and is not bundled. See [Map selection](https://github.com/pozitronik/MatrixGame/blob/dev/docs/MAPS.md).

## Controls

Dismiss the introductory checkmark, then select a friendly robot. Press `M` and click ground to move, `A` and click an enemy to attack, `S` to stop, or `X` to cancel an order. Arrow keys scroll the tactical camera. Enter toggles manual robot control; `W/A/S/D` move the robot in that mode. Escape opens the menu or leaves manual control. Button hints describe available commands.

## Source and notices

`SOURCE.json` identifies the exact source revision, build configuration and file hashes. The matching source ZIP includes the engine source, build helpers and thread-runtime source. Engine sources are available under GPLv2 or any later version; see `LICENSE`. Compiler-runtime and third-party notices are in `THIRD_PARTY_NOTICES.md` and `licenses/`.
