# Shared crazy-robot history

`game.player.crazy_history.*` links production `CMatrixRobotAI::LogicTakt` and the existing synthetic [Session fixture](../../tests/battle_scenario.hpp). The twenty cases characterize the function-static `ppx`, `ppy` and `cnt` values without exposing or resetting them. Each registered case starts in its own process. Both RNG modes and 10/16 ms robot-call arguments are covered.

## Boundary and initial conditions

The fixture uses the Session's two unarmed friendly robots at different map coordinates. A test marks one or both robots crazy as an initial condition; it does not claim to create a CRAZYBOT through the cheat. Real cheat admission, assembly and deployment remain covered by the [registry cases](80-cheat-registry.md).

Before each controlled call the actor is alive, has no logical group or orders, is not in its assigned place and has no perceived enemies. `LogicTakt(step)` executes the real robot code. The test does not advance the world, sides, perception or map clock; Session frame/time remain zero. These are robot-boundary call counts, not game-frame counts or elapsed gameplay durations. Before retargeting, actual coordinates stay unchanged and no order is added. The first retarget must allocate a valid player group, issue Attack and retain the other friendly robot as the actual target. The current crazy-target scan deliberately prefers another player robot; these unarmed controls establish no damage or combat balance result.

Sessions destroy their map, objects and engine fixture normally. No test accesses the static variables or injects their resulting count. Priming consists only of real robot calls. No production seam or reset is added.

## Measured contracts

| Case | Real call schedule | Required observation |
| --- | --- | --- |
| `fresh` | One actor in a fresh process | Calls 1-101 do not allocate a group; call 102 produces the first Attack. The first call changes saved coordinates, then 101 equal-coordinate visits cross `cnt > 100`. |
| `cross_session` | Prime one actor with 37 calls, destroy the Session, then create the same scene twice | The second Session attacks on its call 65, inheriting 36 increments and matching coordinates. After that attack clears the counter, the third Session attacks on call 101 because coordinates still match. |
| `changed_coordinates` | Prime 37 calls in one Session, then use the other initial position in a new Session | The new actor attacks on call 66. A coordinate change skips one increment but does not clear the inherited count. |
| `shared_robots` | Prime actor 1 for 37 calls, then call actor 2 within the same Session | Actor 2 attacks on call 66; actor 1 remains ungrouped. The counter belongs to neither individual robot. |
| `alternating_robots` | Alternate 120 pairs of calls at distinct positions, then call actor 1 alone | Neither actor retargets during the alternation. Every coordinate switch prevents an increment. Actor 1 then needs 102 uninterrupted calls; actor 2 stays ungrouped. |

The current implementation does not count consecutive stationary frames per robot. It counts equal-coordinate visits shared across all crazy actors, carries that count across coordinate changes, clears it on retarget, and retains the three values across Session teardown. Independent processes restore the C++ static initializers; a new Session does not. Neither identical RNG seeds nor fixture cleanup make those situations equivalent.

## Validation and sensitivity

Run the focused cases in each standalone configuration:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.player\.crazy_history\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.player\.crazy_history\.'
```

The registered tests use synthetic assets, no interactive window and no supplied game packages. Each case is bounded by a finite call schedule and the normal 30-second CTest timeout. The shared state is used regardless of the cheat admission setting; matching-build cheat enable/disable coverage remains in the registry suite.

Local MinGW Debug and Release each passed all twenty cases. Three temporary Release source controls checked sensitivity on the five ParkMiller/10 ms cases: making the counter local rejected all five; clearing it on coordinate changes rejected `changed_coordinates` and `shared_robots` while the other three controls passed; changing the threshold from `> 100` to `> 99` rejected all five. The original production bytes were restored and all twenty Release cases passed again. These failures establish sensitivity to counter lifetime, coordinate-change semantics and the exact transition boundary; they are not corrected expectations or accepted reference changes. Supported MSVC and x86 DLL results belong to the current-revision CI record.

## Disposition and remaining evidence

This change retains the measured process-shared behavior as a legacy characterization. It proposes no per-robot or per-battle reset, chooses no historical reference revision and changes no gameplay. A correction that isolates this state would change retarget timing and needs a separate gameplay/reference decision. The earlier deployment digests remain observations of full deployment, not accepted replay goldens.

This suite resolves the bounded lifetime question in #80; it does not establish complete CRAZYBOT combat trajectories, cross-compiler numeric equivalence, native behavior or the full finite reference/candidate campaign. Existing deployment and timing cases retain their own scope. Native execution remains #191; reference selection, comparisons and complete-baseline acceptance remain #69. The separately reviewed [capacity correction](327-group-capacity.md) resolves the exhausted-group defect; complete-baseline acceptance remains separate.
