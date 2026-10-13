# Player command characterization

The `game.player.*` cases link the production engine and reuse the controlled battle fixture described in [Testing](../TESTING.md#controlled-battle-scenarios). They add no production access seam and change no gameplay. These are executable increments toward the [behavior catalog](../research/75-behavior-baseline/README.md), not acceptance of the complete baseline.

## Running the suite

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.player\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.player\.'
```

Use the same filter with `-Cheats` to exercise the form's enabled-cheat dispatch, and without it to check that gameplay input cannot activate cheats. `game.player.intentional_crash` runs the production cheat parser in a separate bounded process: its SIGABRT handler converts the intentional abort to exit code 86. Returning normally, another failure code or a missing dispatch marker fails the wrapper. This checks the parser's intentional termination contract even when the form's cheat entry is disabled; it is not a battle golden and does not allow accidental engine crashes.

All checks use `MG_CHECK`, including Release. Each CTest process has its ordinary 30-second limit; the abort wrapper has a 15-second outer limit and a 10-second child limit. Battle advancement retains the shared fixture's frame and wall-clock bounds. Debug checks for leaked tracked allocations after each case.

## Execution profile

Movement and command scenarios use seed 79, 10 ms map steps, synthetic wheel chassis, the shared fixed obstacle and real generated geometry, synthetic audio, and the fixture's recorded x87 policy. Explicit move, context move and UI-button move each run for 1,500 steps in each supported RNG mode. The test compares all ordered production snapshots at every boundary, including raw floating-point bits and RNG draw count, within that same build and mode. This comparison establishes equality of those three entry routes in this fixture. It does not establish cross-compiler equivalence or compare the build with an accepted historical reference.

Commands reach `CFormMatrixGame::Keyboard`, `CIFaceList::PlayerAction` and the production side mouse handlers. World hit results are supplied at the side-handler boundary; no screen picking or native mouse dispatch is simulated. The UI button has a real `CIFaceElement` carrying the production action name, without rendered artwork. The command fixture initially selects its robots. The group-cap case additionally creates eight synthetic chassis through the existing geometry factory, but performs no battle ticks with that expanded world.

The suite asserts selection identities and UI flags directly. Its move-route comparison keeps the shared robot/group snapshots in memory and reports the first differing boundary; these cases do not emit persistent JSONL traces or transient internal events. The shared harness's original `game.scenario.*` trace files remain a separate evidence source. Final baseline capture still needs persisted reference/candidate recordings with the complete agreed manifest and integration revision.

## Catalog mapping

| Catalog entry | New executable evidence | Remaining baseline work |
| --- | --- | --- |
| CMD-01 | `control_groups` assigns and recalls a number group through the form, cancels a pending preorder on recall, deletes one selected member and proves it cannot return on recall. `group_add_cap` preserves member order, rejects duplicate membership, caps an additive group at nine and checks that the tenth robot receives no logic-group assignment. | Real click/rectangle geometry and overlap, additive mouse selection, mixed entity precedence, exclusion of flyers from the ordinary selection routes, empty-number-group and repeated camera-recall variants. Existing `game.selection.*` tests cover rectangle scheduling/cancellation and the 30-result cap using substituted hit results. |
| CMD-02 | `move_routes` compares keyboard-preorder, UI-button-preorder and context move through arrival. `stop_key` proves S clears selected robot order pools; `stop_button` proves the UI stop retains the unit orders assigned by `TaktPL`. Both prevent completion of the superseded move. | Map edges, minimap clicks, mixed chassis, non-commandable states and independently selected groups. Shared `game.scenario.replacement` and `rejected_command` retain their narrower existing scope. |
| CMD-03 | `preorder_cancellation` cancels move/attack/capture/patrol with X, then supplies a late click and proves the previous move remains unchanged and completes. `capability_rejection` rejects keyboard bomb/repair preorders for unarmed chassis and retains a capture preorder on invalid ground. `attack_routes` accepts explicit ground and friendly-object attack, replaces them with enemy attack and finally movement without retaining the attack target. `patrol_reversal` observes both patrol legs and replacement with stop. | Successful building capture, repair/bomb-capable selection, actual repair/detonation, remaining target kinds and all inactive/deleted-target combinations. Existing bomb and target-removal cases remain local evidence, not substitutes for these missing routes. |
| CMD-04 | `auto_modes` enters all three modes through the keyboard, advances real map ticks and toggles them off. It preserves auto-capture's fallback to stop when there are no buildings, and verifies all auto flags clear on the active-key toggle. | Eligible capture/attack/defence targets, tactical outcomes, mixed-selection flags, group reuse and final-member/target loss in each mode. |
| CMD-05 | `manual_control` enters/leaves direct control through Enter, asserts controlled identity, speed adjustment, cleared prior orders, forward/backward order transitions and return to a ground-attack intent. `manual_focus` executes movement with simultaneous direction keys, checks forward precedence, input-reset/deactivation stopping, the alternate movement binding, retained control ownership and subsequent autonomous move completion. `controlled_unit_removal` retires the controlled robot through the production map deletion path while movement is held, then verifies cleared control ownership and an unaffected survivor. `manual_exit_held` retains the physical key state across manual exit, replaces the return-to-strategy order with an ordinary move and requires arrival without moving the unselected robot. | Armed aiming/fire/bomb, damage-driven controlled destruction, other chassis and native focus/device behavior. |
| MODE-01 | `cheat_dispatch_gate` sends KEEPALIVE and RICHIERICH through the actual form in the compiled cheats variant, asserting their effects or their suppression. `intentional_crash` isolates the intentional parser abort. Existing `game.cheats.*` covers additional parser/flag/resource contracts. | Full registered-cheat effects, spawned units, reinforcement interactions, speed timing and result/statistics transitions; these remain shared with the outcome/lifecycle suite. |
| PRES-04 | Existing settings parsing/model/runtime and input suites remain applicable. | Settings-window key consumption before gameplay and cheats is not established by this suite; it requires the presentation integration fixture. |

Rows with remaining work stay open baseline blockers. Passing this suite alone must not close the whole command leaf or authorize structural refactoring.

## Behaviors intentionally preserved

The stop key calls `SelectedGroupBreakOrders` after issuing a group stop; the corresponding UI button does not. Their resulting unit order pools therefore differ immediately, although both stop the prior journey. Tests assert the difference rather than requiring artificial route equality.

Explicit attack accepts a friendly live robot as its target. These unarmed chassis prove command acceptance and replacement only; they do not establish friendly-fire damage semantics.

Patrol's synchronous `TaktPL` switches the return flag before the first externally advanced frame. The suite observes that initial state and requires a later reversal. Auto-capture without any building changes its group intent to stop during execution; retaining `mpo_AutoCapture` indefinitely would be a behavioral change.

Input reset clears held controls and the next robot logic tick removes the manual movement order. It leaves the robot under direct control. Leaving direct control restores its speed and assigns a ground-attack intent before later player commands replace it.

The speed-boost assertion pins the accepted multiplier to `1.1f` rather than reading the production constant as its own oracle. Direct controlled-unit deletion exercises ownership retirement; it does not substitute for damage, dying-state animation or explosion tests.
