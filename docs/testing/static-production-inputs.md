# Static production inputs and assertions

Six direct component contracts define a finite part of the construction baseline: queue ordering and refunds, capacity rejection, refund ownership, construction prices, affordability and weapon-slot selection. Their [input/assertion catalog](static-production-inputs.json) records the original inputs, ordered calls, active scalar expectations, negative inputs and limits of the assertions. It contains no execution results and is not a runtime manifest or reference selection. [Production contracts](83-production.md) describes the wider construction coverage and temporal journeys.

The declared source revision is [`9a85aa9a98414913e0d2ffd9d7d24b0dc96fa362`](https://github.com/pozitronik/MatrixGame/tree/9a85aa9a98414913e0d2ffd9d7d24b0dc96fa362). The catalog retains canonical Git source blobs and SHA-256 hashes of their Git contents, along with the exact case registrations and source locations. This pin identifies the declarations; it does not designate a historical oracle. A final evidence packet must bind those declarations to the actual binary, build profile, inputs and process results.

## Finite selection

| Exact CTest case | Supplied calls and active observations |
| --- | --- |
| `game.production.queue_order` | Enqueue first/second/third; assert selected ordered links; cancel index 2 and reconnect first/third; reject indices 0 and 9 without changing count or balances; cancel first by pointer; clear third; repeat empty clear without another refund. Resource vectors progress `(100,200,300,400)` -> `(111,217,323,431)` -> `(122,234,346,462)` -> `(133,251,369,493)`. `DeleteItem(2)` returns remaining count 2, not the removed index. |
| `game.production.capacity` | Null insertion leaves zero count. Exactly six entries fill `MAX_STACK_UNITS`; rejected seventh insertion preserves six, leaves rejected links null and retains its local owner. Insertion does not debit balances. Clearing refunds six chassis prices, giving `(166,302,438,586)`, and clears the full predicate. This case does not separately assert zero count after that final clear. |
| `game.production.refund_owner` | Keep queued robot on side 1, change the base to side 2 and set player balances to `(8995,8980,9000,0)`. Clearing yields `(9000,8997,9000,31)`: refund follows the robot and each resource saturates independently at 9000. No separate enemy balance is asserted. |
| `game.production.price` | Empty constructor price is zero. Pneumatic chassis kind 1 costs `(11,17,23,31)`; blocker head kind 1 adds `(3,5,7,9)` for `(14,22,30,40)`. Exact funds pass affordability; each separate one-unit shortage rejects. The source constant is named `wheel_price`, but the inserted chassis is pneumatic kind 1 rather than wheel kind 2. |
| `game.production.ai_constructor_prices` | Load literal definitions `P,1,G,S` and `P,1,G` with explicit armor6/weapon1/head1 prices and synthetic MONOSTACK common capacity 1. Assert two loaded types; for each entry, constructor price, loaded AI resource vector and direct refund agree: `(184,200,220,244)` when a head exists, `(111,121,137,155)` otherwise. The assertions do not independently establish sort order, exact parsed module identities or headed/headless branch distribution. |
| `game.production.weapon_slots` | Retain all six synthetic armor entries and ten weapon kinds in their original loop order: 180 legality calls plus 12 empty-capacity calls. Ordinary weapons select free common slot 0, then 1, then reject. Mortar 5 and bomb 7 select or replace extra slot 2, then reject when its access mask is removed. Empty capacity rejects machine gun and bomb. Final balances are unchanged and the queue is empty. |

Resource vectors use titanium/electronics/energy/plasma order. The common fixture zeroes prices, supplies the chassis price above, initializes a closed player base and allocates production objects with the engine's zeroed-storage allocator. The [fixture](../../tests/command_world.hpp) supplies synthetic map/configuration data without packages, models, a window, a device or an audible backend. Local smart pointers and the queue retain their original ownership handoff. Pointer observations in the catalog are symbolic identities or link relations, never raw address goldens.

The machine catalog expands all 192 weapon-slot calls and retains the exact input slots, armor/weapon indices, access mask and expected result of each call. The two AI-definition iterations retain the source's conditional expectation instead of inventing an assertion about their ordering. A call row can group setup with its immediate observation; its source location identifies the active check. Fields marked `not_asserted` distinguish missing evidence from an intended effect inferred from implementation.

These cases assert integers, booleans and pointer relations; none asserts a floating-point bit pattern or an RNG seed, mode or draw interval. They do not establish a zero-draw or both-RNG result. Invalid indices, null/full insertion, individual shortages, occupied slots and incompatible capacities are existing negative inputs. Evidence of a deliberate perturbation detecting a wrong result, when required for acceptance, must be recorded separately rather than inferred from this source catalog.

## Registration and execution identity

[CTest registration](../../tests/CMakeLists.txt) attaches the six cases unconditionally to `matrixgame_production_queue_tests` when tests are enabled. Each invocation runs `<resolved executable> <exact case>` in a fresh process, with `engine;asset-free;gameplay` labels and a 30-second timeout. The executable links the internal production engine, MatrixLib and the diagnostic overlay stub; MSVC also links dbghelp.

The [test checks](../../tests/test_support.hpp) remain active in Release when legacy assertions are disabled. The [executable's main](../../tests/production_queue_tests.cpp) performs BaseInit, case execution, the conditional tracked-allocation check and BaseDeInit. A printed Passed line precedes the final allocation check, so it cannot replace the process result. These cases emit no Session metadata or JSONL identity header.

The catalog lists the build matrix declared at its source revision. Actual evidence must still identify compiler/configuration, `_DEBUG`, `ASSERT_OFF`, `_TRACE`, relevant source-specific `BUILD_EXE`/`CHEATS_ON`, forced memory instrumentation, coverage/music settings, architecture and imported runtimes. Memory tracking is enabled by `_DEBUG` or `FORCE_ENABLE_MEM_SPY`; do not infer all effective defines from a configuration name. A console test linked with DLL-role engine objects does not demonstrate original-host DLL loading.

Supported helpers can select just these contracts:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.production\.(queue_order|capacity|refund_owner|price|ai_constructor_prices|weapon_slots)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.production\.(queue_order|capacity|refund_owner|price|ai_constructor_prices|weapon_slots)$'
```

Those commands produce process results for their actual configured binaries. They do not automatically collect the complete source/binary/input manifest or select a reference. Keep the actual producer hash/PE role, compiler/dependency/runtime identities, effective cache/defines, input identity and per-case process/JUnit results together. The game executable hash does not identify this separately linked test executable, and a pass-only aggregate does not describe its asserted inputs and outcomes.

## Boundary

The direct deletion/refund call order is retained as finite component input. The separate `neutralization` case calls `TickTimer(99)` before ownership loss/deletion and needs its own disposition as an ordered component-timed family. `turret_progress` and `income_ticks` also advance component timers and are excluded. Income reporting, reinforcement predicates and full-map production journeys are outside this six-case selection.

Temporal construction, ownership and lifecycle journeys still require complete observations. Native visual/audio/focus and original-host requirements remain separate. The [behavior catalog](../research/75-behavior-baseline/README.md) defines the reference/comparison and complete-baseline gates; this input catalog introduces no tolerance, comparison-tier substitution, native waiver or permission for structural implementation.
