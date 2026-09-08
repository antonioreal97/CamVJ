# Build a portable Windows x64 ZIP from the Release output.
#
# Renames atem_fx.exe -> CamVJ.exe only in the distribution folder. DeckLink
# discovery stays off (default build). Packages are unsigned.
#
# Usage (from repo root, after a Release build on Windows):
#   powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 -BuildDir build -Version 0.1.0
#   powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1 -SkipSmoke

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Version = "",
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

if (-not $Version) {
    $cmake = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
    if ($cmake -match 'project\(AtemFx VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    }
}
if (-not $Version) {
    throw "could not read PROJECT_VERSION from CMakeLists.txt; pass -Version"
}

$BuildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $Root $BuildDir
}

$SrcExe = Join-Path $BuildPath "bin\Release\atem_fx.exe"
$SrcShaders = Join-Path $BuildPath "bin\Release\shaders"
if (-not (Test-Path $SrcExe)) {
    # Single-config generators put the exe directly under bin/
    $altExe = Join-Path $BuildPath "bin\atem_fx.exe"
    $altShaders = Join-Path $BuildPath "bin\shaders"
    if (Test-Path $altExe) {
        $SrcExe = $altExe
        $SrcShaders = $altShaders
    }
}

if (-not (Test-Path $SrcExe)) {
    throw "missing $SrcExe — build Release first (Visual Studio 2022 x64)."
}
if (-not (Test-Path $SrcShaders)) {
    throw "missing $SrcShaders — rebuild so atem_fx_shaders copies shaders beside the exe."
}

$DistDir = Join-Path $Root "dist"
$StageDir = Join-Path $DistDir "windows\CamVJ"
$ZipName = "CamVJ-$Version-windows-x64.zip"
$ZipPath = Join-Path $DistDir $ZipName

if (Test-Path (Join-Path $DistDir "windows")) {
    Remove-Item -Recurse -Force (Join-Path $DistDir "windows")
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item -Path $SrcExe -Destination (Join-Path $StageDir "CamVJ.exe")
Copy-Item -Path $SrcShaders -Destination (Join-Path $StageDir "shaders") -Recurse

if (-not $SkipSmoke) {
    Write-Host "smoke: headless 200 frames from stage..."
    & (Join-Path $StageDir "CamVJ.exe") --headless --frames 200
    if ($LASTEXITCODE -ne 0) {
        throw "headless smoke failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}
if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal

Write-Host "wrote $ZipPath"
Get-Item $ZipPath | Format-List FullName, Length
