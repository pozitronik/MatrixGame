# Competing-repair recordings

The existing `game.ai.repair_competing_retirement` and `game.ai.repair_replacement_move` scenarios persist all twelve of their synthetic battles. These recordings expose the repairer's target retention and replacement decisions while its original movement order remains active. They add observation and file output to the established scenarios; they do not add battles, commands, simulation advances or a new replay oracle.

## Finite inventory and execution

| Caller | Family | Sessions | Intervention |
| --- | --- | --- | --- |
| `game.ai.repair_competing_retirement` | `retirement` | 4 | Delete the current patient after its second repair pulse |
| `game.ai.repair_replacement_move` | `replace-far` | 4 | Replace Move while the competitor is outside repair range, after the second pulse |
| `game.ai.repair_replacement_move` | `replace-near` | 4 | Replace Move while the competitor is within repair range, after the third pulse |

Each family runs Park-Miller and LegacyCRT at both 10 ms and 16 ms, with seed 84, the open synthetic layout, unavailable audio and a 3,000-frame limit. Every existing loop runs until time reaches 25,000 ms: 2,500 boundaries at 10 ms or 1,563 boundaries at 16 ms. Assets are synthetic; no installed game or resource package is required.

From the repository root, build and run the two existing CTest cases with the pinned tools:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.(repair_competing_retirement|repair_replacement_move)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.(repair_competing_retirement|repair_replacement_move)$'
```

The test working directory contains `scenario-traces/repair-competing-<family>-<pm|crt>-<10|16>.jsonl`. The retirement caller invalidates all four of its files before setup; the replacement caller invalidates all eight before either of its two family loops. A failed setup therefore cannot leave an accepted stale family. Recordings are buffered during execution and closed after their final assertions. Completion records are appended only after the selected caller returns, fixture destruction and allocation checks succeed, and `BaseDeInit()` returns. An interrupted or failed run is incomplete and must be rejected.

## Observed state

The header uses the existing schema-1 Session metadata: compiled source revision and dirty state, compiler and version, configuration, assertion policy, executable/DLL role, cheats, floating-point precision, effective compatibility modes, RNG, timestep and synthetic fixture policy. The scene adds its family flags and the compiled `_TRACE` state. MinGW reports a nonempty compiler-version string; MSVC reports a positive integer.

Each observation contains the Session snapshot and six explicitly normalized actors. IDs 1 through 4 retain the Session identities; ID 5 is the first patient and ID 6 is the earlier-listed competitor. The ordered logic list is recorded without sorting: initially `[1,2,3,4,6,5]`. Retiring patient 5 removes it from that list and clears its retained core. An absent actor has only ID and presence; a present non-live actor has only ID, presence, life state and state enum. Live-only fields are never read from inactive robot storage.

Live custom actors carry position and health float bits, group, signed place, arrival flag, group intention and destination, and the complete physical order queue. Each order retains type, phase, three float bit patterns, signed Param4 and the normalized object referenced by its retained core. The recorder reads the core directly; it never calls the mutating `GetStatic()`. The Session's existing queue retains its five-field layout, while the custom queue supplies the omitted Param3 and Param4. Unknown non-null objects normalize to -1 and null to 0; pointers and process-global UIDs are never serialized.

The scene also records the current repair target, the target immediately before each advance, both pulse counts and cached health bits, the first patient's core, and whether the intervention has occurred. Observations surround the initial Move, the intervention, every existing completed map boundary and the final assertions. Command records contain the actual input points `[500,200]` and `[300,500]`, or deleted patient ID 5, with RNG counts before and after the existing action. The deferred completion record repeats the final frame, time and RNG count without accessing a destroyed scene.

## Validation and exact comparison

After generating both callers' files, run the scene-specific semantic reader. Substitute the corresponding compiler, target, cheats and trace values when using another supported build profile:

```powershell
$revision = git rev-parse HEAD
python -B tests/check_competing_repair_traces.py build/mingw-debug-exe/tests/scenario-traces --configuration Debug --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
python -B tests/check_competing_repair_traces.py build/mingw-release-exe/tests/scenario-traces --configuration Release --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
```

The reader requires exactly twelve matching filenames. It uses the shared strict schema-1 parser and independently validates typed observations, bounded physical queues, normalized identities, consistent build identity across a corpus, complete boundary coverage and action order. It checks the source's repair pulse amounts and attribution, intervention threshold, target choice after replacement, retained movement intention, retired-core cleanup, terminal health, physical arrival and firing-order cleanup. Comparing two equally damaged files does not waive these semantic checks.

For a separately qualified reference directory, add `--reference <directory> --reference-revision <full-commit>`. Corresponding filenames are compared exactly after each side passes validation. The historical reference commit may differ from the candidate commit; its own provenance and effective profile must remain consistent and clean. No command, timestep, RNG count, payload or float-bit tolerance is skipped. These scenarios contain no interchangeable route pair, so the reader does not invent equality comparisons between different families or profiles.

With `--revision`, the candidate must have that full lowercase 40-character commit identity and dirty state 0. A reference must always have a full clean identity, even when both optional pins are omitted. Omitting `--revision` allows diagnostics on a dirty checkout and does not establish accepted baseline evidence; each corpus must still have a consistent identity. An explicit reference pin always requires a reference directory and a full clean matching identity. An unpinned historical comparison prints a diagnostic warning and does not establish accepted reference evidence.

CI generates the existing cases as part of the engine suite, validates the twelve recordings against its checked-out revision and build profile, and retains them with the other scenario recordings. Python unit tests use synthetic traces to exercise scalar domains, missing observations, completion failures and correlated damage. Run those checks separately with:

```powershell
python -B -m unittest discover -s tests -p test_competing_repair_traces.py
```

## Boundary of this evidence

This package covers the existing competing-patient journeys. Earlier acquisition and repair-movement scenarios, rendering, audible effects and original-host behavior remain separate. The storage-device surrogate does not draw a native frame. Producing these files does not select the programme's final reference revision, qualify its resource manifest, establish overall coverage or satisfy complete-baseline acceptance. Native execution and the final common-revision comparison remain separate acceptance work.
