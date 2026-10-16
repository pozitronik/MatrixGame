# Terron blast after conversion

Terron creates its final explosion before replacing itself with static ruins. The explosion outlives that conversion and may hit a nearby robot after the ruins have left the active logic list. The weapon retains an `SObjectCore` reference, and conversion keeps the core and object alive. Destruction clears the core's object pointer; leaving the logic list does not.

The former Debug attacker check treated absence from the logic list as an invalid pointer and stopped at a breakpoint. The check now also accepts an object registered in the map's ownership list. It compares pointers without dereferencing a rejected attacker and still rejects an address absent from both lists. The additional query and check compile only in Debug without `_RELDEBUG`; Release damage, timing, ownership and attribution are unchanged.

## Regression

`game.outcome.terron_blast_after_conversion` replaces the unregistered defect probe. It runs the synthetic Terron at (400, 100), final-blast radius 150, cooldown 100, seed 85, both RNG modes and independent 10 ms and 16 ms schedules. Each profile keeps or removes the player's two robots before the countdown. The two enemy robots remain subject to normal map logic. The ordinary `terron_destruction` case retains its noncollateral radius 20 and position (600, 100).

The collateral profiles use explicit normal damage of 600 or 1,200 internal health units and friendly damage 7. These are fixture values, not changes to the game's damage tables. They distinguish a surviving hit from a lethal hit and make an accidental friendly-side attribution observable. The Terron is neutral, and its blast must not award either side a robot kill. The separate building kill remains credited to the player.

## Characterized result

Before the correction, the registered test's assertions pass in Release at production revision `fad309b43e6051862af732a912ea5f7bb6c7f113`. The Debug reproduction stops with `0x80000003` on the first post-conversion hit. With the ownership check corrected, the same health, attribution and draw-count assertions pass in both configurations. The complete Release regression output is identical before and after the correction. A temporary change assigning the blast to the player instead of its stored neutral side makes the lethal profile fail its zero-kill-credit assertion; restoring the callback restores the pass.

Enemy robot 3 has 100 displayed health until the final blast. Damage 600 reduces it to 40; damage 1,200 kills it. Enemy robot 4 remains at 100, and both retained player robots remain at 100. Removing the player army does not change the measured draw counts. Neither side earns a robot kill, while the player receives exactly one building kill for Terron's conversion. Victory occurs with either army setup.

| Checkpoint | 10 ms schedule | 16 ms schedule |
| --- | --- | --- |
| Conversion to static ruins | 5,010 ms | 5,008 ms |
| Last tick before collateral damage | 5,200 ms | 5,184 ms |
| Collateral damage/death | 5,210 ms | 5,200 ms |
| Win dialog | 8,050 ms | 8,080 ms |

The following cumulative RNG counts include the complete map simulation, effects and death processing at those four checkpoints. They are measured independently for each schedule; a 16 ms run is not a rescaled 10 ms run.

| RNG | Step | Damage | Conversion | Before hit | After hit | Result |
| --- | --- | --- | --- | --- | --- | --- |
| ParkMiller | 10 | 600 | 129201 | 131865 | 132031 | 157146 |
| ParkMiller | 10 | 1200 | 129201 | 131865 | 132497 | 186432 |
| LegacyCRT | 10 | 600 | 152426 | 157794 | 158108 | 210111 |
| LegacyCRT | 10 | 1200 | 152426 | 157794 | 158571 | 238824 |
| ParkMiller | 16 | 600 | 79842 | 80998 | 81136 | 94537 |
| ParkMiller | 16 | 1200 | 79842 | 80998 | 81602 | 113529 |
| LegacyCRT | 16 | 600 | 81944 | 84362 | 84616 | 108444 |
| LegacyCRT | 16 | 1200 | 81944 | 84362 | 85079 | 127433 |

The scenario checks that the converted owner's retained core still points to the same object, that it has left the logic list, and, in Debug, that the map recognizes it while null and an unregistered object fail the membership query. The scenario does not detach the blast owner or retain unnecessary logic updates.

## Run

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.(terron_destruction|terron_blast_after_conversion)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.(terron_destruction|terron_blast_after_conversion)$'
```

The fixtures link production simulation, damage and effects; synthetic meshes and a storage-only device replace resource loading and drawing. They open no game window and require no proprietary assets. Native appearance, sound and original-map playtests remain separate from this ownership regression.
