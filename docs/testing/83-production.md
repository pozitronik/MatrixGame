# Construction and production contracts

The `game.production.*` contracts characterize production functions from reference source `a64e9db4595bf06cc456772cb02c70f6a34b6804`. They contribute to ECO-01, ECO-02 and ECO-03 in the [behavior catalog](../research/75-behavior-baseline/README.md); they do not complete those families or the whole-game baseline. No production implementation is replaced or changed.

## Run

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game.production\.'
.\tools\test.ps1 -Configuration Release -Filter '^game.production\.'
```

The tests link `MatrixGameInternal` and use the existing `CommandWorld` allocation/map fixture. Buildings, queued robots, cannons, constructor definitions and interface objects are production objects. Map allocation uses the engine allocator, matching startup: nested legacy objects rely on its zeroed storage before their constructors run. Configuration, prices, timings and module definitions are synthetic; no external packages, graphics device or audio backend are needed. Checks remain active in Release and the executable checks for tracked allocation leaks where instrumentation is enabled. Each CTest case starts a fresh process.

The queue executable contains focused subsystem contracts, not complete map-tick traces. The additional production scenario executable uses the existing battle session and non-rendering device; its scope is documented in [Synthetic building fixtures](building-fixtures.md). In particular, the income tests call production `CMatrixBuilding::LogicTakt` with explicit elapsed intervals while map time stays at zero, avoiding unrelated capture scans. They establish payout arithmetic and boundary behavior, not capture timing or whole-battle scheduling. The reinforcement rejection and scaling cases run both Park-Miller and LegacyCRT with seed 83 and asserts zero raw draws. The other cases make no stochastic gameplay decisions and do not claim RNG-sensitive coverage. Native rendering, textures and audible output do not participate; the deployment scenarios use synthetic CPU geometry.

## Assertions and catalog ownership

| Case suffix | Production path and observable | Catalog contribution |
| --- | --- | --- |
| `queue_order` | `CBuildStack::AddItem`, both `DeleteItem` overloads and `ClearStack`: ordered links, middle/head cancellation, invalid indices, exact four-resource refunds, empty/idempotent clear | ECO-01 queue and cancellation |
| `capacity` | Queue accepts exactly `MAX_STACK_UNITS`; null/full insertion does not change balances or consume the rejected object's ownership; clearing refunds only accepted entries | ECO-01 capacity and rejection |
| `neutralization` | Neutralizing or deleting a base at 99/100 ms cancels both queued robots, refunds each once and leaves repeat neutralization/destruction without another refund | ECO-01 queue lifetime component |
| `queue_deployment` | Actual RobotSpawn at 99/100 ms, ordered handoff, build statistics, RNG consumption and blocked second spawn while the base opens | ECO-01 deployment handoff |
| `base_exit` | Complete map ticks open the base, release the robot, restore manual control and close the base without changing resources | ECO-01 actual exit, with preassembled chassis fixture |
| `maintenance_clock` | Ordinary map ticks at 10/16 ms freeze cooldown while paused and decrement/clamp it after resume, including exact-zero and overshoot boundaries in both RNG modes | ECO-03 cooldown scheduling |
| `scheduled_income` | Full map ticks grant each factory/base payout at 100 ms; neutralization stops subsequent income, in both RNG modes | ECO-02 map-scheduled income and ownership |
| `building_geometry` | All six real building kinds have synthetic CPU bounds/picking and correct base-cell teardown | Shared building fixture validation |
| `refund_owner` | Refund follows the queued robot's side after the base owner changes; each balance saturates at 9000 independently | ECO-01/ECO-02 ownership and refund cap |
| `price` | Constructor's production price calculation for empty, chassis-only and headed designs; affordability at exact funds and shortage in each resource separately | ECO-01 price and affordability predicate |
| `ai_constructor_prices` | AI definition loader, constructor price calculation and cancellation refund agree on explicit prices for headed/headless synthetic designs | ECO-01 shared pricing contract, not UI/AI dispatch |
| `weapon_slots` | All ten weapon kinds across all six armor-index entries: first free ordinary slot, exhausted ordinary slots, extra-slot replacement, absent extra slots and empty capacities; selection has no resource or queue effect | ECO-01 slot-selection predicate using synthetic capacities |
| `income` | Income reporting counts only owned bases and matching factories; ownership changes affect the report; querying does not grant resources | ECO-02 ownership and income reporting |
| `income_ticks` | All four factory payouts at 99/100 ms; large elapsed interval grants one payout, discarding surplus time; neutral ownership grants nothing; base multiplier truncation and resource cap | ECO-02 actual payout boundaries |
| `turret_progress` | Two real queued cannons: only the head progresses; canceling it refunds resources and resets the next timer; 99/100 ms separates construction from idle state; completion removes queue ownership without a second debit/refund | ECO-02 turret queue progress and completion |
| `reinforcement_disabled` | Zero map percentage rejects requests at negative, zero and positive cooldowns without changing the pending queue, balances, map logic list or RNG state; initialization/reset keep the map disabled | ECO-03 map-disabled request gate |
| `reinforcement_scaling` | Explicit difficulty/map-percentage/configured-time products, positive and one-millisecond rejection boundaries, reset and reinitialization; zero effective delay is ready rather than map-disabled | ECO-03 difficulty and cooldown initialization |
| `reinforcement_gates` | Positive cooldown and neutral ownership reject reinforcement before any random draw, balance or queue change; configured difficulty scales initial cooldown | ECO-03 request rejection and cooldown initialization |

The queue fixture deliberately does not pretend that `AddItem` debits a price: production callers own debiting. Tests start from declared balances and check the refund operation itself. Similarly, the AI/constructor comparison constructs matching module definitions through public production APIs; it does not execute a rendered construction panel or `BuildRobot` strategy.

## Remaining baseline work

ECO-01 still needs configured capacity/attachment rules across all component families, UI and strategic AI dispatch, exact debit/reservation behavior under insufficient funds, multiple requested units versus queue capacity, constructor-assembled robot completion and damage-driven base destruction during production. The current chassis-only battle harness cannot run `CConstructor::StackRobot` honestly: that route calls `RNeed(MR_Graph)`, loads assembled module geometry and, for player robots, creates textures. A reusable synthetic assembled-model catalog and the corresponding rendering-resource fixture are needed; suppressing those production calls would weaken the contract.

ECO-02 still needs real click-time turret placement and rejection, ownership/destruction during active construction through the game routes, and income alongside actual ownership capture rather than explicit neutralization. The turret queue test starts with already-created production cannons and does not assert placement eligibility, autonomous turret combat or native progress-bar rendering.

ECO-03 still needs successful help/button requests, occupied landing areas, type selection under the score budget, successful-request difficulty effects and cheat input timing variants, carrier creation/cap effects and delivery coordination with NAV-03. Rejection and cooldown initialization alone do not establish successful reinforcement. All these gaps remain baseline blockers; this suite does not justify closing the construction leaf or starting structural refactoring.

## Observed behavior to preserve

Three details deserve explicit expectations: queue insertion itself does not charge resources; a refund follows the queued unit's owner rather than the base's current owner; and a factory crossing several income periods in one call grants one payout and resets its period. These observations are source-backed and asserted here. They are not proposals to change gameplay, and no crash, corruption or undefined behavior is accepted as a golden result.

## Reinforcement gate fixture

The disabled-map test supplies the same maintenance percentage field that map loading populates. A test-only derived declaration forms a protected member pointer and applies it to the actual `CMatrixMapLogic` object; it neither downcasts that object nor replaces a production function. Zero percentage rejects requests even when the cooldown is already zero or negative. A queued robot is retained unchanged throughout the rejected requests.

The scaling examples use exact integer float32 products: configured delay 1000 with map percentage/difficulty pairs 25/0.5, 50/1, 100/1.5 and 175/2 produces 125, 500, 1500 and 3500 ms. Zero difficulty or zero configured delay produces zero cooldown while leaving reinforcement enabled. Positive cooldowns, including one remaining millisecond, reject a request; resetting or reinitializing the timer consumes no RNG and changes no resources or queue ownership. These cases do not call a successful zero-cooldown request and do not establish landing, carrier creation, resource-backed robot assembly or delivery.

Timer clearing exercises `ResetMaintenanceTime` only where `_DEBUG` or `_TRACE` exposes that helper. Release uses the ordinary `SetMaintenanceTime(0)` setter. This does not establish that a reset shortcut is available in a Release game or validate its keyboard dispatch.

The reinforcement gate extension passes all 18 production contracts in MinGW Debug and Release, and repository checks pass. Queue fixture ownership transfers immediately after successful insertion, before assertions, so a failing assertion cannot leave both the queue and a local smart pointer owning the same robot. No production source changed; MSVC validation is left to CI.
