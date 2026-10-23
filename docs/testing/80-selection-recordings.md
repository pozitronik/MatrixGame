# Mixed-selection lifecycle recordings

`game.player.mixed_selection_lifecycle` retains observations for the C1 / CMD-01 mixed-object selection scene. It uses the existing production form mouse dispatch, mesh intersection, lethal damage and map ticks. Its sixteen profiles combine both RNG modes, 10/16 ms steps, both supplied visibility orders and click/rectangle routes. Each profile now runs twice in fresh `Session` lifetimes and compares the complete ordered observations. These are synthetic, non-rendering checks; native picking, frustum membership and visible highlights remain separate requirements.

The case writes 32 schema-1 JSONL files under the test working directory's `scenario-traces/`, named `player-selection-{pm|crt}-{10|16}-{robot|building}-{click|rectangle}-{0|1}.jsonl`. The header retains the actual build/session profile and adds the visibility order, input rectangle, `_TRACE` state and stable actor IDs. Existing session snapshots retain all four registered robot identities and their live state, orders, queues and player groups. A separate selection object records ordered current and temporary membership, current index/object, active object, selection kind, retained core identity, actor lifetime state and kill attribution. The building and cannon have fixture IDs 5 and 6. Pointer addresses are never serialized or dereferenced after retirement.

Named commands retain their actual frame/time, supplied visibility order and mouse rectangle where applicable. Snapshots cover setup, cleared selection, the cannon-only control, robot precedence, lethal damage, building selection during the dying state, every existing retirement tick, a fresh building selection after retirement, and final clearing. Repeated frame-zero observations preserve the order of synchronous input and damage; they are not extra simulation ticks. Each observation verifies that it consumed no RNG draws. The checked completion record follows the final assertions and the replay comparison. A valid file alone does not establish process success or successful teardown; retain the CTest result as well.

Run the producer and validate its actual output in each configuration:

```powershell
. {
    $ErrorActionPreference = 'Stop'
    .\tools\test.ps1 -Configuration Debug -Filter '^game\.player\.mixed_selection_lifecycle$'
    if ($LASTEXITCODE -ne 0) { throw 'Debug selection producer failed.' }
    python -B tools/check-selection-traces.py build/mingw-debug-exe/tests/scenario-traces --configuration Debug --compiler MinGW --target EXE --cheats false --trace false
    if ($LASTEXITCODE -ne 0) { throw 'Debug selection recordings failed validation.' }
    .\tools\test.ps1 -Configuration Release -Filter '^game\.player\.mixed_selection_lifecycle$'
    if ($LASTEXITCODE -ne 0) { throw 'Release selection producer failed.' }
    python -B tools/check-selection-traces.py build/mingw-release-exe/tests/scenario-traces --configuration Release --compiler MinGW --target EXE --cheats false --trace false
    if ($LASTEXITCODE -ne 0) { throw 'Release selection recordings failed validation.' }
}
```

Run the whole block as one statement; it stops on a producer or validator failure before continuing. Direct CTest execution requires the matching built target and runtime DLLs described in [Testing](../TESTING.md). Normal CI validates all 32 files and sixteen exact replay pairs after uploading the engine results, with the actual checked-out revision, compiler, configuration, EXE/DLL role, cheats and trace profile. The repository check runs `test_selection_traces.py`; its malformed recordings test semantic validation independently of equality between two files. The shared strict parser rejects malformed/truncated JSONL and mismatched completion boundaries without a schema exception or numeric tolerance.

For fresh-process or reference/candidate comparisons, retain the 32 files in a separate directory before rerunning the producer, then pass that directory with `--reference <directory>`. The validator compares every corresponding file and reports the first difference, while retaining strict fixture/build-profile matching. `--revision <commit>` additionally checks the candidate's recorded source identity; the common comparator ignores source revision and dirty markers only, so provenance changes do not hide behavior differences. Repeatability of a current candidate does not select or accept a historical reference. The complete manifest, required process campaign, comparison policy, native evidence and exact-revision acceptance remain with the [whole-game baseline](baseline-completion.md#integrated-acceptance-evidence).
