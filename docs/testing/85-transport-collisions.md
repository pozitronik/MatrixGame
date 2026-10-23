# Weapon-attached delivery and projectile outcomes

These O3 contracts extend the [survival schedule profiles](85-survival-profiles.md) with a synthetic weapon-attached transport and actual plasma collision. Production gameplay is unchanged. All cases use seed 85, both RNG modes and separate 10/16 ms schedules, with unavailable audio and no native rendering.

```powershell
$cases = '^game\.outcome\.(armed_transport_survival|armed_flyer_does_not_prevent_elimination|transport_projectile_outcomes|transport_context_rejection)$'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
```

## Attached weapon and survival

The shared transport fixture optionally declares a body attachment and a separate weapon model with its firing matrix. `OrderFlyer` and `ApplyOrder` construct the carrier; ordinary resource preparation parses the descriptor, constructs its laser weapon and updates attachment transforms. A ray in carrier-local coordinates passes outside the body and intersects the weapon mesh at the expected distance. The body-only control must miss the same ray. This catches a descriptor that silently disables the weapon instead of loading it.

`armed_transport_survival` follows the last player robot through carrying, release, landing and carrier retirement. It retains the independently measured release/landing/retirement times of the body-only profiles and verifies active-side survival, carrier links, retirement and no kill credit. `armed_flyer_does_not_prevent_elimination` removes cargo and the owner's ordinary robots, leaving only the weapon-attached carrier. The carrier remains live, but the owner is eliminated at 10/16 ms and the corresponding loss/win dialog opens at 2020/2032 ms. Both player and enemy carrier owners are checked; the enemy-owned carrier's player-owned cargo remains the previously documented Debug-F quirk.

These cases do not call the unused `BuildFlyer` or `CMatrixFlyer::FireBegin` paths. A weapon attachment does not imply an autonomous combat-flyer mode. Rotor/engine animation, native drawing and firing from the carrier are outside this fixture.

## Context target eligibility

`transport_context_rejection` supplies an enemy-owned armed carrier before release and after landing, retaining the original player-owned-cargo quirk. An ordinary player robot, selected directly by the fixture, receives a real context command with that live carrier as the supplied cursor target. In both phases the command becomes a ground Move with a null objective, rather than an Attack on the carrier. `OnRButtonDown` uses `IsLiveUnit`, which recognizes robots and cannons but excludes flyers. This rejection does not make the model immune to weapon traces: the following cases separately establish reachable collision. The cargo is still a live robot in the logic list while carried; these checks do not claim that robot AI can never acquire or fire at it.

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

For loss before release, elimination occurs at 2020/2032 ms and the paused loss dialog at 4030/4048 ms. Loss after landing occurs at 8050/8080 ms with the dialog at 10060/10096 ms. Each preceding tick must remain outside the later state. Destroying an empty carrier after landing must produce neither elimination nor a result dialog through the same deadline. Every projectile and context-command variant repeats in a fresh session and compares its declared observations. Projectile snapshots include carrier/core lifetime, cargo state and callback count; retained-core identity is checked before dereferencing an observed actor. The weapon retains the live enemy owner and its side attribution, and no second shot or callback may occur during the continuation. Retained cores and weapon/projectile ownership are released before fixture teardown.

## Coverage boundary

The cases supply O3's selected weapon-attached carrier and transport collision outcomes, and a reusable body-plus-weapon fixture for N6. The separate [autonomous targeting cases](85-transport-ai.md) now establish carrier rejection, carried-robot acquisition/aiming and firing after landing without a supplied target or shot. The [physical approach obstacle cases](81-carrier-approach.md) separately characterize a body-only carrier crossing a narrow building despite its height response; defect disposition remains open. Other multipart models, native portraits and presentation retain their own coverage obligations. Successful map initialization/restart, selector relaunches and original-host reentry remain separate lifecycle contracts. Baseline acceptance still requires the reviewed integration evidence.

## Validation

The pinned MinGW Debug and Release standalone game and affected test targets build. The four new contracts and the body-only survival and delivery controls pass in both configurations. Disabling the attached weapon descriptor makes the geometry-pick assertion fail; independently suppressing `WeaponHit` damage leaves the traced actor alive and fails the completed lethal-hit assertion. Both mutations were removed and the restored controls passed. Supported compiler and x86 DLL validation remain for CI. No original assets, visible game window or native input were used.
