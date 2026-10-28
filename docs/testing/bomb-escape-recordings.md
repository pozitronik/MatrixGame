# Bomb-escape recordings

The three existing physical bomb-escape cases now persist their original 96 synthetic battles as schema-1 JSONL. The scenarios still perform their original in-memory vector comparisons; the recording adapter also makes the observations available to an independent semantic reader and exact historical comparison. See the [physical bomb-escape guide](84-bomb-escape.md) for the gameplay rules and fixture rationale.

## Finite inventory

| Caller | Refuge family | Existing sessions |
| --- | --- | --- |
| `game.ai.bomb_escape_free_journey` | Free refuge; removal waits for the second diversion, detonation waits for safety | 32 |
| `game.ai.bomb_escape_reserved_journey` | The cover reserves the first refuge; containment mode controls the escaping unit's choice | 32 |
| `game.ai.bomb_escape_unreachable_journey` | Both outgoing refuge edges reject the escaping chassis; clearance begins at the first frame at or after 1,200 ms | 32 |

Each caller runs both RNG modes, 10/16 ms schedules, Corrected/Original place containment, removal/detonation, and the two already-existing replays. Seed 84, unavailable audio, the open synthetic layout, initially unselected units and the 5,000-frame bound remain unchanged. The loop retains its 45-second simulation bound and original completion rule: after arrival, observe for at least 500 ms and until bomber retirement. Each caller retains its 90-second process deadline.

The test working directory receives `scenario-traces/bomb-escape-<free|reserved|unreachable>-<remove|detonate>-<corrected|original>-<pm|crt>-<10|16>-<0|1>.jsonl`. The last component identifies the original replay ordinal. Each caller invalidates its entire 32-file family before any setup. The three filename families are disjoint, including when CTest runs them in parallel.

## Producing the recordings

Use the pinned Windows tools from the [build guide](../BUILD_WINDOWS.md). These cases use synthetic assets and do not launch a visible game or require installed resource packages.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.bomb_escape_(free|reserved|unreachable)_journey$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.bomb_escape_(free|reserved|unreachable)_journey$'
```

File output is buffered. A journey closes its file only after its terminal assertions and records a pending footer containing frame, time and RNG count. The main process appends successful completion records only after the entire caller returns, its original replay comparisons succeed, all scene and containment lifetimes end, allocation checks pass and `BaseDeInit()` returns. Failed setup or an interrupted/failed caller cannot leave an accepted stale 32-file family.

## Observation contract

Headers retain the existing Session metadata: actual compiled source revision and dirty state, compiler/version, configuration, assertions, floating-point precision, executable/DLL role, cheats, RNG, timestep and compatibility modes. The scene adds refuge, detonation, replay ordinal and compiled `_TRACE`. Place containment is the mode actually active in that Session; Original and Corrected are separate profiles. MinGW supplies a nonempty compiler-version string, while MSVC supplies a positive integer.

Normalized IDs remain the four Session actors: cover 1, escaping robot 2, bomber 3 and surviving enemy 4. The recorder never serializes pointers or process-global UIDs. Every observation contains the unchanged Session snapshot and a custom view of the actors' current physical orders, position/health bits, group/place, Move and saved-return coordinates, pending-death state and bomber perception of the cover. Orders retain all seven initialized fields: type, phase, three float-bit parameters, signed Param4 and normalized retained-core object. Raw cores are read directly; `GetStatic()` is not called. Null maps to 0 and an unknown non-null object to -1.

Actor presence follows the current logic list. Bomber-core presence is observed separately through the retained core, with the known bomber identity preserved even if it has left that list. Retirement occurs when the core's object becomes null, not merely when the robot stops being live. Absent actors contain only ID and presence; present non-live actors contain only ID, presence, life state and state enum. Their repurposed live union storage is never read.

The scene also records the source's safety/refuge/first-return/second-escape/clearance/damage/arrival/retirement milestones, maximum-distance float bits, refuge edge masks and both sides' kill counts. Distance samples forward the float results of the fixture's existing evaluations: the maximum-distance input and the conditional refuge-distance call. Unmeasured or short-circuited samples are explicitly absent; setup precedes those measurements and carries zero milestone placeholders. The `planned` observation starts the original distance tracking. The reader uses these exact float bits for safety and refuge predicates, without recomputing distances or introducing a tolerance. No position read or distance calculation is added for these samples. Commands retain their actual inputs and before/after RNG boundaries: cover reservation, chassis-specific refuge-edge blocking, the initial Move to (20,20), the explicit `EscapeFromBomb()` call and bomber removal or detonation. These are the original deliberate component interventions, not claims about human input dispatch.

The original vector consists only of `planned`, `clearance` and `tick` observations. Its former `remove:` and `detonate:` text prefixes survive in `original_prefix` on the clearance snapshot; the `state` itself remains valid schema-1 JSON. Extra `setup-ready`, `before-clearance` and `final` observations expose action/lifetime boundaries and are excluded when reconstructing the original vector. No simulation advance is added for an observation.

## Validation and comparison

After both builds generate all three callers, validate each complete finite corpus:

```powershell
$revision = git rev-parse HEAD
python -B tests/check_bomb_escape_traces.py build/mingw-debug-exe/tests/scenario-traces --configuration Debug --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
python -B tests/check_bomb_escape_traces.py build/mingw-release-exe/tests/scenario-traces --configuration Release --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
python -B -m unittest discover -s tests -p test_bomb_escape_traces.py
```

The reader requires exactly 96 matching filenames, validates each file with the shared strict schema-1 parser and independently checks typed payloads, complete physical orders, normalized lifetime, applicable input schedules and every completed map boundary. It checks initial refuge/reservation choice, saved destination and return, source milestones, damage/retirement/arrival relationships, terminal health and order cleanup. Free-refuge removal retains the measured second diversion at 8,910 ms for 10 ms steps and 8,976 ms for 16 ms steps. Detonation damage remains `(300 // step + 1) * step` after clearance. No deadline, expected event or tolerance is adjusted.

The reader then compares all observations in each of the existing 48 same-profile replay pairs. Only the validated replay ordinal is normalized for that comparison; the original C++ vector equality is still present. Different refuge families, containment modes, clearance actions, timesteps and RNG modes are never equated. Each positive file is parsed once per corpus check, retaining only the current pair rather than the complete corpus.

Add `--reference <directory> --reference-revision <full-commit>` to compare corresponding candidate/reference filenames exactly after independent validation. Historical and candidate commits may differ. References always require a full clean compiled identity, even when no candidate revision pin is supplied. Each corpus must have coherent compiler/effective build identity across its files; containment remains the declared per-file variant. Omitting the historical pin prints a diagnostic warning and cannot establish accepted reference evidence. Omitting `--revision` permits dirty candidate diagnostics and also prints a warning. An explicit reference pin requires its directory and must be a full lowercase 40-character commit. Command payloads, timing, RNG, float bits and remaining headers remain strict.

CI runs the existing production-linked cases, checks the corpus against the checked-out source and actual build profile, and retains JSONL with the normal scenario artifacts. Use the matching `--compiler`, `--target`, `--cheats` and `--trace` values for other supported profiles. Local standalone Debug/Release checks do not establish MSVC, DLL, cheats or trace execution; those profiles remain CI responsibilities.

## Evidence boundary

This package persists the accepted physical escape/return journeys. Planning-only capacity/reservation predicates, human input dispatch, rendering/audio quality, other map/crowd layouts and original-host behavior remain separate contracts. It does not choose a final reference revision, qualify the programme's resource manifest, perform the final fresh-process campaign or accept the complete baseline. Native checks and integrated common-revision evidence retain their existing owners.
