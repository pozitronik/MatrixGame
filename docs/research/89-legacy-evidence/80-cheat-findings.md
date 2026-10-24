# Cheat creation and lifetime findings

Source reference: `662693fb6b6dcfcfd5749625e5af9e61d160e8a5`. This identifies the current implementation examined by this supplemental note, not an accepted behavior-baseline reference.

This supplemental inventory gives the existing [player-command work package #80](https://github.com/pozitronik/MatrixGame/issues/80) ownership of the C6 investigations below. The [registry guide](../../testing/80-cheat-registry.md) records the creation/deployment evidence. The exhausted-slot correction is tracked separately in [#327](https://github.com/pozitronik/MatrixGame/issues/327); no completed correction or complete-baseline acceptance is claimed.

## Exhausted logic-group slots

Classification: **source-derived out-of-bounds hazard, pending separate defect disposition**. `CMatrixSideUnit::RobotToLogicGroup` searches slots zero through `MAX_LOGIC_GROUP - 1`, asserts that it found a slot, then unconditionally indexes `m_PlayerGroup[no]`. When all 61 slots are occupied, `no` equals 61; Release defines `ASSERT_OFF`, leaving an out-of-bounds write and subsequent invalid access. This unsafe path is distinct from the [special-target Q characterization](80-command-findings.md#special-target-reference-loss), where a passing expectation preserves an observable legacy quirk rather than corruption.

The passing SPAWN boundary creates a sixty-first robot in the last valid slot after slots zero through 59 are populated. It does not execute an additional SPAWN with every slot occupied or advance the over-limit side through unrelated fixed-size AI buffers. The exhausted path remains unperformed; the source finding is not claimed as a reproduced runtime defect or a safe rejection contract. [#327](https://github.com/pozitronik/MatrixGame/issues/327) owns a bounded reproduction with failure containment, a separately reviewed correction and meaningful Debug/Release regression evidence before C6 completion. Any gameplay-visible capacity rule needs an explicit maintainer decision; this inventory invents none.

## Cross-battle crazy history

Classification: **measured process-shared legacy behavior, retained without correction**. `CMatrixRobotAI::LogicTakt` retains function-static `ppx`, `ppy` and `cnt` across Sessions and shares them between crazy actors. The [twenty registered history cases](../../testing/80-crazy-history.md) compare fresh-process, repeated-Session and multi-actor calls through production robot logic: fresh retargeting occurs on call 102; after 37 priming calls the next same-position Session retargets on call 65, while a different-position actor needs 66. Coordinate changes do not clear the count; alternating distinct actors prevents increments. These are controlled robot-call boundaries with a fixed map clock, not full deployment or battle-frame goldens.

The existing deployment fixtures still isolate profiles in separate processes and report frame counts/digests; no digest assertion or production reset establishes cross-battle isolation. The bounded lifetime question is characterized, and this change preserves its measured result. A future per-robot/per-battle reset changes retarget timing and requires a separate gameplay/reference decision. Neither this inventory nor the tests promote deployment digests to accepted historical references or accept #69.
