# Legacy markers, disabled code and historical alternatives (#88)

Investigation of `dev` at 540251d. No source was changed. References are repository-relative `path:line` at that revision.

The searchable inventory is `inventory.csv` (58 grouped entries, one row each, columns: id, theme, locations, kind, configurations, callers, intended behavior, disposition, rationale, follow-up, needs, overlap). `unreferenced-functions.tsv` lists 177 engine functions that are compiled but referenced by no relocation in any of the four built configurations.

## Summary

Groups in the inventory: 7 defect candidates (D), 16 marker groups (M), 18 disabled-implementation groups (C), 1 explanatory-comment group (E), 8 preprocessor/optional-path groups (P), 4 unused-module groups (U) and 4 historical-branch groups (H).

Disposition candidates: 10 actionable, 9 active (including P6, whose selected variant is active while its alternatives are obsolete), 12 superseded, 15 obsolete and 12 unproven. Follow-ups that need evidence: 5 need a benchmark or measurement, 7 need a reproduction, 12 need a product decision and 2 need a build experiment. Some entries need more than one of these.

Scale: 38 `TODO` lines, 9 `STUB` lines, 2 `TO DO` lines and 2 Russian task markers (`доделать` = "finish it", `Сделать` = "to do"). About 9,500 comment lines that look like code fall in 619 regions of three or more lines; 148 regions have ten or more. There are no `#if 0` blocks. Most disabled code is disabled through `//` or `/* */`, through macros that are never defined, or through code placed after an early `return`. History shows that about 7,800 commented code lines were already disabled in the 2012 import (2f7f953). About 1,000 were added later, mostly during the 2022-2023 refactoring (6a0dd2c, 5aabeba, b2a6ee7, 0b01d09, 187cafe).

Main conclusions:

- Most disabled code is superseded original-studio code: the scripted tactics system, the old zone pathfinding, old robot collision and steering, the timing-wheel reminder, the Pascal package writer and the rendering-capability fallbacks. These can be deleted under #90 after a short intent note, without measurements.
- Two items are product questions, not cleanup: player-built combat flyers (C6), which are a cut feature that the modding fork restored, and the map-editor road-network builder (U2), which is compiled into every build but never called.
- One historical change affects behavior. The 2023 main-loop refactor (0b01d09) removed the 16-frame elapsed-time smoothing and the `MaxFps` limiter (M1). `MaxFps` is still read from configuration and then ignored.
- Both known defects were re-derived from current source, and two more were found (D3, D4).

## Method

- Markers: case-insensitive search for TODO/FIXME/STUB/XXX/HACK/BUG and Russian task words. Each hit was read in context.
- Disabled code: a script found runs of `//` lines and `/* */` blocks that contain code-like lines. The first lines of all 148 regions with ten or more lines were read, then grouped by subsystem. Constant conditions (`if (0)`, `if (true)`) and code after an early `return` were searched for separately.
- Preprocessor: every `#if/#ifdef/#ifndef` macro was listed and its definition located in source, CMake, CI and tools. Truth tables per configuration come from `CMakeLists.txt`, `MatrixGame/CMakeLists.txt`, `MatrixLib/CMakeLists.txt`, `tools/build.ps1` and `.github/workflows/win-build.yml`, and were confirmed in the generated `build.ninja`:
  - `_DEBUG` is defined in Debug only: MinGW passes `-D_DEBUG`, and MSVC gets it from the Debug CRT.
  - `ASSERT_OFF` is defined in Release only.
  - `BUILD_EXE` is defined only for `MatrixGame.cpp` and `MatrixFormGame.cpp`, and only in EXE builds.
  - `CHEATS_ON` is defined only for `MatrixFormGame.cpp`, and only in cheat builds.
  - `MATRIXGAME_EXPORTS` is always defined.
  - `_RELDEBUG`, `_TRACE`, `_DISABLE_AI_HELPERS`, `MAXEXP_EXPORTS`, `_SUB_BUG`, `USE_SMALL_TEXTURE_IN_ROBOT_ICON`, `_TIMING`, `DO_SMART_COLOROPS`, `DMM`, `BASE_DLL`/`BASE_EXPORTS` and `FORCE_ENABLE_MEM_SPY` are never defined.
- Compiled and reachable code: four MinGW configurations were built with the pinned GCC 13.2 and CMake 4.4.4, using `-ffunction-sections` in a scratch directory: Debug EXE, Release EXE, Release DLL with cheats, and Debug EXE with cheats. All four built successfully. For every engine object, the defined text symbols were compared with relocation targets, skipping unwind and debug sections. Linker section GC could not be used because the DWARF unwind tables keep every function alive. The result is conservative: virtual functions count as referenced through vtables, and inline-only functions do not appear at all.
- Call sites: representative callers of every candidate were checked with grep and by reading the code. Tests were checked separately, so test-only consumers are marked as such.
- History: `git log`, `git log -S/-L` and `--diff-filter=D` were run on dev. The remotes `origin/*`, `original/*` (murgesku) and `upstream/*` (vladislavrv) were compared with their merge bases after a read-only `git fetch`. Only `original/master`, `original/modding`, `original/modding-old`, `upstream/modding` and `upstream/stuff` have commits that are not in dev.
- Existing issues: the open issues #60-#106 were read with read-only `gh api` to find overlap.

## Inventory highlights

The complete table is `inventory.csv`. The groups that matter for later decisions are summarized below.

### Markers (M)

- M1 Main-loop timing:
  - Where: `MatrixLib/3G/3g.cpp:388-394` ("bring smoothness back?"), `3g.cpp:407` ("maybe add back FPS limit?") and `MatrixGame/src/MatrixConfig.cpp:400`.
  - What changed: before 0b01d09, `L3GRun` passed a 16-frame moving average of capped elapsed time to `Takt`, and `CFormMatrixGame::Draw` slept adaptively against `g_MaxFPS`. Now raw capped deltas are used and `g_MaxFPS` is never read.
  - Disposition: actionable investigation that needs a measurement and a product decision (#76, #78, #60).
- M2 BUILDCFG:
  - Where: `MatrixGame/src/DevConsole.cpp:75` says the command segfaults, and `MatrixGame/CFG/README.txt:34` asks for text configuration in the DLL.
  - Disposition: unproven. The crash needs a reproduction, and DLL text configuration is a host-contract decision (#86).
- M3 Rare crash note at `MatrixGame/src/Effects/MatrixEffectPointLight.cpp:599`: unproven and needs a reproduction (#93).
- M4 Pathfinding hotfix at `MatrixGame/src/MatrixLogic.cpp:766-827` (ac21bc2):
  - What it does: replaces `ASSERT(other_cnt<200)` with an early `return false`, so crowded scenes find no place instead of overrunning stack arrays.
  - Disposition: active. Cover it in the navigation baseline (#81).
- M5 MSVC C1001 workaround at `MatrixLogic.cpp:2307-2374` (a36dfc2): active and MSVC-only. Check in CI whether the current compiler still needs it.
- M8 Visibility lists:
  - Where: `MatrixGame/src/MatrixMapStatic.hpp:277-286` (`WillDraw`) and `MatrixMapStatic.cpp:898-926` (`OnEndOfDraw`).
  - What it is: both return immediately. Their unreachable bodies are an unfinished off-screen notification system that was already disabled in 2012.
  - Disposition: obsolete. Revive it only if a measurement justifies it (#98).
- M9 STUB markers in the build stack:
  - Where: `MatrixGame/src/MatrixObjectBuilding.cpp:1635/1738/1774/1787` and related lines.
  - What they mark: live direct calls from simulation into the UI for the player side, not unimplemented code.
  - Disposition: active (#94, #96).
- M10 `BuildRandomBot` at `MatrixGame/src/Interface/CConstructor.h:162-180` ("FAKE FUNCTIONS"): unused, and it mixes `g_MatrixMap->Rnd` with the global `RND`. Obsolete.
- M11 Stale TODO at `MatrixLib/3G/VectorObject.cpp:360`: the "restore triangles" work is implemented directly below the comment. Obsolete.
- M12-M16 Mechanical or naming notes: codecvt, std::format, starts_with, `Takt` naming and the CMake file list. These belong to #97, apart from the CMake file list (M16), which belongs to #90 together with U1. The host-ABI const note (M13) is active and explanatory.

### Disabled implementations (C)

- C1 Scripted tactics system:
  - Where: `MatrixGame/src/Logic/MatrixTactics.cpp`, `MatrixRule.cpp`, `MatrixState.cpp`, `MatrixLogicSlot.cpp`, large parts of `MatrixAIGroup.cpp`, and commented callers in `MatrixSide.cpp:1191-1316`, `MatrixObjectBuilding.cpp:1332-1353` and `MatrixObjectCannon.cpp:1487-1506` (the last is marked "do not uncomment").
  - Status: already disabled in 2012. The four `.cpp` files compile to empty objects. The live `CMatrixGroup` and the `ENEMY_ANY` define (`MatrixEnvironment.cpp:23`) must stay.
  - Disposition: superseded.
- C2-C5 Superseded navigation, movement and loader code in `MatrixLogic.cpp`, `MatrixRobot.cpp` and `MatrixMap.cpp`:
  - The robot `Load` at `MatrixRobot.cpp:3994` reads an older map record, not a save-game. It gives #67 no save format to reuse.
- C6 Player-built combat flyers:
  - Where: `MatrixGame/src/MatrixFlyer.cpp:727-1093`, `MatrixObjectBuilding.cpp:1310-1316` and `1649-1692`, `CInterface.cpp:4403-4426` and `4575-4610`.
  - Status: `CMatrixBuilding::BuildFlyer` and the selection/arcade functions are unreferenced. Flyers are active only as reinforcement transports through `CMatrixSideUnit::OrderFlyer`.
  - Disposition: unproven; needs a product decision.
- C9 AI strategy alternatives:
  - Where: `MatrixSide.cpp`, with blocks duplicated between the `TaktHL` and `TaktPL` families (2935/9964, 5451/8498).
  - Disposition: unproven. Skim them during the #84 baseline, then delete.
- C11 Rendering fallbacks in `MatrixRenderPipeline.cpp`:
  - What: capability-based texture-stage paths and a shader compile. `if (true)` at 2344 replaced the capability check, and seven stage functions are unreferenced.
  - Disposition: obsolete unless low-end hardware support is wanted.
- C15 Package writer in `MatrixLib/Base/Pack.cpp` (about 1,400 lines, much of it Pascal source) and `Pack.hpp:132-324`:
  - Disposition: obsolete for the engine. Confirm with #61 that no writer is needed.
- C10, C12-C14, C16-C18: commented diagnostics and superseded small fragments. For example, `ExtraRemove` in `MatrixSoundManager.cpp:522-546` duplicates the live `FindSlotForSound`. `Texture.cpp:400-438` refers to the removed `CDWORDMap`.

### Optional paths and preprocessor (P, E)

- Active and intentional:
  - P1: Debug diagnostics and debug keys (documented in `docs/CONTROLS.md`).
  - P7: `BUILD_EXE`, `CHEATS_ON`, `ASSERT_OFF` and the compiler checks.
  - P3: Debug-only exception containment around host sound callbacks (`MatrixSoundManager.cpp:662-668` and `1102-1112`), so Debug and Release handle exceptions differently.
  - E1: explanatory comments to keep, including the enum-to-menu tables and the reachable `if (0) { do_animation: }` goto target at `MatrixRobot.cpp:318`.
- Dead in every configuration:
  - P2 `#ifdef _TRACE` blocks.
  - P4 never-defined feature macros. The `_SUB_BUG` blocks use removed `std::wstring().Format` and would not compile. The `MAXEXP_EXPORTS` guards are leftovers from the exporter removed in 2ab569a.
  - P6 alternatives to unconditional local defines.
  - P8 constant conditions.
- `_RELDEBUG` and `_DISABLE_AI_HELPERS` are vestigial: since neither is ever defined, their conditions reduce to `_DEBUG`.
- `RENDER_PROJ_SHADOWS_IN_STENCIL_PASS` (`MatrixMap.cpp:39`, with the dead `#ifndef` at 2215 and the unreferenced `DrawShadowsProjFast`) was reworked by #62 (5dcaa03), which merged into `dev` after this revision. Re-derive it from current `dev` before any cleanup.
- `_ENGLISH_BUILD` (`CIFaceMenu.h:42`) makes the Russian menu widths dead. This is a localization decision.

### Unused modules and functions (U)

- U1: the `MatrixLib/3G` scene-graph prototype (`CWorld.hpp`, `CWorldObject.hpp`, `CCamera.hpp`, `CCoordSystem.hpp`, `CVertexBuffer.hpp`, `SVertex.hpp`) and `MatrixLib/Base/CList.cpp/.hpp`. Nothing includes them, and `CList.cpp` is not in CMake. Obsolete.
- U2: 36 road-network construction functions in `MatrixRoadNetwork.cpp`, including `Test`, `FinalizeBuild` and `Save`. They are compiled into every build and unreferenced; only `Load` is used. Keep them until the scope of content tools is decided.
- U3: the path-reservation subsystem at `MatrixLogic.cpp:3208-3394`. Only `PathClear` is reached. Obsolete.
- U4: 177 functions are unreferenced in all four configurations; see `unreferenced-functions.tsv`. Some are used by tests: `CalcCRC32`, `CStorage::CalcUniqID`, `CBlockPar::BlockDelete` and the `Audio::Service` accessors. Each needs review before deletion.

### Historical branches (H)

- H1 `original/master`: 2d42599 (pointer-width trace test) and 06e4d32 (AI head type) are already in dev. Superseded.
- H2 Modding fork (`original/modding`, `original/modding-old`, `upstream/modding`, 2020-2022): adds named key rebinding, gathering points, F2 select all, `PlayerRobotsAutoBoom`, building control groups, robot design saving, `EnableFlyers`, a loading screen for any resolution, configuration from the map file and view/effect limits. None of these are in dev. Each is a gameplay or UI change that needs a product decision.
- H3 `upstream/stuff` (2023): draw-phase `perf_counter` instrumentation that was never merged. It is a useful reference for #78.
- H4 Modules already removed: the MaxExp 3ds Max exporter (2ab569a), DebugMsg, Window, FileJPG, the sharpening assembly, CDWORDMap, CWStr/CStr and RangersText. The exporter history is a reference for future content tools.

## Defect candidates re-derived from source

1. D1 (known, confirmed):
   - Where: `MatrixGame/src/Interface/CIFaceElement.cpp:148` sets `r.y1 = m_ClearRect.left + y`. The configuration parser at `CInterface.cpp:673-676` fills `top` separately, and the rectangle is used for a depth/stencil clear at Z=0 in `MatrixFormGame.cpp:296-305`.
   - History: present unchanged since the 2012 import.
   - Reach: neither tracked `iface.txt` nor the decompressed tracked `robots_rename_to_use.dat` contains `ClearRect`, so only external configurations exercise the path.
   - Effect: a wrong vertical start for the cleared area. 3D content can disappear above an interface panel, or a strip of the panel area is left uncleared.
   - Next step: a reproduction with a synthetic `ClearRect`, then a one-line fix.
2. D2 (known, confirmed):
   - Where: `MatrixLib/Base/Pack.cpp:2326-2328`. `CPackFile::FindFirst` calls `S.Folder->FindNext` after `GetFolderEx` (1464-1481), which returns `NULL` for a missing path.
   - A second problem: it returns 0 (success) when there is no root folder, while `FindNext` returns 1.
   - Reach: no production or test caller in any configuration.
   - Next step: delete it in #90, or add a null check with a test if the API is kept.
3. D3 (new):
   - Where: `MatrixGame/src/MatrixLogic.cpp:2389`. `CMatrixMapLogic::FindPlace` tests `mappos.x < (tp.y + 4)` where `tp.x + 4` is intended. The same code is in 2f7f953.
   - Caller: `EscapeFromBomb` (`MatrixSide.cpp:2337`), which runs from `TaktHL` (computer sides) and `TaktPL` (player automatic mode).
   - Effect: reservations of other robots' move-to places can be missed or matched wrongly.
   - Fixing it changes AI decisions, so it needs a reproduction and a maintainer decision.
4. D4 (new):
   - Where: `MatrixLib/Base/utils.hpp:29`. The wide-string `utils::format` passes `sizeof(buf)` (20,480 bytes) as the `swprintf` element count for a 10,240-element buffer. Output longer than 10,240 characters overflows the stack instead of failing.
   - Reach: 133 call sites; no evidence that any of them currently exceeds the limit.
   - Next step: a small fix with a boundary regression. No gameplay effect is expected.
5. D5 (existing TODO, behavior unchanged):
   - Where: `MatrixGame/src/Interface/CIFaceButton.cpp:73`, where `find(L"conf")` is used as a boolean.
   - The original `CWStr::Find` had the same -1/0 semantics, so current behavior matches the original: names that start with `conf` get the ordinary click.
   - The intent is probably inverted, but changing it alters audible behavior, so it needs a product decision.
6. D6 (new, unreachable):
   - Where: `MatrixLib/Base/Pack.cpp:2350-2358`. `CPackCollection::DelPacketFile` removes the pointer without `HDelete`.
   - Reach: no callers. Delete it together with D2.
7. D7 (new, separated from M12 during review):
   - Where: `MatrixLib/3G/Math3D.hpp:71-159`. a2dcad3 (2022) replaced the `SPlane` union of `dxplane` with `norm`/`dist` by separate members. `BuildFromPoints` and `BuildFromPointNormal` still fill only `dxplane`, while `UpdateSignBits`, `IsOnSide`, `CalcPointDist`, `Distance`, `BoxSide` and `FindIntersect` read `norm`/`dist`. This is a behavior mismatch, not syntax cleanup.
   - Callers: `CMatrixMap::CalcVis` builds edge and shadow planes with `BuildFromPoints` and tests them with `IsOnSide` (`MatrixVisiCalc.cpp:140-141`, `154`, `157`, `178`, `287-288`). `CalcVis` runs when the standalone executable is started with a map argument (`MatrixGame.cpp:84-92`), from the console command (`DevConsole.cpp:137`) and from Debug F7 (`MatrixFormGame.cpp:1582-1585`). A Debug-only flyer cursor path uses `BuildFromPointNormal` and `FindIntersect` (`MatrixMap.cpp:1205`).
   - Reach: normal play does not call `CalcVis`, and the live camera fills `norm`/`dist` directly, so no in-game culling failure is inferred from this.
   - Next step: a focused geometry regression on the builders and on `CalcVis` edge planes, then restore shared storage or synchronize the members.

## Bounded follow-up proposals

These are drafts; none have been filed.

For #89 (investigate and measure):

1. Main-loop timing (M1, H3):
   - Measure frame-time and step-size distributions for the current loop and the pre-0b01d09 smoothing/limiter in a representative battle.
   - Decide whether `MaxFps` should work again (coordinate with #60 settings and #76 time control).
   - Needs a benchmark and a product decision.
2. Defect reproductions D1, D3, D4 and D7:
   - D1 and D4 can get synthetic regressions through production code.
   - D3 needs a synthetic place grid and an AI-choice comparison.
   - D3 needs a maintainer decision because the fix changes gameplay.
3. Unproven crash notes (M2, M3):
   - Attempt `BUILDCFG` in a Debug EXE, and keep a long Debug battle running with allocation tracking.
   - Record either a reproduction or a bounded no-repro result.
4. Developer switches (P5) and the C1001 workaround (M5):
   - Compile each commented switch once on a scratch branch.
   - Test removing the MSVC pragma in CI.
   - Build experiments only.
5. AI alternatives (C9) and gameplay-intent notes (M6, D5):
   - Summarize the intent of each, with commit references, as input to #84.
   - No implementation.

For #90 (deletion leaves; each small and deletion-only):

1. Unused scene-graph files and `CList` (U1), plus the CMake file-list TODO (M16).
2. The scripted tactics system (C1) and the old zone/path/placement code (C2, C3, U3), after a two-line design note in docs.
3. Superseded robot movement/collision and loader blocks (C4, C5, C7, C12, C13, C14, C16), after the #81 baseline exists.
4. Never-defined macro branches and `MAXEXP_EXPORTS` guards (P2, P4, P8), plus the stale TODO (M11) and `BuildRandomBot` (M10).
5. The package writer (C15) and the unused pack API (D2, D6), once #61 confirms no writer is needed.
6. Commented diagnostics and constants (C10, C17, C18).
7. Reviewed parts of the unreferenced-function list (U4), one subsystem at a time, keeping functions used by tests.

For other existing issues:

- #60: building projected shadows (M7) and `RENDER_PROJ_SHADOWS_IN_STENCIL_PASS` (P6) were changed by #62 (merged after this revision). `MaxFps` (M1) as a setting.
- #61: whether a pack writer (C15) or the commented original resource paths (C17) are needed.
- #63: modding-fork loading screen for any resolution (H2); the `_ENGLISH_BUILD` menu widths (P6).
- #67: no reusable save format exists in the disabled loaders (C5).
- #78: `perf_counter` instrumentation from `upstream/stuff` (H3).
- #80, #84: flyers (C6), modding-fork command features (H2) and AI alternatives (C9).
- #81: the pathfinding capacity hotfix (M4) needs a crowd scenario.
- #86: `BUILDCFG` and DLL text configuration (M2); Debug-only host-call containment (P3).
- #91, #94, #96: STUB-marked UI calls from simulation (M9) and the MatrixLib-to-MatrixGame include (M14).
- #93: the point-light crash note (M3).
- #97: mechanical TODOs and naming (M12, M15) and vestigial `_RELDEBUG`/`_DISABLE_AI_HELPERS`.

## Unresolved decisions

- Should player-built combat flyers (C6), and any of the modding-fork features (H2), become product scope, or be deleted as cut features?
- Should the road-network builder (U2) and the pack writer (C15) stay as a basis for content tools, or be removed?
- Should frame-time smoothing and `MaxFps` limiting be restored (M1)? Either choice changes timing-dependent behavior.
- Should the D3 and D5 corrections be applied, given that they change AI choices and the click sound?
- Should text configuration be allowed for the DLL (M2)?
- Should the Russian layout constants behind `_ENGLISH_BUILD` be kept for localization?

## Coverage gaps

- MSVC was not built locally. The MSVC macro behavior (`_DEBUG` from the Debug CRT, `_MSC_VER`) comes from the CMake configuration. The MSVC Debug DLL and the MSVC Debug EXE with cheats are also outside CI.
- The unreferenced-function scan used MinGW objects only. It cannot see inline-only functions, and it treats every virtual function as referenced.
- Nothing was run. No defect was reproduced and no timing was measured. D1-D7 are source-supported candidates, not demonstrated failures.
- The SR2 host's own `robots.dat` was not available, so D1 reachability with host data is unknown.
- The 619 smaller commented regions (three to nine lines) were classified by subsystem without reading each one. Only the 148 larger regions were reviewed individually.
- The modding-fork features were compared by configuration keys and identifiers, not by behavior.
