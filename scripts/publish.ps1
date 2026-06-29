param (
    [string]$Version = "v1.6.0"
)

$ErrorActionPreference = "Stop"

Write-Host "Creating GitHub Release for version $Version..."

$Title = "FastMD $Version"
$Notes = "Native, lightning-fast Markdown editor for Windows with v1.6.0 improvements: LaTeX math rendering with KaTeX, Clear Recent Files action, Save All action, two toolbar buttons for inline math and block math,and enhanced startup performance. Download the portable zip to get started immediately without installation or the installer if you prefer the traditional Windows installation."

$ChangelogPath = "CHANGELOG.md"
if (Test-Path $ChangelogPath) {
    $VersionNumber = $Version -replace '^v', ''
    $ChangelogContent = Get-Content $ChangelogPath -Raw
    $Pattern = "(?sm)^## \[$VersionNumber\].*?\n(.*?)(?=^## \[|\z)"
    if ($ChangelogContent -match $Pattern) {
        $ReleaseNotes = $matches[1].Trim()
        $Notes += "`n`n## Release Notes`n`n$ReleaseNotes"
    }
}

# Check what files exist
$ZipFile = "dist\FastMD-Portable.zip"
$ExeFile = "dist\FastMD-Setup.exe"

$FilesToUpload = @()

if (Test-Path $ZipFile) {
    $FilesToUpload += $ZipFile
} else {
    Write-Host "Warning: $ZipFile not found. Did you build the portable zip?" -ForegroundColor Yellow
}

if (Test-Path $ExeFile) {
    $FilesToUpload += $ExeFile
} else {
    Write-Host "Warning: $ExeFile not found. Did you compile fastmd.iss?" -ForegroundColor Yellow
}

if ($FilesToUpload.Count -eq 0) {
    Write-Host "No distribution files found. Aborting release." -ForegroundColor Red
    exit 1
}

$FileListStr = $FilesToUpload -join '" "'

# Check if release already exists
$ReleaseExists = (gh release view $Version 2>&1) -match "title:"

if ($ReleaseExists) {
    $Confirm = Read-Host "Warning: Release $Version already exists! Do you want to overwrite? (y/N)"
    if ($Confirm -notmatch "^y$|^Y$") {
        Write-Host "Upload cancelled by user." -ForegroundColor Yellow
        exit 0
    }
    Write-Host "Overwriting files and updating release notes..." -ForegroundColor Yellow
    cmd.exe /c "gh release upload $Version `"$FileListStr`" --clobber"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to upload files." -ForegroundColor Red
        exit 1
    }
    # Update the release description with notes
    $noteFile = [System.IO.Path]::GetTempFileName()
    [System.IO.File]::WriteAllText($noteFile, $Notes)
    cmd.exe /c "gh release edit $Version --notes-file `"$noteFile`""
    Remove-Item $noteFile -Force
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Release $Version updated successfully!" -ForegroundColor Green
    } else {
        Write-Host "Failed to update release notes." -ForegroundColor Red
    }
} else {
    $Command = "gh release create $Version `"$FileListStr`" --title `"$Title`" --notes `"$Notes`""
    Write-Host "Executing: $Command"
    cmd.exe /c $Command
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Release $Version published successfully!" -ForegroundColor Green
    } else {
        Write-Host "Failed to publish release. See error above." -ForegroundColor Red
    }
}
