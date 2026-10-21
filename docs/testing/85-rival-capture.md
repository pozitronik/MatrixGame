# Armed rivals capturing a factory

`game.outcome.rival_armed_capture` supplies the rival portion of O1 in the [outcome completion plan](85-outcome-completion.md). It runs production perception, movement, weapon effects and damage, robot death, factory capture and income in one bounded scene. The shared guarded-base portion remains owned by #84 A3; this case does not replace it or establish complete-baseline acceptance.

## Scene and inputs

A neutral titanium factory stands at (300, 300). A player robot starts at (180, 280), an enemy robot at (420, 320). Both have generated armor/laser geometry and 20 displayed hit points. The configured laser has a 180-unit range, a 100 ms cooldown and a robot damage value of 20. Production hit-point conversion makes an ordinary hit remove two displayed points. Unarmed robots at (50, 600) and (600, 50) remain alive with zero movement speed, preventing this local encounter from ending the battle. Both sides start with zero resources; the factory pays ten titanium per configured 1000 ms period after acquisition.

The player context command enters `OnRButtonDown` with one robot selected directly by the fixture and the factory as the supplied trace target, creating the group's Capture order. The opposing robot receives a direct production `CaptureFactory` order as a declared scenario input. This establishes execution of that order, not the strategic AI's decision to issue it. Target memory, firing orders, hits, movement and capture progress are never injected. Neither rival is deleted or directly damaged by the test.

The fixture retains buildings before robots in the logic list, matching the loader's ordering. It then registers the two rivals as player-first or enemy-first in both the logic list and spatial groups, followed by the two sentinels. Allocation/UID creation and command-submission order remain unchanged. These are two explicit registration profiles, not a claim about every possible map-loader ordering.

Both profiles run with seed 85, Park-Miller and LegacyCRT, and separate 10/16 ms schedules. Each scene has a 4000-frame bound and the common fixture's wall-clock limit. A fresh session repeats each profile and compares the recorded battle snapshots plus factory ownership, paint/color, capturer side, health and income. Geometry, effects, debris, crater properties and score billboards are synthetic; audio is unavailable, and rendering uses the existing storage device without drawing. No original game assets or window are required.

## Observed sequence

Both rivals naturally acquire the other as a combat target, acquire firing orders and receive production weapon damage. The registration order changes the winner in this particular scene. The tests preserve each result separately instead of requiring both orders to agree.

| Registration profile | Step | First paint | Losing robot dies | Factory transfers | First income |
| --- | --- | --- | --- | --- | --- |
| Player first; player wins | 10 ms | 1650 | 3000 | 8550 | 9550 |
| Player first; player wins | 16 ms | 1664 | 3024 | 8560 | 9568 |
| Enemy first; enemy wins | 10 ms | 2160 | 1910 | 9060 | 10060 |
| Enemy first; enemy wins | 16 ms | 2176 | 1920 | 9072 | 10080 |

All times are simulation milliseconds. Per-tick assertions include the last boundary before paint, death, retirement, ownership and income. Before ownership transfers the factory already has all fourteen paint points; the following capture threshold changes its owner. The final paint is green for a player victory and red for an enemy victory. Only the winner receives titanium, and all other resource balances stay at zero. The case continues for just over two seconds after capture, observing two payments and stable ownership.

The losing robot receives ten damage-bearing tick observations. The surviving player receives eight; the surviving enemy receives nine. The test also records first target acquisition and the number of boundaries with a `ROT_FIRE` order. These are firing-order observations, not a shot counter: the weapon's transient fire flag has already been consumed by the completed map tick. Kill statistics award exactly one robot kill to the winning side and none to the other side.

Death and retirement are separate transitions. At death, the winner's target/attack references and enemy memory are clear. The losing object's retained core remains valid during its falling-apart sequence and becomes null only at the measured retirement boundary:

| Losing robot / RNG | 10 ms | 16 ms |
| --- | --- | --- |
| Enemy / Park-Miller | 7320 | 7360 |
| Enemy / LegacyCRT | 6580 | 6608 |
| Player / Park-Miller | 4830 | 4848 |
| Player / LegacyCRT | 6250 | 6272 |

At ownership transfer, the surviving robot's capture order and the factory's capturer reference are released. A surviving player group becomes Stop with a null objective. When the player loses its only group member, the group count becomes zero and its dormant Capture order still references the live factory. `TaktPL` skips empty groups; the fixture records that retained state rather than requiring an unrelated cleanup change. Both sentinels survive.

## Reading the capture references

The factory's `m_Capturer` is set by execution of a robot's capture order in [MatrixRobot.cpp](../../MatrixGame/src/MatrixRobot.cpp). For non-base factories, [MatrixObjectBuilding.cpp](../../MatrixGame/src/MatrixObjectBuilding.cpp) also periodically finds a nearby robot and calls `Capture`; that function performs its own nearest-caller eligibility check. The stored capturer and the robot responsible for a paint point can therefore differ. At the first paint boundary in this scene, the stored capturer is the enemy in both registration profiles, while the paint color already belongs to the eventual winner. The assertions keep these observations separate.

The source establishes why the marker is not exclusive ownership of the painting operation. The two measured profiles establish order sensitivity in this bounded scene; they do not isolate one universal cause for order dependence or prescribe a gameplay correction.

## Validation

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.outcome\.rival_armed_capture$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.outcome\.rival_armed_capture$'
```

The Debug and Release standalone profiles pass locally with the pinned MinGW toolchain. A negative control replaced the production robot kill-credit increment with a zero increment: the case failed at its per-tick kill-statistics assertion. A second negative control changed the scenario seed from 85 to 86 without changing its frozen expectations: it failed at the losing core's retirement boundary. Both mutations were removed and the restored focused tests rerun. `MG_CHECK` assertions and replay comparison remain active in Release. Supported compiler and x86 DLL validation remains for CI; native rendering/input, historical replay and full baseline acceptance remain separate validation obligations.
