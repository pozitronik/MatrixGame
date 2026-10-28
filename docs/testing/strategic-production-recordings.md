# Strategic-production recordings

The three existing `game.production.strategic_*` contracts now write twelve finite schema-1 recordings: deployment, deletion while waiting for resources, and deletion after payment, each with ParkMiller/LegacyCRT and 10/16 ms map steps. These are the existing Sessions and actions, with no added replay, map step or random draw. [The strategic-purchase guide](84-strategic-purchase.md) describes the design selection, income, refund and physical deployment rules.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.production\.strategic_(deployment|wait_base_removed|paid_base_removed)$'
python -B tools/check-strategic-production-recordings.py build/mingw-debug-exe/tests/scenario-traces --configuration Debug --compiler MinGW --target EXE --cheats false --trace false --revision (git rev-parse HEAD)
.\tools\test.ps1 -Configuration Release -Filter '^game\.production\.strategic_(deployment|wait_base_removed|paid_base_removed)$'
python -B tools/check-strategic-production-recordings.py build/mingw-release-exe/tests/scenario-traces --configuration Release --compiler MinGW --target EXE --cheats false --trace false --revision (git rev-parse HEAD)
python -B -m unittest discover -s tests -p test_strategic_production_recordings.py
```

A revision pin requires an exact full lowercase Git revision and a clean compiled source identity. Commit the source before producing pinned evidence. Use the actual compiler, target, configuration, cheats and trace settings; the example selects standalone MinGW without cheats or trace. Header identities include the compiler version, precision mode, assertion mode, compatibility settings, synthetic navigation layout and unavailable audio/rendering profile. MSVC records its numeric `_MSC_VER`; MinGW records its version string.

## Records and lifetime boundaries

Each file begins with the Session header and an observation before the original first `BuildRobot` call. Commands distinguish the initial choice, subsequent explicit strategic calls, queue recheck and base deletion. Tick observations follow each existing completed `Session::advance`; strategy observations follow the existing explicit call. The paid, queue-recheck and deletion boundaries are separate snapshots at the same map time. The reader verifies this complete ordered schedule, including the hundred post-deletion advances and strategic calls in each removal case.

Snapshots preserve the four Session actors, logical groups, RNG count, resource balances, waiting/previous-design indices, build count, base animation and FIFO identity, and created robot registration and initialized order pool. Physical orders retain all seven fields, including Param3, signed Param4 and normalized object identity; Session orders keep their existing compact five-field format. Float values retain their exact uint32 representations. Logical orders and physical queue orders use their separate declared enums.

The base is role 5 and the produced robot is role 6; the four fixture robots retain roles 1 through 4. Null is 0 and an external identity is -1. Before queue insertion there is no produced robot. While the queue owns an unregistered robot, the observer records only its opaque identity, presence and queue/list membership. It does not read its uninitialized state, environment or order storage. Once registered, it records initialized live fields; retired cores permit only absence observations. The already-existing constructor-price, two repair-module and squared detachment-distance samples are forwarded without refreshing poses or adding cache-mutating queries.

The reader checks the source's ten payouts, exact debit/refund, retained purchase history, deployment milestones, release of the base/manual-control restriction, final balances and total RNG interval. It checks the original sampled distance against the existing threshold rather than recomputing a trajectory. The successful trajectory continues to earn income after payment. The waiting-base case intentionally retains the dormant design after capacity disappears. The four deployment draws are an interval total, not an attribution of each later draw to a subsystem.

## Completion and comparisons

Each CTest owner truncates its own four output paths before any Session setup. Owners do not invalidate another caller's paths. A file remains incomplete if setup, an existing assertion, a later profile, a Session destructor, the tracked-allocation check or `BaseDeInit` fails. Completion tails contain only captured standard-library values and are appended with checked close after successful process cleanup. A complete file must still be accompanied by successful CTest/process results: completion is not a substitute for the process exit code.

The footer binds the last frame, map time and RNG count, snapshot count and FNV-1a digest of every complete observation payload plus its newline. The reader independently recomputes that digest and checks the semantic observations before accepting a file. This noncryptographic digest is a preservation diagnostic; campaign SHA-256 manifests identify the actual evidence files.

For a later same-profile comparison, retain an independent clean reference directory and pin its selected revision separately:

```powershell
python -B tools/check-strategic-production-recordings.py build/mingw-release-exe/tests/scenario-traces --revision <candidate-40-hex> --reference <reference-directory> --reference-revision <reference-40-hex> --configuration Release --compiler MinGW --target EXE --cheats false --trace false
```

Candidate and reference revisions may differ, but each directory must have one coherent clean source/build identity. Every file is independently validated before exact record comparison. An unpinned reference prints a diagnostic warning and does not select the final baseline reference. There are no original replay pairs or cross-ending prefix comparisons in these three cases, so the tool invents neither.

These recordings cover the selected strategic purchase trajectories. They do not cover the separate player-constructor, ordinary AI purchase, repair, other production contracts, native visuals/audio, a complete computer-side strategic loop or whole-game baseline acceptance. Final campaign identities, reference selection and native validation remain separate acceptance requirements.
