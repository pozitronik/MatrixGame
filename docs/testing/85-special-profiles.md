# Special-object timing profiles

This extends the existing special-object cases selected by O4 in the [outcome completion plan](85-outcome-completion.md). It covers separate 10 ms and 16 ms schedules through production map ticks, with seed 85 and both ParkMiller and LegacyCRT. The isolated `special_survival_flags` case retains seed 79 and now uses both schedules and RNG modes as well. The [outcome guide](85-battle-outcomes.md) describes the underlying synthetic descriptors, direct damage calls and fixture limits.

## Assertions

`game.outcome.special_break`, `game.outcome.special_explosion` and `game.outcome.special_animation` retain enemy-damage rejection, minimum-damage thresholds, single objective retirement and kill attribution. Removing the enemy army while one objective survives does not win the battle. Destroying the last objective then losing the player's army still produces the delayed win. Each RNG/schedule profile is run twice in one process; ordered snapshots from the final objective retirement to the result dialog, including raw RNG draw counts, must match within that profile. Earlier transitions have explicit state assertions rather than repeated snapshot comparisons. Different schedules are independent contracts and are not expected to produce identical trajectories.

`game.outcome.terron_destruction` retains enemy-damage rejection, Pain and Death animation selection, repeated damage during the countdown, conversion to static ruins and delayed victory. Both player-army survival variants run in each RNG/schedule profile. The test observes the final tick before conversion and the final tick before the result dialog, so a shifted boundary cannot pass merely by reaching the eventual result.

`game.outcome.special_survival_flags` supplies each special-survival flag, removes the player's robots and observes that ordinary elimination remains suppressed across status scans. This is a control for the flag predicate; it does not substitute for production damage and destruction.

## Measured boundaries

Times below are elapsed map time from a fresh fixture. Each value is asserted separately for its schedule and both RNG modes; the 16 ms values were measured through production ticks rather than rounded from the 10 ms profile.

| Observation | 10 ms schedule | 16 ms schedule |
| --- | --- | --- |
| One surviving ordinary special objective still blocks victory; final objective is then destroyed | 2,020 ms | 2,032 ms |
| Last tick before the Break/Explode/Anim result dialog | 6,030 ms | 6,048 ms |
| Break/Explode/Anim win dialog opens | 6,040 ms | 6,064 ms |
| Repeated Terron hit during the death countdown | 2,000 ms | 2,000 ms |
| Last tick with Terron still special and no building kill credited | 5,000 ms | 4,992 ms |
| Terron converts to static ruins and awards one building kill | 5,010 ms | 5,008 ms |
| Last tick before the Terron result dialog | 8,040 ms | 8,064 ms |
| Terron win dialog opens | 8,050 ms | 8,080 ms |

The pre-dialog checks assert absence of a dialog and pause. Break/Anim cases observe every tick until the first pause and assert its exact time; Terron checks the final pending tick and advances once. `CMatrixMapObject::LogicTakt` converts Terron only after its death timer becomes negative, so the 10 ms profile retains the objective at exactly 5,000 ms. For the 16 ms schedule, the map splits a frame into 10 ms and 6 ms logic portions: the countdown has 8 ms left at 4,992 ms and becomes negative during the next frame. The delayed result is governed separately by the map's scheduled status checks. Starting from the fixture's status timestamp of -1,500, the strict `now - previous > 1001` condition and assignment of `previous = now + 1001` produce scans at 16, 2,032, 4,048, 6,064 and 8,080 ms; conversion occurs between scans, and win scheduling and dialog presentation occur at the following two scans.

## Run the cases

From the repository root, using the pinned prerequisites:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.(special_break|special_explosion|special_animation|special_survival_flags|terron_destruction)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.(special_break|special_explosion|special_animation|special_survival_flags|terron_destruction)$'
```

These five CTest cases contain their own RNG/schedule variants. Their fixtures require no original resource package, visible game window or listening check.

## Related coverage and remaining gates

The [mixed-objective cases](85-mixed-objectives.md) cover Break/Anim retirement in either order, an ordinary-object negative control, Terron alongside a surviving Break objective and last-player loss immediately before versus after the decisive transition. The [post-conversion Terron blast regression](186-terron-owner.md) covers the former Debug-only failure separately; the normal Terron case keeps nearby robots outside the damaging radius. These fixtures establish the finite O4 family selection, not every original map or weapon/input route. Persisted comparisons and same-revision integrated acceptance remain with the [baseline checklist](baseline-completion.md).
