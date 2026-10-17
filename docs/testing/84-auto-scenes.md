# Ordinary-map AUTO scenes

`game.ai.auto_activation` extends A8 with ordinary-map activation, high-level decisions, reinforcement eligibility and return to player control. It starts with all three automatic/demo flags clear and enters AUTO through the production `Cheats::processInput` parser. AUTO enables only `MMFLAG_AUTOMATIC_MODE`; it does not enable `MMFLAG_FULLAUTO` or `MMFLAG_FLYCAM`.

## Scene and schedule

A synthetic open map has two robots per side and one titan factory at (100, 100). The factory belongs either to the player or to the enemy. Setup explicitly assigns the player robots to valid team 0 so the production high-level strategy can take over, and clears every live robot's navigation-place reservation, including the enemy robots. Resource periods exceed the observation window, reinforcement maintenance is enabled with zero initial cooldown, and a successful request starts a 20000 ms cooldown. [Synthetic transport assets](reinforcement-fixtures.md) provide the real carrier and cargo assembly. Native rendering, icons and audible playback are outside this fixture.

Both ParkMiller and LegacyCRT run at 10 and 16 ms fixed map steps. Each of the eight profiles is repeated in a fresh session. Seed 84 initializes the scene. A normal context Move toward (500, 200) advances until 200/208 ms, and the test verifies actual player movement before enabling AUTO. At that declared command boundary it reseeds to 79 so the first reinforcement decision has a reproducible chance input. The parser itself consumes no RNG draw. From then on complete production map ticks drive the side's strategy, tactical decisions, reinforcement carrier and camera.

| Observation at 1200 ms | Enemy-owned factory: no eligible source | Player-owned factory: eligible source |
| --- | --- | --- |
| First reinforcement request, 10/16 ms | None | 210/224 ms |
| Player high-level group and team action | Capture | Defence |
| High-level group members | 2 | 3, including the carrying robot |
| First player's capture order | Targets the enemy factory and remains interruptible | No capture order |
| Remaining maintenance cooldown, 10/16 ms | 0/0 | 19010/19024 ms |
| RNG draws since the declared seed, 10/16 ms | 110/108 | 113/111 |

The request assembles a live player robot with 150 displayed health in `ROBOT_CARRYING` and an actual carrying flyer. No carrier is injected by the fixture. The whole side decision follows the existing [reinforcement contracts](84-ai-decisions.md#automatic-reinforcement-through-delivery); the new case connects it to parser activation, ordinary player movement and subsequent map ticks.

At 1200 ms, another AUTO command disables automatic control without consuming RNG. A context Move toward (120, 500) clears the capture synchronously and preserves the original factory owner. Further full frames through 4000 ms verify that the first robot moves toward the player's destination, retains a Move order and has no capture target. The already requested carrier remains part of the simulation. No result dialog or pause is introduced.

The test checks these transitions and exact draw totals, then compares the complete repeated trace from scene construction through 4000 ms. Each trace records the shared robot snapshot, RNG count, automatic flags, high-level group/team action and membership, factory owner and paint count, capture target/interruptibility, maintenance cooldown and carrier/cargo state. It includes both sides of each command and the deliberate RNG reset. It does not compare wall-clock statistics.

## Boundaries and running

The command boundary is the real cheat parser, below the form's compile-time cheat gate. Calling it in a normal test build does not mean that a cheats-disabled game accepts typed AUTO. `game.player.registered_cheat_effects` separately checks that keyboard gate in enabled and disabled configurations. These scenes do not load a map through `CGame::Init` and do not establish introduction behavior or demo startup. Genuine demo initialization, automatic results and following-session cleanup remain shared A8/O9 work. Flying-camera fight selection is also a separate scene; AUTO alone does not enable it.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.auto_activation$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.auto_activation$'
```
