# Armed-combat recording validation

The existing `game.combat.robot_firing`, `plasma_flight` and `cannon_firing` cases drive laser attacks, plasma impacts after shooter removal, and turret target replacement through production logic. Their fixed hit schedules and damage assertions are described in [Combat contracts](82-combat.md#armed-scenarios-and-reusable-geometry). This validator preserves their current C++ producers and checks the retained recordings as well as their in-process replay comparisons.

The finite corpus contains sixteen schema-1 JSONL files under the test working directory's `scenario-traces/`: eight `laser-attack-{pm|crt}-{10|16}-{0|1}.jsonl`, four `plasma-flight-{pm|crt}-10-{0|1}.jsonl`, and four `cannon-retarget-{pm|crt}-10-{0|1}.jsonl`. These form eight exact same-profile replay pairs. Seed 82 and unavailable audio are part of the fixture profile. Laser horizons are 3,000 ms at 10 ms steps and 2,992 ms at 16 ms steps; plasma advances to 3,600 ms and the turret scene to 2,000 ms. The 16 ms laser schedule deliberately ends its attack prefix at 992 ms, then advances 2,000 ms after deletion.

Every completed map boundary retains the original full Session robot/group state and custom health, status and robot-state observations. Heat is required while the shooter exists; the turret scene additionally requires acquisition, yaw, pitch, turret yaw and cannon health. Patient health must match the corresponding registered actor, including the turret's switch from actor 1 to actor 2. The validator checks actor retirement, exact command/observation ordering, actual RNG boundaries, command acceptance and resulting order/target/destination, complete file inventory and terminal boundaries. The context attack must retain Attack (order 3), target 3 and footprint destination (48,48); either removal must report the retired actor values: order -1, null target 0 and destination (-1,-1). It reuses the strict parser and comparator without an observation filter or numeric tolerance.

Before case-specific assets or Session setup, each of these three CTest cases truncates its entire finite output set: eight laser files or four plasma/turret files. An early setup failure therefore cannot leave complete recordings from a preceding run in that case's slots. A journey captures its terminal frame/time before teardown and defers its completion marker. The selected markers are appended only after every Session lifetime, replay comparison, outer tracked-allocation check and base cleanup has succeeded; a later failed assertion leaves incomplete recordings. This changes publication timing without changing commands, observations, RNG use, horizons or the schema-1 completion contents. Other combat cases retain their existing recording behavior.

Keep the successful process/JUnit result with the recordings: a completion marker does not establish the complete CI matrix or baseline acceptance, and a final file-write error still fails the process. Normal CI validates all sixteen files after engine-result upload and only after successful producer tests; source identity, compiler, configuration, EXE/DLL role and cheats must match that job. Malformed-record unit tests exercise semantic checks independently of equality between paired files.

Run this block as one statement from the repository root. It stops on a producer or validator failure:

```powershell
& {
    $ErrorActionPreference = 'Stop'
    $revision = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify combat source revision.' }
    foreach ($configuration in @('Debug', 'Release')) {
        .\tools\test.ps1 -Configuration $configuration -Filter '^game\.combat\.(robot_firing|plasma_flight|cannon_firing)$'
        if ($LASTEXITCODE -ne 0) { throw 'Combat producer failed.' }
        $directory = 'build/mingw-' + $configuration.ToLowerInvariant() + '-exe/tests/scenario-traces'
        python -B tools/check-combat-recordings.py $directory --revision $revision --configuration $configuration --compiler MinGW --target EXE --cheats false
        if ($LASTEXITCODE -ne 0) { throw 'Combat recordings failed validation.' }
    }
}
```

Preserve the sixteen files in a separate directory before another run overwrites them. Add `--reference <directory>` to compare every candidate file against the corresponding retained file and report the first difference; reference and candidate must have matching fixture/build profiles and retained manifests. Source revision and dirty markers are provenance, not behavioral comparison fields. These existing headers omit `_TRACE`, so retain that build setting in the external manifest and compare matching trace profiles only; this tool does not establish equivalence across them.

This corpus is one part of the [whole-game baseline](baseline-completion.md#integrated-acceptance-evidence). Other weapon families, splash, status ticking, damage/lifetime continuations, native rendering/audio and original-host behavior keep their existing tests and evidence obligations. A passing validator neither completes COM-01/COM-02/COM-03 nor selects a historical reference, supplies a complete G1 manifest or accepts the baseline.
