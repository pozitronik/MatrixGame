# Production-path test harness: feasibility and design

This report addresses [the fixture investigation](https://github.com/pozitronik/MatrixGame/issues/77). It recommends an in-process scenario runner linked to `MatrixGameInternal`, with a separate native/resource-backed lane. It does not propose replacing the simulation or exposing an external transport before the whole-game baseline. The source reference is `540251d4662c590bad4684df01e0c521e6c4aee7`; the accompanying probe adds tests only.

## What the experiment establishes

[`player_command_probe.cpp`](../../tests/player_command_probe.cpp) constructs a production map, side, interface list, form and mesh-free robot. Fixture setup marks the robot alive and selected. The scenario then calls `CFormMatrixGame::Keyboard` for the configured default `U`, `C`, `U` actions, with a release after each press. No test writes the resulting logic-group order. The production form checks its input context, converts the selected group through `SelGroupToLogicGroup`, changes interface mode flags and calls `PGOrderAutoAttack` or `PGOrderAutoCapture`.

The checks observe order replacement, group population, null explicit target, sentinel destination, selection identity, input release and mutually exclusive mode flags. A second case starts a move preorder and verifies that an auto-attack key cannot bypass that input context. A third repeats the command scenario three times in one process and checks that map/heap/data/interface globals and the logic list are empty after each session. All assertions remain active in Release; Debug also checks tracked engine allocations.

This is a command-acceptance experiment. The robot is intentionally mesh-free, audio is unavailable, the diagnostic overlay uses the existing test substitute, and no map tick or autonomous execution occurs. The selected group is fixture setup, not evidence for production picking. The probe therefore cannot establish movement, target choice, battle determinism, sound RNG consumption or full game startup. Its value is demonstrating that a real guarded command route can be tested without rewriting it or creating a device.

Run the probe through the supported helper:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.command_probe\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.command_probe\.'
```

## Relevant production paths

| Boundary | Source evidence | Consequence for fixtures |
| --- | --- | --- |
| Keyboard/UI intent | [`CFormMatrixGame::Keyboard`](../../MatrixGame/src/MatrixFormGame.cpp) checks console/dialog/control/order mode; auto orders also update interface flags | Calling `PGOrder*` alone bypasses part of the human contract. Retain a form-level test for every supported semantic command |
| Explicit/context target | [`OnLButtonDown`, `OnRButtonDown`](../../MatrixGame/src/MatrixSide.cpp) use traced target, minimap conversion, ownership/type checks and robot footprint offsets | A supplied world point is not already a valid movement-cell destination. A future adapter must preserve conversion, validation and flyer handling |
| Selected versus logic group | `SelGroupToLogicGroup` recounts live robots, finds a free logic slot and reassigns selected robots | A logic-group array index is not a stable external entity/group ID; observe membership and intent, not assumed slot persistence |
| Immediate command effects | `PGOrderMoveTo` clears route state, assigns places and invokes `TaktPL`; auto-attack/capture accept intent without the same immediate tick | A uniform new command scheduler can change behavior. Record dispatch and subsequent simulation separately |
| Autonomous execution | `TaktPL`, `WarPL`, `RepairPL`, [`CMatrixRobotAI::LogicTakt`](../../MatrixGame/src/MatrixRobot.cpp) | Observe intent and actual action independently. Do not substitute a test AI or ideal route |
| Time advancement | [`CMatrixMapLogic::Takt`](../../MatrixGame/src/MatrixLogic.cpp) increments map time, splits object updates by `LOGIC_TAKT_PERIOD`, catches side logic up against `m_TaktNext`, and runs map presentation | Calling `robot.LogicTakt(100)` is a component check, not a battle step. Frame-size changes may alter update order |
| Device integration | form `Takt` performs pending display changes and lost-device handling before map update; [`PrepareMap`](../../MatrixGame/src/MatrixMapPrepare.cpp) uses `g_D3DD` at its start | A null device is not a supported substitute for full preparation or the full form loop |
| Geometry dependence | `MoveByMovePath`, `LowLevelMove`, `WallAvoid`, robot weapon/animation paths and [`CMatrixRoadNetwork`](../../MatrixGame/src/Logic/MatrixRoadNetwork.cpp) | Movement grids, road regions, collision geometry and weapon attachment transforms need real fixture data; skipping draw calls does not remove these requirements |
| Lifetime | [`CommandWorld`](../../tests/command_world.hpp), [`bomb fixtures`](../../tests/bomb_order_tests.cpp), map/robot/interface destructors | Keep globals valid throughout dependent destruction. Matching constructors and engine heap ownership matter even without graphics |

One concrete lifetime constraint surfaced while assembling the probe: destroying the interface after clearing `g_IFaceList` crashes the synthetic fixture. The passing probe destroys it while the global still points to that interface, then restores the previous pointer. This was an invalid fixture teardown order, not a demonstrated new game defect. The runner must make such ordering explicit and test partial setup as its capabilities grow.

## Recommended runner structure

Keep the runner small and local to the test target. Its proposed responsibilities are:

1. **Scenario specification:** versioned synthetic topology, robot definitions, sides, initial resources, command sequence, reference identity and explicit execution profile. Setup ends before the first command observation. Tests may build initial state, but may not teleport a unit or edit its order to satisfy an expected action.
2. **Session fixture:** owns heap/configuration/cache/map/interface/form and registered entities with production construction/destruction. Only one session may occupy process globals at a time. Use one process per independent CTest case and additional same-process repeat cases; do not parallelize engine sessions inside one process.
3. **Command driver:** initially use existing form/UI entry points. A later narrow semantic dispatcher must share existing validation and mutations with human input and have paired route-equivalence tests. Test-only state assignment is not that dispatcher.
4. **Advancement driver:** runs the agreed production boundary and schedule, with a work/step limit and timeout. Final choice depends on [time/RNG research](https://github.com/pozitronik/MatrixGame/issues/76). It must report unsupported advancement rather than silently substituting a different loop.
5. **Observer:** copies semantic state at documented boundaries without loading geometry, allocating effects, drawing, playing sounds or consuming RNG. It produces value snapshots; it never lends raw engine pointers to clients.
6. **Comparator:** reports the first divergent command/boundary and field, using the [behavior catalog](https://github.com/pozitronik/MatrixGame/issues/75). Exact state, measured numerical tolerance and event-order assertions are separate comparison modes.

Start with code-built fixtures using the real map/side/robot constructors and plain test specifications. A general scenario scripting language is unnecessary for the first case. An executable can accept a scenario name, manifest path and bounded output location through the existing test-runner conventions. External API encoding must not become a prerequisite for running these CTest cases.

### Ownership and repeatability

Use a fixture state guard established before fallible initialization. Capture only globals the fixture is permitted to replace; reject an already-active session. Match `HNew` with `HDelete` for objects whose production construction depends on cleared storage. Do not generalize `make_unique` to those types just because one existing map fixture uses it. Synthetic grid/road allocations must follow their production destructor contracts; a stack array borrowed by an owning map field is unsafe.

Teardown must stop input and detach the form while its dependencies live, remove selections/borrowed references, destroy entities and effect owners, destroy the map/side/interface in a verified dependency order, clear helpers/cache/audio, and finally restore globals. The exact order varies by fixture capability: the current probe explicitly removes group membership and deletes its robot and interface before the map-owned side. Do not claim that this proves the complete `CGame::SafeFree` sequence. Exercise failure after each acquired capability in the reusable runner, then a successful session and a second cleanup.

Record command and observation IDs in test-owned state. For future external identities use a session identifier plus a monotonic entity registration number (or generation-tagged slot), never a pointer or logic-group slot. Register at a lifetime boundary and retire on removal; scanning pointers once per frame cannot detect destruction and address reuse between scans. Local fixtures can initially assign names to owned entities and invalidate them at their explicit destruction points. This needs no production entity-layout change.

### Command/result correlation and observations

A driver assigns a monotonically increasing command sequence before dispatch. Record the input context and pre-state, the selected production route, dispatch result and post-state. Current `PGOrder*` functions generally return `void`; observing a changed group state is evidence of that transition, not a new engine acknowledgement protocol. Do not report an invented reason for rejection. Where exact acceptance cannot be distinguished from a legitimate no-op, describe the result as observed/no transition until a reviewed shared boundary can report it reliably.

Snapshots should contain session and boundary IDs, map time, scenario entity identity/type/side/alive state, position, semantic order and phase, target identity, group membership, resource values, production queue, capture state and result status as supported by the fixture. Separate diagnostic fields from player-visible fields. Keep arrays in source order where traversal affects decisions.

Snapshot differences can prove a change between two boundaries; they cannot recover a transient shot, create/delete pair or overwritten command between them. Use minimal test observation hooks for those events only when needed, preserving call order and avoiding work when disabled. The observer must not query lazy `RNeed`/mesh accessors as if they were read-only. Compare RNG state/draw evidence before and after observing when the timing investigation makes it available.

## Minimum seams before the global baseline

The accompanying feasibility probe needs **zero production changes**. Reuse it and the existing bomb/input/lifecycle fixtures first. Introduce the following only in separately reviewed increments when the first integrated scenario demonstrates the need:

| Candidate seam | Allowed purpose and preservation evidence | Forbidden expansion |
| --- | --- | --- |
| Test-owned initial world builder | Supply valid synthetic grid/road/side/unit state; validate counts, ownership and known geometry queries before a scenario | Reimplement navigation or bake expected routes into the fixture |
| Narrow command dispatch extraction | Share a current handler body between native input and structured tests; compare guards, flags, targets, coordinate conversion and RNG draws before/after | Skip human validation, change when commands execute or redesign order ownership |
| Read-only transition observation | Capture otherwise unobservable events at the same production point; prove disabled/enabled runs preserve results and draw order | Add a second event-driven game logic or change object lifetime |
| Clock/presentation boundary selected by time/RNG evidence | Inject the minimum agreed observation/control point, with reference/candidate comparisons | Fixed-step conversion, split RNG streams, wholesale headless engine extraction |

The first implementation leaf must enumerate its actual seams before editing production code. A seam being described here is not approval for every item in the table. If safe synthetic execution requires substantial architectural separation, stop and report the concrete dependency; broaden synthetic resources or retain the native lane rather than weakening the global gate.

## First integrated scenario

Proposed fixture: a 64 by 64 movement-cell map, one friendly group of two compatible robots, one passive target and a solid obstacle separating start and first destination. Choose an obstacle at least one robot footprint wide, with a verified traversable corridor around it. Supply consistent movement cells, zones/regions/places, collision data and height values; validate the fixture itself with production traversability queries. The exact synthetic mesh/weapon data is an implementation deliverable, not supplied by the probe.

1. Start at a named boundary with a fresh manifest and selected friendly group. Issue a move through the production player route to a point on the far side of the obstacle.
2. Observe accepted group intent and assigned destinations. Advance through the production schedule. Require actual position progress and a route around the obstacle; compare measured trajectories/places with the reference. Do not assert a guessed optimal route.
3. Before arrival, submit a replacement move to a reachable point on the original side. Observe that the new intent governs subsequent evaluation and that the other group, if present, is unchanged. Preserve existing intermediate avoidance/formation behavior.
4. Submit a supported object-target order, then remove the target through a production lifetime operation before its next evaluation. A diagnostic scenario action may trigger destruction, but it must be labeled separately from a player command. Verify target retirement, no stale-reference access, the measured fallback order and continued bounded execution.
5. End the session and repeat it in the same process. Require no surviving entities/groups/input state or tracked Debug allocations. Repeat with both RNG modes and the agreed Debug/Release profiles; do not compare different RNG modes to one golden result.

The scenario has three distinct requirements: obstruction, replacement and removal. The existing bomb test covers a piece of target removal; the new probe covers guarded acceptance/replacement of auto-order intent. Neither is a substitute for this integrated scenario, and this report does not mark it complete.

## Alternatives and recommendation

| Approach | Benefit | Cost/limit |
| --- | --- | --- |
| Direct unit/order-method tests only | Fast, already feasible, useful local contracts | Miss player guards, scheduling, geometry and cross-system effects |
| Screenshot/click automation only | Exercises a complete native build | Slow, weak state oracle and difficult failure diagnosis; retain for a small native lane |
| Resource-backed in-process runner only | Uses real meshes/maps and preparation | Private assets block public reproducibility; requires device/setup evidence |
| Full headless engine extraction first | Could simplify broad scenarios eventually | Structural work before the required baseline; may change RNG/timing and ownership |
| Incremental synthetic production runner plus native lane | Builds on working fixtures, exposes dependencies one at a time, keeps public checks reproducible | Must prove synthetic geometry and every narrow seam; cannot promise all battle logic is currently device-free |

Use the final approach. The executable probe supports its first step, not its eventual completeness.

## Validation and decisions still open

The three new probe cases pass locally with the pinned MinGW x86 Debug and Release configurations. The complete engine suite passes all 175 cases in each configuration, including the three added cases. Debug includes tracked allocation checks. MSVC and DLL configuration coverage belongs to CI; no native battle or playtest was launched by this investigation.

Before claiming the controlled-scenarios implementation, reconcile this design with the accepted behavior catalog and the time/RNG report. Required outputs from the latter are the exact advancement boundary, frame/logic schedule rules, effects/audio RNG treatment, seed/state capture and justified repeatability limits. Until then, the fixture design is reviewable but its timing contract is provisional. The external API should reuse these command/observation concepts without exporting fixture mutation or making transport a test dependency.
