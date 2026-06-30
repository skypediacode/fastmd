# FastMD Changelog

All notable changes to this project will be documented in this file.

## [1.6.5] - 2026-07-01

### Added

- **Browser-Driven Printing:** Print documents via browser preview with `Ctrl+P`, enabling standard page layouts and offline KaTeX rendering.
- **Keep Open Preference:** Option under Preferences -> General to prevent the application from closing when the last tab is closed (automatically spawns a new document instead).
- **Post-Export PDF Dialog:** Non-intrusive dialog after exporting a PDF to immediately open the PDF or show it in its containing folder.
- **PDF Write Check:** Verifies destination file write permissions before PDF export to prevent silent failures if the file is locked or open in another application.

### Changed

- **Shortcut Adjustment:** Reassigned the Markdown Preview Toggle shortcut to `Ctrl+Shift+V` to avoid conflict with the print shortcut (`Ctrl+P`).
- **Improved Font Scale for Printing:** Print stylesheet scales body text to 87.5% (approx. 14px) and automatically migrates legacy 12px print/PDF settings to 14px.
- **Compact Editor Mode Button:** Reduced the width of the Switch Editor Mode button on the toolbar for a cleaner layout.
- **Enhanced PDF Styling:** Improved styling of links (GitHub-like blue, underlined) and inline code (black text on a light gray background with padding and rounded corners) in exported PDFs.

## [1.6.1] - 2026-06-29

### Added

- **Table Size Picker:** The toolbar table button now opens an 8x8 grid picker so you can insert custom table dimensions instead of a fixed 3x3 table
- **Separate Find Actions:** The toolbar now exposes distinct Find and Find and Replace buttons with matching shortcuts

### Changed

- Export dialogs now prefill a sensible filename based on the current document and remember the last open/export folders
- PDF export now prefers browser-backed rendering when Chrome or Edge is available, which improves fidelity and page-breaking behavior
- Markdown-only formatting actions are disabled in Plain Text mode so the toolbar and shortcuts stay consistent with the active file type

## [1.6.0] - 2026-06-29

### Added

- **LaTeX Math Rendering:** Render LaTeX math expressions with bundled KaTeX in browser preview and exports
- **Clear Recent Files Action:** New separator and clear action in the Recent Files toolbar menu
- **Save All Action:** Save all modified files at once using the Ctrl+Shift+S keyboard shortcut
- **Inline & Block Math Toolbar Buttons:** Two new toolbar buttons to insert inline math (`$...$`, Ctrl+Shift+M) and block math (`$$...$$`, Ctrl+Shift+B)
- **Default Save Folder:** New Preferences option to set a fixed default folder for the Save dialog; falls back to the last-used save location across sessions when not configured

### Changed

- Improved startup performance through lazy initialization of Find dialog and workspace model

## [1.5.0] - 2026-06-29

### Added

- **Workspace Tree:** Browse and open files from a folder tree panel with toggleable visibility
- **Plain Text Mode:** Dedicated mode for editing plain text files (.txt, .log, .csv, etc.) alongside Markdown
- **Session Restore:** Automatically save and restore open files, tab order, cursor position, and editor mode on startup
- **Preferences Dialog:** User-configurable settings for startup behavior and session restore options
- **File Type Association:** Associate common text and markdown file types with FastMD from Preferences
- **Check for Updates:** Built-in GitHub release checker via Help menu with update notifications
- **Workspace Memory:** Remember workspace tree width and visibility between sessions

### Changed

- Improved editor layout and tab bar positioning to eliminate overlaps
- Enhanced mode toggle button design and accessibility (16px text, better sizing)
- Updated SVG and ICO application graphics for better visual quality
- Improved markdown syntax highlighting accuracy
- Enhanced HTML and PDF export functionality

### Fixed

- Fixed tab bar overlap with workspace tree toggle button
- Fixed mode toggle button sizing inconsistencies
- Improved layout handling when toggling workspace tree visibility

### Chore

- Removed obsolete `package.ps1` build script
- Updated `.gitignore` to properly exclude build artifacts and agent-generated files
- Removed `AGENTS.md` (documentation moved inline)

## [1.0.0] - 2026-06-25

### Added

- Initial stable release
- Native C++20 and Qt 6 Widgets implementation
- Core Markdown editing with CommonMark support via MD4C
- Live Markdown preview with synchronized scrolling
- HTML export with clean, self-contained output
- PDF export with configurable pages, margins, and fonts
- Light and Dark themes
- Material Icons toolbar
- Tabbed interface for managing multiple files
- Quick Open dialog
- Find and Replace functionality
- Zoom controls for editor and preview
- Version checking infrastructure

### Features

- Ultra-fast startup (< 0.1s)
- Minimal memory footprint (~50 MB idle)
- Small executable size (~1 MB)
- Distraction-free writing environment
- Syntax highlighting for Markdown
- Auto-detection of file encodings
- Session state preservation (zoom levels, theme)

---

**FastMD:** Lightning-fast, native Markdown editing for Windows.
