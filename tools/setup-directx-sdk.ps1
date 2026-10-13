# MatrixGame - licensed under GPLv2 or any later version.
# Prepares the DirectX SDK (June 2010) headers and x86 libraries needed by MSVC builds.
[CmdletBinding()]
param([string]$ArchivePath = '')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download-verified.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$sdkParent = Join-Path $repoRoot '.tools/directx-sdk'
$sdkRoot = Join-Path $sdkParent 'DXSDK'
$archiveUrl = 'https://download.microsoft.com/download/a/e/7/ae743f1f-632b-4809-87a9-aa1bb3458e31/DXSDK_Jun10.exe'
$archiveHash = '705271DC83BFEE54D9B94E028426E288D5F070784B7446D164F48ECFBB2A02CB'
# Get-DirectoryHash of the extracted DXSDK/Include and DXSDK/Lib/x86 subset.
$subsetHash = '0D0A95D446C66D7B22CACA3BD6744D84C6983F1FA861290E382949104AA4DEC3'

if (Test-Path -LiteralPath $sdkParent) {
    if ((Test-Path -LiteralPath $sdkRoot) -and (Get-DirectoryHash -Root $sdkRoot) -eq $subsetHash) {
        Write-Output "DirectX SDK subset already prepared: $sdkRoot"
        return
    }
    Write-Warning 'Discarding a DirectX SDK subset that does not match the pinned extraction.'
    Remove-Item -LiteralPath $sdkParent -Recurse -Force
}

$extractor = Get-Command 7z -ErrorAction SilentlyContinue
if (-not $extractor) { throw '7-Zip is required to prepare the DirectX SDK subset.' }
$downloaded = -not $ArchivePath
if ($downloaded) {
    # Kept apart from .tools/downloads so the large installer never enters that cache.
    $ArchivePath = Join-Path $repoRoot '.tools/directx-sdk-archive/DXSDK_Jun10.exe'
    Save-VerifiedDownload -Uri $archiveUrl -Path $ArchivePath -Sha256 $archiveHash
}
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'DirectX SDK archive checksum mismatch. The archive was not extracted.'
}

$staging = Join-Path $repoRoot '.tools/directx-sdk.partial'
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
& $extractor.Source x $ArchivePath 'DXSDK/Include' 'DXSDK/Lib/x86' "-o$staging" -y -bso0 -bsp0
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    throw 'DirectX SDK extraction failed.'
}
if ((Get-DirectoryHash -Root (Join-Path $staging 'DXSDK')) -ne $subsetHash) {
    Remove-Item -LiteralPath $staging -Recurse -Force
    throw 'Extracted DirectX SDK files do not match the pinned subset.'
}
Move-Item -LiteralPath $staging -Destination $sdkParent
if ($downloaded) { Remove-Item -LiteralPath (Split-Path -Parent $ArchivePath) -Recurse -Force }
Write-Output "DirectX SDK subset prepared: $sdkRoot"
