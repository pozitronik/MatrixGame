# Successful War detour recordings

The [successful tactical detour cases](84-war-detours.md) retain the complete observations of their existing journeys as schema-1 JSONL files. The scene still runs one armed player robot against a stationary target, with synthetic geometry, an Open movement layout, unavailable audio and a device that never draws. Recording adds no game commands, sessions, replays or simulated time. It does not select a historical reference or establish complete-baseline acceptance.

## Build and run

Use the pinned Windows tools described in [Windows builds](../BUILD_WINDOWS.md). The existing `matrixgame_ai_war_detour_scenarios` target includes the scene-specific recorder. Run all twelve registered cases in each configuration:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.war_detour\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.war_detour\.'
```

Each normal test build writes under its own `build/<compiler>-<configuration>-<target>[-trace]/tests/scenario-traces/` directory. The helpers use separate normal build directories for the selected compiler and EXE/DLL target. Files identify caller context as well as actual journey variant, so concurrent cases never invalidate another caller's outputs.

## Finite inventory

Names follow `war-detour-<caller>-<rng>-<step>-<role>.jsonl`. Caller is `control`, `retirement` or `replacement`; RNG is `pm` or `crt`; step is `10` or `16` milliseconds. Every caller has `first` and `replay`; interrupted callers also have `intact`, which runs the original uninterrupted control within that caller.

| Caller | First and replay | Intact | Files across both RNGs and steps |
| --- | --- | --- | --- |
| `control` | Uninterrupted combat and return to Patrol | None | 8 |
| `retirement` | Delete the still-live target at 800 ms | Uninterrupted control | 12 |
| `replacement` | Replace Patrol with context Move at 800 ms | Uninterrupted control | 12 |

This is exactly 32 existing Session lifetimes. Sixteen are uninterrupted journeys when the interrupted callers' intact controls are included; those controls keep distinct filenames. No extra run is added to obtain the files.

The twelve existing first/replay comparisons retain their complete tick boundaries. Each interrupted caller also compares its first journey with its intact control through the 800 ms tick, before the intervention: eight existing prefix comparisons. The consumer applies those same edges to the original Session/detail observations and the new observation fields; it does not compare different post-intervention commands as equivalent.

## What is recorded

The header identifies the original `reachable-war-detours-v1` fixture, recording version, caller, role, actual change, seed 84, timestep, original 24-second frame bound, initial Patrol endpoints and actual source/build/compatibility profile. MSVC versions retain the numeric `_MSC_VER`; MinGW versions retain the nonempty `__VERSION__` string. A separate compiled `trace` boolean records `_TRACE`, which the shared Session metadata does not contain.

An initial snapshot precedes the actual setup Move to world `(500,100)`. A command and post-command snapshot retain that context route, followed by the actual `PGOrderPatrol` call to grid corner `(48,38)`. Its From endpoint remains `(48,8)`. The scene continues to use the original command route; recording does not inject a tactical destination or target memory. Interventions are recorded after the 800 ms observation and before the next original tick.

Each tick keeps the original Session snapshot and original custom detail without reserializing them in C++. The detail includes War, tactical place/search state, enemy count, target/attack comparisons, retained-core presence, Patrol From and current MoveTo coordinates. Supplemental observations retain the normalized environment and attack target identities, the firing target selected immediately before each tick, physical MoveTo, firing state, all four actors' complete initialized order parameters, original pulse counter, last already-observed health and the journey's selected firing-place history. IDs are Session-local: zero is null, one through four identify registered robots, and minus one is a non-null reference outside that inventory.

Full orders contain type, phase, the exact bits of all three floating parameters, the fourth integer parameter and normalized object identity. The shared Session queue retains only its existing five-field projection. Reading the additional initialized parameters uses no mutating order accessor and adds no retained engine references.

Target lifetime has three distinct states: a live object, a DIP object whose retained core still points to it, and a retired object whose core is empty. After DIP, the recorder reads only the core, robot state and scalar death flag; it omits live health and order storage. The final health comes from the original journey's already-observed cache. It never reads undefined trace/aim coordinates or dereferences retired target pointers.

The terminal record retains all original event times, firing place, pulse count and health bits, plus the final RNG draw count. That count must match the final snapshot. First War, firing-place assignment and physical arrival, hit/health progression, death or explicit removal, resumed movement and final endpoint arrival are checked from the recorded observations against the original source predicates, rather than substituting a table of measured timings.

## Completion and failure

Before any scene setup, the owning caller truncates its complete two- or three-file output set. An early setup failure therefore cannot leave an old replay or intact control looking successful. Other callers' files are untouched.

Journeys flush and close their body streams but defer the completion records. The process appends those records only after all original journey assertions, exact replay and intact-prefix comparisons, Session/core/asset destruction, the Debug memory check and engine cleanup succeed. Appended footer streams are explicitly flushed, closed and checked. Always require the producer's successful exit and the complete strict corpus: a filesystem failure while appending a later footer can occur after an earlier footer has been written.

## Strict validation and references

The dedicated consumer requires the exact 32-file inventory, complete schema-1 framing, correctly typed Session/custom/order data, actual command/tick schedules, original physical and combat milestones, coherent source/build identity and matching terminal RNG. It parses every positive file once. Two representative files supply eight bounded malformed-observation, milestone and truncated-tail controls; it does not reparse the entire corpus for every negative control.

For a clean MinGW Debug EXE build, run:

```powershell
$revision = git rev-parse HEAD
python -B tests/check_war_detour_traces.py build/mingw-debug-exe/tests/scenario-traces --revision $revision --configuration Debug --compiler MinGW --target EXE --cheats false --trace false
python -B -m unittest discover -s tests -p test_war_detour_traces.py
```

Supply the actual six profile arguments together. They bind the compiled revision, configuration, compiler, EXE/DLL role, cheats and trace status; they do not infer assertion flags from a configuration name or identify external resource bytes. CI supplies its actual matrix values. Diagnostic use without profile arguments permits inspection of dirty or source-archive outputs and cannot establish accepted baseline evidence.

Add `--reference-directory <directory> --reference-revision <reviewed-full-commit>` to compare corresponding complete files. A reference may come from a different reviewed historical commit, while keeping the supplied build profile. Strict reference validation independently requires a clean full revision and one coherent source/build identity throughout its corpus; candidate flags do not force the reference to the candidate commit. Omitting the explicit reference pin is diagnostic and does not approve a historical oracle. Each side must independently satisfy the finite semantic guard before the existing exact comparator is used.

These recordings cover this declared Patrol detour scene only. Failed place search, ordinary Move, other target configurations, native rendering/audio and original-host operation remain separate evidence. The original scene's deadlines, compatibility settings and limitations remain those documented in the [case guide](84-war-detours.md).
