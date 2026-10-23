# Successful tactical detours

`game.ai.war_detour.*` follows an armed player robot through a successful tactical firing-place assignment, actual travel, combat and continuation of player intent. It extends A2 / AI-02 and CMD-04 without changing obedience, AI priorities or production code. Existing occupied-place Patrol cases retain their separate failed-search coverage.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.war_detour\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.war_detour\.'
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game\.ai\.war_detour\.'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^game\.ai\.war_detour\.'
```

## Inputs and boundaries

The scene uses seed 84, both supported RNG modes and independent 10/16 ms schedules, bounded at 24 simulated seconds. It starts from the integrated Open movement grid, retains one region and its four places, and supplies footprint-corner coordinates `(8,18)`, `(16,22)`, `(16,24)`, `(48,8)`. Every pair has an unobstructed production movement check and a graph connection; `InitPL` rebuilds the place index after setup. This nearby connected set makes real firing positions available instead of relying on the default layout's four widely separated points.

The observer remains at (100,200). A stationary friendly robot is placed at (100,100), the unarmed target at (240,280), and an uninvolved enemy at (600,600) so that target death cannot finish the battle. Relocation refreshes matrices, bounds and terrain membership at setup; explicit centre checks reject stale geometry. The other robots retain ordinary production logic with zero movement speed. Every frame requires their positions and uninvolved health to stay unchanged.

The observer receives a real laser assembly with a 160-unit radius, 100 ms cooldown and two internal damage units per pulse. The target starts with 200 internal health units, displayed as 20; the test checks the receiver and exact progression of every damaging pulse. Synthetic heat and rotation settings are explicit in the fixture. A retained object core guards observation after target deletion or combat death.

At time zero, a normal context Move requests (500,100), followed by a direct `PGOrderPatrol` call to footprint-corner `(48,38)`, centred at (500,400). Patrol derives its From point from the preceding planned Move endpoint, so the current leg remains (500,100) and the other endpoint is (500,400). Setup adds a synthetic Patrol chassis-sound entry for that call; the Patrol key, button and click routes are outside this scene. This is a declared two-command schedule, not a forced tactical place or injected target memory. Ordinary perception starts War; production tactical logic chooses `(16,22)`, centred at (180,240), and changes the actual MoveTo order away from the original leg.

## Cases and assertions

| Case | Event | Required behavior |
| --- | --- | --- |
| `control` | No intervention | Physically reach (180,240), stop there before the kill, deliver 100 damaging pulses, clear combat references and firing, then resume the original Patrol leg. Resume must appear within one 100 ms side-update interval plus one frame after death. |
| `retirement` | At 800 ms, delete the still-live target through `Session::remove` / `StaticDelete`, while travelling to the firing point. | Core and target-memory references clear. The robot finishes its existing tactical leg, reaches the former firing point and resumes Patrol within one side-update interval plus one frame of that arrival. This preserves the measured continuation delay; target disappearance alone does not immediately cancel the physical leg. |
| `replacement` | At the same point, issue a context Move to (500,400). | Group intent changes to MoveTo, War clears and actual movement coordinates change synchronously to `(48,38)`. The next frame confirms that route; the robot reaches the exact requested point while the target remains alive. Existing firing can continue during the new movement, then must cease with empty target references at arrival. |

Every case checks unchanged player intent until the declared replacement, no failed-place-search timestamp, a real matching place/MoveTo assignment during War, damage while on the detour and eventual continuation. The control and retirement cases stop at the original Patrol leg's approach boundary, x > 490 and |y − 100| < 5, before a later patrol leg can begin another encounter. Replacement requires exact (500,400) coordinates and the production in-place result. Merely setting War or assigning a cached place cannot satisfy the tests: the physical order must change and the unmodified control must reach that place before killing its target.

The twelve registered cases repeat each complete journey in a fresh session. Both intervention cases additionally run the untouched control and compare every frame through 800 ms, before intervention. This is 32 sessions per build configuration. Complete same-profile traces compare exact position/health bits, order queues, group intent and destination, tactical place/search state, enemy/target observations, retained-core presence and RNG draw counts. Observation must not consume RNG. Metadata identifies the revision, compiler/configuration, compatibility settings, synthetic fixture, RNG and schedule; event summaries record War, assignment, physical arrival, damage, retirement, command change, resumption and endpoint arrival.

Both RNG modes are exercised independently; this fixed scene has not demonstrated a mode-dependent tactical choice or event time. The 10 ms and 16 ms schedules do differ, including replacement journey time and pulse count. No RNG-dependent gameplay coverage is claimed from different random state alone.

## Sensitivity and remaining work

Source controls temporarily omit the successful `WarPL` movement dispatch or ignore later `PGOrderMoveTo` commands. The retirement case must reject an absent actual detour at 800 ms. The replacement case must reject an ignored command, while the unchanged-control journey still passes with later Move commands suppressed. Restore exact production bytes, rebuild and rerun the positive matrix after each control; no mutation belongs in the committed change.

The scene runs the full map tick against generated meshes, a no-draw surrogate device and unavailable audio. It establishes neither native presentation nor original-host battle compatibility. It covers one armed robot against one stationary unarmed target, one declared topology and fixed compatibility defaults, not all valid place rankings or distance calculations. Obstructed/ineffective firing against moving targets, the manual-chassis Move nonarrival near a live target, other Patrol cycles and the catalog's ordinary-Move detour wording remain unresolved A2 work. Ordinary Move does not enter War; these tests use an eligible Patrol order and do not silently alter that catalog contract or complete #84/the whole-game baseline.
