# Updating the bundled external tools

FastMD bundles two external tools. Neither binary is committed to git (the
repo's `*.exe` rule excludes them); both are produced by
`scripts\sync-bundled-tools.ps1` at packaging time.

| Tool       | What it powers            | Where it comes from                                  |
| ---------- | ------------------------- | ---------------------------------------------------- |
| **Pandoc** | Export DOCX/EPUB/RTF/ODT/LaTeX | Prebuilt binary downloaded from the [GitHub release](https://github.com/jgm/pandoc/releases) |
| **AnyDoc** | File ▸ Import as Markdown | A crates.io **library**, compiled into our own wrapper (`tools/anydoc-wrapper/`) with `cargo` |

That difference is the only thing you need to remember: Pandoc is *downloaded*,
AnyDoc is *rebuilt*. Updating either one is the same three steps.

### Why AnyDoc is not downloaded like Pandoc

AnyDoc *does* have a GitHub releases page, and it looks like it should work the
same way — it doesn't. Checked at v0.2.4, the release assets are:

- 7 × `anydoc.<platform>.node` — Node.js N-API native addons
- 7 × `firecrawl_anydoc-*.whl` — Python wheels
- 1 × source tarball

There is **no standalone executable**. The `npx @firecrawl/anydoc` CLI in the
project's README is a Node package that loads `anydoc.win32-x64-msvc.node`, so
using it would mean shipping Node — one of FastMD's hard exclusions (AGENTS.md
Rule 1). Compiling the crate into our own wrapper is what keeps the importer
native and Node-free. Do not "simplify" this into a download step.

The releases page is still the best place to *watch* for new versions;
`-Check` below reads it for you.

---

## The routine update

### 0. Is there a new version?

```powershell
.\scripts\sync-bundled-tools.ps1 -Check
```

Reads both upstream release feeds and reports what you have pinned against
what's available. Changes nothing.

```
  Pandoc  3.12.1 is current.
  AnyDoc  0.2.4 is current.
```

### 1. Change one number

Open **`scripts\sync-bundled-tools.ps1`** and edit the param block at the top:

```powershell
[string]$PandocVersion = "3.12.1",    # https://github.com/jgm/pandoc/releases
[string]$AnyDocVersion = "0.2.4",   # https://crates.io/crates/anydoc/versions
```

This is the only place these versions are written. Do not edit them in
`build-and-create-portable-app.ps1`, in `Cargo.toml`, or in the Licenses
dialog — all three are updated for you.

### 2. Run it

```powershell
.\scripts\sync-bundled-tools.ps1
```

You can limit it to one tool or force a refetch:

```powershell
.\scripts\sync-bundled-tools.ps1 -Tool pandoc
.\scripts\sync-bundled-tools.ps1 -Tool anydoc -Force
```

It is idempotent: when the pinned version is already in place it prints one
line and exits, so it costs nothing on a normal release build.

What it does:

- **Pandoc** — downloads `pandoc-<version>-windows-x86_64.zip`, finds
  `pandoc.exe` inside it (by search, not by a hardcoded folder layout), installs
  it to `tools\pandoc\`, and verifies the binary reports the version you asked
  for.
- **AnyDoc** — rewrites the `anydoc = "=x.y.z"` pin in
  `tools\anydoc-wrapper\Cargo.toml`, then builds the requested explicit Rust
  target: x64 uses `x86_64-pc-windows-msvc`; Arm64 uses
  `aarch64-pc-windows-msvc`. The outputs are installed at
  `tools\anydoc\x64\fastmd-anydoc.exe` and
  `tools\anydoc\arm64\fastmd-anydoc.exe`, respectively, with a separate
  `SHA256.txt` beside each. `VERSION.txt` stays shared because the pinned crate
  version is shared.
- **Both** — updates their version metadata and SHA-256 files (AnyDoc keeps a
  shared `VERSION.txt` plus one hash per architecture; packaging re-verifies
  those hashes), and regenerates
  `src\BundledToolVersions.h`, which is what Help ▸ Licenses displays.

### 3. Build, smoke-test, commit

```powershell
.\scripts\build.ps1
```

Then check the tool you touched — export a DOCX for Pandoc, import a `.docx`
for AnyDoc — and commit the files the sync changed. Build both AnyDoc targets
before a release:

```powershell
.\scripts\sync-bundled-tools.ps1 -Tool anydoc -Architecture x64
.\scripts\sync-bundled-tools.ps1 -Tool anydoc -Architecture arm64
```

```
tools/pandoc/VERSION.txt          tools/anydoc/VERSION.txt
tools/pandoc/SHA256.txt           tools/anydoc/x64/SHA256.txt
                                  tools/anydoc/arm64/SHA256.txt
                                  tools/anydoc-wrapper/Cargo.toml
                                  tools/anydoc-wrapper/Cargo.lock
src/BundledToolVersions.h
scripts/sync-bundled-tools.ps1
```

A Store release (`scripts\build-store-release.ps1`, also available through the
legacy `build-all-and-publish-to-github.ps1` wrapper) runs the sync first, so it also picks up a version bump on its own — step 1 above is still
the only edit you make.

---

## Requirements

- **Pandoc** needs nothing but a network connection.
- **AnyDoc** needs the Rust toolchain ([rustup.rs](https://rustup.rs `winget install Rustlang.Rustup`), 1.88+) and both targets on the toolchain selected for `tools\anydoc-wrapper`:
  `x86_64-pc-windows-msvc` and `aarch64-pc-windows-msvc`. The sync script checks
  that exact selected toolchain and prints the precise `rustup target add ... --toolchain ...`
  command when one is missing.
  Without it the sync prints a warning and skips the wrapper; FastMD builds and
  runs fine, and Import as Markdown reports that the converter is missing. Rust
  is **not** a FastMD build requirement — only a requirement for shipping the
  importer.

---

## What can still go wrong

Most upgrades are boring. These are the ones that aren't.

### Pandoc: the download 404s

The release tag has no `windows-x86_64.zip` asset, or the version doesn't exist.
Check the [releases page](https://github.com/jgm/pandoc/releases) — the tag is
the bare version (`3.11`), with no `v` prefix.

### Pandoc: a major version changes CLI behavior

The sync only checks that the binary *is* the version you asked for; it cannot
know that Pandoc changed a flag. After a major bump, export one file per format
(DOCX, EPUB, RTF, ODT, LaTeX) before releasing. `ExportManager::exportViaPandoc()`
is where the arguments live.

### AnyDoc: `cargo build` fails after the bump

The crate moved its API. `tools/anydoc-wrapper/src/main.rs` uses
`Format::from_bytes`, `Format::from_path`, `to_document`, `to_markdown_bytes`,
`Error::code()`, and `Document::assets` — fix whichever moved. This one is loud
and safe: you cannot ship a wrapper that doesn't compile.

### AnyDoc: error codes get renamed — *silent*

`ImportManager::errorMessage()` (`src/ImportManager.cpp`) matches the wrapper's
error codes as **strings**: `unsupported`, `encrypted`, `resourceLimit`,
`outputConflict`, `assetSupportUnavailable`. If upstream renames one, nothing
fails to compile — the user just gets raw untranslated crate English instead of
FastMD's message. Diff the crate's error codes on every bump.

This is not hypothetical: v0.1.6's own release note was *"binding error codes"*
([#25](https://github.com/firecrawl/anydoc/pull/25)), so it is an actively
changing surface. Read the release notes for this specifically.

### AnyDoc: new formats appear — *silent*

A new version may support formats that `ImportManager::supportedExtensions()`
doesn't list, which makes them invisible in the Import dialog. That list is the
only place to add them: the Import and Insert file filters and
`handlesExtension()` are all derived from it, and `tests/ImportManagerTest.cpp`
iterates it rather than repeating it.

Removing an extension from it has a second effect worth knowing:
`Insert ▸ Content from File…` routes on `handlesExtension()`, so a format that
leaves the list is no longer converted — it is offered to
`loadEditableTextFile()` and inserted verbatim if it decodes as text. That is
correct for a format AnyDoc genuinely dropped, but it is why `rtf` and `csv`
must stay listed even though both are plain text.

### Either: the license text changes

`resources/licenses/pandoc-GPL-2.0.txt` and `resources/licenses/anydoc-MIT.txt`
are committed copies. On a major bump, confirm upstream's license and copyright
year still match. `tools/pandoc/LICENSE.txt` and the two `NOTICE.txt` files are
curated by hand and are deliberately not overwritten by the sync.

---

## The upgrade that isn't routine: AnyDoc asset support

Embedded images currently become **alt text** and the bytes are dropped. That is
upstream behavior, not a FastMD choice — `render_image()` in the crate renders
`ImageSource::Asset` as bare alt text, with the comment *"Markdown cannot embed
bytes, and the bytes stay available in `Document::assets`."*

We cannot work around it from outside the crate. `to_document()` hands back a
`Document` whose `assets` and `blocks` are fully public, so the image sources
*could* be rewritten to point at extracted files — but `mod render;` is
**private**, so there is no way to render a modified `Document` back to Markdown.
The only public entry points are `to_markdown`, `to_markdown_bytes` and
`to_document`. Extracting the bytes anyway would just leave orphan files that no
Markdown references.

So the wrapper reports `droppedImages` (the `Document::assets` count) and FastMD
says so in the status bar. Watch for either of these upstream:

- `render` becoming public, or
- an asset-aware serializer entry point.

Either one makes real extraction possible, and that is a code change rather than
a version bump: extract to a `<name>.assets` folder resolved with
`WorkspaceFileOps::uniqueDestinationPath()` (same base name as the `.md`, so
`Report (2).md` pairs with `Report (2).assets`), rewrite the image sources to
URL-encoded relative paths, and drop `droppedImages`. Until then, do not add
`.assets` plumbing that can only ever produce empty folders.
