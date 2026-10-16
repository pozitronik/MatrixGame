# Populated object and effect-spawner reload

`game.restart.objects_spawners` extends the [restart scenarios](85-restart.md) with map-object and effect-spawner reconstruction. It runs seed 79 in both RNG modes at 10 ms and 16 ms, repeats each profile in a fresh session and compares its recorded movement/RNG trace. Every session performs a loader bootstrap followed by two successive menu-confirmed restarts of an altered population. The four profiles and their replays exercise sixteen post-bootstrap reconstruction cycles.

## Stored scene and production route

The synthetic `CStorage` file contains the existing two unarmed robots and distinct side resource balances, plus one static map object at scale 1, one ordinary destructible object at scale 1.5, one smoke spawner and one fire spawner. Object positions, scales, descriptor IDs and spawner coordinates are written to the production storage columns. The destructible object's descriptor converts it to the static type after ten points of damage; it is not a victory objective.

The fixture retains the map's ID catalog and preloads synthetic mesh/texture cache entries, matching Restart's existing cache lifetime. `ReloadDynamics` reads the saved object and effect records, `StaticPrepare` resolves object geometry through the production resource path, and `AddEffectSpawner` parses the descriptor strings. No object graph or spawner is attached after reload. This is a dynamics-loader check; it does not exercise a cold disk texture/mesh load or fresh `CGame::Init`.

Escape, R and Enter enter `CFormMatrixGame::Keyboard`, the real menu and confirmation handlers, and `CMatrixMap::Restart`. Enter dismisses the resulting introduction. The bootstrap uses the same route to create the initial population from storage before its first mutation.

## Reconstruction checks

Before each measured restart, the real damage receiver converts the destructible object and retires its fire spawner. The fixture then removes the converted object, moves a selected robot through the real ground command, changes the player's titanium balance and changes the surviving spawner's position. A separate smoke effect has a live handler at the restart boundary.

After confirmation, the test checks that the map instance, terrain allocation and simulation clock remain intact. Retained cores show that the surviving old static object and old robot were retired; the removed destructible object remains detached. The transient effect handler is cleared and the old effect list is empty. The saved two object types, positions, scales, map UIDs, geometry and destructible hit points return. Both side resource balances are restored, and current and scratch selection are empty. These selection assertions do not cover inactive selection groups; the separate registered `retired_group` regression covers that list.

Both spawners return with the saved positions, types, lifetimes, emission periods, smoke color, speed, intensity and fire dispersion. Their production owner lookup attaches them to the reconstructed objects after simulation resumes. The new player robot accepts a fresh Move and changes position. Smoke and fire effects are counted by their real effect types, independently of movement markers and wheel effects.

Reload consumes exactly two RNG draws, one for each spawner's initial scheduling, while retaining the selected RNG mode. This differs from the robot-only restart fixture, which has no spawners and consumes no draws during reload. With a fixed 100 ms spawner interval and the production strict `now > next` comparison, emissions occur at 110 ms and 220 ms in the 10 ms profile; after 25 frames there are two live effects of each type. At 16 ms, emissions occur at 112 ms, 224 ms and 336 ms, producing three of each type after 25 frames. The test compares the final robot position bits and RNG draw counts across fresh-session replays; it does not claim a complete per-frame state trace.

## Validation and limits

Run the registered restart cases with the normal helpers:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.(movement|pending_win|pending_loss|retired_group|objects_spawners)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.(movement|pending_win|pending_loss|retired_group|objects_spawners)$'
```

Local MinGW Debug and Release checks pass all five selected restart scenarios. Two temporary Release mutations preserve the bootstrap but omit either map-object or effect-spawner loading during subsequent restarts. The new case rejects both at its restored population checks. The production source is restored before final validation. CI supplies the other supported build configurations.

Populated base/factory reconstruction remains a separate native-device case. Restart unconditionally redraws each loaded building on the minimap, even when the whole-scene minimap option is disabled. The storage-only device deliberately rejects rendering calls; this fixture does not simulate successful drawing or bypass the production redraw. Cannon reconstruction, lightning and sound spawner descriptors, rendered output and malformed external map recovery are outside this case. The existing queued-base test establishes old-base/queue deletion, not saved-building reconstruction.
