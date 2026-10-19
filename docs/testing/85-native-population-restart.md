# Native building and cannon reconstruction

These O6 checks extend the [synthetic native host](85-native-sessions.md) through populated map Restart. They cover the rendering-dependent building path that the CPU-only dynamics fixture cannot execute: Restart redraws loaded buildings on the minimap. Initialization, reload, graphics calls and cleanup remain production implementations, using a hidden borrowed window and a real Direct3D 9 HAL device.

## Stored population

The generated 32 by 32 landscape contains two unarmed robots, a friendly base at (180, 480) and an enemy titanium factory at (400, 100). Generated building models provide the base's three parts and the factory's body. Long resource periods isolate restored balances from income. The saved balances are 101/102/103/104 for side 1 and 201/202/203/204 for side 2.

The cannon variant adds a separate basis, turret and shaft model chain, including attachment/muzzle matrices. Two stored cannon records describe one occupied base slot at (80, 480), angle 0.5, and one empty slot at (80, 400). Production property 1 attaches an active cannon to the nearest building; property 2 supplies a place without a live cannon. The occupied cannon inherits its base's side, even though the stored cannon side is 2. The assertions preserve this loader rule rather than treating the initial side column as an independent owner.

The map writer's optional population arguments default to false. Existing callers retain the original two-robot map. The extra asset helpers must run inside the caller's isolated synthetic deployment, after the normal resource materializer and before packing or initializing the populated map. No original game models, packages or configuration copies are added to the repository.

## Checked sequence

| Case | Configuration | Population |
| --- | --- | --- |
| `game.session.native_building_restart` | Text; middle lifetime fails after terrain loading begins | Base and factory |
| `game.session.native_cannon_restart` | Text; the same middle-lifetime failure | Base, factory, occupied and empty cannon slots |
| `game.session.native_packed_building_restart` | Packed with loose configuration parked; middle lifetime has an invalid packed header | Base and factory |
| `game.session.native_packed_cannon_restart` | Packed; the same invalid-header failure | Base, factory, occupied and empty cannon slots |

Each process performs successful Run, failed Run and successful Run again using the same borrowed window/device. Each successful battle dismisses its introduction and performs two menu-confirmed Restarts, for four populated reloads per case. A fresh robot then accepts a Move, advances physically and closes normally. A successful close returns 0; the intervening failure returns 100 without changing the caller's result buffer and retains the existing configuration-error diagnostic assertion.

Before each reload the fixture opens the base, neutralizes the factory and changes titanium. In the cannon profile it also queues a new cannon through building selection, `BeginBuildTurret`, production terrain/slot eligibility and `OnLButtonDown`, then partially advances construction. It changes the stored cannon's side and health. Initial selection and the extra resources supplied for placement are explicit fixture setup. The eligibility update calls side logic with 16 ms and construction receives one quarter of its configured duration; these component calls are not a claim about the native loop's timestep or a completed construction journey.

After Escape, R and Enter, retained cores must show the old buildings and, where applicable, the old stored and queued cannons retired. The map and terrain allocations remain. The saved base is closed, the factory belongs to side 2, both robots exist, all side balances are restored and the introduction owns the pause. The base has exactly nine terrain footprint cells. In the cannon profile the new stored cannon is healthy, idle, attached to the base and bound to its navigation place; the two slots and their occupancy return, and the build queue is empty. Dismissing the introduction permits the next cycle.

After every Run the original host checks still require retired game/configuration/cache/graphics/form aliases, no live game objects, released text buffers, the restored borrowed window procedure, unchanged device reference count and a device that can clear/present. Debug also requires an empty allocation tracker. The hidden-device run establishes successful graphics execution, not image fidelity or original-host integration.

## Running and limits

Enable the existing opt-in native registration as described in the [host guide](85-native-sessions.md#running), then select the population cases:

```powershell
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game.session.native_(packed_)?(building|cannon)_restart$'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^game.session.native_(packed_)?(building|cannon)_restart$'
```

Run `'^game.session.'` to include the original successful/recovering text and packed sessions, invalid-header recovery, repeated pneumatic cleanup and held-input retirement. A usable native Direct3D adapter is required; an unavailable device fails rather than silently skipping. Each process has the existing 30-second deadline and 5,000-callback bound. Generated files are removed after normal completion; interrupted-run cleanup and Debug leak-report limitations remain documented in the host guide.

The negative controls omit either building or cannon reconstruction only inside `CMatrixMap::Restart`, leaving initial loading intact. Both text/packed building cases reject the missing buildings; both cannon cases reject the missing stored cannon and slots. Restoring the production reader restores the passes. These mutations are temporary experiments, not registered alternate implementations.

The DLL chooses ParkMiller and a time-based seed internally. These cases preserve that contract and use bounded invariants; they do not inject a fixed 10/16 ms schedule or claim identical native trajectories across seeds. They do not cover saved map-object/effect-spawner reconstruction, malformed external map semantics, settings application at fresh initialization, original assets or rendered/audible presentation. Those remain separately scoped baseline checks. No new session-replacement or lifecycle policy is introduced.
