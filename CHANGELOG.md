# Changelog

Player-facing changes are recorded here before release. Each release uses a level-two heading matching its tag, for example `## 2026.10.11` or `## [2026.10.11]`. An absent or empty entry falls back to GitHub's automatically generated release notes. See [Preparing a release](docs/RELEASING.md).

## Unreleased

- Standalone sound effects now work with `DATA/sound.pkg` alone. Sound mappings are built in; `CFG/sounds.txt` remains an optional override.

- Configure rendering resolution and choose windowed, borderless or exclusive fullscreen in `CFG/standalone.txt`. Use Alt+Enter to switch between windowed and fullscreen modes during play.
