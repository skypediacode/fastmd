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

# 4. Zip it up for the Portable Release
Write-Host "Compressing into FastMD-Portable.zip..."
Compress-Archive -Path "$DistDir\*" -DestinationPath "dist\FastMD-Portable.zip" -Force

Write-Host ""
Write-Host "SUCCESS! Your portable release is ready at: dist\FastMD-Portable.zip"
Write-Host "To build the Setup Installer, open 'fastmd.iss' in Inno Setup and click Compile."
