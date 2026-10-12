# Responsibility and ownership boundaries for refactoring (#91)

Source revision: `origin/dev` 540251d. All references are repository-relative `path:line` at that revision. This is an investigation; no tracked file was changed and nothing was built or run.

## 1. Summary

The engine has one implicit session made of 14 named globals, about 63 static class members and 17 `StaticInit` resets. Battle state, presentation and input meet inside a few very large classes. `CMatrixSideUnit` (`MatrixGame/src/MatrixSide.cpp`, 10,895 lines) holds input handlers, visual selection, player-group intent, order execution, computer strategy, economy, statistics and manual control. `CMatrixRobotAI` mixes rules, the order stack, navigation state and presentation calls. `g_MatrixMap` appears 2,257 times in `MatrixGame/src`.

The most important finding is that presentation is part of the simulation contract, not only coupled to it. Rendering and audio draw from the shared battle RNG. Some of those draws depend on whether audio is available, on the camera, or on the frame rate. A visual effect gates a damage rule, and a cap shared by gameplay and visual effects can refuse projectiles. Hit tests use animated meshes, and visibility advances those animations. "Separate simulation from presentation" therefore cannot mean "stop calling presentation from simulation" without a gameplay decision. Section 9 lists these unavoidable decisions.

Recommendation: an incremental, adapter-first migration that keeps call order and RNG consumption exactly as they are.

1. A concrete `Session` owner that keeps the globals as aliases while callers migrate.
2. An RAII `EntityRef` built on the existing `SObjectCore` weak-reference block, plus a 32-bit session entity id.
3. One player-command front door that takes a value-type intent and an explicit command source. It replaces the global sound-suppression flag and keeps today's synchronous execution.
4. Rule extraction only along existing statement boundaries, using the existing explicit-input-plus-legacy-overload pattern. All UI and AI callers route through the extracted rule.
5. Simulation/presentation work in three steps: classify and measure first, then move only RNG-neutral device calls, then add one optional observer for the test harness and automation.

No event bus, service locator or per-subsystem virtual interface is proposed. The plan refines leaves #92 to #98 into 19 bounded siblings (section 8).

## 2. Current state map

### 2.1 Session objects, owners and lifetimes

| State | Created | Destroyed | Owner / notes |
| --- | --- | --- | --- |
| `g_MatrixHeap` | `MatrixGame/src/MatrixGame.cpp:213` | `MatrixGame.cpp:824-827` | Helper object only; storage is CRT `calloc`. `HNew` zeroing is a contract (`docs/ARCHITECTURE.md:50`) |
| Package collection | `MatrixGame.cpp:215-222` | `MatrixGame.cpp:822` | `CFile` statics; must close after audio |
| `g_LoadProgress` | `MatrixGame.cpp:224-225` | never reset | Points to a stack object of `CGame::Init`, so it dangles after Init returns. Restart avoids it only through `skip_progress` (`MatrixMapPrepare.cpp:1637`, `:1785`) |
| `g_MatrixData` | `MatrixGame.cpp:247` | `MatrixGame.cpp:729-732` | Configuration tree; `g_Config` reads it |
| Cache, device, window | `MatrixGame.cpp:314`, `:332-349` | `MatrixGame.cpp:151-153` (via `Session::cleanup_standalone`) | Owned in the EXE; device and window borrowed from the host in the DLL (`docs/LIFECYCLE.md`) |
| `g_Render` | `MatrixGame.cpp:352` | `MatrixGame.cpp:715-718` | |
| `g_Config` | static; filled at `MatrixGame.cpp:359-362` | `MatrixGame.cpp:713`, `:803-817` | Mixes rules (damage, prices, timings: `MatrixGame/src/MatrixConfig.hpp:552-619`), presentation (shadows, gamma, sky, cursors) and input bindings |
| `g_MatrixMap` | `MatrixGame.cpp:372` | `MatrixGame.cpp:722-727` | Owns entities (`MatrixMap.hpp:270`, `:554-559`), effects, sides (`MatrixMap.hpp:337-339`), navigation (`m_RN`, `MatrixLogic.hpp`), camera, cursor, minimap, console and debug overlay |
| `g_IFaceList`, menus, popups, history | `MatrixGame.cpp:376`, `:444-528` | `MatrixGame.cpp:736-801` | Must outlive the map, because object teardown calls the UI (`MatrixRobot.cpp:5189-5196`) |
| Labels and descriptions | inside `g_Config.ReadParams` | `MatrixGame.cpp:803-817` | Destroyed by hand-written `~wstring` loops |
| Standalone audio | `MatrixGame.cpp:364` | `MatrixGame.cpp:821` | `SoundBridge`; the host path uses `g_RangersInterface` |
| `g_RangersInterface` | host `Init` (`MatrixGameDll.cpp:91-93`) | host `Deinit` (`MatrixGameDll.cpp:95-97`) | Borrowed; lives across `Run` |
| Game form | `MatrixGame.cpp:79` (`HNew`); `MatrixGameDll.cpp:133` (stack) | `MatrixGame.cpp:148-149`; scope exit | Different allocation in EXE and DLL |
| Static class state | `static_init`, `MatrixGame.cpp:161-193` | various `ClearAll`/`Clear` | 17 classes, about 63 `static m_*` members in headers (logic list `MatrixMapStatic.hpp:226-234`, `:264-265`; effects; selection; skins) |

Teardown is not the reverse of startup. `CGame::Deinit` (`MatrixGame.cpp:708-828`) deletes the map before `g_MatrixData` and the UI list, after `CMatrixHint::ClearAll`, and it is not protected stage by stage: an exception stops the rest of `Deinit`. Only the outer standalone sequence (`MatrixGame.cpp:145-156`) and `SafeFree` (`MatrixGame.cpp:857-899`) continue after a failure. Entity destructors dereference the map and the player side (`MatrixMapStatic.cpp:67-81`), so entities must die before `ClearSide` (`MatrixMap.cpp:163` before `:190`).

### 2.2 Startup flow

1. `WinMain` (`MatrixGame.cpp:48-143`) seeds the RNG from wall-clock time (`:75`), then calls `CGame::Init`.
2. `CGame::Init` sets the random mode (`:203-211`) before the map constructor's first draw (`MatrixLogic.cpp:50`).
3. Packages, configuration, cache and 3G/device are created, then settings (`:359-367`).
4. The map is created, with sides, robot meshes and the map (`:372-411`), followed by AI types (`:423`) and interfaces (`:453-528`).
5. Host only: `m_Begin` (`:536-540`).
6. Effects and default-pool resources are created (`:549-551`) and the dialog is entered (`:556-557`).
7. Back in `WinMain`, the form is created and `L3GRun` starts.

The DLL `Run` (`MatrixGameDll.cpp:128-150`) follows the same path, with a stack form and host-supplied settings.

### 2.3 Player order flow (right-click attack)

1. `L3GRun` dispatches messages (`MatrixLib/3G/3g.cpp:362-365`) to `CFormMatrixGame::MouseKey` (`MatrixFormGame.cpp:519`), then to `CMatrixSideUnit::OnRButtonDown` (`MatrixFormGame.cpp:690`).
2. The target is the cursor trace computed in the previous frame's `BeforeDraw` (`MatrixMap.cpp:1120-1122`), read at `MatrixSide.cpp:838-843`. UI focus and minimap state can override it (`MatrixSide.cpp:846-857`).
3. The order kind is chosen from the target class (`MatrixSide.cpp:862-891`). For move orders, flyers receive targets randomized from the shared RNG at input time (`MatrixSide.cpp:883-886`).
4. `PGOrderAttack` (`MatrixSide.cpp:9182-9261`):
   - Plays an acknowledgement sound, with `IRND(2)` only for a repeated order and only when the global flag `MMFLAG_SOUND_ORDER_ATTACK_DISABLE` is clear (`:9186-9197`).
   - Writes intent into `m_PlayerGroup[no]` and resets execution state on every robot in the group (`:9199-9216`).
   - Sets robot environment targets (`:9235-9252`).
   - Forces `TaktPL()` synchronously inside message dispatch (`:9256-9257`).
   - Creates the place-marker effects (`PGShowPlace`, `:9614`).
5. Simulation code calls the same entry point: internal callers raise the sound flag around the call (`MatrixRobot.cpp:534-540`, `MatrixObject.cpp:1333-1335`, `MatrixRobot.cpp:2184-2195`).
6. Execution runs in `TaktPL` (`MatrixSide.cpp:6872`), throttled to every 100 ms (`:6883-6886`). It validates targets by scanning the logic list for pointer identity (`:6975-6990`), then drives each robot's `SOrder` stack (`MatrixRobot.hpp:173-234`) and navigation (`MatrixLogic.hpp:118-198`, robot zone/path fields `MatrixRobot.hpp:261-276`).
7. `PGOrderMoveTo` chooses its acknowledgement sound from the current visual selection, not from logic group `no` (`MatrixSide.cpp:9051-9062`).
8. Manual control is not an order. The robot's `LogicTakt` reads held-key state and the cursor trace directly (`MatrixRobot.cpp:929-945`). The arcaded robot is skipped in `ProceedLogic` (`MatrixMapStatic.cpp:301`) and stepped once with the whole variable step (`MatrixLogic.cpp:3013-3020`).

### 2.4 Simulation tick (`CMatrixMapLogic::Takt`, `MatrixLogic.cpp:2767-3190`)

The per-frame order inside the tick is:

1. Host `m_Takt` and music volume (`:2770-2789`).
2. Statistics dialog text written into `g_MatrixData` (`:2815-2872`).
3. Selection time, console and pause branch (`:2876-2925`).
4. `m_Time += step` (`:2933`).
5. UI logic tick (`:2976`).
6. `GatherInfo` every 100 ms (`:2980-2985`).
7. `ProceedLogic` in 10 ms portions plus the remainder (`:2988-3000`).
8. Fixed 10 ms side ticks to catch up with game time (`:3003-3010`).
9. The arcaded object (`:3013-3020`).
10. `CMatrixMap::Takt` with the whole float step (`:3024`). This runs object animation for objects visible in the last frame (`MatrixMap.cpp:2421`, `MatrixMapStatic.cpp:719-729`), visible-group graphics (`MatrixMap.cpp:2425-2429`), skins, water, cursor, effects including projectiles and area damage (`MatrixMap.cpp:2448-2468`), minimap and sound (`MatrixMap.cpp:2493`).
11. The camera (`:3028`).
12. Once per second, a device call `g_D3DD->SetMaterial` (`:3072`) and the win/lose checks (`:3075-3180`).

Side ticks (`MatrixSide.cpp:434-616`) also do these:

- Write debug-overlay text (`:437-444`).
- Read the wall clock for `STAT_TIME` (`:447-451`), which also feeds the host result (`MatrixGame.cpp:848-855`).
- Update the drag selection from the cursor (`:482-491`).
- Change cursor visibility (`:533-545`).
- Trace from the camera frustum for turret placement (`:554-556`).

### 2.5 Damage flow (robot)

A weapon effect fires (`Effects/MatrixEffectWeapon.cpp:267-345`). Its trace uses mesh picking on the current animation frame (`MatrixObjectRobot.cpp:918-929`, `MatrixLib/3G/VectorObject.hpp:645`). A hit calls `CMatrixRobotAI::Damage` (`MatrixRobot.cpp:1820`), which interleaves:

1. Difficulty and rules from `g_Config` (`:1840-1855`).
2. AI target memory (`:1871-1882`).
3. `SoundHit` (`:1884`).
4. Hit points and the UI progress bar (`:1886-1898`).
5. Minimap flash (`:1900`).
6. Flycam war pair (`:1902-1905`).
7. Ablaze or shorted status (`:1908-1935`).
8. A hit explosion, which consumes RNG (`:1940-1945`).
9. On death: statistics, the BigBoom proximity scan over all objects (`:1963-1993`), `ReleaseMe` (`:2003`), explosion and crater (RNG), `cstay = FRND` (`:2029`) and debris velocities from the RNG (`:2044-2060`).

Ablaze damage is applied only when a visual "shleif" effect exists (`MatrixRobot.cpp:404-438`). That effect can be refused by the shared effect cap (`Effects/MatrixEffect.cpp:940-954`, `MatrixMap.hpp:764-791`).

### 2.6 Destruction flow

Death sets `ROBOT_DIP`. `ReleaseMe` (`MatrixRobot.cpp:5127-5237`) fans out by hand to the selection, the current group, UI progress bars and icons, every robot's environment list (an O(n) loop, `:5199-5205`), textures, the zone path and the arcade state. `DIPTakt` later calls `StaticDelete` (`MatrixRobot.cpp:280`). Capture deletes immediately without a DIP state (`MatrixRobot.cpp:1296`).

`StaticDelete` (`MatrixMap.cpp:832-893`) does the following:

1. Sets the DIP flag.
2. Patches `m_NextLogicObject`, which is what makes deletion during iteration safe (`MatrixMapStatic.cpp:299-311`).
3. Unlinks the object from the logic list and from `m_AllObjects` by swapping with the last element.
4. Deletes it immediately by concrete type; `~CMatrixMapStatic` is not virtual.
5. The destructor clears the trace-stop, arcade and always-draw pointers and nulls `SObjectCore::m_Object` (`MatrixMapStatic.cpp:67-81`).

Two reference mechanisms coexist:

- Weak references through `SObjectCore`, a ref-counted block that observes destruction (`MatrixMapStatic.hpp:134-191`). It has 19 call sites, among them orders (`MatrixRobot.hpp:199-207`), weapon owners (`MatrixEffectWeapon.cpp:838-846`), cannon targets, camera and missiles.
- Raw pointers invalidated by fan-out or by scans: `CInfo` targets and enemy lists (`Logic/MatrixEnvironment.h`, removal at `Logic/MatrixEnvironment.cpp:107-118`), `SMatrixPlayerGroup::m_Obj` (`MatrixSide.hpp:235`), `CMatrixGroupObject::m_Object` (`Logic/MatrixAIGroup.h:39`) and selection.

### 2.7 Rendered frame

`L3GRun` (`3g.cpp:343-420`) uses a variable step capped at 100 ms (`:386`). `CFormMatrixGame::Takt` (`MatrixFormGame.cpp:342-449`) handles pending display changes, lost devices, the pending selection, `g_MatrixMap->Takt` and camera scrolling. `CFormMatrixGame::Draw` (`MatrixFormGame.cpp:240-338`) then runs:

1. `BeforeDraw` (`MatrixMap.cpp:1058`): frame counter, camera, pick vector and cursor trace with `TRACE_ALL` (`:1120`). It recomputes matrices lazily through `RNeed` for visible objects (`MatrixObjectRobot.cpp:947-956`).
2. The effects' `BeforeDraw`. It consumes the RNG: the laser draws `FRND` at `Effects/MatrixEffectWeapon.cpp:795-800`.
3. `Draw`. It also consumes the RNG: the volcano draws `IRND(3)` at `Effects/MatrixEffectWeapon.cpp:824-835`.
4. `Present`.

The visibility computed by `BeforeDraw` selects which objects animate in the next frame's tick. The camera and frame rate therefore decide how many RNG draws happen and which hit geometry is used.

## 3. Problems with evidence

Each item is labelled E (demonstrated from source), R (a risk derived from source and not measured), or O (opinion).

### Presentation consumes the battle RNG (E)

| Consumer | Location | When it draws |
| --- | --- | --- |
| `CSound::Play` | `MatrixSoundManager.cpp:766-772` | Two draws, only when `SoundBridge::available()` (`SoundBridge.cpp:63`) |
| Laser `BeforeDraw` | `MatrixEffectWeapon.cpp:797` | Every rendered frame |
| Volcano `Draw` | `MatrixEffectWeapon.cpp:828` | Every rendered frame |
| Inshore waves | `MatrixMapGroup.cpp:703` | For visible groups |
| Hovercraft dust | `MatrixObjectRobot.cpp:880-895`, `Effects/MatrixEffectDust.cpp:28-34` | For visible robots |
| Water | `MatrixWater.cpp:19-36` | |
| Flycam | `MatrixCamera.cpp:463`, `:478` | In flycam mode |

The docs already warn about a shared stream (`docs/RANDOM.md:40`, `docs/TESTING.md:112`). They do not state that audio availability and rendering change the number of draws.

### Presentation state gates rules (E)

- Ablaze damage requires the visual effect (`MatrixRobot.cpp:409-438`).
- The 1,280-effect cap (`Effects/MatrixEffect.hpp:8`) is shared by projectiles (priority `MAX_EFFECT_PRIORITY`) and visual effects. When no evictable effect exists, a new projectile is released and refused (`MatrixMap.hpp:774-777`).
- `PGOrderAttack` draws from the RNG depending on a global presentation flag (`MatrixSide.cpp:9186-9189`).

### Simulation reads render-derived geometry (E/R)

- Mesh picks use animation frames (E: `MatrixObjectRobot.cpp:918-929`).
- Non-pneumatic chassis and other units animate only in the graphics tick of visible objects (E: `MatrixObjectRobot.cpp:743-758`, `:900`; `MatrixMapStatic.cpp:719-729`).
- Matrices refresh lazily on draw, on `CalcBounds` (with a 100 ms cache, `MatrixObjectRobot.cpp:1140-1154`) and for the arcaded robot (`MatrixLogic.cpp:3018`).
- Whether the camera changes battle outcomes is R and needs the #76 experiments.

### Simulation calls UI, audio and device directly (E)

Within `MatrixSide.cpp` there are 63 `g_IFaceList` and 52 `CSound::` uses. `MatrixObjectBuilding.cpp` has 26 `CSound::` and 18 effect creations. `MatrixObjectCannon.cpp` has 9 `g_D3DD` uses. `MatrixLogic.cpp:3072` sets material state in the tick, and `Damage` updates progress bars. These block device-free tests only where a service is absent. Several call sites already tolerate absence (`MatrixRobot.cpp:5154`, `:5189`). The tests work around the rest by substituting objects at link time (`tests/diagnostic_overlay_stub.cpp`).

### Input and selection state feed rules (E)

- Order targets come from the last frame's cursor trace (`MatrixSide.cpp:838-843`).
- Preorder mode is UI list state (`Interface/CInterface.h:80`).
- The move acknowledgement depends on the visual selection (`MatrixSide.cpp:9060-9062`).
- The turret preview runs in the simulation tick using the camera (`MatrixSide.cpp:517-588`).

These prevent an external command from being equivalent to a human command without the picking state.

### References are mixed and depend on convention (E/R)

- Ref counting on `SObjectCore` is manual.
- `SOrder` is moved by `MoveMemory`, `memcpy` and `memset` without ref adjustment (`MatrixRobot.cpp:4675`, `:4697`; `MatrixRobot.hpp:223`). The copy at `MatrixRobot.cpp:4715` is shallow.
- `RemoveOrder` calls `memcpy` on overlapping ranges, which is undefined behavior (E: `MatrixRobot.cpp:4697`).
- Player-group targets are validated by pointer identity. A target deleted immediately (capture, `MatrixRobot.cpp:1296`) and an allocation at the reused address before the next scan would retarget the order (R, not demonstrated).
- `CInfo::RemoveFromList` leaves `m_TargetLast` and `m_Ignore[]` in place (R: `Logic/MatrixEnvironment.cpp:107-118`).
- Entity identity is fragmented. Robots have a process-static `size_t UID` used only for relative ordering (`MatrixRobot.hpp:315-317`, `MatrixRobot.cpp:2942`, `:3044`). Map objects have a map-file `m_UID` (`MatrixMapPrepare.cpp:1729`). Other entities have none.

### Session ownership is implicit (E)

- Owned and borrowed objects are released by an order written out by hand (section 2.1).
- `g_LoadProgress` dangles after Init.
- The heap-zeroing dependency is inconsistent: one fixture uses `std::make_unique<CMatrixMapLogic>()` (`tests/command_world.hpp:24`), against `docs/ARCHITECTURE.md:50` and the `HNew` fixture in `tests/bomb_order_tests.cpp:32-35`.
- The DLL builds the form on the stack (`MatrixGameDll.cpp:133`), while the EXE allocates it with `HNew` (`MatrixGame.cpp:79`).

### Domain rules are duplicated or live in UI code (E)

Robot pricing is computed three times:

- The UI price, `CConstructor::GetConstructionPrice` (`Interface/CConstructor.cpp:891`), deducted through the UI counter widget (`:237-240`).
- The AI type price, accumulated at load (`Interface/AIRobotTypes.cpp:112-239`) and deducted with a different expression (`MatrixSide.cpp:6548`, `:6721`).
- The refund, `CBuildStack::ReturnRobotResources` (`MatrixObjectBuilding.cpp:1907-1941`).

`ProduceRobot` makes a draw whose value is immediately overwritten (`Interface/CConstructor.cpp:121-123`). That draw must be preserved.

### Floating-point evaluation depends on build configuration (E)

The pinned GCC 13.2.0 i686 defaults to `-march=pentiumpro -mfpmath=387` with SSE2 disabled, and `CMakeLists.txt:56-79` does not override it. MSVC x86 defaults to SSE2. Release uses `-Os`. Extracting or inlining float code can change x87 intermediate precision, so "pure rule extraction" is not automatically bit-identical in MinGW builds.

### Opinions (O)

- The size of `CMatrixSideUnit` and `CMatrixRobotAI` raises review cost, but splitting files without explicit inputs does not improve testability.
- `CMatrixMapStatic`'s type tags and C-style `AsRobot()` casts are acceptable as they are.

## 4. Decision record

### 4.1 Rejected abstractions

These decisions apply throughout and follow the acceptance criterion that no abstraction exists only for principle compliance.

| Rejected | Reason |
| --- | --- |
| General event bus or deferred events | They reorder side effects and RNG draws (section 3) |
| Service locator or all-purpose context | #92 explicitly rejects a replacement all-purpose global; a context hides dependencies just as a global does |
| `ISound`, `IRenderer`, `INavigation` interfaces | No second production implementation exists. Sound already has a selection point (`SoundBridge`), and test substitution already works through link-time stubs and null tolerance |
| New parallel rules engine, ECS rewrite | Cannot be reviewed for equivalence |

An abstraction is admitted only with a named second consumer: the test harness (#77, #79), automation (#99, #100), the host versus standalone split, or save/load (#67).

### 4.2 D1: Session ownership (#92)

- **Responsibility:** one concrete `Session` owns what `CGame::Init` creates and records what is borrowed: the host callbacks, and the host device and window. It extends the existing `namespace Session` (`MatrixGame/src/SessionCleanup.hpp`).
- **Dependency direction:** entry point → `Session` → subsystems. Subsystems never receive a `Session&`. They receive the narrowest object they need, such as `CMatrixMapLogic&`, a rules table or a span.
- **Lifetime:** members are `std::unique_ptr` with the engine-heap deleter already used in tests (`tests/bomb_order_tests.cpp:26-35`), so `HNew` zeroing is kept. `Session::teardown()` reproduces `CGame::Deinit` exactly (`MatrixGame.cpp:711-827`). Stage-by-stage exception tolerance inside Deinit is a separate, explicitly reviewed failure-path change.
- **Migration adapter:** the globals stay as non-owning aliases, assigned and cleared in one place by the session. This matches how fixtures already rebind `g_MatrixHeap`, `g_MatrixData` and `g_MatrixMap` (`tests/command_world.hpp:19-26`).
- **Static class state:** first inventory it, then move it cluster by cluster with an explicit reset owner.
- **Repeated full initialization (#66):** must not be precluded, but is not promised.

### 4.3 D2: Entity references and identity (#93)

- **Responsibility:** `EntityRef` is a 4-byte RAII handle over `SObjectCore`. Copying it increments the reference count, destroying it decrements it, and `get()` returns `m_Object` or null. It reuses the existing destruction observation, so it needs no registry lookup.
- **Identity:** `EntityId` is a 32-bit, session-unique, never-reused serial stored in `SObjectCore` at `Create` (`MatrixMapStatic.hpp:149-168`). An id-to-core index is added only when #99 or #67 needs lookup.
- **Robot UID:** robot `UID` comparisons depend only on relative creation order. Unifying them under one per-session counter keeps that order, but the numeric values change (U9).
- **Destruction:** stays immediate. `StaticDelete` semantics and the `m_NextLogicObject` patch are unchanged; no graveyard is introduced.
- **Fan-out:** `ReleaseMe` keeps its side effects, such as `PLDropAllActions` and UI regeneration. Fan-out loops are removed only after their consumers hold `EntityRef` and equivalence is shown.
- **Prerequisite:** nontrivial `SOrder` contents require replacing the `memset`, `memcpy` and `MoveMemory` order-stack code with element moves. The overlapping-`memcpy` undefined behavior is a separate fix with its own regression.

### 4.4 D3: Domain values and rules (#94)

- **Data:** split `CMatrixConfig` into a rules table (damage, hit points, prices, timings, capture, overheat) and presentation and input settings. `g_Config` stays as the aggregate, so callers change mechanically. This is an in-memory layout only, not an ABI.
- **Functions:** rules become free functions with explicit inputs, following the existing pattern in `SSpecialBot::LoadAIRobotType(bp, span)` (`Interface/AIRobotTypes.cpp:51`) with its legacy overload adapter (`Interface/CConstructor.cpp:1414-1419`).
- **Extraction limits:** only RNG-free computations at existing statement boundaries, for example the hit-point delta. Callers keep every side effect and RNG draw in its original position.
- **Pricing:** one production price function serves the UI, the AI loader and the refund. The UI and AI deduction paths call one transaction function that keeps today's clamping.
- **Floating point:** float rules must be shown bit-identical per supported configuration, because of x87 in MinGW.

### 4.5 D4: Intent, execution and navigation (#95)

- **Intent:** `PlayerCommand` is a value type holding the kind (the existing `EMatrixPlayerOrder`), the logic group, a map cell, an optional `EntityRef` target and a `CommandSource` (PlayerInput, Internal or Automation). `IssuePlayerCommand(side, cmd)` returns accepted or rejected with a reason. The existing `PGOrder*` bodies remain the implementation. The source replaces the global sound-suppression flag one-for-one, so the conditional `IRND(2)` and sound draws are unchanged.
- **Execution:** `SMatrixPlayerGroup` is split into intent members (order, `m_To`, target) and execution members (region path, road path, war, show-place and patrol flags) within the same struct. Synchronous `TaktPL` on issue is kept.
- **Target resolution:** picking stays in the input layer. A command carries a resolved target, which is what automation needs.
- **Navigation:** it stays on `CMatrixMapLogic`. Route functions already take explicit parameters (`MatrixLogic.hpp:118-153`). Only global access is removed. A separate navigation class is deferred until #81 coverage exists.
- **Diagnostics:** decision reasons go through the optional observer (D5), which costs nothing when it is null.

### 4.6 D5: Simulation and presentation (#96)

- **Stage A:** classify every effect type as gameplay-bearing or visual, and every RNG call site as simulation or presentation. Count draws per category in a test-only build (#76). No production change.
- **Stage B:** remove only RNG-neutral presentation work from the tick. Examples are `SetMaterial` (`MatrixLogic.cpp:3072`), cursor visibility and the turret preview (`MatrixSide.cpp:517-588`, after verifying that its trace consumes no RNG), and debug-overlay text. Each move needs a native presentation check.
- **Stage C:** add one optional observer pointer owned by the session. It is called at existing points: command accepted or rejected, damage applied, entity died, capture changed and outcome reached. It is non-virtual unless two implementations exist, and its consumers are the harness and automation.
- **Device-free scenarios:** they run production effects with unloaded cache entries, as the bomb fixture shows (`tests/bomb_order_tests.cpp:110-113`). They install a fake audio backend so that `available()` matches the reference configuration (U1). Render-path draws remain a declared difference until U2 is decided.

### 4.7 SOLID/GRASP evaluation against outcomes

| Principle | Applied where | Outcome | Rejected where |
| --- | --- | --- | --- |
| SRP / High cohesion | Command front door, pricing rule, rules table, session owner | Each enables a named test (#80, #83, #85) and removes duplication | Splitting `CMatrixSideUnit` computer strategy into classes: no explicit inputs, so no test gain. A file split is at most a #97 mechanical option |
| OCP | none | | Plug-in points for effects or orders: no new variants are planned |
| LSP | Keep concrete-type destruction | | Adding a virtual destructor to `CMatrixMapStatic` solves nothing today |
| ISP | Spans and narrow references instead of map access (existing `std::span<const SRobotWeaponMatrix>`) | Already enabled `tests/ai_robot_tests.cpp` without a map | Fat "game services" interfaces |
| DIP | Only at the host/standalone audio boundary (existing) and the observer (two consumers) | | Inverting map, navigation or rendering |
| GRASP Information Expert | Prices belong with rules data, not the UI counter (`CConstructor.cpp:237-240`) | | |
| GRASP Controller | `IssuePlayerCommand` | One validated entry point for UI, keys, cheats, internal callers and automation (callers in section 2.3) | |
| GRASP Pure Fabrication | `Session`, `EntityRef` | Explicit lifetime; replaces manual reference counting | Registry or slot map until a lookup consumer exists |
| GRASP Protected Variations | Host ABI, RNG stream order | Both are frozen contracts | |

## 5. Path walkthroughs through the proposed design

### Startup

1. `WinMain` or `Run` constructs a `Session` with a standalone or host-borrow description.
2. The steps of `MatrixGame.cpp:199-557` run in the same order. Each created object is placed in its session member, and the alias global is set immediately after creation, as today.
3. The RNG seed and mode come before the map constructor (`MatrixLogic.cpp:50`), so the initial draw is unchanged.
4. On failure, `Session::teardown()` releases exactly the members that were set.

Difference from today: none observable. `g_LoadProgress` is cleared when Init returns (F3).

### Order

1. Input resolves the target from `m_TraceStopObj` and `m_TraceStopPos`, as today.
2. Input builds `PlayerCommand{mpo_Attack, SelGroupToLogicGroup(), cell, EntityRef(target), PlayerInput}`.
3. `IssuePlayerCommand` checks `m_RobotCnt > 0` (rejected otherwise), then runs the existing `PGOrderAttack` body with the sound condition `source == PlayerInput`, and calls `TaktPL` synchronously.
4. The observer receives "accepted".
5. Internal callers pass `Internal` instead of toggling the flag.
6. `TaktPL` validates the target with `EntityRef::get() && IsLive()` instead of the scan. This is identical except in the address-reuse case (U3).

RNG draws stay identical in count and order.

### Damage

`CMatrixRobotAI::Damage` calls a pure `ComputeRobotDamage(rules, difficulty, weapon, relation, hp, protection)` at the point where `damage` is computed today (`MatrixRobot.cpp:1887-1892`). `SoundHit` (`:1884`) still precedes it. Effects, statistics, the death branch and its RNG draws stay inline and in order. The observer receives "damaged" or "died" after the hit points change, with no RNG. Ablaze gating by the visual effect is unchanged (U5).

### Destruction

`ReleaseMe` and `StaticDelete` are unchanged. `EntityRef` holders observe `m_Object == nullptr` after the destructor (`MatrixMapStatic.cpp:79`). Raw-pointer holders are migrated one by one (#93 splits), and the fan-out code is kept until each migration proves equivalence.

### Rendered frame

The order is unchanged. Stage B moves `SetMaterial` from the once-per-second logic block into the form's draw path before `BeginScene`, keeping its rate. Effect `BeforeDraw` and `Draw` RNG draws stay where they are until U2 is decided. Visibility still drives animation in the next tick.

## 6. Per-frame and per-unit cost

| Path | Today | Proposed | Expected cost |
| --- | --- | --- | --- |
| `TaktPL` target validation (`MatrixSide.cpp:6975-6990`) | O(groups × logic objects) every 100 ms | One `SObjectCore` dereference per group | Lower; measure with #78 |
| `SOrder` stack shifts (`MatrixRobot.cpp:4675`, `:4697`) | `memmove`/`memcpy` of at most 5 elements | Element moves with ref adjustment | A few increments per order change; negligible |
| `EntityRef` copies in loops | n/a | Pass by `const&`; no copies in per-object loops | None if reviewed |
| Session alias versus member access | Global load | Global alias kept; later a reference parameter | Unchanged |
| Observer | n/a | One null check per event (per hit or death, not per object per tick) | Negligible |
| `AddEffect` at the cap (`MatrixMap.hpp:764-785`) | O(effects) scan per add | Unchanged (behavior coupling, U5) | n/a |
| Side robot count (`MatrixSide.cpp:594-604`) | O(objects) per 10 ms per side | Unchanged; a #98 candidate | Measure first |

No virtual dispatch, `std::function`, heap allocation or container growth is added on per-object, per-tick or per-route paths. #98 candidates come only from the measured list.

## 7. Constraints

### 7.1 Host ABI (unchanged by every proposal)

- `SMGDRobotInterface` (`MatrixGameDll.hpp:96-101`): its layout is asserted (`tests/dll_entry_tests.cpp:116-117`).
- `SMGDRangersInterface` (`:104-128`): host callbacks are invoked from the simulation tick (`MatrixLogic.cpp:2771`) and from UI text rendering (`Interface/CInterface.cpp:5315`, `Interface/MatrixHint.cpp:715`). Its signature passes `Base::CRect*`, so the `CRect` layout (`MatrixLib/Base/BaseDef.hpp:57`) is host ABI.
- `SRobotsSettings` (`:19-81`): it holds pointers in `long` fields (`:20-21`).
- `SRobotGameState` (`:83-90`), filled from `STAT_*` (`MatrixGame.cpp:848-855`).
- Exit-code semantics (`MatrixGameDll.hpp:151-160`).

Only `SMGDRobotInterface` is frozen today. Freezing the rest belongs to #86 and must precede #92 and #96 changes near the host layer.

### 7.2 RNG consumption

Every leaf must preserve the count and order of draws from `random::*`, `RND`, `FRND`, `FSRND` and `IRND`. This includes:

- The map constructor draw (`MatrixLogic.cpp:50`).
- The overwritten draw (`CConstructor.cpp:121`).
- The conditional acknowledgement draws (`MatrixSide.cpp:9053`, `:9189` and the other `IRND(2)` sites).
- The input-time flyer draws (`MatrixSide.cpp:707-710`, `:883-886`).
- Sound draws when audio is available.
- Render-path draws.

Evidence is a draw-count trace per call-site category under a fixed schedule (#76). Any change in RNG consumption becomes a separate maintainer decision.

### 7.3 x64 assumptions (open, for #102)

- X1: `EntityId` is a fixed 32-bit value on every architecture. The existing robot `UID` is `size_t` (`MatrixRobot.hpp:315-317`) and is used only for relative comparison.
- X2: `EntityRef` and `SObjectCore` are in-memory only. No persisted or host layout contains them. #102 should confirm `SObjectCore` is never written to disk.
- X3: callback payloads are `uintptr_t` (`MatrixMap.hpp:92-93`, `Effects/MatrixEffect.hpp` `FIRE_END_HANDLER` users), and nothing truncates them to `DWORD`. #102 should audit this.
- X4: the sentinel pointers `TRACE_STOP_*` and `IS_TRACE_STOP_OBJECT` (`MatrixMap.hpp:49-53`) stay valid on x64.
- X5: the host session layer is compiled only for the x86 DLL. x64 standalone never uses `SRobotsSettings`' `long` pointer fields.
- X6: `HNew` zeroing (calloc) is kept on x64; the session keeps the engine deleter.
- X7: x86 MinGW uses x87, while x64 uses SSE. Bit-exact equivalence is required only within one configuration; cross-architecture comparison uses #105 tolerances.
- X8: if #67 save/load or #99 use STRG storage, ids and references are serialized as fixed-width fields, not as native `CBuf` layouts (`docs/DATA_FORMATS.md:24`).
- X9: package `SFileRec` packing and pointer-width fields (`MatrixLib/Base/Pack.hpp:37-50`) are untouched by these boundaries.

### 7.4 Automation API needs (for #99)

| Need | Boundary that provides it | Notes |
| --- | --- | --- |
| Stable entity ids, invalidated on restart | `EntityId` in `SObjectCore`; id index added with #99 | Restart (`MatrixMapPrepare.cpp:1717`) invalidates every id |
| Commands through the production path | `IssuePlayerCommand` with `CommandSource::Automation` and an explicit target (no picking) | Result codes for rejection |
| Defined injection point | After message dispatch, before `CFormMatrixGame::Takt` (`3g.cpp:362-402`) | Human input runs inside dispatch, so sub-frame equivalence is a declared limit (U4) |
| Manual control | The existing input action state (`isKeyPressed`, polled at `MatrixRobot.cpp:929-945`), not new robot calls | |
| Events | The session observer: command accepted or rejected, order-phase change, damage, death, capture, build, outcome | |
| Observations | Read-only reads that never call caching mutators such as `CalcBounds` or `RNeed` | Reading must not change state |
| Player-visible versus privileged information | `IsLogicVisible` (`MatrixLogic.cpp:3433`) and minimap rules | To be measured by #99 |
| Bounded advancement | Depends on the #76 time and frame driver | The 100 ms cap and visibility-dependent work |

## 8. Refined implementation leaves

All of these are blocked by #69. Splits are proposed siblings under the same parent; none has been published.

### #92 Startup: session ownership

**92.1 Session owner for Init/Deinit objects**

- **Target:** move creation and destruction of the objects in section 2.1 into `Session` members, keep the globals as aliases, and reproduce the Deinit order exactly. Reader code is unchanged.
- **Compatibility risks:** teardown order; DLL stack-form lifetime; timer pairing (`MatrixGame.cpp:147`, `:859`); host-borrowed device and window.
- **Evidence:**
  - The lifecycle suite, extended with failure injection at each creation stage, checks that each owned object is released exactly once and that the Debug heap has no leaks.
  - The DLL host test (`tests/dll_host_tests.cpp`).
  - EXE and DLL builds with MinGW and MSVC, Debug and Release.
- **Done when:** `CGame::Init` and `Deinit` contain no direct `HNew`/`HDelete` of session objects; the `docs/LIFECYCLE.md` table matches the code; every stage has an injection case; there is no change in startup log order or exit codes.

**92.2 Static class-state inventory and reset ownership**

- **Target:** a table of every `StaticInit`/`Clear*` pair and every static member, with its owner and reset point. Move only clusters whose reset is currently split, one cluster per PR.
- **Compatibility risks:** reset timing relative to Restart.
- **Evidence:** a repeated in-process synthetic map construction, destruction and reconstruction test that asserts reset state, including the `ValidateAfterReset` checks (`MatrixMapStatic.hpp:314-323`).
- **Done when:** the inventory is complete, each moved cluster has a test, and Restart behavior is unchanged.

**92.3 Explicit map dependency for `Logic/` navigation and environment**

- **Target:** the 25 `g_MatrixMap` uses in `MatrixGame/src/Logic/*.cpp`, replaced by an explicit reference parameter through legacy overloads.
- **Compatibility risks:** performance on the route path.
- **Evidence:** #81 navigation scenarios and #78 crowded-path benchmark distributions.
- **Done when:** zero `g_MatrixMap` uses in `Logic/`, and benchmarks are within the reviewed noise.

### #93 Memory: references and destruction

**93.1 `EntityRef` and `EntityId` over `SObjectCore`, applied to its existing users**

- **Target:** the 19 `GetCore` sites and the `SOrder` stack. Replace `memset`, `memcpy` and `MoveMemory` with element operations.
- **Prerequisite:** the separate fix F1 for the overlapping `memcpy`.
- **Compatibility risks:** reference-count timing; `SOrder::GetStatic` lazy release semantics (`MatrixRobot.hpp:199-207`); Debug and trace build variants of `GetCore` (`MatrixMapStatic.hpp:428-442`).
- **Evidence:** order lifecycle tests (#80); Debug leak and refcount checks; the order-stack replacement cases.
- **Done when:** there is no manual `RefInc`/`Release` outside `EntityRef`, and no byte operation on types that contain one.

**93.2 Player-group targets**

- **Target:** `SMatrixPlayerGroup::m_Obj` becomes an `EntityRef`, and the pointer scan is removed (`MatrixSide.cpp:6975-6990`).
- **Compatibility risks:** the address-reuse semantics (U3).
- **Evidence:** destroyed, captured and deleted target cases through the real order path; a #78 measurement.
- **Done when:** the scan is gone, the baseline is unchanged, and U3 is decided.

**93.3 AI environment references**

- **Target:** `CInfo` targets, the enemy list and the ignore list.
- **Compatibility risks:** the "slow removal" counters (`Logic/MatrixEnvironment.cpp:126-145`) and the order of fan-out side effects.
- **Evidence:** #84 autonomous-decision scenarios.
- **Done when:** no raw `CMatrixMapStatic*` remains in `CInfo`, and `ReleaseMe` fan-out is either kept with a justification or shown to be equivalent.

**93.4 Group and selection object references**

- **Target:** `CMatrixGroupObject` and `CMultiSelection` items.
- **Evidence:** the selection suite (`tests/selection_tests.cpp`) and #80.
- **Done when:** destruction before refresh is covered with references.

**93.5 Byte-relocation audit**

- **Target:** the `HAllocEx` realloc users of class types, such as `SSpecialBot` (`Interface/AIRobotTypes.cpp:249`), `CEffectSpawner` (`MatrixMap.cpp:3029`) and `CBillboardLine` (`Effects/MatrixEffectLightening.cpp:74-77`). Add `static_assert(std::is_trivially_copyable_v<...>)` or convert to proper storage.
- **Overlap:** #88, #90 and #103.
- **Done when:** every realloc'd type is asserted trivially copyable or converted.

### #94 Gameplay: domain values and rules

**94.1 Rules table split out of `CMatrixConfig`**

- **Target:** a mechanical aggregate split (section 4.4).
- **Compatibility risks:** load order in `ReadParams`; the #60 settings work (merged as #62 after this revision) also touches `CMatrixConfig`.
- **Evidence:** the configuration tests and a `manual.storage.buildcfg` round trip.
- **Done when:** rules fields live in one struct readable without UI or presentation members.

**94.2 One robot pricing function and one resource transaction**

- **Target:** the UI price, the AI type price, the refund and both deduction paths (section 3).
- **Compatibility risks:** clamping differences between `AddResourceAmount` and `std::max`; the counter widget multiplying the cost.
- **Evidence:** #83 construction and resource scenarios, run through both the UI entry and the AI path, plus the existing AI tests.
- **Done when:** all five callers route through the production functions, and there is no test duplicate.

**94.3 Damage computation extraction**

- **Target:** the RNG-free hit-point delta in `CMatrixRobotAI::Damage`, then the cannon, building and flyer equivalents. Each is a separate PR.
- **Compatibility risks:** x87 precision; evaluation order.
- **Evidence:** #82 combat scenarios, bit-identical per configuration, with a negative control.
- **Done when:** each `Damage` calls the rule; draw counts are unchanged.

### #95 AI: intent, execution and navigation

**95.1 `PlayerCommand` front door and `CommandSource`**

- **Target:** every `PGOrder*` caller (section 2.3). Remove `MMFLAG_SOUND_ORDER_ATTACK_DISABLE` toggling.
- **Depends on:** 93.1 for the target reference.
- **Compatibility risks:** the conditional acknowledgement draws; synchronous `TaktPL`; selection-dependent sounds (U10).
- **Evidence:** #80 command scenarios that assert RNG draw counts; a cheats path test.
- **Done when:** every order enters through one function with accepted or rejected results, and the flag has no remaining writers.

**95.2 Intent and execution split inside `SMatrixPlayerGroup` and `SMatrixLogicGroup`**

- **Target:** a mechanical member regrouping.
- **Compatibility risks:** `memset`/copy users of these structs.
- **Evidence:** #80 and #84.
- **Done when:** intent fields are written only by the front door.

**95.3 Order lifecycle transitions and decision diagnostics through the observer**

- **Depends on:** 96.3.
- **Evidence:** a diagnostics-disabled benchmark equal to the baseline; transition sequences asserted in the #79 first scenario.
- **Done when:** accepted, rejected, completed and cancelled transitions are observable without a policy change.

Deferred: a separate navigation component, until #81 freezes behavior and a test needs grid-only construction.

### #96 Rendering: simulation results and presentation

**96.1 Classification and draw accounting**

- **Target:** a test-only RNG call-site tagging build and an effect-category table.
- **Evidence:** the #76 experiments.
- **Done when:** a published per-frame draw breakdown exists for one scenario, both with and without audio.

**96.2 RNG-neutral device and UI calls removed from the tick**

- **Target:** `MatrixLogic.cpp:3072`, `MatrixSide.cpp:437-444` and `:517-588` (after verification), and similar sites. Each is one PR.
- **Evidence:** draw counts unchanged, plus a native presentation check under `docs/PLAYTESTING.md`.
- **Done when:** the listed calls are out of the tick.

**96.3 Session observer**

- **Target:** one optional observer, with events emitted at the points listed in section 4.6.
- **Evidence:** a null-observer benchmark equal to the baseline; harness assertions.
- **Done when:** #79 and #100 consume it, and nothing changes when it is absent.

**96.4 Device-free effect construction**

- **Target:** effects constructible with unloaded cache entries for the supported paths, with the remaining dependencies listed.
- **Evidence:** a scenario run with a fake audio backend that matches the declared reference configuration (U1).
- **Done when:** the support matrix is documented.

### #97 Docs: terminology and scoped checks

**97.1 Glossary**

- The word "group" names four different things: map chunk (`CMatrixMapGroup`), visual selection (`CMatrixGroup`, `CMultiSelection`), logic group (`m_GroupLogic` with `m_PlayerGroup`/`m_LogicGroup`) and control group (`m_CtrlGroup`). Team, intent and execution need definitions too.
- **Done when:** the glossary is in `docs/ARCHITECTURE.md`, and comments in touched files use it.

**97.2 Scoped warnings**

- Re-enable `-Wclass-memaccess` and `-Wuninitialized` per migrated module (currently disabled at `CMakeLists.txt:70-79`).
- **Done when:** migrated modules build with them, and there is no new suppression.

### #98 Gameplay: measured optimizations

**98.1 Measured candidates only**

- **Candidates:** the side robot-count loop, `AddEffect` at the cap (behavior-bound, U5), and the `PGOrder*` group scans. The `TaktPL` scan is covered by 93.2.
- **Evidence:** #78 distributions and work counters before and after.
- **Done when:** each change has an accepted measured benefit and unchanged scenarios.

## 9. Unavoidable behavior decisions

| Id | Decision | Source | Recommendation |
| --- | --- | --- | --- |
| U1 | Reference configuration with or without audio. Draws differ (`MatrixSoundManager.cpp:766-772`) | | With audio available; the harness installs the fake device |
| U2 | Whether render and visibility-dependent draws stay in the shared stream | Section 3 | Keep for the baseline; propose a separate visual stream only with #76 evidence and a maintainer decision |
| U3 | Address-reuse retargeting of stale player-group targets | `MatrixSide.cpp:6975-6990` | Treat as a defect if it is ever demonstrated; references then fix it as a separate evidenced change |
| U4 | Automation commands: injection point, and whether they play acknowledgements and draw | | Inject at frame start; play acknowledgements as player input for human equivalence |
| U5 | The effect cap shared between projectiles and visuals, and the ablaze gate | `MatrixMap.hpp:764-791`, `MatrixRobot.cpp:409-438` | Preserve; any separation is a gameplay change |
| U6 | `STAT_TIME` from the wall clock, including pause | `MatrixSide.cpp:447-451`, `MatrixMapPrepare.cpp:1759` | Preserve; inject a clock source only for tests |
| U7 | Bit-exact equivalence per configuration under x87 | | Required for MinGW and MSVC Debug and Release separately |
| U8 | Repeated full initialization in one process (#66) | | Not promised by #92 |
| U9 | Robot `UID` numeric values if unified with `EntityId` | | Accept; only relative order is used |
| U10 | Move acknowledgement chosen from the visual selection, not the commanded group | `MatrixSide.cpp:9060-9062` | Preserve for player input; define the Automation behavior with #99 |

## 10. Alternatives considered

| Alternative | Verdict | Reason |
| --- | --- | --- |
| A1: Big-bang ECS or data-oriented rewrite | Rejected | Equivalence cannot be reviewed; RNG order is lost |
| A2: Context object or service locator passed everywhere | Rejected | A hidden global; it conflicts with #92 |
| A3: Event bus or deferred simulation events | Rejected | Reorders side effects and draws. A synchronous single observer replaces it |
| A4: Per-subsystem virtual interfaces | Rejected | No second implementation; link-time stubs and null tolerance already serve tests |
| A5: Central slot-map registry instead of `SObjectCore` | Deferred | Adds lookup cost and duplicates existing weak semantics. An id index is added only for #99 or #67 |
| A6: Deferred destruction (graveyard) | Rejected for now | Changes iteration, address reuse and timing |
| A7: Separate visual RNG stream or fixed timestep | Out of scope | Gameplay decisions tracked by #76 |
| A8: Extract a navigation class now | Deferred | No test consumer before #81 |
| A9: Queue human input like automation | Rejected | Changes sub-frame ordering relative to dispatch |

## 11. Unresolved decisions

- U1 to U10, which need maintainer decisions.
- Whether 92.1 should also make `CGame::Deinit` tolerate exceptions stage by stage. This changes behavior only on failure paths.
- Whether the camera and visibility measurably change hit outcomes (section 3). This needs #76.
- Final `EntityId` width and serialization, pending #102 (X1 to X8) and the #99 protocol.
- Whether `-ftrivial-auto-var-init=zero` (`CMakeLists.txt:60`, MinGW only) hides uninitialized-local behavior that MSVC does not reproduce. This affects equivalence claims across compilers.

## 12. Overlaps and incidental findings

Overlapping issues:

| Issue | Overlap with this plan |
| --- | --- |
| #67 | Ids and references |
| #76 | RNG and time; prerequisite of 96.1 |
| #77, #79 | Fixtures, observer consumer, F4 |
| #78 | Benchmarks for 92.3, 93.2 and #98 |
| #80 to #87 | Baseline evidence per leaf; #86 must freeze the host structures first |
| #88, #89, #90 | F1, F2, 93.5 |
| #99, #100 | Command front door, ids, observer |
| #102, #103 | Section 7.3 |
| #60, #64, #66 | `g_Config` and session lifetime (concurrent) |

No new issue is proposed beyond the siblings in section 8. Incidental findings that need separate items, after triage:

- F1: overlapping `memcpy` in `CMatrixRobotAI::RemoveOrder` (`MatrixRobot.cpp:4697`). This is undefined behavior; it needs a separate fix with a regression.
- F2: `GetGroupByIndexTest` checks `y` against `m_GroupSize.x` (`MatrixMap.hpp:686`). Its callers are presentation-only (`Effects/MatrixEffect.cpp:483`, `MatrixShadowManager.cpp:196`).
- F3: `g_LoadProgress` dangles after `CGame::Init` (`MatrixGame.cpp:224-225`).
- F4: `tests/command_world.hpp:24` bypasses `HNew` zeroing.
- F5: `CInfo::RemoveFromList` leaves `m_TargetLast` and `m_Ignore` in place (`Logic/MatrixEnvironment.cpp:107-118`).
- F6: Debug-only flyer targeting in `BeforeDraw` (`MatrixMap.cpp:1199-1214`) makes Debug input behavior differ from Release.

## 13. Coverage gaps

- No build, test, benchmark or game run was performed. Every claim comes from reading source at 540251d. Items marked R in section 3 are unmeasured.
- The outputs of the concurrent #76, #77, #88 and #102 research were not available. Sections 7.3, 7.4 and 9 are open constraints to reconcile with them before the affected leaves are claimed.
- Only part of the code was traced: `Interface/CInterface.cpp` (5,357 lines), flyers, cannons, building capture and `ReloadDynamics`. The pause branch and the dialog and transition modes were not walked.
- The DLL host path and MSVC-specific behavior were checked only by reading. Host callback use beyond the cited sites was not enumerated.
- The #60 settings work merged into `dev` as #62 after this revision, so its effect on `g_Config` and settings ownership is not reflected here.

## 14. Reconciliation with #76 and #102

The concurrent #76 and #102 investigations finished after this report was drafted. Their results change the open constraints in section 7.3 as follows.

- X3 does not hold today. #102 found `uintptr_t` values narrowed again to `DWORD` or `uint32_t` in the vector-object draw parameter, effect user data and the selection enumeration parameter (#102 O7, O8, O9), and a base pointer kept in `int m_iParam` for interface identity (O10). These width fixes (#103 phase 1c) must land before or together with 93.1 and 93.4, and O10 should use the `EntityId` from D2 rather than a widened pointer.
- X2 and X8 are consistent with #102: the only persisted record that contains a runtime pointer is the package `SFileRec::m_Extra` (O4), which is outside these boundaries. `SObjectCore` and `EntityRef` stay in memory only, and ids must be counters, never addresses.
- X5 becomes a requirement for 92.1. #102 recommends that the standalone session no longer reuse the host `SRobotsSettings` (`MatrixGame/src/MatrixGame.cpp:328`); the session should hold its own settings and convert host types only in the DLL adapter.
- X7 needs one addition from #76: creating the Direct3D device without `D3DCREATE_FPU_PRESERVE` (`MatrixLib/3G/3g.cpp:192`) switches the x87 control word to 24-bit precision, and it stays switched after the device is released. Bit-exact equivalence for rule extraction (D3, U7) must therefore be checked at the in-game precision, for example with the test-only `_control87(_PC_24, _MCW_PC)` seam from #76, not at the 64-bit default of a headless test.
- #76 measured the presentation draws listed in section 3: one draw per laser per drawn frame, two draws per sound played only with audio present, and creation gates for eleven effect types at 3000 units from the camera. U1, U2 and U4 should be decided together with #76 decisions 3, 5, 7 and 8.
- #102 adds a constraint for D1 and D5: C++ exceptions must not cross C libraries or host `__stdcall` callbacks, and objects with internal pointers must not be relocated by reallocation or byte copies. This supports 93.5 and the exception policy for 92.1.
