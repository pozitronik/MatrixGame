# Combat firing, damage and lifetime contracts

These contracts implement part of [combat preservation](https://github.com/pozitronik/MatrixGame/issues/82). They exercise production damage handlers, weapon-hit callbacks and bounded armed full-map scenarios; they do not establish the complete COM-01, COM-02 or COM-03 behavior baseline. The source reference is `a64e9db4595bf06cc456772cb02c70f6a34b6804`, with scenario ownership defined by the [behavior catalog](../research/75-behavior-baseline/README.md). No production behavior or resource format changes are included.

## Running the tests

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.combat\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.combat\.'
```

The thirteen component CTest contracts link `MatrixGameInternal`, use `MG_CHECK` in every configuration and run in separate processes without an interactive window or supplied resource packages. Every contract runs with seed 82 in both Park-Miller and legacy CRT modes. The fixture is the existing `CommandWorld`, with engine-allocated objects and synthetic damage tables restored at teardown. No simulation frames are advanced: the schedule consists of the explicit damage/callback sequence in each case. This is component evidence, not a substitute for the controlled full-map scenario profile or its trace comparison.

Audio has no backend. Hit positions are deliberately far outside the default camera's effect distance, so production explosion culling prevents presentation assets from being loaded. Robot family cases also remain above zero and at or below 50 internal health units. Do not generalize their RNG consumption or lifetime evidence to visible explosions or a native device. The owner-retirement fixture supplies an identity-only core without mesh-derived bounds; it tests ownership and callback dispatch, not collision detection.

## Families and assertions

Tracked configuration in [`CFG/robots/data.txt`](../../MatrixGame/CFG/robots/data.txt), `Source.Damages`, names seventeen entries per target table: plasma, volcano/machine gun, homing missile, bomb/mortar, flamethrower, big boom/robot bomb, lightening/electric, laser, gun, repair, cannon 0 through 3, and the secondary ablaze, shorted and debris damage types. The tests enumerate all seventeen explicitly. The tables use distinct synthetic values to catch wrong index selection; they do not freeze the tracked configuration's balance values or prove its parser.

| Contract suffix under `game.combat.` | Production path and exact observations | Catalog contribution |
| --- | --- | --- |
| `robot_families` | `CMatrixRobotAI::Damage` for all seventeen entries; expected health, live state and flame/electric flags | COM-01 damage-family routing; COM-02 nonlethal state |
| `building_families` | `CMatrixBuilding::Damage` for all seventeen entries; expected health and absence of death transition | COM-01 building damage-family routing |
| `cannon_families` | `CMatrixCannon::Damage` for all seventeen entries; expected health and absence of death transition | COM-01 cannon damage-family routing; no autonomous firing claim |
| `difficulty` | Robot, building and cannon damage: player versus other side, same-side hit and side-zero attacker; enemy/friendly multipliers 0, 0.5, 1 and 2 with fixed expected health | COM-01 multiplier and friendly-fire boundaries |
| `minimum_health` | Robot health immediately below, equal to and above the configured minimum-damage threshold, then a second hit | COM-01 threshold boundary: the check is before subtraction, not a floor clamp |
| `repair` | Robot same-side versus other-side repair amount, saturation, already-full health, `NeedRepair`, zero difficulty factors and dead-state rejection | COM-01 repair arithmetic; COM-02 no revival |
| `status_effects` | Repeated flame hits cap TTL at 5000; electric hits cap at 3000, at 1500 with half protection and at zero with full protection; health remains unchanged | COM-01 resistance/status application, not status ticking |
| `bomb_protection` | Big-boom damage with no, half and full bomb protection | COM-01 bomb resistance, not area propagation |
| `building_death` | A hit reaching zero enters `BUILDING_DIP`, stores the death sentinel, clears a linked robot's base and marks it to die, detaches a cannon's parent and increments the killer's building statistic exactly once; later repair/hits cannot revive or recount it | COM-02 lethal transition and reference cleanup |
| `cannon_repair_state` | Under-construction cannon reports no repair need; invulnerability prevents damage and repair without RNG draws; removing protection allows repair and saturation; dead state rejects repair | COM-02 eligibility and already-dead behavior |
| `building_repair` | Damage followed by friendly repair, other-side repair, saturation and repeated full-health repair; difficulty zero does not suppress repair | COM-01 building repair |
| `projectile_range_callback` | Real `CMatrixEffectWeapon::WeaponHit` callback for homing missile, gun, cannon 0/1/3 at squared distances 99, 100 and 101 with radius 10; equality damages, outside does not; callback count/target/damage flags checked | COM-01 projectile impact range boundary, not trajectory or aiming |
| `projectile_owner_retirement` | Weapon retains its owner's core and side; deleting the shooter clears `GetOwner` but retains side attribution, and a later impact still applies friendly damage and callback flags | COM-02 effect ownership after shooter removal, without an in-flight projectile |

Fractional public health getters scale robot/building internal units by ten. Where an exact float result matters, assertions compare the returned binary32 representation, avoiding a comparison between an x87 extended temporary and a rounded float literal. No approximate tolerance is introduced. Other health/status/stat expectations are exact and use representable integer or half values.

## Armed scenarios and reusable geometry

The armed contracts use the existing `Session` full-map boundary. Firing, repair and detonation scenarios run twice in the same process with seed 82 in each RNG mode, except the separately bounded heat variants; the two traces must match exactly. The laser scenario uses both 10 ms and 16 ms schedules. Plasma and turret scenarios use 10 ms. These are controlled profiles with a storage-only Direct3D surrogate, no native drawing and no audio backend. They do not establish equivalence with a packaged game's frame pacing or its rendering-dependent RNG consumption.

`battle_mesh.hpp` exposes `load_battle_mesh(attachments, cache_name, combat_animations)`. It writes a genuine storage-format box mesh, loads it through production `CVectorObject::LoadSpecial`, and removes its temporary file immediately. Each `MeshAttachment` provides a matrix ID, name and translation. The optional cache name makes a preloaded synthetic mesh available to production `LoadObject`; the optional animations add `Fire` and `FireLoop`. The original unarmed chassis uses the same box, frame and attachment values as before this extraction. Its implementation remains in `battle_geometry.cpp`, so existing targets need no additional mesh translation unit.

`BattleCombatAssets` must be constructed before `Session` and installed after it. It supplies two debris types, non-uploaded billboard texture handles and the plasma-impact and crater decal properties, and synthetic rocket/bullet/mine meshes. The session owns production effect teardown; the outer guard then restores the borrowed static catalogs. `arm_battle_robot` attaches synthetic armor and a weapon, invokes production `RobotWeaponInit` and `CalcRobotMass`, and updates real attachment transforms. All ten robot weapon kinds are supported and exercised, including the second attachment matrix required by repair. `create_battle_cannon` provides the basis/turret/shaft mesh chain expected by turret type 1; production `RNeed` creates the units, weapon, owner reference and firing callback. No targeting, collision, projectile, damage or cooling implementation is substituted.

| Contract | Fixed observations |
| --- | --- |
| `game.combat.robot_firing` | A human context attack order drives player AI, aiming and laser firing. At 10 ms steps, impacts occur at 200 to 1000 ms in 100 ms increments. At 16 ms steps they occur at 224, 320, 432, 528, 624, 720, 832 and 928 ms. Each hit removes exactly two internal health units; sampled heat is zero or ten. Deleting the shooter after 1000 ms (992 ms for the 16 ms schedule) prevents further laser damage during the following 2000 ms. |
| `game.combat.plasma_flight` | The patient's initial maximum movement speed is zero, so it remains a reproducible projectile target. Real plasma creation, movement, collision and callbacks produce impacts at 1120, 1220, 1310, 1410, 1510 and 1600 ms before shooter removal. Already-fired projectiles continue hitting from 1690 through 2450 ms. Fifteen total impacts leave 20 internal health units; subsequent ticks through 3600 ms leave that value unchanged. |
| `game.combat.cannon_firing` | An enemy laser turret chooses the first player robot and hits at 220 to 920 ms in 100 ms increments. Removing that target at 1000 ms causes reacquisition of the other player robot; hits then occur at 1120 to 1920 ms. Exact health is checked after every impact, and the final retained target core must reference the replacement. |
| `game.combat.axis_aligned_trace` | The existing object-sphere trace misses a robot when travel has exactly zero X component, whereas an oblique ray to the same robot hits. `ScanLandscapeGroup` starts its upper interval at zero and expands it in the nonzero-X branch. This records a source-confirmed legacy asymmetry as a candidate for separate investigation, not maintainer acceptance of intended gameplay. It is not a proposed correction or evidence that every vertical ray in a native battle fails. |

Synthetic weapon settings are explicit in the test: range 600 for the robots, cooldown 100 ms, damage two, minimum-damage threshold ten, heat and cooling increments ten, and cooling period 100 ms. The turret uses range/seek radius 300 and its own angle limits. These values establish reproducible control-flow examples, not a balance baseline for packaged configuration. The no-draw billboard catalog uses synthetic intense texture handles; visual correctness and alternate effect catalogs are outside this profile.

Each run writes JSONL under the test executable's `scenario-traces` directory. The header records source revision and dirty state, compiler/configuration, assertion/cheat flags, floating-point mode, RNG and schedule. The header identifies the armed fixture and scenario; standard schema-1 snapshots add a `combat` object. Every boundary includes the session's robots/groups/RNG count, patient health bits, weapon heat while the shooter exists, and turret target/angle observations where applicable. Command records include acceptance, before/after RNG counts and the resulting group order; a completion footer follows the last snapshot. The files are accepted by `tools/compare-battle-traces.py`. Repeated runs report the first divergent boundary with both states. Fixed impact schedules and health assertions provide an oracle in addition to repeatability; two identically wrong runs are insufficient to pass those checks.

## Additional weapon, repair and bomb scenarios

Nine further contracts cover the remaining robot weapon kinds, heat and cooling, real repair effects, and manual bomb detonation. They retain the same synthetic no-draw profile. Six family scenarios use a stationary patient, range 600, cooldown 500 ms and two internal damage units per normal impact. Ablaze/shorted health damage is explicitly zero in those family fixtures, so status effects still tick and consume RNG without obscuring the primary impact sequence. This separation does not establish secondary-damage arithmetic or timing; those are separate cases.

The table lists simulation milliseconds and internal health after each observed health transition. Multiple damage callbacks can occur within one boundary, so a transition is not necessarily a single shot. In particular, the gun fixture includes boundaries with four internal damage units. Expectations record that result without merging it into the other weapon families.

| Contract suffix | Exact health transitions through 4000 ms |
| --- | --- |
| `volcano_firing` | 200:48, 700:46, 1200:44, 1700:42, 2200:40, 2700:38 |
| `electric_firing` | 200:48, 700:46, 1200:44, 1700:42, 2200:40, 2700:38 |
| `flame_firing` | 1820:48, 1850:46, 1880:44, 1910:42, 1940:40, 1970:38, 2000:36 |
| `gun_firing` | 410:46, 900:44, 1390:42, 1890:38, 2380:36, 2880:32 |
| `missile_firing` | 980:48, 1470:46, 1950:44, 2430:42, 2920:40, 3410:38 |
| `mortar_firing` | 1590:48, 2080:46, 2580:44, 3080:42, 3580:40 |

Both RNG modes must match these event/health sequences. Their final draw counts are asserted separately: volcano 263/263, electric 43841/42476, flame 18770/18758, gun 90177/91297, missile 76405/76405 and mortar 41251/41203 for Park-Miller/CRT respectively. Full same-profile replay includes positions, orders, targets, heat and intermediate RNG counts. These deliberately fixed profiles do not imply that moving patients, native rendering or a different weapon configuration should produce those numbers.

`repair_firing` damages a friendly robot from 50 to 30 internal units, issues production `PGOrderRepair`, and observes actual repair-tool acquisition and effects. Four units are restored at 110, 210, 310, 410 and 510 ms. Health remains saturated through 2000 ms. The paired failure variant removes the patient at 300 ms: only the first two repairs occur, and by 400 ms the group target is null and the healer no longer has a firing order. Both variants are replayed in both RNG modes.

`heat_boundary` exercises the firing eligibility boundary using the existing maximum heat of 1000. A heat increment of 500 permits the equality case, then suppression and recovery with cooling increments of 100 every 100 ms. Real hits occur at 200, 790, 1380 and 1970 ms; the sampled maximum heat is 500. Increment 501 prevents the first shot, leaves heat at zero and preserves patient health. Both modes are checked. The 590 ms spacing is the observed combined weapon/robot schedule, not a test-side cooling simulation.

`bomb_detonation` invokes production `BigBoom` with radius 150. The bomber enters `ROBOT_DIP` on the next 10 ms boundary. Its friendly stationary neighbor loses exactly four internal health units at 310 ms; an enemy outside the radius remains unchanged, and neither side receives a robot-kill statistic for the self-destruction. A retained production core distinguishes death from deletion: the bomber's object disappears at 4080 ms with Park-Miller and 4140 ms with CRT. The scenario continues through 6010 ms and is replayed, exercising actual explosions, debris, callbacks and teardown. This is manual activation; reaching a bomb order's destination remains a separate requirement.

## Turret projectile and boundary scenarios

`cannon_projectiles` adds the three non-laser turret weapon families. Each uses the existing synthetic three-part turret and its single muzzle, with range 300, cooldown 250 ms and a stationary robot patient at 50 internal health units. This tests the configured weapon implementations, not the native attachment layout of each turret model. The normal runs last 1000 ms; the destruction variants deal lethal production damage to the turret at 220 ms, after its first shot and before that shot hits, then continue through 6500 ms.

| Weapon | Normal impact time:remaining internal health | Impact after owner destruction | Actual owner removal, Park-Miller/CRT |
| --- | --- | --- | --- |
| `WEAPON_CANNON0` | 240:48, 490:46, 740:44, 990:42 | 240:48 | 3730/4730 ms |
| `WEAPON_CANNON1` | 250:46, 500:42, 750:38, 1000:34 | 250:46 | 3730/4730 ms |
| `WEAPON_CANNON3` | 520:48, 770:46 | 520:48 | 4250/4010 ms |

The destruction variants assert entry into `CANNON_DIP`, exactly one player turret-kill statistic and eventual clearing of a retained production object core. They preserve the observed multiple damage callbacks for `WEAPON_CANNON1`. No extra hits may occur after the one in-flight shot. Every normal and destruction variant is replayed under each RNG mode.

`cannon_boundaries` fixes the distance between the turret and patient centers at exactly 50 units throughout each run. A range of 49 allows acquisition and aiming but no shot. Ranges 50 and 51 produce seven laser hits, at 330 through 930 ms in 100 ms steps. Two additional variants restrict the lower pitch angle: zero radians prevents firing; -0.5 radians allows seven shots but no damage because the beam misses the synthetic patient. Every boundary also checks angle limits, retained target identity, final health and same-profile replay. Acquiring a target, firing and hitting therefore have separate assertions.

`cannon_obstruction` places an inert, under-construction turret between the attacker and its patient. The attacker has no target while obstructed. Removing the obstruction at 500 ms through production `StaticDelete` permits reacquisition and hits at 660, 760, 860 and 960 ms. The unobstructed control hits at 220 through 920 ms, while an attacker initially under construction never acquires a target or damages the patient. These are initial-state and obstacle-removal tests; they do not claim to exercise construction completion. All three variants are replayed in both RNG modes.

## Remaining acceptance work

COM-01 remains incomplete: real firing against building/cannon target geometry; obstruction and friendly-fire shot geometry outside the bomb example; robot range/angle boundaries; armor/head modifiers; splash boundary cases; repair redirection to another valid patient; continuing ablaze/shorted health damage; and packaged balance/configuration integration. All ten robot weapon kinds now have real weapon/effect evidence against robots, and all seventeen damage-table entries have component tests across target classes. Neither proves a complete cross-product of trajectories, target shapes and balance data. There is no identified need for a new production seam for those extensions.

COM-02 still requires spawn/base-exit transitions, bomb-order arrival, death during capture/repair/direct control, chained lethal damage, and death during repair with retained references. Manual robot bomb detonation now covers the real death/debris/removal path with a bounded cleanup oracle. Plasma now covers projectiles surviving explicit shooter removal, including delayed production damage callbacks. The building component test covers the immediate lethal transition; it does not advance the later explosion/removal schedule or prove dependent robots subsequently complete their own deaths.

COM-03 now has laser-turret acquisition, aiming, repeated firing and replacement-target evidence. All four configured turret weapon families now have real firing evidence, including obstruction/reacquisition, firing-radius equality, two pitch limits, initial under-construction suppression and destruction with live projectiles. Remaining requirements include acquisition-radius boundaries, upper pitch and rotation-speed limits, construction completion, friendly-fire/difficulty geometry, and target classes outside robots. The single synthetic attachment layout does not establish native model compatibility.

These gaps retain their original ownership under the combat issue and remain blockers for complete-baseline acceptance. They do not authorize a gameplay change or structural extraction. Windows x86 host DLL and MSVC configurations need their CI results; local MinGW checks alone do not establish those profiles.

## Validation evidence

The initial MinGW Debug and Release engine suites each passed 231 tests, including thirteen component contracts. Two temporary production mutations were rejected in Debug: changing the robot minimum-health comparison from `>` to `>=` failed `minimum_health`, and changing the projectile range rejection from `<` to `<=` failed `projectile_range_callback`. Both mutations were removed before the complete passing runs; no production source change remains.

The first armed extension added four contracts. All 235 engine tests passed locally in both MinGW configurations. A temporary production mutation adding 10 ms to weapon cooldown was rejected by all three firing scenarios and then removed. The original unarmed scenario suite also passed after exposing the mesh writer; sixteen same-profile exported trace pairs were validated and matched exactly, as did eight Debug/Release pairs with an explicit build-profile override; no native battle, MSVC build or host DLL execution is claimed by the local scenario runs.

The additional family/repair/bomb batch adds nine contracts. All 244 tests passed in MinGW Debug and Release. Fifty-two same-profile replay pairs and thirty Debug/Release pairs matched through the supported comparator, with an explicit build-profile override for the latter. Changing the production heat eligibility comparison from `>` to `>=` made `heat_boundary` fail; the mutation was then removed. The fixtures continue to use synthetic assets and do not substitute for native or packaged-resource checks.

The turret extension adds three contracts: all 247 engine tests pass in MinGW Debug and Release. Fifty-six new same-profile trace pairs and twenty-eight Debug/Release pairs match through the supported comparator; the cross-build comparisons explicitly override only build-profile metadata. A temporary change from `>` to `>=` in the turret firing-range rejection failed the exact-range case and was reverted before both full passing runs.
