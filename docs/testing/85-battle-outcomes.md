# Capture and battle outcome characterization

The `game.outcome.*` suite adds production-backed checks for capture arithmetic, income ownership, battle results, pause and repeated synthetic sessions. It reuses the [controlled battle harness](../TESTING.md#controlled-battle-scenarios) and makes no production changes. The [behavior catalog](../research/75-behavior-baseline/README.md) remains the complete acceptance contract; the gaps below prevent treating this increment as a finished outcome baseline.

## Running the checks

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.'
```

Assertions remain active in Release. The shared fixture bounds map advancement and restores its declared globals; CTest bounds each process. Debug additionally rejects leaked tracked allocations. The normal CI matrix supplies the MSVC and DLL build variants. These tests run without external resources or a native window.

## Capture boundaries

`neutral_capture`, `enemy_capture`, `capture_rejection` and `capture_rollback` invoke the actual `CMatrixBuilding::Capture` and, where required, `LogicTakt`. The nearby capturing robot has the shared fixture's real generated mesh and map membership; distance eligibility goes through production `FindObjects`, not a substituted answer. Side colors, the building's original ownership/color and its titanium income period are explicit initial data. Neutral and enemy capture each run independently under ParkMiller and LegacyCRT with seed 79.

The building is a component fixture rather than an object scheduled by the map: the test owns it and supplies its capture and logic calls explicitly. Its automatic capture-search timer is deferred and its capturer marker is initialized, so those independently scheduled acquisition paths cannot add calls to a boundary test. The real robot command that travels to and captures a registered building is not exercised. No unit orders or capture results are written to obtain the expected transition.

With the default 750 ms erase and 500 ms paint periods, neutral capture at time zero is busy. The first point appears at 10 ms; equality at 500 ms remains busy and the next point appears at 510 ms. Fourteen painted points at 6,510 ms still leave the building neutral; ownership changes on the subsequent 7,010 ms call. Enemy capture keeps the old owner when its final colored point is erased at 9,760 ms, then neutralizes the building at 10,510 ms before repainting. Assertions distinguish count/color changes from ownership changes rather than treating a fully painted or erased bar as the completed transition.

Income checks call the real building logic at 999 ms and one further millisecond with a 1,000 ms period. Only the new owner receives titanium. The neutral interval in enemy capture does not pay the previous owner. These are component timing checks, not an income replay across the complete capture journey.

Rejection checks pass the farther friendly robot to the actual distance search, confirm no paint, then let the eligible robot paint. A second call at the same boundary is busy. Rollback checks stop calling `Capture`, exhaust its 1,250 ms keep-alive interval through building logic, verify that equality with the rollback clock does nothing, and advance the real map clock before checking removal of the point. The deliberately supplied building elapsed intervals isolate timeout arithmetic; they do not represent normal full-map scheduling or the production death/interruption route.

## Result and pause boundaries

`victory`, `defeat` and `simultaneous_loss` delete the relevant registered robots through the shared fixture and run the complete `CMatrixMapLogic::Takt`. Deletion bypasses damage and dying animations; the tests establish the subsequent live-entity outcome evaluation. They assert the player's side status, the win flag, the delayed dialog identity, pause ownership and absence of premature exit. Simultaneous loss follows the player-death branch and produces defeat.

The status scheduler compares `now - previous > 1001` but then stores `previous = now + 1001`. Consequently the first result check at 10 ms is followed by the dialog at 2,020 ms in this profile. Tests assert that no result dialog is present at 2,010 ms. This preserves the observed interval despite the source comment describing a once-per-second check.

Dialog setup uses an actual empty `CInterface`, owned by the normal interface list, plus an empty template block. Production dialog state, pause and teardown therefore execute without loading artwork. No confirmation button or rendered hint is created: opening and leaving a dialog do not prove its confirmation callback, statistics screen or visible appearance.

`automatic_results` supplies `MMFLAG_FULLAUTO` and verifies exit state 1 and loop termination for both victory and defeat, with no result dialog or pause. `full_auto_escape` exercises Escape through the form under that same flag. These cases isolate full-auto result routing. They do not set `MMFLAG_AUTOMATIC_MODE`, construct a demo map or prove that complete high-level player AI and flying-camera startup work together.

`special_survival_flags` supplies each of `MMFLAG_TERRON_DEAD` and `MMFLAG_SPECIAL_BROKEN` as initial conditions, removes both player robots and verifies that ordinary side elimination is suppressed. It does not exercise the earlier Terron/special-object destruction that produces these flags or establish the complete special-map victory sequence.

`pause_resume` moves through the production command route, pauses using the form's Pause key, and checks that robot position, simulation time and reinforcement cooldown remain unchanged while paused. It resumes, verifies the first clock/cooldown decrement and requires arrival. Both RNG modes run independently. The suite does not infer that presentation, audio, camera or RNG activity stops merely because these gameplay clocks stop.

## Statistics, cheats and repeated sessions

Side statistics use `timeGetTime`, independently of the fixed simulation clock. Defeat checks verify that the eliminated player's recorded time stays frozen. Victory checks bracket the final tick with native clock reads and verify that the stored negated duration lies inside that observed interval, including unsigned clock arithmetic. This establishes the sign-changing result operation without claiming a deterministic numeric timestamp. Exact reference traces for statistics still need an agreed clock-observation policy or a separately reviewed clock seam.

`legacy_loss_cheat` feeds IAMLOOSER through the production cheat parser. It preserves exit state 3, the win dialog, paused play, an unchanged statistics time and an unset `MMFLAG_WIN`. It calls the parser directly in either compiled variant; enabled/disabled keyboard dispatch is a different command-suite contract. Confirmation of this dialog and the later statistics state remain uncovered.

`repeated_sessions` alternates full-auto win/loss outcomes and constructs fresh synthetic sessions in the same process. It asserts cleared engine globals/live objects, fresh clocks, default selection/group membership, active player status and no carried pause or loop-exit flag. This tests the shared fixture's declared ownership and the production objects it owns. It does not exercise `CGame::Init` on a loaded map, the restart menu or arbitrary process-global startup/teardown.

## Catalog mapping and remaining blockers

| Catalog entry | Executable evidence | Required follow-up before baseline acceptance |
| --- | --- | --- |
| OUT-01 | Neutral/enemy capture, strict erase/paint/ownership thresholds, distance rejection, caller interruption timeout, rollback and income owner. | Full robot capture order, competing capturers/colors, protected bases/turrets, reassignment or death during capture, production acquisition and rollback scheduling, resource queues on ownership transfer. |
| OUT-02 | Last-robot victory/defeat, simultaneous loss, exact delayed result boundary, isolated full-auto exit, supplied special-outcome survival flags, statistics freeze/sign. | Last-base versus remaining non-base units, actual special-object/Terron destruction and map rules, full demo profile, confirmation/result/statistics callbacks, quit/restart during pending result and complete native evidence. |
| OUT-03 | Pause/resume movement/time/cooldown, full-auto Escape, repeated outcome and fresh synthetic-session construction. | Loaded-map restart and startup failure, menu restart, settings pause/key ownership, alternate legal frame schedules and complete resource-backed session lifecycle. Existing `game.lifecycle.*` and `game.scenario.partial_setup` retain their stated narrower coverage. |
| MODE-01 | IAMLOOSER's initial dialog/result/statistics quirk. | Confirmation/statistics continuation, reinforcement cheats and Debug F, actual four-speed scheduling, keep-alive timing, other cheat lifecycle effects and `_TRACE` result paths. |
| PRES-04 | Pause's effect on selected gameplay clocks only. | Settings-driven pause, input capture, Apply/Cancel and restart-only settings; owned with the presentation suite. |

These cases assert specific outcomes and do not provide persisted historical reference traces. Complete baseline acceptance still requires the agreed manifest, reference comparison, integrated Debug/Release/compiler/DLL evidence and all remaining core catalog variants. No structural gate is waived by this suite.
