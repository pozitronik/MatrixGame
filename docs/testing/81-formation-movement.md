# Intermediate formation movement and a moving trailing reference

`game.navigation.formation_movement.intermediate.<profile>` and `formation_movement.trailing_reference.<profile>` add physical N5 journeys through the existing `Session::advance()` map boundary. The profiles are `park_miller10`, `park_miller16`, `legacy10` and `legacy16`, all with seed 81. The measured unarmed journeys consume zero RNG draws after command setup; exercising both profiles does not establish an RNG-dependent outcome. They retain the original [mixed wheel/track slowdown, replacement and recovery journey](81-navigation.md#mixed-speed-regulation-and-formation-recovery) unchanged. No production seam, direct `CalcMaxSpeed` call or replacement movement algorithm is added.

## Fixture and command boundary

The Open synthetic map supplies flat terrain, movement topology and CPU-only wheel meshes. The two selected wheels are positioned once during setup; one or two more wheels are created and added to that selection. All have their existing maximum speed 0.6. Matrix, spatial membership and cached geometric bounds are refreshed before commanding. All members start at Y=260, facing positive X. The three-member X positions in member order are `[80,350,500]`; the four-member positions are `[80,140,350,500]`. The first is the rear member, the last is the leader, and the next-to-last is the intermediate member. In the four-member scene the member at 140 is the front of the rear cluster.

A single ordinary context Move targets world (600,260), group cell (58,24). Production assigns member destinations; the asserted movement-cell vectors are `[(58,24),(54,20),(54,28)]` and `[(58,24),(54,20),(54,28),(58,20)]`. These correspond to world (600,260), (560,220), (560,300), and, for the fourth member, (600,220). After this command the fixture only advances the session and observes state. It does not teleport robots, replace orders, supply runtime velocities/allowances, modify clocks or directly invoke side regulation.

## A reference oracle during live movement

After each advance, the fixture reads actual member positions and low-level Move destinations. It computes the direction from the mean position toward the mean destination, using the production movement-cell-to-world convention. Projecting the named members onto this direction permits a bounded geometry window without duplicating the production sort or reference-selection scan.

The three-member window requires the intermediate member more than 190 units ahead of the rear reference, the leader more than 130 ahead of the intermediate member, and total spread above 330. The four-member window also requires the named rear-cluster gap strictly between 40 and 100. Thus the first gap is below the production threshold 126 and both following gaps are above it. The designated reference is the member before the first separated gap, at the front of that rear cluster; it differs from both the absolute rear member and the member before the later gap. All members must retain Move orders, no Return orders and no known enemies, and the mean destination must be at least 100 units away.

Within this window, the source-derived expected intermediate allowance is `0.6 * (0.6 + 0.4 * front_gap / (spread_from_designated_reference - 180))`. The fixture requires this to be strictly between 0.37 and 0.59, the leader allowance to be 0.36 and the reference allowance to remain 0.6. Allowance comparisons use 0.000001 for arithmetic rounding. In the four-member window, the expected intermediate allowance must differ by more than 0.01 from the absolute-rear prediction and from the later-gap result 0.6. This establishes a distinguishable reference choice during ordinary ticks, rather than inferring it from arrival alone. The component boundary cases proposed separately in [PR #310](https://github.com/pozitronik/MatrixGame/pull/310) remain a separate scope; no unmerged helper is imported.

## Physical consumption and continuation

Objects move before side logic recomputes group allowances. `LOGIC_TAKT_PERIOD` is 10 ms: a 10 ms map advance runs one object portion, while a 16 ms advance runs 10 and 6 ms object portions before the side updates. Therefore the previous advance's predicted allowance applies to the next movement observation. The fixture saves both that independent prediction and the actual allowance before advancing. For a progressing intermediate member whose final scalar speed matches the preceding actual allowance, the complete advance's displacement must equal the independently predicted allowance times elapsed map milliseconds divided by 10, within 0.001 world units. The check measures displacement across the whole advance, including both object portions for 16 ms; it does not substitute the final scalar speed for travelled distance.

Each case requires at least ten reference-window observations and ten physical-consumption observations from a preceding verified window. Turning, collision and endpoint phases do not count as physical samples unless the stated scalar-speed condition holds; any counted sample must satisfy the independent distance assertion. All frames retain live identities, member count, group Move intent, original individual destinations, maximum speeds, finite bounded state and pairwise centre separation of at least 36 units (with 0.001 for arithmetic rounding).

All members must arrive at the distinct declared positions within 50,000 simulated milliseconds, followed by twenty stationary advances and empty order pools. The existing Session limit is 6,000 advances and twenty wall-clock seconds, with the normal CTest timeout. No arrival time or exact cross-compiler trajectory is frozen. The measured Debug MinGW exploration produced the following diagnostic counts, identical in the two RNG modes; assertions use the stated observation requirements rather than these exact counts.

| Scene / step | Reference observations | Physical observations | Arrival (ms) |
| --- | ---: | ---: | ---: |
| Intermediate / 10 ms | 98 | 98 | 10100 |
| Intermediate / 16 ms | 61 | 61 | 10112 |
| Rear cluster / 10 ms | 53 | 49 | 10830 |
| Rear cluster / 16 ms | 33 | 31 | 10816 |

## Reproduce and sensitivity

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.(formation_movement\.|mixed_formation_regulation$|formation_speed_boundaries$)'
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.(formation_movement\.|mixed_formation_regulation$|formation_speed_boundaries$)'
```

A physical-consumption control should make `Seek` derive velocity from maximum chassis speed while retaining its correct scalar speed and allowance; the displacement check must fail. A reference control should choose the absolute rear projection rather than the front of the first separated cluster. Temporarily masking only the new intermediate allowance-output comparison must still leave a failure at the next physical displacement check, proving that output assertions are not the sole protection. A later-gap control should likewise fail the live reference observation. Each control is temporary: restore source byte-for-byte, rebuild and rerun the positive cases. No control is a game option or a committed production change.

These asset-free scenes use the storage device without drawing, unavailable sound and no native window. They establish the declared physical regulation/reference journeys, not arbitrary directions, every chassis, native animation or accepted historical trajectory/performance references. The separate [scoped counter prerequisite](81-navigation-work.md) is available; crowded consumer/reporting work in [PR #311](https://github.com/pozitronik/MatrixGame/pull/311) is not credited as integrated by this branch. N5, #81, native evidence and complete-baseline acceptance retain their integration and maintainer gates.
