param(
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$VSBase  = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vcvars  = "$VSBase\VC\Auxiliary\Build\vcvarsall.bat"
$Qt6Dir  = "C:\Qt\6.11.1\msvc2022_64"
$cmake   = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$ninja   = "C:\Qt\Tools\Ninja\ninja.exe"
$buildDir= Join-Path $PSScriptRoot "build"

# Validate
if (!(Test-Path $vcvars)) { throw "vcvarsall.bat not found at $vcvars" }
if (!(Test-Path $cmake))  { throw "cmake not found at $cmake" }
if (!(Test-Path $ninja))  { throw "ninja not found at $ninja" }

# Import MSVC environment into current PowerShell session
Write-Host "[env] Loading MSVC environment..." -ForegroundColor Cyan
$envLines = cmd.exe /c "`"$vcvars`" amd64 > nul 2>&1 && set"
foreach ($line in $envLines) {
    $eq = $line.IndexOf('=')
    if ($eq -gt 0) {
        $k = $line.Substring(0, $eq)
        $v = $line.Substring($eq + 1)
        [System.Environment]::SetEnvironmentVariable($k, $v, "Process")
    }
}
$oldPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
Write-Host "[env] MSVC ready: $(& cl.exe /? 2>&1 | Select-Object -First 1)" -ForegroundColor Green
$ErrorActionPreference = $oldPreference

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[clean] Removing $buildDir" -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force
}

# Configure
if (!(Test-Path "$buildDir\build.ninja")) {
    Write-Host "[cmake] Configuring..." -ForegroundColor Cyan
    & $cmake -S $PSScriptRoot -B $buildDir `
        -G Ninja `
        -DCMAKE_PREFIX_PATH="$Qt6Dir" `
        -DCMAKE_BUILD_TYPE="$Config" `
        -DCMAKE_MAKE_PROGRAM="$ninja" `
        -DCMAKE_C_COMPILER=cl `
        -DCMAKE_CXX_COMPILER=cl
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "[build] Building $Config..." -ForegroundColor Cyan
& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$exe = "$buildDir\FastMD.exe"
Write-Host "[done] $exe" -ForegroundColor Green
