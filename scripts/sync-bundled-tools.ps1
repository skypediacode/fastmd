# sync-bundled-tools.ps1
#
# THE single place to change the version of an external tool FastMD bundles.
#
# To upgrade a tool: change one number in the param block below, then run
#   .\scripts\sync-bundled-tools.ps1
# (or just run a release build - every packaging path calls this script).
#
# Everything else - downloading, extracting, rebuilding, re-pinning the
# SHA-256, and the version strings in Help > Licenses - follows from that
# number. See dev-docs/BUNDLED_TOOLS.md.
param(
    # Pandoc ships prebuilt Windows binaries, so this is a GitHub release tag.
    # Releases: https://github.com/jgm/pandoc/releases
    [string]$PandocVersion = "3.14",

    # AnyDoc is a crates.io *library*, so there is no binary to download: this
    # pins the crate that tools\anydoc-wrapper compiles against, and the
    # wrapper is rebuilt locally with cargo.
    # Versions: https://crates.io/crates/anydoc/versions
    [string]$AnyDocVersion = "0.2.5",

    [ValidateSet("all", "pandoc", "anydoc")]
    [string]$Tool = "all",

    # AnyDoc is a native executable and must match the FastMD package it will
    # be staged into. Pandoc remains x64-only and is intentionally unaffected.
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",

    # Re-fetch / rebuild even when the pinned version is already in place.
    [switch]$Force,

    # Report the latest upstream release of each tool and exit, changing
    # nothing. This is the "is there a new version?" check.
    [switch]$Check
)

$ErrorActionPreference = "Stop"
# Invoke-WebRequest is roughly an order of magnitude slower with the progress
# bar enabled, which matters for Pandoc's ~230MB archive.
$ProgressPreference = "SilentlyContinue"

$Root = Split-Path $PSScriptRoot -Parent

function Get-Stamp([string]$path) {
    if (Test-Path $path) { return (Get-Content $path -Raw).Trim() }
    return ""
}

function Set-Pin([string]$dir, [string]$version, [string]$exe) {
    Set-Content (Join-Path $dir "VERSION.txt") $version -NoNewline
    $hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content (Join-Path $dir "SHA256.txt") $hash -NoNewline
    Write-Host ("  Pinned {0} -> {1}" -f $version, $hash.Substring(0, 16)) -ForegroundColor DarkGray
}

function Sync-Pandoc {
    $dir   = Join-Path $Root "tools\pandoc"
    $exe   = Join-Path $dir "pandoc.exe"
    $stamp = Join-Path $dir "VERSION.txt"

    $installed = Get-Stamp $stamp
    # Installs predating this script have a binary but no stamp. Ask the binary
    # rather than re-downloading ~230MB just to learn what it already is.
    if (!$installed -and (Test-Path $exe)) {
        $probe = (& $exe --version 2>$null | Select-Object -First 1)
        if ($probe -match 'pandoc\s+([\d.]+)') { $installed = $Matches[1] }
    }

    if (!$Force -and (Test-Path $exe) -and $installed -eq $PandocVersion) {
        Write-Host "  Pandoc $PandocVersion already bundled." -ForegroundColor DarkGray
        if (!(Test-Path $stamp) -or !(Test-Path (Join-Path $dir "SHA256.txt"))) {
            Set-Pin $dir $PandocVersion $exe
        }
        return
    }

    $url     = "https://github.com/jgm/pandoc/releases/download/$PandocVersion/pandoc-$PandocVersion-windows-x86_64.zip"
    $zip     = Join-Path $dir "pandoc-$PandocVersion.zip"
    $extract = Join-Path $dir "_extract"

    Write-Host "  Downloading Pandoc $PandocVersion (~230MB)..." -ForegroundColor Cyan
    try {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    } catch {
        throw "Could not download $url`nCheck that Pandoc $PandocVersion exists at https://github.com/jgm/pandoc/releases"
    }

    try {
        if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        # Located by search rather than by a hardcoded folder layout, which has
        # changed across Pandoc releases before.
        $found = Get-ChildItem $extract -Recurse -Filter "pandoc.exe" | Select-Object -First 1
        if (-not $found) { throw "pandoc.exe not found inside $url" }
        Copy-Item $found.FullName -Destination $exe -Force
    } finally {
        if (Test-Path $zip)     { Remove-Item $zip -Force }
        if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    }

    $probe = (& $exe --version 2>$null | Select-Object -First 1)
    if ($probe -notmatch ('pandoc\s+' + [regex]::Escape($PandocVersion) + '\b')) {
        Write-Warning "Downloaded Pandoc reports '$probe' but $PandocVersion was requested."
    }
    Write-Host "  Pandoc $PandocVersion installed." -ForegroundColor Green
    Set-Pin $dir $PandocVersion $exe
}

function Sync-AnyDoc {
    $dir      = Join-Path $Root "tools\anydoc"
    $wrapper  = Join-Path $Root "tools\anydoc-wrapper"
    $rustTarget = if ($Architecture -eq "arm64") {
        "aarch64-pc-windows-msvc"
    } else {
        "x86_64-pc-windows-msvc"
    }
    $artifactDir = Join-Path $dir $Architecture
    $exe      = Join-Path $artifactDir "fastmd-anydoc.exe"
    $hashFile = Join-Path $artifactDir "SHA256.txt"
    $stamp    = Join-Path $dir "VERSION.txt"
    $manifest = Join-Path $wrapper "Cargo.toml"

    # The crate pin lives in Cargo.toml; this keeps it in step with the number
    # at the top of this script so there is still only one place to edit.
    $toml   = Get-Content $manifest -Raw
    $wanted = 'anydoc = "={0}"' -f $AnyDocVersion
    $repinned = $false
    if ($toml -notmatch [regex]::Escape($wanted)) {
        $updated = [regex]::Replace($toml, 'anydoc\s*=\s*"=[\d][\w.\-+]*"', $wanted)
        if ($updated -eq $toml) {
            throw "Could not find an 'anydoc = `"=x.y.z`"' dependency pin in $manifest"
        }
        Set-Content $manifest $updated -NoNewline
        Write-Host "  Pinned anydoc = $AnyDocVersion in Cargo.toml" -ForegroundColor Cyan
        $repinned = $true
    }

    # VERSION.txt is intentionally shared: it records the pinned crate version,
    # not which target was last built. When that pin changes, Cargo.toml becomes
    # newer than an old target artifact, ensuring that a following x64 then
    # Arm64 sync rebuilds both without needing duplicated version manifests.
    $manifestChangedSinceBuild = (Test-Path $exe) -and
        ((Get-Item $manifest).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc)
    if (!$Force -and !$repinned -and !$manifestChangedSinceBuild -and (Test-Path $exe) -and (Test-Path $hashFile) -and (Get-Stamp $stamp) -eq $AnyDocVersion) {
        Write-Host "  AnyDoc $Architecture wrapper $AnyDocVersion already built." -ForegroundColor DarkGray
        return
    }

    # Rust is not a FastMD build requirement: without it the importer is simply
    # absent and FastMD says so at runtime (see tools/anydoc/NOTICE.txt).
    # rustup's PATH entry only reaches shells started after the install, so fall
    # back to its default location rather than claiming Rust is missing.
    $cargo = (Get-Command cargo -ErrorAction SilentlyContinue).Source
    if (-not $cargo) {
        $fallback = Join-Path $env:USERPROFILE ".cargo\bin\cargo.exe"
        if (Test-Path $fallback) { $cargo = $fallback }
    }
    if (-not $cargo) {
        Write-Warning "Rust toolchain not found - skipping the AnyDoc wrapper. Install from https://rustup.rs and re-run to enable Import as Markdown."
        return
    }

    $rustup = (Get-Command rustup -ErrorAction SilentlyContinue).Source
    if (-not $rustup) {
        $fallback = Join-Path $env:USERPROFILE ".cargo\bin\rustup.exe"
        if (Test-Path $fallback) { $rustup = $fallback }
    }
    if (-not $rustup) {
        throw "rustup is required to verify the Rust target for AnyDoc. Install Rust with rustup, then run: rustup target add $rustTarget"
    }

    Push-Location $wrapper
    try {
        # Resolve from the wrapper directory so a rust-toolchain.toml added in
        # the future is honoured rather than accidentally checking the shell's
        # default toolchain.
        $activeToolchain = ((& $rustup show active-toolchain) -replace '\s+\(.*$', '').Trim()
    }
    finally {
        Pop-Location
    }
    if (!$activeToolchain) {
        throw "Could not determine the Rust toolchain selected for $wrapper."
    }
    $installedTargets = & $rustup target list --installed --toolchain $activeToolchain
    if ($LASTEXITCODE -ne 0 -or $installedTargets -notcontains $rustTarget) {
        throw "Rust target $rustTarget is not installed for toolchain $activeToolchain. Run: rustup target add $rustTarget --toolchain $activeToolchain"
    }

    Write-Host "  Building $Architecture AnyDoc wrapper against anydoc $AnyDocVersion..." -ForegroundColor Cyan
    & $cargo build --release --target $rustTarget --manifest-path $manifest
    if ($LASTEXITCODE -ne 0) {
        throw "cargo build failed. If anydoc $AnyDocVersion moved its API, update tools\anydoc-wrapper\src\main.rs (see dev-docs/BUNDLED_TOOLS.md)."
    }

    $built = Join-Path $wrapper "target\$rustTarget\release\fastmd-anydoc.exe"
    if (!(Test-Path $built)) { throw "cargo build succeeded but $built is missing" }
    New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
    Copy-Item $built -Destination $exe -Force
    Write-Host "  AnyDoc $Architecture wrapper $AnyDocVersion installed." -ForegroundColor Green
    Set-Content $stamp $AnyDocVersion -NoNewline
    $hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content $hashFile $hash -NoNewline
    Write-Host ("  Pinned {0} {1} -> {2}" -f $Architecture, $AnyDocVersion, $hash.Substring(0, 16)) -ForegroundColor DarkGray
}

# Keeps the Help > Licenses versions from silently rotting: they are compiled
# from this header, which is generated here and committed.
function Write-VersionHeader {
    $header = Join-Path $Root "src\BundledToolVersions.h"
    $text = @"
#pragma once

// Generated by scripts/sync-bundled-tools.ps1 - do not edit by hand.
// Change a version there and re-run it; see dev-docs/BUNDLED_TOOLS.md.

#define FASTMD_PANDOC_VERSION "$PandocVersion"
#define FASTMD_ANYDOC_VERSION "$AnyDocVersion"
"@ -replace "`r`n", "`n"

    $current = if (Test-Path $header) { (Get-Content $header -Raw) -replace "`r`n", "`n" } else { "" }
    # Only write on a real change, so an unchanged sync does not invalidate the
    # build cache for every file that includes this header.
    if ($current -ne $text) {
        Set-Content $header $text -NoNewline
        Write-Host "  Updated src\BundledToolVersions.h" -ForegroundColor Cyan
    }
}

function Get-LatestRelease([string]$repo) {
    try {
        $release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" `
            -Headers @{ "User-Agent" = "fastmd-sync-bundled-tools" }
        return $release.tag_name.TrimStart("v")
    } catch {
        return $null
    }
}

function Test-Upstream {
    Write-Host "[tools] Checking upstream releases..." -ForegroundColor Cyan
    $behind = $false
    foreach ($t in @(
        @{ Name = "Pandoc"; Repo = "jgm/pandoc";       Pinned = $PandocVersion },
        @{ Name = "AnyDoc"; Repo = "firecrawl/anydoc"; Pinned = $AnyDocVersion }
    )) {
        $latest = Get-LatestRelease $t.Repo
        if (-not $latest) {
            Write-Warning ("  {0,-7} pinned {1} - could not reach github.com/{2}" -f $t.Name, $t.Pinned, $t.Repo)
        } elseif ($latest -eq $t.Pinned) {
            Write-Host ("  {0,-7} {1} is current." -f $t.Name, $t.Pinned) -ForegroundColor Green
        } else {
            Write-Host ("  {0,-7} {1} -> {2} available." -f $t.Name, $t.Pinned, $latest) -ForegroundColor Yellow
            $behind = $true
        }
    }
    if ($behind) {
        Write-Host "`nTo upgrade: edit the version at the top of this script, then re-run it." -ForegroundColor Cyan
        Write-Host "Read the release notes first - see dev-docs/BUNDLED_TOOLS.md for what can break silently." -ForegroundColor DarkGray
    }
}

if ($Check) {
    Test-Upstream
    return
}

Write-Host "[tools] Syncing bundled tools..." -ForegroundColor Cyan
if ($Tool -eq "all" -or $Tool -eq "pandoc") { Sync-Pandoc }
if ($Tool -eq "all" -or $Tool -eq "anydoc") { Sync-AnyDoc }
Write-VersionHeader
Write-Host "[tools] Done. Commit any changes to VERSION.txt / architecture SHA256.txt files / Cargo.toml / BundledToolVersions.h." -ForegroundColor Green
