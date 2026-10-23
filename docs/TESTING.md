# Testing

Automated engine tests use CTest and small C++ console executables. They run with synthetic data, without game resources or an interactive window. Use the Windows x86 tools from the [build guide](BUILD_WINDOWS.md); building the base library still needs the normal compiler prerequisites.

The project registers tests directly with CMake's `enable_testing` and `add_test`. Optional CDash/dashboard tool discovery is omitted, so configuring the engine suite does not probe Windows registry entries for unrelated memory-checking tools.

## Run the automated tests

From the repository root:

```powershell
.\tools\setup-toolchain.ps1
.\tools\test.ps1 -Configuration Debug
.\tools\test.ps1 -Configuration Release
```

The helper configures the selected build, enables `BUILD_TESTING`, builds the `matrixgame_tests` target and runs every test labelled `engine`. It rebuilds changed test/library code before execution. EXE configurations also build the standalone game for the native startup tests; staging playable resources remains the responsibility of `tools/build.ps1`. A failed test, missing executable or empty selection makes the command fail. `MG_CHECK` assertions remain active in Release.

Command tests link the engine library and import the x86 `d3dx9_43.dll` component. The test helper prepares a checksum-verified copy from Microsoft's June 2010 redistribution package under `.tools/directx-x86` and stages it beside the test executable. The first run needs network access and 7-Zip. For offline preparation, use `tools/setup-directx-runtime.ps1 -ArchivePath <directx_Jun2010_redist.exe>` with the pinned package. This preparation does not install system components.

Run a subset by CTest name, using a regular expression:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^base\.crc\.'
```

Use `-NoBuild` when the selected build is already current, such as immediately after a full build. Changing source requires rebuilding before relying on the results.

```powershell
.\tools\build.ps1 -Configuration Debug -WithoutResources
.\tools\test.ps1 -Configuration Debug -NoBuild
```

Both helpers accept `-Compiler`, `-Configuration`, `-DLL`, `-Cheats`, `-Trace`, `-WithoutMusic`, `-ToolchainRoot`, `-CMakeRoot` and `-Jobs`. Use the same options for building and testing. Relative toolchain/CMake paths are resolved from the current PowerShell working directory. For MSVC, set up Visual Studio and the DirectX SDK as described in the build guide:

```powershell
.\tools\test.ps1 -Compiler MSVC -Configuration Release
```

Normal game builds also compile the test executables. `tools/build.ps1 -WithoutTests` sets `BUILD_TESTING=OFF` for a game-only build; the test helper enables it again on its next build.

The optional `-Trace` configuration builds all targets with the legacy `_TRACE` definition and registers `game.outcome.trace_result_callbacks`. It uses a separate build directory and does not change normal gameplay builds. [Trace result tests](testing/85-trace-results.md) distinguish the real generic Shift+Escape menu path from synthetic side-specific result injection and give focused commands.

## Results and direct CTest commands

Each named case runs in its own process, normally with a 30-second timeout. The native standalone startup cases, four `game.player.*_command_lifecycle` cases, sixteen [special/ordinary object command cases](testing/80-special-object-commands.md) and three `game.ai.bomb_escape_*_journey` cases have a 90-second timeout. Startup cases can run four bounded child launches; the lifecycle and bomb-escape cases execute complete profile matrices in one process. Object-command cases split each family by RNG and timestep, retaining every scene/route profile and replay assertion. These process limits do not change simulation or per-session bounds. CTest reports the failed case, assertion expression, source location and captured output. The helper writes `test-results.xml` in the selected build directory, replacing the report with the current selection's results; detailed output is in `Testing/Temporary/LastTest.log` below that directory.

MSVC Debug tests retain compiler runtime checks and send fatal CRT reports to standard error instead of waiting for a desktop dialog. Native failures include a best-effort symbolized stack when the executable's PDB is available, then exit with a failure status. This reporting applies to the console tests; interactive game diagnostics are unchanged.

To list or run already-built tests directly:

```powershell
$ctest = '.\.tools\cmake-4.4.4-windows-i386\bin\ctest.exe'
& $ctest --test-dir build/mingw-debug-exe -C Debug -N -L '^engine$'
& $ctest --test-dir build/mingw-debug-exe -C Debug -L '^engine$' --output-on-failure --no-tests=error
```

Always select the matching configuration with `-C` for MSVC's multi-configuration generator. CTest runs binaries already on disk; use the helper to build them first.

CI runs the engine suite after every game-build configuration. Test failures fail the build job and the aggregate Required checks gate. JUnit reports are retained as separate artifacts, including when a test fails. Contribution-policy tests remain a separate check.

For current command, navigation, AI and presentation completion work, see the [baseline completion checklist](testing/baseline-completion.md). It reconciles shared test evidence and distinguishes proposed remaining scenario selections from final baseline acceptance. Completed implementation and pending native execution have separate records in the [native validation ledger](testing/native-validation.md); automated regressions continue to run during implementation.

Synthetic native fixtures can use the [test-window containment helper](testing/native-window-containment.md) to keep their own game/loading windows hidden and adapt desktop cursor operations. Its cursor contracts run without a graphics device; its import/hidden-window checks are separately opt-in. Neither establishes visible presentation or original-host integration.

## Controlled battle scenarios

The `game.scenario.*` cases run synthetic battles through `CMatrixMapLogic::Takt`, including production robot movement, pathfinding, side AI, effects and outcome evaluation. The fixture starts with selected unarmed wheel chassis; context commands enter `CMatrixSideUnit::OnRButtonDown` with a supplied world trace. This exercises its ordinary validation, footprint conversion, group assignment and synchronous command execution. Initial selection is fixture setup; these cases do not claim coverage of screen picking or selection acknowledgement sounds.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(scenario|harness)\.|^base\.random\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(scenario|harness)\.|^base\.random\.'
```

For synthetic building geometry, queue deployment and exact base-exit lifecycle contracts, see [Synthetic building fixtures](testing/building-fixtures.md).

`BattleGeometry` accepts a navigation layout; `CentralObstacle` remains the default. The default world is a 64 by 64 movement grid with flat terrain, real terrain groups, navigation zones/places and an obstacle occupying cells `[28,36) by [20,44)`. Small generated storage-format meshes supply actual animation, bounds and chassis attachment data. Temporary mesh files are eagerly loaded and removed; supplied game packages are never read. The production pathfinder chooses the route. Two friendly chassis start at `(100,200)` and `(100,260)` in world coordinates; unarmed autonomous enemy chassis start at `(500,500)` and `(540,540)`.

| Contract | Evidence |
| --- | --- |
| Movement | Both selected chassis pass around the obstacle and reach assigned places on its far side; every frame checks finite positions, map bounds and obstacle exclusion |
| Replacement | A second context command replaces the first destination during movement and both chassis arrive in the new area |
| Target removal | An attack command binds an existing enemy; direct deletion through `CMatrixMap::StaticDelete` retires it; the reference stays stale until the next scheduled player-AI update, then becomes null while the ground destination remains unchanged |
| Rejection | Production ignores a ground command without a selection; the fixture rejects unknown and retired scenario IDs before production dispatch, with no observed state change |
| Repeatability | Same-profile runs in one process produce identical ordered snapshots; each RNG mode is checked independently |
| Observation | Recording every frame leaves the terminal snapshot and raw RNG draw count identical to a run without intermediate snapshots |
| Cleanup and bounds | Failures after topology, entity and selection setup are followed by a successful session; frame limits reject further advancement; tracked Debug allocations and synthetic device references must be released |

The primary profile uses seed 79 and 10 ms ticks. Movement also runs a separate 16 ms profile; its frame count represents its own schedule, not an equivalent 10 ms trajectory. Both ParkMiller and LegacyCRT are reseeded before constructing the map. The new `random::draw_count()` observer counts raw draws from the shared production stream without changing values, range mapping or consumption. Direct CRT calls outside this wrapper are outside its count. These unarmed scenarios consume some random draws, but neither the varied mode nor its generated values has demonstrated an effect on the recorded gameplay state. They do not establish random-dependent gameplay behavior; a scenario that does must include a seed/mode negative control.

Presentation dependencies are explicit. A COM-compatible test device stores vertex/index buffers, material/state data and bounded, single-level managed ARGB textures in CPU memory; it aborts on drawing and unsupported texture operations. [Material fixtures](testing/material-fixtures.md) defines the supported texture contract and the production assembly and demolition paths it enables. Unset device states return `D3DERR_INVALIDCALL`, rather than native Direct3D defaults; the current scenarios do not reach a caller that relies on those defaults. A scenario reaching such a path must supply the needed state or extend the surrogate with verified defaults. Real water and movement effects still construct and tick. A cursor presentation surrogate and the existing diagnostic-overlay surrogate avoid loading artwork or displaying UI. Synthetic audio uses one fixed PCM mapping; non-looping voices finish at the next frame boundary, while loops remain active. An unavailable-audio profile is checked separately. These choices do not establish rendering, native audio timing, Win32 input, device-reset or original-host compatibility; those remain separate contracts and native checks.

MinGW scenarios select x87 single precision to match the normal x86 Direct3D device profile, check it before and after each map step, and restore the previous setting at teardown. A negative control changes precision and verifies rejection before any step occurs. MSVC keeps and records its compiler/runtime profile; this synthetic profile differs from the native MSVC game running under a Direct3D device that selects PC24. Exact comparisons apply within one profile. Debug/Release or compiler comparisons need explicit analysis; a successful same-build replay is not proof of equivalence across profiles. The fixture preserves process counters such as robot UID and selection time, normalizes entity identity, and checks repeatability without resetting them. It does not promise arbitrary full-game restart equivalence.

### Traces and diagnosis

CTest writes JSONL files under `build/<compiler>-<configuration>-<kind>/tests/scenario-traces/`. Every file begins with a header identifying the source revision, tracked dirty state, compiler, configuration, relevant defines, delayed-damage attribution, AI group-count, place containment and zero-X trace modes, RNG mode/seed, precision, viewport, fixture, audio/render policy and step bounds. Build-time generation refreshes revision and tracked dirty state on incremental builds without requiring reconfiguration; `-NoBuild` preserves the identity compiled into the executable. Untracked files are not included in the dirty flag.

Command records describe scheduled interventions, raw RNG draw counts immediately before and after dispatch, acceptance observed from the resulting group order/destination/target, and those resulting values. The production context-command entry point returns no acceptance flag; the recorded flag is an outcome assertion, not an internal engine event. For direct deletion it records the fixture operation's return value. Snapshots record completed boundaries using stable scenario IDs, raw float bits, orders, targets, player-group membership/counts and RNG draw counts. A target value of `0` means null, `-1` means a non-null reference outside the registered live entities, and a positive value identifies a registered entity. `target_is_null` reads the actual group pointer independently of ID normalization. The deletion scenario proves that a stale target stays distinguishable from null before the next scheduled cleanup, then asserts both nullness and unchanged destination afterward.

The runner prints the profile and trace path before commands, and includes the current frame in caught assertion failures. It flushes the last completed boundary before advancing, so a native failure leaves the profile and an incomplete trace for diagnosis. A completion record is written only after scenario assertions pass; incomplete traces cannot be accepted as matching runs. CI uploads these traces with the test reports.

```powershell
python -B tools/compare-battle-traces.py <reference.jsonl> <candidate.jsonl>
```

The comparator rejects incompatible profile headers by default, checks command records as well as snapshots, and reports the earliest differing record and the alphabetically first differing field within it. `--allow-profile-difference` waives only configuration, compiler/version, assertion mode, precision control, EXE/DLL mode, delayed-damage attribution, AI group-count, place containment and zero-X trace modes and cheats. The attribution and the three modes follow the build role by default: corrected in the EXE and original in the DLL. Scenario identity, including seed, RNG mode, tick interval, fixture and presentation policy, must still match; it does not introduce tolerances or establish that those profiles ought to match. Keep accepted reference traces with the validation evidence for the behavior being frozen. The initial harness asserts the bounded gameplay contracts above and reproducibility; it does not constitute the complete whole-game baseline. It does not record transient internal events or automate a comparison across three fresh processes. Direct deletion bypasses damage and the `ROBOT_DIP` dying state, so cleanup of an inactive object still present in the live logic list remains uncovered. These need additional bounded scenarios or observation seams before claiming the corresponding research contracts.

### Shared scenario fixtures

The [player command guide](testing/player-orders.md) maps input and cheat contracts to the behavior catalog and distinguishes real form routes from supplied fixture preconditions.

The [synthetic armed fixtures](testing/armed-fixtures.md) describe generated attachment meshes, weapon/effect dependencies, ownership and bounded firing evidence. Scenario-specific fixture guides live under `docs/testing/`; this page remains their navigation entry point and the authority for common validation rules.

The [synthetic building fixtures](testing/building-fixtures.md) describe building geometry, queue deployment, exact base-exit transitions and safe teardown constraints.

The [synthetic material fixtures](testing/material-fixtures.md) describe CPU textures, production skin loading, constructor assembly and complete building demolition. Their material-bearing models extend the no-render profile without establishing native graphical correctness.

The [synthetic reinforcement fixtures](testing/reinforcement-fixtures.md) cover real request, carrier flight, cargo landing and retirement under an explicit unavailable-icon-renderer profile.

The [construction and production contracts](testing/83-production.md) cover queue ownership and refunds, income, AI and player purchase, and reinforcement gates. They distinguish tested callbacks and components from remaining rendered UI and strategic coverage.

The [combat contracts](testing/82-combat.md) describe firing, damage, status and object lifetime oracles; select them with `^game\.combat\.`.

The [navigation contracts](testing/81-navigation.md) describe alternate topology, local movement and carrier lifetime; select them with `^game\.navigation\.`. The [road-ring journeys](testing/81-road-guided-journeys.md) add paired regional-guidance and damage-driven blocker controls with complete-session replay.

The [presentation evidence guide](testing/87-presentation.md) maps PRES-01 through PRES-04 to interface/audio contracts, registered clear-rectangle and plane regressions, unregistered legacy command aliases and native procedures.

The [capture and battle outcome guide](testing/85-battle-outcomes.md) describes capture transitions, survival and objective rules, transport lifetime and result/pause boundaries.

The [autonomous transport targeting case](testing/85-transport-ai.md) distinguishes carried-robot acquisition and aiming from actual firing after landing, with empty-carrier rejection, a ground-combat control and both-RNG replay.

The [standalone selector continuation cases](testing/85-map-window-continuations.md) connect the battle menu and win/loss/surrender statistics to actual selector input, cancellation and persisted same/different-map choices. They stop at the relaunch request; full process startup and the parent/child handshake remain separate lifecycle checks.

The [owned-session cleanup and Restart failure guide](testing/85-restart-failures.md) distinguishes the passing loop/teardown control from two opt-in terminating failure probes. Missing-map callback containment and corrupt-storage recovery remain separate correction prerequisites. Debug leaks during production cleanup can still show the engine's modal leak report, as documented in that guide.

The [loop scheduler contracts](testing/85-loop-scheduler.md) run production `L3GRun` with a scripted platform, freezing speed scaling, timestep capping, inactive waits and quit boundaries. The [moving-battle cases](testing/85-loop-movement.md) attach the real game form, typed speed/background cheats and moving robots in both cheat configurations. They remain separate from native focus delivery, full initialization and game-owned cleanup.

The [result-loop cases](testing/85-loop-results.md) carry menu Exit/Surrender and typed or trace diagnostic callbacks through actual loop return, with cancellation/resumed-movement and compile-gate controls.

The [Restart-after-toggles case](testing/85-loop-restart.md) connects typed speed/KEEPALIVE flags, moving robots, paused introductions and two storage-backed reloads within the production loop. It measures four-speed scheduling after reload and retains the KEEPALIVE bit; focus changes after Restart are not exercised. Full initialization, game-owned cleanup and native delivery retain separate coverage.

The [strategic and tactical AI guide](testing/84-ai-decisions.md) describes decision fixtures, declared initial state and separately tracked correctness probes.

### Extending the fixture

Reuse `tests/battle_scenario.hpp` and its implementation for a bounded scenario, or extend the synthetic initial data in `battle_geometry.cpp` when a new behavior requires it. Keep scenario setup distinct from actions. Add commands through the relevant production entry point; never write a resulting path, group order or AI decision to simulate acceptance. Entity lookup checks the live logic list and original UID before dereferencing stored pointers. Observers must avoid mutating helpers such as `SOrder::GetStatic()`, reference-acquiring `GetCore()` and lazy geometry preparation.

Add explicit outcome and intermediate-state assertions, including rejection or retirement where relevant. Trace comparisons can diagnose changed behavior but cannot replace an assertion about what the scenario establishes. Bound execution by frame count, game time and CTest timeout; the between-frame deadline cannot interrupt a stuck engine tick. New event requirements may need separately reviewed, behavior-preserving access seams. This harness does not authorize structural changes before the complete behavior-baseline gate is accepted.

## Initial coverage

| Area | Contracts exercised |
| --- | --- |
| Checksums | Empty input, frozen engine checksum fixtures, incremental/one-shot agreement, binary bytes and selected byte ranges |
| Points | Coordinate arithmetic and squared distance |
| Rectangles | Empty bounds, strict interior containment and normalization |
| Storage | Record/column growth, schema copies, deletion/reuse, duplicate parameters, UTF-16 values, legacy bytes, compressed round trips, rejected format tags/versions and partial-load cleanup |
| Dependency formats | Frozen compressed configuration and two-block package data from zlib 1.2.11, multiblock UTF-16 configuration, linked header/library agreement, frozen grayscale/RGB/RGBA PNGs from libpng 1.6.37, padded BGR write/read conversion and rejected PNG header/truncation |
| Random numbers | Original generator sequence, seed normalization, range endpoints, reversed/equal bounds, fractional scales, index bounds and shared-stream consumption |
| AI robot definitions | Head aliases and resource valuation, headless definitions, weapon strength ordering and missing armor-capacity diagnostics |
| Audio | PCM8/16 mono/stereo parsing, rejected chunk/frame metadata, shared clip caching, unique handles, loop flags, gain/balance, failure diagnostics, shutdown order, frontend layers and preserved host callback routing; battle music queueing, track order, failures, format changes, levels, sessions, host routing and the map's music fade |
| Battle scenarios | Production map stepping, obstacle movement, replacement, target removal, rejection, repeatability, read-only observation, partial setup and bounded execution |
| Player orders | Real keyboard auto-order replacement, slot reassignment and stale counts, toggle-to-stop, preorder suppression, partial fixture setup and repeated synthetic sessions |
| Bomb commands | A moving bomber retains its ground order when the object target is absent, inactive or destroyed; target validation and fixture cleanup use production implementations |
| Lifecycle | Repeated cursor cleanup and reload, empty configuration, form transitions and constructor unwinding, partial game/standalone teardown, cleanup before cache creation, continued cleanup after C++ failures, owned/borrowed graphics references and rejected graphics configuration before window creation |
| Keyboard and mouse state | Independent key releases and repeats, focus-loss reset, fresh presses, configured actions, mouse-camera and console Shift release through the real form, and keep-alive activation dispatch without deactivation/resource changes |
| Selection | Queued mouse moves without geometry scans, one refresh of the latest rectangle, release before a frame, reversed/small rectangles, hit masks, the existing 30-object order/limit, direct/group-number cancellation followed by immediate release and object teardown before refresh |
| Device recovery | Resource release/reset/restore order, waiting during loss, transient and permanent reset failures and presentation error handling |
| UI font lifetime | Existing font definitions and stable cache identity, owned-interface release, repeated loss/reset notifications, absent fonts, partial creation and callback failure diagnostics |
| UI text | Words/spaces, case-insensitive inline colors, Unicode views, CRLF/LF/lone CR, guarded input bounds, invalid channels, many adjacent tags and line-height accounting |
| Developer input | Cheat suffix/history matching, flag toggles, capped resource changes, console activation and editing |
| Error diagnostics | Omitted secondary messages, errors without an active Debug trace, file/line metadata and missing-file operation/path diagnostics |
| DLL entry | Engine/standard/unknown initialization exceptions, form/loop/result exceptions, cleanup before returning, preserved success codes and callback layout; DLL configurations also load the real library with rejected synthetic configuration |
| Executable paths | Unicode and long module paths, native image lookup, API failure diagnostics and bounded retry of truncated module queries |
| Compiler/runtime | Catching a C++ exception across a callback boundary, running stack cleanup and reporting an assertion with a normal failure exit while retaining a representative global configuration layout |

The checksum fixtures preserve the engine's existing data compatibility contract. Tests link the production `MatrixLib` target and use its real implementations. The console executables use static MinGW runtimes so they can run outside the compiler environment.

`matrixgame_player_command_probe` registers `game.orders.*` contracts. Run them with `tools/test.ps1 -Configuration Debug -Filter '^game\.orders\.'` and repeat in Release. They use a mesh-free robot selected by fixture setup and the real form keyboard handler; U/C/U checks replacement and slot reuse, U/U checks stop and synchronous group recount. Failure injection checks interface-alias restoration during constructor unwinding. These tests have no audio/device or loaded map and do not establish picking, movement, combat, full-session cleanup or whole-battle replay. See the [production harness investigation](research/77-production-harness/README.md) for the exact fixture limits.

DLL entry tests link the production callback boundary with deterministic session operations to exercise exceptions and successful return values. Failure cases hold the regular `test.log` open without write sharing and verify the dedicated `matrixgame-dll-errors.log`. DLL build configurations also register a synthetic host that loads the actual `MatrixGame.dll`, supplies a rejected packed configuration three times and checks return `100`, untouched result statistics, the host window/procedure, released configuration handles and recorded diagnostics. This case uses a message-only window and needs no game assets or graphics device. A DLL test build also builds the library and stages its imported MinGW runtimes; `tools/test.ps1` supplies the x86 DirectX runtime beside the test host. These checks establish the failure boundary and callback contract; a complete original-host battle requires separate integration evidence.

The dependency fixtures are synthetic bytes encoded with zlib 1.2.11 and libpng 1.6.37. `matrixgame_dependency_tests` loads them through the production storage, package and bitmap paths. Package checks read across the 65,536-byte block boundary and seek back into it; configuration checks preserve duplicate keys, Unicode and the 65,000-byte compression boundary. New writes are compared by decoded content, not identical compressed streams. The frozen fixtures define decoding compatibility independently of the installed dependency versions.

Build-tool and packaging checks use Python's standard library and isolated fixtures below `build/`, without game assets or a desktop window. Run `python -B -m unittest discover -s tests -p 'test_*.py'`. CMake setup rejects a bad archive before extraction, DirectX SDK setup keeps a supplied installer and refuses one inside the directories it replaces, and packaging checks retain the committed dependency manifest in source identification.

Release automation checks use temporary Git repositories, synthetic archives and mocked GitHub requests. They cover release-tag ancestry, changelog selection and generated-note fallback, checksums and architecture identity, draft reruns and refusal to modify published releases. Run them separately with `python -B -m unittest discover -s tests -p 'test_release.py'`; see [Preparing a release](RELEASING.md) for the tag workflow.

Storage record copies intentionally recreate an empty schema, while moves preserve owned column buffers. Storage objects have single ownership and support moves. The fixture checks preserve the existing STRG versions, ZL03 framing and swap-with-last record deletion order. The storage executable initializes the base services, supplies a console logger and checks for tracked heap leaks in Debug.

To check the configuration-packing operations used by `BUILDCFG` against the tracked text files, run these commands from the repository root after building the tests:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
.\build\mingw-release-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
```

This local smoke check parses the interface/data configuration, removes runtime replacements, packs the `if` and `da` roots in memory, restores them and compares their parameter/block trees. It leaves externally supplied resources and `robots.dat` unchanged. The CTest suite uses synthetic fixtures; invoking the console command in a running battle remains a playtest.

The default random-number generator uses the original map simulation's Park-Miller sequence (`16807`, modulus `2147483647`), with the output shifted down by one. Seeds are reduced modulo the modulus; a zero result selects state one. Integer results span `0..2147483645`; floating results span the closed interval `0..1`. The fixed integer sequence is portable across the supported compilers. Standalone `CFG/standalone.txt` can select corrected legacy CRT behavior; [Battle random generation](RANDOM.md) documents both modes, seed/range/draw rules and compatibility limits.

Integer and finite floating bounds are normalized when reversed and include both endpoints. Ordinary integer ranges use modulo mapping, which carries modulo bias; wider ranges combine enough raw draws using 64-bit arithmetic. Equal bounds consume one draw. `IRND(n)` selects an index in `0..n-1` for a positive count and returns zero otherwise. The fractional-scale helpers take double arguments and return float values; their scales should be finite and representable as float.

Default `IRND` uses integer modulo mapping instead of rounded interpolation. Corrected legacy mode retains the previous endpoint weighting while keeping fractional/range safety fixes. Tests freeze both raw sequences, full-width draw consumption and exhaustive four-index legacy counts. Configuration tests cover missing/explicit/invalid selection, unreadable text, mode stability and initialization before the real map constructor consumes its first draw.

Simulation and effects still share one seeded stream, so changing the order or number of effect calls can change later simulation draws. Stream separation would require an explicit gameplay decision. Floating-point mappings are checked by their range/endpoint contract; the suite does not claim cross-compiler replay of an entire battle.

The AI robot executable compiles the production definition loader and pricing code using the common engine compiler options. Synthetic configuration prices and armor weapon capacities let it check definition parsing, total resource costs and selection ordering without a map, renderer or game package. Robot mesh assembly and AI construction in a running battle remain playtests.

The bomb executable links `MatrixGameInternal` and constructs a synthetic map, side, robot and idle weapon through their real constructors. Map and robot objects use the same zeroed engine heap and matching destruction as production; their constructors do not initialize every member for ordinary stack allocation. A narrow fixture grants access to weapon state so meshes are unnecessary. A test diagnostic overlay records messages and rejects drawing or device transitions. The cases exercise `TaktPL`, including clearing an inactive or destroyed target, and check that no graphics device or loaded cache entries appear. Debug checks also require all tracked allocations to be released, including visual helpers normally retired by drawing. Actual detonation, effects, selection and UI/manual-control equivalence remain playtests.

The input executable compiles the production keyboard-state code and supplies synthetic action bindings. Form input and cheat executables link the engine and construct a synthetic map/side with the diagnostic overlay substitute. They exercise ordinary deactivation, middle-button release, console modifiers/editing, input-only reset with graphics/activity preservation and selected developer handlers without physical input or an interactive window. Tests call cheat handlers directly regardless of the form's compile-time recognition setting; visible gating and complete command behavior remain playtests.

The text executable compiles the production parser with deterministic font metrics. A synthetic memory view ends at an inaccessible page so reading beyond its length fails the case. It uses no private strings or game assets. Actual glyph dimensions and pixel rendering remain native/playtest checks. [Controls and UI text](CONTROLS.md) records the supported syntax, controls and limits.

Keep-alive cases call the production window procedure and game form with synthetic map/side state. They check an input-only notification on activation loss, release all 256 tracked keys, clear the last-key marker, preserve application/map flags and accept fresh input. Regain does not synthesize a second reset; ordinary deactivation remains covered. The tests use no window/device and do not establish visible fullscreen or mouse-drag behavior.

The error executable checks catchable engine diagnostics using literal and string messages, an empty Debug trace and a nonexistent synthetic file path. It suppresses native error dialogs so a crash fails its CTest process rather than waiting for desktop input. These cases do not validate a live startup exception, renderer teardown or the standalone message box.

The executable-path cases link the production module-path reader. Synthetic Windows API replies check buffer growth and error handling; the native case resolves the console test image without creating a window. Actual cmd.exe launch spelling, map arguments and startup diagnostics require the separate standalone checks described in the build/playtest guides.

The `game.loading.*` cases compile the production battle-loading implementation. They validate private event names, keep the event unsignaled after failed, occluded or mode-change presentations, release it after `S_OK` or startup failure, and launch a console child to check readiness and premature exit. Additional cases check a bounded timeout, cancellation, readiness priority, invalid handles, an expired parent event and pumping a message-only window. The `dismiss_button`, `dismiss_close` and `dismiss_escape` cases create the actual loading window on a private Windows desktop, without switching the user's desktop. They send a button click, Close or Escape to its native controls, verify window destruction and confirm that a gated child remains alive and can subsequently report readiness. These tests require permission to create a desktop; failure is reported rather than skipped. They use no game packages, graphics device or artificial loading delay. `game.startup.arguments`, `game.startup.relaunch_command` and `game.startup.action_keys` exercise the production argument classifier, command construction (decoded by Windows with spaces and Unicode) and action-key latch. They preserve ordinary map arguments, including names that merely contain the reserved token. Display shortcut tests also cover an ordinary Enter after a lost Alt+Enter key release.

The four windowless `game.startup.deployment_cleanup_*` cases check native Windows file-sharing failures using a nested synthetic file. Startup deployment teardown retries only system-category `ERROR_SHARING_VIOLATION`, for at most 151 attempts with 20 ms between attempts (3,000 ms total requested delay). This bounds requested sleeps rather than total wall-clock time; filesystem calls and scheduling add time. Persistent sharing failures and all other errors remain hard failures. The contracts check synchronous lock release after the first failure, bounded persistent-lock exhaustion, first-attempt read-only handling (native access-denied with the file retained, or successful removal where the standard library supports it), ordinary removal and an already-absent directory; they use short zero-delay retry budgets without wall-time assertions. These cases launch no process and have no `native-process` label.

Five `game.startup.native_*` cases launch the built `MatrixGame.exe` in synthetic space/Unicode deployments and inspect startup diagnostics on private desktops. The additional `native_diagnostic_readiness` case starts a synthetic helper dialog in a foreign process to check staged caption readiness. A normal EXE test run includes eight game child launches and one helper child; none switches the user's desktop or loads a playable map. Desktop creation and job assignment must be permitted; failures fail the test rather than skipping it. A kill-on-close job terminates the child if the harness is interrupted, although an interrupted run can leave its synthetic deployment directory for manual cleanup. These six cases carry the dedicated `native-process` label. To run the already-built engine suite without these game-process launches, use:

```powershell
& '.\.tools\cmake-4.4.4-windows-i386\bin\ctest.exe' --test-dir build/mingw-debug-exe -C Debug -L '^engine$' -LE '^native-process$' --output-on-failure --no-tests=error
```

This exclusion covers the six native startup cases and the optional [synthetic native DLL session cases](testing/85-native-sessions.md), including [packed configuration and pneumatic cleanup](testing/85-native-packed-sessions.md), [demo and cheat-toggle lifetimes](testing/85-native-mode-reentry.md), [loaded-demo AI/camera continuations](testing/85-native-demo-ai.md), [autonomous native demo combat](testing/85-native-demo-combat.md) and [populated building/cannon Restart](testing/85-native-population-restart.md), when those cases have been explicitly enabled. The DLL fixture requires a Direct3D 9 HAL adapter and renders through child windows under an invisible parent on the current desktop. Fixture-local imported adapters contain cursor operations and capture diagnostics, including Debug leak reports, as scenario failures. Its registration persists until `MATRIXGAME_NATIVE_SESSION_TESTS` is configured `OFF`; use the same label exclusion when running the remaining suite. The loading-window tests and other native in-process checks still run. See [native standalone startup failures](testing/87-presentation.md#native-standalone-startup-failures) for the exact failure contracts and resource limitations.

The same exclusion covers the [contained standalone lifecycle checks](testing/85-native-exe-lifecycle.md), registered as `game.session.exe_*` only with `MATRIXGAME_NATIVE_EXE_TESTS=ON`. They run production `WinMain` with generated resources through startup, natural results, statistics, selectors, process relaunch, readiness, failures and cleanup. They start game processes with real Direct3D 9 devices under an invisible parent on the current desktop; imported window, cursor and Documents-folder adapters contain presentation and generated output. The option remains enabled in a build directory until explicitly configured `OFF`, so restore it after an opted-in run. These cases carry both `native-process` and `current-desktop`; the exclusion above omits them. Visible presentation, desktop focus and original-host lifetimes remain separate requirements.

The exclusion also covers opt-in `game.session.native_special_scenes`, `native_break_anim`, `native_anim_break`, `native_terron` and `native_special_diagnostic`. Their [mixed-objective scene guide](testing/85-native-special-scenes.md) and [outcome guide](testing/85-native-special-outcomes.md) separate generated inputs, automated outcomes and pending visible observations. They use a real Direct3D 9 HAL device under an invisible parent on the caller's current desktop. Fixture-local imported adapters keep game windows hidden without activation and cursor state private; captured diagnostics fail the run. The cases retain `current-desktop` and `native-process` labels and are not asset-free; default CI runs only windowless export-layout and ownership cases. Their registration persists with `MATRIXGAME_NATIVE_SESSION_TESTS=ON`; restore it to `OFF` after a native run. The standalone manual scene procedure additionally preserves and restores its two minimap cache files under Documents.

The settings integration case `game.presentation.settings_display_rollback` carries the `current-desktop` label: it creates a hidden top-level `STATIC` window on the caller's current desktop, rather than a private desktop. It never shows or activates that window, confines the cursor or creates a native graphics device; the CPU device supplies scripted Reset outcomes. To exclude this case as well as the game-process cases, replace the preceding command's label exclusion with `-LE '^(native-process|current-desktop)$'`. The private-desktop loading-window cases still run; add `-E '^game\.loading\.dismiss_'` to omit those too. The two other settings-window integration cases create no window. See [settings-driven display rollback](testing/87-presentation.md#settings-driven-display-rollback) for the exact boundary.

The default-registered `game.loop.restart_focus` also carries `current-desktop`; the label exclusion above omits its hidden-parent activation/input-reset checks after Restart. See [focus after Restart](testing/85-loop-restart.md#focus-after-restart) for its containment and native-focus limits.

The opt-in `game.session.window_*` import-containment checks also carry `current-desktop`: they create a hidden parent, verify their own imported adapters before synthetic cursor calls, and observe import restoration. They neither launch a game nor create a Direct3D device. CI explicitly enables them for the build matrix; the local label exclusion above omits them when enabled. See [native-window containment](testing/native-window-containment.md) for their limits.

Separate native startup checks remain: `StartupMenu::run` and the early Exit return from `CGame::Init`, rendered menu-to-battle device reset, actual `MapSelection::relaunch` event creation/collision and process-creation failure, visible loading-screen presentation and focus, first rendered battlefield, exclusive-mode focus transfer and complete selector input routing. See [Map selection](MAPS.md#verification). The optional reset case below covers a synthetic device reset, not a loaded battlefield. Any resource-backed hidden startup harness not included in the repository is a one-off local check, not a registered or reproducible repository regression.

Resource-file cases check actual temporary files with the production standalone query: no packages, independent audio presence, exact filenames, directories and a registered but unopened archive. `game.startup.resources_damaged` opens synthetic packages through the production startup helper: a damaged optional package counts as missing while the others stay open, `music.pkg` is ignored without a decoder, only damaged packages leave audio disabled, and a valid `sound.pkg` still opens without `robots.pkg`. They preserve supplied resources and create no window. Audio frontend coverage also checks that absent optional packages disable and release a prior service without reading mappings or creating a device. Device-error coverage injects a critical error through the fake device. `game.audio.device_recovery` recreates the service and music through the production startup path, keeping the effects, music and fade levels. Old layered handles no longer report a sound, old bridge handles reach nothing in the new service, and new handles continue the sequence. Music continues with the track after the one that was playing. `game.audio.device_recovery_limit` checks the bound of three recreations, a fresh budget after a new standalone initialization and a failing recreation. `game.audio.installed_device_error` releases a directly installed service without creating a device in its place. `game.outcome.form_audio_device_error` checks that `CFormMatrixGame::Takt` handles the error even when a lost device skips the map. The native XAudio2 callback and a real endpoint change need the listening check in [Standalone sound](AUDIO.md#check-sound). The missing-game-package dialog is automated by `game.startup.native_missing_resources`; full package loading and successful battle startup remain separate native checks.

The audio service tests use synthetic WAV bytes and a fake device; frontend cases link the real command/layer implementation with a non-rendering diagnostic substitute. They cover layer interrupt/skip, stale slot reuse and repeated clearing through standalone and synthetic host callbacks without initializing an audio device or opening private archives. A local, muted hardware smoke test explicitly exercises XAudio2 creation, looping, balance and teardown:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_audio_tests.exe manual.audio.native_device
.\build\mingw-release-exe\tests\matrixgame_audio_tests.exe manual.audio.native_device
```

This opt-in check requires a working Windows 10/11 audio endpoint. It emits no sound and is not registered in CTest. Audible game checks follow [Standalone sound](AUDIO.md).

Battle music has two layers of checks. `game.audio.music_*` drive the production music player with synthetic PCM decoders and a fake stream voice: the bounded queue of 250 ms buffers, looping and track order, the track a later session starts with, one diagnostic per failing track (unreadable, rejected, empty, corrupt mid-stream or unsupported layout), a voice replaced only after its queue has played out when the format changes, the product of the listener and fade levels, and device failures. `game.audio.vorbis_decode` and `game.audio.vorbis_rejects`, registered only when `MATRIXGAME_MUSIC` is on, encode synthetic tones with libvorbisenc and decode them through the production Ogg Vorbis decoder, including partial reads, mono and stereo layouts, non-Ogg, truncated and corrupted input. The frontend cases `game.audio.music_sessions`, `game.audio.music_unavailable` and `game.audio.host_music_routing` replace only the platform device and the decoder factory: they list loose synthetic tracks, retain `MusicVolume` across sessions, keep it independent of `SoundVolume`, start each session after the previous track, run without tracks or with an undecodable one, and route the fade level to host callbacks in the DLL role. The first two are registered with the decoder; without it (`MATRIXGAME_MUSIC=OFF`, which includes every DLL build) `game.audio.music_not_built` checks that loose tracks are ignored, music stays unavailable and effects still play. CI therefore checks the decoder in the EXE jobs and the build without libogg and libvorbis in the DLL jobs. `game.audio.music_track_names` checks the case-insensitive track order and that packaged and loose names differing only in case form one track. `game.outcome.music_fade` fades music out and back in through `SetMusicVolume` and `RestoreMusicVolume`, the calls made by the Terron death and the `MUSIC` console command, on a running scenario map. `game.outcome.music_form_refill` drives `CFormMatrixGame::Takt`: under both supplied timesteps, two lost-device frames fill and then refill an explicitly drained music queue without advancing the map, and a subsequent healthy-device frame refills it and advances the map. The display-change return before it cannot be reached in a scenario, whose display service is inactive; the listening check covers it. The native XAudio2 stream voice, the original `music.pkg` tracks and audible continuity need the [listening checks](AUDIO.md#check-sound).

The lifecycle executable links production game cleanup, configuration, map, cache and forms with the same diagnostic overlay substitute. It checks empty and partially populated state without invoking interactive startup. [Session lifetime](LIFECYCLE.md) describes ownership and the boundaries this establishes. Menu restart, result dialogs, actual startup failure handlers and graphics-device teardown remain playtests or require separate regressions.

Recovery tests compile the production decision path and font cache. Synthetic HRESULTs and COM fonts keep them independent of physical graphics hardware and game packages. An explicit native font-reset check is available outside CTest; [DirectX device recovery](DEVICE_RECOVERY.md) describes the procedure, covered transitions and limits. Visible restored graphics and complete renderer resource lifetimes still need playtests.

The lifecycle suite also checks visibility requests before cursor images are loaded and after they are cleared, with both a null device and a non-null sentinel that must never be dereferenced. This distinguishes the unloaded-image guard needed during partial cleanup from the no-device guard. The optional native startup-reset check renders synthetic menu geometry and text on a hidden DirectX 9 window, invokes the production battle video setup, and draws again after each of three resets. It needs the native DirectX runtime and a graphics device, but no game packages; it is deliberately not registered with CTest. Its narrow friend accessor reaches the existing private video setup without changing startup behavior. It does not load a battlefield or replace the interactive map-selection playtest.

```powershell
.\build\mingw-debug-exe\tests\matrixgame_lifecycle_tests.exe manual.lifecycle.startup_reset
.\build\mingw-release-exe\tests\matrixgame_lifecycle_tests.exe manual.lifecycle.startup_reset
```

Construction, rendering, lifecycle, audible/device audio behavior and the gameplay effects of corrected random ranges need additional regressions and playtests. Passing these tests establishes only the contracts listed above.

Selection cases link production multi-selection and game-form input. Synthetic visible buildings count rectangle hit tests without meshes, packages or a device. They verify queued movement and final release through the real form, plus pending refresh, membership, masks, limits and cancellation in the selection implementation. They do not invoke the rendered frame/device path; native queue-load measurements and [Selection dragging](PLAYTESTING.md#selection-dragging) cover that integration separately. Debug rejects tracked allocation leaks and test crashes do not open native dialogs.

The [controlled standalone demo exporter](testing/85-native-demo-export.md) prepares six independent ordinary/reinforcement/no-reinforcement/win/loss/changing-fight scenes in Text and Packed configurations for NV-05-1. Its three `game.session.demo_export_*` cases validate generated maps/configuration/packages, destination ownership, failed-export cleanup and the actual Unicode CLI in EXE and DLL test builds. They launch only the console exporter, without initializing a game, creating a graphics device or recording a native gameplay pass. Running an exported standalone scene remains a separate operator action.

## Wire layouts and the host contract

`matrixgame_layout_tests` registers `base.layout.*`. Its fixtures are explicit little-endian byte sequences built field by field, following the specification in [Configuration and resource contracts](DATA_FORMATS.md), so they do not depend on native structure sizes or pointer width. Type identifiers, tags, versions and limits in the fixtures are literal documented values rather than production enums or macros, and static assertions require the production `EStorageType`, `EFileType` and package limits to keep those values. Production `CStorage`, `CPackFile`, `CPackCollection` and the `CFile` package fallback must parse them, and the storage writers must emit identical bytes. The cases cover all six column types, a nested configuration tree and its record naming, `ZL03` framing at the 65,000-byte boundary, per-column compression, nested package folders, free records, ignored stored runtime fields, 62-character names, compressed 64 KiB blocks, listings, searches, handle slots, collection order and loose-file override. zlib fixtures use stored blocks, so they do not depend on a compressor version. Negative cases load every truncated prefix of two uncompressed fixtures and bounded bad tags, versions, framing, oversized blocks and counts for whole bodies and compressed columns, and malformed package headers, record sizes and nested folders. A collection drops a malformed package alone, keeps reading the valid packages registered before and after it, with the later one taking the dropped index, names the dropped packages and fails only when none opens. They check the documented return value or `Base::CException`, released file handles and, in Debug, tracked leaks. Raw map records, including the bridge `CRect`, the vector-object records copied from `ST_BYTE` columns and the current x86 `SFileRec` have static size and offset checks with little-endian decoding. Inputs that the readers cannot yet reject safely are listed in the format guide and are not exercised.

Host-contract cases cover the x86 interface described in [Original host integration](ORIGINAL_HOST.md). `game.dll.host_layout` checks every host structure's size and offsets on x86 builds, freezes the result and support codes and decodes host-built byte images, `game.dll.entry_table` checks the static export table and borrowed callback registration, and `game.dll.borrowed_arguments` checks that `Run` forwards host pointers unchanged and seeds from the current time. `matrixgame_host_contract_tests` (`game.host.*`) links the engine: host settings bytes reach the configuration and sampler state, `CGame::SaveResult` writes exactly the six result fields, load progress reports strictly increasing values and sound callbacks receive exact identifiers, values, paths, group counters and call order. DLL configurations add `game.dll.native_entry_table` and `game.dll.native_language_configuration`, which load the real library to check the exact export, language-specific configuration selection, unchanged host strings, settings and results, untouched callbacks and released files. These cases use synthetic callbacks and a message-only window. A successful battle in the original host requires the native procedure and its separately recorded evidence.

## Add a regression

1. Define a bounded behavior and construct the smallest synthetic input that demonstrates it. Keep tests deterministic; use fixed seeds where random behavior matters.
2. Add a test function in the appropriate executable. Use `MG_CHECK(expression)` from `tests/test_support.hpp` for assertions and local objects for automatic cleanup. Assertions throw on failure and are evaluated in every build configuration.
3. Add the function to the executable's `tests::Case` array with a stable name such as `base.storage.round_trip`.
4. Register the same name in `tests/CMakeLists.txt`, for example `add_matrix_test(base.storage.round_trip matrixgame_base_tests base storage)`. Registration adds the required `engine` and `asset-free` labels and the timeout.
5. Run the case in Debug and Release, then run the affected suite. For a defect correction, verify that the regression fails before the fix and passes afterward.

An executable with another test group can reuse `tests::run` and `tests::Case`. Create it with `add_matrix_test_executable`, which links production `MatrixLib`, applies matching compile definitions and adds it to `matrixgame_tests`:

```cmake
add_matrix_test_executable(matrixgame_storage_tests storage_tests.cpp)
add_matrix_test(base.storage.round_trip matrixgame_storage_tests base storage)
```

CMake target names in `add_matrix_test` resolve to the correct executable for each compiler/configuration. Keep ordinary unit-test entry points headless; isolate native startup and window integration in dedicated cases with documented execution and exclusion requirements. Link additional production targets only when the test needs them.

Fixtures belong in the test sources or a clearly named directory under `tests/`. Use synthetic data and explicit byte layouts for format checks. Temporary output belongs in the build directory and should be isolated per case if tests run concurrently. Keep externally supplied game packages and configuration copies untracked.

## Repository checks

```powershell
.\tools\check-repository.ps1
python -B -m unittest discover -s .github/scripts -p 'test_*.py'
```

These checks validate repository hygiene and contribution-policy tooling. For game or build changes, also compile the affected Debug and Release standalone configurations using `tools/build.ps1` and perform relevant playtests.

## Playtesting

The `game.audio.default_catalog`, `game.audio.map_spawner_catalog`, `game.audio.mapping_override`, `game.audio.mapping_fallback` and `game.audio.startup_unavailable` contracts exercise standalone initialization with synthetic WAV files and a fake platform audio device. Default-catalog startup has no `CFG/sounds.txt`; the map-spawner case checks all 13 known map sound names, including `FormPlanetNone1`, `FormPlanetNone3` and `HangarOpen`. Partial overrides preserve unspecified defaults, and invalid files fall back without disabling audio. `tests/test_sound_catalog.py` checks catalog coverage against tracked battle configuration and the map-only aliases. A fresh release archive still needs the [listening checks](AUDIO.md#check-sound) with separately supplied packages and no private override.

Settings regressions are registered under `game.settings.*`. They cover `CFG/standalone.txt` parsing and per-key fallback, text-preserving atomic saves, the window model's selection, stepping, hit areas and labels, and the routing of startup and immediate options into engine settings and audio. `game.audio.master_volume` and `game.audio.master_level_retained` cover master volume; `game.audio.music_volume` and `game.audio.music_sessions` cover the music level. The rendered window and menu entry need the [Settings window](SETTINGS.md#verification) playtest.

Map selection regressions are registered under `game.maps.*`, with `game.settings.map_choice` for the remembered map and `base.layout.package_listing` for package folder listing. They cover the map list's sources, precedence, previews and path rules, and the Map window's selection, paging, hit areas, text and hint markup. `game.maps.external_folders` checks duplicate and preview priority across `maps/`, `DATA/maps/`, the legacy folder and packages; `game.maps.external_sources` discovers and loads synthetic storage files through those actual paths, including invalid overrides and nonrecursive scanning. `game.settings.save_creates_file` checks saved selections from both new folders. The rendered window, previews and restart need the [Map selection](MAPS.md#verification) playtest.

Display-mode regressions are registered under `game.display.*`, including resolution-change planning and the controller's size rollback through injected reset results, with integration checks for shortcut routing, pointer leave, deferred screenshots and canceled selection. `game.screen.*` covers edge anchoring and dialog centring at several sizes. Both run without a visible window. Use [Display settings and verification](DISPLAY.md) for the required native windowed, borderless and exclusive playtests; synthetic reset callbacks do not prove a driver can perform a fullscreen transition.

Use [Standalone playtesting](PLAYTESTING.md) for packed/text configuration cases, resource failures, the map-argument path and a reproducible validation record.

Launch the standalone EXE without arguments, using locally supplied resources.
Check the affected behavior in Debug and Release with cheats disabled first.

- Start a battle and check map rendering and UI text.
- Select robots, move, attack, and construct robots and turrets.
- Detonate bombs through both the UI and manual control.
- Check sound, pause/resume, focus changes and supported window transitions.
- Finish or exit the battle, then repeat launch and exit several times.
- Test relevant developer commands separately with cheats enabled.

Record the source revision, build configuration, map or seed where available,
steps performed and results. For a failure, include the relevant part of
`test.log` and a debugger stack. Keep resource packages and extracted assets
out of reports.

## Coverage and performance measurements

The [navigation work contracts](testing/81-navigation-work.md) verify scoped operation counts, exception-safe nesting and complete-trace preservation with collection enabled or inactive. Their basic journey is preservation evidence; crowded-scene reporting remains separate.

`tools/test.ps1 -Coverage` collects gcov line, function and branch coverage of the production areas with the MinGW toolchain; CI uploads the report without thresholds. `matrixgame_benchmarks` times representative navigation, selection and side-logic workloads through production code. CTest runs only its `game.benchmark.smoke` pass, which checks repeatable work without a timing limit. [Coverage and performance measurements](MEASUREMENTS.md) describes collection, the report denominator and exclusions, running and comparing benchmarks, and the manual native frame measurement.
