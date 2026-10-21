# Mixed special-object outcomes

These cases cover the finite mixed-objective selection in O4 of the [outcome completion plan](85-outcome-completion.md). They complement the [single-family profiles](85-special-profiles.md) and [post-conversion Terron blast regression](186-terron-owner.md). No production behavior changes here.

## Fixtures and actions

The fixture registers each object with the map and parses actual `+Break`, `+Anim`, `Break` and `Static` descriptors. A leading `+` makes an object a victory objective. Break objects become the catalog's static entry; the Anim object advances from state zero to state one. Terron uses the named Main, Pain and Death animations. The catalog helper accepts several descriptors but preserves the existing single-family catalog and all earlier oracles.

Object hits call production `CMatrixMapObject::Damage` directly. Last-player loss calls production `CMatrixRobotAI::Damage` with lethal laser damage, credits the opposing side, creates death effects and continues through the complete map tick. These are component damage deliveries to registered objects, not a robot aiming or firing, a bomb command or player input. Those dispatch paths remain with C3. Initial removal of unwanted robots is explicit fixture setup.

Each case uses synthetic meshes, seed 85, both supported RNG modes and separate 10 ms and 16 ms schedules. It requires no original packages or visible game window. Terron's radius is 20 here, excluding collateral damage; the separately registered blast regression covers nearby units with radius 150.

## Selected cases

| CTest case | Variants | Contract |
| --- | --- | --- |
| `game.outcome.mixed_special_order` | Both retirement orders and last-player loss before versus after the preceding status scan; 16 profiles, each repeated | The first retirement leaves the other objective active; only the second sets the special-completion flag and awards the second building kill. A scan that has already scheduled defeat keeps that outcome; completion before the scan protects the player and eventually wins. |
| `game.outcome.ordinary_objective_control` | Four RNG/schedule profiles, each repeated | An ordinary Break object accepts enemy damage and becomes static ruins without consuming the surviving special objective, awarding building credit or setting special/win flags. Removing the enemy army still does not win. Later loss of the player's last robot produces defeat. |
| `game.outcome.mixed_terron_objective` | Last-player loss before/after conversion, plus loss scanned before the lethal Terron hit; twelve profiles, each repeated | With the Terron-dead flag already set, both conversion-order controls win despite a surviving special Break object. A loss scanned before the lethal hit instead reaches defeat while Terron is still in its death countdown, with both objectives still special and no building kill. |

For mixed Break/Anim, the early-loss variant kills the last player robot at 2,010/2,016 ms, before the status scan at 2,020/2,032 ms. The scan schedules defeat and sets the player side to `SS_NONE`; only then does the final objective retire. In the late-loss variant, the final hit and player death occur after that scan, so the next scan sees the special-completion protection. The direct hit and death need no operating-system input arbitration: the tested distinction is the intervening production status scan.

The Terron conversion-order controls retain the previous schedule: player loss occurs on the last tick before conversion or immediately afterwards, with the Terron-dead flag already set in both. An additional early-loss variant kills the player at 2,010/2,016 ms, runs the status scan, and only then delivers the lethal Terron hit at 2,020/2,032 ms. This distinguishes protection acquired before elimination from protection acquired after defeat is pending.

Special-object setup rejects enemy damage without health, progress-bar or RNG changes. Player damage reduces health from ten to six; the minimum-health threshold prevents another reduction at six. The final hit uses damage six with threshold zero. Repeated hits after retirement cannot consume another objective or award additional building credit. A repeated Terron hit at 2,000 ms leaves Death animation, zero health, the objective count and RNG unchanged, and must not restart its countdown. The Anim object's post-retirement health is still subject to ordinary direct damage; the assertions do not claim that it becomes invulnerable.

## Transition times

| Observation | 10 ms schedule | 16 ms schedule |
| --- | --- | --- |
| Early loss, before the decisive status scan (Break/Anim and early-loss Terron) | 2,010 ms | 2,016 ms |
| Mixed Break/Anim final retirement; late loss, or early-loss Terron lethal hit | 2,020 ms | 2,032 ms |
| Ordinary-object control: last-player loss after enemy elimination failed to win | 2,020 ms | 2,032 ms |
| Early-loss Break/Anim and Terron: Lose dialog; late-loss Break/Anim schedules victory | 4,030 ms | 4,048 ms |
| Mixed win / ordinary-control loss dialog | 6,040 ms | 6,064 ms |
| Terron: last tick before conversion | 5,000 ms | 4,992 ms |
| Terron conversion | 5,010 ms | 5,008 ms |
| Next status scan schedules the Terron win | 6,040 ms | 6,064 ms |
| Terron win dialog with one surviving special objective | 8,050 ms | 8,080 ms |

The map's strict `now - previous > 1001` status condition, followed by `previous = now + 1001`, places checks more than 2,002 ms apart. In these fixtures the first check is at 10 or 16 ms. Without a previously pending result, direct Break/Anim retirement sets `SS_JUST_WIN` and the special-completion flag; the next status check sets the win flag and schedules the dialog, and the following check opens it. The ordinary-object control follows the corresponding loss path.

Q: a previously scheduled defeat is not canceled by later special-object completion or Terron protection. In the early-loss Break/Anim variant, the Lose dialog opens with `MMFLAG_SPECIAL_BROKEN` set, `MMFLAG_WIN` clear and the player status still `SS_JUST_WIN`. In the early-loss Terron variant, the dialog opens with `MMFLAG_TERRON_DEAD` set, `MMFLAG_WIN` clear, player status `SS_NONE`, and Terron still in its uncompleted death countdown. These assertions record the existing ordering quirk; they do not correct it or infer a preferred result from the contradictory state. A gameplay correction requires a separate decision and regression contract. The existing single-family guide explains the separate Terron countdown boundary.

Terron differs from the Break/Anim families: conversion requests victory without checking whether the remaining special-object count reached zero. The source contains a commented count check, but it is not active gameplay logic. The mixed-Terron assertions preserve the implemented rule rather than inferring a different rule from that comment. As a negative control, temporarily enabling that count condition makes the mixed case fail its conversion-time `SS_JUST_WIN` assertion; restoring the source restores the pass. A separate mutation that permits ordinary special completion at one remaining objective fails the mixed Break/Anim case immediately after the first retirement, before the second objective is damaged.

## Observation and comparison

Each profile runs twice in a fresh fixture in the same process. Ordered traces start after catalog/object registration and initial robot removals, include every subsequent full map tick, and finish at the first result dialog. Additional samples follow scheduled damage batches, retirement, player death and repeated hits. Rejection/minimum-health actions within a setup batch have explicit assertions; they are not individually sampled between every call.

The trace contains the normal scenario snapshot (robot identity, health, movement/orders and RNG draw count), objective count, selected outcome flags, both side statuses, building and robot kill credit, pause/dialog identity, and each object's catalog identity, behavior, special flag, health, animation state where applicable and progress-bar presence. Object identity is its stable fixture position; no pointer addresses enter comparisons. Retained `SObjectCore` references verify that each observed object is still the same live instance before its state is read; they preserve the reference record without preventing object destruction. Explicit assertions establish the transition states and final dialog times. Repeated traces test reproducibility within one RNG/schedule/order profile; they are not claimed to match different profiles or a persisted historical baseline. Same-revision integration and baseline acceptance remain separate gates.

## Run

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.(mixed_special_order|ordinary_objective_control|mixed_terron_objective)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.(mixed_special_order|ordinary_objective_control|mixed_terron_objective)$'
```

The three registered cases execute 64 fresh scenario runs in total. They do not cover every original map script, visual objective feedback, native weapon/input delivery, saved-game loading or the whole-game baseline gate.
