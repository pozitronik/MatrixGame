# Real loop scheduling contracts

`game.loop.*` executes the production `L3GRun` loop with a scripted clock and message source. The new overload accepts a synchronous `Loop::Platform` containing four callbacks: read time, poll, dispatch and wait. The existing parameterless entry point supplies the same clock and Windows API calls as before. Tick calculation, flag checks, reminder updates, form dispatch, drawing and telemetry remain in the single loop implementation.

This is a narrow test seam for O8. It does not change the scheduler, add a gameplay control API or waive the complete behavior-baseline gate. Every callback must be valid for the entire call. The platform object and context are borrowed; no global override or retained callback survives the run.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.loop\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.loop\.'
```

## Cases

| Case | Contract |
| --- | --- |
| `timing` | Preserve submillisecond truncation, normal elapsed values below/at/above 100 ms, and four-speed multiplication before truncation and the 100 ms cap. Toggle four-speed on and off within one loop. A 24.75 ms interval becomes 99 ms at four-speed, distinguishing the multiplication order. |
| `activation` | Deliver deactivation/reactivation through `L3G_WndProc`. Ordinary inactivity waits without ticking or drawing, retaining the last active timestamp; the first resumed 250 ms interval caps at 100 ms. KEEPALIVE retains active ticking and emits the input-reset event. |
| `exit_boundaries` | A preexisting exit request prevents the first tick/draw. A request raised inside the form tick allows that iteration's draw and memory query, then stops before another tick. |
| `missing_form` | An active application without a current form waits and never reads a frame timestamp or draws; a dispatched close ends the loop. |
| `native_exit` | The parameterless entry point uses its normal platform callbacks and honors a preexisting exit request without invoking the form. |

The timing and activation cases dispatch `WM_CLOSE` through the real window procedure and verify that the loop observes `GFLAG_APPCLOSE` before another simulation tick. The synthetic queue is bounded; failure to terminate is a test failure rather than an unbounded wait. The no-render device counts texture-memory queries and reports a fixed 64 MiB for telemetry. The test does not model native memory availability or freeze the separately timed FPS counter.

## Limits

These cases use a recording form to isolate the scheduler. Speed toggles are injected application flags, not typed cheat dispatch. The window procedure receives scripted messages without a real window; focus delivery, minimization and the real form's held-key release remain separate contracts. No robot movement, map reload, full initialization or production cleanup is claimed here. Follow-up O8 scenarios must attach the real game form and combine this loop with O6/O7 fixtures. Native Alt+F4 and focus observations remain in the deferred validation handoff.
