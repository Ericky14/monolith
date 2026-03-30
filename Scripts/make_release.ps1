# Monolith Release Zip Builder
# Creates a release zip with "Installed": true for Blueprint-only compatibility.
# Automatically builds with optional dependencies disabled (MONOLITH_RELEASE_BUILD=1).
#
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\make_release.ps1 -Version "0.10.0"
#
# What it does:
#   1. Sets MONOLITH_RELEASE_BUILD=1 (forces BA/GBA optional deps OFF in Build.cs)
#   2. Runs UBT to produce clean release binaries
#   3. Packages tracked files + binaries into a zip with Installed=true
#   4. Unsets env var (your next dev build auto-detects deps normally)
#
# Source users (GitHub clones) are unaffected — Build.cs auto-detects at compile time.

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$PluginDir = Split-Path -Parent $PSScriptRoot
$ProjectDir = Split-Path -Parent $PluginDir
$OutputZip = Join-Path (Split-Path -Parent $PluginDir) "Monolith-v$Version.zip"
$TempDir = Join-Path $env:TEMP "Monolith_Release_$Version"
$UBT = 'C:\Program Files (x86)\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe'
$UProject = Join-Path $ProjectDir "Leviathan.uproject"

Write-Host "Building Monolith v$Version release zip..." -ForegroundColor Cyan

# --- Step 1: Build with optional deps disabled ---
if (-not $SkipBuild) {
    Write-Host "`n  [1/4] Building release binaries (optional deps OFF)..." -ForegroundColor Yellow

    # Set env var so Build.cs files skip optional dependency detection
    $env:MONOLITH_RELEASE_BUILD = "1"
    Write-Host "    MONOLITH_RELEASE_BUILD=1 (BA/GBA/ComboGraph forced off)" -ForegroundColor DarkGray

    try {
        & $UBT LeviathanEditor Win64 Development "-Project=$UProject" -waitmutex
        if ($LASTEXITCODE -ne 0) {
            throw "UBT failed with exit code $LASTEXITCODE. Is the editor closed?"
        }
        Write-Host "    Build succeeded" -ForegroundColor Green
    }
    finally {
        # Always unset — even if build fails, don't poison future dev builds
        Remove-Item Env:\MONOLITH_RELEASE_BUILD -ErrorAction SilentlyContinue
        Write-Host "    MONOLITH_RELEASE_BUILD unset" -ForegroundColor DarkGray
    }
} else {
    Write-Host "`n  [1/4] Skipping build (-SkipBuild flag)" -ForegroundColor DarkGray
    Write-Host "    WARNING: Ensure you built with MONOLITH_RELEASE_BUILD=1" -ForegroundColor Red
}

# --- Step 2: Copy tracked files ---
Write-Host "`n  [2/4] Copying tracked files..." -ForegroundColor Yellow

if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }
New-Item -ItemType Directory -Path $TempDir | Out-Null

Push-Location $PluginDir
$trackedFiles = git ls-files
foreach ($file in $trackedFiles) {
    $destPath = Join-Path $TempDir $file
    $destDir = Split-Path -Parent $destPath
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    Copy-Item $file -Destination $destPath -Force
}
Pop-Location
Write-Host "    $($trackedFiles.Count) files copied" -ForegroundColor Green

# --- Step 3: Copy binaries (gitignored but needed for Blueprint-only users) ---
Write-Host "`n  [3/4] Copying binaries..." -ForegroundColor Yellow

$binDir = Join-Path $PluginDir "Binaries"
if (Test-Path $binDir) {
    $destBin = Join-Path $TempDir "Binaries"
    New-Item -ItemType Directory -Path $destBin -Force | Out-Null
    $binCount = 0
    Get-ChildItem $binDir -Recurse -File |
        Where-Object { $_.Extension -ne '.pdb' -and $_.Name -notmatch '\.patch_' } |
        ForEach-Object {
            $rel = $_.FullName.Substring($binDir.Length)
            $dest = Join-Path $destBin $rel
            $destParent = Split-Path -Parent $dest
            if (-not (Test-Path $destParent)) { New-Item -ItemType Directory -Path $destParent -Force | Out-Null }
            Copy-Item $_.FullName -Destination $dest -Force
            $binCount++
        }
    Write-Host "    $binCount binary files included (no .pdb, no .patch_*)" -ForegroundColor Green
} else {
    Write-Host "    WARNING: No Binaries/ found - Blueprint-only users will need to compile" -ForegroundColor Red
}

# --- Step 4: Patch and package ---
Write-Host "`n  [4/4] Packaging..." -ForegroundColor Yellow

# Set Installed=true for Blueprint-only users
$upluginPath = Join-Path $TempDir "Monolith.uplugin"
$content = Get-Content $upluginPath -Raw
$content = $content -replace '"Installed":\s*false', '"Installed": true'
Set-Content $upluginPath $content -NoNewline
Write-Host "    Installed=true set in .uplugin" -ForegroundColor Green

# Create zip
if (Test-Path $OutputZip) { Remove-Item $OutputZip -Force }
Compress-Archive -Path "$TempDir\*" -DestinationPath $OutputZip -Force

# Clean temp
Remove-Item $TempDir -Recurse -Force

$fileSize = [math]::Round((Get-Item $OutputZip).Length / 1MB, 1)
Write-Host "`nRelease complete: $OutputZip" -ForegroundColor Green
Write-Host "Size: ${fileSize}MB" -ForegroundColor Green
Write-Host "`nVerify: optional deps should be OFF in the binaries." -ForegroundColor Cyan
Write-Host "  WITH_BLUEPRINT_ASSIST=0, WITH_GBA=0" -ForegroundColor Cyan
Write-Host "  Your next editor build will auto-detect deps normally." -ForegroundColor DarkGray
