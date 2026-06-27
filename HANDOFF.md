# HANDOFF

## Current Status

Build passing (Release). All 14 roadmap phases implemented and running.
UI modernization complete — Material Icons toolbar, light theme, tab bar polished.

---

## Recently Completed

- **Full project built** from scratch: C++20 + Qt 6.11.1 + MD4C 0.5.2, all 14 phases.
- **Material Icons toolbar**: downloaded `MaterialIcons-Regular.ttf` via Qt resource system (`resources/resources.qrc`). Icons rendered via `src/IconHelper.h` at 26px. Toolbar is light-colored (Notepad++ style, `#f3f3f3`).
- **Toolbar buttons**: New, Open, Save, Recent | Bold, Italic, Strikethrough | H1, H2, H3 | Bullet list, Numbered list, Checklist, Blockquote | Code, Table, Link, Image | Find, Preview, Export PDF.
- **Separate H1/H2/H3 slots** added to `MarkdownEditor` (toggle on/off per level). Also added `fmtChecklist`, `fmtTable`, `fmtImage`.
- **Light tab bar** in light theme (`#e0e0e0` background, white selected tab with blue bottom border).
- **Double-click on vacant tab bar area** opens a new untitled tab (`TabBar` subclass in `TabWidget.h`).
- **Theme-aware icons**: `updateToolbarIcons(bool dark)` regenerates all toolbar icons on theme switch (dark = `#cccccc`, light = `#444444`).
- **PDF Export Setup**: Configurable page setup for PDF export (page size, orientation, margins, font size) via `ExportManager` using `PdfExportOptions`. Oversized images correctly scale to fit page width.

---

## Active Work

Nothing in progress. Stable state.

---

## Known Issues

- Toolbar icon area is slightly clipped on the right edge (Find/Preview buttons may not be fully visible at default window width) — cosmetic only.
- `windeployqt` POST_BUILD may emit a Vulkan warning (non-blocking).
- MD4C `/MT` vs `/MD` CRT warning at build time — harmless, Qt forces dynamic CRT.

---

## Next Recommended Tasks

1. Test all toolbar formatting buttons on actual Markdown content.
2. Verify dark theme icon colors and tab bar appearance.
3. Add keyboard shortcut hints to toolbar tooltips (currently some shortcuts missing).
4. Consider adding a `+` button at the right end of the tab bar as a visible affordance for new tab.
5. Test PDF and HTML export end-to-end.
6. Verify auto-reload (file watcher) works correctly on external edits.

---

## Modified Areas

| File | Change |
|---|---|
| `src/IconHelper.h` | New — Material Icons + text icon renderer |
| `src/Stylesheet.h` | Light toolbar QSS, light tab bar QSS, per-theme toolbar |
| `src/MainWindow.h/.cpp` | `createToolbar()` rewritten with icon registry; `updateToolbarIcons()` added |
| `src/MarkdownEditor.h/.cpp` | Added `fmtH1/H2/H3`, `fmtChecklist`, `fmtTable`, `fmtImage` |
| `src/TabWidget.h/.cpp` | Added `TabBar` subclass; double-click vacant area → new tab |
| `resources/resources.qrc` | New Qt resource file embedding the Material Icons font |
| `resources/fonts/MaterialIcons-Regular.ttf` | Downloaded from Google GitHub |
| `CMakeLists.txt` | Added `resources/resources.qrc` to SOURCES |

---

## Important Notes

- Build command: run `build.ps1` from `E:\myApps\fastmd\` in PowerShell.
- Qt at `C:\Qt\6.11.1\msvc2022_64`; CMake at `C:\Qt\Tools\CMake_64\bin\cmake.exe`.
- MSVC Build Tools at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
- Executable: `E:\myApps\fastmd\build\FastMD.exe`.
- Do NOT `git commit` or `git push` unless the user explicitly asks.
