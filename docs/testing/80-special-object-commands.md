# Player commands for special and ordinary map objects

Four `game.player.*` contract families characterize keyboard, interface-action and context commands against special Break/Anim objectives, with ordinary-object and live-unit controls. Each family has four CTest cases, split by RNG mode and timestep. They reuse the outcome executable's generated geometry, material/weapon fixtures and production map ticks. Seed 80, both RNG modes, separate 10/16 ms schedules and unavailable audio define the profiles; no original resources, native window, rendered picking or direct damage submission participates.

```powershell
$cases = '^game\.player\.(special_attack_routes|special_bomb_routes|special_bomb_stop|ordinary_object_commands)\.'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
.\tools\test.ps1 -Configuration Debug -DLL -Filter $cases
.\tools\test.ps1 -Configuration Release -DLL -Filter $cases
```

The sixteen cases retain all 88 declared scene/route/RNG/schedule profiles, each twice in a fresh session. A registered case executes 8, 12 or 16 fresh sessions for one fixed RNG/timestep pair. Splitting the process matrices keeps the slower MSVC Debug checks bounded without reducing the profile set, simulation duration or replay assertions. Each process retains its existing 90-second CTest limit; individual simulation and fixture wall-clock bounds are unchanged.

## Selected scenes and input boundary

The primary robot receives a generated armor/laser or armor/bomb assembly and is selected alone. Two ordinary unarmed enemies remain under production AI. The tested map object is placed at `(400,100)` and a second special objective at `(600,100)`. The spare prevents a tested destruction from ending the battle. Special descriptors are `+Break,1,10,None` or `+Anim,0:0:10:1#1:0:0:-1`; an ordinary Break descriptor omits the leading plus. Object cores and real meshes establish live identity, geometric bounds and temporary-logic-list membership before input.

World picking is supplied through the map trace fields. Keyboard A/E/K enters the real form's Attack/Bomb/Capture preorder; the interface path invokes `CIFaceList::PlayerAction` with the corresponding element name. The subsequent world click enters the side's ordinary left-button handler. Context Attack/Move enters its right-button handler. These routes validate and dispatch production group commands without directly assigning group intent or a tactical target.

Before each special-object keyboard/button command, the test cancels a preorder with X, submits a late click and requires an unchanged battle snapshot, cleared ordering flags and no detonation. It then submits the real command. Context input has no preceding explicit preorder.

| Case | Selected profiles before repetition | Required continuation |
| --- | ---: | --- |
| `special_attack_routes` | 24: both special families and three routes, both RNGs and schedules | Accepted Attack retains the object's map-cell destination but immediately loses its actor reference; the robot approaches, its ordinary targeting fires at enemies while Attack stays active, and it leaves the objective unchanged for 30 seconds, then accepts Stop and a fresh Move |
| `special_bomb_routes` | 16: both special families and keyboard/button routes, both RNGs and schedules | The reference is likewise cleared, but the bomb reaches the saved area, requests detonation, dies, damages/converts the objective, credits one building kill and retires its core |
| `special_bomb_stop` | 16 with the same dimensions | After at least 500 ms and physical progress, Stop cancels the bomb; the robot and both objectives survive the 30-second observation, the robot stays stationary in the final five seconds and subsequently completes a fresh Move |
| `ordinary_object_commands` | 32: ordinary context Move, two explicit Attack fallbacks, two Capture rejections, and three live-enemy Attack controls, both RNGs and schedules | Ordinary input preserves the ground-footprint fallback or pending Capture rejection; a fresh Move completes. A living enemy instead remains bound as the target and takes actual laser damage through each attack route |

## Special-target reference loss

This is a **Legacy quirk (Q), preserved by maintainer decision on 2026-10-24** under [the command finding owned by #80](../research/89-legacy-evidence/80-command-findings.md#special-target-reference-loss). The characterization does not accept a correction or establish that targeted Attack succeeds. Both input handlers explicitly accept `IsSpecial` for attack-related commands. `PGOrderAttack` and `PGOrderBomb` store the selected actor, then synchronously invoke `TaktPL`. Its target validation rejects objects that fail `IsLive`, which recognizes robots, cannons and buildings. The special object is still present in the temporary logic list, but the command's actor reference is already null when input returns.

The tests require that immediate null reference and the preserved special-object destination, obtained from its geometric center without the ground footprint subtraction. They then observe what the existing engine does with that command. Attack remains active through the 30-second bound without damaging or converting the objective. The robot's ordinary environment targeting must produce fire intent and actual emission at enemies while the Attack order stays active; these observations do not attribute engagement to the Attack order's own war branch. Normalized environment targets exclude either special object from the initial fire attention. Laser heat is configured as one unit per emission with no cooling, making emission observable without consuming the production `IsFireWas` flag. The two normal enemies use zero incoming laser damage in this scene, preserving their lifetimes for the full observation.

The ordinary-enemy positive control uses the same assembled laser and range 120, with a live opponent initially at `(400,200)` and sufficient health to survive. It retains the actor reference and delivers actual damage within ten seconds. That control demonstrates working targeting and weapon integration, while its different model and position are explicit fixture differences. The reference-loss finding therefore does not rely only on an undamaged object or a missing effect.

A separate correction needs review of downstream target consumers and a recorded gameplay/reference decision. In particular, general position helpers and tactical enemy lists have narrower actor-type contracts than the input handlers. Merely changing the meaning of `IsLive` throughout the engine would expand unrelated behavior. The accepted disposition keeps the measured result; a correction is a separate post-baseline proposal. This changes no production code and approves no complete reference replacement.

## Bomb, cancellation and recovery

The bomb has radius 150, a 100 ms cooldown and object damage 10; incoming bomb damage to ordinary robots is zero. Production travel and detonation, rather than a supplied damage call, convert the target. Break becomes the ordinary static type, while Anim advances to its terminal non-special state. Exactly one special-object count and one building kill are consumed; the spare remains untouched, both sides retain the required ordinary survivors and no win/pause dialog may open.

The first sampled detonation request coincides with the group becoming Stop. Robot death follows on a later step, objective conversion follows death, and the retained actor core retires later still. This sequence distinguishes a completed bomb request from explicit cancellation. Stop controls must never request death or damage the objective. Keyboard and button Stop can rebuild the player's logic group; observations follow the actor's resulting group rather than assuming that the previous slot remains active.

After bounded Attack non-completion or explicit bomb Stop, the robot receives Stop and a new context Move toward `(120,500)`. Complete map ticks must reach an assigned place in that region within a further twenty seconds, leaving the objective intact. This recovery interval includes the obstacle detour from the objective area. Initial selection and command dispatch remain separate from native pointer visibility or screen picking.

Ordinary non-live/non-special objects take different input branches: context input becomes Move, explicit Attack becomes a targetless ground attack with the usual footprint subtraction, and explicit Capture remains pending without changing the group. X clears that rejected Capture preorder; a late click still cannot create a capture command. The ordinary controls then replace the command with a fresh Move and require physical arrival while both map objects remain unchanged.

## Replay and validation

Every scenario repeats and compares its ordered observations within the same compiler/build/RNG/schedule profile. The existing session snapshots supply raw position/health/order bits, registered identities and shared RNG draw counts. Additional fields capture special-object type/state/health/count, normalized tactical target roles, full fire-order aim bits, weapon heat, death-request state and group destination/target nullness. Retained cores guard actor and object reads. No pointer address or process-global robot UID is used as a replay oracle; no persisted historical reference artifact is added by these cases.

Sensitivity controls should remove special eligibility from input, suppress bomb damage to map objects or suppress laser damage in the ordinary-enemy control. The affected cases must reject these changes at the destination, conversion or receiver-health assertions. Restore production source before rebuilding and rerunning the positive profiles. Supported compiler/DLL and combined-baseline validation remain separate requirements.

The input and current-behavior evidence covers the selected command variants; the special-target legacy quirk (Q) retains its [accepted preservation disposition](../research/89-legacy-evidence/80-command-findings.md#special-target-reference-loss), without establishing correct targeted Attack or accepting the complete baseline. The command work package and whole-baseline gate remain open. Native input/visual evidence, accepted historical comparisons and gameplay correction are outside this increment.
