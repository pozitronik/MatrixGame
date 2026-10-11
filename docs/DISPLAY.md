# Resolution and display modes

Edit `CFG/standalone.txt` beside `MatrixGame.exe`, then restart the game. These settings apply to the standalone EXE with either `CFG/robots.dat` or the text game configuration. They do not change the DLL host's display settings. Source builds copy this file from `MatrixGame/CFG/standalone.txt` each time, so edit the source copy if you want settings to survive rebuilding.

```text
ResolutionWidth=1920
ResolutionHeight=1080
WindowMode=Windowed
FullscreenMode=Borderless
```

| Setting | Values | Default |
| --- | --- | --- |
| `ResolutionWidth` | Integer from 640 through 8192; the graphics device must support it | `1024` |
| `ResolutionHeight` | Integer from 480 through 8192; the graphics device must support it | `768` |
| `WindowMode` | `Windowed`, `Borderless`, `Exclusive` | `Windowed` |
| `FullscreenMode` | `Borderless` or `Exclusive`, selected by Alt+Enter when starting windowed | `Borderless` |

Mode values are case-insensitive. Missing settings use their defaults. A malformed file, duplicate display key or invalid value resets all display settings to 1024x768 windowed and writes a diagnostic to `test.log`. Random-generator selection is independent. Resolutions below 1024x768 are accepted but may crowd the original interface; 1280x720, 1920x1080 and your monitor's native resolution are useful alternatives to check. Extremely large valid values can exceed the graphics device's capabilities; choose a lower resolution if device creation reports a failure.

The EXE's [application manifest](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process) establishes system DPI awareness before creating windows so primary-display dimensions and pointer coordinates use physical pixels. This does not add interface scaling or per-monitor DPI support, and it does not change a DLL host's DPI policy.

## Modes and switching

**Windowed** creates a movable window with a title bar and a fixed client size. Its client area uses the configured resolution when it fits on the primary display; oversized windows are fitted into the work area and the rendered image is scaled. The window cannot be resized by dragging its border.

**Borderless** covers the primary display without changing the desktop resolution. The game renders at the configured resolution and DirectX scales that image to fill the display. Mouse coordinates are mapped back into rendering coordinates for selection, button clicks and camera scrolling. Choose the monitor's native resolution for a sharp image, or a lower resolution with the same aspect ratio to reduce rendering cost. A different aspect ratio stretches the image; letterboxing and independent interface scaling are not implemented.

**Exclusive** asks DirectX to use an enumerated fullscreen mode at the configured resolution and the driver's default refresh rate. If that resolution is unavailable at startup, the game logs the reason and starts windowed. If it is unavailable when toggling, the current mode is retained. A failed switch attempts to restore the previous mode; a permanent recovery failure exits through the normal DirectX diagnostic.

Press **Alt+Enter** to switch between windowed mode and the configured fullscreen mode. If the game starts in `Borderless` or `Exclusive`, that starting mode becomes the fullscreen member of the pair. Holding the shortcut switches once. The shortcut does not dismiss dialogs or activate Enter's robot-control command. Resolution remains fixed until restart, and toggles do not rewrite the configuration file.

Windowed/borderless switches change only the window; the rendering surface and game resources remain in place. Exclusive switches run between frames and use the existing device-resource release/reset/restore path. Reset receives a copy of the presentation settings because [DirectX modifies that input structure](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset), including its dimensions. Failed resets retain the intended settings for recovery.

Focus loss clears held input, cancels an unfinished selection drag and releases the cursor. Windowed and borderless games pause without forcing minimization; exclusive fullscreen minimizes. Fullscreen modes confine the cursor while the game is active. Moving the pointer outside a window stops pointer-based edge scrolling and cancels a drag without clearing held keyboard keys. F9 captures the rendered image at the configured resolution.

## Verification

Automated `game.display.*`, `game.input.display_shortcut`, `game.input.pointer_left`, `game.input.screenshot_deferred` and `game.selection.display_cancel` contracts cover parsing and file fallback, shortcut routing through the real window procedure, coordinate scaling, selection cancellation, deferred screenshots and reset/rollback ordering. They use synthetic data and no visible game window. Hardware presentation, image quality, cursor alignment and exclusive transitions still require the following playtest.

Test Debug and Release, with cheats disabled, using a supported resolution such as 1280x720. Run one configuration with packed game settings and the other with text settings. Do not rebuild while a test is running.

1. Set `WindowMode=Windowed` and `FullscreenMode=Borderless`. Start the game. Before dismissing the introduction, press Alt+Enter and confirm that it covers the primary display while the introduction remains open. Hold Alt+Enter for two seconds; it should switch only once. Release the keys, then press them again to return.
2. Dismiss the introduction. In each mode, select a robot, drag a selection rectangle, click command buttons and use the minimap. Click near all four sides of the image; the cursor, selection box and buttons must agree. Press `M` and click clear ground to check movement. Plain Enter must still toggle manual robot control.
3. Start dragging a selection rectangle, press Alt+Enter, then release the mouse. The canceled rectangle must not add units. Make another normal selection. Move the window, toggle to borderless and back, and confirm its position is retained.
4. Move the pointer outside the window without clicking. Pointer-based scrolling must stop. Alt+Tab away while holding an arrow key, release it elsewhere, then return. No scrolling or mouse-camera rotation should remain stuck. Windowed and borderless modes must not minimize merely because another application takes focus.
5. Repeat several toggles. Check text, terrain, water, effects and robot models for missing or corrupted graphics. Press F9 once and check that the screenshot contains the whole rendered image. Close with Alt+F4 and record the exit code; normal exit is `0`.
6. Restart with `WindowMode=Borderless` and repeat a toggle round trip, selection and focus loss. Then restart with `WindowMode=Exclusive` at a supported resolution and repeat steps 1–5, expecting exclusive mode to minimize on focus loss. Confirm each launch's actual mode in `test.log`; a logged fallback is not an exclusive-mode pass.

Also check one different resolution after restart. Record the actual modes and resolutions, configuration type, compiler/build, graphics adapter, failures and exit codes. Physical driver resets, monitor hot-plugging and selecting another monitor are outside this feature's validation.
