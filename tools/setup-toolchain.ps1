# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [string]$ArchivePath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $repoRoot '.tools'
$installRoot = Join-Path $toolsRoot 'winlibs'
$compilerRoot = Join-Path $installRoot 'mingw32'
$archiveName = 'winlibs-i686-mcf-dwarf-gcc-13.1.0-mingw-w64ucrt-11.0.0-r1.7z'
$archiveUrl = "https://github.com/brechtsanders/winlibs_mingw/releases/download/13.1.0-16.0.2-11.0.0-ucrt-r1/$archiveName"
$archiveHash = 'A3989CBBCA282A35B6BA022BBD5F51F88CD3830BB081011838A95E786227DDCA'

if (Test-Path -LiteralPath (Join-Path $compilerRoot 'bin/g++.exe')) {
    Write-Output "Toolchain already installed: $compilerRoot"
    exit 0
}

$extractor = Get-Command 7z -ErrorAction SilentlyContinue
if (-not $extractor) {
    throw '7-Zip is required. Install it and add 7z.exe to PATH.'
}

if (-not $ArchivePath) {
    $downloadRoot = Join-Path $toolsRoot 'downloads'
    New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
    $ArchivePath = Join-Path $downloadRoot $archiveName
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        Write-Output 'Downloading the pinned Windows x86 toolchain...'
        Invoke-WebRequest -Uri $archiveUrl -OutFile $ArchivePath -UseBasicParsing
    }
}

$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $archiveHash) {
    throw 'Toolchain archive checksum mismatch. The archive was not extracted.'
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
& $extractor.Source x $ArchivePath "-o$installRoot" -y -bso0 -bsp0
if ($LASTEXITCODE -ne 0) { throw 'Toolchain extraction failed.' }
foreach ($program in @('gcc.exe', 'g++.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot "bin/$program"))) {
        throw "The downloaded toolchain is missing $program."
    }
}
Write-Output "Toolchain installed: $compilerRoot"
