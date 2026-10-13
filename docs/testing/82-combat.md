# Combat damage and lifetime contracts

These contracts implement part of [combat preservation](https://github.com/pozitronik/MatrixGame/issues/82). They exercise production damage handlers and weapon-hit callbacks; they do not establish the complete COM-01, COM-02 or COM-03 behavior baseline. The source reference is `a64e9db4595bf06cc456772cb02c70f6a34b6804`, with scenario ownership defined by the [behavior catalog](../research/75-behavior-baseline/README.md). No production behavior or resource format changes are included.

## Running the tests

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.combat\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.combat\.'
```

The thirteen CTest contracts link `MatrixGameInternal`, use `MG_CHECK` in every configuration and run in separate processes without an interactive window or supplied resource packages. Every contract runs with seed 82 in both Park-Miller and legacy CRT modes. The fixture is the existing `CommandWorld`, with engine-allocated objects and synthetic damage tables restored at teardown. No simulation frames are advanced: the schedule consists of the explicit damage/callback sequence in each case. This is component evidence, not a substitute for the controlled full-map scenario profile or its trace comparison.

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

## Remaining baseline work

COM-01 still requires actual firing for every configured weapon family, aiming and obstruction, cooldown/heat timing, head/armor modifier application, chained/area damage, all supported target classes including flyers and map objects, target disappearance during attack/repair, and the full effective difficulty/configuration profiles. The current tests directly supply damage tables and resistance values; they do not prove module construction derives those values correctly. `WeaponHit` callback injection does not prove projectile creation, travel, collision selection or terminal callback/refcount balance.

COM-02 still requires robot spawn/active/death animation and bounded removal, real bomb arrival/manual activation, death during capture/repair/direct control, robot and cannon debris/effect cleanup, and actual projectiles surviving their shooter. The building test covers the immediate lethal transition; it does not advance the later explosion/removal schedule or prove dependent robots subsequently complete their own deaths.

COM-03 autonomous target acquisition, rotation, firing, retargeting, obstruction, angle/range/cooldown boundaries and destruction with live projectiles remain uncovered. Damage to a cannon is not evidence for its tactical logic.

The shared battle fixture currently creates unarmed wheel chassis. Extending these cases to firing requires real synthetic armor/weapon attachment data and the corresponding effect resources/configuration. Cannon `RNeed`/`LogicTakt` additionally requires basis, turret and shaft geometry, fire matrices and `m_CannonsProps`. Projectile branches in `MatrixEffectWeapon.cpp` load object meshes and use billboard/effect resources; replacing them with no-op stubs would not satisfy the baseline. Coordinate a reusable fixture extension before adding full-map schedules and reference traces. Native rendering/audio evidence remains separate even after synthetic firing is supported.

These gaps retain their original ownership under the combat issue and remain blockers for complete-baseline acceptance. They do not authorize a gameplay change or structural extraction. Windows x86 host DLL and MSVC configurations need their CI results; local MinGW component checks alone do not establish those profiles.

## Validation evidence

The MinGW Debug and Release engine suites each passed 231 tests, including all thirteen combat contracts above. Two temporary production mutations were rejected in Debug: changing the robot minimum-health comparison from `>` to `>=` failed `minimum_health`, and changing the projectile range rejection from `<` to `<=` failed `projectile_range_callback`. Both mutations were removed before the complete passing runs; no production source change remains. Repository/whitespace, branch-policy and local Markdown-link checks passed. No native battle, MSVC build or host DLL execution was performed locally for this increment.
