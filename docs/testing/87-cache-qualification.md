# Qualify a map's minimap-cache key

Procedure version: **1**. Use this once for a map without a reviewed cache-key record, before its P1-P4 cases in [the presentation procedure](87-native-procedures.md). Qualification is a separate Debug launch, not a visual pass. It supplies the exact key for subsequent Debug/Release records of the same map/resource identity. It adds no map reader, production option or game-state override.

The game normally uses Documents `SpaceRangersHD/Cache`; failed Documents lookup uses the game's own `Cache` directory. Before launch, copy both possible cache trees into the record's private backup. The source trees stay in place. After the Debug log identifies the exact entry, return only that entry to its prior state. Other games using either location must stay closed from preparation through restoration. The launch guard detects standalone `MatrixGame` processes; it cannot detect an original game with a different process name or prevent a later concurrent launch. Keep that operator check in force. Do not upload the backup's cached images.

## Copy the pre-launch state

First run the main guide's path setup and settings preparation. They supply absolute `$game`/`$exe`, `$caseBackup`, `$cacheRoots` (objects with `Name` and absolute `Path`), a fresh launch state and `$configuration`. Run each fenced block whole, including `. {` and the final `}`. It is one statement even in a console that executes pasted lines separately; do not paste the next block until the current one completes successfully.

```powershell
. {
    $ErrorActionPreference = 'Stop'
    if ($configuration -ne 'Debug' -or -not $settingsReady) { throw 'Qualification requires prepared Debug settings.' }
    if ($cacheActive -or $cacheReady -or $cacheQualificationActive) { throw 'Restore the previous cache preparation first.' }
    $nextSnapshot = Join-Path $caseBackup 'cache-qualification'
    if (Test-Path -LiteralPath $nextSnapshot) { throw 'Do not overwrite an earlier snapshot.' }
    $cacheQualificationActive = $true
    $cacheQualificationReady = $false
    $qualifiedCacheName = $null
    $qualificationRecordPath = $null
    $qualificationCopies = [Collections.Generic.List[object]]::new()
    $qualificationEntries = [Collections.Generic.List[object]]::new()
    $qualificationRoots = [Collections.Generic.List[object]]::new()
    $qualificationSnapshot = $nextSnapshot
    New-Item -ItemType Directory -Path $qualificationSnapshot -ErrorAction Stop | Out-Null
    foreach ($root in $cacheRoots) {
        $savedRoot = Join-Path $qualificationSnapshot $root.Name
        New-Item -ItemType Directory -Path $savedRoot -ErrorAction Stop | Out-Null
        $rootState = [pscustomobject]@{Path=$root.Path; Saved=$savedRoot; Present=(Test-Path -LiteralPath $root.Path)
            Attributes=$null; SavedAttributes=[IO.File]::GetAttributes($savedRoot)}
        $qualificationRoots.Add($rootState)
        if (-not $rootState.Present) { continue }
        $sourceRoot = Get-Item -LiteralPath $root.Path -Force -ErrorAction Stop
        if (-not $sourceRoot.PSIsContainer -or ($sourceRoot.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unexpected cache root; preserve the snapshot and inspect it.'
        }
        $rootState.Attributes = $sourceRoot.Attributes
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($root.Path)
        while ($pending.Count) {
            foreach ($entry in Get-ChildItem -LiteralPath $pending.Dequeue() -Force -ErrorAction Stop) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cache links need separate qualification.' }
                $relative = $entry.FullName.Substring($root.Path.Length + 1)
                $copy = Join-Path $savedRoot $relative
                if ($entry.PSIsContainer) {
                    New-Item -ItemType Directory -Path $copy -ErrorAction Stop | Out-Null
                    $qualificationEntries.Add([pscustomobject]@{
                        Path=$entry.FullName; Saved=$copy; Directory=$true; Hash=$null
                        Attributes=$entry.Attributes; SavedAttributes=[IO.File]::GetAttributes($copy)
                    })
                    $pending.Enqueue($entry.FullName)
                } else {
                    $hash = (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256 -ErrorAction Stop).Hash
                    [IO.File]::Copy($entry.FullName, $copy, $false)
                    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256 -ErrorAction Stop).Hash -ne $hash) {
                        throw 'Cache snapshot differs; do not launch.'
                    }
                    $copied = [pscustomobject]@{Path=$entry.FullName; Saved=$copy; Directory=$false; Hash=$hash
                        Attributes=$entry.Attributes; SavedAttributes=[IO.File]::GetAttributes($copy)}
                    $qualificationCopies.Add($copied)
                    $qualificationEntries.Add($copied)
                }
            }
        }
    }
    $cacheQualificationReady = $true
}
```

This copy can take space proportional to the existing caches. A failed copy cannot authorize launch: the ready flag remains false. It has not parked or changed the originals. If no launch was attempted, abort the copy with the final block below, restore settings and use the main guide's guarded backup cleanup. Do not delete an unresolved snapshot after a launch.

## Perform the separate Debug qualification

Use the main guide's launch block; it permits qualification only after the snapshot is complete and only in Debug. Choose the one intended installed map/bonus, reach its introduction within 60 seconds, dismiss it and inspect the minimap. Do not select another map, Restart or take screenshots during qualification. Quit normally, require exit 0, then run the separate post-quit/log-excerpt block. Its `$newLog` contains only bytes appended by this launch. Qualification does not replace any P1-P4 observation.

A procedure using the supported `playtest-outcomes.ps1` may instead retain the actual child PowerShell helper process. That adapter must protect settings first, retain the helper handle, require its exit 0, verify that its `.native-outcome-backup` is gone and that no MatrixGame process remains, and only then mark launch completion for restoration. Helper exit 0 does not establish native exit 0: the ordinary route prints `Exit code: N`, while `-WatchRelaunches` prints `Process ID: <pid>; exit code: N`. Qualification permits exactly one owned game process, with `-ExpectedProcesses 1` for monitoring. Parse exactly one matching native exit row from that invocation's stdout, require native exit 0 and set `$gameExitCode` to that value. Do not take it from the helper's `ExitCode`. The helper owns/verifies its game-process handles internally. A fake process object, an unresolved helper error or a still-running game cannot satisfy the adapter. Before invoking the helper, clear `$newLog`, `$newLogProcessId` and `$gameExitCode` and capture the game's append-only log offset. Only after the owned helper has exited and that invocation's log excerpt is completely read may the adapter bind `$newLogProcessId = $process.Id`, using the retained helper handle. Use the same staged map filename and command that the later record uses; a renamed copy has a different cache key even if its contents match.

Debug logs `Found minimap cache file at` or `Storing minimap into cache file at`. Release suppresses both debug messages, so it cannot discover a key this way. Both messages name the path before the corresponding read/write; a reported write failure is not a successful cache or rendering check. If no path is reported, use [no-path recovery](#recover-a-launch-with-no-cache-path) after the owned process has exited. Multiple paths or an unverified log still require investigation with the snapshot retained; do not guess `.0` or delete a wildcard of entries.

```powershell
. {
    $ErrorActionPreference = 'Stop'
    $qualifiedCacheName = $null
    if (-not $cacheQualificationReady -or -not $launchAttempted -or -not $launchSucceeded -or $null -eq $process -or -not $process.HasExited) {
        throw 'Finish the owned qualification launch and log excerpt first.'
    }
    if ($null -eq $newLog -or $null -eq $newLogProcessId -or $newLogProcessId -ne $process.Id -or $null -eq $gameExitCode) {
        throw 'Read and bind the current owned launch log and native exit code before qualification.'
    }
    $reported = @([regex]::Matches($newLog, '(?m)\|DEBUG\| (?:Found minimap cache file at|Storing minimap into cache file at) ([^|\r\n]+?) \| [^\r\n]+:\d+\r?$') |
        ForEach-Object {
            $path = $_.Groups[1].Value
            if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $game $path }
            [IO.Path]::GetFullPath($path)
        } | Select-Object -Unique)
    if ($reported.Count -ne 1) { throw 'No single confirmed cache path; preserve the snapshot.' }
    $selected = $reported[0]
    $matchingRoots = @($cacheRoots | Where-Object {
        $selected.StartsWith($_.Path + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matchingRoots.Count -ne 1) { throw 'Reported output is outside the protected roots; do not remove it.' }
    $qualificationBefore = @($qualificationCopies | Where-Object { $_.Path -ieq $selected })
    if ($qualificationBefore.Count -gt 1) { throw 'Ambiguous snapshot entry.' }
    if ($qualificationBefore.Count -eq 1 -and
        (Get-FileHash -LiteralPath $qualificationBefore[0].Saved -Algorithm SHA256 -ErrorAction Stop).Hash -ne $qualificationBefore[0].Hash) {
        throw 'Original snapshot changed; preserve it.'
    }
    if (Test-Path -LiteralPath $selected) {
        if (-not (Test-Path -LiteralPath $selected -PathType Leaf)) { throw 'Unexpected cache directory; stop.' }
        Remove-Item -LiteralPath $selected -Force -ErrorAction Stop
    }
    if ($qualificationBefore.Count -eq 1) {
        [IO.File]::Copy($qualificationBefore[0].Saved, $selected, $false)
        [IO.File]::SetAttributes($selected, $qualificationBefore[0].Attributes)
        if ((Get-FileHash -LiteralPath $selected -Algorithm SHA256 -ErrorAction Stop).Hash -ne $qualificationBefore[0].Hash -or
            [IO.File]::GetAttributes($selected) -ne $qualificationBefore[0].Attributes) { throw 'Cache restoration differs.' }
    } elseif (Test-Path -LiteralPath $selected) { throw 'Original cache absence was not restored.' }
    $observedCacheName = $selected.Substring($matchingRoots[0].Path.Length + 1)
    $qualificationExitCode = $gameExitCode
    $qualificationRecord = [ordered]@{
        procedure='87-cache-qualification.md'; version=1; cacheName=$observedCacheName
        exitCode=$qualificationExitCode; qualified=($null -ne $qualificationExitCode -and $qualificationExitCode -eq 0)
        logProcessId=$newLogProcessId
        observedRoot=$matchingRoots[0].Name; debugBinarySHA256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    }
    $qualificationJson = $qualificationRecord | ConvertTo-Json
    $qualificationRecordPath = Join-Path $game ('playtest-cache-key-' + [guid]::NewGuid().ToString('N') + '.json')
    $keyStream = [IO.File]::Open($qualificationRecordPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $keyWriter = $null
    try {
        $keyWriter = [IO.StreamWriter]::new($keyStream, [Text.UTF8Encoding]::new($false))
        $keyWriter.Write($qualificationJson)
    } finally {
        if ($keyWriter) { $keyWriter.Dispose() } else { $keyStream.Dispose() }
    }
    $qualificationRecordPath
    $qualificationJson
    $cacheQualificationReady = $false
    $cacheQualificationActive = $false
    $qualifiedCacheName = $null
    if (-not $qualificationRecord.qualified) { throw 'Cache restored, but qualification did not exit 0; do not reuse its key as accepted evidence.' }
    $qualifiedCacheName = $observedCacheName
}
```

The block prints the JSON and writes a uniquely named `playtest-cache-key-<id>.json` beside the executable, outside the recursively cleaned backup. Retain that exact record with the installed filename, map/resource identities and Debug execution evidence; cleanup leaves it intact. Require its successful qualification and the stated scene observations before reuse; a nonzero native exit still restores the known entry but cannot supply accepted qualification evidence. The name keeps the map extension and uppercase unpadded hexadecimal uniqueness and does not depend on Debug/Release. Reuse it only with those same inputs. If the reported relative name has path separators, restoration above still protects that exact entry, but the main guide's leaf-key recipe needs separate review for that unusual map path. Leave those cases pending rather than flattening the path. The same applies to links, missing output access or an ambiguous log.

Restore settings using the main guide, then run its guarded record cleanup. That removes the copied snapshot only after the original selected entry is restored; it leaves all unrelated caches and screenshots in place. Shared cache directories created by the game may remain, as in the existing [special-scene procedure](85-native-special-scenes.md#preserve-and-restore-the-minimap-cache).

## Recover a launch with no cache path

A startup-menu quit or failure before minimap preparation can leave the bound excerpt without a cache message. The following block permits ending that record only when both original trees and their snapshots still match the complete recorded file/directory inventories, hashes and attributes. It changes no cache file, records no key and supplies no accepted qualification or native observation. A Release binary mislabeled as Debug cannot qualify here; any unreported cache change keeps the snapshot active for inspection. Missing, added, changed or linked entries also refuse recovery. Do not use this route for multiple paths or a skipped/failed post-quit log read.

```powershell
. {
    $ErrorActionPreference = 'Stop'
    if (-not $cacheQualificationActive -or -not $cacheQualificationReady -or -not $launchAttempted -or
        -not $launchSucceeded -or $null -eq $process -or -not $process.HasExited) { throw 'Finish the owned launch before no-path recovery.' }
    if ($null -eq $newLog -or $null -eq $newLogProcessId -or $newLogProcessId -ne $process.Id -or $null -eq $gameExitCode) {
        throw 'Require the current owned log excerpt and native exit code.'
    }
    if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) { throw 'Close all MatrixGame processes before recovery.' }
    if ($newLog -match '(?m)\|DEBUG\| (?:Found minimap cache file at|Storing minimap into cache file at) ') {
        throw 'A cache message was reported; use exact-key restoration or retain the ambiguous snapshot.'
    }
    if ($qualificationRoots.Count -ne 2 -or $cacheRoots.Count -ne 2) { throw 'Incomplete two-root snapshot record.' }
    foreach ($root in $qualificationRoots) {
        if (@($cacheRoots | Where-Object { $_.Path -ieq $root.Path }).Count -ne 1) { throw 'Snapshot roots no longer match path setup.' }
        $before = @($qualificationEntries | Where-Object {
            $_.Path.StartsWith($root.Path + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
        })
        foreach ($saved in @($false, $true)) {
            $treePath = if ($saved) { $root.Saved } else { $root.Path }
            $present = if ($saved) { $true } else { $root.Present }
            if (-not $present) {
                if (Test-Path -LiteralPath $treePath) { throw 'Originally absent cache root now exists; retain the snapshot.' }
                continue
            }
            $tree = Get-Item -LiteralPath $treePath -Force -ErrorAction Stop
            $attributes = if ($saved) { $root.SavedAttributes } else { $root.Attributes }
            if (-not $tree.PSIsContainer -or ($tree.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                $tree.Attributes -ne $attributes) { throw 'Cache root or snapshot differs.' }
            $expected = @($before | ForEach-Object { if ($saved) { $_.Saved } else { $_.Path } })
            $seen = 0
            $pending = [Collections.Generic.Queue[string]]::new()
            $pending.Enqueue($treePath)
            while ($pending.Count) {
                foreach ($entry in Get-ChildItem -LiteralPath $pending.Dequeue() -Force -ErrorAction Stop) {
                    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $expected -notcontains $entry.FullName) {
                        throw 'Unrecorded or linked cache/snapshot entry; retain both trees.'
                    }
                    $seen++
                    if ($entry.PSIsContainer) { $pending.Enqueue($entry.FullName) }
                }
            }
            if ($seen -ne $before.Count) { throw 'Cache or snapshot inventory is incomplete.' }
            foreach ($entry in $before) {
                $path = if ($saved) { $entry.Saved } else { $entry.Path }
                $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
                $attributes = if ($saved) { $entry.SavedAttributes } else { $entry.Attributes }
                if ($item.PSIsContainer -ne $entry.Directory -or $item.Attributes -ne $attributes) { throw 'Cache/snapshot type or attributes changed.' }
                if (-not $entry.Directory -and
                    (Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash) { throw 'Cache or snapshot bytes changed.' }
            }
        }
    }
    $qualifiedCacheName = $null
    $qualificationRecordPath = $null
    $cacheQualificationReady = $false
    $cacheQualificationActive = $false
    'No cache path: verified unchanged trees; record remains unqualified. Native exit code: ' + $gameExitCode
}
```

After this block succeeds, retain the exit/log and failed or aborted observation, restore settings and use guarded record cleanup. No cache-key JSON is created. Prepare a fresh Debug qualification record before trying the intended map again. If any check fails, preserve the active snapshot and resolve the discrepancy; do not reset its flags manually.

For a snapshot that failed before any launch, this is the only abort path; it changes no original cache file:

```powershell
. {
    if ($launchAttempted -or $launchSucceeded) { throw 'A launch was attempted; retain the snapshot for recovery.' }
    $cacheQualificationReady = $false
    $cacheQualificationActive = $false
}
```

No qualification, native run or cache operation is reported as performed by this document. Unavailable inputs/hardware or an unresolved key remain explicit preparation/execution gaps under #191; they are not a waiver of the catalog.
