# Exhausted player command groups

The correction for #327 rejects a new assignment when all `MAX_LOGIC_GROUP` slots are occupied. `RobotToLogicGroup` and `SelGroupToLogicGroup` return -1 before touching the unavailable slot. All ten `PGOrder*` entry points reject negative and out-of-range identifiers before reading group data or executing the order. Existing live membership, orders, targets and routes are retained; cached membership counts may be recomputed from the live list.

The maintainer selected this memory-safety contract on 2026-10-24. It is not a gameplay quirk or selectable compatibility mode. Fixed robot-dependent storage is retained for this bounded correction; the source comment and deferred #329 own the future dynamic-storage work after #69's complete-baseline gate.

## Creation and availability

`HasFreePlayerLogicGroup` checks actual live same-side membership without changing cached counts or consuming RNG. SPAWN uses it before assembling or inserting a robot. If no slot exists, world objects, resources, RNG and icon-attempt count remain unchanged. A defensive failed-assignment check also retires a newly assembled robot rather than passing its failure index onward.

The existing 60-occupied-slots to final-slot-60 boundary remains unchanged. This does not establish safe normal simulation with more than `MAX_ROBOTS` robots: other AI arrays still have fixed capacities. The correction does not raise/remove the army limit or claim every creation path has been validated for larger armies.

## Synthetic fixture

`player_group_capacity_fixture.hpp` uses the existing production-linked Session and a synthetic multi-region map. It creates 61 live player robots with one occupied slot each and keeps two enemies available. Every group starts with a nontrivial Attack/Patrol order, target, destination, flags, regional data and a nonempty production-allocated road route. Tests compare all those fields, route storage/content, object identity, membership, selected objects, resources, RNG, flags and registered world observations across rejection.

No world tick advances this over-limit fixture through unrelated fixed-size side/AI buffers. Selection is supplied fixture state, not native screen picking. Road data is initial state for preservation checks; these tests do not claim the seeded orders physically complete. No game resources or interactive windows are used.

## Registered contracts

| CTest case | Required result |
| --- | --- |
| `game.player.group_capacity.robot_exhausted` | Individual assignment returns -1; passing that result to Attack does nothing and preserves existing state. |
| `game.player.group_capacity.selection_exhausted` | Assignment of the supplied selected pair returns -1; passing it to Move does nothing and preserves membership/selection and group state. |
| `game.player.group_capacity.invalid_orders` | All ten order entry points ignore -1, the first unavailable index, `INT_MIN` and `INT_MAX`, preserving the world. |
| `game.player.group_capacity.availability` | Availability follows live membership even when all cached counts say zero or all say occupied; the query itself changes nothing. |
| `game.player.group_capacity.recovery` | Removing the final-slot robot permits ordinary allocation to slot 60, later reuse of newly vacated slot zero, and separate selected-pair assignment to slot 60. |
| `game.player.spawn_cheat_exhausted` | Raw parser and real keyboard routes preserve the world when all slots are occupied. The matching build controls actual cheat admission. |

Each case covers both RNG modes and 10/16 ms profile identities. These rejection cases make synchronous calls without advancing simulation time; the profile values are not claims about observed frame timing. Existing `spawn_cheat_creation` and `spawn_cheat_capacity` retain actual under-boundary creation and last-slot coverage.

## Reproduction and protected failure control

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.player\.(group_capacity\.|spawn_cheat_)'
.\tools\test.ps1 -Configuration Release -Filter '^game\.player\.(group_capacity\.|spawn_cheat_)'
.\tools\test.ps1 -Configuration Debug -Trace -Filter '^game\.player\.(group_capacity\.|spawn_cheat_)'
```

For the old-allocation control, remove only the two new exhaustion returns in a disposable source change and build the Debug trace target. Verify `_DEBUG` and `_TRACE` are defined and `ASSERT_OFF` is absent. Run only the two `robot_exhausted`/`selection_exhausted` cases: the original checked assertion throws before either invalid index is used. The test prints the engine exception location and fails instead of accepting the missing refusal contract. Do not run this control in ordinary Debug (which breaks into a debugger) or Release (which removes the assertion).

The separate SPAWN control removes only its preflight while retaining corrected allocation/order guards. The raw-parser exhaustion case must fail preservation because unnecessary assembly/icon work occurred before the defensive rollback. It never requires the unprotected out-of-bounds write. Restore exact source bytes and rerun the passing cases after each control. Native full-capacity stress execution is not part of this validation.

MinGW Debug and Release each pass the full engine suite. Checked Debug trace and a cheats-enabled Release x86 DLL configuration also pass the eight-case focused selection. Supported MSVC and remaining profiles are validated by current-revision CI. Native presentation and complete-baseline acceptance remain #191/#69; this bounded correction does not accept either.
