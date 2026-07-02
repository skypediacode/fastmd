param(
    [string]$Version = "v1.7.2",
    [string]$Config = "Release",
    [switch]$Clean,
    [string]$IsccPath = "C:\Program Files\Inno Setup 7\ISCC.exe"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path $PSScriptRoot -Parent
$BuildScript = Join-Path $Root "build.ps1"
$PackageScript = Join-Path $Root "scripts\package.ps1"
$PublishScript = Join-Path $Root "scripts\publish.ps1"
$InstallerScript = Join-Path $Root "fastmd.iss"

if (!(Test-Path $BuildScript)) { throw "build.ps1 not found at $BuildScript" }
if (!(Test-Path $PackageScript)) { throw "package.ps1 not found at $PackageScript" }
if (!(Test-Path $PublishScript)) { throw "publish.ps1 not found at $PublishScript" }
if (!(Test-Path $InstallerScript)) { throw "fastmd.iss not found at $InstallerScript" }

if ([System.IO.Path]::IsPathRooted($IsccPath)) {
    if (!(Test-Path $IsccPath)) { throw "Inno Setup compiler not found at $IsccPath" }
    $IsccExe = $IsccPath
} else {
    $isccCommand = Get-Command $IsccPath -ErrorAction SilentlyContinue
    if (!$isccCommand) {
        throw "Inno Setup compiler '$IsccPath' not found in PATH"
    }
    $IsccExe = $isccCommand.Source
}

Write-Host "[1/4] Building app..." -ForegroundColor Cyan
Push-Location $Root
try {
    if ($Clean) {
        & $BuildScript -Config $Config -Clean
    } else {
        & $BuildScript -Config $Config
    }
    if ($LASTEXITCODE -ne 0) { throw "Build step failed" }

    Write-Host "[2/4] Creating portable package..." -ForegroundColor Cyan
    & $PackageScript
    if ($LASTEXITCODE -ne 0) { throw "Package step failed" }

    Write-Host "[3/4] Compiling installer with Inno Setup..." -ForegroundColor Cyan
    & $IsccExe $InstallerScript
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed" }

    Write-Host "[4/4] Publishing GitHub release..." -ForegroundColor Cyan
    & $PublishScript -Version $Version
    if ($LASTEXITCODE -ne 0) { throw "Publish step failed" }
}
finally {
    Pop-Location
}

Write-Host "Release pipeline completed successfully." -ForegroundColor Green
