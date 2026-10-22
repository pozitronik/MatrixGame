# Native selector and relaunch procedures

Procedure version: **2**. These cases prepare the bounded ordinary-map portions of NV-06-2. They share the [native timing/session setup](85-native-outcomes.md#build-and-preserve-the-candidate), including the build, source/binary identity, resource/configuration profiles and restoration rules. Native execution remains pending. Natural victory/defeat and NV-06-3's pending-result trigger remain explicitly unprepared below.

## Inputs and process observations

The first map is `Matrix\Map\desert1_3e.cmap`. Use the common startup steps in [the session guide](85-native-outcomes.md#common-launch-and-scene): startup New battle, the remembered map, bonus None, introduction, then a successful `M` movement order. Use a reachable destination and act while movement remains pending. `Escape`, then `N`, opens the battle-menu selector; the Settings window has no map-selection row.

Same-map and Cancel controls need no additional map. Different-map cases require an owned, already extracted `desert2_2e.cmap` from the ordinary original maps, with its shared resources available in the local package. Supply its actual path; no map data is bundled or downloaded by this helper. Copying the first map under another filename does not establish the different-payload case.

```powershell
$alternate = Read-Host 'Full path to your owned desert2_2e.cmap'
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -AlternateMapFile $alternate
```

Inspection prints the source hash and intended temporary entry without writing or launching. During `-Launch`, the helper copies that file to `Matrix/Map/native-outcome-alternate.cmap` beside the executable. It refuses an existing file at that reserved output path, preserves the source bytes, and removes only its own copy during restoration. Newly created empty map directories are removed; existing directories and other map files remain. The alternate selector entry has no original catalog match or bundled preview, so its filename and **No preview** placeholder are expected. Renaming is appropriate here only because the selected source is an ordinary map; demo/Terron filenames select additional gameplay behavior and are outside this setup.

The selector now gives `maps/` and `DATA/maps/` priority over `Matrix/Map/`. Inspection and launch refuse a case-insensitive `desert1_3e.cmap` in either higher-priority directory, and also refuse `native-outcome-alternate.cmap` there when an alternate is requested. Rejection happens before configuration backups or staging; the helper never moves or deletes those independent maps. Move the reported output copy aside manually or use another build directory, then inspect again. Unrelated files and nested maps do not block the procedure, because discovery is nonrecursive. Keep these directories unchanged during a run. Explicit `-Restore` remains available after an interrupted run even if an overriding map was installed meanwhile.

For normal relaunches, add `-WatchRelaunches -ExpectedProcesses 2`. A fresh launch followed by one accepted map replacement must produce two observed processes. Controls with no replacement use an expected count of one. The monitor retains process handles, records every observed exit, and continues until its observed processes have ended. It polls every 250 ms and waits for one second without an observed live process before finishing. It identifies lifetimes by process ID and start time, and filters by the selected executable path; run no concurrent copy of that executable. It does not independently prove parentage or render readiness. On interruption, automatic close/kill is restricted to the exact process returned by this launch; polled processes are observed and their handles disposed without terminating them. A still-running observed or unrelated game, an indeterminate cleanup result, or a timeout after forced closure retains the configuration/map backup for explicit recovery once every game is closed. Both the graceful-close and post-kill waits are limited to five seconds.

Each wait is bounded so PowerShell can handle interruption and enter restoration; this is not a total playtest deadline. Stop and record the case when a stated step deadline is exceeded. The monitor rejects a missing/extra process count, an unavailable exit code or a nonzero observed exit. Nevertheless, polling cannot establish that every short-lived failing child was seen; a child that exits between observations can be missed. Record a launch/failure with incomplete observations as incomplete, never infer successful initialization from the parent's zero exit. These procedures are for normal transitions with a visible, usable destination battle. Exact short-lived failure tracing remains an NV-06-3 preparation gap.

For every accepted replacement, require all of these observations: the loading window appears when loading is observable; the replacement battle arrives without another startup menu; the expected map and bonus appear; the introduction is usable; a fresh robot Move works; and both parent and child eventually report code 0. Record the process IDs and the latest `test.log` launch/map/RNG sections. A disappearing parent, the correct process count or a saved map preference alone is insufficient.

Allow 30 seconds for menus/dialogs and 60 seconds for the new battlefield. A timeout is a recorded failure or blocked observation, not permission to wait indefinitely. The production loading handoff's 120-second limit is a separate automated contract. Do not race a fast load to click Hide: [selector verification](../MAPS.md#verification) identifies the gated synthetic dismissal checks. Record an unobservable loader interval honestly.

## Menu-origin cases: cheats disabled

Build and inspect the matching cheats-disabled candidate first. Repeat the applicable profiles from the native ledger; these commands show Release/Text/ParkMiller. After each run, wait for all exit records and configuration restoration before the next command.

### M1: Cancel, resume, then replace with the same map

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -WatchRelaunches -ExpectedProcesses 2 -Launch
```

1. Start the common scene. While a Move is active, press Escape, then `N`. The selector should replace the battle menu and the battle must remain paused.
2. Browse another entry if available and choose bonus Armor (`3`), without starting it. Click **Cancel**. The battle menu must return. Press Escape to resume; the existing robot should continue its original Move in the same battlefield. Browsing/canceling must not change the active battle or saved map/bonus. Verify a fresh Move.
3. Open Escape, `N` again. The current map and original bonus None should be selected. Press Escape to cancel this time, then Escape to resume. This checks both visible Cancel and keyboard Cancel from the menu origin.
4. During another Move, open Escape, `N`, keep the current map and choose None (`1`). Press Enter once. Require the normal replacement observations above. The same map starts as a fresh battlefield: starting units and the introduction return, and the preceding moved unit does not remain as an extra unit at its old destination.
5. Dismiss the introduction and verify a fresh Move. Close through Escape, `E`, Enter; inspect statistics and press Enter. Both observed processes must exit with code 0; the final Exit must not open another selector or produce a third process.

### M2: replace with the different map

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -AlternateMapFile $alternate -WatchRelaunches -ExpectedProcesses 2 -Launch
```

Start the common scene and open Escape, `N` during a Move. Navigate to **native-outcome-alternate** with the map list/page controls. Choose None (`1`), then double-click its name. Require loading continuity when observable and a usable introduction in the different battlefield, with no intervening startup menu. Confirm the log names `Matrix\Map\native-outcome-alternate.cmap` and compare the terrain/start layout with the first map; a saved filename alone is insufficient. Select a friendly robot and verify a Move. Close through menu Exit/statistics as in M1. Expect exactly two observed processes, both code 0, followed by removal of the temporary map and restoration of settings.

## Battle-end selector cases

### S0: Surrender, selector Cancel and window close

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -WatchRelaunches -ExpectedProcesses 1 -Launch
```

Start the common scene, issue a Move and press Escape, `S`, Enter while it remains pending. Inspect statistics, then press Enter once and release it. The selector must appear and remain open until a fresh action. Click Cancel; expect code 0 with one process. Repeat in a fresh launch, using Alt+F4 at the battle-end selector instead of Cancel. Again expect one process and code 0. NV-06-1 separately covers Escape Cancel. None of these is natural defeat detection.

### S1/S2: Surrender, same-map and different-map acceptance

For S1 use the M1 launch command. For S2 use the M2 command with the supplied alternate file. In either fresh battle, issue a Move, then Escape, `S`, Enter to surrender. Inspect statistics and press Enter once to reach the selector. For S1 retain the current map, choose None and press Enter once. For S2 choose **native-outcome-alternate**, choose None and click Start.

Require the complete replacement observations: no new startup menu, correct destination scene/logged path, introduction and fresh movement. Close the child through menu Exit/statistics. Each run must record exactly two observed processes, both code 0. A child whose window appears but never reaches a usable battlefield fails the readiness check even if it later exits normally.

### W1/W2: direct victory-dialog routing through a developer code

These two additional cases require a freshly rebuilt `-Cheats` candidate and `-Cheats On` in the helper. Use the same-map and different-map commands below:

```powershell
.\tools\build.ps1 -Configuration Release -Cheats
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats On -ConfigurationMode Text -WatchRelaunches -ExpectedProcesses 2 -Launch
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats On -ConfigurationMode Text -AlternateMapFile $alternate -WatchRelaunches -ExpectedProcesses 2 -Launch
```

Run the two launches separately. Start the common scene, deselect robots, keep the pointer over bare terrain and leave the developer console closed. Type `IAMLOOSER` using an English keyboard layout. Despite its spelling, this code immediately opens the **victory** dialog and sets exit state 3. Confirm that dialog, inspect the resulting statistics, then press Enter once to reach the selector. In W1 accept the same map with None; in W2 accept **native-outcome-alternate** with None. Verify destination readiness and close through menu Exit/statistics. Each run requires two observed code-0 exits.

W1/W2 establish the real victory-dialog callbacks, statistics, selector and relaunch route reached by this code. They do **not** establish natural victory, defeated-side detection or the result scheduler's pending interval. There is no equivalent supported cheat here that reliably prepares the loss dialog; Surrender is a separate exit-state-4 route. Natural win/loss preparation remains open.

### E0: Exit control

Use a fresh cheats-disabled launch with `-WatchRelaunches -ExpectedProcesses 1`. Start a Move and choose Escape, `E`, Enter, inspect statistics, then Enter. The process must exit with code 0 without a selector or child. Record this control separately from S0: the two routes deliberately differ after statistics.

## NV-06-3 trigger investigation

The current `processCheat_IAMLOOSER` in [cheats.cpp](../../MatrixGame/src/cheats.cpp) sets `g_ExitState=3` and enters `TEMPLATE_DIALOG_WIN` synchronously. It never arms `m_BeforeWinLooseDialogCount`. `STATS` opens statistics, and Surrender goes to statistics through its own callback; neither is a pending natural result. The registered developer console commands also provide no delayed-result gate.

Natural result preparation lives in [CMatrixMapLogic::Takt](../../MatrixGame/src/MatrixLogic.cpp). Its status section runs when more than 1001 simulation milliseconds have elapsed since the previous status check. A detected win/loss arms the result counter; a later status visit opens the dialog. Four-speed changes wall-clock opportunities but does not give a reliable observation of that counter. Debug's pointer-directed damage key and a sequence of fast human clicks therefore do not establish that Restart or Quit occurred inside the selected pending interval.

The required follow-up is a reloadable scene with exact survival/objective inputs plus a reviewed observation/control mechanism that demonstrates the pending state before delivering Restart/Quit. The successful-initialization work in O7 is developing a complete synthetic map/configuration; a dynamic-only restart storage fixture cannot itself be launched as a full map. Materializing the future O7 fixture may supply the scene, but it does not by itself solve the timing/observation requirement. No production test seam, new cheat or change to result timing is introduced by this procedure.

The [native failure procedure](85-native-failures.md) prepares failed Restart through a disposable header change and late selected-map initialization through an unchanged valid map plus a missing staged text interface. It requires exact diagnostics, visible child observation before acknowledging its exception and a subsequent successful launch. General malformed selected-map input and short-lived failure tracing remain missing. Merely naming a missing map tests an earlier rejection route. No NV-06-3 execution is recorded here.

## Coverage and restoration

| Selected portion | Preparation status |
| --- | --- |
| Menu-origin Cancel/resume and same-map replacement | Ready: M1 |
| Menu-origin different-map replacement | Ready when the declared owned alternate source and its shared resources are available: M2 |
| Surrender statistics, Cancel/close and same/different-map replacement | Ready with the same resource prerequisite: S0/S1/S2 |
| Victory-dialog/statistics route entered by developer code | Ready: W1/W2; direct-dialog scope only |
| Menu Exit with no selector/child | Ready: E0 |
| Natural win and natural loss through their scheduled dialogs | Missing exact repeatable scene/trigger and observations; W1/W2 and Surrender do not substitute |
| Failed reload and late text-interface initialization/recovery | Prepared separately in the [native failure procedure](85-native-failures.md); no execution recorded |
| Pending-result Restart/Quit and general malformed selected-map failure | Missing reviewed trigger/input and complete failure-process evidence; NV-06-3 remains open |

Use the same profile/result record as the session guide, adding case ID, selector origin, source/destination map identity, activation method, expected/observed process count, every captured exit and each readiness observation. After all observed processes exit, the helper restores the staged configuration and removes its own alternate-map copy. It leaves any source map and existing unrelated output maps unchanged. Abandoned-run recovery uses explicit `-Restore` after all games close; preserve independent later settings edits before that command.

The helper's synthetic contracts cover retained-process counts/exits, missing/extra/foreign processes, null or failing exits, repeated observations and PID reuse, monitor interruption cleanup with an unrelated same-path lifetime left running, bounded forced closure and retained backups on a post-kill timeout, read-only alternate inspection, temporary-map collision/restoration and abandoned or malformed recovery records. Existing MSVC output/cache selection, exact-byte backup, text/packed profiles and interruptible ordinary waits remain checked. They start no game and do not establish native rendering, map compatibility or a successful relaunch. NV-06-2 as a whole remains incomplete until its missing natural win/loss preparation is delivered and the applicable native executions are reviewed.
