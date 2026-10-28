# Static combat inputs and assertions

Thirteen direct combat contracts have a finite set of source inputs and active assertions. The [machine-readable catalog](static-combat-inputs.json) records that selection at source revision `9a85aa9a98414913e0d2ffd9d7d24b0dc96fa362`, with source locations, body hashes, literal vectors, ordered calls and the actual `MG_CHECK` expressions. The [combat guide](82-combat.md) describes the gameplay coverage; this catalog supplies the input and assertion identities needed to bind execution evidence to it.

## Reading the catalog

`cases` contains exactly the registered names below. Shared setup is defined once in `shared`: the seventeen weapon enum values and table indices, synthetic damage tuples, radius/cooldown values, hit position/direction, fixture ownership and health units. Loop dimensions and formulas retain source order without expanding their Cartesian products into additional scenarios.

| Case suffix under `game.combat.` | Finite input identity |
| --- | --- |
| `robot_families`, `building_families`, `cannon_families` | Seventeen ordered weapon entries per target class, fresh target health 50, synthetic damage tuple `{i + 1, 2, 10}`. |
| `difficulty` | Twelve literal victim/attacker/multiplier/expected-health rows, each applied to robot, building and cannon in that order. |
| `minimum_health` | Initial health 9, 10 and 11; two ordered gun hits per robot. |
| `repair` | Five ordered hits, including saturation and manually assigned dead-state rejection. |
| `status_effects` | Six ordered flame/electric hit batches, with the original repeat counts and protection/TTL assignments. |
| `bomb_protection` | Protection 0, 0.5 and 1 on fresh robots. |
| `building_death` | One lethal hit followed by repair and another hit, retaining linked-object and kill-count observations. |
| `cannon_repair_state` | Five ordered hits across invulnerability, ordinary repair and manually assigned dead state. |
| `building_repair` | One damaging hit followed by three repair hits. |
| `projectile_range_callback` | Five real weapon types and squared distances 99, 100 and 101, using direct `WeaponHit` callbacks. |
| `projectile_owner_retirement` | An identity-only owner is deleted before one direct impact callback. |

Every case visits ParkMiller and LegacyCRT in source order. Each mode constructs one `World`, whose first member selects seed 82 before the map fixture and damage tables are constructed. Inner weapon/example loops do not reseed. Only the two invulnerable cannon hits have an asserted before/after draw-count equality; there is no whole-case zero-draw result or assertion that the two RNG modes produce identical streams.

The observations preserve their source representation. The Robot test stub reads raw internal health, buildings expose health divided by ten, and cannons expose raw health. Exact scalar comparisons, source float expressions, literal binary32 checks, flags and symbolic pointer relations remain distinct. Assertion lists and `not_asserted_or_outside_scope` describe the actual endpoints: for example, the status case checks unchanged health after its first seventeen flame hits, and the owner-retirement case checks callback count/flags without separately asserting its callback target.

The catalog's source hashes identify canonical Git blob bytes. Body hashes cover UTF-8 source with LF line endings from the first signature character through the closing `}` or `};`, including original comments and internal whitespace, without a trailing LF. Source line ranges are inclusive. Assertion expressions collapse whitespace for readability; their complete bodies remain hashed. Each case also has a canonical JSON input/assertion hash, with its exact exclusions and serialization settings specified in `hash_conventions`. These are content identities, not a new execution or manifest protocol.

## Registration and execution identity

[CTest registration](../../tests/CMakeLists.txt) attaches these cases to `matrixgame_combat_damage_tests` when tests are enabled. Each ordinary CTest invocation runs one named case in a fresh process, with `engine;asset-free;gameplay` labels and a 30-second timeout. The executable links the production internal engine, MatrixLib, the diagnostic overlay stub and test support; MSVC also links dbghelp.

The [checks](../../tests/test_support.hpp) remain active in Release. The [main function](../../tests/combat_damage_tests.cpp) only returns `tests::run`; it does not perform the explicit `BaseInit`/`BaseDeInit` or final global memory-allocation scan found in some other test executables. Shared support configures CRT reporting in MSVC Debug. The cases emit neither Session metadata nor a source/input identity header.

The supported helper can select only these contracts:

```powershell
$cases = @(
    'robot_families', 'building_families', 'cannon_families', 'difficulty',
    'minimum_health', 'repair', 'status_effects', 'bomb_protection',
    'building_death', 'cannon_repair_state', 'building_repair',
    'projectile_range_callback', 'projectile_owner_retirement'
)
$filter = '^game\.combat\.(' + ($cases -join '|') + ')$'
.\tools\test.ps1 -Configuration Debug -Filter $filter
.\tools\test.ps1 -Configuration Release -Filter $filter
```

Those commands produce results for their actual configured test binaries. Evidence still needs the exact producer hash/PE role, source and finite input identities, effective compiler/cache/defines, dependency/runtime/FP profile, and per-case process exits, stdout and JUnit. A game executable hash does not identify this separately linked producer. Existing CI results must be bound explicitly; their existence or absence is not inferred from this source catalog.

## Boundary

These cases and their shared fixture/support do not dispatch map or component ticks. The directly entered robot/building/cannon `Damage` and weapon `WeaponHit` bodies also contain no tick dispatch. Ordered damage, TTL assignments, reference changes and actual owner deletion remain direct component inputs; the catalog does not turn them into a simulation history or claim zero RNG/effect work in nested helpers. Distant hit positions avoid native effect asset paths; there is no native graphics or audio result.

The selection contributes only its declared COM-01 damage/repair/resistance/range predicates and COM-02 immediate death/reference/ownership predicates. Direct damage to a cannon supplies no COM-03 autonomous acquisition, rotation, firing or retargeting evidence. Temporal armed scenarios, periodic status damage, complete lifecycle histories and native host/presentation requirements remain separate. The [behavior catalog](../research/75-behavior-baseline/README.md) defines the reference/comparison and complete-baseline gates; this input catalog grants no tolerance, comparison-tier substitution, native waiver or structural implementation permission.
