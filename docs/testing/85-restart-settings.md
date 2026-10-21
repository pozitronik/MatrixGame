# Saved settings across a battle restart

The EXE-only `game.restart.saved_settings` case covers the O6/P4 distinction between reloading the current battle and starting a new game process. It combines the production settings persistence/runtime functions with the real menu-confirmed storage Restart. The [settings-window integration tests](87-presentation.md#settings-input-pause-and-persistence) separately cover editing, input, Apply/Cancel and save failures.

The synthetic battle starts with camera speed 2, high-quality skybox configuration and the selected RNG mode. The test saves camera speed 125%, low-quality skybox and the opposite RNG mode in an isolated `CFG/standalone.txt`, then calls the same successful runtime-application boundary used by the settings window. The immediate camera setting becomes 2.5. The active skybox configuration and RNG mode remain unchanged, while the saved values and restart-pending indicator reflect the requested options.

For both supported RNG modes and 10/16 ms map steps, the test issues a Move and confirms Restart through Escape, R and Enter twice in the same map instance. Each reload must replace the robot, open the paused introduction, retain map time and RNG draw count, preserve the settings file, and keep the camera speed at 2.5. Skybox quality and active RNG mode must still have their initial values; the restart-pending indicator must remain set. Dismissing the introduction and issuing a fresh Move must produce movement without changing those settings.

This is the current contract: menu Restart reloads battle dynamics and does not reapply process-start settings. The test does not call `initialize_runtime` again to manufacture that result. In particular, saving a different RNG mode does not reseed or switch the running generator during a map reload.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.saved_settings$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.saved_settings$'
```

The fixture generates its dynamics and hint pixels and uses the existing synthetic device. It checks configuration values, persistence and gameplay continuation; it does not render a skybox or measure native camera input. Full standalone initialization applying the saved options remains the separate O7/P4 obligation. A successful map-reload check cannot establish that process boundary.

The fixture creates a unique temporary deployment and saves only its synthetic `CFG/standalone.txt`. It verifies restoration of the previous working directory and deletion of the deployment after each successful profile and after an injected exception following a real save. Failure diagnostics also pass through those restoration checks; the caller's configuration and supplied packages are not rewritten.

A temporary Release mutation that applies the saved skybox during map Restart fails the post-reload effective-settings assertion. Restoring the production source restores the pass. Applying saved startup options through actual full initialization remains untested here; existing settings component tests separately establish their startup mapping and pending-state calculation.
