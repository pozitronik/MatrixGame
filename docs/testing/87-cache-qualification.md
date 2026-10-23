# Qualify a map's minimap-cache key

Procedure version: **1**. Use this once for a map without a reviewed cache-key record, before its P1-P4 cases in [the presentation procedure](87-native-procedures.md). Qualification is a separate Debug launch, not a visual pass. It supplies the exact key for subsequent Debug/Release records of the same map/resource identity. It adds no map reader, production option or game-state override.

The game normally uses Documents `SpaceRangersHD/Cache`; failed Documents lookup uses the game's own `Cache` directory. Before launch, copy both possible cache trees into the record's private backup. The source trees stay in place. After the Debug log identifies the exact entry, return only that entry to its prior state. Other games using either location must stay closed from preparation through restoration. Do not upload the backup's cached images.

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
    $qualificationCopies = [Collections.Generic.List[object]]::new()
    $qualificationSnapshot = $nextSnapshot
    New-Item -ItemType Directory -Path $qualificationSnapshot -ErrorAction Stop | Out-Null
    foreach ($root in $cacheRoots) {
        $savedRoot = Join-Path $qualificationSnapshot $root.Name
        New-Item -ItemType Directory -Path $savedRoot -ErrorAction Stop | Out-Null
        if (-not (Test-Path -LiteralPath $root.Path)) { continue }
        $sourceRoot = Get-Item -LiteralPath $root.Path -Force -ErrorAction Stop
        if (-not $sourceRoot.PSIsContainer -or ($sourceRoot.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unexpected cache root; preserve the snapshot and inspect it.'
        }
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($root.Path)
        while ($pending.Count) {
            foreach ($entry in Get-ChildItem -LiteralPath $pending.Dequeue() -Force -ErrorAction Stop) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cache links need separate qualification.' }
                $relative = $entry.FullName.Substring($root.Path.Length + 1)
                $copy = Join-Path $savedRoot $relative
                if ($entry.PSIsContainer) {
                    New-Item -ItemType Directory -Path $copy -ErrorAction Stop | Out-Null
                    $pending.Enqueue($entry.FullName)
                } else {
                    $hash = (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256 -ErrorAction Stop).Hash
                    [IO.File]::Copy($entry.FullName, $copy, $false)
                    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256 -ErrorAction Stop).Hash -ne $hash) {
                        throw 'Cache snapshot differs; do not launch.'
                    }
                    $qualificationCopies.Add([pscustomobject]@{
                        Path=$entry.FullName; Saved=$copy; Hash=$hash; Attributes=$entry.Attributes
                    })
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

A procedure using the supported `playtest-outcomes.ps1` may instead retain the actual child PowerShell helper process. That adapter must protect settings first, retain the helper handle, require its exit 0, verify that its `.native-outcome-backup` is gone and that no MatrixGame process remains, and only then mark launch completion for restoration. The helper owns/verifies its game-process handles internally. A fake process object, an unresolved helper error or a still-running game cannot satisfy the adapter. The adapter must capture the game's append-only log offset before invoking the helper. Use the same staged map filename and command that the later record uses; a renamed copy has a different cache key even if its contents match.

Debug logs `Found minimap cache file at` or `Storing minimap into cache file at`. Release suppresses both debug messages, so it cannot discover a key this way. Both messages name the path before the corresponding read/write; a reported write failure is not a successful cache or rendering check. If no single path is reported, retain the snapshot and investigate; do not guess `.0` or delete a wildcard of entries.

```powershell
. {
    $ErrorActionPreference = 'Stop'
    if (-not $cacheQualificationReady -or -not $launchSucceeded -or $null -eq $process -or -not $process.HasExited) {
        throw 'Finish the owned qualification launch and log excerpt first.'
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
    $qualificationExitCode = $process.ExitCode
    $qualificationRecord = [ordered]@{
        procedure='87-cache-qualification.md'; version=1; cacheName=$observedCacheName
        exitCode=$qualificationExitCode; qualified=($null -ne $qualificationExitCode -and $qualificationExitCode -eq 0)
        observedRoot=$matchingRoots[0].Name; debugBinarySHA256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText((Join-Path $caseBackup 'cache-key.json'),
        ($qualificationRecord | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    $cacheQualificationReady = $false
    $cacheQualificationActive = $false
    $qualifiedCacheName = $null
    if (-not $qualificationRecord.qualified) { throw 'Cache restored, but qualification did not exit 0; do not reuse its key as accepted evidence.' }
    $qualifiedCacheName = $observedCacheName
}
```

Attach the key record to the installed filename, map/resource identities and Debug execution record before cleanup. Require its successful qualification and the stated scene observations before reuse; a nonzero exit still restores the known entry but cannot supply accepted qualification evidence. The name keeps the map extension and uppercase unpadded hexadecimal uniqueness and does not depend on Debug/Release. Reuse it only with those same inputs. If the reported relative name has path separators, restoration above still protects that exact entry, but the main guide's leaf-key recipe needs separate review for that unusual map path. Leave those cases pending rather than flattening the path. The same applies to links, missing output access or an ambiguous log.

Restore settings using the main guide, then run its guarded record cleanup. That removes the copied snapshot only after the original selected entry is restored; it leaves all unrelated caches and screenshots in place. Shared cache directories created by the game may remain, as in the existing [special-scene procedure](85-native-special-scenes.md#preserve-and-restore-the-minimap-cache).

For a snapshot that failed before any launch, this is the only abort path; it changes no original cache file:

```powershell
. {
    if ($launchAttempted -or $launchSucceeded) { throw 'A launch was attempted; retain the snapshot for recovery.' }
    $cacheQualificationReady = $false
    $cacheQualificationActive = $false
}
```

No qualification, native run or cache operation is reported as performed by this document. Unavailable inputs/hardware or an unresolved key remain explicit preparation/execution gaps under #191; they are not a waiver of the catalog.
