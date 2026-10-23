# Moving-target fire feedback

The `game.ai.fire_feedback.*` cases extend A2 / AI-02 through complete production map ticks. They exercise perception, Patrol combat, physical projectiles or laser pulses, impact callbacks, stale-feedback decisions, place assignment, relocation and resumed damage. They do not preload enemy memory, hit/fire timestamps, bad places or War state, and do not call `WarPL` directly.

## Fixture and command schedule

Each session uses the synthetic open 64 by 64 movement grid, seed 84, three live wheel robots and the normal production map loop. The fourth initial robot is removed during setup. Four mutually connected, unobstructed road places have movement-grid coordinates `(8,8)`, `(56,8)`, `(20,2)` and `(56,56)`; their four-cell footprint centers are world `(100,100)`, `(580,100)`, `(220,40)` and `(580,580)`. Setup refreshes relocated bounds and verifies direct traversability. Robot meshes, weapons, terrain and configuration are synthetic; no game package is loaded.

The armed player robot retains its initial world position `(100,200)`. An ally starts at `(220,40)`, and an unarmed, autonomous enemy starts at `(240,400)` for ineffective fire or `(400,280)` for obstructed fire. Enemy health is initialized to 1,000 internal setup units, exposed as 100 by `GetHitPoint`; each damaging pulse removes 0.2 on that exposed scale. The weapon has range 600 and a 100 ms cooldown, with declared heat/cooling settings. The enemy cannot die during the 25-second observation window. Its movement and reaction to attackers remain production AI decisions.

Ordinary context Move commands reserve the ally's initial place and request `(100,100)` for the player robot. The complete map advances until at least 200 ms: 200 with 10 ms frames, 208 with 16 ms frames. A normal `PGOrderPatrol` call then requests movement-grid `(48,8)`, world `(500,100)`. Its starting endpoint comes from the preceding Move, so this is a nondegenerate Patrol between `(100,100)` and `(500,100)`. Patrol enters War after actual perception. The robot first settles at the reserved `(100,100)` firing place. Selection is fixture-controlled; command dispatch uses production group/context routes, without screen picking or keyboard emulation.

## Ineffective plasma fire

The enemy has configured maximum/group speed 0.35 and moves under autonomous AI. The player's real plasma projectiles initially miss: the fire order remains active, last-fire time advances, target health remains 100 and last-hit-target remains zero. These observations must persist for at least three seconds after settling. At the first abandonment, the enemy has physically moved over 50 world units; target age and last-hit age exceed four seconds, while last-fire age remains at most 120 ms. The occupied place must become the single first bad-place entry, `0`, and the current place becomes `-1`.

Only after that measured decision, an ordinary context Move sends the ally to `(100,500)`, freeing the alternative firing place. The player robot must receive a real MoveTo for place 2, physically reach `(220,40)`, and resume impacts and damage before 20 seconds. The test continues to at least 25 seconds, rather than stopping at its first successful relocation. In the measured MinGW profiles, another ineffective period causes a later move to `(580,100)` before sustained hits resume. This is a bounded characterization, not a claim that one reposition always solves interception.

The stationary-enemy control uses speed zero from setup and keeps the ally at its initial place. Real plasma hits must continuously refresh feedback, reduce health below 60 by the final boundary, and retain firing place 0 without a bad-place entry. Because speed differs from setup, this control does not claim a common initial trajectory with the moving-enemy case.

## Friendly obstruction of laser fire

Both the main scene and its control use enemy maximum/group speed 0.1. Before the intervention they must acquire the enemy, fire and deal damage. At the first boundary at or after 1,000 ms, an ordinary context Move sends the ally from `(220,40)` to `(220,140)`. It physically crosses the already active firing line and releases its old road place. The test independently observes the production sphere/ray intersection with radius 25 and robot aim-height offset 5; this geometry check does not replace the AI decision.

While the ally obstructs the line, last-fire time stops advancing and damage ceases. At least four seconds of obstructed, silent frames must be observed with unchanged enemy health. First abandonment requires target age over four seconds and last-fire age over six seconds; the previous occupied place must become bad entry 0 and be invalidated. The robot must receive a real relocation order to the vacated place 2, move, reopen the line, hit the enemy again, and physically arrive at `(220,40)`. A hit can resume during movement, before arrival. Observation continues to at least 25 seconds.

The control leaves the ally at `(220,40)`. Its full state sequence, including RNG draw counts, must match the main scene through the last boundary before the ally command. It must avoid abandonment through 12 seconds and retain effective fire much longer than the main scene. It can later abandon the position because it starts missing the moving target: the test checks this as ineffective fire with a current last-fire timestamp, not friendly obstruction. A control with no commanded obstruction is not a promise of uninterrupted damage for the entire battle.

## Profiles, evidence and sensitivity

Sixteen registered cases cover both engagement families and their separately runnable controls with ParkMiller and LegacyCRT RNGs and 10/16 ms frames. Each main case runs its scene twice and its control twice; each separate control case runs twice. This gives 48 fresh sessions per build configuration. Every session runs the full 25-second window, or the first completed 16 ms boundary beyond it. Repeat runs compare all recorded boundaries exactly within the same build/profile; they do not assume equal trajectories across timesteps or compilers. Both RNG modes are exercised, but these scenes have not demonstrated RNG-dependent gameplay divergence.

Every completed boundary records the shared session snapshot and normalized feedback for all three live robots: current/bad places, failed-search time, enemy count, target identities, target/fire/hit timestamps, health bits and the actual movement order. Records also include War state and ally-ray intersection. Observation verifies that the raw RNG draw count stays unchanged. Per-frame checks retain Patrol intent, War after settling, live entities, finite in-bounds positions and declared relocation/feedback milestones. Trace headers identify the build, compatibility profile, fixture, mode, seed, step and configured enemy speed. JSONL traces are written below the selected build's `tests/scenario-traces/`, with separate main/control and repeat names; the last completed boundary is flushed before the next tick.

Three temporary source sensitivity controls were applied to the actual `WarPL` implementation and restored byte-for-byte. Suppressing the one-enemy ineffective-fire abandonment made all four moving-plasma cases fail while all four stationary controls passed. Suppressing stale-fire blocked-place abandonment made all four obstructed cases fail while all four clear-line controls passed. Suppressing physical War relocation after three seconds made all eight main cases fail while the four stationary controls passed. Restoring production code returned all sixteen cases to passing. These controls are validation evidence, not runtime switches or committed engine changes.

## Reproduce and limits

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.fire_feedback\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.fire_feedback\.'
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game\.ai\.fire_feedback\.'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^game\.ai\.fire_feedback\.'
```

Keep cheats off. The ordinary build helper stages the imported runtime DLLs needed by DLL-linked tests. A focused name such as `^game\.ai\.fire_feedback\.obstructed_control_park_miller_10$` selects the independent clear-line control.

These are synthetic CPU-side battle tests. They do not establish visible rendering, native audio, screen-input selection, original-host integration, moving multi-enemy retargeting, other chassis, later Patrol cycles or resource-backed gameplay. The existing component matrix in [AI decisions](84-ai-decisions.md#ineffective-and-obstructed-fire-boundaries) retains exact threshold, multiple-enemy and failed-search-retry cases. The baseline catalog's ordinary-Move attack-detour wording still needs reconciliation because ordinary Move does not enter War. This increment does not complete A2, issue #84, or the whole-game acceptance gate.
