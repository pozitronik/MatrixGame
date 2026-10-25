# Registered capture completion recordings

The existing player-command factory/base completion tests persist 48 synthetic battles. They cover Context, Keyboard and Button capture routes, neutral/enemy initial ownership, both RNG modes and 10/16 ms schedules. The [capture profiles](85-capture-profiles.md) explain the underlying rules; these recordings expose the same journeys to independent semantic validation and qualified historical comparison. They add no Sessions, commands or simulation advances.

## Finite inventory

| Existing caller | Recorded Sessions |
| --- | --- |
| `game.outcome.registered_capture` | 4 neutral-factory Context profiles |
| `game.outcome.registered_enemy_capture` | 4 enemy-factory Context profiles |
| `game.outcome.registered_base_capture` | 8 neutral/enemy-base Context profiles |
| `game.player.capture_keyboard_completion` | 16 factory/base and neutral/enemy profiles through K press/release and left-click confirmation |
| `game.player.capture_button_completion` | 16 profiles through the capture button action and left-click confirmation |

Files are `scenario-traces/capture-command-<factory|base>-<neutral|enemy>-<context|keyboard|button>-<pm|crt>-<10|16>.jsonl` under the test working directory. Every combination already runs once in its owning caller. There are no original replay pairs or cross-route equality comparisons; the reader does not invent any.

The source retains seed 79, the default central-obstacle layout, initially selected player robots, unavailable audio and a 3,000-frame bound. The scene declares that existing default layout explicitly in its header. Each complete capture is followed by exactly 100 original map ticks: 1,000 ms at 10 ms, 1,600 ms at 16 ms. This is not a common wall-time tail. The 48 Sessions contain 41,118 completed map ticks in total.

## Generate and validate

Use the pinned Windows tools. All assets are synthetic; no game package or visible game window is required.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(outcome\.registered_(capture|enemy_capture|base_capture)|player\.capture_(keyboard|button)_completion)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(outcome\.registered_(capture|enemy_capture|base_capture)|player\.capture_(keyboard|button)_completion)$'
$revision = git rev-parse HEAD
python -B tests/check_capture_completion_traces.py build/mingw-debug-exe/tests/scenario-traces --configuration Debug --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
python -B tests/check_capture_completion_traces.py build/mingw-release-exe/tests/scenario-traces --configuration Release --compiler MinGW --target EXE --cheats OFF --trace OFF --revision $revision
python -B -m unittest discover -s tests -p test_capture_completion_traces.py
```

Each caller invalidates its whole 4/4/8/16/16-file family before setup. Recording files use buffered output and are closed after final scene assertions. Checked completion records are deferred until the entire caller returns, all fixtures are destroyed, allocation checks pass and `BaseDeInit()` returns. Setup, caller or cleanup failure cannot leave an accepted stale family. Footers retain final frame/time/RNG without reading destroyed objects.

## Observed state and lifetime

Headers retain actual compiled revision/dirty state, compiler/version, configuration, assertion and floating-point policy, target role, cheats, compatibility modes, RNG and timestep. They add route, kind, initial owner, owning caller, effective default layout and compiled `_TRACE`. MinGW reports a nonempty version string; MSVC reports a positive integer.

IDs 1-4 are the existing Session robots; the registered building is ID 5. Queue entries 6/7 are the two enemy base templates. They are opaque stack-owned identities: only current queue order and their explicitly initialized owner are serialized. They are never registered as map robots or observed through live robot layout. No additional engine-core lease is acquired to observe them.

Every snapshot combines the existing Session state with ordered logic membership, full initialized physical orders, raw capture targets and group intent, building owner/kind/paint/color/capturer/keepalive, applicable base phase/floor bits, opaque queue contents and four resource balances for both sides. Physical orders contain type, phase, three float-bit parameters, signed Param4 and normalized retained-core object. The common Session queue remains five fields; the custom view supplies Param3/Param4 and projects building/queue references back to the Session's unknown-object value -1.

The observer never calls `GetCaptureFactory()` or `GetStatic()`: the former uses the latter and can release an expired order core. Existing source getter calls remain in place, while added observation reads raw initialized cores. No cache-refresh/render access is added. Absent actors retain only ID/presence; non-live actors retain only ID/presence/life/state. Live-only fields are not read after base consumption or death. Deferred completion retains paths and scalar boundaries rather than borrowed objects.

Typed commands record the actual Context right-click, K press/release or `IF_ORDER_CAPTURE` element value `oca`, and left-click confirmation. They include client input, normalized target, source trace-position bits, preorder flags and RNG before/after the original actions. Synthetic hit-test inputs establish production command dispatch, not native screen picking or OS input delivery. Optional route instrumentation stays inactive for unrelated capture callers.

## Source milestones and guard rules

Factory milestones are first paint, enemy neutralization and ownership completion. Base milestones are opening complete, closing begun and ownership completion. Each snapshot forwards the source's three initialized scalar observations. The reader derives the same events from the recorded state and checks the measured schedule, complete frame sequence, actual command ordering, typed fields, opaque queue count/order/ownership and terminal RNG independently of equality.

| Transition | 10 ms | 16 ms |
| --- | --- | --- |
| Factory first paint | 630 | 640 |
| Neutral factory completed | 7,530 | 7,536 |
| Enemy factory neutralized / completed | 11,030 / 18,540 | 11,040 / 18,544 |
| Base opened / closing / transferred | 1,280 / 4,330 / 5,580 | 1,280 / 4,336 / 5,600 |

Base capture consumes the chosen robot and leaves its companion alive. At the transfer boundary, the source requires a closed base and null capturer. Later candidate scanning can assign another capturer during the tail; a final-tail null requirement would change the contract. Base paint/color likewise does not follow the factory's green-paint completion assertion. The reader preserves these distinctions. Factory capture instead retains the selected robot and releases its physical capture order.

Enemy base templates remain queued until transfer, then both are removed and all four costs (11,12,13,14) refunded twice to the old owner. The player keeps its initial 200 units per resource; the enemy starts at 100. Neutral base profiles have no refund. Queue insertion here is explicit fixture setup, not constructor input coverage. Resource periods are 100,000 ms, so these profiles do not establish continuous income; that remains a separate component journey.

## Historical comparison and evidence limits

Add `--reference <directory> --reference-revision <full-commit>` for exact comparison of matching filenames after independent validation. Historical and candidate commits may differ; each corpus must have coherent effective build/profile identity. References always require a full clean compiled source identity, independently of candidate pinning. An unpinned historical reference prints a diagnostic warning and does not establish accepted baseline evidence. Omitting `--revision` permits explicitly labeled dirty candidate diagnostics. An explicit pin must be a full lowercase 40-character commit; a reference pin also requires its directory.

CI runs the existing production-linked cases, binds the reader to the checked-out source and actual build profile, and retains the prefix through the existing `scenario-traces/*.jsonl` upload. Use matching compiler/target/cheats/trace options for other supported profiles. Python unit tests exercise malformed payloads, fixed actor-side domains, correlated omissions, opaque queue and lifetime rules, source milestones, tail completeness and independent provenance checks.

The source completion cases previously had assertions/stdout rather than retained vectors or replay comparisons. A passive before/after witness can preserve their actual state, source milestones, terminal RNG and original stdout; it does not manufacture a historical golden. The package covers these 48 command-completion Sessions only. Capture interruption/death/target loss, real placement, guarded/rival combat, autonomous strategy, continuous income, native visuals/input/audio and original-host behavior retain their separate owners. Final reference/resource qualification, the fresh-process campaign and complete-baseline acceptance remain separate work.
