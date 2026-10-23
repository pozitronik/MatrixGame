# Player-command legacy findings

This supplemental inventory records the disposition owner for the player-command finding below. It changes no production behavior or accepted baseline reference. The existing [player-command work package #80](https://github.com/pozitronik/MatrixGame/issues/80) owns the pending decision; no separate correction issue or completed disposition is claimed.

## Special-target reference loss

Classification: **Legacy quirk (Q), pending separate disposition**. The [special-object command cases](../../testing/80-special-object-commands.md) reproduce the current path with synthetic Break/Anim objectives: explicit/context Attack and explicit Bomb accept a special object, but the synchronous `CMatrixSideUnit::TaktPL` validation clears its stored reference because the object fails `IsLive`. The destination remains available. Attack leaves the objective intact; ordinary robot environment targeting can fire at enemies while that order stays active. Bomb can complete against the saved area through normal movement and detonation.

The passing null-reference expectation characterizes this quirk pending a gameplay/reference decision; it does not establish correct targeted Attack or classify the behavior as a reproduced corruption defect. Before any correction, #80 must review downstream target consumers and the intended special-object contract, obtain the maintainer's gameplay/reference decision and provide focused regression evidence. Changing `IsLive` globally would extend unrelated actor-type contracts and is not authorized by this inventory.

This finding is distinct from the source-derived exhausted cheat logic-slot out-of-bounds hazard. No unsafe exhausted-slot execution, new defect registration or correction is part of these special-object cases. The command package and complete-baseline gate remain open.
