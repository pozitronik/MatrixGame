# Testing

Verify changes in both Debug and Release builds:

```powershell
.\tools\check-repository.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

For correctness fixes, add a regression that reproduces the defect before the
change. Use fixed seeds for random behavior and synthetic fixtures for
configuration, storage and command tests. Automated tests should run without
game resources or an interactive window.

For CTest targets, run:

```powershell
ctest --test-dir <build-directory> --output-on-failure --no-tests=error
```

## Playtesting

Launch the standalone EXE without arguments, using locally supplied resources.
Check the affected behavior in Debug and Release with cheats disabled first.

- Start a battle and check map rendering and UI text.
- Select robots, move, attack, and construct robots and turrets.
- Detonate bombs through both the UI and manual control.
- Check sound, pause/resume, focus changes and supported window transitions.
- Finish or exit the battle, then repeat launch and exit several times.
- Test relevant developer commands separately with cheats enabled.

Record the source revision, build configuration, map or seed where available,
steps performed and results. For a failure, include the relevant part of
`test.log` and a debugger stack. Keep resource packages and extracted assets
out of reports.
