# FastMD Changelog

All notable changes to this project will be documented in this file.

## [1.6.0] - 2026-06-29

### Added

- **LaTeX Math Rendering:** Render LaTeX math expressions with bundled KaTeX in browser preview and exports
- **Clear Recent Files Action:** New separator and clear action in the Recent Files toolbar menu
- **Save All Action:** Save all modified files at once using the Ctrl+Shift+S keyboard shortcut
- **Math Inline & Block Math button**: add two toolbar buttons for inline math and block math

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
