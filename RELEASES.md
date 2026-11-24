# FastMD releases and updates

[Microsoft Store](https://apps.microsoft.com/detail/9N4DSD4WNZ9D) is the primary
installation and automatic update channel for FastMD.

The permanent [FastMD Installer](https://github.com/skypediacode/fastmd/releases/download/store-installer/FastMD.Installer.exe)
launches Store installation of the version currently available to you. It requires
Microsoft Store access and is not a standalone/offline application installer.
Its address and `store-installer` release tag stay the same across app versions.

Version releases (`vX.Y.Z`, such as 3.7.2) go through Microsoft Store. Each GitHub
release is created by `scripts/build-all-and-publish-to-github.ps1` from the tagged
source and carries that version's changelog section as its notes, without Portable,
MSI, or standalone EXE assets. The permanent launcher is uploaded once, separately
from version releases, and is never modified by them. Historical releases are
unchanged. Old standalone packages do not represent the current Store version.

This repository contains the application source code, build scripts, website pages
(`docs/`), screenshots, maintainer documentation (`dev-docs/`), and release notes.
FastMD is open source software released under the MIT License; see [LICENSE](LICENSE). The release
procedure is described in [scripts/README.md](scripts/README.md#store-release-workflow).

See the [changelog](CHANGELOG.md) for version history. Store availability can vary
with certification, rollout, and the user's device; GitHub notes do not guarantee
that a version is available to every Store user yet.
