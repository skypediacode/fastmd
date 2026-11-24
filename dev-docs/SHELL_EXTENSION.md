# Shell Extension — "Edit with FastMD"

## What it is

`FastMDShellExtension.dll` adds an **Edit with FastMD** command to the Windows
11 modern Explorer context menu (and the Windows 10 classic menu) for **every
file and every folder**, Notepad++-style — there is no extension allowlist.
It ships **only** inside the MSIX package built by `scripts/build-msix.ps1` —
the portable ZIP (`scripts/build-and-create-portable-app.ps1`) and the Inno
Setup installer (`fastmd.iss`) do not include it and are unaffected.

The DLL is Qt-free (no CRT-heavy work, no blocking calls) because it is
loaded directly into Explorer or a `DllHost.exe` COM surrogate process. It
implements `IExplorerCommand` plus a minimal class factory and
`DllGetClassObject`/`DllCanUnloadNow`. It does not know how to open a file or
folder — it starts `FastMD.exe <path>` and lets the existing single-instance
forwarding (`src/main.cpp`) and startup path handling
(`MainWindow::openFilePaths` in `src/MainWindowFile.cpp`) do the rest:

- A **file** dedupes against already-open tabs, opens as a new tab, and
  brings the existing window forward. If it's not an editable text file (and
  not PDF/HTML), it opens as a raw source tab instead of being handed to the
  OS default app — see "Unsupported files" below.
- A **folder** becomes the Workspace root (`MainWindow::openFilePaths` already
  sets the root for any directory argument — this is the same mechanism used
  for `FastMD.exe <folder>` on the command line).

## CLSID

```
{74C889E4-66D1-4D59-B0FB-114B1740A1F8}
```

This value is declared once in `src/shellext/FastMDShellExtension.cpp`
(`CLSID_FastMDExplorerCommand`) and once in `scripts/build-msix.ps1`
(`$ShellExtensionClsid`, without braces — the MSIX schema requires a bare
GUID). `Test-AppxManifest` in `build-msix.ps1` asserts the manifest's
`com:Class Id` matches this value. If you ever regenerate the CLSID, update
both places.

## Manifest shape

Registration is **manifest-only** — no runtime registry writes, no
`DllRegisterServer`. Two extension blocks are added to the packaged app's
`<Application><Extensions>` in `scripts/build-msix.ps1`'s
`Write-AppxManifest`:

- `windows.comServer` (`com:` namespace) registers the DLL as a
  surrogate-hosted COM class (`ThreadingModel="STA"`).
- `windows.fileExplorerContextMenus` (`desktop4:` namespace, with
  `desktop5:ItemType`/`desktop5:Verb` children — note the namespace switch
  between parent and children, an easy detail to get wrong) maps the
  `EditWithFastMD` verb (bound to the CLSID above) to two `ItemType`s:
  `Type="*"` (the documented "all files" wildcard, equivalent to classic
  `HKCR\*`) and `Type="Directory"` (equivalent to classic `HKCR\Directory`,
  needed separately because `*` only matches files).

Because registration is all-files, there is no extension list to keep in
sync here — `src/FastMDFileTypes.h` (`FASTMD_WRITING_EXTENSIONS`) now only
drives `WorkspaceFileOps::extensionList()` (the Workspace panel's writing
file filter) and is unrelated to this manifest.

## Why packaged COM instead of registry writes

Packaged (MSIX) COM registration is declarative and self-cleaning: Windows
reads the manifest to know the CLSID exists and to load the surrogate on
demand, and removes all of it automatically on uninstall
(`Remove-AppxPackage`). A classic unpackaged shell extension would need
`DllRegisterServer` plus `HKCR`/`HKCU\Software\Classes` registry writes for
the CLSID and verb, which is exactly the kind of runtime registry
side-effect FastMD's packaging story avoids elsewhere (see `AGENTS.md`).

## Package architecture

The DLL is built by the same toolchain as `FastMD.exe` and
`FastMDLauncher.exe`, so it always matches the package architecture. Use
`scripts\build-msix.ps1 -Architecture arm64` to create an Arm64 package; its
DLL is taken from `build-arm64\`. Do not copy an x64 DLL into an Arm64 package.

## Scope: all files and folders, not a type list

The command supports every file, matching Notepad++'s own "Edit with
Notepad++" entry, plus every folder (to open it as the Workspace root).
`FastMDExplorerCommand::GetState` / `resolveEligiblePath` in
`src/shellext/FastMDShellExtension.cpp` only check that exactly one item is
selected and it resolves to a real filesystem path (`SIGDN_FILESYSPATH`) —
there is no extension check and no folder exclusion.
`src/FastMDFileTypes.h` is unrelated to this command; it exists solely for
`WorkspaceFileOps::extensionList()` (the Workspace panel's writing file
filter). Do not touch `DocumentModel::modeForPath` either — its `PlainText`
fallback is intentionally open-ended and belongs to a different concern
(editor mode selection, not shell registration).

## Unsupported files

`MainWindow::openFilePath(path, openUnsupportedAsSource)`
takes a second parameter set to `true` only by `openFilePaths()` — the
command-line/single-instance-forwarding entry point every "Edit with FastMD"
launch goes through (same as file-type-association double-click and
`FastMD.exe <path>` from a terminal). When set, a file that isn't PDF/HTML and
fails `SyntaxDefinitionResolver::isEditableTextFile()` (binary content, or no
matching syntax/MIME type) opens as a raw source tab via
`MainWindow::openFileAsText()` instead of being hedged off to the OS default
app. This is the same code path as the Workspace panel's **Open as Source Code**
context-menu item (`WorkspacePanelController::textEditorRequested`); the
Workspace panel's own double-click-to-open (`fileActivated`) does **not** set
this flag, so double-clicking an unsupported file in the Workspace tree still
opens it externally — "Open as Source Code" is the explicit escape hatch there.

## Debugging

1. Install the signed MSIX (`dist/FastMD-<version>.msix`), trusting
   `cert/FastMD-Test.cer` in *Current User → Trusted People* if this is a new
   machine (build-msix.ps1 prints this reminder).
2. Explorer caches shell extensions aggressively. After every
   install/uninstall/reinstall during testing, restart Explorer:
   ```
   taskkill /f /im explorer.exe
   start explorer.exe
   ```
3. If the verb still doesn't appear: confirm the package is actually
   installed and check its identity —
   ```powershell
   Get-AppxPackage skypediacode.FastMD* | Format-List
   ```
4. Check Event Viewer's **Microsoft-Windows-AppModel-Runtime** log (under
   *Applications and Services Logs*) for activation or COM-surrogate
   failures.
5. To debug the DLL itself, attach a debugger to `explorer.exe` (Windows 11
   modern menu) or the `DllHost.exe` surrogate process hosting the CLSID
   (visible in Task Manager's Details tab while the context menu is open),
   and set breakpoints in `FastMDExplorerCommand::GetState` / `Invoke`.

## Known limitations

- `windows.fileExplorerContextMenus` requires Windows 10 1809+
  (`10.0.17763.0`, already the manifest's `MinVersion`). Below that version
  it is silently ignored — no command, no error.
- The command is single-selection only and hides itself for multi-selection
  and virtual/namespace items with no real filesystem path
  (`SIGDN_FILESYSPATH` fails, e.g. a zip archive member) — see
  `FastMDExplorerCommand::GetState` / `resolveEligiblePath` in
  `src/shellext/FastMDShellExtension.cpp`. Every other file or folder,
  regardless of extension, shows the command.
