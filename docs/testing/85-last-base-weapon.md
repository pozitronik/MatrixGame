# Last-base weapon outcome

`game.outcome.last_base_weapon` covers the last-base encounter in O3 of [the outcome completion plan](85-outcome-completion.md). It connects the normal player group attack command, actual robot laser hits, building demolition, side elimination and the delayed Win dialog in one registered map session. A live enemy factory and turret remain throughout the encounter. An otherwise identical control retains one enemy robot and verifies that the side stays active after its base is gone.

## Fixture and command path

Each case uses seed 85, ParkMiller or LegacyCRT, and a 10 or 16 ms fixed map step. Two independently constructed sessions must produce the same complete trace. The eight profiles therefore execute sixteen fresh sessions per test invocation. Traces start after synthetic scene construction, include every map step, record both sides of the attack command, and end at the Win dialog or at 12000 ms for the surviving-robot control. They include the shared robot snapshot and RNG draw count, base lifetime and health, side status, remaining factory/turret state, attacker target references, building kill credit and result state. They do not compare wall-clock play time.

The synthetic open map contains two player robots, an enemy base at (200, 200), an enemy titan factory at (500, 100), and a live enemy turret at (500, 500). Both enemy robots are removed during setup; the control retains the second one. Robot movement speed is zero, keeping this encounter within a fixed weapon range. The player group still receives `PGOrderAttack`; no target pointer, hit, damage receiver or result is injected after that command. The first robot has a production laser weapon attached to synthetic geometry. The second is unarmed. The base has 50 internal hit points, and each laser hit deals 30. Its displayed health starts at 5 and falls to 2 after the first hit.

The player weapon range is 180 units. The turret remains live with cannon logic enabled, has a one-unit seek radius and a valid navigation place bound through `CalcCannonPlace`, and cannot engage the distant attackers. The factory and turret are outside the player's weapon range. Resource periods exceed the bounded observation window, and the base's final explosion executes with zero collateral damage. These choices isolate the last-base survival rule from production, travel and splash-damage scenarios, which have separate coverage.

Before attacking, the test passes two scheduled side-status scans. The enemy side remains active with its base intact, even when it has no robots. At 2020/2032 ms the test submits the group attack and then advances the complete production map. Robot targeting, weapon updates, map tracing and ordinary damage dispatch produce both impacts. Synthetic graphs and textures replace packaged art; native drawing and audible playback are outside this fixture.

## Observed transitions

Both RNG modes have the following timings; each profile is checked against these values and against its fresh replay.

| Event | 10 ms steps | 16 ms steps |
| --- | ---: | ---: |
| Group attack submitted | 2020 | 2032 |
| First laser hit; base health becomes 2 | 2130 | 2160 |
| Second hit; base enters `BUILDING_DIP`, player receives one building kill | 2230 | 2256 |
| Group releases the destroyed objective | 2320 | 2256 |
| Next side scan: no-robot enemy becomes `SS_NONE` | 4030 | 4048 |
| Base explosion state | 5140 | 5168 |
| Physical base removal | 5240 | 5264 |
| Following side scan: Win dialog pauses the map | 6040 | 6064 |

The dead base stops preserving its side before its demolition animation finishes. `CMatrixMapLogic::Takt` counts live robots and live bases as survival entities. When neither remains, it neutralizes the side's surviving factory, leaves the live turret owned by that side, and schedules the result. The tests assert these identities, ownership changes and status boundaries on every observed step. Retaining an enemy robot leaves the factory and turret enemy-owned, prevents `MMFLAG_WIN`, and keeps the map running without a result through 12000 ms.

The status scan uses `now - previous > 1001` and stores `previous = now + 1001`. With the initial value -1500, scans occur at 10/2020/4030/6040 ms or 16/2032/4048/6064 ms. The result is displayed on the scan following elimination. Building demolition sets hit points to -1 and then uses that field as its countdown. The base explosion and removal compare strict thresholds at -2900 and -3000 before decrementing it by the current step. The fixed object traversal and step size account for the recorded explosion and removal times.

At death, the test verifies player building-kill attribution without robot or turret kill credit. The attacker drops the dead base from enemy memory and its attack target. The group subsequently releases its objective. Physical deletion nulls the retained object core, leaves only the fixture's observation reference and clears every terrain unit's reference to the base. The initial footprint is checked to contain nine base references, so the removal check cannot pass merely because the fixture omitted those references. The factory and turret retain their health, and all robots expected to survive remain alive through the result boundary.

## Running and limits

Run the registered test in each supported standalone configuration:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.last_base_weapon$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.last_base_weapon$'
```

Local negative controls bypass the laser's production damage dispatch and count the surviving factory as a survival entity. The first fails at the measured first-impact health assertion, and the second fails at the expected elimination scan. Both mutations are removed before rebuilding and rerunning the focused outcome suite.

This case supplies the O3 last-base weapon encounter and its factory/turret non-survival control. [Guarded-base journeys](84-guarded-base.md) separately cover actual guard combat followed by capture or objective destruction. Existing `game.outcome.base_survival` checks the mirrored player-loss status/result receiver using explicit base deletion; it does not constitute an enemy-weapon-driven player-defeat encounter. The new test does not claim that additional mirror, strategic enemy attack selection, splash effects, packaged-map loading, rendering or native input coverage.
