# Regional-road recording contract

These recordings retain the eight existing [road-guided and destroyed-blocker cases](81-road-guided-journeys.md). They add no journeys, sessions, simulation steps or input changes. The ordinary `navigation-traces` writer and other navigation requirements remain separate.

## Finite corpus

Each RNG/timestep profile runs unguided twice, guided twice, live-blocker twice and destroyed-blocker twice: thirty-two existing Session lifetimes across eight CTest cases. Filenames are `scenario-traces/road-journey-<pm|crt>-<10|16>-<unguided|guided|live-blocker|destroyed-blocker>-<0|1>.jsonl`. Each owning case invalidates only its four paths before fixture setup, so separately scheduled case families cannot overwrite each other's results.

Headers record actual compiled revision/dirty state, compiler/version, Debug/Release, EXE/DLL, cheats, trace, precision and compatibility settings, with seed 81, `road-ring-v1`, unavailable audio and the no-draw surrogate device. MSVC emits integer `_MSC_VER`; MinGW emits string `__VERSION__`. Both RNG modes retain their own 10/16 ms schedules and 40,000 ms horizon. No equality between compilers, timesteps or RNG modes is required.

## Retained observations

Each original observation retains the complete Session JSON and the exact original `:blocker=...:enemy_kills=...` suffix as one string. Typed fields expose the same blocker core presence, identity, state, position/health bits and kill credit. State/position/health remain observable while the retained core's object exists, including the original death-in-progress observations. Retirement omits object fields; physical queues are read only while the robot is live, without touching debris unions.

Every original map boundary also records the group Move destination, actual regional road list/junctions/road zones, each actor's current movement cell/zone/region and allocated ordinary/additional place. Session's existing compact queues remain intact; supplemental queues retain all seven initialized fields, including Param3 and signed Param4. Object identities use the Session's four actors and blocker 5; an external reference is -1. No robot private path buffer, cache-refresh accessor or new production seam is needed.

Ground Move and explicit regional guidance are recorded as commands with actual RNG boundaries. The destroyed-blocker journey records the existing lethal laser intervention at 6,000 ms and one immediate post-hit observation. This extra observation is marked separately and does not enter the original vector or its pre-hit comparison. Original vectors still have 4,001/2,501 observations; the direct hit still calls the original `GetGeoCenter` exactly once. No extra ticks or RNG draws are introduced.

Ordered per-frame zones preserve actual corridor travel. Cumulative visit masks, first crossing/arrival, minimum separation and retirement accompany those observations. Earlier progress after destruction does not identify an internal replanning or avoidance mechanism.

Body files close before teardown. Successful tails are appended and explicitly closed only after all original journey, replay, prefix, lifetime, tracked-allocation and main cleanup checks succeed. Process success is required as well as complete files. Completion retains original-vector counts and FNV digest words as diagnostics; the digest is not a semantic oracle. Full records, exact comparisons and campaign file hashes provide the evidence.

## Collect and inspect

From a clean checkout with the pinned Windows tools:

```powershell
& {
    $ErrorActionPreference = 'Stop'
    .\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.road_(guided_journey|destroyed_blocker)\.'
    if ($LASTEXITCODE -ne 0) { throw 'Road journey producer failed.' }
    python -B tools/check-road-recordings.py build/mingw-debug-exe/tests/scenario-traces --revision (git rev-parse HEAD) --configuration Debug --compiler MinGW --target EXE --cheats false --trace false
    if ($LASTEXITCODE -ne 0) { throw 'Road recording validation failed.' }
}
```

Repeat for Release and its matching directory. DLL/cheats/trace profiles need matching build/test switches and validator arguments. CI validates each finite corpus after uploading producer results. Run protocol regressions with `python -B -m unittest discover -s tests -p test_road_recordings.py`.

The semantic guard checks the complete inventory, every original boundary, command/hit schedule, required field types, physical order domains, normalized identities, actual road/junction shape, preserved legacy payload, measured visit/crossing/arrival/retirement history and original settling conditions. It independently rejects equally malformed recordings before replay equality. It then retains sixteen exact replay pairs, four guided/unguided corridor distinctions and four complete pre-hit prefixes plus their original relative crossing/arrival conditions. It does not recompute production collision/pathfinding or floating-point `hypot` as a second simulation.

Preserve all thirty-two files before another run overwrites them. Add `--reference <directory> --reference-revision <full-commit>` for exact file comparisons. Candidate and reference independently require full clean compiled revisions and matching supplied build settings; all reference files must share one effective revision/profile even without a pin. A historical reference may differ from the candidate. Unpinned comparisons remain diagnostic.

These synthetic no-draw records do not validate native trajectories, projectile aiming, map compilation, every static path helper or original-host integration. Selecting a historical reference and accepting the integrated complete baseline remain separate work.
