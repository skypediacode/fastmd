<#
.SYNOPSIS
Turns Windows Error Reporting full crash dumps for FastMD.exe on or off.

.DESCRIPTION
Writes only HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\FastMD.exe:
full dumps (DumpType 2), at most 10 (WER replaces the oldest), in a dedicated
folder. It covers the Store, installer and portable builds alike, because WER
matches the executable name. Nothing runs this automatically; it needs an
elevated PowerShell.

Full dumps hold the whole FastMD process memory: open documents, unsaved text,
file paths, clipboard data, terminal output and anything typed into it. Treat
them as private, and do not attach them to public issues. Each can take several
hundred MB, so 10 can reach several GB.

The shared LocalDumps key is left alone. If it does not exist yet, creating it
also turns on Windows' default minidumps for every other application, so this
script refuses unless -CreateLocalDumpsKey is given, and -Disable removes it
again only when this script created it and nothing else uses it.

.EXAMPLE
.\scripts\enable-crash-dumps.ps1
.\scripts\enable-crash-dumps.ps1 -Disable
#>
param(
    [switch]$Disable,
    [string]$DumpFolder = (Join-Path $env:LOCALAPPDATA "FastMD\CrashDumps"),
    [switch]$CreateLocalDumpsKey
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$LocalDumpsKey = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps"
$AppKey = Join-Path $LocalDumpsKey "FastMD.exe"
$CreatedParentMarker = "FastMDCreatedLocalDumpsKey"

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this from an elevated PowerShell: LocalDumps settings live under HKLM."
}

if ($Disable) {
    if (!(Test-Path $AppKey)) {
        Write-Host "FastMD crash dumps are not configured."
        return
    }
    $createdParent = $null -ne (Get-ItemProperty $AppKey -Name $CreatedParentMarker -ErrorAction SilentlyContinue)
    $folder = (Get-ItemProperty $AppKey -Name DumpFolder -ErrorAction SilentlyContinue).DumpFolder
    Remove-Item $AppKey -Recurse -Force
    Write-Host "FastMD crash dumps disabled."

    if ($createdParent) {
        $parent = Get-Item $LocalDumpsKey
        if ($parent.SubKeyCount -eq 0 -and $parent.ValueCount -eq 0) {
            Remove-Item $LocalDumpsKey -Force
            Write-Host "Removed the LocalDumps key this script created."
        }
    }
    if ($folder) {
        Write-Host "Dumps already written were kept in $folder; delete them when no longer needed."
    }
    return
}

$createdParent = $false
if (!(Test-Path $LocalDumpsKey)) {
    if (-not $CreateLocalDumpsKey) {
        throw ("$LocalDumpsKey does not exist. Creating it also makes Windows write default " +
            "minidumps for every other application that crashes. Rerun with -CreateLocalDumpsKey to accept that.")
    }
    New-Item -Path $LocalDumpsKey -Force | Out-Null
    $createdParent = $true
}

$DumpFolder = [System.IO.Path]::GetFullPath($DumpFolder)
New-Item -ItemType Directory -Path $DumpFolder -Force | Out-Null

New-Item -Path $AppKey -Force | Out-Null
New-ItemProperty -Path $AppKey -Name DumpFolder -PropertyType ExpandString -Value $DumpFolder -Force | Out-Null
New-ItemProperty -Path $AppKey -Name DumpCount -PropertyType DWord -Value 10 -Force | Out-Null
New-ItemProperty -Path $AppKey -Name DumpType -PropertyType DWord -Value 2 -Force | Out-Null
if ($createdParent) {
    New-ItemProperty -Path $AppKey -Name $CreatedParentMarker -PropertyType DWord -Value 1 -Force | Out-Null
}

Write-Host "FastMD full crash dumps enabled."
Write-Host "  Folder: $DumpFolder (at most 10 dumps; the oldest is replaced)"
Write-Host "  Turn off with: .\scripts\enable-crash-dumps.ps1 -Disable"
