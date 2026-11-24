# FastMD AnyDoc wrapper

This is the native Rust bridge FastMD launches for **Import as Markdown**. It
is intentionally separate from the Qt/CMake build: normal FastMD development
and startup do not require Rust, Node, Python, or a network connection.

Build the wrapper with the explicit target that matches the FastMD package.
The release workflow runs these commands from the repository root; `rustup`
must have the target installed for the toolchain selected in this directory:

```powershell
.\scripts\sync-bundled-tools.ps1 -Tool anydoc -Architecture x64
.\scripts\sync-bundled-tools.ps1 -Tool anydoc -Architecture arm64
```

The Rust targets are `x86_64-pc-windows-msvc` (x64) and
`aarch64-pc-windows-msvc` (ARM64). Cargo writes the executables under
`target\<target>\release\fastmd-anydoc.exe`; the sync script stages them at
`tools\anydoc\x64\fastmd-anydoc.exe` and
`tools\anydoc\arm64\fastmd-anydoc.exe`, with a SHA-256 alongside each.
Packaging validates each executable's PE machine type before it enters the
matching portable/MSIX layout. Do not commit the executables.

The wrapper currently rejects documents with embedded assets rather than
silently losing them. Enable asset extraction only when the pinned AnyDoc
revision supplies an asset-aware Markdown serialization API.
