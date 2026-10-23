# Road-guided journeys and destruction of a route blocker

These asset-free cases cover the selected N2 scene in the [baseline completion checklist](baseline-completion.md#navigation). They join the production regional road planner, robot zone/local searches, context commands, movement, damage and retirement in one synthetic battle. They do not change production code or establish completion of the entire navigation package.

## Run the cases

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.road_(guided_journey|destroyed_blocker)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.road_(guided_journey|destroyed_blocker)\.'
```

Add `-DLL` for the x86 DLL compile-time profile. Eight CTest cases separate the two scenario families, both RNG modes and 10/16 ms schedules. Each case runs its control and candidate twice in fresh `Session` lifetimes. Seed 81, unavailable audio and the ordinary no-draw storage device apply; the complete header records build identity and corrections. Each journey advances exactly 40 seconds and has a matching frame limit. `MG_CHECK` remains active in Release; instrumented Debug builds also require tracked allocation balance.

## Synthetic topology

`RoadRing` adds a 16-zone ring to the existing 64-by-64 movement grid. A central blocked island occupies movement cells `[16,48) × [16,48)`. The upper and lower corridors connect the left start to the right destination. Two additional walls narrow the lower corridor to four cells between Y 52 and 56; a wheel robot's four-cell footprint can pass. Zones, places, roads and two regions are declared fixture inputs, rather than generated from terrain or extracted from a game package.

```text
                     upper corridor
              3 -- 4 -- 5 -- 6
              |              |
              2   blocked    7
              |   island     |
              1              8  destination
              |              |
    start     0              9
              |              |
             15             10
              |              |
             14 --13 --12 --11
                     lower corridor
                       ^
                 narrow passage
```

Zone 0's neighbor order favors the upper corridor when the two complete zone routes tie. The first region contains zones 13–15 and 0–4; the second contains 5–12. Production `FindPathFromRegionPath` reaches the neighboring region through the shorter junction chain `0 → 15 → 14 → 13 → 12`. The resulting road list has five entries and distance 50. Its first entry identifies the starting junction without a road; the remaining entries identify real roads and their endpoint zones.

Road points use movement-footprint origins. The road between zones 12 and 13 bends through Y 52 so that a four-cell footprint fits the opening. Production `IsAbsenceWall` and `CanMoveFromTo` check every declared road point and segment for that footprint during fixture construction. The reported road distance follows production `CalcDist`, which sums zone-centre distances rather than polyline lengths.

The topology deliberately separates two production behaviors. `FindPathInZone` permits supplied road zones and two additional graph-neighbor layers. `FindLocalPath` then uses a bounding rectangle from up to six upcoming zones; it does not require every visited movement cell to belong to that zone path. Short rings therefore let the local path cross to the opposite corridor even with a road hint. Eight edges on each complete route make this fixture's guidance observable in physical travel. The test never writes a robot's zone path, local path, cursor or cached search result.

## Context command and physical route

The two selected wheel robots start at world positions (100, 360) and (100, 400). An ordinary ground context command targets (500, 280), assigning the same Move group with destination movement cell (48, 26). In the unguided control the group has no road list. In the guided candidate the fixture calls the real regional planner once after command dispatch and before the first tick. Manual Move does not populate this list by itself; this case explicitly supplies regional guidance and tests its consumption by the normal robot update.

Both robots must traverse upper interior zones 3, 4 and 5 without entering lower interior zones 12, 13 and 14 in the control, and traverse those lower zones without entering the upper ones in the candidate. The start zone must also be visited. Endpoint-adjacent zones can vary while the formation settles; their complete visit masks are reported rather than made a separate golden. Complete per-frame traces must repeat exactly within each profile, and the guided and unguided traces must differ.

Every tick checks live registered identities, finite bounded positions, the wheel centre's legal movement cell, unchanged group membership and destination, and cleared temporary zone-access marks. Both robots must arrive before 35 seconds at their distinct allocated positions, (500, 280) and (500, 240), then retain those positions with empty low-level order queues for the final five seconds. Unarmed enemy sentinels are configured with zero speed and placed at (540, 50) and (600, 50); their unchanged positions are asserted throughout. Their AI still ticks.

## Live blocker and production death

The second family uses the guided route and adds an unselected, unordered friendly wheel robot at (320, 540), in the lower passage. Its configured speed is zero and its displayed maximum health is 10. This is an ordinary physical robot: collision response may displace it, so it is not an immovable wall and the test does not claim permanent impassability.

The no-hit control leaves it alive. The candidate applies one lethal laser hit from the enemy side at exactly 6,000 ms through `CMatrixRobotAI::Damage`. Before the hit, the moving group must already have approached within 60 world units of the blocker, and its first member must have progressed but not yet passed the obstruction. Damage must report death, set `ROBOT_DIP` and award exactly one enemy kill. A retained `SObjectCore` observes the object until normal map ticks retire it; the test never calls `StaticDelete` on the blocker.

The live and lethal journeys must have identical snapshots through 6,000 ms inclusive. After the hit they must differ. In each profile both moving robots must first cross world X 360 after the hit and earlier than their counterparts in the live control, then arrive earlier as well. Both controls still have to complete their original Move. The live blocker must remain alive and registered through the entire run; the lethal candidate must naturally retire before the journey ends. Player kill credit stays zero.

Snapshots contain the runner's time, registered unit state and raw RNG draw count, plus blocker presence, state, raw position/health bits and enemy kill credit. Exact same-profile replay compares the complete traces and measured crossing/arrival/retirement observations, without requiring equality between different timesteps, RNG modes or compilers. The test's direct receiver call establishes navigation after a production combat death; it does not cover a projectile being aimed or traced into the blocker.

## Sensitivity and limits

Two temporary source controls check that assertions depend on the intended production paths. Replacing the player road-list argument in `ZonePathCalc` with a null route must fail the required corridor assertion, although the group still holds its valid regional road list. Making laser damage return success without applying death must fail the blocker state assertion. Restore the exact source bytes, rebuild and rerun all eight cases after each control.

These tests characterize no-draw wheel navigation with declared masks and a controlled laser hit. They do not establish terrain-to-mask compilation, other chassis on this ring, native drawn trajectories, internal search/avoidance cost counters, accepted historical reference traces or original-host integration. The remaining N1/N5/N6 requirements and the complete-baseline acceptance gate retain their separate evidence and review requirements.
