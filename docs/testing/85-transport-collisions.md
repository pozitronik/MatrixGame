# Weapon-attached delivery and projectile outcomes

These O3 contracts extend the [survival schedule profiles](85-survival-profiles.md) with a synthetic weapon-attached transport and actual plasma collision. Production gameplay is unchanged. All cases use seed 85, both RNG modes and separate 10/16 ms schedules, with unavailable audio and no native rendering.

```powershell
$cases = '^game\.outcome\.(armed_transport_survival|armed_flyer_does_not_prevent_elimination|transport_projectile_outcomes)$'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
```

## Attached weapon and survival

The shared transport fixture optionally declares a body attachment and a separate weapon model with its firing matrix. `OrderFlyer` and `ApplyOrder` construct the carrier; ordinary resource preparation parses the descriptor, constructs its laser weapon and updates attachment transforms. A ray in carrier-local coordinates passes outside the body and intersects the weapon mesh at the expected distance. The body-only control must miss the same ray. This catches a descriptor that silently disables the weapon instead of loading it.

`armed_transport_survival` follows the last player robot through carrying, release, landing and carrier retirement. It retains the independently measured release/landing/retirement times of the body-only profiles and verifies active-side survival, carrier links, retirement and no kill credit. `armed_flyer_does_not_prevent_elimination` removes cargo and the owner's ordinary robots, leaving only the weapon-attached carrier. The carrier remains live, but the owner is eliminated at 10/16 ms and the corresponding loss/win dialog opens at 2020/2032 ms. Both player and enemy carrier owners are checked; the enemy-owned carrier's player-owned cargo remains the previously documented Debug-F quirk.

These cases do not call the unused `BuildFlyer` or `CMatrixFlyer::FireBegin` paths. A weapon attachment does not imply an autonomous combat-flyer mode. Rotor/engine animation, native drawing and firing from the carrier are outside this fixture.

## Projectile collision and outcome continuation

`transport_projectile_outcomes` hits either the player carrier or its cargo, once while attached and once after landing. The map initially contains both ordinary enemy robots; the player has only the delivery cargo. The shot is submitted at the production weapon boundary with a declared synthetic launch position and a live enemy robot as owner. It does not simulate high-level target selection, robot aiming or a human mouse command.

At 1000/1008 ms or 7000/7008 ms, the fixture computes a diagonal ray into the actual target model and requires a production map trace to identify that actor. It then creates one plasma weapon shot through `FireBegin` and weapon `Takt`, ends firing and advances ordinary map ticks. The projectile, collision trace and `WeaponHit` callback deliver damage; the test does not call the damage receiver directly. The shot uses a range of 200 world units, a 1000 ms cooldown and explicitly lethal damage. Damage arithmetic and nonlethal receiver behavior remain covered by the earlier combat and transport controls.

| Target and phase | 10 ms impact | 16 ms impact |
| --- | ---: | ---: |
| Carrier before release | 1080 | 1088 |
| Cargo before release | 1080 | 1088 |
| Carrier after landing | 7080 | 7088 |
| Cargo after landing | 7100 | ParkMiller 7120; LegacyCRT 7104 |

Exactly one shot and one completed hit callback are required. A destroyed carrier retires its retained core and active-delivery entry. Before release, its dependent cargo dies and the player loses without robot-kill credit; after landing, the cargo survives and keeps the player active. A direct projectile hit on the cargo enters its death state, clears any carrying link and credits exactly one enemy robot kill while leaving the carrier alive at impact.

For loss before release, elimination occurs at 2020/2032 ms and the paused loss dialog at 4030/4048 ms. Loss after landing occurs at 8050/8080 ms with the dialog at 10060/10096 ms. Each preceding tick must remain outside the later state. Destroying an empty carrier after landing must produce neither elimination nor a result dialog through the same deadline. Every variant repeats in a fresh session and compares map snapshots, carrier/core lifetime, cargo state and callback count. Retained cores and weapon/projectile ownership are released before fixture teardown.

## Coverage boundary

This supplies the selected O3 weapon-attached empty-carrier and transport collision variants, and reusable attachment data for navigation work. It does not establish every multipart carrier model, strategic target eligibility, native portraits or presentation. Successful map initialization/restart, selector relaunches and original-host reentry remain separate lifecycle contracts. Baseline acceptance still requires the reviewed integration evidence.
