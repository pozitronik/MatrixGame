# Cheat creation and lifetime findings

Source reference: `662693fb6b6dcfcfd5749625e5af9e61d160e8a5`. This identifies the current implementation examined by this supplemental note, not an accepted behavior-baseline reference.

This supplemental inventory gives the existing [player-command work package #80](https://github.com/pozitronik/MatrixGame/issues/80) ownership of the C6 investigations below. The [registry guide](../../testing/80-cheat-registry.md) records the passing bounded evidence. The historical source reference remains unchanged; the bounded #327 correction is recorded below, without complete-baseline acceptance.

## Exhausted logic-group slots

Classification: **memory-safety defect corrected under #327**, not a preserved gameplay quirk. The historical source path in `RobotToLogicGroup` and the analogous selected-group assignment exhausted all 61 slots, asserted that a free index existed, then used the unavailable index. Release removed that assertion. The maintainer selected refusal on 2026-10-24: return -1 without changing existing membership/orders, reject that result in all ten order handlers, and reject SPAWN before assembly when no slot exists. No compatibility switch is required.

The [capacity regressions and protected control](../../testing/327-group-capacity.md) cover both assignment methods, invalid order identifiers, live versus cached membership, freed-slot recovery and raw/form SPAWN refusal. Existing creation into the last valid slot remains covered. The checked old-allocation control uses Debug trace assertions to fail before invalid indexing; no unprotected Release access or over-limit side tick is required. Other fixed robot-capacity arrays are not established safe by this correction. Deferred [#329](https://github.com/pozitronik/MatrixGame/issues/329) owns dynamic storage and the planned robot-limit removal after #69.

## Cross-battle crazy history

Classification: **measured process-shared legacy behavior, retained without correction**. `CMatrixRobotAI::LogicTakt` retains function-static `ppx`, `ppy` and `cnt` across Sessions and shares them between crazy actors. The [twenty registered history cases](../../testing/80-crazy-history.md) compare fresh-process, repeated-Session and multi-actor calls through production robot logic: fresh retargeting occurs on call 102; after 37 priming calls the next same-position Session retargets on call 65, while a different-position actor needs 66. Coordinate changes do not clear the count; alternating distinct actors prevents increments. These are controlled robot-call boundaries with a fixed map clock, not full deployment or battle-frame goldens.

The existing deployment fixtures still isolate profiles in separate processes and report frame counts/digests; no digest assertion or production reset establishes cross-battle isolation. The bounded lifetime question is characterized, and this change preserves its measured result. A future per-robot/per-battle reset changes retarget timing and requires a separate gameplay/reference decision. Neither this inventory nor the tests promote deployment digests to accepted historical references or accept #69.
