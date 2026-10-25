# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('Text','Packed')][string]$ConfigurationMode = 'Text',
    [ValidateSet('flat','ridge')][string]$Terrain = 'ridge',
    [string]$ToolchainRoot = '',
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$destinationRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
$receipt = Join-Path $destinationRoot 'calcvis-preparation.json'
function Assert-RegularFile([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Expected a regular file: $Path" }
}
if ($VerifyOnly) {
    Assert-RegularFile $receipt
    $record = [IO.File]::ReadAllText($receipt, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($record.Version -ne 1 -or $record.Profile -notin @('calcvis-flat','calcvis-ridge') -or
        $record.Configuration -notin @('Debug','Release') -or $record.ConfigurationMode -notin @('Text','Packed') -or
        -not $record.Files -or $record.Files.Count -lt 10) { throw 'Invalid preparation receipt.' }
    $allowedRoot = [IO.Path]::GetFullPath($destinationRoot).TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    foreach ($file in $record.Files) {
        if ([IO.Path]::IsPathRooted($file.Path)) { throw 'Receipt file paths must be relative.' }
        $path = [IO.Path]::GetFullPath((Join-Path $destinationRoot $file.Path))
        if (-not $path.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt path leaves the prepared export.' }
        Assert-RegularFile $path
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $file.SHA256) { throw "Prepared file changed: $($file.Path)" }
    }
    $writer = Join-Path $destinationRoot 'CalcVisExport.exe'
    & $writer --verify-calcvis $record.Profile $destinationRoot $record.ConfigurationMode
    if ($LASTEXITCODE -ne 0) { throw 'Controlled CalcVis input verification failed.' }
    foreach ($optional in @('sound.pkg','voices.pkg','music.pkg')) {
        if (Test-Path -LiteralPath (Join-Path $destinationRoot "DATA/$optional")) { throw 'The controlled profile must remain silent.' }
    }
    Write-Output 'Prepared CalcVis files and controlled input verified. No game launched.'
    return
}
if (Test-Path -LiteralPath $destinationRoot) { throw 'Destination already exists; use a fresh export or -VerifyOnly.' }
if (-not (Test-Path -LiteralPath (Split-Path -Parent $destinationRoot) -PathType Container)) { throw 'Destination parent must already exist.' }
$buildRoot = Join-Path $repoRoot ('build/mingw-{0}-exe' -f $Configuration.ToLowerInvariant())
$cache = Join-Path $buildRoot 'CMakeCache.txt'
foreach ($expected in @("CMAKE_BUILD_TYPE:STRING=$Configuration", 'MATRIXGAME_BUILD_DLL:BOOL=OFF', 'MATRIXGAME_CHEATS:BOOL=OFF')) {
    if (-not (Select-String -LiteralPath $cache -SimpleMatch $expected -Quiet)) { throw "Build configuration differs: $expected" }
}
$writer = Join-Path $buildRoot 'tests/matrixgame_native_demo_export.exe'
$observer = Join-Path $buildRoot 'tests/matrixgame_calcvis_native_view.exe'
$game = Join-Path $buildRoot 'MatrixGame/MatrixGame.exe'
$runtime = Join-Path $repoRoot '.tools/directx-x86/d3dx9_43.dll'
foreach ($path in @($writer,$observer,$game,$runtime)) { Assert-RegularFile $path }
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $repoRoot '.tools/winlibs-13.2.0/mingw32' }
$ToolchainRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ToolchainRoot)
$toolBin = Join-Path $ToolchainRoot 'bin'
$objdump = Join-Path $toolBin 'objdump.exe'
Assert-RegularFile $objdump
$profile = "calcvis-$Terrain"
$savedPath = $env:PATH
try {
    $env:PATH = "$toolBin;$(Split-Path -Parent $runtime);$savedPath"
    Push-Location -LiteralPath (Split-Path -Parent $writer)
    try {
        & $writer --export $profile $destinationRoot $ConfigurationMode
        if ($LASTEXITCODE -ne 0) { throw 'Synthetic export failed.' }
    } finally { Pop-Location }
} finally { $env:PATH = $savedPath }
# A staging failure leaves the new export for diagnosis; no installation is modified.
Copy-Item -LiteralPath $game -Destination (Join-Path $destinationRoot 'MatrixGame.exe')
Copy-Item -LiteralPath $writer -Destination (Join-Path $destinationRoot 'CalcVisExport.exe')
Copy-Item -LiteralPath $observer -Destination (Join-Path $destinationRoot 'CalcVisObserver.exe')
Copy-Item -LiteralPath $runtime -Destination $destinationRoot
$pending = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in @('MatrixGame.exe','CalcVisExport.exe','CalcVisObserver.exe')) { $pending.Enqueue((Join-Path $destinationRoot $name)) }
while ($pending.Count) {
    $binary = $pending.Dequeue()
    if (-not $seen.Add($binary)) { continue }
    $imports = & $objdump -p $binary
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect staged imports.' }
    foreach ($line in $imports) {
        if ($line -match 'DLL Name:\s+([A-Za-z0-9_+.-]+)') {
            $dependency = Join-Path $toolBin $Matches[1]
            if (Test-Path -LiteralPath $dependency -PathType Leaf) {
                $target = Join-Path $destinationRoot $Matches[1]
                Copy-Item -LiteralPath $dependency -Destination $target -Force
                $pending.Enqueue($target)
            }
        }
    }
}
$revision = git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
$record = [ordered]@{Version=1; SourceRevision=$revision; Configuration=$Configuration; ConfigurationMode=$ConfigurationMode; Profile=$profile; Files=@()}
$prefix = $destinationRoot.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
$record.Files = @(Get-ChildItem -LiteralPath $destinationRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    Assert-RegularFile $_.FullName
    [pscustomobject]@{Path=$_.FullName.Substring($prefix.Length); SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
[IO.File]::WriteAllText($receipt, ($record | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
& $PSCommandPath -Destination $destinationRoot -VerifyOnly
Write-Output "Prepared $Configuration/$ConfigurationMode $profile at $destinationRoot. No game launched."
