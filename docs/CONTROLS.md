# Controls and UI text

Keyboard action defaults are defined by `CMatrixConfig::SetDefaults`. The active configuration can replace those action bindings through `Config/AssignKey`; the tracked `data.txt` has an empty `AssignKey` block. Fixed virtual-key and mouse handlers are separate from those bindings, so `AssignKey` does not remap every input below. Button hints identify the active commands. This reference describes the existing interfaces; key rebinding and UI replacement are separate work.

## Battle controls

| Action | Default input and context |
| --- | --- |
| Select | Left-click a friendly unit or drag a selection rectangle. |
| Move | Select a robot, press `M`, then left-click the ground destination. |
| Attack | Select a robot, press `A`, then left-click a reachable enemy. |
| Stop or cancel | `S` stops the selected group; `X` cancels a pending order. |
| Patrol, capture, repair | `P`, `K`, `R`, followed by a target appropriate to that command. In Debug, `K` over an object can also damage or destroy it; see Debug shortcuts below. |
| Camera | Arrow keys scroll; Home/End and Page Up/Page Down rotate. Hold the middle mouse button to rotate by mouse; the wheel zooms. Scroll/rotation keys are action bindings; the middle button and wheel are fixed handlers. |
| Manual control | Enter or Space enters the selected robot and returns to tactical control. `W/A/S/D` and arrows move in robot mode; `E` detonates its bomb. |
| Construction | Select the base and press `B` for the robot constructor; `T` opens turret construction when an eligible building is selected. |
| Pause | The fixed Pause key toggles battle pause outside a modal dialog. |
| Dialogs and menu | The fixed Enter key activates a confirmation checkmark. In the main menu, fixed `R`, `S`, `E` request restart, surrender and exit; Escape cancels its confirmation or closes the menu. Other dialogs ignore Escape. Outside a dialog, Escape leaves manual robot control or opens the menu; in full-auto mode it exits the loop. |

Pause, Escape, Enter for the checkmark, menu `R`/`S`/`E`, the middle mouse button and the wheel are fixed handlers in [MatrixFormGame.cpp](../MatrixGame/src/MatrixFormGame.cpp). For example, rebinding the Stop action does not change the menu's `S` key. Manual-mode Enter/Space remain action bindings; their role differs from the fixed dialog Enter handler.

### Debug shortcuts

Builds with `_DEBUG` and without `_RELDEBUG` include extra keyboard handlers independently of the cheats option. Some normal actions can fall through to them. In particular, `K` over a unit, map object or building also damages or destroys that target, so a Debug capture check does not isolate ordinary capture behavior. Use Release for that ordinary command check. Existing Debug handlers also include `E` for a minimap event, `F` for maintenance, `Q` for a logic dump, F3/F5/F6/F7/F11 for geometry/visibility/resource diagnostics or test actions, and Delete for removing a debug path. Some handlers change battle state or write diagnostics; see `CFormMatrixGame::Keyboard` for their conditions.

Ordinary activation loss releases held keys, clears the last-key marker, cancels mouse-camera rotation and releases the developer console's Shift modifier. Returning requires a fresh press for those held inputs; typed console text is retained. Selection, construction, combat, complete drag behavior and manual-control feedback still require visible playtests.

The form also accepts an input-only reset with the same held-input cleanup, preserving application activity and graphics resources. A keep-alive activation path can send this event while continuing simulation and rendering; the ordinary deactivation path still releases default-pool resources.

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
| `KEEPALIVE` | Toggle continued activity while unfocused. Activation loss clears held keys, mouse-camera rotation and console Shift through an input-only reset while preserving simulation and graphics resources. |
| `IAMTESTER` | Enable automatic mode, keep-alive, four-times speed and side/FPS diagnostics. |
| `IAMLOOSER` | Open the victory dialog and select the win result; it does not prove natural victory detection. |
| `SPAWN` | Create the hard-coded antigravity/fullstack/dynamo robot with four lasers at the cursor; this is not a bomber shortcut. |

The complete code table remains in [cheats.cpp](../MatrixGame/src/cheats.cpp). Some codes require battle objects, mesh resources or a particular simulation state. `CRASH` intentionally aborts the process and belongs in an isolated diagnostic run. Keep developer checks distinct from ordinary gameplay checks.

In the console, letters, digits, Space and the mapped punctuation keys edit the command. Left/Right and Home/End move the caret; Backspace/Delete remove characters. Shift uppercases letters. Enter dispatches the command name without case sensitivity. Escape first clears nonempty input; another Escape closes the empty console. This is an ASCII virtual-key editor, not a general localized text-entry widget.

`HELP` lists console commands. Existing handlers include `SHADOWS`, `CANNON`, `LOG`, `TRACESPD`, `BUILDCFG`, `MUSIC`, `COMPRESS` and `CALCVIS`; availability of a handler does not establish every argument or side effect. `BUILDCFG` packs the loaded data and text interface into one `robots.dat` in the working directory, which is the executable directory in standalone operation. Its handler still carries a TODO for a reported segfault; the synthetic packing check does not establish safe live invocation. `LOG e` writes `log.txt`, `LOG s` writes a sound log when that handler is compiled in, and `COMPRESS` writes a `.strg` file derived from its input filename. `CALCVIS` performs visibility calculation. Read [DevConsole.cpp](../MatrixGame/src/DevConsole.cpp) before using commands that write output. The standalone audio backend and the `MUSIC` console command have separate contracts.

## Text format and validation

UI tokenization preserves UTF-16 text and spaces. CRLF, LF and lone CR each represent one line break. Flat `<color=r,g,b>text</color>` tags are case insensitive. Channel values use decimal digits in `0..255`; a leading `+` and negative zero are also accepted. Whitespace, extra fields and other negative values are rejected. Complete tags with invalid channel values use the caller's default color. An opening sequence remains literal only if no `>` follows within its space/newline-delimited word. A malformed sequence such as `<color=255,0,0red</color> tail` consumes the word through the closing tag's `>` and leaves ` tail`; this legacy behavior is not a guarantee that malformed tags remain visible. Parsing stays within the supplied string view, including views without a null terminator, and adjacent tags do not consume recursive stack frames. Nested color markup has no established contract.

Automatic line height includes exact-fit words on their current line and counts a word carried onto the next line. The renderer still uses its existing wrapping behavior; its `wordwrap` argument is currently ignored. Font availability, actual glyph widths, localized glyphs and readable clipping must be checked in the running game. Supplied packed configuration and tracked text configuration can contain different language strings.

The registered tests use production tokenization/layout, cheat recognition and game-form input with synthetic text, map/side state and diagnostics. Deterministic font metrics replace GDI/DirectX only in parser tests. They cover bounds, newline variants, colors, Unicode preservation, long tag sequences, layout, console editing, resource caps and focus-state release. Follow [Standalone playtesting](PLAYTESTING.md#controls-and-text) for the remaining visible checks.
