# Map selection

The standalone game opens a main menu before loading a battlefield. Choose **New battle**, select an installed map and press **Start**. During a battle, Escape opens the battle menu; **New battle (N)** replaces Help and opens the same selector with the battle paused. Map selection is separate from Settings. The host chooses maps and difficulty in the DLL build.

## The selector

The left pane lists installed maps, 14 per page. A blue `>` marks the selected map; the current battle map is grey-blue. The right pane shows its preview, name, catalog category, qualitative length rating, enemy-side colors and description. The length rating comes from the original catalog; it is not a terrain dimension or a measured completion time. Missing information has explicit placeholders.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| Select a map | Click a name; wheel over the list | Up/Down, Home/End |
| Change map page | `<` / `>` below the list | Left/Right |
| Read the description | Wheel over the description; `^` / `v` below it | Page Up/Page Down |
| Select a bonus | Click its radio label | 1: None; 2: Air support; 3: Armor |
| Start the selected battle | Start or double-click a map name | Enter |
| Return without saving | Cancel | Escape |

The description wraps to the actual font width and scrolls independently of the map list. Its range indicator shows the visible lines and total line count. Selecting another map returns the description to its beginning. All controls fit the minimum supported 640x480 rendering resolution. Alt+Enter remains available before and during battles.

Cancel returns to the startup or battle menu without saving the browsed map or bonus. After a completed battle, closing the statistics also opens the selector; Cancel there exits. The startup main menu has New battle and Exit; Escape exits it successfully without loading any map. Its text follows the game configuration's language.

## Map information

To show original names and descriptions, copy the desired original game's language catalog to `CFG/Lang.dat` beside the executable. For example, copy the original installation's `CFG/Eng/Lang.dat` for English or `CFG/Rus/Lang.dat` for Russian. No conversion tool or Python runtime is needed. The build helper stages a `Lang.dat` placed at the repository root there. This optional file is not included in the repository or release archive.

The catalog describes 74 maps in the inspected original installation; availability still comes from installed `.cmap` files. Entries match case-insensitive filenames, including loose replacements with the same name. A replacement using an original filename therefore inherits that catalog entry; use a distinct filename for an unrelated custom map. Without a matching entry, the selector shows the filename and available preview. An absent catalog is normal; invalid data is ignored with a diagnostic in `test.log`, and maps remain selectable.

Names, complete description paragraphs, categories and enemy-side fields come from the optional catalog. Formatting and substitution markup is converted to safe display text. The metadata reader's format and bounds are documented in [Data formats](DATA_FORMATS.md#original-language-map-catalog).

Extracted JSON catalogs are archival exports, not a supported runtime sidecar format. Custom descriptions supplied separately from `Lang.dat` require a future sidecar contract; they are not needed to play custom maps.

## Bonuses

| Choice | Effect on the next battle |
| --- | --- |
| None | Preserve the configured difficulty unchanged |
| Air support | Air-support maintenance interval is 33% of its configured duration |
| Armor | Damage from other sides and sideless sources to the player's robots, buildings and turrets is multiplied by 0.5 |

Bonuses scale the existing configuration's difficulty multipliers; None leaves them unchanged. All choices preserve the current friendly-fire multiplier. Armor also affects sideless sources such as falling debris and, with `DelayedDamage=Original`, later burn/shock pulses. It does not increase maximum hit points. The selector does not apply the original host's complete difficulty presets or their 40% friendly-fire setting. Browsing bonuses never changes the current battle. The selected bonus applies once when the new battlefield is constructed.

## Installed maps

Maps are `Matrix/Map/*.cmap` files from the registered packages or the loose `Matrix/Map` folder beside `MatrixGame.exe`. Subfolders are not scanned, and arbitrary additional `.pkg` files are not registered automatically. The required `DATA/robots.pkg` supplies shared game resources even when the selected map is loose.

Names compare case-insensitively and the list is sorted by filename. A loose map takes precedence over a packaged one with the same name; for duplicate packaged maps the first registered package wins. A same-stem `.jpg` or `.png` supplies the preview, with JPEG preferred. Loose previews override packaged previews. An absent or undecodable image shows **No preview** and does not prevent starting the map.

Supported filenames contain ASCII letters, digits, spaces and `_-.()+!#&,'~`. Other names are skipped because the existing resource loader and settings format impose narrower filename rules than Windows. A custom map must use a compatible `.cmap` format and have its referenced resources installed. Listing and the initial readability check do not establish full payload validity.

Names retain their existing gameplay significance: `demo` selects automatic demonstration behavior, and `terron` enables that map's special logic. Demo maps play once and are then forgotten; the next startup menu selects the configured fallback. No maps, previews or original catalogs are bundled with releases.

## Saving and starting

| Key in `CFG/standalone.txt` | Values | Default |
| --- | --- | --- |
| `Map` | Relative `.cmap` path, for example `Matrix\Map\desert1_3e.cmap`, or empty | Empty |
| `BattleBonus` | `None`, `AirSupport`, `ReinforcedArmor` | `None` |

An empty remembered path selects `Config/Map` when opening the startup selector. If that map is not listed, the first installed map is selected. The `cfg.txt` override still applies with packed configuration. Missing bonus keys default to None; invalid or repeated values default individually and are reported in `test.log`. Settings preserves both keys when applying unrelated options.

Start first loads the selected file as storage and checks its `properties` table's `Name` and `Value` columns, then saves the map and bonus. If validation or saving fails, the selector stays open and displays an error. The check is not a complete map-schema validator: damaged payloads or missing dependencies can still fail during battlefield preparation.

At startup, successful selection proceeds to the first battlefield in the same process. From an existing battle, the old session shuts down and the executable relaunches with `--start-selected-battle`, which bypasses the startup menu only when the remembered map is readable. A missing selection returns to the startup menu. There is one initialized battlefield per process. An animated loading window stays visible during teardown and loading, until the new battle presents its first frame. It also closes if the new process reports an error or exits. If process creation fails, a dialog asks the player to launch the game manually; the saved selection remains available.

Just before loading a remembered map, its path is cleared from the settings file and restored after successful initialization. A failed load therefore leaves the configured fallback for the next startup selector. This does not clear the saved bonus. If the file cannot be updated, the diagnostic does not claim the failed map was forgotten. Failures before loading retain the selection.

The loading screen has a taskbar entry and a Hide loading screen button. Close it or press Escape while it has focus to dismiss it; initialization continues. During a process transition, the old process pumps messages and waits for at most 120 seconds. Timeout or dismissal removes the loader and exits the old process normally without terminating the child. The new game can still show its window when loading finishes. Starting a map saves only the map and bonus, preserving stored display preferences: temporary Alt+Enter toggles are not saved, and a new process uses the stored mode.

A normal command-line map argument retains its visibility-calculation meaning and bypasses the startup menu and standalone battle bonus. `--start-selected-battle` is reserved for interactive relaunch; its optional second argument is a private loading-screen event name supplied by the game. It is not a visibility-tool map name. The x86 DLL continues to receive its map and difficulty from the host.

## Verification

The baseline's [versioned native selector procedures](testing/85-native-selector.md) separate menu-origin, Surrender, direct cheat-victory-dialog and Exit routes, with configuration restoration and observed-process exit reporting. They name the missing natural win/loss and pending-result inputs explicitly; neither those instructions nor the general checklist below report completed native execution.

The `game.maps.*` and `game.settings.*` tests cover package/loose precedence, safe filenames, optional DAT parsing, metadata matching and fallback, description wrapping/scrolling, selection and bonus controls, unchanged friendly fire, settings round trips and save failure. The `game.loading.*` cases check loading handoff, failed presentation, startup failure and premature child exit. Device recovery and partial-session cleanup have separate synthetic contracts. These checks use synthetic fixtures, not distributed game files.

The complete rendered startup-to-battle transition still needs a native playtest. Use a Release build with cheats disabled; extra human Debug/configuration permutations are unnecessary. Build using `tools/build.ps1 -Configuration Release`, supply the game resources, and launch with no arguments. Keep a copy of `CFG/standalone.txt` if you want to restore your preferences afterwards.

1. At startup, confirm the main menu appears before any battlefield or introduction. Choose Exit. The process must exit with code 0.
2. Launch again and choose New battle. Select several maps with mouse and keys. Check previews and placeholders, map paging, description scrolling and all three bonus controls. Cancel must return to the main menu. Reopening the selector must discard unsaved choices.
3. With `CFG/Lang.dat` present, inspect named maps and a long description through its last line. Test the other language by replacing only this optional catalog between launches. Without the file, filenames/previews must remain usable.
4. Start a map with Enter. Confirm the normal introduction and gameplay. Open the battle menu and use New battle by its label, icon and N. Cancel must return to that menu; Settings must have no Map row.
5. Start another map with a different bonus. Check Enter after clicking a map name, then double-click a map name during a second transition. The loading window must remain visible until the new battle appears without another startup menu. Also check double-click activation from the startup selector. Exit normally, relaunch, and confirm the map and bonus are remembered. Confirm no setting changed merely by browsing/canceling.
6. Repeat the selector checks at 640x480. Use Alt+Enter and lose/regain focus in both the startup menu and selector. Text, preview, input and exit must still work. Report any exception, incorrect map, clipped control or nonzero exit code.
7. Hold Enter on startup New battle: repeats must leave the selector open until release and a fresh press. Hold Escape in that selector: it must return to the menu without exiting on repeats. After battle statistics, held Enter must not immediately start the next map. Navigation arrows should still repeat.
8. If a transition lasts long enough, choose Hide loading screen or close its window; the new battle must still be able to start. Check Escape after activating the loader when practical. Do not race a short load: the registered `game.loading.dismiss_*` cases exercise the real controls on a private desktop with a gated child. Report an unobserved manual check as such. Repeat an ordinary transition with exclusive fullscreen and focus loss/recovery when that mode is available. A synthetic test checks the timeout; deliberately hanging a real game is not required. Confirm that using Alt+Enter before choosing another map does not rewrite saved display settings, and that the new process uses those saved settings.

A malformed-map failure, optional metadata, original-language catalogs and the relaunch window are distinct checks; a passing model test does not establish native rendering or gameplay acceptance.
