# Store distribution and releases

FastMD ships through Microsoft Store. This repository (`skypediacode/fastmd`)
holds the application source, build scripts, website (`docs/`), maintainer
documentation (`dev-docs/`) and release notes. See
[script usage](../scripts/README.md#store-release-workflow) for the commands.

## Workflow

- `scripts/build-store-release.ps1` derives the version from `CMakeLists.txt`,
  rejects a conflicting explicit version, syncs bundled tools for both
  architectures and builds x64 then Arm64 Store packages. It never publishes.
- `scripts/build-all-and-publish-to-github.ps1` wraps it: preflight, build and
  validate, push `main` (fast-forward only), push the annotated `vX.Y.Z` tag, and
  create a GitHub Release from that tag carrying the changelog section as notes.
  No release assets are uploaded. It never commits.
- MSIX packaging stages runtime assets and the shell extension, creates PRI and
  manifest data, validates packages and native dependencies, signs locally with a
  test certificate, archives symbols and writes MSIX and MSIXUPLOAD files.
  The portable ZIP and Inno Setup installer are retired and not part of the
  workflow; the deployment helper is reused with `-SkipArchive`.
- Store submission to Partner Center is manual.

## Permanent installer

`FastMD.Installer.exe` is a permanent Store launcher attached once to the
`store-installer` release. It installs the version currently available to the
user through Microsoft Store and is not a standalone installer.
`https://github.com/skypediacode/fastmd/releases/download/store-installer/FastMD.Installer.exe`
(SHA-256 `bbefb47d09f82c88f28c4be25ae32dc2d7d5e45781633d5373e985f925a20a2b`).
Routine tooling never touches that tag, release or asset.

## Validation

`tests/ReleaseWorkflowTest.ps1` exercises the release script with temporary
repositories and a mocked `gh`. Run it under PowerShell 7 and Windows PowerShell
5.1, together with `scripts/build-store-release.ps1 -WhatIf`. A local build does
not prove Store certification or installer behavior.
