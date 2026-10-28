# Manual-control recording contract

The existing track and hovercraft scenes retain their complete schema-1 recordings, with their original gameplay calls, schedules, assertions and session count. This package covers C5's two chassis lifecycle cases and the sixteen neighboring manual-exit cases reused by C5/A2. The [UI-hover/fire case](87-presentation.md#interface-focus-and-manual-firing), wheel/bomb/death cases, native focus and visual alignment keep separate ownership. These files do not complete CMD-05 or select a historical reference.

## Finite corpus

| Existing owner | Profiles and episodes | Retained comparison |
| --- | --- | --- |
| `game.player.manual_track_lifecycle`, `manual_hovercraft_lifecycle` | Two chassis, stationary/moving targets, both RNGs and 10/16 ms: sixteen Original episodes, one run each | Fresh-process comparison; no Original replay is added |
| `game.ai.manual_exit_*` | Sixteen cases, each retaining its existing Live twice and Removed twice: sixty-four episodes | Thirty-two complete replay pairs and sixteen declared Live/Removed manual prefixes |

The eighty filenames are `manual-control-<track|hovercraft>-<stationary|moving>-<pm|crt>-<10|16>-<original|removed|live>-<index>.jsonl` below `scenario-traces/`. Original uses index 0; Live and Removed use 0/1. Each original case invalidates its eight paths before setup; each continuation case invalidates its four. Require the complete successful eighteen-case selection and its process/JUnit evidence before accepting the corpus.

Seed 80, Open synthetic terrain, unavailable audio, supplied picking and the surrogate device remain unchanged. Chassis kind, motion choice, effective manual bindings and compiled trace mode supplement the existing Session build/RNG/precision/compatibility header. Replay indices belong only to filenames. Original intentionally keeps its existing bounds behavior; the neighboring variants refresh target bounds and add retry/free-point navigation. Enabling that branch for Original would change the scenario.

The effective compiler header preserves the Session writer's types: MSVC records `_MSC_VER` as a positive signed 32-bit JSON integer, while MinGW records `__VERSION__` as a nonempty JSON string. The validator checks the type for the named compiler and rejects booleans, missing values and crossed compiler/version representations.

## Observations and completion

Every whole map advance and existing checkpoint retains the four Session actors and added manual actor 5. A live-list address/UID check precedes live-only fields. Absent or non-live objects supply absence/liveness information without reading a chassis/weapon union reused for debris. The added actor records exact position, health, speed and direction bits; group intention/target/destination/War; actual Move and ordinary/additional place; enemy references; and all physical order parameters, including Param3 bits and signed Param4. Shared Session object identities remain -1/0/1..4; the local observer also identifies actor 5.

Initialized public weapon heat/cooling/range state and actual target health are retained. The observer does not call the flag-consuming `IsFireWas()` and does not infer a shot count from an order. Target and distant-opponent place observations preserve reservation-transfer evidence. No extra `GetGeoCenter`, `RNeed`, bounds refresh, selection action or simulation advance is introduced.

Commands retain actual keyboard edges, mouse messages/coordinates, input reset, explicit fixture removal and ordinary context Move inputs, with actual frame/time and RNG-before/after. Every command has an immediate result snapshot; same-frame observations are intentional. Held keys/mouse and controlled identity distinguish manual input from autonomous travel with a former manual key held.

Aim classifies null/landscape/water sentinels before object identity. Existing `m_TraceStopPos` bits are read only after the fixture assigns them. Frame zero explicitly marks coordinates undefined and omits them. Supplied aim is not native screen-space picking evidence.

The [original schedule](player-orders.md#armed-manual-control-on-track-and-hovercraft) exits at 2,100/2,176 ms and ends at 15,100/15,184 ms for 10/16 ms. The [neighboring continuations](84-manual-enemy.md) keep their later navigation and end at 25,100/25,216 ms. Each existing interval rounds independently to whole ticks. Original retains its arrival, in-place and no-fire checks; it adds no requirement that every MoveTo order retire.

Completion captures terminal frame/time/RNG and existing vector/prefix counts and diagnostic digests before teardown. Tails are appended only after replay/prefix assertions, session/asset lifetimes, tracked-allocation checks and base cleanup succeed. Digests accompany full recordings and assertions; they do not replace direct comparison.

## Collect and compare

After a clean matching build, run the existing cases and strict validator:

```powershell
& {
    $ErrorActionPreference = 'Stop'
    .\tools\test.ps1 -Configuration Debug -Filter '^game\.(player\.manual_(track|hovercraft)_lifecycle|ai\.manual_exit_(track|hovercraft)_(stationary|moving)\.(park_miller|legacy)(10|16))$'
if ($LASTEXITCODE -ne 0) { throw 'Manual-control producer failed.' }
python -B tools/check-manual-control-recordings.py build/mingw-debug-exe/tests/scenario-traces --revision (git rev-parse HEAD) --configuration Debug --compiler MinGW --target EXE --cheats false --trace false
if ($LASTEXITCODE -ne 0) { throw 'Manual-control recordings failed validation.' }
}
```

Repeat with Release and its matching directory/profile. CI validates after producer results are uploaded, without adding a Python dependency to CTest.

Preserve all eighty files separately before another complete run overwrites them. Add `--reference <directory> --reference-revision <full-commit>` to compare every file directly. A reference independently requires a full clean revision and the supplied build/role/trace settings; its commit may differ from the candidate. A reference pin must match that reference. An unpinned comparison is diagnostic. Final selection and the full manifest remain with G1/#324; matching these files supplies neither native results nor baseline acceptance.
