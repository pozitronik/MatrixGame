# Synthetic building fixtures

`tests/battle_building.hpp` supplies a real `CMatrixBuilding` with a synthetic graph for CPU battle scenarios. It reuses a caller-owned, already loaded `CVectorObject`; it adds one real group node for factories or three nodes for the base floor and doors. It calls production `OnLoad`, `RNeed` and `JoinToGroup`, including base registration in terrain cells. It neither replaces building logic nor exercises the packaged building model loader.

The returned engine pointer owns an unregistered building. Keep the map and donor geometry alive until it is destroyed. Transfer ownership explicitly with `map.AddObject(building.get(), true)` followed by `building.release()` when the ordinary map scheduler should tick it. Coordinates must be finite and inside the map. Before allocating or registering a base, the helper checks that its complete three-by-three footprint lies inside the map and inside the production destructor's cleanup window.

That restriction guards a concrete production defect: `CMatrixBuilding::~CMatrixBuilding` uses the x coordinate for both cleanup axes, whereas `OnLoad` registers cells using x and y separately. An unsafe asymmetric footprint can leave terrain `m_Base` pointers referring to a deleted building. The fixture rejects it before registration; it does not repair the destructor or establish arbitrary-coordinate deletion safety. Safe asymmetry such as (100,120) is permitted and checked. The defect requires a separate correction and regression.

`game.production.building_geometry` checks all six building kinds, live ownership, graph bounds, turret capacity, CPU ray picking and base-cell cleanup. Rejected asymmetric, edge and out-of-map base coordinates leave no registered terrain cells. The ray-distance check permits four float ULPs at 40 world units because triangle intersection arithmetic introduces rounding; it is a fixture geometry check, not a relaxed gameplay trajectory comparison.

`game.production.queue_deployment` follows production queue ownership into `RobotSpawn` at the exact synthetic 100 ms construction boundary. It asserts queue order, robot/base states, build statistics, one RNG draw and unchanged balances. The enqueue helper retains ownership if the queue rejects an item and transfers ownership immediately after successful insertion. This component test closes the base explicitly to isolate the second queue handoff; it does not advance the map clock.

`game.production.base_exit` runs the complete ordinary map boundary at 10 ms steps. The base at (100,100) has angle 0 and the robot starts facing (0,1,0), following the constructor's angle-to-forward mapping. The test observes and asserts each first transition:

| Transition | Map time |
| --- | --- |
| Queue completion and opening starts | 100 ms |
| Base reaches `BASE_OPENED` | 1,340 ms |
| Robot enters `ROBOT_BASE_MOVEOUT` | 1,340 ms |
| Robot detaches; manual control returns and closing starts | 3,590 ms |
| Base reaches `BASE_CLOSED` with floor zero | 4,840 ms |

The robot must be at least `BASE_DIST` (70 world units) from the base when it detaches. After closing, explicit production deletion must clear all nine registered terrain cells. The timing reference describes this fixed synthetic world, including its nearby robots; it does not claim identical travel time for other maps or spawn positions.

Both deployment cases use seed 83 in Park-Miller and LegacyCRT modes with audio disabled. Their asserted lifecycle outcomes are mode-independent. Queue completion consumes one draw to choose a build-end sound; audible output and the primary audio-available battle profile are not covered here. The existing battle fixture supplies a non-rendering device; no native window or private resource package is needed.

Setup differs from resource-backed construction in several deliberate ways. The robot is already assembled and contains only a chassis; constructor UI, payment and module assembly remain outside scope. Hit points come from the helper parameter, not `g_Config.m_BuildingHitPoints`. The base starts closed with floor zero rather than closing from 0.2. `m_TopPoint` is not supplied from a packaged building model; the exit scenario sets the base income interval to 100,000 ms to avoid the income billboard path throughout its shorter run. These fixtures must not be used to claim complete building presentation or income coverage.

Run these contracts with `tools/test.ps1 -Configuration Debug -Filter '^game\.production\.(building_geometry|queue_deployment|base_exit)$'`, then repeat with `Release`. They contribute to construction/deployment evidence but do not complete the behavior baseline.
