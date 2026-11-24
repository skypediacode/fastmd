param(
    [string]$Config = "Release",
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Qt is found in $env:FASTMD_QT_ROOT (default C:\Qt). Visual Studio is found with
# vswhere, so the Build Tools, Community and Professional editions all work.
$QtRoot = if ($env:FASTMD_QT_ROOT) { $env:FASTMD_QT_ROOT } else { "C:\Qt" }
$QtVersionDir = Join-Path $QtRoot "6.11.1"
$QtKitArchitecture = if ($Architecture -eq "x64") { "64" } else { "arm64" }
$Qt6Dir  = Join-Path $QtVersionDir "msvc2022_$QtKitArchitecture"
$QtHostDir = Join-Path $QtVersionDir "msvc2022_64"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$VSBase = $null
if (Test-Path $vswhere) {
    $VSBase = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
}
if (!$VSBase) { throw "Visual Studio 2022 with the 'Desktop development with C++' workload was not found. Install it from https://visualstudio.microsoft.com/downloads/ (Build Tools is enough)." }
$vcvars  = Join-Path $VSBase "VC\Auxiliary\Build\vcvarsall.bat"

function Resolve-BuildTool([string]$Name, [string]$QtToolPath) {
    if (Test-Path $QtToolPath) { return $QtToolPath }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "$Name was not found at $QtToolPath or on PATH. Install it with the Qt maintenance tool (Developer and Designer Tools)."
}
$cmake = Resolve-BuildTool "cmake.exe" (Join-Path $QtRoot "Tools\CMake_64\bin\cmake.exe")
$ninja = Resolve-BuildTool "ninja.exe" (Join-Path $QtRoot "Tools\Ninja\ninja.exe")
$Root = Split-Path $PSScriptRoot -Parent
$buildDirName = if ($Architecture -eq "x64") { "build" } else { "build-$Architecture" }
$buildDir = Join-Path $Root $buildDirName

# Validate
if (!(Test-Path $vcvars)) { throw "vcvarsall.bat not found at $vcvars" }
if (!(Test-Path $Qt6Dir)) { throw "Qt $Architecture kit not found at $Qt6Dir" }
if (!(Test-Path $QtHostDir)) { throw "Qt x64 host kit not found at $QtHostDir" }
if (!(Test-Path $cmake))  { throw "cmake not found at $cmake" }
if (!(Test-Path $ninja))  { throw "ninja not found at $ninja" }

# Import MSVC environment into current PowerShell session
Write-Host "[env] Loading MSVC environment..." -ForegroundColor Cyan
# Preserved so tools outside the VS/Qt toolchain (git, gh, ...) stay reachable
# after the clean-environment vcvars capture below replaces PATH -- see its
# use after the vcvars output is parsed.
$callerPath = $env:Path
# This build script runs on x64 development machines.  vcvarsall's `arm64`
# argument means an Arm64 *host* environment, while `amd64_arm64` selects the
# installed x64-host compiler that targets Arm64.
$vcvarsTarget = if ($Architecture -eq "arm64") { "amd64_arm64" } else { "amd64" }
# vcvarsall.bat/vsdevcmd.bat are not safely re-entrant with a different target
# architecture: when this process already carries a dev environment from an
# earlier x64 invocation (e.g. build-all-and-publish-to-github.ps1 building
# x64 then Arm64 in one PowerShell session), vsdevcmd's own idempotency checks
# can skip re-deriving WindowsSdkDir/LIB for the new architecture and silently
# leave the previous architecture's LIB (um\x64) ahead of the new one -- the
# compiler then targets Arm64 correctly (selected via PATH below) while the
# linker resolves bare Windows SDK libs (kernel32.lib, etc.) from the stale
# x64 directory, which fails with LNK2001/LNK2019 "unresolved external" for
# every CRT/Win32 import. Capture vcvarsall's output from a process with a
# minimal, clean environment so it always computes the requested architecture
# from scratch, regardless of what a prior call in this session left behind.
$cleanEnvNames = @(
    'SystemRoot', 'windir', 'ComSpec', 'PATHEXT', 'TEMP', 'TMP',
    'USERPROFILE', 'USERNAME', 'ProgramFiles', 'ProgramFiles(x86)',
    'ProgramData', 'ProgramW6432', 'ALLUSERSPROFILE', 'APPDATA',
    'LOCALAPPDATA', 'HOMEDRIVE', 'HOMEPATH', 'NUMBER_OF_PROCESSORS', 'OS',
    'PROCESSOR_ARCHITECTURE', 'PROCESSOR_IDENTIFIER', 'PROCESSOR_LEVEL',
    'PROCESSOR_REVISION', 'PUBLIC', 'SystemDrive'
)
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = "cmd.exe"
$psi.Arguments = "/c `"`"$vcvars`" $vcvarsTarget >nul 2>&1 && set`""
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.EnvironmentVariables.Clear()
foreach ($name in $cleanEnvNames) {
    $value = [System.Environment]::GetEnvironmentVariable($name, "Process")
    if ($value) { $psi.EnvironmentVariables[$name] = $value }
}
$psi.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
$vcvarsProc = [System.Diagnostics.Process]::Start($psi)
$vcvarsOutput = $vcvarsProc.StandardOutput.ReadToEnd()
$vcvarsProc.WaitForExit()
$envLines = $vcvarsOutput -split "`r?`n"
foreach ($line in $envLines) {
    $eq = $line.IndexOf('=')
    if ($eq -gt 0) {
        $k = $line.Substring(0, $eq)
        $v = $line.Substring($eq + 1)
        # vcvars was captured from a stripped-down environment (see above), so
        # its own PATH value only has the VS/Windows SDK/tool dirs it added --
        # it does not carry git, gh, or anything else the caller already had.
        # Put those additions first (so the right cl.exe/link.exe win) and
        # keep the caller's PATH as a fallback rather than discarding it.
        if ($k -eq "Path" -or $k -eq "PATH") {
            $v = "$v;$callerPath"
        }
        [System.Environment]::SetEnvironmentVariable($k, $v, "Process")
    }
}
$msvcHostBin = Join-Path $env:VCToolsInstallDir "bin\Hostx64\$Architecture"
if (!(Test-Path (Join-Path $msvcHostBin "cl.exe")) -or !(Test-Path (Join-Path $msvcHostBin "link.exe"))) {
    throw "MSVC compiler/linker directory for $Architecture is incomplete: $msvcHostBin"
}
# Developers can have MSYS/MinGW on PATH. Put MSVC's linker ahead of it: an
# MSYS ld.exe accepts the name `link.exe` but cannot link MSVC Arm64 objects.
$env:Path = "$msvcHostBin;$QtHostDir\bin;$Qt6Dir\bin;$env:Path"
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if ($Architecture -eq "arm64") {
        throw "The Visual Studio Arm64 C++ toolchain is not installed. In Visual Studio Installer, add the MSVC C++ ARM64 build tools component (or the Desktop development with C++ workload with Arm64 tools), then rerun this command."
    }
    throw "The Visual Studio x64 C++ toolchain is not available after loading vcvarsall. Repair or install the Desktop development with C++ workload."
}

$oldPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
Write-Host "[env] MSVC ready: $(& cl.exe /? 2>&1 | Select-Object -First 1)" -ForegroundColor Green
$ErrorActionPreference = $oldPreference

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[clean] Removing $buildDir" -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force
}

# Configure when the build directory is new or its cached configuration does not
# match the configuration requested by this invocation.
$needsConfigure = $Architecture -eq "arm64" -or !(Test-Path "$buildDir\build.ninja")
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
if (!$needsConfigure -and (Test-Path $cacheFile)) {
    $cachedBuildType = (Select-String -Path $cacheFile -Pattern '^CMAKE_BUILD_TYPE:STRING=(.*)$').Matches.Groups[1].Value
    $needsConfigure = $cachedBuildType -ne $Config
}

if ($needsConfigure) {
    Write-Host "[cmake] Configuring..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-S", $Root,
        "-B", $buildDir,
        "-G", "Ninja",
        "-DCMAKE_PREFIX_PATH=$Qt6Dir",
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_C_COMPILER=cl",
        "-DCMAKE_CXX_COMPILER=cl",
        "-DCMAKE_LINKER=$msvcHostBin\link.exe",
        "-DCMAKE_AR=$msvcHostBin\lib.exe",
        "-DQT_HOST_PATH=$QtHostDir",
        "-DFASTMD_TARGET_ARCH=$Architecture"
    )
    if ($Architecture -eq "arm64") {
        $hostIndexer = Join-Path $Root "build\bin\katehighlightingindexer.exe"
        if (!(Test-Path $hostIndexer)) {
            throw "Arm64 builds require the x64 KSyntaxHighlighting host helper at $hostIndexer. Run scripts\build.ps1 once without -Architecture arm64 first."
        }
        $cmakeArgs += "-DCMAKE_SYSTEM_NAME=Windows"
        $cmakeArgs += "-DKATEHIGHLIGHTINGINDEXER_EXECUTABLE=$hostIndexer"
    }
    & $cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "[build] Building $Config..." -ForegroundColor Cyan
& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$exe = "$buildDir\FastMD.exe"
Write-Host "[done] $exe" -ForegroundColor Green
