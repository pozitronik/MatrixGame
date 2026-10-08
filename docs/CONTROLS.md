# Controls and UI text

Keyboard defaults are defined by `CMatrixConfig::SetDefaults`. The active configuration can replace action keys through `Config/AssignKey`, so a supplied packed configuration can differ from the tracked text defaults. Button hints identify the active commands. This reference describes the existing interfaces; key rebinding and UI replacement are separate work.

## Battle controls

| Action | Default input and context |
| --- | --- |
| Select | Left-click a friendly unit or drag a selection rectangle. |
| Move | Select a robot, press `M`, then left-click the ground destination. |
| Attack | Select a robot, press `A`, then left-click a reachable enemy. |
| Stop or cancel | `S` stops the selected group; `X` cancels a pending order. |
| Patrol, capture, repair | `P`, `K`, `R`, followed by a target appropriate to that command. |
| Camera | Arrow keys scroll; Home/End and Page Up/Page Down rotate. Hold the middle mouse button to rotate by mouse; the wheel zooms. Packed configuration may supply other scroll keys. |
| Manual control | Enter or Space enters the selected robot and returns to tactical control. `W/A/S/D` and arrows move in robot mode; `E` detonates its bomb. |
| Construction | Select the base and press `B` for the robot constructor; `T` opens turret construction when an eligible building is selected. |
| Pause | Pause toggles battle pause outside a modal dialog. |
| Dialogs and menu | Enter activates the visible confirmation checkmark. Escape opens or cancels menu/dialog state according to context. While the main menu is open, `R`, `S`, `E` request restart, surrender and exit. |

Ordinary activation loss releases held keys, clears the last-key marker, cancels mouse-camera rotation and releases the developer console's Shift modifier. Returning requires a fresh press for those held inputs; typed console text is retained. Selection, construction, combat, complete drag behavior and manual-control feedback still require visible playtests.

## Developer input

Build separately with `tools/build.ps1 -Configuration Debug -Cheats` or the matching Release command. Type a code into the battle view while the developer console is closed. In-game recognition is compiled into the form only when cheats are enabled. Console tests invoke production handlers directly and do not establish that disabled game builds recognize them.

| Code | Existing effect |
| --- | --- |
| `DEVCON` or the tilde key | Open the developer console. |
| `SHOWFPS` | Open the console; this is currently an alias and does not itself toggle the FPS overlay. |
| `RICHIERICH` | Fill all four battle resources to the 9,000 cap. |
| `AUTO` | Toggle automatic robot management. |
| `FLYCAM` | Toggle the free-camera flag. |
| `NEED4SPEED` | Toggle four-times simulation speed. |
| `KEEPALIVE` | Toggle continued activity while unfocused. The inherited activation path skips the ordinary input/resource reset in this mode. |
| `IAMTESTER` | Enable automatic mode, keep-alive, four-times speed and side/FPS diagnostics. |
| `IAMLOOSER` | Open the victory dialog and select the win result; it does not prove natural victory detection. |
| `SPAWN` | Create the configured laser-equipped robot at the cursor; this is not a bomber shortcut. |

The complete code table remains in [Cheats.cpp](../MatrixGame/src/Cheats.cpp). Some codes require battle objects, mesh resources or a particular simulation state. `CRASH` intentionally aborts the process and belongs in an isolated diagnostic run. Keep developer checks distinct from ordinary gameplay checks.

In the console, letters, digits, Space and the mapped punctuation keys edit the command. Left/Right and Home/End move the caret; Backspace/Delete remove characters. Shift uppercases letters. Enter dispatches the command name without case sensitivity. Escape first clears nonempty input; another Escape closes the empty console. This is an ASCII virtual-key editor, not a general localized text-entry widget.

`HELP` lists console commands. Existing handlers include `SHADOWS`, `CANNON`, `LOG`, `TRACESPD`, `BUILDCFG`, `MUSIC`, `COMPRESS` and `CALCVIS`; availability of a handler does not establish every argument or side effect. `BUILDCFG` writes configuration files, and `CALCVIS` performs visibility calculation. Read the command's implementation before using commands that write output. The standalone audio backend and the `MUSIC` console command have separate contracts.

## Text format and validation

UI tokenization preserves UTF-16 text and spaces. CRLF, LF and lone CR each represent one line break. Flat `<color=r,g,b>text</color>` tags are case insensitive; decimal channel values must be in `0..255`. Complete tags with invalid channel values use the caller's default color. An opening tag without `>` remains literal text. Parsing stays within the supplied string view, including views without a null terminator, and adjacent tags do not consume recursive stack frames. Nested color markup has no established contract.

Automatic line height includes exact-fit words on their current line and counts a word carried onto the next line. The renderer still uses its existing wrapping behavior; its `wordwrap` argument is currently ignored. Font availability, actual glyph widths, localized glyphs and readable clipping must be checked in the running game. Supplied packed configuration and tracked text configuration can contain different language strings.

The registered tests use production tokenization/layout, cheat recognition and game-form input with synthetic text, map/side state and diagnostics. Deterministic font metrics replace GDI/DirectX only in parser tests. They cover bounds, newline variants, colors, Unicode preservation, long tag sequences, layout, console editing, resource caps and focus-state release. Follow [Standalone playtesting](PLAYTESTING.md#controls-and-text) for the remaining visible checks.
