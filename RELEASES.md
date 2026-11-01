# FastMD releases and updates

[Microsoft Store](https://apps.microsoft.com/detail/9N4DSD4WNZ9D) is the primary
installation and automatic update channel for FastMD.

The permanent [FastMD Installer](https://github.com/skypediacode/fastmd/releases/download/store-installer/FastMD.Installer.exe)
launches Store installation of the version currently available to you. It requires
Microsoft Store access and is not a standalone/offline application installer.
Its address and `store-installer` release tag stay the same across app versions.

Version releases (3.7, 3.8, 4.0, and later) go through Microsoft Store. GitHub may
host corresponding release notes, without new Portable, MSI, or standalone EXE
assets. The permanent launcher is uploaded once, separately from version releases.
Existing repository binaries and historical releases are retained as legacy files;
they are no longer refreshed and do not represent the current Store version.

This public repository contains website pages, screenshots, product documentation,
and release notes. The proprietary application source, build tooling, and Store
submission packages live in a separate private repository. Maintainers prepare
public changelog/notes there, review and commit documentation here, and publish
only after approval. No application source or version packages belong here.

See the [changelog](CHANGELOG.md) for version history. Store availability can vary
with certification, rollout, and the user's device; GitHub notes do not guarantee
that a version is available to every Store user yet.
