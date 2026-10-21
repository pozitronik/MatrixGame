# Robot movement through the game loop

`game.loop.speed_movement` and `game.loop.focus_movement` combine production `L3GRun`, `L3G_WndProc`, `CFormMatrixGame::Keyboard`, `CFormMatrixGame::Takt` and the synthetic battle world. They extend O8 and the shared C7 timing contract. The clock and message source use the [loop platform seam](85-loop-scheduler.md); the tests do not calculate a separate simulation timestep or call `Session::advance`.

Run both configurations with cheats disabled and enabled:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.loop\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.loop\.'
.\tools\test.ps1 -Configuration Debug -Cheats -Filter '^game\.loop\.'
.\tools\test.ps1 -Configuration Release -Cheats -Filter '^game\.loop\.'
```

The helper reconfigures the ordinary build directory for the selected cheat setting. Rebuild without `-Cheats` before using that directory as a normal playable candidate.

## Scenarios and assertions

Each scenario uses seed 85, both Park-Miller and legacy CRT RNG modes, and initial frame intervals of 10 and 16 ms. The same scenario then runs in a fresh synthetic session and must reproduce every recorded frame snapshot. These are deliberately variable loop schedules, not fixed-step reference profiles. Snapshot prefixes count loop draws; the fixture's fixed-step frame counter is not advanced.

The fixture directly selects two friendly robots and marks the current selection as robots before issuing the production context Move to world position (500, 300), resolving to destination cell (48, 28). Selection construction is a precondition, not mouse-selection coverage. Both robots must remain alive, advance from their initial x position and retain that order and destination when the loop closes. Each delivered form timestep must advance the real map clock by that amount and retain live robots with finite positions and the Move destination. Every simulated iteration must have one draw boundary and snapshot. `WM_CLOSE` must stop the loop before another simulation iteration; the state captured after close dispatch must exactly match the state after return.

| Scenario | Schedule and expected behavior |
| --- | --- |
| `speed_movement` | Type `NEED4SPEED` before a 24.75 ms interval, then again before a 0.75 ms interval. With cheats enabled, the observed form deltas are the initial 10/16 ms, then 99, 100, 100, 0, 100 and 100 ms. The four-speed raw intervals are 24.75/25/50 ms, exercising scaled deltas below/at/above the cap. After switching back, ordinary 0.75/100/200 ms intervals cover truncation and normal deltas at/above the cap. With cheats disabled, the corresponding deltas are 24, 25, 50, 0, 100 and 100 ms; the words do not enable four-speed. |
| `focus_movement` | Hold the Up arrow, deactivate, pass an inactive interval, reactivate and release a fresh Up press. Repeat with and without typing `KEEPALIVE`. Effective KEEPALIVE produces an input-reset event and continued ticks without waits. Ordinary inactivity produces deactivate/activate events, two waits and a capped 100 ms first resumed tick. The key is proven held before the first reset event and clear afterward; the two inactive waits must preserve identical battle snapshots and the initial map time. A fresh Up press is recognized after activation, and the final input latch and map key state are clear. Cheats-disabled builds must follow ordinary inactivity even after the word is typed. |

Typing developer words also passes their letters through ordinary shortcut handling. The fixture deselects units and issues a fresh context Move after each complete word, before the next loop tick. It therefore measures the subsequent scheduling behavior and does not claim that entering a cheat preserves an existing order. The trace compares actual robot, order, map-clock and RNG observations across fresh runs; it does not assert that the cheats-enabled and cheats-disabled trajectories are identical.

## Rendering and lifecycle boundary

The form's draw override updates the CPU camera view and captures state; it does not present a native frame. The fixture has no default-pool presentation resources, so its deactivation override temporarily clears the video-resource-ready flag while calling the real form event handler. Input reset is exercised; GPU resource release/recreation is not. Device recovery contracts and native focus validation cover that separate boundary.

Local negative controls omit the real form's map tick and retain held keys during focus reset. The moving-battle cases reject both at the per-frame clock and inactive-key assertions; sources are restored before the passing suites.

These tests construct and destroy the synthetic battle fixture. They do not establish `CGame::Init`, storage-backed restart, game-owned cleanup, the native message pump, minimization or cursor confinement. The separate [Restart-after-toggles case](85-loop-restart.md) combines the loop with O6 storage reloads, measuring four-speed after reload and retaining KEEPALIVE without subsequent focus messages. Full initialization and shutdown after toggles remain separate O7 lifecycle evidence; successful movement here does not complete O8 by itself.
