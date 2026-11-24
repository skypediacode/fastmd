# build-and-create-portable-app.ps1
param(
    # Skips the bundled-tool sync (network + cargo). Use only for an offline
    # repackage of tools that are already at their pinned versions.
    [switch]$SkipToolSync,
    [switch]$SkipArchive,
    [string]$Config = "Release",
    [switch]$Clean,
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path $PSScriptRoot -Parent
$BuildScript = Join-Path $Root "scripts\build.ps1"
$BuildDir = if ($Architecture -eq "x64") { "build" } else { "build-$Architecture" }
$DistDir = if ($Architecture -eq "x64") { "dist\FastMD" } else { "dist\FastMD-$Architecture" }
$QtKitArchitecture = if ($Architecture -eq "x64") { "64" } else { "arm64" }
$VCRedistRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
$RequiredVCRuntimeDlls = @(
    "msvcp140.dll"
    "msvcp140_1.dll"
    "msvcp140_2.dll"
    "vcruntime140.dll"
    "vcruntime140_1.dll"
)

function Get-VCRuntimeSourceDir {
    if (!(Test-Path $VCRedistRoot)) {
        throw "Visual C++ redist root not found at $VCRedistRoot"
    }

    $versionDir = Get-ChildItem $VCRedistRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1

    if (-not $versionDir) {
        throw "No Visual C++ redist version directories found under $VCRedistRoot"
    }

    $candidate = Join-Path $versionDir.FullName "$Architecture\Microsoft.VC143.CRT"
    if (!(Test-Path $candidate)) {
        throw "$Architecture Visual C++ runtime folder not found at $candidate"
    }

    return $candidate
}

function Copy-VCRuntimeDlls {
    param([string]$DestinationDir)

    $sourceDir = Get-VCRuntimeSourceDir

    Write-Host "Bundling Visual C++ runtime DLLs..."
    foreach ($dll in $RequiredVCRuntimeDlls) {
        $sourcePath = Join-Path $sourceDir $dll
        if (!(Test-Path $sourcePath)) {
            throw "Required Visual C++ runtime DLL not found: $sourcePath"
        }

        Copy-Item $sourcePath -Destination $DestinationDir -Force
    }
}

function Get-PEMachine {
    param([string]$Path)

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) { throw "$Path is not a PE executable (missing MZ header)." }
            $stream.Position = 0x3C
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 0 -or $peOffset + 6 -gt $stream.Length) { throw "$Path has an invalid PE header offset." }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Path is not a PE executable (missing PE header)." }
            return $reader.ReadUInt16()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-AnyDocArchitecture {
    param([string]$Path)

    $expected = if ($Architecture -eq "arm64") { 0xAA64 } else { 0x8664 }
    $actual = Get-PEMachine -Path $Path
    if ($actual -ne $expected) {
        throw ("AnyDoc wrapper architecture mismatch: expected {0} (0x{1:X4}) for {2}, found 0x{3:X4} in {4}. Run scripts\\sync-bundled-tools.ps1 -Tool anydoc -Architecture {2} -Force." -f $Architecture, $expected, $Architecture, $actual, $Path)
    }
}

function Deploy-QtRuntime {
    param([string]$DestinationDir)

    if ($Architecture -ne "arm64") {
        $WindeployQt = "C:\Qt\6.11.1\msvc2022_$QtKitArchitecture\bin\windeployqt.exe"
        if (!(Test-Path $WindeployQt)) {
            throw "Qt $Architecture windeployqt not found at $WindeployQt"
        }
        & $WindeployQt --no-translations --no-system-d3d-compiler --no-quick-import "$DestinationDir\FastMD.exe"
        if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for $Architecture" }
        return
    }

    # Qt's Arm64 deployment tool is itself Arm64 and cannot run on this x64
    # build host. Copy the runtime surface windeployqt selects for FastMD,
    # strictly from the Arm64 kit.
    $qtBin = "C:\Qt\6.11.1\msvc2022_arm64\bin"
    foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6PrintSupport.dll", "Qt6Svg.dll", "Qt6Widgets.dll")) {
        $source = Join-Path $qtBin $dll
        if (!(Test-Path $source)) { throw "Required Arm64 Qt DLL not found: $source" }
        Copy-Item $source -Destination $DestinationDir -Force
    }

    $qtPlugins = "C:\Qt\6.11.1\msvc2022_arm64\plugins"
    foreach ($pluginDir in @("generic", "iconengines", "imageformats", "networkinformation", "platforms", "styles", "tls")) {
        $source = Join-Path $qtPlugins $pluginDir
        if (!(Test-Path $source)) { throw "Required Arm64 Qt plugin directory not found: $source" }
        $destination = Join-Path $DestinationDir $pluginDir
        Copy-Item $source -Destination $destination -Recurse -Force
        # Qt's optional debug-information files put PDBs beside the plugins.
        Get-ChildItem $destination -Recurse -File -Filter *.pdb | Remove-Item -Force
    }
}

function Get-PETimeStamp {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [System.BitConverter]::ToInt32($bytes, 0x3C)
    return [System.BitConverter]::ToUInt32($bytes, $peOffset + 8)
}

# PDBs never go into the distribution folder, which feeds the ZIP, the
# installer and the MSIX. They are archived per version, architecture and link
# time instead, so a crash dump from any package of this exact build resolves.
function Save-ReleaseSymbols {
    $exe = Join-Path $BuildDir "FastMD.exe"
    $version = (Get-Item $exe).VersionInfo.FileVersion
    $linkStamp = "{0:x8}" -f (Get-PETimeStamp -Path $exe)
    $symbolDir = "dist\symbols\FastMD-$version-$Architecture-$linkStamp"
    New-Item -ItemType Directory -Path $symbolDir -Force | Out-Null

    foreach ($binary in @("FastMD.exe", "FastMDLauncher.exe", "FastMDShellExtension.dll")) {
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($binary)
        foreach ($file in @($binary, "$baseName.pdb", "$baseName.public.pdb")) {
            $source = Join-Path $BuildDir $file
            if (!(Test-Path $source)) {
                throw "Missing $source. Release builds must produce matching symbols."
            }
            Copy-Item $source -Destination $symbolDir -Force
        }
    }
    Write-Host "Symbols archived in $symbolDir"
}

function Assert-NoDebugFiles {
    param([string]$Directory)

    $debugFiles = @(Get-ChildItem $Directory -Recurse -File |
        Where-Object { $_.Extension -in @('.pdb', '.ilk') })
    if ($debugFiles.Count -gt 0) {
        throw "Debug files must not ship:`n$(($debugFiles.FullName) -join "`n")"
    }
}

if (!(Test-Path $BuildScript)) {
    throw "build.ps1 not found at $BuildScript"
}

Write-Host "Building app..." -ForegroundColor Cyan
Push-Location $Root
try {
    & $BuildScript -Architecture $Architecture -Config $Config -Clean:$Clean
    if ($LASTEXITCODE -ne 0) {
        throw "build.ps1 failed"
    }
}
finally {
    Pop-Location
}

Write-Host "Setting up distribution directory..."
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Path $DistDir | Out-Null

# 1. Copy the executables
Write-Host "Copying FastMD executables..."
Copy-Item "$BuildDir\FastMD.exe" -Destination $DistDir
Copy-Item "$BuildDir\FastMDLauncher.exe" -Destination $DistDir

# 2. Deploy the required target-architecture Qt runtime.
Write-Host "Deploying Qt runtime..."
Deploy-QtRuntime -DestinationDir $DistDir

$VcRedistExe = Join-Path $DistDir "vc_redist.$Architecture.exe"
if (Test-Path $VcRedistExe) {
    Remove-Item $VcRedistExe -Force
}

# 3. Bundle the required Visual C++ runtime DLLs for the target architecture.
Copy-VCRuntimeDlls -DestinationDir $DistDir

# 4. Copy documentation
Write-Host "Copying README and license..."
Copy-Item "README.md" -Destination $DistDir
Copy-Item "LICENSE" -Destination $DistDir

# 4b. Bundle KaTeX assets for offline math rendering (Browser Preview / HTML / PDF)
Write-Host "Bundling KaTeX assets..."
Copy-Item "resources\katex" -Destination $DistDir -Recurse

# 4b-i. Bundle the WebView2 preview shell (preview.html/css/js + highlight.js +
# Mermaid). WebView2 serves these offline from the exe directory via
# SetVirtualHostNameToFolderMapping (https://fastmd.assets/preview/...), so the
# folder must sit next to FastMD.exe. Without it the live preview cannot load.
Write-Host "Bundling WebView2 preview shell (Mermaid, highlight.js)..."
Copy-Item "resources\preview" -Destination $DistDir -Recurse

# 4b-ii. Bundle the local xterm.js page used by the integrated terminal.
Write-Host "Bundling integrated terminal assets (xterm.js)..."
Copy-Item "resources\terminal" -Destination $DistDir -Recurse

# 4b-iii. Bundle KSyntaxHighlighting definitions and themes used by the
# Markdown editor and fenced-code highlighter.  The runtime repository loads
# these from the directory beside FastMD.exe, so they must also be included
# in portable and MSIX distributions.
Write-Host "Bundling KSyntaxHighlighting definitions and themes..."
Copy-Item "$BuildDir\ksyntaxhighlighting" -Destination $DistDir -Recurse

# 4b-iv. Bundle the ODT reference document (used via --reference-doc so
# exported display math is centered even in viewers that flatten Pandoc's
# math frame, e.g. Microsoft Word).
Write-Host "Bundling ODT reference document..."
Copy-Item "resources\odt" -Destination $DistDir -Recurse

# 4b-v. Bundle the remaining export-styling assets so EPUB/LaTeX/RTF
# exports carry the app's visual identity (see ExportManager::exportViaPandoc):
#   epub/ -> --css   latex/ -> header   rtf/ -> template
Write-Host "Bundling EPUB/LaTeX/RTF export styling..."
foreach ($fmtDir in @("epub", "latex", "rtf")) {
    Copy-Item "resources\$fmtDir" -Destination $DistDir -Recurse
}

# 4c. Bundle Pandoc (external tool for Export DOCX; not linked as a library).
# pandoc.exe is not checked into git (see tools/pandoc/NOTICE.txt). Versions for
# every bundled tool live in scripts\sync-bundled-tools.ps1 - change them there,
# never here. The sync is a no-op once the pinned versions are in place.
if (!$SkipToolSync) {
    & (Join-Path $PSScriptRoot "sync-bundled-tools.ps1") -Architecture $Architecture
}

$PandocExe = "tools\pandoc\pandoc.exe"
if (!(Test-Path $PandocExe)) {
    throw "Missing $PandocExe. Run scripts\sync-bundled-tools.ps1 (or drop the -SkipToolSync switch)."
}

Write-Host "Bundling Pandoc..."
New-Item -ItemType Directory -Path "$DistDir\tools\pandoc" -Force | Out-Null
Copy-Item "tools\pandoc\*" -Destination "$DistDir\tools\pandoc" -Recurse

# 4d. Bundle the FastMD-owned native AnyDoc wrapper. It is produced by the
# companion Rust release pipeline and deliberately never falls back to Node.
# The wrapper has not shipped its first release yet, so its absence only skips
# the importer (FastMD says so at runtime) instead of failing the whole package.
# A wrapper that IS present must always match its pinned hash.
$AnyDocSourceDir = "tools\anydoc\$Architecture"
$AnyDocExe = Join-Path $AnyDocSourceDir "fastmd-anydoc.exe"
$AnyDocHashFile = Join-Path $AnyDocSourceDir "SHA256.txt"
if (!(Test-Path $AnyDocExe)) {
    Write-Warning "Skipping AnyDoc importer: $AnyDocExe not built. Import as Markdown will be unavailable in this package."
} else {
    $ExpectedAnyDocHash = (Get-Content $AnyDocHashFile -Raw).Trim().ToLowerInvariant()
    if ($ExpectedAnyDocHash -notmatch '^[0-9a-f]{64}$') {
        throw "Missing or invalid pinned SHA-256 in $AnyDocHashFile"
    }
    $ActualAnyDocHash = (Get-FileHash $AnyDocExe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualAnyDocHash -ne $ExpectedAnyDocHash) {
        throw "FastMD AnyDoc wrapper SHA-256 does not match $AnyDocHashFile"
    }
    Assert-AnyDocArchitecture -Path $AnyDocExe
    Write-Host "Bundling AnyDoc importer..."
    New-Item -ItemType Directory -Path "$DistDir\tools\anydoc" -Force | Out-Null
    Copy-Item $AnyDocExe -Destination "$DistDir\tools\anydoc\fastmd-anydoc.exe" -Force
    Copy-Item $AnyDocHashFile -Destination "$DistDir\tools\anydoc\SHA256.txt" -Force
    foreach ($metadata in @("VERSION.txt", "NOTICE.txt")) {
        $source = Join-Path "tools\anydoc" $metadata
        if (Test-Path $source) {
            Copy-Item $source -Destination "$DistDir\tools\anydoc\$metadata" -Force
        }
    }
}

# 5. Keep this build's symbols, and make sure none of them ship.
Save-ReleaseSymbols
Assert-NoDebugFiles -Directory $DistDir

# Store packaging needs the deployment and symbols, but no portable ZIP.
if (!$SkipArchive) {
    $PortableArchive = if ($Architecture -eq "x64") { "dist\FastMD-Portable.zip" } else { "dist\FastMD-Portable-$Architecture.zip" }
    Write-Host "Compressing into $PortableArchive..."
    Compress-Archive -Path "$DistDir\*" -DestinationPath $PortableArchive -Force
    Write-Host "Portable archive ready: $PortableArchive"
}
Write-Host "Deployment ready: $DistDir. For Store packages, run scripts\build-store-release.ps1."
