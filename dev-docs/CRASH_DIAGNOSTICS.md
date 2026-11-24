# Crash diagnostics

How to turn a FastMD crash into a symbolized stack. None of this changes
FastMD's runtime behavior.

## Symbols

Release builds produce PDBs (see the comment at the top of `CMakeLists.txt`):

| Step | Flags |
| --- | --- |
| Compile | `/O2 /Ob2 /DNDEBUG /Z7` (`/Z7` adds debug records only) |
| Link | `/INCREMENTAL:NO /DEBUG:FULL /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%` |
| Link, shipped binaries | `/PDBSTRIPPED:<name>.public.pdb` |

- `/OPT:REF /OPT:ICF` restore what `/DEBUG` turns off, so the generated code is
  unchanged. Do not drop them.
- `/PDBALTPATH:%_PDB%` records only `FastMD.pdb`, not the build machine's path.
- `<name>.pdb` is the full PDB (functions, source lines, locals, types).
  `<name>.public.pdb` has public symbols only, with the same signature; it is
  what the Store receives.

PDBs never ship in the ZIP, installer or MSIX; the packaging scripts fail if
one would. `build-and-create-portable-app.ps1` archives every build's binaries
and PDBs in `dist\symbols\FastMD-<version>-<arch>-<link timestamp>\`. Keep those
folders for every release: a dump can only be resolved with the PDB from the
exact link that produced the binary.

## Microsoft Store

`build-msix.ps1` writes `dist\FastMD-<version>[-arm64].msixupload` next to the
`.msix`. Upload the `.msixupload` files (x64 and Arm64) to Partner Center
instead of the bare `.msix`. It holds the same signed package plus an
`.appxsym` of the public PDBs, which the Health report needs for stack traces.
Partner Center may offer a CAB (dump) download in a failure's details; open it
as below.

## Local dumps

Windows Error Reporting can keep full dumps of FastMD crashes on a development
or test machine. From an elevated PowerShell:

```powershell
.\scripts\enable-crash-dumps.ps1            # full dumps, at most 10, in %LOCALAPPDATA%\FastMD\CrashDumps
.\scripts\enable-crash-dumps.ps1 -Disable   # remove the setting (existing dumps are kept)
```

It writes only `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\FastMD.exe`
and covers Store, installer and portable builds. If the shared `LocalDumps` key
does not exist, creating it would also turn on default minidumps for every other
application, so the script stops unless `-CreateLocalDumpsKey` is given.

Full dumps contain the whole process memory: document text, unsaved edits,
paths, clipboard data and terminal input/output. Keep them private. Each can be
several hundred MB.

## Reading a dump in WinDbg

Install WinDbg (`winget install Microsoft.WinDbg`), open the `.dmp` and run:

```text
.symfix C:\Symbols
.sympath+ <repository>\dist\symbols\FastMD-3.6.9.0-x64-<stamp>
.sympath+ C:\Qt\6.11.1\msvc2022_64\bin
.exepath+ <repository>\dist\symbols\FastMD-3.6.9.0-x64-<stamp>
.reload /f
!analyze -v
.ecxr
kn
```

- `lmvm FastMD` shows the binary's timestamp, which names the symbols folder,
  and whether its PDB matched (`Loaded symbol image file` / `private pdb symbols`).
- `.frame <n>` then `dv /t` shows a frame's locals; `.srcpath` pointed at a
  checkout of the release commit shows its source.
- Qt frames resolve only with Qt's own PDBs: install *Qt Debug Information
  Files* for 6.11.1 (MaintenanceTool) and keep the `C:\Qt\...\bin` entry above.
  Use `msvc2022_arm64` for Arm64 dumps.
- Builds older than this change shipped without PDBs. Their FastMD frames show
  only as `FastMD+0x...` offsets; Qt and Windows frames still resolve.
