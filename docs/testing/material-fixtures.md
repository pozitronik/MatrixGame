# Synthetic material fixtures

The material fixtures extend the no-render battle profile so production object loading can resolve skins and textures. `load_battle_material_mesh` writes a real storage-format box with one material surface, eagerly loads it into the cache, removes the temporary file and gives the object its requested cache name. The existing surface-free mesh helper is unchanged. `install_battle_texture` uploads a four-colour, opaque 2x2 image through production `CTextureManaged::CreateLock`, `UnlockRect` and `Preload`. No game packages, extracted textures, window or native Direct3D device are needed.

`LoadObject`, `CSkinManager::GetSkin`, constructor assembly and building demolition remain production implementations. The material profile advertises two simultaneous texture stages while preparing the real fixed-function skin callbacks; it never invokes their drawing callbacks. It restores the previous capability value after the session. The map destructor owns skin destruction, followed by a reset of the skin manager's static slots before another session; cache teardown releases the texture resources. Sequential sessions and the Debug tracked-allocation check exercise this order.

## CPU texture contract

The test device supports one explicitly requested mip level of `D3DFMT_A8R8G8B8`, `D3DPOOL_MANAGED`, usage zero. Dimensions must be between 1 and 4096; textures and vertex/index buffers share a 128 MiB allocation limit. Texture levels, descriptions, COM identity, reference ownership, priority, preload and bounded rectangle locking are implemented. Pixels retain their contents between locks. Invalid dimensions, rectangles, nested locks and unsupported lock flags return `D3DERR_INVALIDCALL`; operations outside the supported device profile terminate the test with the method name.

The five additional `game.harness.device.*` contracts cover pixel retention and pitch, invalid arguments, COM lifetimes, production bitmap and direct-lock uploads, and the shared allocation budget. Rendering, render targets, surface extraction, mip generation, compressed formats, other pools, native driver behaviour and device reset remain excluded. CPU texture storage does not establish graphical correctness.

## Constructor contracts

The constructor fixture provides wheel chassis 2, Birex armour 1 and their attachment matrices under the production cache names. Both modules have real material records. Synthetic name entries produce `Synthetic hull wheels`; the configured module prices sum to 32 titanium, 34 electronics, 36 energy and 38 plasma, and structure totals 150 displayed hit points.

| Contract | Observed production behaviour |
| --- | --- |
| `game.production.constructor_assembly` | `OperateUnit` and `StackRobot` assemble the two modules, load their skins, apply all four base orientations and the requested team, and mark a negative-team design as crazy with team zero. Enqueueing leaves balances unchanged; calling the queue cancellation component refunds the module price to the owning computer side. Repeated sessions load and retire the same cache identities safely. |
| `game.production.constructor_capacity` | No base or an empty design produces no queue entry. Six designs retain FIFO order and their teams; a seventh enqueue changes neither the queue nor RNG draw count. |
| `game.production.constructor_deployment` | `BuildSpecialBot` produces the design through the constructor. At the configured 100 ms queue boundary, `TickTimer` transfers it to the map, opens the base and increments the computer side's build statistic. `RobotSpawn` chooses the first empty computer team, replacing the design's team 1 with team 0. Computer deployment consumes no player build-sound RNG draw. |

The side-2 cancellation call checks the queue refund component; it is not an AI action. The computer production strategy does not cancel queued robots. These assembly cases do not pair a production purchase debit with its refund. The [AI purchase scenarios](83-production.md#ai-purchase-and-ownership-loss) separately pair a real debit with the refund on base neutralization/deletion; the [player purchase scenarios](83-production.md#player-purchase-callback-and-deployment) cover build-callback payment and per-robot refunds through the queue cancellation component.

These are component contracts with seed 83 in both Park-Miller and LegacyCRT modes. The deployment case calls the queue timer directly; the separate [base-exit contract](building-fixtures.md) covers map-driven opening, movement and closing. Constructor tests use computer side 2 because the player constructor additionally renders robot icons. The production suite additionally loads one repair-equipped design through AI purchase. Rendered constructor interaction, affordability/capacity rejection through the UI, successful icon rendering, other armed and headed designs, the full module catalog and broader strategic production decisions require further coverage. No complete construction baseline is claimed.

## Demolition contracts

`game.harness.materials.demolish_{base,titan,plasma,electronics,energy,repair}` covers every building kind. Each creates a registered building at the safe position (200,200), supplies synthetic ruin models and textures under the production names, and applies lethal laser damage through `Damage`. Ordinary 10 ms map steps then execute explosions, ruin loading, smoke placement and deletion. The base's final blast executes with zero configured damage and a finite radius so this check isolates demolition ownership; weapon blast damage belongs to combat tests.

The factory is removed at 1,010 ms. The base enters its final exploded state at 2,910 ms and is removed at 3,010 ms. The test asserts one primary ruin for the base and an additional secondary ruin for factories, the loaded geometry and skin identities, position and orientation, and the original shadow modes. No shadow resources are generated in this profile. Ruins appear in spatial groups but are absent from the logic list. A retained object core becomes null on deletion; kill credit remains one after subsequent ticks. The base's nine registered terrain references must be cleared.

Smoke records must have the production smoke type and configured properties, a bounded positive count, and owners that resolve to the resulting ruins or landscape rather than the deleted building. Each RNG mode repeats the complete scenario and compares per-frame state, RNG draw count and smoke placement exactly within the same build. Exact smoke counts and positions are not asserted across compilers because placement depends on random ray/triangle intersections. Both Debug and Release execute the same lifecycle assertions.

This extends the synthetic building lifetime contract. It does not test packaged model decoding, rendered explosions or shadows, arbitrary-coordinate base cleanup, or destruction caused by a complete weapon attack. The existing [base cleanup defect](https://github.com/pozitronik/MatrixGame/issues/130) remains separate; the safe-placement guard is retained. Further combat scenarios can reuse these materials without bypassing the ruin loader.

## Running the checks

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(harness\.(device|materials)|production)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(harness\.(device|materials)|production)\.'
```

Use `-Compiler MSVC` for the corresponding compiler profile. Successful local fixture checks are prerequisites for further scenarios, not acceptance of the whole-game baseline or native rendering.
