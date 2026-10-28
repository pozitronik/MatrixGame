# Static settings inputs and assertions

Twelve existing contracts define the discrete parser, text-transformation and scalar part of settings behavior. The [input/assertion catalog](static-settings-inputs.json) retains their literal strings, ordered calls, typed values, compact loops and actual assertions at source revision `9a85aa9a98414913e0d2ffd9d7d24b0dc96fa362`. The [presentation guide](87-presentation.md#settings-input-pause-and-persistence) describes the menu, pause and runtime continuations; this catalog identifies the finite inputs needed to bind the discrete checks to execution evidence.

## Finite selection

| Exact case | Input and assertion boundary |
| --- | --- |
| `game.settings.defaults` | Empty text gives an empty invalid-key vector and full default `Values`, with additional scalar default checks. |
| `game.settings.explicit_values` | The complete custom text gives full custom `Values`; later alias, path-spelling and empty-map calls check only their declared fields. |
| `game.settings.invalid_values` | Invalid ranges, unknown or duplicate values and map paths retain exact per-key fallback and diagnostic order; valid volume/camera endpoints are separate inputs. |
| `game.settings.preserves_text` | Two updates compare against a complete expected text. A separate Unix-newline call checks a prefix and absence of CR only. |
| `game.settings.round_trip` | Default and custom values roundtrip with empty diagnostics; an Exclusive variant checks exact values without a separate diagnostic-vector assertion. |
| `game.settings.round_trip_inline_blocks` | Five original/expected-prefix/expected-suffix rows cover nested statements, duplicate keys and top-level closing braces. The parsed update equals custom values; the second update is idempotent. |
| `game.display.defaults` | An unrelated random-generator key leaves the default dimensions and display modes. |
| `game.display.options` | Three ordered parses retain dimensions, case/space handling and mode/fullscreen precedence, with only the fields actually asserted at each call. |
| `game.display.invalid_options` | Eight invalid width strings and ordered height/mode/duplicate inputs check rejection and specified fallback fields. |
| `game.display.toggle_pairs` | Four direct mode-pair inputs produce the declared target mode. |
| `game.display.coordinates` | Four client extents cover endpoints, outside coordinates and midpoint bounds, followed by three degenerate/large scalar inputs. |
| `game.display.change_plans` | Nine direct planner calls cover mode/size requests and reset requirements, retaining the original three-mode loop and sparse field assertions. |

`shared` records full default/custom values, header enum domains, parser bounds and the two source helpers. `cases` records exact literal strings, ordered contracts and original assertion expressions. It keeps source loops compact without introducing extra cases or a Cartesian execution matrix. Source hashes identify canonical Git bytes; body hashes retain inclusive source ranges with UTF-8/LF and no trailing LF. The finite-case JSON hash excludes only its source-binding object and the self hash, using sorted keys, compact separators and UTF-8. These are content identities, not a new manifest or execution protocol.

The catalog preserves assertion strength. The standalone invalid-key vector contains twelve names and excludes `ResolutionWidth`, even though invalid display input causes fallback. Range failures reject or fall back rather than clamp. Coordinate midpoints satisfy only `>=383` and `<=384`; outside coordinates retain only their asserted bounds. The Borderless-to-Windowed planner sample checks target and `!reset`, not every size/resize field. Exclusive samples check reset only; the final combined request does not separately assert resize.

Text equality and containment also remain distinct. `preserves_text` has complete expected-text comparisons, followed by a prefix/no-CR check. The five inline rows check their original volume, updated prefix/suffix, parsed values and exact equality of a second update to the first. Those prefix/suffix checks do not establish complete equality between the original and updated text. The exact literal strings and assertions remain in the machine catalog rather than being replaced by a broader preservation claim.

## Registration and execution identity

The [registrations](../../tests/CMakeLists.txt) place six cases in `matrixgame_standalone_settings_tests` and six in `matrixgame_display_tests`, unconditionally when tests are enabled. Each ordinary CTest invocation runs one named case with a 30-second timeout. Both have engine/asset-free labels; settings uses startup/settings and display uses rendering/input. The first target compiles the settings/random/map parser sources with MatrixLib; it has no MatrixGameInternal or diagnostic overlay. The display target adds MatrixGameInternal, the diagnostic overlay stub and WINDOWS_IGNORE_PACKING_MISMATCH. MSVC also links dbghelp through shared test setup.

The [settings main](../../tests/standalone_settings_tests.cpp) only returns `tests::run`. The [display main](../../tests/display_tests.cpp) performs SetErrorMode, BaseInit, case dispatch and BaseDeInit. Neither has the explicit final global allocation scan used by some other test executables. Shared [checks](../../tests/test_support.hpp) stay active in Release, and MSVC Debug diagnostic setup remains separate from the case's scalar behavior.

Select only these contracts with the supported helper:

```powershell
$settings = 'defaults|explicit_values|invalid_values|preserves_text|round_trip|round_trip_inline_blocks'
$display = 'defaults|options|invalid_options|toggle_pairs|coordinates|change_plans'
$filter = '^game\.(settings\.(' + $settings + ')|display\.(' + $display + '))$'
.\tools\test.ps1 -Configuration Debug -Filter $filter
.\tools\test.ps1 -Configuration Release -Filter $filter
```

Results need the exact test-producer hash/PE role, declared source/input identity, actual compiler/cache/defines, dependencies, imported runtimes and per-case process exits, stdout and JUnit. A game executable hash does not identify either separately linked test producer. Existing CI or diagnostic results must be bound explicitly; this source catalog does not infer them or choose a reference. Meaningful failure evidence and same-profile comparison remain separate acceptance inputs.

## Boundary

The selected case paths parse or rewrite in-memory text and call scalar display resolvers, coordinate mapping or planning. Parsing the random-mode option does not initialize a battle stream, and validating a map path does not discover or load a map. No selected case dispatches file save/load, a window/device/controller operation, a restart or a timed wait. Test-process startup and diagnostic allocation are harness operations. No seed, draw-count or RNG-state equality is asserted here, so the catalog supplies no zero-draw guarantee.

All seven standalone save/load/lock cases and the mixed parse-and-save `battle_bonus` case are excluded. Display file loading, shortcut latches, reset-copy callbacks, transition order/failure and rollback remain outside this selection. Host byte images, resource predicates and CPU plane proofs have their own evidence. A parser fragment does not qualify an entire excluded lifecycle case as static.

This selection supplies only discrete PRES-04 parser/text/scalar/planner predicates. Menu navigation, Apply/Cancel, pause ownership, effective runtime settings, restart, actual display modes, GPU resources and native focus remain separate. The [behavior catalog](../research/75-behavior-baseline/README.md) retains the reference/comparison and complete-baseline gates. This catalog grants no tolerance, native waiver, comparison-tier substitution or structural implementation permission.
