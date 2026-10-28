# Physical bomb escape and return

Three registered `game.ai.bomb_escape_*_journey` cases extend the time-zero planning tests through physical movement, threat removal or detonation, and completion of the saved route. They link production `EscapeFromBomb`, robot orders, navigation, perception, map ticks, weapon effects and damage callbacks. No production behavior is changed.

## Fixture and observation boundary

The shared synthetic session supplies an open 64 by 64 movement grid and its four connected refuge places. Two friendly robots begin at (120,100) and (180,100); an enemy bomber is at (100,100), with another enemy at (540,540) to keep the battle active after its removal. Positions are world coordinates. The nearer friendly robot is the cover unit; the farther robot is the escaping unit. Only the escaping unit has nonzero movement speed. Each robot starts with 100 internal health units, exposed as 10 by `GetHitPoint`.

The bomber has a real assembled bomb with radius 250 and damage 10 against enemies. The cover unit has an assembled laser with range 150, but receives no firing order. That weapon matters to perception: `GatherInfo` uses the larger ordinary fire range of the two robots. A bomb-only robot and an unarmed enemy both have zero ordinary range, so the initial enemy observation would otherwise expire during the journey. The fixture asserts that the bomber keeps observing the cover until threat clearance and that the cover never fires.

The initial observation and low-level movement intent are explicit component inputs. Robots have no player logic group, and the escaping robot receives `MoveTo(20,20)` before the first `EscapeFromBomb` call. This isolates the escape and return behavior from group-level order replacement. It does not establish that a manual player Move permits autonomous escape; the existing `bomb_escape_planning` test separately checks that group-level eligibility rule. The original destination is movement cell (20,20), whose robot-center position is (220,220).

After setup, `Session::advance` executes complete production map ticks. There are no position writes, synthetic damage calls, injected return orders or patched perception entries during movement. Threat removal uses the session's production object-deletion route; detonation invokes the assembled robot's `BigBoom`, then lets production effect ticks discover and damage nearby objects. These are deliberate component interventions, not claims about mouse or keyboard input. A retained object core observes eventual bomber retirement without dereferencing a deleted robot.

## Scenarios

| Registered case | Setup and required observations |
| --- | --- |
| `game.ai.bomb_escape_free_journey` | The first refuge is movement cell (48,8). The closer cover unit stays behind; the escaping unit saves (20,20), physically reaches its refuge, and later reaches (220,220) with both movement and return orders retired. The removal variant leaves the threat present through the first return promotion and a second automatic diversion, then removes it. The detonation variant fires once the escaping robot is more than 330 world units from the bomber; the cover takes damage while the escaping robot remains unharmed. |
| `game.ai.bomb_escape_reserved_journey` | The stationary cover has a Move destination at (48,8). Corrected place containment reserves it, selecting (8,48); Original containment fails to recognize that off-diagonal reservation and still selects (48,8). Both variants physically visit the selected refuge, preserve the saved destination, and return after removal or detonation. Threat clearance occurs beyond the same 330-unit safety threshold. |
| `game.ai.bomb_escape_unreachable_journey` | Both outgoing edges from the initial refuge place reject the escaping chassis. The original movement destination remains intact and no return order is created. At the first frame at or after 1200 ms, the threat is removed or detonated. The robot continues its original route; detonation damages both it and the cover. The inaccessible refuge graph does not prohibit ordinary movement within the open navigation zone. |

The safety threshold is a fixture trigger outside both the 300-unit escape-threat radius and the configured 250-unit blast radius. It does not redefine either engine rule. Physical arrival at the first refuge is checked within one world unit, while the final original destination is exact. No-refuge runs must remain within 250 world units of the bomber. There is a 45-second simulation bound, a separate session wall-clock bound, and continued observation for at least 500 ms after final arrival and until the retained bomber core becomes null.

## Return timing and damage

The return order does not wait for threat retirement. When the escape Move finishes and `PlaceIsEmpty` accepts the saved destination, `ROT_MOVE_RETURN` promotes it to Move. If the robot subsequently re-enters the threat radius, a later side tick can divert it again. The free-refuge removal case requires this second diversion and removes the bomber at that precise boundary. It then follows the newly chosen refuge and completes the saved destination. The original and corrected containment profiles can choose different subsequent refuges and therefore have different journey durations; their snapshots are never compared to one another.

The free-refuge removal case pins the second diversion at 8910 ms for 10 ms frames and 8976 ms for 16 ms frames, under both RNG modes and both containment profiles. This schedule-specific observation distinguishes the existing 300-unit threat gate: reducing it to 275 units postpones diversion and fails the assertion. It is a contract of this synthetic scene, not a universal AI deadline. Earlier events retain their event-order and physical-state assertions; for example, the MinGW 10 ms profile records the safety crossing at 4180 ms, the first refuge within one unit at 6000 ms and return promotion at 6180 ms. Those three illustrative times are printed rather than asserted. Exact integrated reference comparison remains a separate baseline task.

The bomb effect applies its area callback when its 300 ms lifetime becomes negative. The cases require damage exactly `(300 / step + 1) * step` after detonation: 310 ms for 10 ms frames and 304 ms for 16 ms frames. The damaged robots finish with 90 internal health units (9 through `GetHitPoint`); safe robots retain 100. Neither side gains a robot kill from these nonlethal collateral hits. The bomber's later object retirement is observed independently of the damage deadline.

## Repetition and validation

The [bomb-escape recording guide](bomb-escape-recordings.md) describes the finite persisted corpus, its semantic checks and exact reference comparison.

Each registered case runs both RNG modes, both 10/16 ms schedules, Original/Corrected place containment, and removal/detonation. Every combination runs twice in fresh sessions: 32 sessions per case, 96 per build configuration. Seed 84 is explicit. Snapshots compare every completed frame, including exact position and health bits, order queues, destinations, target identities and RNG draw counts, from the initial escape decision through terminal observation. Threat-clearance boundaries are included separately. Observation must not consume RNG; the initial planning call consumes none. This is same-build reproducibility, not a historical reference or cross-compiler equality claim.

Each of the three registered cases has a 90-second CTest process limit to accommodate its complete 32-session matrix on slower runners, including MSVC Debug. The 45-second simulation bound, separate per-session wall-clock bound, profile combinations and timing assertions are unchanged.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.bomb_escape_.*_journey$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.bomb_escape_.*_journey$'
```

The three cases passed locally with pinned MinGW 13.2 x86 Debug and Release, cheats disabled. Reducing the production escape-threat radius from 300 to 275 units makes the free-refuge journey fail its second-diversion timing assertion. The production source was restored byte-for-byte, rebuilt and all three Debug journeys passed again. An earlier control disabling `ROT_MOVE_RETURN` promotion made the free-refuge journey fail its bounded completion assertion; neither mutation is retained. Current CI remains responsible for MSVC and DLL build coverage. Existing planning, reservation and capacity regressions remain separate contracts.

These are synthetic, non-rendering component battles. They do not establish native display/audio quality, human command dispatch, every crowd layout, production map configuration, or complete AI baseline acceptance. Native execution and integrated reference evidence retain their existing owners in the [completion checklist](baseline-completion.md) and [native validation ledger](native-validation.md).
