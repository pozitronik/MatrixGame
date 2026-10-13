# Strategic and tactical AI contracts

The `game.ai.*` suite includes thirteen decision and memory cases for [the AI behavior baseline](https://github.com/pozitronik/MatrixGame/issues/84), in addition to the earlier robot-definition checks. They call production algorithms and use generated geometry where a decision needs positions, visibility or navigation. They do not replace the AI or change its priorities. The complete catalog remains open: the uncovered scenarios below are baseline blockers.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.'
```

## What the tests establish

| Catalog | Executable cases | Assertions |
| --- | --- | --- |
| AI-01: strategic choices | `action_priorities`, `action_ties` | `CompareAction` ranks attack above forward/capture/intercept, retreat above capture/intercept and capture above forward. Waiting for a team union changes defence-versus-attack preference; already fighting changes it back. Forward candidates prefer shorter region paths. `BestAction` retains the first candidate on an attack tie, and an empty list remains empty |
| AI-01: grouping | `regroup_thresholds`, `regroup_teams_and_actions` | Production `Regroup` merges distinct groups only below 300 world units, keeps an existing group together through 400 units and splits above that distance. Different teams do not merge. A nearby group with a real action transfers that action to the merged group when the other action is None |
| AI-01: integrated strategy | `strategic_tick` | Twenty 10 ms map frames run `TaktHL` and `TaktTL` for the computer side. Its two unarmed robots share team/group membership, choose Defence in region 0 and receive movement-cell destinations (8, 8) and (48, 8). Both RNG modes use seed 79 |
| AI-02: target memory | `enemy_memory`, `memory_target_retirement` | Slow acquisition deduplicates observations. Enemy-kind lookup respects insertion order. Two missed observations retain a target; a new observation resets the miss count; the third consecutive miss retires it and clears current target references. Production object deletion removes the robot from another robot's enemy list and clears its current target/attack pointers |
| AI-02: reconsideration memory | `ignore_expiry`, `bad_positions` | An ignored target remains ignored at 4,990 ms and expires at 5,000 ms. Re-adding refreshes its timestamp; acquiring it removes the ignore entry. Bad places and coordinates deduplicate and retain entries through their declared capacities |
| AI-02: tactical choice and player intent | `tactical_target_choice`, `tactical_blocked_line` | Production `FirePL` chooses the first equal-distance, unobstructed enemy, then chooses the surviving candidate after target deletion. A friendly robot blocking one equal-distance line causes the open candidate to be preferred; when it is removed from consideration, the blocked candidate remains a valid fallback target. The player group's MoveTo intention remains unchanged. The robots are unarmed, so the checks explicitly require no firing |
| AI-02 / AI-03: order interruption | `interruptibility` | A human-controlled side may interrupt an ordinary capture order, whereas a computer robot may not. AUTOMATIC_MODE alone does not change this condition; FULLAUTO does. Spawn, base move-out and base capture states cannot be interrupted. Removing the capture order restores ordinary interruptibility |
| AI-03: automatic reinforcement gate | `automatic_reinforcement_gate` | A disabled map percentage, nonzero cooldown or ordinary player mode consumes no reinforcement chance draw. Automatic mode at zero cooldown consumes one chance draw per side call, including when no eligible building exists. Seed 79 enters the building scan on its first draw in both RNG modes. No carrier appears; FULLAUTO alone does not enable this scan, and the side method does not decrement the map cooldown |

## Fixture boundaries

The shared `Session` retains its wheel-only synthetic map, active sides, fake audio service and storage-only D3D device. It uses seed 79, controlled map time and the declared no-draw floating-point profile. The integrated strategy case runs both RNG modes at 10 ms; it is not a resource-backed campaign or a native rendering check.

The priority tests populate a public candidate list and call the real comparison and reduction methods. `TaktHL` first initializes the real regional observations where they are needed. This characterizes the ranking contract without pretending that the fixture generated its candidates through a complete strategic expansion. The grouping tests establish initial positions, team IDs and group IDs before calling `Regroup`; they do not teleport robots during a battle. They assert membership and retained action, not the validity of every cached group count.

The tactical tests add synthetic enemies at known positions, then populate the production `CInfo` lists as their initial observations. `FirePL` performs its real nearest-target and friendly-line checks with production geometry. These tests do not establish how `GatherInfo` first discovers those enemies, fog-of-war equivalence, an armed firing solution, hit probability or damage. Selecting a blocked fallback target does not mean that the engine is allowed to fire through a friendly robot. The observed MoveTo order, selected target, lack of firing and the triggering test boundary are recorded in JSON decision records in CTest output. Trigger names describe the supplied observation or deletion; they are not claimed to be an internal reasoning log.

The memory tests distinguish current target pointers from historical fields. They never dereference a retired object. Ignore timing uses a separate production `CInfo` under the advancing session, so autonomous `GatherInfo` cannot refresh that particular test list. Capacity-overflow eviction is deliberately excluded: the current implementation uses overlapping `CopyMemory`, which requires separate defect handling before a portable expectation can be accepted.

The automatic-mode test uses a small test-only map subclass to set protected initial map inputs: reinforcement percentage and remaining cooldown. It is allocated with `HNew` and destroyed through its exact derived type. The fixture has no topology, robots or buildings and calls the production player-side `LogicTakt(10)` directly. This is a gate and RNG-consumption contract; it does not advance map time, load a demo map or demonstrate delivery. The seed probe verifies that the first chance draw enters the scan, then reseeds before the tested calls. No production access seam was added.

## Remaining baseline work

AI-01 still needs multi-region expansion and threat comparison, strategic production under scarce resources, competing reachable/unreachable objectives, uneven-team redistribution, and base/leader/objective loss. The current one-region Defence case provides a repeatable integrated starting point, not coverage of those strategic choices. Group-cache inconsistencies observed during source analysis need a separate reproducer and disposition before exact bookkeeping expectations are frozen.

AI-02 still needs armed opportunistic fire and repair, capture priority against attached turrets, acquisition visibility/range thresholds, repeated failure to hit, resumption of intended movement after a genuine armed detour, and the catalog's bomb-escape placement investigation. These depend on faithful armed/repair fixtures and additional scenario evidence. Directly assigning a target or weapon result would not fill those gaps. Full-capacity ignore eviction and reset/clear reuse also need defect investigation rather than an expectation derived from undefined behavior.

AI-03 still needs eligible-building selection, seeded robot choices, carrier requests and interruption, demo-map startup with AUTOMATIC_MODE/FULLAUTO/FLYCAM together, flying-camera/RNG effects and session transitions. The empty-building gate cannot establish these outcomes. Construction and navigation fixtures must supply the production assembly and carrier lifecycle before their automatic trigger can be tested end to end.

Historical decision references, fresh-process repetition, compiler and DLL comparisons, native evidence and performance measurements remain part of the complete baseline gate. A passing component decision suite is insufficient to authorize structural refactoring.
