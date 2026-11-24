# Release workflow tests. Everything is temporary: local git repositories, a bare
# repository standing in for GitHub, and a mocked `gh`. No network access.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture = Join-Path $tempRoot ('fastmd-release-test-' + [guid]::NewGuid().ToString('N'))
$originalPath = $env:PATH
$utf8 = New-Object System.Text.UTF8Encoding($false)
$eacute = [string][char]0xE9

function Assert-True($Condition, $Message) {
    if (!$Condition) { throw "ASSERT: $Message" }
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern, [string]$Label) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "ASSERT ($Label): wrong error: $($_.Exception.Message)" }
        return
    }
    throw "ASSERT ($Label): expected failure matching: $Pattern"
}
function Rgit([string]$Repo, [string[]]$Arguments) {
    $previous = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $prefix = @(if ($Repo -like "*remote.git") { "--git-dir=$Repo" } else { "-C"; $Repo })
    try { $out = @(& git @prefix @Arguments 2>&1); $code = $LASTEXITCODE } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "git $($Arguments -join ' ') failed: $($out -join ' ')" }
    @($out | ForEach-Object { [string]$_ })
}
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

$mockGh = @'
$state = $env:FASTMD_MOCK_GH
Add-Content -LiteralPath (Join-Path $state 'gh.log') -Value ($args -join ' ')
if (Test-Path (Join-Path $state 'api-fail')) { [Console]::Error.WriteLine('HTTP 502: Bad Gateway'); exit 1 }
if ($args[0] -eq 'api' -and $args[1] -eq 'user') {
    $login = Join-Path $state 'login.txt'
    if (Test-Path $login) { Get-Content $login } else { 'skypediacode' }
    exit 0
}
if ($args[0] -eq 'api') {
    $file = Join-Path $state 'releases.txt'
    if (Test-Path $file) { Get-Content $file -Encoding UTF8 }
    exit 0
}
if ($args[0] -eq 'release' -and $args[1] -eq 'create') {
    if (Test-Path (Join-Path $state 'create-fail')) { [Console]::Error.WriteLine('HTTP 500: server error'); exit 1 }
    $tag = $args[2]
    $notes = $args[[array]::IndexOf($args, '--notes-file') + 1]
    [IO.File]::WriteAllBytes((Join-Path $state 'notes-captured.md'), [IO.File]::ReadAllBytes($notes))
    Add-Content -LiteralPath (Join-Path $state 'releases.txt') -Value ('{"tag_name":"' + $tag + '","draft":false,"url":"https://github.com/skypediacode/fastmd/releases/tag/' + $tag + '"}')
    'https://github.com/skypediacode/fastmd/releases/tag/' + $tag
    exit 0
}
[Console]::Error.WriteLine('unexpected gh call: ' + ($args -join ' '))
exit 2
'@

$script:counter = 0
function New-Fixture {
    $script:counter++
    $base = Join-Path $fixture ("case$($script:counter)")
    $bare = Join-Path $base 'remote.git'
    $work = Join-Path $base 'work'
    $state = Join-Path $base 'mock'
    $bin = Join-Path $base 'bin'
    New-Item -ItemType Directory -Path $bare, $work, $state, $bin | Out-Null
    & git init --quiet --bare -b main $bare
    & git init --quiet -b main $work
    foreach ($pair in @(@('user.name', 'Tester'), @('user.email', 'tester@example.invalid'), @('commit.gpgsign', 'false'), @('tag.gpgsign', 'false'), @('core.autocrlf', 'false'))) {
        Rgit $work @('config', $pair[0], $pair[1]) | Out-Null
    }
    Rgit $work @('remote', 'add', 'origin', 'git@github.com:skypediacode/fastmd.git') | Out-Null
    Rgit $work @('config', ('url.' + ($bare -replace '\\', '/') + '.insteadOf'), 'git@github.com:skypediacode/fastmd.git') | Out-Null
    $scripts = Join-Path $work 'scripts'
    foreach ($name in @('build-store-release.ps1', 'build-all-and-publish-to-github.ps1')) {
        New-Item -ItemType Directory -Path $scripts -Force | Out-Null
        Copy-Item (Join-Path $root "scripts\$name") (Join-Path $scripts $name)
    }
    Write-Text (Join-Path $work '.gitignore') "dist/`ntrace.txt`nfail`nfail-sync`n"
    Write-Text (Join-Path $work 'CMakeLists.txt') 'project(FastMD VERSION 9.8.7 LANGUAGES CXX C)'
    Write-Text (Join-Path $work 'CHANGELOG.md') "# Changelog`n`n## [9.8.7] - 2026-01-01`n`n### Fixed`n- Current fix caf$eacute.`n`n## [9.8.6] - 2025-01-01`n- Older fix.`n"
    Write-Text (Join-Path $scripts 'sync-bundled-tools.ps1') @'
param($Architecture)
$root = Split-Path $PSScriptRoot -Parent
Add-Content (Join-Path $root 'trace.txt') "sync:$Architecture"
'@
    Write-Text (Join-Path $scripts 'build-msix.ps1') @'
param($Version, $Architecture, $Config, [switch]$Clean, [switch]$SkipToolSync)
$root = Split-Path $PSScriptRoot -Parent
Add-Content (Join-Path $root 'trace.txt') "$Architecture|$Version|$Config|$Clean|$SkipToolSync"
if (Test-Path (Join-Path $root 'fail')) { throw 'Mock packaging failed' }
New-Item -ItemType Directory -Path (Join-Path $root 'dist') -Force | Out-Null
$suffix = if ($Architecture -eq 'x64') { '' } else { '-arm64' }
foreach ($ext in @('msix', 'msixupload')) { Set-Content (Join-Path $root "dist\FastMD-$Version$suffix.$ext") "mock $Architecture $ext" }
'@
    Rgit $work @('add', '-A') | Out-Null
    Rgit $work @('commit', '-q', '-m', 'initial') | Out-Null
    Rgit $work @('push', '-q', 'origin', 'main:refs/heads/main') | Out-Null
    Set-Content (Join-Path $bin 'gh.cmd') "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0mock-gh.ps1`" %*"
    Set-Content (Join-Path $bin 'mock-gh.ps1') $mockGh
    $env:PATH = "$bin;$originalPath"
    $env:FASTMD_MOCK_GH = $state
    [pscustomobject]@{
        Base = $base; Bare = $bare; Work = $work; State = $state
        Script = Join-Path $scripts 'build-all-and-publish-to-github.ps1'
        Trace = Join-Path $work 'trace.txt'
    }
}
function Get-Snapshot($f) {
    $files = Get-ChildItem $f.Work -Recurse -File -Force | Where-Object { $_.FullName -notlike '*\.git\*' } |
        ForEach-Object { $_.FullName.Substring($f.Work.Length) + ':' + (Get-FileHash $_.FullName).Hash }
    [pscustomobject]@{
        Files = ($files -join "`n")
        LocalRefs = @(Rgit $f.Work @('for-each-ref', '--format=%(refname) %(objectname)')) -join "`n"
        RemoteRefs = @(Rgit $f.Bare @('for-each-ref', '--format=%(refname) %(objectname)')) -join "`n"
        Index = (Get-FileHash (Join-Path $f.Work '.git\index')).Hash
        Staged = @(Rgit $f.Work @('diff', '--cached', '--binary')) -join "`n"
        Releases = $(if (Test-Path (Join-Path $f.State 'releases.txt')) { Get-Content (Join-Path $f.State 'releases.txt') -Raw } else { '' })
    }
}
function Assert-Unchanged($Before, $After, $Label) {
    foreach ($name in 'Files', 'LocalRefs', 'RemoteRefs', 'Index', 'Staged', 'Releases') {
        Assert-True ($Before.$name -eq $After.$name) "$Label changed $name."
    }
}
function Count-GhCreates($f) {
    $log = Join-Path $f.State 'gh.log'
    if (!(Test-Path $log)) { return 0 }
    @(Get-Content $log | Where-Object { $_ -like 'release create*' }).Count
}
function Run($f, [hashtable]$Arguments = @{}) {
    $script:last = (& $f.Script @Arguments *>&1 | Out-String)
}
function Commit-File($f, [string]$Name, [string]$Text) {
    Write-Text (Join-Path $f.Work $Name) $Text
    Rgit $f.Work @('add', '-A') | Out-Null
    Rgit $f.Work @('commit', '-q', '-m', "change $Name") | Out-Null
}
function Remote-Ref($f, [string]$Ref) {
    $line = @(Rgit $f.Bare @('for-each-ref', '--format=%(objectname)', $Ref))
    if ($line.Count) { $line[0] } else { '' }
}

try {
    New-Item -ItemType Directory -Path $fixture -Force | Out-Null

    # --- dry run is read-only -----------------------------------------------------
    $f = New-Fixture
    $before = Get-Snapshot $f
    Run $f @{ WhatIf = $true }
    Assert-Unchanged $before (Get-Snapshot $f) 'dry run'
    Assert-True (!(Test-Path $f.Trace)) 'Dry run invoked a build.'
    Assert-True ((Count-GhCreates $f) -eq 0) 'Dry run created a release.'
    Assert-True (!(Test-Path (Join-Path $f.Work 'dist'))) 'Dry run created dist.'
    Assert-Throws { & $f.Script -Version v9.8.7 -Config Debug } 'ValidateSet|does not belong' 'config'
    Assert-Throws { & $f.Script -Version store-installer } 'pattern' 'version pattern'

    # --- version and changelog errors ----------------------------------------------
    $f = New-Fixture
    Assert-Throws { & $f.Script -Version v9.8.8 -WhatIf } 'does not match' 'version mismatch'
    Write-Text (Join-Path $f.Work 'CMakeLists.txt') 'project(FastMD LANGUAGES CXX)'
    Rgit $f.Work @('commit', '-qam', 'break version') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'Cannot read the application version' 'no version'
    $f = New-Fixture
    Write-Text (Join-Path $f.Work 'CHANGELOG.md') "# Changelog`n`n## [9.8.6] - 2025-01-01`n- Older.`n"
    Rgit $f.Work @('commit', '-qam', 'no section') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'exactly one section' 'missing section'
    Write-Text (Join-Path $f.Work 'CHANGELOG.md') "# Changelog`n`n## [9.8.7] - 2026-01-01`n`n## [9.8.6] - 2025-01-01`n- Older.`n"
    Rgit $f.Work @('commit', '-qam', 'empty section') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'empty' 'empty section'
    Write-Text (Join-Path $f.Work 'CHANGELOG.md') "## [9.8.7] - a`n- x`n## [9.8.7] - b`n- y`n"
    Rgit $f.Work @('commit', '-qam', 'duplicate') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'exactly one section' 'duplicate section'

    # --- repository state guards -----------------------------------------------------
    $f = New-Fixture
    Write-Text (Join-Path $f.Work 'untracked.txt') 'x'
    Assert-Throws { & $f.Script -WhatIf } 'not clean' 'untracked file'
    Remove-Item (Join-Path $f.Work 'untracked.txt')
    Add-Content (Join-Path $f.Work 'CHANGELOG.md') 'edit'
    Assert-Throws { & $f.Script -WhatIf } 'not clean' 'modified file'
    Rgit $f.Work @('checkout', '--', 'CHANGELOG.md') | Out-Null
    Rgit $f.Work @('checkout', '-q', '-b', 'topic') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } "current branch is 'topic'" 'wrong branch'
    Rgit $f.Work @('checkout', '-q', 'main') | Out-Null
    Rgit $f.Work @('config', 'remote.origin.url', 'git@github.com:skypediacode/other-repo.git') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'origin must be' 'wrong origin'
    Rgit $f.Work @('config', 'remote.origin.url', 'git@github.com:skypediacode/fastmd.git') | Out-Null
    Set-Content (Join-Path $f.State 'login.txt') 'someone-else'
    Assert-Throws { & $f.Script -WhatIf } 'active gh account' 'wrong account'
    Remove-Item (Join-Path $f.State 'login.txt')

    # --- divergence --------------------------------------------------------------------
    $f = New-Fixture
    $other = Join-Path $f.Base 'other'
    & git clone -q $f.Bare $other
    Rgit $other @('config', 'user.name', 'Other') | Out-Null
    Rgit $other @('config', 'user.email', 'o@example.invalid') | Out-Null
    Write-Text (Join-Path $other 'remote-only.txt') 'x'
    Rgit $other @('add', '-A') | Out-Null
    Rgit $other @('commit', '-q', '-m', 'remote only') | Out-Null
    Rgit $other @('push', '-q', 'origin', 'main') | Out-Null
    $before = Get-Snapshot $f
    Assert-Throws { & $f.Script -WhatIf } 'behind or diverged' 'remote ahead (commit unavailable locally)'
    Rgit $f.Work @('fetch', '-q', 'origin') | Out-Null
    Assert-Throws { & $f.Script -WhatIf } 'behind or diverged' 'behind'
    Rgit $f.Work @('update-ref', '-d', 'refs/remotes/origin/main') | Out-Null
    Commit-File $f 'local-only.txt' 'y'
    Assert-Throws { & $f.Script -WhatIf } 'behind or diverged' 'diverged'
    Assert-True ((Remote-Ref $f 'refs/tags') -eq '') 'divergence published a tag'

    # --- API failure -------------------------------------------------------------------
    $f = New-Fixture
    Set-Content (Join-Path $f.State 'api-fail') 'x'
    $before = Get-Snapshot $f
    Assert-Throws { & $f.Script } 'GitHub CLI, network or authentication failure' 'api failure'
    Assert-Unchanged $before (Get-Snapshot $f) 'api failure'
    Assert-True (!(Test-Path $f.Trace)) 'API failure still built.'

    # --- build failure publishes nothing --------------------------------------------------
    $f = New-Fixture
    Set-Content (Join-Path $f.Work 'fail') 'x'
    $remoteBefore = (Get-Snapshot $f).RemoteRefs
    Commit-File $f 'feature.txt' 'new work'
    Assert-Throws { & $f.Script } 'Mock packaging failed' 'build failure'
    Assert-True ((Get-Snapshot $f).RemoteRefs -eq $remoteBefore) 'Build failure pushed something.'
    Assert-True ((Count-GhCreates $f) -eq 0) 'Build failure created a release.'
    Assert-True (@(Rgit $f.Work @('tag')).Count -eq 0) 'Build failure created a tag.'
    Remove-Item (Join-Path $f.Work 'fail'), $f.Trace
    Remove-Item $f.Trace -ErrorAction SilentlyContinue

    # --- full release, notes and ordering ----------------------------------------------------
    $f = New-Fixture
    Commit-File $f 'feature.txt' 'new work'
    $head = @(Rgit $f.Work @('rev-parse', 'HEAD'))[0]
    Run $f @{ Clean = $true }
    $calls = @(Get-Content $f.Trace) -join ','
    Assert-True ($calls -eq 'sync:x64,sync:arm64,x64|9.8.7.0|Release|True|True,arm64|9.8.7.0|Release|True|True') "Unexpected build trace: $calls"
    Assert-True ((Remote-Ref $f "refs/heads/main") -eq $head) "main was not pushed. head=[$head] remote=[$(Remote-Ref $f "refs/heads/main")]"
    Assert-True (@(Rgit $f.Bare @('cat-file', '-t', 'refs/tags/v9.8.7'))[0] -eq 'tag') 'Remote tag is not annotated.'
    Assert-True (@(Rgit $f.Bare @('rev-parse', 'refs/tags/v9.8.7^{commit}'))[0] -eq $head) 'Tag points to the wrong commit.'
    Assert-True (@(Rgit $f.Bare @('for-each-ref', 'refs/tags')).Count -eq 1) 'Unexpected remote tags.'
    Assert-True ((Count-GhCreates $f) -eq 1) 'Release was not created exactly once.'
    $createLine = Get-Content (Join-Path $f.State 'gh.log') | Where-Object { $_ -like 'release create*' }
    Assert-True ($createLine -match '--verify-tag' -and $createLine -match 'FastMD 9\.8\.7' -and $createLine -notmatch '\.(msix|msixupload|zip|exe)') 'Release arguments are wrong.'
    $notesBytes = [IO.File]::ReadAllBytes((Join-Path $f.State 'notes-captured.md'))
    Assert-True (!($notesBytes.Length -ge 3 -and $notesBytes[0] -eq 0xEF -and $notesBytes[1] -eq 0xBB)) 'Notes have a BOM.'
    $body = [Text.Encoding]::UTF8.GetString($notesBytes)
    Assert-True ($body.Contains("Current fix caf$eacute.") -and !$body.Contains('Older fix.') -and !$body.Contains('## [')) 'Wrong release notes.'
    Assert-True (@(Get-ChildItem (Join-Path $f.Work 'dist') -Filter '*.zip').Count -eq 0) 'Unexpected ZIP.'
    Assert-True (Test-Path (Join-Path $f.Work 'dist\build-receipt.json')) 'Missing build receipt.'
    Assert-True (@(Rgit $f.Work @('status', '--porcelain')).Count -eq 0) 'Release left the tree dirty.'

    # --- rerun: everything published -> no change, no build ------------------------------------
    Remove-Item $f.Trace
    $before = Get-Snapshot $f
    Run $f
    Assert-True (!(Test-Path $f.Trace)) 'Rerun of a published release built again.'
    Assert-Unchanged $before (Get-Snapshot $f) 'completed rerun'
    Assert-True ((Count-GhCreates $f) -eq 1) 'Rerun created another release.'

    # --- rerun: local tag, not pushed --------------------------------------------------------------
    $f = New-Fixture
    Rgit $f.Work @('tag', '-a', 'v9.8.7', '-m', 'FastMD 9.8.7') | Out-Null
    $tagObject = @(Rgit $f.Work @('rev-parse', 'refs/tags/v9.8.7'))[0]
    Run $f
    Assert-True ((Remote-Ref $f 'refs/tags/v9.8.7') -eq $tagObject) 'Existing local tag was not pushed as is.'
    Assert-True ((Count-GhCreates $f) -eq 1) 'Release missing after pushing the local tag.'

    # --- rerun: tag pushed, no release ----------------------------------------------------------------
    $f = New-Fixture
    Rgit $f.Work @('tag', '-a', 'v9.8.7', '-m', 'FastMD 9.8.7') | Out-Null
    Rgit $f.Work @('push', '-q', 'origin', 'refs/tags/v9.8.7:refs/tags/v9.8.7') | Out-Null
    Run $f
    Assert-True ((Count-GhCreates $f) -eq 1) 'Release was not created from the existing tag.'

    # --- rerun failures never overwrite -------------------------------------------------------------------
    $f = New-Fixture
    Set-Content (Join-Path $f.State 'releases.txt') '{"tag_name":"v9.8.7","draft":true,"url":"https://example.invalid/d"}'
    $before = Get-Snapshot $f
    Assert-Throws { & $f.Script } 'draft release' 'draft release'
    Assert-Unchanged $before (Get-Snapshot $f) 'draft release'
    Set-Content (Join-Path $f.State 'releases.txt') '{"tag_name":"v9.8.7","draft":false,"url":"https://example.invalid/r"}'
    $before = Get-Snapshot $f
    Assert-Throws { & $f.Script } 'without a remote tag' 'release without tag'
    Assert-Unchanged $before (Get-Snapshot $f) 'release without tag'
    $f = New-Fixture
    Rgit $f.Work @('tag', '-a', 'v9.8.7', '-m', 'old') | Out-Null
    Rgit $f.Work @('push', '-q', 'origin', 'refs/tags/v9.8.7:refs/tags/v9.8.7') | Out-Null
    Rgit $f.Work @('tag', '-d', 'v9.8.7') | Out-Null
    Commit-File $f 'later.txt' 'z'
    $before = Get-Snapshot $f
    Assert-Throws { & $f.Script } 'Remote tag v9\.8\.7 points to' 'remote tag on another commit'
    Assert-Unchanged $before (Get-Snapshot $f) 'remote tag elsewhere'
    $f = New-Fixture
    Rgit $f.Work @('tag', '-a', 'v9.8.7', '-m', 'old') | Out-Null
    Commit-File $f 'later.txt' 'z'
    Assert-Throws { & $f.Script } 'Local tag v9\.8\.7 points to' 'local tag on another commit'
    Assert-True (!(Test-Path $f.Trace)) 'Tag conflict still built.'
    $f = New-Fixture
    Rgit $f.Work @('tag', 'v9.8.7') | Out-Null
    Assert-Throws { & $f.Script } 'not an annotated tag' 'lightweight tag'

    # --- -SkipBuild -----------------------------------------------------------------------------------------
    $f = New-Fixture
    Assert-Throws { & $f.Script -SkipBuild } 'No build receipt' 'no receipt'
    Set-Content (Join-Path $f.State 'create-fail') 'x'
    Assert-Throws { & $f.Script } 'gh release create' 'release creation failure'
    Assert-True ((Remote-Ref $f 'refs/tags/v9.8.7') -ne '') 'Tag should already be pushed before the release failure.'
    Remove-Item (Join-Path $f.State 'create-fail'), $f.Trace
    Run $f @{ SkipBuild = $true }
    Assert-True (!(Test-Path $f.Trace)) '-SkipBuild rebuilt.'
    Assert-True ((Count-GhCreates $f) -eq 2) '-SkipBuild did not resume to the release (one failed attempt plus one).'
    $f = New-Fixture
    Set-Content (Join-Path $f.State 'create-fail') 'x'
    Assert-Throws { & $f.Script } 'gh release create' 'setup failure'
    Remove-Item (Join-Path $f.State 'create-fail')
    Add-Content (Join-Path $f.Work 'dist\FastMD-9.8.7.0.msix') 'tampered'
    Assert-Throws { & $f.Script -SkipBuild } 'does not match the build receipt' 'package hash mismatch'
    Remove-Item (Join-Path $f.Work 'dist\FastMD-9.8.7.0-arm64.msixupload')
    Assert-Throws { & $f.Script -SkipBuild } 'Package missing|does not match' 'missing package'
    $f = New-Fixture
    Set-Content (Join-Path $f.State 'create-fail') 'x'
    Assert-Throws { & $f.Script } 'gh release create' 'setup failure 2'
    Remove-Item (Join-Path $f.State 'create-fail')
    Rgit $f.Work @('tag', '-d', 'v9.8.7') | Out-Null
    Rgit $f.Work @('push', '-q', 'origin', ':refs/tags/v9.8.7') | Out-Null
    Commit-File $f 'after-build.txt' 'q'
    Assert-Throws { & $f.Script -SkipBuild } 'Build receipt is for commit' 'receipt commit mismatch'
    $receiptPath = Join-Path $f.Work 'dist\build-receipt.json'
    $text = (Get-Content $receiptPath -Raw).Replace('9.8.7', '9.8.6')
    Set-Content $receiptPath $text
    Assert-Throws { & $f.Script -SkipBuild } 'Build receipt is for version' 'receipt version mismatch'

    Write-Host 'PASS: dry run, version/changelog/repository guards, divergence, API failure, build failure, notes, publish order, rerun states, and -SkipBuild verification.' -ForegroundColor Green
}
finally {
    $env:PATH = $originalPath
    Remove-Item Env:\FASTMD_MOCK_GH -ErrorAction SilentlyContinue
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolved -Leaf) -match '^fastmd-release-test-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
