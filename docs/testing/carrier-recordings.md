# Physical carrier recording contract

These recordings retain the existing [delivery and neighboring-landing cases](81-navigation.md#delivery-flights-and-carrier-damage) and [low/tall approach controls](81-carrier-approach.md). They add no Session lifetimes, gameplay commands or movement steps. Weapon-attached transport, economic request/cap predicates, native models/icons and other navigation cases keep their separate contracts.

## Finite inventory

| Family | Existing cases and profiles | Sessions | Comparison |
| --- | --- | --- | --- |
| `flights` | `game.navigation.delivery_flights`: both RNGs, 10/16 ms, four cardinal/configuration variants | 16 | Exact corresponding-file comparison; no replay added |
| `neighbors` | `neighboring_delivery_landings`: both RNGs, 10/16 ms, three surface/exclusion variants | 12 | Each Session contains the original two carriers; no replay added |
| `damage` | `delivery_carrier_damage`: both RNGs, original 10 ms schedule, three damage stages | 6 | Original repair/laser and survival assertions; no replay added |
| `approach` | Four `carrier_approach_obstacle.*` cases: both RNGs, 10/16 ms, low/high buildings twice each | 16 | Eight exact replay pairs and four original low/high comparisons |

The fifty filenames are `scenario-traces/carrier-<family>-<pm|crt>-<10|16>-<variant>-<replay>.jsonl`. Variants are 0–3 for flights, 0–2 for neighbors/damage, and 0/1 for low/high approach. Replay is 0 except the original approach pairs, which use 0/1. The three aggregate owners invalidate only their own sixteen/twelve/six paths before setup; each approach case invalidates its own four paths. Independent cases cannot truncate another owner's records.

Headers retain the actual compiled revision/dirty identity, compiler/version, configuration, EXE/DLL, cheats, trace, precision, compatibility settings, seed 81, Open topology and unavailable icon-target profile. MSVC's compiler version is an integer; MinGW's is a string. Different RNGs, timesteps, configurations and compilers are distinct executions. The original flight/landing timing tables, thirty-second horizons, damage ready conditions plus twenty-second continuation, thirty-two-second approach horizon and existing bounds remain unchanged.

## Observation and lifetime

Each original map advance has a complete schema-1 observation; frame zero describes the requested delivery before the first tick. Damage also retains immediate repair/laser results. Commands record the actual request inputs, ordered maintenance choices, cooldown/resources, damage origin/weapon/side, reported outcomes and RNG-before/after boundaries. One Session is one episode even when its maintenance request creates two carriers.

Session actors keep IDs 1–4. Request-order cargo/carrier pairs use 5/6 and 7/8. The approach obstacle is 9; null and external references are 0/-1. Supplementary queues retain all seven initialized physical order fields, including Param3 bits and signed Param4; Session's existing compact queues remain unchanged. Logical player orders use their own enum, including AutoDefence 9.

Retained cores qualify cargo/carrier identity before access. Carrier presence is checked against the actual active list. Cargo's logic-list membership is reported separately: absence from that list does not establish retirement while a retained core still owns it. DIP and retired cargo omit all live-only queue/environment fields; a retired carrier supplies no stale pose or health. Observation borrows existing cores and never acquires another ownership lease.

Poses come from the driver's existing samples. No extra `RNeed`, `GetGeoCenter`, `GetPos`, drawing or matrix refresh is introduced to fill gaps. Unsampled poses/heights are explicitly marked and omitted. In particular, ordinary damage-loop observations have no sampled flight pose or cargo height; the original hit point is retained as a command input and post-repair sample. Cached cargo transform height remains distinct from raw current X/Y. Neighboring deliveries retain the final complete standing poses and original sampled `hypot` separation in the footer, including the existing cache-lag allowance.

The original approach observation string retains complete Session JSON plus its carrier/cargo suffix. Low/high comparisons keep equal horizontal samples, RNG histories and lifecycle times, taller sampled altitude and the original height response. The high building's measured penetration remains characterized; these files do not assert collision-free flight or substitute a new obstacle-avoidance rule. Interpolated surface and separation results come from the original C++ samples; Python does not rerun interpolation or `hypot`.

Bodies close before teardown. Checked completion is appended only after original episode assertions, all caller replay/control comparisons, object lifetimes, tracked-allocation checks and main cleanup succeed. Process success is required in addition to complete files. Observation digest words are diagnostic; full records, exact comparisons and campaign file hashes carry the evidence.

## Collect and compare

From a clean checkout with the pinned Windows tools:

```powershell
& {
    $ErrorActionPreference = 'Stop'
    .\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.(delivery_flights|neighboring_delivery_landings|delivery_carrier_damage|carrier_approach_obstacle\.(park_miller|legacy)(10|16))$'
    if ($LASTEXITCODE -ne 0) { throw 'Carrier journey producer failed.' }
    python -B tools/check-carrier-recordings.py build/mingw-debug-exe/tests/scenario-traces --revision (git rev-parse HEAD) --configuration Debug --compiler MinGW --target EXE --cheats false --trace false
    if ($LASTEXITCODE -ne 0) { throw 'Carrier recording validation failed.' }
}
```

Repeat for Release and its matching directory. DLL, cheats and trace require corresponding build/test switches and validator arguments. CI runs this finite consumer between the N5 and armed-combat checks. Protocol regressions run with `python -B -m unittest discover -s tests -p test_carrier_recordings.py`.

The consumer checks the complete fifty-file inventory, required scalar/bit/queue domains, normalized identity and lifetime, sampled/unavailable fields, all original step boundaries, command chronology, lifecycle counters, frozen flight/landing tables, resource/cooldown outcomes, source approach payloads and existing control comparisons before accepting equality. It retains full raw observations rather than sampling away frames or deriving new tolerances. The original C++ geometry, ground-contact and cached-transform assertions continue to run; the consumer does not implement a second flight or collision simulation.

Preserve all fifty files before another complete run overwrites them. Add `--reference <directory> --reference-revision <full-commit>` to compare corresponding files exactly. Candidate and reference independently require full clean compiled revisions and matching supplied build settings. All files in each directory must share one coherent effective revision/profile, even without a reference pin; a historical reference can differ from the candidate. Unpinned comparison is diagnostic.

Collecting these synthetic no-draw records does not select the historical reference, run the final fresh-process campaign, establish visible carrier clearance/icons or original-host execution, or accept the complete behavior baseline.
