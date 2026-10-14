# Settings window

The standalone EXE has an in-game Settings window for display, sound, graphics and gameplay options. It reads and writes `CFG/standalone.txt` beside `MatrixGame.exe`. Editing that file by hand keeps working: the window rewrites only the options listed below. The DLL build has no Settings window; the host owns its settings.

## Opening and using the window

Press Escape during a battle to open the battle menu. Then click the settings entry (the label and the round button below it), or press `C`. The battle remains paused while the window is open. Its labels follow the language of the game configuration: Russian when the menu text is Russian, otherwise English.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Select an option | Point at its row | Up/Down |
| Step a value | Click the left half of the value (`<`) to go back, the right half (`>`) to go forward | Left/Right |
| Apply and save | Checkmark button | Enter |
| Discard changes | Cross button | Escape |

Apply and Cancel both return to the battle menu. Options marked `*` take effect after the game restarts. The others change immediately when applied.

## Options

| Window row | Key in `CFG/standalone.txt` | Values | Default | Takes effect |
| --- | --- | --- | --- | --- |
| Resolution | `ResolutionWidth`, `ResolutionHeight` | Modes reported by the primary display adapter, plus the current size | `1024`, `768` | After restart |
| Window mode | `WindowMode` | `Windowed`, `Borderless`, `Exclusive` | `Windowed` | Immediately |
| Alt+Enter | `FullscreenMode` | `Borderless`, `Exclusive` | `Borderless` | Immediately |
| Volume | `SoundVolume` | `0`-`100`, in steps of 10 in the window | `100` | Immediately |
| Stencil shadows | `StencilShadows` | `On`, `Off` | `On` | Immediately |
| Projected shadows | `ProjectedShadows` | `On`, `Off` | `On` | Immediately |
| Sky | `Skybox` | `Off`, `Low`, `High` | `High` | After restart |
| Robot shadows | `RobotShadows` | `On`, `Off` | `On` | After restart |
| Camera speed | `CameraSpeed` | `25`-`300` percent of the configured scroll speed, in steps of 25 in the window | `100` | Immediately |
| Random generator | `RandomGenerator` | `ParkMiller`, `LegacyCRT` | `ParkMiller` | After restart |
| Map | `Map` | A map path such as `Matrix\Map\desert1_3e.cmap`, or empty for `Config/Map` | Empty | Opens the [Map window](MAPS.md), which starts the game again on the chosen map |

**Display.** The display keys keep the contract in [Display settings](DISPLAY.md). The window shows the current window mode, including a mode reached with Alt+Enter, and Apply saves it. Choosing `Borderless` or `Exclusive` as the window mode also makes it the Alt+Enter mode, matching how a fullscreen start behaves. A window-mode change uses the same transition and rollback as Alt+Enter. If exclusive mode is unavailable at the current resolution, the game keeps its current mode and logs the reason.

**Sound.** Volume scales every standalone sound effect and speech voice, including sounds already playing. It does not affect a DLL host's audio.

**Graphics.** The stencil and projected shadow options correspond to the original game's shadow settings. Previously the EXE always enabled them; they now default to the same values. Each option controls only its own kind of shadow, so either kind can be shown alone. The sky's `Low` and `High` options select the original low- and high-quality sky textures.

**Gameplay.** Camera speed scales keyboard and screen-edge scrolling. See [Battle random generation](RANDOM.md) for the random generators. The Map row shows the current battle map; clicking it or pressing Left/Right opens the [Map window](MAPS.md). Playing a map there also saves the Settings window's current values.

Values in the file are case-insensitive, and `true`/`false` and `1`/`0` are also accepted for `On`/`Off`. A missing key uses its default. An invalid or repeated `SoundVolume`, `StencilShadows`, `ProjectedShadows`, `Skybox`, `RobotShadows`, `CameraSpeed` or `Map` falls back to its own default and writes a diagnostic to `test.log`. Display and random-generator keys keep their existing fallback rules.

## Saving

Apply writes the file before changing the running game. It replaces the supported keys' values in place. It keeps comments, blank lines, unknown keys, blocks and line order. It reads the file's structure as the game does, so a key written after a block on the same line, such as `Extension {} SoundVolume=40`, is updated in place. It removes repeated definitions of a supported key and appends missing keys at the end. The game stops reading at a closing brace outside any block, so missing keys are added before such a brace instead. The file keeps its encoding (UTF-8, or UTF-16 with a byte-order mark) and its line endings. The new content is written to `CFG/standalone.txt.tmp` and then replaces the original. If saving fails, the window shows an error and stays open, the previous file is left unchanged, and the cause is written to `test.log`.

Source builds stage `MatrixGame/CFG/standalone.txt` beside the EXE on every build, so a rebuild replaces settings saved in a build directory. Edit the tracked copy to change the defaults of local builds. Release packages contain the tracked defaults.

## Verification

Automated `game.settings.*` contracts cover:
- File parsing, defaults and per-key fallback.
- Text-preserving rewrites in both encodings, including keys that follow a block on the same line.
- Atomic saving and failed-save behavior.
- Window selection, stepping, mode coupling, hit areas and both label languages.
- Startup mapping of graphics options.
- Immediate routing of shadows, camera speed and volume, and shadow pass selection for each combination of the two shadow options.
- The remembered map; see [Map selection](MAPS.md#verification) for the Map window's own contracts and playtest.

`game.audio.master_volume` and `game.audio.master_level_retained` cover volume scaling and its retention across audio sessions.

The rendered window, menu integration and real display transitions need a playtest. Run it in Debug and Release with cheats disabled, once with packed and once with text game configuration:

1. Start a battle and press Escape. The settings label must use the same color as the other menu labels and highlight when the pointer is over it or its button. Click it. The Settings window must open centered, with the battle paused.
2. Move the pointer over the rows and step every option with the mouse, including both ends of Volume and Camera speed. Use Up/Down/Left/Right on the keyboard. Check that labels and values stay inside the frame and that the checkmark and cross sit in their holes.
3. Press Escape. The menu must reappear and the file must be unchanged. Press `C` to reopen the window; the values must match the file.
4. Set Volume to 30 and apply. Effects must become quieter immediately, including continuing sounds. Change camera speed, apply, and confirm the visible effect. Switch Window mode between Windowed, Borderless and Exclusive, and check the result after each Apply.
5. Starting with both shadow options on, apply in turn: Stencil shadows off, then Projected shadows off, then Stencil shadows on, then Projected shadows on. Each kind of shadow must appear only while its option is on, and a kind turned back on must reappear on objects that were already shadowed.
6. Change Resolution, Sky, Robot shadows and the random generator, apply, and restart. The game must start with the new values. Check that the comments in `CFG/standalone.txt` are preserved.
7. Make `CFG/standalone.txt` read-only and apply a change. The window must show the save error and keep the original file. Restore write access.
8. Repeat steps 1 and 2 at 640x480 and with a Russian and an English game configuration.

Record the build, configuration type, resolution, modes, failures and exit codes.
