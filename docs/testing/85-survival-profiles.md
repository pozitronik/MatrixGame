# Survival and transport outcome schedules

This extends the existing O3 schedule inventory in the [outcome completion plan](85-outcome-completion.md#existing-schedule-extensions). The nine cases below run independent 10 ms and 16 ms map schedules in both ParkMiller and LegacyCRT modes. Their source is [battle_outcome_scenarios.cpp](../../tests/battle_outcome_scenarios.cpp); the existing [outcome guide](85-battle-outcomes.md) describes their synthetic building, cannon and delivery fixtures. Production gameplay is unchanged.

## Reproduce

Run these commands from the repository root with the pinned Windows x86 tools:

```powershell
$cases = '^game\.outcome\.(base_survival|factory_does_not_prevent_elimination|turret_does_not_prevent_elimination|flyer_does_not_prevent_elimination|transported_robot_prevents_elimination|transported_robot_removal|transported_robot_damage|carrier_removal|carrier_damage)$'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
```

Each case retains its original 10 ms assertions and adds the 16 ms profile to the same CTest entry. Metadata reports the RNG mode, seed, timestep, fixture identity and build revision/dirty marker. Base/factory cases use seed 79; cannon and transport cases use seed 85. Audio is disabled. Assertions remain active in Release. All fixtures are synthetic and need no game packages, rendering window or player input.

## Survival and delayed results

`base_survival` removes the side's two initial robots, leaves one live base and observes that the side remains active. The base is then deleted directly. `factory_does_not_prevent_elimination` instead leaves a factory, which becomes neutral when the side is eliminated. `turret_does_not_prevent_elimination` separately leaves an active and an under-construction turret; both remain owned and live after side elimination. `flyer_does_not_prevent_elimination` removes a delivery carrier's cargo and the side's initial robots, leaving only the empty carrier. Each fixture runs for player and enemy ownership and asserts the corresponding win/loss flag and dialog.

| Transition | 10 ms schedule | 16 ms schedule |
| --- | ---: | ---: |
| Side without robots/base eliminated | 10 ms | 16 ms |
| Its result dialog opens | 2020 ms | 2032 ms |
| Surviving base deleted | 2020 ms | 2032 ms |
| Side eliminated after base deletion | 4030 ms | 4048 ms |
| Result after base deletion | 6040 ms | 6064 ms |

The tests check the last tick before the delayed dialog and the first paused tick. The base case also checks that the side is still active immediately before its post-deletion elimination scan. These times describe production status scanning for this fixture, rather than a prescribed new timer rule.

## Delivery and loss

`transported_robot_prevents_elimination` leaves the delivery cargo as the player's only robot and advances through 20000 ms. It records the first release, landing and carrier-retirement tick, verifies carrying/falling/landed states and the carrier's cargo reference, and requires the side to remain active throughout. The surviving cargo remains live; the retired carrier's retained core has a null object and the active-delivery list is empty. Neither side receives robot kill credit.

| RNG / timestep | Cargo released | Cargo landed | Carrier retired |
| --- | ---: | ---: | ---: |
| ParkMiller / 10 ms | 6100 ms | 6420 ms | 11960 ms |
| ParkMiller / 16 ms | 6112 ms | 6432 ms | 11968 ms |
| LegacyCRT / 10 ms | 4940 ms | 5260 ms | 9670 ms |
| LegacyCRT / 16 ms | 4944 ms | 5264 ms | 9664 ms |

The 16 ms values were observed from the production flight and falling paths, independently of the 10 ms expectations. In particular, the LegacyCRT carrier retires earlier in the 16 ms run; rounding its 10 ms retirement upward would specify the wrong behavior.

The remaining four cases act once before release and once after landing. Nominal 1000/7000 ms interventions run on the first scheduled tick at or after that time: 1000/7000 ms for the 10 ms profile and 1008/7008 ms for the 16 ms profile. The test explicitly verifies the cargo's carrying versus landed state at each intervention. It does not round a measured result deadline to fit a profile.

| Scenario | Production behavior asserted |
| --- | --- |
| `transported_robot_removal` | Direct cargo deletion immediately nulls its retained core and clears the carrier's cargo reference. The player's side then dies on its next status scan, with no robot kill credit. |
| `transported_robot_damage` | A lethal direct laser-damage call enters the cargo's death state, releases it from the carrier and credits exactly one enemy robot kill. The side is eliminated on its next status scan. |
| `carrier_removal` | Direct carrier deletion immediately retires its object/core and active-delivery entry. Before delivery the released cargo is marked to die and enters the death state on the next tick; after delivery it remains live through 10000 ms. No robot kill is credited. |
| `carrier_damage` | Direct nonlethal laser damage reduces carrier hit points from 1000 to 999 without ending the battle; a subsequent lethal call retires the carrier. Cargo death before release and survival after delivery follow the same boundaries as direct carrier removal, without robot kill credit. |

| Cargo loss timing | 10 ms elimination / loss dialog | 16 ms elimination / loss dialog |
| --- | --- | --- |
| Before release | 2020 / 4030 ms | 2032 / 4048 ms |
| After landing | 8050 / 10060 ms | 8080 / 10096 ms |

The cargo-loss checks assert an active side on the preceding tick, elimination at the recorded boundary, no dialog on the tick before its deadline, and a paused loss dialog at the deadline. Destroying the carrier after delivery has no loss dialog: the landed robot keeps the side alive.

## Limits

This completes the nine existing survival/transport cases' schedule extension, not all O3 implementation. Last-base destruction through a weapon, a weapon-attached carrier, and projectile/targeting collisions with the carrier or cargo remain selected work. The current carrier has only a synthetic body mesh, and lethal damage is supplied directly to the production damage receiver. Direct deletion is a separate lifetime control. Neither path establishes target eligibility or weapon collision.

These fixtures execute map logic and retained-core cleanup, not loaded-map restart, successful initialization/reentry, real rendering or native result interaction. Full-baseline recording, comparison, compiler/DLL integration and acceptance retain their existing owners and gates.
