# Synthetic building fixtures

`tests/battle_building.hpp` supplies a real `CMatrixBuilding` with a synthetic graph for CPU battle scenarios. It reuses a caller-owned, already loaded `CVectorObject`; it adds one real group node for factories or three nodes for the base floor and doors. It calls production `OnLoad`, `RNeed` and `JoinToGroup`, including base registration in terrain cells. It neither replaces building logic nor claims to exercise the packaged building model loader.

The returned engine pointer owns an unregistered building. Keep the map and donor geometry alive until it is destroyed. Transfer ownership explicitly with `map.AddObject(building.get(), true)` followed by `building.release()` when the ordinary map scheduler should tick it. Use interior coordinates suitable for the base's three-by-three terrain footprint. The supplied regressions use (100, 100); arbitrary asymmetric coordinates and packaged mesh dimensions are not validated by this fixture.

`game.production.building_geometry` checks all six building kinds, live ownership, graph bounds, turret capacity, CPU ray picking and base-cell cleanup. The ray-distance check permits four float ULPs at 40 world units because triangle intersection arithmetic introduces rounding; it is a fixture geometry check, not a relaxed gameplay trajectory comparison.

`game.production.queue_deployment` follows production queue ownership into `RobotSpawn` at the exact synthetic 100 ms construction boundary. It asserts queue order, robot/base states, build statistics, one RNG draw and unchanged balances. This component test closes the base explicitly to isolate the second queue handoff.

`game.production.base_exit` instead runs the complete ordinary map boundary at 10 ms steps: queue completion, base opening, robot move-out, restoration of manual control, base closing and final cell-safe teardown. The test supplies an already assembled chassis-only robot; constructor UI, payment, module assembly and packaged model loading remain outside its scope. Both deployment cases use seed 83 in Park-Miller and LegacyCRT modes with audio disabled. The existing battle fixture supplies a non-rendering device; no native window or private resource package is needed.

Run these contracts with `tools/test.ps1 -Configuration Debug -Filter '^game.production.(building_geometry|queue_deployment|base_exit)$'`, then repeat with `Release`. They contribute to construction/deployment evidence but do not complete the behavior baseline.
