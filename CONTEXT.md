# AGENTS.md — FastMD

Long-term repository knowledge for AI agents and maintainers.

---

## Project Context

**FastMD** is a native, lightweight Markdown editor for Windows built with C++20 and Qt 6 Widgets.

- Target users: developers and writers who want a fast, distraction-free Markdown editor.
- Philosophy: Speed > Simplicity > Stability > Maintainability > Features. Typing performance is inviolable.
- Hard exclusions (never add): Electron, Qt WebEngine, QML, SQLite, plugins, cloud sync, AI features, Git integration, project explorer/sidebar.

---

## Architecture

### Pipeline

Markdown rendering goes through `ExportManager`, but with **two math render modes**
(`MathRenderMode`) so the lightweight in-app preview and the browser/export paths
are kept separate:

```
                                    ┌─ UnicodePreview ─→ HTML body ─→ PreviewWidget (QTextBrowser)
Markdown text → MD4C (md4c-html) ──┤
                                    └─ KatexOutput ────→ HTML body (raw LaTeX) ─→ Browser Preview
                                                                                →  HTML export (file)
                                                                                →  PDF export (headless Chrome/Edge)
```

- `markdownToHtml(md, mode, &mathDetected)` — `UnicodePreview` converts math to the
  Unicode/HTML fallback (fast, synchronous, used by `QTextBrowser`); `KatexOutput`
  preserves raw LaTeX and its delimiters (`$…$`, `$$…$$`, `\(…\)`, `\[…\]`) so KaTeX
  auto-render can typeset it in a real browser.
- `buildFullHtml(body, dark, fontSize, isPrint, injectKatex)` — when `injectKatex`
  is true (only when math is detected) it references the **bundled local KaTeX**
  assets (`<exe-dir>/katex/`) and runs auto-render on load. The in-app preview never
  injects KaTeX.
- Browser Preview / HTML export / PDF export **regenerate from raw markdown** via
  `KatexOutput` — they never reuse the in-app preview's Unicode body.
- PDF export: documents with math are printed by launching installed Chrome/Edge in
  headless `--print-to-pdf` mode against a temp HTML file (so KaTeX executes);
  math-free documents keep the fast `QTextDocument`/`QPrinter` path. No Qt WebEngine.
- KaTeX assets live in `resources/katex/` and are copied next to the executable by
  CMake (build) and `scripts/package.ps1` (release).

### Per-tab architecture

Each tab owns a `TabPage` struct:

```
TabPage { DocumentModel*, MarkdownEditor*, PreviewWidget*, QSplitter* }
```

- `DocumentModel` — file path, dirty flag, untitled index. No Qt parent; manually deleted by `TabWidget::closeTabAt()`.
- `MarkdownEditor` (QPlainTextEdit subclass) — editor with line numbers, syntax highlighting, 200ms debounce timer, formatting slots.
- `PreviewWidget` (QTextBrowser subclass) — renders the HTML body; scroll position preserved across refreshes.
- `QSplitter` owns the editor and preview as children (65/35 default split).

### MainWindow responsibilities

- Owns `TabWidget`, `FindReplaceDialog`, `QFileSystemWatcher`, status bar labels.
- `connectEditor()` / `disconnectEditor()` re-wire signals when tabs switch.
- `applyTheme()` sets `qApp->setStyleSheet()` and calls `updateToolbarIcons(dark)` to regenerate icon colors.
- All QSS lives in `src/Stylesheet.h` — `AppStyle::light()` and `AppStyle::dark()` (each calls `AppStyle::common()` for shared scrollbar/splitter rules).

---

## Project Structure

```
E:\myApps\fastmd\
├── CMakeLists.txt
├── build.ps1                  # PowerShell build helper (initializes MSVC env)
├── resources/
│   ├── resources.qrc          # Qt resource file
│   └── fonts/
│       └── MaterialIcons-Regular.ttf   # Embedded via Qt resource system
└── src/
    ├── main.cpp
    ├── MainWindow.h/.cpp      # Top-level window, menus, toolbar, theme
    ├── TabWidget.h/.cpp       # QTabWidget + custom TabBar subclass
    ├── MarkdownEditor.h/.cpp  # QPlainTextEdit + line numbers + formatting slots
    ├── MarkdownHighlighter.h/.cpp  # QSyntaxHighlighter for Markdown
    ├── PreviewWidget.h/.cpp   # QTextBrowser-based live preview
    ├── DocumentModel.h/.cpp   # File path + dirty state per tab
    ├── ExportManager.h/.cpp   # MD4C conversion + HTML/PDF export
    ├── FindReplaceDialog.h/.cpp
    ├── IconHelper.h           # Toolbar icon renderer (Material Icons font)
    └── Stylesheet.h           # All QSS strings (AppStyle namespace)
```

---

## Dependencies & Toolchain

| Component      | Version / Path                                                                         |
| -------------- | -------------------------------------------------------------------------------------- |
| Qt             | 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`                                                   |
| MSVC           | 19.x (Build Tools at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`) |
| CMake          | 3.30.x at `C:\Qt\Tools\CMake_64\bin\cmake.exe`                                         |
| Ninja          | 1.12.x at `C:\Qt\Tools\Ninja\ninja.exe`                                                |
| MD4C           | 0.5.2 via `FetchContent` from `https://github.com/mity/md4c.git`                       |
| Material Icons | Font TTF embedded in `resources/fonts/`, loaded via `QFontDatabase`                    |

Build: run `build.ps1` from the project root in PowerShell (initializes MSVC env via `vcvarsall.bat amd64`).

Output: `build\FastMD.exe` (WIN32 subsystem, Release).

`windeployqt` runs as a POST_BUILD command and copies required Qt DLLs next to the executable.

---

## Technical Decisions

### Qt Widgets, not WebEngine or QML

Chosen for: native feel, fast startup, small deployment, low RAM. Chromium-based alternatives were explicitly rejected. QTextBrowser is sufficient for Markdown preview.

### MD4C for Markdown parsing

Chosen for: CommonMark compliance, C library (easy to integrate), very fast. Flags enabled: `MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS`.

### Single HTML pipeline

All three consumers (preview, HTML export, PDF export) use the same `ExportManager` functions. This guarantees consistency between what you see and what you export. For PDF export, `ExportManager` receives `PdfExportOptions` to allow configurable page sizes, margins, and fonts.

### QPlainTextEdit for the editor

Handles large documents efficiently. Rich-text alternatives (QTextEdit) were rejected due to interference with raw Markdown.

### QSettings for persistence

Stores: window geometry, recent files list, theme. No database.

### Material Icons font via Qt resource system

Font is embedded in the binary via `resources.qrc`. `IconHelper::materialFontFamily()` loads it once via `QFontDatabase::addApplicationFont`. Icons are rendered at 26×26 logical pixels to `QPixmap` and wrapped in `QIcon`.

---

## Conventions

- **C++20**, `#pragma once`, no exceptions in UI code.
- **No comments** unless the WHY is non-obvious. No docstrings.
- All QSS (stylesheets) lives exclusively in `src/Stylesheet.h`. Never hardcode style strings in widget code except for one-off overrides (e.g., the status-bar label on the toolbar).
- Toolbar icon definitions are stored in `MainWindow::m_tbIcons` (`QList<TbIcon>`) so `updateToolbarIcons(bool dark)` can regenerate all icons on theme change without recreating the toolbar.
- Formatting slots on `MarkdownEditor` follow the naming convention `fmt<Name>()` and are `public slots`. New formatting actions must be added there, not in MainWindow.
- `ExportManager` is stateless (all static methods). Keep it that way.
- Build uses `/W3 /utf-8` on MSVC. Do not suppress warnings with pragmas.
- All string literals that appear in the UI use `tr()` for potential future i18n.

---

## Known Constraints

- **Windows only** — build system and deployment are Windows/MSVC-specific. No cross-platform build path exists yet.
- **QTextBrowser preview limitation** — it uses Qt's HTML/CSS subset, not a full browser engine. Complex CSS in the preview will not render correctly.
- **PDF export via QPrinter** — page layout is approximate; the CSS used for screen preview does not transfer perfectly to print. (Note: Oversized images are scaled via CSS to fit page width).
- **Font rendering of Material Icons** — `QFont::setPixelSize` in a `QPainter` on `QPixmap` operates in physical pixels. Do not use `setDevicePixelRatio` on the pixmap (causes misaligned glyphs on 1× displays).
- **MSVC curly-quote issue** — the Edit/Write tools may silently insert Unicode curly quotes (`"` `"`) into `.cpp` files written as UTF-8. MSVC rejects them. If a build fails with `C3873`/`C3872` (character not allowed), use PowerShell `String.Replace([char]0x201C, '"')` to sanitize the file.

---

## Project History & Previous Decisions

- **Dark toolbar rejected** — an initial version used a permanently dark toolbar (`#2b2b2b`). This was replaced with a light Notepad++-style toolbar (`#f3f3f3`) based on user feedback.
- **Text-only toolbar labels rejected** — "B", "I", "H", "•≡" text buttons were replaced with Material Icons glyphs for clarity.
- **Single "H" heading cycle button replaced** — the original single button cycled H1→H2→H3→none. Replaced with separate H1/H2/H3 toggle buttons per user request.
- **DPI-scaled icon rendering rejected** — attempted rendering icons at 2× with `pm.setDevicePixelRatio(2.0)`. This caused blurry glyphs on 1× displays because the painter coordinates conflicted with the DPR setting. Reverted to direct logical-pixel rendering at 26px.
- **`QFontFamily` deprecated** — `setFontFamily(QString)` was deprecated in Qt 6. Use `setFontFamilies(QStringList)` in `QTextCharFormat`.
- **`QMenu::addAction(text, recv, slot, shortcut)` overload deprecated** — rewrote `createMenus()` using explicit `new QAction` + `connect()` + `setShortcut()`.

---

## Rules For Future Agents

1. Do not add Electron, WebEngine, QML, or any non-Qt-Widgets renderer to the preview.
2. Do not add a database, cloud sync, AI, plugin system, or sidebar — these are explicitly out of scope by design.
3. All new QSS must go into `src/Stylesheet.h` inside `AppStyle::light()` and/or `AppStyle::dark()`.
4. All new formatting operations must be `public slots` on `MarkdownEditor`, not anonymous lambdas in MainWindow.
5. New toolbar buttons must be registered in `m_tbIcons` so theme switching updates their icon color.
6. Never call `git commit` or `git push` unless the user explicitly requests it.
7. After any code change, build and verify — do not report success without confirming the executable was produced.
8. Check for curly-quote contamination after writing `.cpp` files (see Known Constraints above).
9. `ExportManager` must remain stateless. Do not add member variables to it.
