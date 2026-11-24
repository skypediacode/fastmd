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
  download the Store launcher from the `store-installer` release (checked against its
  pinned SHA-256), build the portable ZIPs for x64 and Arm64, and create a GitHub
  Release from that tag carrying the changelog section as notes and three assets:
  `FastMD.Installer.exe`, `FastMD-Portable-<version>-x64.zip` and
  `FastMD-Portable-<version>-arm64.zip`. It never commits.
- MSIX packaging stages runtime assets and the shell extension, creates PRI and
  manifest data, validates packages and native dependencies, signs locally with a
  test certificate, archives symbols and writes MSIX and MSIXUPLOAD files.
  The MSIX packaging reuses the deployment helper with `-SkipArchive`; the portable
  ZIPs come from `build-and-create-portable-app.ps1`, once per architecture.
- Store submission to Partner Center is manual.

## Permanent installer

`FastMD.Installer.exe` is a permanent Store launcher attached once to the
`store-installer` release. It installs the version currently available to the
user through Microsoft Store and is not a standalone installer.
`https://github.com/skypediacode/fastmd/releases/download/store-installer/FastMD.Installer.exe`
(SHA-256 `bbefb47d09f82c88f28c4be25ae32dc2d7d5e45781633d5373e985f925a20a2b`).
Routine tooling only reads that asset: it downloads it to attach a copy to each
version release, checked against the pinned hash, and never changes it.

## Validation

`tests/ReleaseWorkflowTest.ps1` exercises the release script with temporary
repositories and a mocked `gh`. Run it under PowerShell 7 and Windows PowerShell
5.1, together with `scripts/build-store-release.ps1 -WhatIf`. A local build does
not prove Store certification or installer behavior.
