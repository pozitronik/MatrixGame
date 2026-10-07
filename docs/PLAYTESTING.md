# Standalone playtesting

Use the Windows x86 toolchain and locally supplied resources described in [Windows builds](BUILD_WINDOWS.md). Run the first pass with cheats disabled in both Debug and Release. Automated engine tests establish specific code contracts; a visible battle and usable controls require this separate check.

## Prepare the configuration cases

Build before choosing a configuration case. The build helper refreshes tracked text files and stages a root-level `robots.dat` when supplied, so another build can restore packed configuration into an output directory.

| Case | Files beside the EXE | Purpose |
| --- | --- | --- |
| Packed configuration | `DATA/robots.pkg`, `CFG/robots.dat`, tracked `CFG/robots/` text files | Check the supplied packed configuration and its interface data |
| Text fallback | `DATA/robots.pkg`, tracked `CFG/robots/data.txt` and `iface.txt`; no `CFG/robots.dat` | Check the tracked standalone configuration |
| Missing resources | Temporarily remove only the output copy of `DATA/robots.pkg` | Check the failure diagnostic and cleanup |
| Invalid configuration | Replace only the output copy of `CFG/robots.dat` with a small synthetic invalid file | Check rejection and cleanup |

Change output copies only. Keep supplied files at the repository root unchanged. Rename an output copy to a clearly named backup before a failure/fallback case, restore it after the game exits, and remove any synthetic replacement. Do not extract or publish the resource package. Run one game process at a time.

## Check an interactive battle

Launch `MatrixGame.exe` without arguments. Record the map named by the active configuration. Dismiss the introductory dialog with its confirmation control before testing commands.

1. Confirm map rendering, interface text and the resource display. Record the configured resolution and Windows display scaling; physical client pixels can differ from the configured resolution.
2. Select a robot, issue a move order and verify movement to the selected ground point. Move several robots through a shared route and observe avoidance rather than testing only a single empty route.
3. Issue an attack against a reachable enemy and verify firing, damage and subsequent selection.
4. Construct a robot and a turret. Inspect a representative AI construction and head price against the configured resource costs; a head must not be valued using the weapon price table.
5. Observe a flyer reaching its target and repeat a command after completion. Distinguish a range/position defect from a blocked route or unsupported target.
6. Close through the interface, then check the window-close path separately. Confirm process termination and that mouse/keyboard use is restored. Record the process exit status rather than assuming that the absence of a window establishes success.

Check sound, pause/resume, focus transitions, manual control and developer-only paths separately where relevant. Repeat affected cases after a correction. Record any limitation or unperformed step explicitly; do not treat startup, compilation or synthetic tests as proof of successful combat.

## Map arguments and failure cases

A command-line map argument selects visibility calculation rather than the interactive battle loop. A bare filename is prefixed with `Matrix\Map\`; a path containing a backslash is used as supplied. Visibility calculation still initializes the game and loads its resources. It can create `calcvis.log` and cache data, so it is not an asset-free unit test or a replacement for the interactive checklist.

For missing/invalid-resource cases, inspect the diagnostic, dismiss any error dialog, and confirm process termination and restored cursor state. Keep these runs bounded. Record whether failure occurred before or after window/device creation and whether the output resource backups were restored. Preserve the smallest relevant log excerpt and stack; avoid publishing configuration dumps or extracted assets.

## Record the result

Use the source revision, compiler and build configuration, cheats setting, configuration mode, map, resolution/display scaling, steps and observations. Include normal and failure exit statuses. A report should separate verified behavior from remaining checks and describe a reproducible failure with the smallest useful evidence.
