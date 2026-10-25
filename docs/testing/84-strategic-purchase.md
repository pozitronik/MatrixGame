# Strategic waiting, purchase and deployment

Three `matrixgame_production_scenarios` cases extend the A7 purchasing decision through actual income, payment, assembly, queue ownership and base exit. They use the production `BuildRobot`, constructor, building income, queue, robot spawn and movement implementations. Both RNG modes run with seed 83 and separate 10/16 ms frame schedules, without supplied assets or an interactive window.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.production\.strategic_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.production\.strategic_'
```

## Decision and income boundary

The synthetic catalog contains wheeled Birex designs with one or two repair weapons. Configured prices are 60 per weapon in each resource; other modules are free in this fixture. Configured strategic strengths are 50 and 100. Each resource starts at 90, making the weaker design affordable immediately and the stronger design worth waiting for under the existing strength comparison. The weaker definition is supplied first, so the normal parser must sort and price these definitions. The one-candidate wait lottery consumes one draw; subsequent waiting and its eventual purchase consume no further selection draws.

The fixture calls `PGCalcStat` and then the real `BuildRobot` directly on the player side. This is an explicit strategic component boundary, allowing the existing manual side to remain free of automatic tactical commands. It does not exercise the AUTO activation route or claim to reproduce the cadence of a computer side's complete high-level loop. Every intervening frame uses `Session::advance`, including full map logic, production building income, queue processing and movement. No resources are injected after setup, and no robot is attached to the queue or deployed by the fixture.

The base pays three of each resource at its configured 100 ms income boundary. Income resets its accumulator to zero rather than retaining overshoot. Ten 10 ms frames therefore give one payout. A 16 ms frame contains separate 10 and 6 ms object ticks, giving two payouts per thirteen frames. The tenth payment makes all balances 120 at 1000 or 1040 ms, respectively. Purchase spends exactly 120 of each resource, clears the waiting index, updates the previous-design index and leaves the assembled robot owned by the base's queue. Calling the strategic method again with the unchanged zero balances preserves that same queued robot and the balances. This observation does not isolate the queue guard; the existing ai_purchase case pins that guard with funds available.

The base and robot models are synthetic graph nodes, not loaded building files. The closed base starts at floor zero, matching the existing deployment fixture rather than the packaged initial floor of 0.2. Its income billboard point is initialized explicitly. Repair models include both attachment matrices required when their production update runs. Robot icons use the declared unavailable-target device profile; audio is unavailable and no draw is claimed.

## Registered cases

| Case | Required observations |
| --- | --- |
| `strategic_deployment` | Ten actual income payments; retained stronger design; exact debit; both assembled repair modules; unchanged queued identity on another purchase request; one robot-build statistic; production spawn, opening, physical exit and closing; released base pointer and manual-control restriction; exact final balances and four total draws over the declared interval |
| `strategic_wait_base_removed` | Delete the only base immediately after selection, before payment. Another 100 frames and repeated strategic calls leave balances and build statistics unchanged, with no additional draw. The waiting design remains stored because the capacity and no-base guards both precede cancellation; it is not a pointer to the deleted base |
| `strategic_paid_base_removed` | Delete the base after payment but before queue completion. Queue destruction refunds exactly 120 of each resource. Another 100 frames and strategic calls preserve the refund, purchase history and zero build count; no unit is deployed and the waiting index remains cleared |

The base-removal cases call the actual `StaticDelete` as an explicit lifetime intervention. Test-owned core references distinguish actual base and queued-robot retirement from an unchanged build statistic, and are released before session teardown. The successful case separately verifies the same queued robot remains live and player-owned after physical deployment. They do not simulate lethal weapon damage or neutralization. The waiting case intentionally preserves the dormant design; changing that behavior would be a separate gameplay decision. Existing `production_wait_limits` tests retain coverage of cancellation when a design exceeds the income forecast.

## Deployment clock

The following map-clock observations are checked in both RNG modes. Opening and closing use the existing `BASE_FLOOR_SPEED` of 0.0008 per millisecond. Exit uses actual `ROBOT_BASE_MOVEOUT` movement and must cross the production `BASE_DIST` threshold before the base pointer and manual-control restriction are released. These timestamps describe this synthetic geometry and movement profile, not universal game loading times.

| Frame schedule | Paid and queued | Spawned | Base open | Robot detached | Base closed | Each final resource |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 ms | 1000 | 1100 | 2340 | 4590 | 5840 | 144 |
| 16 ms | 1040 | 1152 | 2400 | 4656 | 5904 | 138 |

The final balances include continuing base income after purchase. The four-draw total includes the decision and subsequent production activity; only the first draw is attributed to choosing the design. The suite checks the decision interval separately, so a changed selection lottery cannot be hidden by later activity. Debug's tracked-allocation check also runs after each selected test process has torn down its sessions.

This completes the selected A7 component trajectory. It does not establish native presentation, audible production feedback, a complete autonomous opponent or acceptance of the whole-game behavior baseline.

[Strategic-production recordings](strategic-production-recordings.md) document the finite JSONL inventory, ordered observations, strict consumer and independent reference comparison.
