# Historical gameplay alternatives

Source reference: `fad309b43e6051862af732a912ea5f7bb6c7f113`. This is a source and existing-evidence investigation; no new gameplay run, benchmark or compiler experiment is claimed. Unmerged prerequisites are identified explicitly.

## Corrections to the old inventory

- Do not describe all disabled code as disabled in a verified “2012 import”. Following the file rename reaches `2f7f953` (2020-07-07), where `MatrixGame/src/MatrixTactics.cpp` already comments its implementations; `f5f48a5` later moves the file. The 2012 copyright year does not establish when the feature was disabled. The defensible statement is that the implementation was disabled by the first recorded import, with the earlier date unknown.
- C8's GetObjectUnderCursor name is ambiguous. MatrixSide.cpp:1170's zero-argument overload has live callers at 631, 777, 817 and 839. Only the coordinate-output overload at 1158 has no caller in the inspected tree. ResetSystemSelection is used by current player_order_scenarios.cpp fixtures, including lines 470, 509 and 539. Do not delete either as an undifferentiated inventory entry.
- U2's “Save unreferenced” is no longer a safe programme-wide claim. Prepared shared native assets (07e568fb464ec23fa835791a5a7ffee94e919158, tests/native_session_assets.cpp:126) call road.Save(network). That dependency has not yet reached dev; removal would break the already prepared #85 tests.
- H2's blanket “features absent from dev” is false. MatrixConfig.cpp:432–446 implements numeric AssignKey rebinding now; standalone resolution/window/loading work also exists. Historical named action bindings and separate persisted RobotsCFG.txt are distinct features.

## Dispositions and exact boundaries

The canonical [disposition table](disposition.md) owns the full intent, status and follow-up boundaries for C1-C10, C18, U2 and U3. The corrections above and historical feature proposals below add context without duplicating those rows.

## Historical feature proposals (H2)

Use original/modding and upstream/modding as evidence sources, not replacement integration bases. Their commits combine behavior, UI, configuration and formatting, so a clean new implementation against characterized dev is safer than bulk cherry-picking.

| Idea | Historical evidence | Current distinction / decision acceptance |
| --- | --- | --- |
| Named key bindings, user file | 6dbc0e3, 54e6538, d3d1b48 | Dev already reads numeric AssignKey. Decide stable action names, key conflict semantics, defaults and persistence precedence; test old numeric configs and missing/invalid user files. Do not claim all rebinding absent. |
| Building rally/gathering points | 7533e6f, 4923688 | Product proposal must define newly built vs delivered robots, ownership transfer, canceled orders and blocked destinations; overlaps #80/#82. |
| F2 select-all | 04d88df | Specify eligible sides/states, offscreen/dying/carried units, interaction with groups/manual control; current selection contract takes precedence. |
| Player automatic bomb detonation | a6de4d1 | Explicit gameplay choice, not cleanup; define player intent precedence, collateral and configuration defaults before implementation. |
| Building control groups | 472ae4c | Decide mixed building/robot group semantics and stale ownership/deletion behavior; reuse #80 baseline, not an assumed feature parity requirement. |
| Saved robot designs | d70d36f, 20772d6, faaa506 | Distinct from whole-battle saves (#67). Specify schema/version, mod-part availability, persistence path and invalid data recovery. |
| Combat flyers | be466e3, 53f1219 | [C6 in the disposition table](disposition.md); retain current reinforcement contract. |
| Any-resolution loading UI | b177462 | Dev has later standalone display/loading work (8729e91, e17b006, selector/loading changes). Compare a demonstrated missing layout behavior before proposing a port. |
| Configuration embedded in map | 30160bf | Inspect configuration precedence, trust/size limits and DLL vs standalone behavior. Existing map catalog/metadata is not proof of equivalent arbitrary map-config support. Decide this as content format policy before implementation. |
| View/effect-distance settings | Historical modding configuration contains distance fields | No isolated commit/behavior proof established in this bounded audit. Retain as bounded investigation: identify exact introduction and current equivalent fields; measure render workload and visible omissions under fixed camera/map before proposing a setting. Do not call it a measured optimization. |

## Safe follow-up order

First retain this corrected inventory and historical references in the reviewed #89 report. Product alternatives above can remain bounded unresolved investigations under #89 until separately triaged; this audit creates no remote issues. #90 comment-only changes should be small, preserve explanations and map each removed block to this disposition. Compiled subsystem deletion (U3 and qualified C8/C6 helpers) needs its own call/build/ABI evidence and the programme's accepted whole-game baseline. Tests using a method are consumers, even when gameplay currently does not call it. There is no demonstrated new crash or proven performance gain in this slice, so no defect or optimization claim should be manufactured from dormant code.
