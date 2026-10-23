# Cheat creation and lifetime findings

Source reference: `662693fb6b6dcfcfd5749625e5af9e61d160e8a5`. This identifies the current implementation examined by this supplemental note, not an accepted behavior-baseline reference.

This supplemental inventory gives the existing [player-command work package #80](https://github.com/pozitronik/MatrixGame/issues/80) ownership of the pending C6 investigations below. The [registry guide](../../testing/80-cheat-registry.md) records the passing bounded evidence. No new defect issue, completed correction or complete-baseline acceptance is claimed.

## Exhausted logic-group slots

Classification: **source-derived out-of-bounds hazard, pending separate defect disposition**. `CMatrixSideUnit::RobotToLogicGroup` searches slots zero through `MAX_LOGIC_GROUP - 1`, asserts that it found a slot, then unconditionally indexes `m_PlayerGroup[no]`. When all 61 slots are occupied, `no` equals 61; Release defines `ASSERT_OFF`, leaving an out-of-bounds write and subsequent invalid access. This unsafe path is distinct from the [special-target Q characterization](80-command-findings.md#special-target-reference-loss), where a passing expectation preserves an observable legacy quirk rather than corruption.

The passing SPAWN boundary creates a sixty-first robot in the last valid slot after slots zero through 59 are populated. It does not execute an additional SPAWN with every slot occupied or advance the over-limit side through unrelated fixed-size AI buffers. The exhausted path remains unperformed and unregistered; the source finding is not claimed as a reproduced runtime defect or a safe rejection contract. #80 owns a bounded reproduction with failure containment, a separately reviewed correction and meaningful Debug/Release regression evidence before C6 completion. Any gameplay-visible capacity rule needs an explicit maintainer decision; this inventory invents none.

## Cross-battle crazy history

Classification: **unresolved lifetime investigation**. `CMatrixRobotAI::LogicTakt` retains function-static `ppx`, `ppy` and `cnt` for crazy-robot behavior across Sessions. The deployment fixtures isolate profiles in separate processes and report frame counts and digests for manual fresh-process comparison; no registered digest assertion or production reset establishes cross-battle isolation. #80 owns a bounded repeated-battle comparison and the resulting disposition. A correction and accepted reference change require their own evidence and review; this inventory does not reset production state or promote the digest to a golden.
