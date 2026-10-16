# Guarded base capture through combat

`game.ai.guarded_base_journey` and `game.ai.guarded_base_target_destruction` cover the shared A3/#85 O1 encounter in the [baseline completion plan](baseline-completion.md). They extend the earlier fixed-time capture-priority decisions with natural enemy detection, weapon damage, guard death, resumed movement and capture, and cleanup. They do not complete the rest of the AI or outcome baseline.

## Scene and boundaries

Each case uses an open synthetic map, an enemy base at (100, 100), its attached active cannon at (220, 140), two selected player robots and two unarmed enemy robots. The first player robot carries a laser with a 400-unit range and 100 ms cooldown. Four five-point laser impacts destroy the cannon's 20 hit points. The cannon has a 300-unit range and 100 ms cooldown; its shots inflict one internal robot-health point, displayed as 0.1. No test directly damages or deletes the guard, injects its enemy-memory entry, or calls a side decision in place of advancing the map.

Before any command, the fourth initial navigation place is positioned at the cannon's movement coordinates (22, 14), retaining the four-place graph capacity and connections. `InitPL` rebuilds its lookup and the production `CalcCannonPlace` binds the cannon; the test checks a valid matching place. This follows the real loader's required binding. A combat-only cannon with the constructor's `m_Place == -1` is not a valid fixture for capture movement, because `PlaceFindNear` inspects active cannon reservations. Cannon construction costs are set to 1000 per resource, above the scene's available income, so the enemy cannot replace the guard during the bounded encounter.

The context Capture command enters through `OnRButtonDown`. Every subsequent frame runs `Session::advance`, including production perception, side decisions, robot movement, firing, object damage, effects and lifetime processing. Buildings are initially registered before robots, matching map loading. Synthetic meshes, weapon parameters, initial positions and cannon parent/slot ownership are fixture inputs. The renderer supplies storage operations without drawing, and audio is unavailable. These are registered-map behavior tests, not a resource loader or native playtest.

Both RNG modes use seed 84 and separate 10/16 ms schedules. Every profile runs twice in fresh sessions: eight runs per case, sixteen in total. The compared trace begins immediately after the context command and ends at 12000 ms. It contains every map-frame snapshot plus the state before and after the explicit base-damage intervention in the destruction case. Robot state, orders, RNG draw counts, group intent and War state, guard/base lifetime, guard health, capture assignments, current guard-memory/target references, base ownership/paint fields, income and kill counters are included. Equality is a repeatability check within one profile; fixed impact sequences and transition assertions provide independent behavior oracles. It is not comparison against an earlier game's recording or cross-toolchain byte equality.

## Combat and capture

The group initially retains Capture with one assigned capturer and no known guard. The cannon begins shooting before the group interrupts capture. A member's naturally acquired enemy memory makes the group enter War, cancel its individual capture and select the attached cannon. The group-level Capture intention remains in place throughout combat.

| Observation | 10 ms frames | 16 ms frames |
| --- | --- | --- |
| First cannon hit on the second player robot | 220 ms | 224 ms |
| Capture interrupted; War begins | 310 ms | 240 ms |
| Player laser impacts on the guard | 530, 630, 730, 830 ms | 480, 576, 688, 784 ms |
| Capture reassigned after guard death | 910 ms | 800 ms |
| Base fully open for the resumed capture | 4240 ms | 3984 ms |
| Base transferred to the player | 8800 ms | 8576 ms |
| Completed group capture intention released | 8810 ms | 8640 ms |

The cannon's complete damage sequence is 220, 320, 420, 520, 620, 720 and 820 ms at 10 ms; at 16 ms it is 224, 320, 432, 528, 624 and 720 ms. Every hit affects the second player robot, with the exact health decrement asserted. The guard's final player hit enters `CANNON_DIP`, releases the parent slot, decrements the guard count, removes current enemy-memory/attack references and credits exactly one player turret kill. The debris object remains until 5420/4592 ms under Park–Miller or 4370/5088 ms under the legacy RNG, for 10/16 ms respectively. Retained object cores distinguish combat death from later physical deletion.

Capture resumes with exactly one capturer. The first robot enters the base and is consumed when ownership transfers; this is the existing base-capture rule, and neither side receives a robot-kill credit. The second robot survives. The tests require a closed player-owned base, no remaining capturer, no capture order on the surviving robot and a stopped group with no objective reference.

Bases use their owning side's texture in `CMatrixBuilding::Draw`. Base capture changes `m_Side`; it does not run the factory paint/erase sequence. The tests therefore check the new side's green color and preserve the full red factory-paint fields supplied initially. Those unused fields do not mean that the captured base should render red. Actual rendering remains outside this fixture.

The income interval is 1000 ms with both sides' resource multiplier at 100%. Income is checked after every frame for all four resources. Building logic runs before robot logic, so a payment on a transfer frame would still belong to the previous owner. By 12000 ms, each resource has paid 24 to the enemy and 12 to the player at 10 ms, or 24 and 9 respectively at 16 ms. The 16 ms schedule pays once per 1008 ms because the timer resets when it crosses the threshold.

## Capture objective destroyed after guard combat

The second case follows the same combat and resumed capture until 6000 ms. At that point the base is open and its capturer has disabled manual control. A declared component-level laser `Damage` call destroys the base, credited to the player. This hit isolates objective retirement; it does not establish a second weapon projectile hitting the building. The guard is still killed through actual combat in this case. The base's explosion effect runs with zero fixture damage and a 20-unit radius, excluding unrelated collateral damage from this capture-lifetime scenario.

The intervention clears the capturer's objective and marks that robot for destruction. The group releases its now-invalid target at 6010/6064 ms. Physical building removal follows at 9010/9008 ms, with generated ruin geometry and explosion assets; retained cores verify retirement without dereferencing deleted objects. The base never transfers, no player income is paid, and each enemy resource remains at 18/15 after its final pre-destruction payment. Exactly one turret kill and one building kill are credited to the player, with no robot-kill credit. The second player robot and both enemy robots survive.

## Running the cases

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.guarded_base_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.guarded_base_'
```

The remaining AI and outcome families stay subject to their own completion requirements and the exact-revision baseline acceptance gate. These generated-geometry encounters do not replace native visual/audio checks, host integration, arbitrary terrain, or capture under additional armed opponents.

The focused cases pass MinGW x86 Debug and Release. Temporary negative controls that retain the parent's guard count after cannon death, or bypass the guarded-base capture-priority branch, each fail the corresponding runtime assertion. Restoring those production paths restores passing checks; neither mutation is retained. Release also passed 30 consecutive invocations of each case after the fixture's cannon navigation binding was established. MSVC and DLL coverage are delegated to CI.
