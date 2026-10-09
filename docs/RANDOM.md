# Battle random generation

Standalone battles select a random mode from `CFG/standalone.txt` before packages, game objects and the map are initialized. The file is separate from packed `robots.dat`. The mode stays fixed until another battle initialization; editing the file while a battle is running does not switch or reseed that battle.

## Select a mode

Edit [MatrixGame/CFG/standalone.txt](../MatrixGame/CFG/standalone.txt), then build the desired configuration with the supported helper:

```text
RandomGenerator=LegacyCRT
```

Use `ParkMiller` to retain the default. Mode values are case-insensitive and allow surrounding whitespace; the parameter name is `RandomGenerator`. The helper copies this file into each executable's `CFG` directory. Rebuilding refreshes that output copy from the source configuration. For a temporary playtest, edit only the selected output copy after building and restore it afterward.

An absent file or parameter selects Park-Miller. Empty, unknown or duplicate values, and a file that cannot be read/parsed, select Park-Miller and produce an initialization diagnostic. The game logs `Battle random generator: ParkMiller` or `Battle random generator: LegacyCRT`; check this line to confirm the actual selection. The file uses the normal engine text format, including UTF-8 and UTF-16LE with a BOM.

DLL startup retains its existing Park-Miller default and does not read this standalone options file. Host callback/structure layouts and resource formats are unchanged.

## Sequence and range contracts

| Contract | ParkMiller | LegacyCRT |
| --- | --- | --- |
| Raw generator | Park-Miller recurrence, multiplier 16807 and modulus 2147483647, with output shifted down by one | `std::srand` / `std::rand`, as used by the fork's legacy implementation |
| Raw output | `0..2147483645` | `0..RAND_MAX`, 32767 on the supported Windows CRT |
| Seed | Reduced modulo 2147483647; a resulting zero selects state one | Passed as the full unsigned 32-bit CRT seed; zero is a distinct seed |
| Unit floating result | Raw output divided by 2147483645 | Raw output divided by the actual `RAND_MAX` |
| Ordinary integer range | Inclusive modulo mapping | Inclusive modulo mapping |
| `IRND(n)` for positive `n` | Modulo mapping to `0..n-1` | Rounded interpolation to `0..n-1`, retaining the old endpoint weighting |

For seed one, the first Park-Miller raw outputs are `16806, 282475248, 1622650072`; the supported CRT outputs are `41, 18467, 6334`. Reseeding through `random::seed(value)` preserves the active mode, including developer code that reseeds an existing battle. A new initialization explicitly selects the mode and seed.

Microsoft documents its [CRT raw range](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/rand?view=msvc-170). The legacy mode uses that CRT's state rather than an independent emulation; other `rand`/`srand` calls on the game thread can affect it. New simulation/effect callers should use the engine helpers. The default Park-Miller raw sequence is independent of CRT selection.

Both modes normalize reversed integer and finite floating bounds. Floating bounds include both endpoints, use a double-precision unit value and avoid overflowing the interpolation of finite endpoints. `FRND` and `FSRND` accept fractional scales; scales should be finite and representable as float. These corrections remain active in legacy mode.

Integer ranges use unsigned 64-bit intermediates. A range no wider than one raw output's capacity consumes one draw; wider ranges combine base-capacity digits until enough capacity is available. Park-Miller consumes two draws for a full 32-bit signed range. The Windows CRT consumes two draws above 32768 values and three above 1073741824 values, including the full signed range. This preserves bounds without the old signed-width overflow, but wide-range values are not promised to reproduce the erroneous implementation.

Every floating helper call, equal-bound call and `IRND` call consumes one raw draw. `IRND(0)` and negative counts return zero. Rounded legacy index mapping gives the endpoints roughly half the weight of interior indices: enumerating all 32768 raw CRT values for `IRND(4)` yields counts `5462, 10922, 10922, 5462`. This is an intentional compatibility choice, not a uniform-index guarantee. Both ordinary modulo mappings also retain modulo bias.

Map methods, simulation and effects consume the same selected stream. Changed effect-call order, wide-range draw counts or a different mode can therefore change later simulation decisions. The option does not promise original-game battle replay, identical Debug/Release floating intermediates or identical battle results across compilers. Corrected legacy behavior preserves the CRT sequence and rounded index weighting while repairing collapsed floating ranges, fractional truncation, invalid indices and integer overflow.

## Verification

Run `tools/test.ps1 -Configuration Debug` and repeat with `Release`. Named `base.random.*` cases freeze raw sequences, seeds, range endpoints and draw counts; rounded weighting is checked by exhaustive raw-value mapping rather than random sampling. `game.random.*` cases exercise the production options loader, defaults/invalid data, mode stability, UTF-8/UTF-16 files and actual map wrappers, including the constructor's initial draw. Fixtures contain synthetic configuration only and create no graphics device.

Sequence, range, weighting and draw-consumption correctness are established by deterministic tests, rather than by judging visible random outcomes. Confirm actual standalone selection through the startup log in Debug and Release, including the diagnostic/default for invalid options.

Live movement, avoidance and reinforcement-carrier behavior remain general standalone integration checks. [Standalone playtesting](PLAYTESTING.md#random-generator-modes) includes an optional two-mode smoke procedure for investigating a suspected regression, keeping the supplied packages unchanged. A successful synthetic map-wrapper test does not establish visible flight or navigation behavior, and a successful playtest does not prove a generator's sequence or distribution.
