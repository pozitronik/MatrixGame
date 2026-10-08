# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param([string]$ArchivePath = '')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $repoRoot '.tools/directx-x86'
$runtimeDll = Join-Path $runtimeRoot 'd3dx9_43.dll'
$dllHash = '0B28546BE22C71834501F7D7185EDE5D79742457331C7EE09EFC14490DD64F5F'
$archiveHash = '053F76DCBB28802E23341B6A787E3B0791C0FA5C8D4D011B1044172DBF89C73B'
$archiveUrl = 'https://download.microsoft.com/download/8/4/a/84a35bf1-dafe-4ae8-82af-ad2ae20b6b14/directx_Jun2010_redist.exe'

if (Test-Path -LiteralPath $runtimeDll) {
    if ((Get-FileHash -LiteralPath $runtimeDll -Algorithm SHA256).Hash -ne $dllHash) {
        throw 'The local DirectX component does not match the pinned x86 runtime.'
    }
    Write-Output "DirectX runtime already prepared: $runtimeRoot"
    return
}

$extractor = Get-Command 7z -ErrorAction SilentlyContinue
if (-not $extractor) { throw '7-Zip is required to prepare the DirectX runtime.' }
if (-not $ArchivePath) {
    $downloadRoot = Join-Path $repoRoot '.tools/downloads'
    New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
    $ArchivePath = Join-Path $downloadRoot 'directx_Jun2010_redist.exe'
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        Invoke-WebRequest -Uri $archiveUrl -OutFile $ArchivePath -UseBasicParsing
    }
}
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'DirectX archive checksum mismatch. The archive was not extracted.'
}

New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
$cabName = 'Jun2010_d3dx9_43_x86.cab'
& $extractor.Source e $ArchivePath $cabName "-o$runtimeRoot" -y -bso0 -bsp0
if ($LASTEXITCODE -ne 0) { throw 'DirectX cabinet extraction failed.' }
& $extractor.Source e (Join-Path $runtimeRoot $cabName) 'd3dx9_43.dll' "-o$runtimeRoot" -y -bso0 -bsp0
if ($LASTEXITCODE -ne 0) { throw 'DirectX runtime extraction failed.' }
if (-not (Test-Path -LiteralPath $runtimeDll) -or
    (Get-FileHash -LiteralPath $runtimeDll -Algorithm SHA256).Hash -ne $dllHash) {
    throw 'Extracted DirectX component does not match the pinned x86 runtime.'
}
Write-Output "DirectX runtime prepared: $runtimeRoot"
