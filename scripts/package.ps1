# package.ps1
$ErrorActionPreference = "Stop"

$BuildDir = "build"
$DistDir = "dist\FastMD"

Write-Host "Setting up distribution directory..."
if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" }
New-Item -ItemType Directory -Path $DistDir | Out-Null

# 1. Copy the executable
Write-Host "Copying FastMD.exe..."
Copy-Item "$BuildDir\FastMD.exe" -Destination $DistDir

# 2. Run windeployqt to automatically grab all required Qt DLLs
Write-Host "Running windeployqt..."
$WindeployQt = "C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe"
& $WindeployQt --no-translations --no-system-d3d-compiler --no-quick-import "$DistDir\FastMD.exe"

# 3. Copy documentation
Write-Host "Copying README and LICENSE..."
Copy-Item "README.md" -Destination $DistDir
Copy-Item "LICENSE" -Destination $DistDir

# 3b. Bundle KaTeX assets for offline math rendering (Browser Preview / HTML / PDF)
Write-Host "Bundling KaTeX assets..."
Copy-Item "resources\katex" -Destination $DistDir -Recurse

# 3c. Bundle Pandoc (external tool for Export DOCX; not linked as a library).
# pandoc.exe is not checked into git (see tools/pandoc/NOTICE.txt) - download the
# official Windows binary here and cache it in tools\pandoc so repeat packaging
# runs don't re-download. Bump $PandocVersion to update.
$PandocVersion = "3.10"
$PandocExe = "tools\pandoc\pandoc.exe"

if (!(Test-Path $PandocExe)) {
    Write-Host "Downloading Pandoc $PandocVersion..."
    $PandocZip = "tools\pandoc\pandoc-$PandocVersion.zip"
    $PandocUrl = "https://github.com/jgm/pandoc/releases/download/$PandocVersion/pandoc-$PandocVersion-windows-x86_64.zip"
    Invoke-WebRequest -Uri $PandocUrl -OutFile $PandocZip

    $PandocExtractDir = "tools\pandoc\_extract"
    Expand-Archive -Path $PandocZip -DestinationPath $PandocExtractDir -Force
    Copy-Item "$PandocExtractDir\pandoc-$PandocVersion\pandoc.exe" -Destination $PandocExe

    Remove-Item $PandocZip -Force
    Remove-Item $PandocExtractDir -Recurse -Force
} else {
    Write-Host "Using cached Pandoc binary at $PandocExe..."
}

Write-Host "Bundling Pandoc..."
New-Item -ItemType Directory -Path "$DistDir\tools\pandoc" -Force | Out-Null
Copy-Item "tools\pandoc\*" -Destination "$DistDir\tools\pandoc" -Recurse

# 4. Zip it up for the Portable Release
Write-Host "Compressing into FastMD-Portable.zip..."
Compress-Archive -Path "$DistDir\*" -DestinationPath "dist\FastMD-Portable.zip" -Force

Write-Host ""
Write-Host "SUCCESS! Your portable release is ready at: dist\FastMD-Portable.zip"
Write-Host "To build the Setup Installer and publish a release, run 'scripts\release.ps1'."
