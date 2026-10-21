# Changelog

Player-facing changes are recorded here before release. Each release uses a level-two heading matching its tag, for example `## 2026.10.11` or `## [2026.10.11]`. An absent or empty entry falls back to GitHub's automatically generated release notes. See [Preparing a release](docs/RELEASING.md).

## Unreleased

- Install custom maps in `maps/` or `DATA/maps/` beside the executable, with optional same-name JPEG or PNG previews. The original `Matrix/Map/` location remains supported.

- Battle music plays in the standalone game when the original `music.pkg` is copied to `DATA/music.pkg`. The new Music row of the Settings window and `MusicVolume` in `CFG/standalone.txt` set its level independently of sound effects. `music.pkg` is optional.

- Choose a battlefield from a startup menu or New battle in the battle menu. The selector shows previews, optional original descriptions and characteristics, and remembers the map and bonus. Start with the button, Enter or a double-click; a loading screen remains visible while the next battle loads.

- Change display mode, resolution, volume, shadows, sky, camera speed and the random generator from an in-game Settings window in the battle menu (`C`). Changes are saved to `CFG/standalone.txt`, keeping its comments.

- Standalone sound effects now work with `DATA/sound.pkg` alone. Sound mappings are built in; `CFG/sounds.txt` remains an optional override.

- Configure rendering resolution and choose windowed, borderless or exclusive fullscreen in `CFG/standalone.txt`. Use Alt+Enter to switch between windowed and fullscreen modes during play.
