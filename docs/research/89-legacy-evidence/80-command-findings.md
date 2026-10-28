# Player-command legacy findings

Source reference: `662693fb6b6dcfcfd5749625e5af9e61d160e8a5`. This identifies the current implementation examined by this supplemental note, not an accepted behavior-baseline reference.

This supplemental inventory records the disposition of the player-command finding below. It changes no production behavior or accepted baseline reference. The maintainer accepted preservation of the measured result under [player-command work package #80](https://github.com/pozitronik/MatrixGame/issues/80) on 2026-10-24; any gameplay correction is a separate post-baseline proposal.

## Special-target reference loss

Classification: **Legacy quirk (Q), preserved by maintainer decision on 2026-10-24**. The [special-object command cases](../../testing/80-special-object-commands.md) reproduce the current path with synthetic Break/Anim objectives: explicit/context Attack and explicit Bomb accept a special object, but the synchronous `CMatrixSideUnit::TaktPL` validation clears its stored reference because the object fails `IsLive`. The destination remains available. Attack leaves the objective intact; ordinary robot environment targeting can fire at enemies while that order stays active. Bomb can complete against the saved area through normal movement and detonation.

The baseline retains the passing null-reference expectation as the accepted characterization; it does not establish correct targeted Attack or classify the behavior as a reproduced corruption defect. A future correction needs a separate post-baseline proposal, review of downstream target consumers, an explicit gameplay/reference decision and focused regressions. Changing `IsLive` globally would extend unrelated actor-type contracts and is not authorized by this disposition. Accepting this one characterization does not select a complete reference revision or accept #69.

This finding is distinct from the [separately corrected exhausted cheat logic-slot defect](../../testing/327-group-capacity.md). No unsafe exhausted-slot execution, new defect registration or correction is part of these special-object cases. The command package and complete-baseline gate remain open.
