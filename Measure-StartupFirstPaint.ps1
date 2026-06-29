<#
.SYNOPSIS
    Measures FastMD's *perceived* startup time using the in-app instrumentation.

.DESCRIPTION
    The older window-handle / WaitForInputIdle benchmarks answer the wrong
    question: a Qt window owns a MainWindowHandle and starts pumping messages
    well before it has actually painted usable content, so those numbers report
    the app as "ready" too early and are noisy.

    This script instead runs FastMD with FASTMD_STARTUP_PROFILE=1, which makes
    the app print a precise timeline to stderr from a QElapsedTimer started on
    the first line of main(). The most meaningful single number is
    "first window paint" — the moment the OS delivers the first paint event to
    the top-level window, i.e. the first instant the user sees real UI.

    Note: this measures from main() onward. On a cold launch, mapping the ~38 MB
    of Qt DLLs from disk happens *before* main() and adds (very roughly)
    100-250 ms that this number does not include. Run with -Cold to approximate
    that by clearing the file cache is NOT attempted here (requires admin / is
    disruptive); instead compare warm medians, which are stable and low-noise.

.EXAMPLE
    .\Measure-StartupFirstPaint.ps1 -Iterations 15
#>
param(
    [int]$Iterations = 12,
    [string]$Exe = (Join-Path $PSScriptRoot 'build\FastMD.exe'),
    [int]$SettleMs = 1200,
    [string]$OutputCsv = (Join-Path $PSScriptRoot 'startup-firstpaint.csv')
)

$ErrorActionPreference = 'Stop'
if (!(Test-Path $Exe)) { throw "FastMD.exe not found at $Exe — build it first (.\build.ps1)." }

$env:FASTMD_STARTUP_PROFILE = '1'

# Phases we extract from each run's stderr timeline.
$phases = @(
    'QApplication constructed',
    'MainWindow constructed',
    'window.show() returned',
    'first window paint'
)

$rows = New-Object System.Collections.Generic.List[object]

for ($i = 1; $i -le $Iterations; $i++) {
    $err = Join-Path $env:TEMP ("fastmd_fp_{0}.txt" -f $i)
    $p = Start-Process -FilePath $Exe -RedirectStandardError $err -PassThru
    Start-Sleep -Milliseconds $SettleMs
    if (-not $p.HasExited) {
        $p.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 200
        if (-not $p.HasExited) { $p.Kill() }
    }

    $text = Get-Content $err -Raw
    $row = [ordered]@{ Iteration = $i }
    foreach ($ph in $phases) {
        $m = [regex]::Match($text, '(?m)^\[startup\]\s+(\d+) ms.*?' + [regex]::Escape($ph))
        $row[$ph] = if ($m.Success) { [int]$m.Groups[1].Value } else { $null }
    }
    $rows.Add([pscustomobject]$row)
    Write-Host ("[{0}/{1}] first paint = {2} ms" -f $i, $Iterations, $row['first window paint'])
}

$rows | Export-Csv -Path $OutputCsv -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host "Medians across $Iterations runs (ms, from main()):" -ForegroundColor Green
foreach ($ph in $phases) {
    $vals = $rows.$ph | Where-Object { $_ -ne $null } | Sort-Object
    if ($vals.Count -gt 0) {
        $median = $vals[[int]([math]::Floor($vals.Count / 2))]
        "{0,-26} median={1,5}  min={2,5}  max={3,5}" -f $ph, $median, $vals[0], $vals[-1]
    }
}
Write-Host ""
Write-Host "Detailed results saved to $OutputCsv"
