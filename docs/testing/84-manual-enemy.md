# Move completion with a reserved destination

`game.ai.manual_exit_*` extends A2 / AI-02 and CMD-05 with the nearby-enemy continuation that the original manual-chassis cases deliberately excluded. It distinguishes the player's requested point, the navigation place actually assigned and physical arrival. This is characterization of production reservation rules, not an AI obedience correction.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.manual_exit_|^game\.player\.manual_(track|hovercraft)_lifecycle$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.manual_exit_|^game\.player\.manual_(track|hovercraft)_lifecycle$'
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game\.ai\.manual_exit_'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^game\.ai\.manual_exit_'
```

## Inputs and shared manual phase

The [existing track and hovercraft lifecycle fixtures](player-orders.md#armed-manual-control-on-track-and-hovercraft) supply generated meshes, real laser assembly, form-dispatched manual movement/turning/fire/input reset and an Enter exit with Forward still held. The shared profile helper retains the original lifecycle cases and their enemy-removed arrival checks. New cases select one chassis, stationary/moving target, RNG mode and timestep per CTest process so that each bound remains independent.

The scene uses seed 80, ParkMiller/LegacyCRT, 10/16 ms map steps, Open geometry, unavailable audio and the no-draw surrogate device. Track speed is 0.5 and hovercraft speed 0.7 before the manual multiplier. The player robot starts at (100,200), its unarmed target at (300,250) with 1,000 internal health units, and a stationary surviving opponent at (540,540). The target's ordinary computer logic runs with zero speed or 0.15; its moving trajectory is not supplied. The new fixture explicitly refreshes its relocated mesh bounds and checks its initial geometric centre. Laser radius/cooldown/damage settings and the complete manual schedule remain those documented in the existing lifecycle guide.

The manual phase moves, turns, fires, releases, fires again, resets input and exits through Enter while Forward is held. It must damage the target, stop damage on release/reset, restore normal movement speed and establish the existing targetless Attack intention. The exit is at 2,100 ms in the 10 ms profile and 2,176 ms in the 16 ms profile; interval advancement rounds upward to whole ticks. Both continuations observe the same complete prefix through that exit.

## Requested point versus assigned place

At exit, the nearby opponent holds road place 0 at footprint-corner `(8,8)`, centred at (100,100). A normal context Move requests that exact point. `PGAssignPlacePlayer` collects other robots' planned places, including hostile robots, and passes those reservations to `PlaceFindNear`; `PGSetPlace` records the resulting ordinary or additional place. It is not limited to physical occupants of the destination.

| Continuation | Observed result after 13 further simulated seconds |
| --- | --- |
| Remove the nearby opponent before Move | The command can take road place 0. The robot reaches (100,100), reports in-place and has no fire order. |
| Keep the nearby opponent alive | The group still requests `(8,8)`, but the individual assignment is additional place `(4,4)`, centred at (60,60). The robot reaches that assigned point, reports in-place, has no MoveTo order and retains a type-0 ordinary fire order for a moving target or a type-2 repair-aim order for a stationary target. War remains false throughout Move. |

The final fire-order type is asserted through `m_Param4`: moving profiles retain ordinary type 0 from `FirePL`, while stationary profiles retain type 2 from `RepairPL` after the player's relocation leaves the stationary target outside effective laser range. The suite records target health and each order's `m_Param4`; it does not infer shots from an order's presence. Q observation: `RepairPL` reuses a leftover target to issue `Fire(des, 2)` even for this laser robot without a repair weapon. This production aim-loop behavior is characterized here and is not corrected by the fixture. This difference is not established as a combat detour, a failed path search or failure to reach the robot's assigned destination.

The test also releases Forward after the first autonomous second and requires progress while that former manual key is still held. In the live-target branch, the opponent must remain alive through the bounded nonarrival at the player's requested point. Physical positions, retained group intent and both kinds of place identity are asserted separately.

## Reservation transfer and a free destination

After the live-target observation, ordinary deletion removes that target. The world advances another 500 ms. Combat references and firing must clear while the player robot stays at (60,60); the distant surviving opponent now holds place 0. Reissuing Move to (100,100) therefore still assigns `(4,4)`. Removing the first opponent does not imply that the desired place will remain free. The enemy-removed control retains its previously acquired place and stays at (100,100).

After another 500 ms, both continuations issue Move to the separate unreserved point (100,300), footprint-corner `(8,28)`. Actual movement coordinates must change to that point, and nine simulated seconds must finish with exact physical arrival, the production in-place result, the retained new Move intention and no remaining MoveTo/fire order. This distinguishes reservation effects from a robot that cannot resume autonomous movement after manual control.

## Evidence and limits

Sixteen registered cases cover two chassis, stationary/moving targets, both RNGs and both timesteps. Each runs the live-target continuation twice and the enemy-removed control twice: 64 sessions per build configuration. Complete same-profile recordings compare the manual robot's exact position/health/speed/direction bits, order queues including `m_Param4` with normalized entity IDs, group intent and destination, actual place and Move coordinates, enemy/target observations, held input and the shared RNG draw count. The session snapshot includes the other registered actors; the added manual robot has fixture ID 5. Observation is checked not to consume RNG. The two continuations require an identical recorded manual prefix and a different first record after the declared intervention.

Source controls temporarily exclude hostile planned places from `PGAssignPlacePlayer` or ignore the later free-point Move. The first must fail the `(4,4)` assignment assertion; the second must fail the new actual movement assertion. The existing enemy-removed track lifecycle remains a passing control for both mutations. Restore exact production bytes, rebuild and rerun the positive cases afterward; mutations are not committed game variants.

Both RNG modes are exercised independently, without claiming a demonstrated RNG-dependent endpoint in this fixed scene. The new cases reuse synthetic geometry and surrogate presentation; they do not establish native picking, rendering, audio, original-host battles, arbitrary reservation layouts or every post-manual order. The manual weapon-range correction in [#209](https://github.com/pozitronik/MatrixGame/issues/209) has its own contract and remains separate. [Successful War detours](84-war-detours.md) are covered separately. Moving-target ineffective/obstructed firing and catalog reconciliation remain open A2 work. These cases do not complete #84 or accept the whole-game baseline.
