# Intermediate formation allowances and trailing references

`game.navigation.formation_intermediate_allowance` and `formation_trailing_reference` exercise the existing public `CMatrixSideUnit::CalcMaxSpeed` implementation with bounded, synthetic formations. They add N5 component evidence for an intermediate member and the reference chosen from separated clusters. The [mixed-speed journey](81-navigation.md#mixed-speed-regulation-and-formation-recovery) separately proves physical slowdown and recovery with a tracked member, both RNG modes and 10/16 ms frames. These direct cases do not replace that journey or accept the complete behavior baseline.

## Declared inputs and observation boundary

Each case creates an Open-layout Session, retains the two selected wheel robots and adds one or three synthetic wheel robots. A context Move establishes the common player logic group. Setup then supplies exact positions, positive per-unit maximum speeds and matching low-level Move destinations. It refreshes map membership and bounds, requires no Return order or known enemy, and checks the actual group membership and intent before the observed call. No production seam, friend accessor or replacement algorithm is added.

All layouts run along positive and negative X and Y. The input values below are distances along that movement axis, in logic-list member order; reversed layouts map each value v to 640 - v. Destinations are world 620 or 20 on the active axis and 200 on the other axis, corresponding to the declared movement-cell corners. The mean destination is far enough from the mean position to enter production regulation. The trailing layouts distinguish sorted projections from enumeration order; the three-member intermediate layout alone does not.

The fixture calls `CalcMaxSpeed` twice without advancing a map frame. Each call must leave positions, maximum speeds, Move coordinates and group intent unchanged, return finite expected allowances, consume no RNG draws and leave frame/time unchanged. The second call also requires resetting allowances to each member's maximum before scaling rather than compounding the previous reduction. Actual and expected allowance vectors are printed before checking them, with a tolerance of 0.000001 for arithmetic rounding.

These are direct component boundaries, not timed journeys. The fixed profile metadata uses ParkMiller seed 81 and a nominal 10 ms step, but no step is executed and the observed function does not read a random generator or time. Repeating those identical inputs under both RNG/timestep labels would add no distinct boundary. The physical journey retains its separate two-mode, two-schedule evidence. The two new cases execute 24 fresh Sessions and 48 observed calls per build configuration, with no native window, rendering, audio or original resources.

## Intermediate member

Three members have axis positions `[520,100,400]`, maxima `[0.6,0.5,0.3]` and required allowances `[0.36,0.5,0.24]`. The first member is the leader, the second is the trailer and the third has the intermediate position. This three-member layout establishes the interpolated allowance; it does not independently establish sorting.

Source inspection explains the expected intermediate factor: the reference is at 100, the leader's relative projection is 420 and the intermediate projection is 300. At the 180-unit threshold, those declared values give an intermediate factor of 0.8, between the leader's 0.6 and the trailer's full allowance. The test asserts the literal output vector through production logic rather than implementing the selection/interpolation algorithm in the fixture. Replacing every reduced allowance with the leading endpoint's 0.6 factor must fail this case.

## First gap and trailing-cluster reference

Five members have positions `[520,100,400,40,100+gap]` and maxima `[0.6,0.5,0.3,0.4,0.2]`. Production sorts the projections and stops at the first neighboring gap strictly above 126 (`COLLIDE_BOT_R * 7`). The member before that gap supplies the reference. The table's reference column is derived from that source path; it is not exposed or directly read by the fixture. The tested observable is the changed allowance of the member at 400, while the leader remains 0.36 and the other three retain their maxima.

| Gap | Sorted trailing portion | Source-derived reference | Required allowance at 400 |
| ---: | --- | ---: | ---: |
| 125 | 40, 100, 225 | 225 | 0.3 |
| 126 | 40, 100, 226 | 226 | 0.3 |
| 127 | 40, 100, 227 | 100 | 0.24 |
| 140 | 40, 100, 240 | 100 | 0.24 |

The 126/127 pair distinguishes strict from inclusive gap comparison. The rear-most member at 40 is never the intended reference in these layouts, so using the absolute minimum instead of the trailing cluster's front also changes a required output. The 140 layout contains another large gap farther ahead, which does not replace the first gap's reference.

A connected-cluster control uses positions `[520,160,400,40,280]`. Its sorted gaps are all 120; even with total spread 480, production selects the front member as reference and keeps every allowance at its maximum. This records the current reference behavior without inventing a rule that total spread alone must cause slowdown.

## Reproduce and limits

```powershell
$cases = '^game\.navigation\.(mixed_formation_regulation|formation_(intermediate_allowance|trailing_reference|speed_boundaries))$'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
```

Sensitivity controls should separately replace the interpolation with the 0.6 endpoint, select `pr[0]` instead of the first gap's reference, and make the gap comparison inclusive. The first must fail the intermediate case; the other two must fail the trailing-reference case. Restore production source byte-for-byte, rebuild the actual game and affected targets, and rerun all four positives after each control. These controls are temporary validation, not game options or committed production changes.

The new component checks establish allowances and preserved command/state inputs. The separate [physical formation journeys](81-formation-movement.md) assert actual intermediate displacement and a distinguishable trailing-cluster reference during live ticks; these direct calls do not establish that movement, collision avoidance, native animation or complete formation behavior. Return orders, active-enemy exclusions, arbitrary directions and wider group sizes retain their existing coverage boundaries. The [crowded/opposing-flow consumers and reports](81-navigation-work.md#crowded-scene-consumers-and-reports) supply separate scoped operation evidence. Native execution, accepted reference manifests, final representative measurements and complete-baseline acceptance remain separate requirements. No gameplay, resource format, host ABI or production ownership change is made.
