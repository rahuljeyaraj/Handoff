<#
.SYNOPSIS
    Configure and build the Handoff firmware from a plain terminal.

.DESCRIPTION
    The toolchain lives under ~/.pico-sdk (installed by the Raspberry Pi Pico
    VS Code extension, or by the versions pinned below) and is deliberately not
    on PATH. This script points CMake at it so a command-line build behaves
    exactly like the extension's "Compile Project".

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Clean
    .\scripts\build.ps1 -Flash        # needs the board in BOOTSEL, or already running
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Flash,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Debug'
)

$ErrorActionPreference = 'Stop'

# Keep these in step with the DO-NOT-EDIT block in CMakeLists.txt.
$SdkVersion       = '2.3.1'
$ToolchainVersion = '15_2_Rel1'
$PicotoolVersion  = '2.3.1'
$CMakeVersion     = 'v4.3.4'
$NinjaVersion     = 'v1.13.2'

$Root = Split-Path -Parent $PSScriptRoot
$Pico = Join-Path $HOME '.pico-sdk'

$Cmake    = Join-Path $Pico "cmake\$CMakeVersion\bin\cmake.exe"
$Ninja    = Join-Path $Pico "ninja\$NinjaVersion\ninja.exe"
$Picotool = Join-Path $Pico "picotool\$PicotoolVersion\picotool\picotool.exe"
$Build    = Join-Path $Root 'build'

foreach ($tool in @($Cmake, $Ninja, $Picotool)) {
    if (-not (Test-Path $tool)) {
        throw "Missing toolchain component: $tool`nOpen this folder in VS Code and let the Raspberry Pi Pico extension install SDK $SdkVersion, or see README.md."
    }
}

$env:PICO_SDK_PATH       = Join-Path $Pico "sdk\$SdkVersion"
$env:PICO_TOOLCHAIN_PATH = Join-Path $Pico "toolchain\$ToolchainVersion"

if ($Clean -and (Test-Path $Build)) {
    Write-Host "Removing $Build" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $Build
}

Write-Host "Configuring ($Config)..." -ForegroundColor Cyan
& $Cmake -S $Root -B $Build -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$Ninja" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    -Dpicotool_DIR="$(Join-Path $Pico "picotool\$PicotoolVersion\picotool")" `
    -Dpioasm_DIR="$(Join-Path $Pico "tools\$SdkVersion\pioasm")"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

Write-Host "Building..." -ForegroundColor Cyan
& $Cmake --build $Build
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

Get-ChildItem -Path $Build -Filter *.uf2 | ForEach-Object {
    Write-Host ("  {0}  {1:N1} KB" -f $_.Name, ($_.Length / 1KB)) -ForegroundColor Green
}

if ($Flash) {
    Write-Host "Flashing..." -ForegroundColor Cyan
    & $Picotool load (Join-Path $Build 'blink.uf2') -fx
    if ($LASTEXITCODE -ne 0) { throw "Flash failed ($LASTEXITCODE)" }
}
