# Delivery approaches past physical obstacles

These production-backed cases supply the physical approach scene selected by N6 in the [baseline completion checklist](baseline-completion.md#navigation). Existing [delivery and neighboring-landing cases](81-navigation.md#delivery-flights-and-carrier-damage) cover configured flights, inherited orders, landing restrictions and carrier damage. The [weapon-attached transport cases](85-transport-collisions.md) cover multipart collision and combat outcomes. This fixture adds a physical building on the incoming flight path and a low-building control; it does not change gameplay.

## Run the cases

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.carrier_approach_obstacle\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.carrier_approach_obstacle\.'
```

Add `-DLL` for the x86 DLL compile-time profile. Four cases separate both RNG modes and 10/16 ms schedules. Each case runs a low building and a tall building, then repeats each in a fresh `Session`: sixteen complete journeys per configuration. Seed 81 and a 32-second bound apply. Assertions remain active in Release; instrumented builds also require tracked allocation balance. No native device or external resources are used. The shared runner records unavailable audio and icon targets, no drawing, compiler/build options and revision identity.

## World and intervention

The Open synthetic movement map contains the original four unarmed robots, placed at (100, 100), (100, 150), (100, 550) and (100, 600). Their configured speeds are zero and every tick verifies that they remain alive at those positions. Their normal AI continues to run. Initial selection is empty.

A player-owned titanium factory stands at (480, 330). Its single synthetic graph node loads a closed box mesh with footprint X 468–492, Y 318–342 and floor Z 0. The low control has roof Z 20; the candidate has roof Z 500. They otherwise use the same construction, ownership and registration. Runtime flat-cell terrain provides the floor; this does not claim terrain-mask compilation or native factory-model loading. Production `CalcBounds` must reproduce the declared box within 0.0001 world units, allowing a few float ULP from graph transforms. A vertical production map trace must identify the factory and hit its roof within 0.001 world units; the longer ray has a larger rounding allowance.

Ordinary `OrderFlyer(FO_GIVE_BOT)` requests delivery at (480, 480), using place 3, approach angle 0, distance 300 plus a +/-150 random offset, height 100 and landing height 40 from the shared synthetic transport configuration. Production `ApplyOrder` constructs the body-only carrier, wheel cargo, attack intent and trajectory. The test never changes a trajectory point, carrier position, cargo position or collision result after the request.

## Observed behavior

The carrier reacts to the tall building's interpolated height field, but still crosses its physical volume while carrying the robot. The candidate and control have bit-identical horizontal flight samples and the same raw RNG draw counts at every tick. At least one matching position over the building must be more than one world unit higher in the candidate; its altitude must never be lower. The candidate's maximum sampled object-height field must exceed the control's by more than 200 world units. These are causal comparison assertions, not a duplicated height-interpolation implementation.

For at least ten sampled frames spanning at least 200 ms, the tall candidate's centre lies strictly inside the building's physical bounds before release. Every such sample requires the cargo to remain attached and a production vertical trace at that exact XY to hit the building roof above the carrier. The low control must have no enclosed samples. This establishes a geometry penetration; it does not establish a visible rendering defect from a screenshot or pretend that a collision-free route was found.

The recorded initial observations are:

| RNG | Step | Tall-building enclosed interval | Enclosed samples | Release / landing / carrier retirement |
| --- | ---: | --- | ---: | --- |
| ParkMiller | 10 ms | 480–1170 ms | 70 | 3420 / 3740 / 6690 ms |
| ParkMiller | 16 ms | 480–1168 ms | 44 | 3424 / 3744 / 6704 ms |
| LegacyCRT | 10 ms | 2770–3130 ms | 37 | 5410 / 5730 / 10600 ms |
| LegacyCRT | 16 ms | 2768–3136 ms | 24 | 5408 / 5728 / 10608 ms |

The table makes the selected behavior inspectable; these measurements are not accepted historical reference traces or exact timestamp assertions in this fixture. Existing delivery cases separately pin their release/landing/retirement schedules. Here low and high buildings must have equal event times, and same-profile replay must reproduce every recorded observation exactly.

Both journeys must retain their inherited Attack destination, preserve live cargo and finite transforms, release it, land it, and retire the carrier naturally before 30 seconds. The final two seconds require cargo at exactly (500, 500, 0), in place with an empty low-level order queue. The active carrier list must be empty and its retained core retired. Both sides' robot-kill counters remain zero, and exactly one unavailable icon target is reported.

Snapshots include the ordinary battle state and raw RNG draw count, carrier presence and raw position bits, and cargo state and raw position/height bits. Retained cores are checked before pointers are used. The complete trace, horizontal samples, altitude samples and measured lifecycle/penetration observations repeat within each profile. Comparing the two building heights independently requires unchanged horizontal samples, RNG counts and lifecycle times, but a different full trace.

## Why the obstacle does not stop the flight

`ApplyOrder` derives a trajectory from the landing point and configured approach geometry. Each `ProceedTrajectory` advances that curve and assigns its next point. `LogicTakt` subsequently blends altitude toward `CalcFlyerZInPoint`, which samples interpolated object/robot height. This correction is active for the delivery: it is not disabled merely because a trajectory exists. The interpolated group-maximum field under-represents a narrow building: on these paths the clearance target stays below the roof over the footprint, so even immediate tracking would cross it. The field interpolates maxima over 200-unit map groups while the building is only 24 units wide. The next tick starts from another curve point, so the weak altitude correction further limits the response and does not by itself construct a collision-free approach.

`CalcCollisionDisplace` has no live caller on this path and searches for other flyers rather than building surfaces. The test characterizes the resulting enclosed samples and completed delivery. A safe obstacle-clearance or rejection rule needs a separate contract and correction, including the attached robot, reservation ownership, release timing and possible shared-RNG consequences. Freezing the current observation does not endorse penetration as a desired future feature.

## Sensitivity and limits

A temporary source control replaces only the object-height sampling in `CalcFlyerZInPoint` with terrain-only interpolation. The new height-response assertion must fail although the building still has real physical bounds and remains traceable. Restore exact source bytes, rebuild and require all four cases to pass again.

This is a no-draw, body-only, fixed-obstacle scene. It does not cover dynamic obstacle insertion, native models/icons, visible carrier clearance, general combat-flyer behavior or original-host execution. Other N6 phases retain their linked evidence; complete-baseline reference, performance and exact-revision acceptance remain separate requirements. The discovered penetration needs explicit defect disposition before a later correction replaces its characterization.
