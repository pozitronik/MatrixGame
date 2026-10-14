# Map selection

The standalone EXE can start a battle on any installed map. Choose it in the in-game Map window. The choice is saved as the remembered map in `CFG/standalone.txt`, and the game starts again on that map. The DLL build has no Map window; the host chooses the map.

## Opening the window

Open the Map window in either of two ways:
- From the [Settings window](SETTINGS.md): select the Map row, which shows the current battle map, and click it or press Left/Right.
- After a won, lost or surrendered battle, close the statistics dialog. Leaving through the battle menu's Exit still ends the game without the window.

The battle stays paused while the window is open. Its text follows the language of the game configuration, as in the Settings window.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Select a map | Click its name, or use the wheel to move one map per notch | Up/Down, Home/End |
| Change page | Click `<` or `>` under the list | Page Up/Page Down |
| Play the selected map | Checkmark button | Enter |
| Close the window | Cross button | Escape |

The list shows 14 maps per page. The current battle map is drawn in blue-grey and the selected map in blue. The selected map's preview is on the right, scaled to fit a 200x200 box. Below it is the map's name, marked as a loose file when it comes from the folder beside the EXE.

Closing the window returns to the Settings window, keeping its unsaved edits. When the window was opened after a battle, closing it exits the game.

## Playing a map

Play first checks that the selected map loads as map storage with the `Name` and `Value` columns of a `properties` table. If it does not, the window shows an error and stays open, so another map can be chosen. Otherwise the game:
1. saves the map path as `Map` in `CFG/standalone.txt`, together with the Settings window's current values when the window was opened from Settings;
2. ends the battle and shuts down normally;
3. starts `MatrixGame.exe` again without arguments.

If saving fails, the window shows an error and stays open; the cause is written to `test.log`. If the new process cannot be started, a message asks the player to start the game again manually; the choice is already saved.

Every battle therefore runs in a new process, with the established startup and teardown; see [Lifecycle](LIFECYCLE.md). The restart takes as long as a normal launch, and the game window closes and reopens.

## Installed maps

The list contains `Matrix\Map\*.cmap` files from two sources:
- the registered packages, `DATA\robots.pkg` and the optional sound packages;
- the loose `Matrix\Map` folder beside `MatrixGame.exe`.

Subfolders are not searched. Names compare case-insensitively. A loose file replaces a packaged map with the same name, because the loader opens loose files first. When several packages contain the same map, the first registered package wins, as when loading.

The list is sorted by name and shows each file name without the extension. Map files have no display name or description.

A preview is a `.jpg` or `.png` file with the same name as the map, in any capitalisation and from either source; JPEG is preferred. A loose preview replaces a packaged one. Maps without a preview show "No preview".

Only maps whose paths fit the settings file and the window's text markup are listed:
- the file name may contain ASCII letters, digits, spaces and the punctuation `_-.()+!#&,'~`;
- other names are skipped.

Map names still change behavior as they do with `Config/Map`:
- names containing `demo` start an automatic demonstration that ends the game when it finishes. Escape exits in that mode and no statistics appear, so the Map window cannot be reached. A remembered demo map therefore plays once: the game forgets it after it starts, and the next launch uses the configured map;
- names containing `terron` enable that map's special logic.

## The remembered map

| Key in `CFG/standalone.txt` | Values | Default |
| --- | --- | --- |
| `Map` | A relative path ending in `.cmap`, such as `Matrix\Map\desert1_3e.cmap`, or empty | Empty |

An empty value starts the map named by `Config/Map` in the game configuration, as before. The `cfg.txt` override still applies when the packed configuration is used.

**Invalid values.** A path the map list would not accept is ignored with a diagnostic in `test.log`. This includes an absolute path, a parent reference, a repeated key or an unsupported character. The configured map starts instead.

**Unreadable maps at startup.** Before the game window opens, the game checks that the remembered map can still be read. If it cannot, for example because a loose file was removed, the game:
- shows a message;
- clears `Map` in the file;
- starts the configured map.

**Failures while loading.** The readability check covers only the map's properties table, so a damaged map can still fail or crash while it loads. The game therefore clears `Map` in the file just before the remembered map loads and writes it back once battle initialization succeeds. If loading fails with an error, the error dialog says the map was forgotten; when the settings file cannot be written, the dialog does not make that claim and the map stays remembered. If the game crashes, there is no message, but the next start uses the configured map. Failures before the map loads, such as device creation, keep the choice. Clearing and restoring `Map` saves the whole settings file, so loading a remembered map also replaces invalid values with their defaults and appends missing keys, as saving from the Settings window does.

The Settings window saves `Map` unchanged when it applies other options.

## Command line and DLL

A command-line map argument keeps its meaning: it runs the visibility calculation described in [Standalone playtesting](PLAYTESTING.md#map-arguments-and-failure-cases) and ignores the remembered map. There is no new command-line form. A restart started from the Map window passes no arguments.

DLL builds ignore `CFG/standalone.txt`. The statistics dialog ends the DLL session as before.

## Verification

Automated contracts cover the following:
- `base.layout.package_tree` and `base.layout.package_listing`: package folder listing.
- `game.maps.merge`, `game.maps.unsafe_names` and `game.maps.paths`: merging, precedence, preview matching, ordering and path validation.
- `game.maps.sources`: a synthetic package and loose folder, scanned and read through the production file fallback.
- `game.maps.window_*`: the window model's selection, paging, hit areas, text in both languages and hint markup.
- `game.maps.preview_markup`: the production parameter parser accepts the preview bitmap's required placement field in both languages and portrait/landscape layouts. This catches malformed markup before native rendering; it does not validate preview decoding or the rendered window.
- `game.settings.*`: the `Map` key. `game.settings.map_choice` covers saving a choice, using and forgetting a remembered map, and a failed save.

The rendered window, previews, restart and battle-end entry need a playtest. Run it in Debug and Release with cheats disabled, with packed and text game configuration, at 640x480 and at a large resolution:
1. Open Settings, select Map and open the Map window. Check the list, pager, current-map colour and previews, including maps without previews, demo and training maps. Use the mouse, wheel and keys. Escape must return to Settings with its edits intact.
2. Choose another map and play it. The game must restart on that map, and `CFG/standalone.txt` must name it. Restart the game manually; the same map must start.
3. Copy a packaged map into `Matrix\Map` beside the EXE, and add a preview with different capitalisation. The map must be marked as a loose file and show the loose preview.
4. Add a corrupt `.cmap` loose file and try to play it. The window must report it and stay open.
5. Remember a loose map, remove the file and start the game. The message must appear, the configured map must start, and `Map` must be empty.
6. Win or lose a battle and close the statistics. The Map window must open; Cancel must exit the game. Leave a battle with Exit and confirm that the game exits without the window.
7. Run the visibility calculation with a map argument and confirm that it ignores the remembered map.
8. Choose a demo map. It must play in full-auto mode, and the next launch must start the configured map with `Map` empty.

Record the build, configuration type, resolution, maps, failures and exit codes.
