# FastMD Changelog

All notable changes to this project will be documented in this file.

## [3.1.1] - 2026-08-21

### Added
- **Notepad Mode:** Added an editor-only layout with F12, Command Palette support, shortcut-dialog visibility, and persistence across launches.
- **Export Progress:** Added simulated percentage feedback in the status bar while exports run, reaching 100% only when the export completes.

### Changed
- **View Menu:** Reorganized related layout and view commands, and removed Mind Map and Reading Mode buttons from the main toolbar while retaining their menu, shortcut, and palette access.
- **Mode Behavior:** Reading Mode and Notepad Mode now coordinate their layout overrides and preserve the user's underlying Workspace, Preview, and splitter preferences.

## [3.1.0] - 2026-08-19

### Added
- **Markdown Alignment:** Added left, center, right, and justified alignment commands through the new Format menu and toolbar controls.
- **Store Rating Prompt:** Added an eligibility-gated Microsoft Store rating prompt for packaged installations.

### Changed
- **Preview Responsiveness:** Rendered changed preview blocks immediately before asynchronously loading and applying highlight.js, KaTeX, and Mermaid decorations.
- **Export Performance:** Made document exports asynchronous, isolated export work on dedicated thread pools, and added persistent warmed browser infrastructure.
- **PDF Export:** Routed PDF rendering through FastMD's native WebView2 preview pipeline for visual parity with live preview, with a browser fallback.
- **Image Resources:** Improved handling of absolute and relative local images in browser preview, printing, and exports.
- **Auto Save:** Expanded the available automatic-save delay options.

### Fixed
- Improved export shutdown and recovery behavior, including bounded waits and handling for crashed persistent browser profiles.
- Improved workspace search and update-flow reliability, and added compatibility and PDF metadata regression coverage.

## [3.0.6] - 2026-08-13

### Added
**Command Palette Conversions:** Added searchable commands for 
- Converting document line endings to Windows (CRLF), Unix (LF), or Mac (CR)
- converting text encoding to UTF-8, UTF-8 with BOM, UTF-16 LE, or UTF-16 BE, Clear Recent Files / Folders
- Insert Matrix
- Browse Image
- Indent / Unindent Selection
- Copy File Path

### Fixed
- **Editor Alignment:** Removed the extra code-fence inset and aligned the line-number gutter and fenced-code background with the editor text.

## [3.0.5] - 2026-08-13

### Added
- **Insert Reference Links:** Added numbered Markdown reference-link insertion with validation, source-safe rendering, and editor guidance.
- **Preview in Browser:** Added a File-menu and toolbar action for opening the current preview in the default browser.
- **Preview Link Destinations:** Added native status-bar feedback for hovered preview links.

### Changed
- **Insert Menu:** Promoted Insert to a top-level menu and reorganized code, links, footnotes, callouts, date/time, and table-of-contents commands into clearer groups.
- **Toolbar Overflow:** Preserved shortcut labels for toolbar actions in the overflow menu and refined code/link insertion actions.

## [3.0.4] - 2026-08-12

### Added
- **Reading Mode:** Added a distraction-free preview layout with F11, a toolbar action, Command Palette support, and in-preview find for rendered Markdown.
- **Bundled User Guide:** Added Help > User Guide, an offline read-only guide that opens directly in Reading Mode.
- **Mind Map Toolbar Action:** Added the Mind Map action back to the main toolbar.

### Changed
- **Reading Mode Activation:** New tabs opened while Reading Mode is active now start directly in the preview without a split-layout flash.
- **Command Palette:** Removed the redundant View-menu entry while retaining the F1 shortcut and palette functionality.
- **Toolbar Feedback:** Restored hover feedback for checked toolbar buttons.

## [3.0.3] - 2026-08-12

### Added
- **User Guide:** Added Help > User Guide, which opens a bundled, offline, read-only Markdown guide in a new tab, in Reading Mode, without changing Reading Mode or panel preferences for other tabs.
- **GitHub Callouts:** Added Note, Tip, Important, Warning, and Caution insertion commands under Insert, with selectable templates, selected-text conversion, toolbar/menu actions, and Command Palette entries.
- **Horizontal Rule Toolbar Command:** Added a toolbar action for inserting Markdown horizontal rules with blank-line separation that avoids Setext heading parsing.

### Fixed
- **Preview Source Mapping:** Preserved source-line provenance through table-of-contents expansion, math and footnote processing, definition lists, callouts, Mermaid, and fenced-code highlighting so Preview and Go to Source navigate to the correct Markdown lines.
- **Callout Rendering:** Restored live preview rendering for GitHub callouts when source mapping spans surround their markers.

## [3.0.2] - 2026-08-11

### Added
- **Document Format Indicators:** Added per-document line-ending and text-encoding indicators to the status bar, with menus for changing the format used on save.

### Changed
- **Page Break Shortcut:** Ctrl+Enter now works with both the main Enter key and the numeric keypad Enter key.
- **Update Safety:** Updates now detect unsaved documents, offer Save All or Cancel, use the normal Save As flow for untitled documents, and stop installation when saving is cancelled or fails.
- **About Dialog:** Adjusted spacing and margins for a more balanced layout.

## [3.0.1] - 2026-08-10

### Added
- **Go To Outline:** Added a hierarchical Outline tab for searching and navigating document headings.
- **Shared Heading Model:** Unified heading detection and filtering between Go To and the preview outline.

### Changed
- **Outline Navigation:** Added cursor-aware heading selection and keyboard navigation for the Outline tab.
- **Go To Line Dialog:** Improved input-field sizing and alignment across Windows display scales.

## [3.0.0] - 2026-08-10

### Added
- **Workspace Shortcuts:** Added F8 to toggle the Workspace panel and listed it in the Keyboard Shortcuts dialog.
- **Workspace Toolbar Actions:** Added toolbar buttons to reveal the current file in Explorer and open its folder in Terminal, using the Workspace root for unsaved documents.

### Fixed
- **Save and Undo Reliability:** Saving and Auto Save now preserve the editor's exact text and undo/redo history instead of normalizing and replacing the Markdown buffer.

## [2.9.9] - 2026-08-09

### Fixed
- **Preview Source Navigation:** Double-clicking text in a multi-line paragraph, list item, or heading now flashes the exact Markdown source line instead of the block's first line.

## [2.9.8] - 2026-08-06

### Added
- **Import as Markdown:** Import Office, OpenDocument, RTF, EPUB, CSV, text-based PDF, and other supported documents through the bundled native AnyDoc converter.
- **Import Image Handling:** Imported embedded images are preserved as alt text, with a status message when image assets are omitted.

### Changed
- **Import Workflow:** Imported Markdown is saved beside the source with collision-safe naming and opened as a normal document without a Save As step.
- **Bundled Tools:** Centralized Pandoc and AnyDoc version and integrity management for repeatable packaging and release builds.
- **Editor and Menus:** Improved folder-operation and editing shortcuts, added Ctrl+numpad-plus zoom support, removed unintended Markdown editor top margin, and reordered Import as Markdown above Export in the File menu.

## [2.9.7] - 2026-08-06

### Added
- **Tab Bar New Tab Control:** Added a theme-aware trailing “+” button and an empty-area context menu for creating or reopening tabs.
- **Full Closed-Tab Restoration:** Reopen Closed Tab now restores tab position, cursor and scroll state, language selection, preview state, and unsaved untitled documents, while avoiding duplicate open files.

### Changed
- **New Tab Shortcuts:** Added Ctrl+T as an alternate shortcut for New and included all supported New shortcuts in the Keyboard Shortcuts dialog.

## [2.9.6] - 2026-08-05

### Fixed
- **Command Palette Shortcut:** Restored the F1 shortcut in the main menu and preview so it consistently opens the command palette.
- **Markdown Preview:** Unlabelled fenced code blocks now remain plain code instead of being auto-detected and shown with a false language badge.
- **Store Update Dialog:** Fixed overlapping text by reserving the full wrapped height for installation guidance.

## [2.9.5] - 2026-08-05

### Added
- **Mind Maps:** Added an offline Markmap-powered mind-map presentation for Markdown, with source navigation, fit-to-window, cursor-centered wheel zoom, and interactive standalone HTML export.
- **Mind Map Exports:** Added source-based SVG, PDF, and DOCX export paths with deterministic headless rendering, KaTeX support, relative-image handling, and cropped DOCX raster output.
- **SVG Viewer:** Added a read-only WebView2 viewer for SVG files, including native zoom, printing, copying, and external-navigation handling.

### Changed
- **Preview and Export Reliability:** Preserved mind-map state across edits, fixed source mapping for headings, lists, tables, code, CRLF, and front matter, and made Pandoc report failed conversions and missing resources.
- **Interaction and UI:** Added a Fit Mind Map command, stabilized wheel zoom behavior, reorganized View menu actions, and fixed initial Workspace header icons.

## [2.9.0] - 2026-08-04

### Changed
- **Startup Performance:** Deferred Workspace construction and other optional startup work until after first paint, avoided unnecessary syntax setup for blank tabs, and coalesced settings maintenance.
- **Editor Performance:** Limited fenced-code painting to visible blocks and removed redundant whole-document scans and scroll-sync updates during editing.
- **Preview Performance:** Reused rendered preview state, reduced repeated feature and math scans, and safely discarded stale asynchronous conversions while keeping preview results current.
- **Workspace Performance:** Moved filesystem probing off the UI thread and cached extension/filter data used during tree filtering.
- **Packaging:** Reduced the deployed footprint by excluding unused Qt graphics and compiler runtime DLLs.

### Fixed
- Preserved preview correctness across tab switches and theme changes, prevented recursive highlighter construction, flushed pending splitter settings when closing, and removed transient math-decoration state.

## [2.8.6] - 2026-08-04

### Added
- **Release Notes:** Added a Help > Release Notes dialog backed by the project changelog, with the current release history embedded in the application.
- **Command Palette Shortcut:** Added the Command Palette shortcut to the Keyboard Shortcuts dialog.

### Changed
- **Markdown Formatting:** Auto Format Document now repairs numbering in ordered lists while preserving the list's starting number, indentation, delimiter style, and separate list runs.
- **Workspace Defaults:** New installations now show the Workspace panel by default and sort its entries by file type; existing saved preferences remain unchanged.
- **Update Experience:** Store updates now explain the possible application shutdown, verify the Store operation's completion state, and offer Restart Now/Later when FastMD remains open.
- **Terminology and Dialog Polish:** Renamed Document Summary to Document Statistics and fixed uneven Keyboard Shortcuts row spacing.

## [2.8.5] - 2026-08-03

### Changed
- **Auto Format Document — Table Prettifier:** Auto Format Document/Selection now aligns Markdown tables, padding every cell so column separators line up vertically. Alignment markers (`---`, `:---`, `---:`, `:---:`), tables with or without leading/trailing pipes, escaped pipes (`\|`), pipes inside inline code, and wide/combining Unicode are all handled; content is never changed beyond surrounding whitespace.
- **Large-document Preview Performance:** Large Markdown documents now refresh asynchronously, with incremental preview updates and targeted syntax/math/diagram decoration so typing remains responsive.
- **Command Palette:** Added a lazy VS Code-style command palette opened with F1 or the toolbar launcher, including fuzzy search, aliases, matched-text highlighting, and automatic menu/toolbar command discovery.
- **Workspace and Toolbar Polish:** Increased the Workspace panel width with a double-click reset, improved rename selection behavior, refined toolbar overflow and the Insert Image menu, and clarified the Show All filter tooltip.

### Fixed
- Fixed false external-change conflicts caused by asset drops.
- Fixed automatic restart after resetting application settings and restored print page-size/margin settings correctly.
- Open as Source is now offered only when a file would not already open as editable source, avoiding redundant actions for Markdown, text, and code files.

## [2.8.4] - 2026-08-02

### Added
- **YAML Front Matter & Export Metadata:** Exporting to PDF or HTML now parses leading YAML front matter (`title`, `author`, `description`, `keywords`), embedding metadata into HTML `<head>` meta tags and native PDF Document Properties while stripping the raw front-matter block from the document body in preview and exports.
- **Workspace Navigation:** Added Enter key support in the Workspace tree panel to expand/collapse folders or open files (matching double-click behavior).

### Changed
- **File Explorer Parity:** Enhanced "Reveal in File Explorer" to scroll target files into view in Windows File Explorer and removed the redundant "Open Containing Folder" context menu entry.
- **External Change Detection:** Improved file-change watching by monitoring parent directories so external file replacements (such as atomic temp-file rename operations from external tools and AI agents) cleanly trigger automatic tab reloads.
- **Updater & Installation UX:** Refined update installation progress bar visibility and suppressed unnecessary Microsoft Store popups during update installation.

## [2.8.3] - 2026-08-02

### Added
- **Context-Menu Command Bar:** Added a Notepad-style command strip to Editor and Workspace context menus with quick Cut, Copy, Paste, Select All, and Delete actions.

### Changed
- **Menu Interactions:** Added smooth fade-and-slide opening animations and eased hover transitions across context menus throughout the app.
- **Startup Experience:** Removed the post-update startup dialog; manual update checking remains available.

### Fixed
- Fixed the Auto Format Document keyboard shortcut

## [2.8.2] - 2026-08-01

### Added
- **Markdown Insert Commands:** Added an Insert menu for date/time, footnotes, horizontal rules, and page breaks, plus toolbar actions and shortcuts for subscript and superscript; Page Break now shares one action between the menu and toolbar. Insertion commands are disabled in code/plain-text documents.
- **Image Viewer Toolbar:** Added a floating, auto-hiding image-viewer toolbar with Zoom Out/In, zoom percentage, Fit to Window, Actual Size (1:1), Rotate 90° Right, and a leftmost Copy Image action. Rotation is view-only.
- **Safer File Renaming:** Renaming a Workspace item to a name that already exists now prompts for conflict resolution instead of failing silently or overwriting.

### Changed
- **Preview Panel Layout:** Moved the in-page Outline button from bottom-right to top-right of the preview, and anchored the native Find/Replace overlay to the editor pane so it no longer competes with the Outline button or floats over the preview.
- **Scrollbars:** Thickened the Editor and Preview scrollbars by 2px and rounded the Workspace panel's scrollbar thumbs to match.
- **File Change Detection:** Reduced the external file-change debounce from 400ms to 100ms for faster reload/conflict detection.
- **Preview Panel Context Menu:** Right-aligned the keyboard shortcut column in the Preview panel's right-click context menu.

### Fixed
- Fixed the Find/Replace overlay not repositioning when toggling the Preview panel's splitter.
- Fixed named footnote ID validation to avoid duplicate definitions, and prevented timestamp values from being misread as emoji shortcodes.

## [2.8.0] - 2026-07-31

### Added
- **Explorer Integration:** Added the packaged “Edit with FastMD” Windows context-menu command for files, folders, and unsupported file types, with raw-source opening for command-line and shell activations.
- **Editor Productivity:** Added document statistics, per-document language selection, broader source-file associations, Find/Replace result flashing, and session-only per-window Back/Forward navigation history.
- **Preview and Export:** Added consistent preview/export rendering, fenced-code language badges, and editor-to-preview navigation with source highlighting.
- **Workspace and Tabs:** Added safer cross-window tab dragging, duplicate-path handling, project-asset image pasting, and improved Workspace file operations and filtering.

### Changed
- **Startup and Runtime Performance:** Reduced startup settings work, narrowed the application event filter, reused settings snapshots, pre-warmed syntax highlighting and WebView2 at idle, rendered notation icons on demand, and removed stale deployed syntax definitions.
- **Editing and File Handling:** Added language-aware saving and source highlighting, preserved preview state across refreshes and image updates, and improved context menus and menu transitions.
- **Preview and Printing:** Unified the live preview, browser preview, HTML export, PDF export, and printing shell; corrected Page Setup margins for Mermaid documents and preserved preview scroll state.
- **Windows Packaging:** Expanded the MSIX shell command registration and kept the Qt-free shell extension isolated to the MSIX package.

### Fixed
- Fixed dragged-tab source-window closure crashes and duplicate-path transfer edge cases.
- Fixed Workspace Show All fallback persistence, workspace toggle hover sizing, notation icon aspect-ratio distortion, and toolbar/outline/menu flicker.
- Fixed print/PDF page size and margin handling for Mermaid documents.

## [2.7.0] - 2026-07-29

### Added
- **Math & Chemistry Toolbars:** Added dedicated symbol toolbars with searchable, categorized popups for formulas, matrices, calculus, functions, accents, chemistry notation, reaction arrows, and physical states.
- **Visible Soft Breaks:** Added a preference to render single-line Markdown breaks visibly and apply the setting consistently to preview, printing, and exports.
- **Toolbar Customization:** Added native context menus for toolbar visibility and overflow controls, with wrapping layouts for the Math and Chemistry toolbars.
- **Character Count:** Added total character count to the status bar.

### Changed
- **Math Rendering Performance:** Replaced repeated live icon rendering with baked PNG assets and improved popup sizing, hover transitions, and cross-toolbar navigation.
- **Startup Performance:** Deferred optional toolbar, preview, workspace, recovery, and session work; restored tabs incrementally and moved session file loading off the UI thread.
- **Math Insertion:** Improved selection wrapping, math-region detection, list Enter behavior, and notation insertion consistency.
- **Update Experience:** Refined the Microsoft Store update dialog and toolbar overflow spacing.

### Fixed
- Fixed duplicate windows during single-instance file forwarding.
- Fixed WebView2 initialization for read-only viewers and Matrix previews.
- Fixed stale or duplicated toolbar hover highlights and popup dismissal behavior.
- Fixed live theme refresh for Matrix dialog controls and the incorrect Enter behavior inside Markdown lists.
- Fixed math delimiter scanning for unclosed dollar delimiters and dollar signs inside backtick code spans.

## [2.6.3] - 2026-07-25

### Added
- **Go to Source:** Added a Preview context-menu action that navigates selected mapped content to its source line in the editor.
- **Line Movement:** Added VS Code-style Alt+Up/Alt+Down commands for moving current or selected lines while preserving selection and undo behavior.

### Changed
- **Edit Menu Organization:** Grouped line editing commands into a dedicated Line submenu and listed the new movement shortcuts in Keyboard Shortcuts.
- **Workspace Rename Editing:** Preserved standard copy, cut, paste, select-all, and undo shortcuts while renaming Workspace items.

### Fixed
- **Footnote Preview:** Preserved paragraph text surrounding inline footnote references.

## [2.6.2] - 2026-07-24

### Added
- **All-Languages Preview Highlighting:** Expanded the bundled highlight.js build to support 192 programming languages in the live WebView2 preview.
- **Chemical Formula Rendering:** Added KaTeX mhchem support for chemical formulas and equations in the live preview, portable HTML, and browser/PDF exports.

### Fixed
- **Window Activation State:** Fixed single-instance file activation so an already-running window is restored only when minimized, preserving its existing state otherwise.

## [2.6.1] - 2026-07-24

### Fixed
- **MSIX/Store Markdown Highlighting:** Bundled the KSyntaxHighlighting definitions and themes with portable and MSIX distributions, restoring Markdown and fenced-code syntax highlighting after installation.

## [2.6.0] - 2026-07-23

### Added
- **KSyntaxHighlighting Integration:** Migrated editor syntax highlighting to KDE Frameworks `KSyntaxHighlighting` library. Includes bundled language definitions and light/dark Markdown themes for real language-aware syntax highlighting inside fenced code blocks.
- **Generic Source File Editing:** Added language-aware highlighting and editable UTF-8 fallback handling for supported source/text files, with automatic source-highlighter switching in plain-text mode.
- **Home Folder Workspace Feature:** Introduced a dedicated Home Folder feature for the Workspace panel with a toolbar action button, configurable Home Folder path in Preferences, and smooth navigation.
- **Workspace Explorer Columns & Natural Sorting:** Added persistent Workspace metadata columns (File Type, Date Modified, Size) with localized human-readable formatting, Explorer-style natural sorting with folders kept first, and toolbar/header sort controls.
- **Per-Workspace Filtering:** Persisted the Workspace “Show All” setting independently for each normalized workspace root, while retaining the global fallback for new workspaces and drive-list mode.
- **Preview HTML Refresh Flag & Optimization:** Optimized WebView2 preview HTML update logic by introducing a refresh flag to prevent redundant full-body updates when switching tabs or re-rendering unmodified documents.
- **Apache 2.0 Material Icons Licensing:** Added official Apache 2.0 license declaration for Material Icons in the Open Source Licenses dialog.

### Changed
- **Workspace Navigation & Casing:** Preserved case-sensitivity in Workspace root folder display to match Windows Explorer naming while maintaining case-insensitive path lookup.
- **Toolbar & Action Layout:** Reorganized Open Folder actions, manage separator visibility dynamically, updated icon styles for inline workspace open actions, and updated UI terminology from "Text" to "Code/Text".
- **Editor Zoom Synchronization:** Synchronized editor zoom changes across open editable tabs and routed source/preview zoom commands through the active window.

### Fixed
- **Editor Dirty State Transition:** Fixed a false dirty-state transition bug in `src/MarkdownEditor.cpp` during syntax highlighting initialization.

### Refactored
- **Deferred KSyntaxHighlighting Initialization:** Deferred syntax highlighter setup for new empty Markdown editor instances to optimize tab creation performance.

## [2.5.2] - 2026-07-23

### Added
- **Native Read-Only PDF Viewer:** Introduced `EditorMode::Pdf` to open `.pdf` files directly in FastMD as read-only tabs using the embedded Edge WebView2 PDF engine. Includes session restoration, external file-change watching, native PDF printing via `ShowPrintUI`, and an `openPdfExternally` preference setting in Preferences and context menus.
- **Native Read-Only HTML Viewer:** Introduced `EditorMode::Html` to open `.html` and `.htm` files directly in FastMD rendered via Edge WebView2.
- **HTML Viewer Controls & Context Menu:** Added custom context menu for HTML viewer tabs supporting Zoom In (`Ctrl+=`), Zoom Out (`Ctrl+-`), Reset Zoom (`Ctrl+0`), Print (`Ctrl+P`), Copy (`Ctrl+C`), and Select All (`Ctrl+A`).
- **Workspace "Open as Text" Option:** Added context menu action in the Workspace panel to force-open HTML files in raw text editor mode when editing HTML source code.
- **External Viewer & Browser Preferences:** Added `openPdfExternally` and `openHtmlExternally` options in General Preferences to choose between opening PDF/HTML files in native FastMD tabs or sending them to default system apps/browsers.
- **WebView2 Dark Theme Synchronization:** Propagated application light/dark theme to the WebView2 profile's `PreferredColorScheme`, allowing Edge's native PDF viewer to follow app theme colors.

### Changed
- **Export Auto-Open in FastMD:** Connected PDF and HTML export completion actions to reopen exported PDF and HTML files directly in FastMD's native read-only viewer tabs.

### Refactored
- **Tab Context Menu Styling:** Streamlined tab context menu actions by removing `IconHelper` icon rendering for a cleaner, native UI appearance.

## [2.5.1] - 2026-07-22

### Added
- **Recycle Bin Deletion & Del Key Support:** Workspace panel file/folder deletions now move items to the OS Recycle Bin (`moveToTrash`) instead of permanently deleting them. Added support for pressing the `Delete` key on items in the Workspace tree.
- **New Plain Text File Action:** Added "New Plain Text File" creation option (`Untitled.txt`) to the Workspace toolbar dropdown menu and Workspace panel context menu.
- **Global Reveal Shortcut & Menu Actions:** Added global `Shift+Alt+R` shortcut ("Reveal in Explorer") across both the editor and Workspace tree selection. Added "Open Folder in Terminal" and "Copy Path" options to the Workspace context menu.

### Changed
- **Non-Intrusive External File Change Handling:** Implemented VS Code-style external file change handling:
  - Clean tabs reload silently on external changes while preserving cursor, selection, and scroll position.
  - Edits with identical disk content (metadata-only changes or identical tool rewrites) are ignored without prompts or refreshes.
  - Dirty tabs flag external conflict warnings, pause AutoSave for the tab, and prompt with a non-blocking dialog (Reload from Disk / Keep My Version / Decide Later).
- **Workspace Context Menu Restructure:** Moved Cut, Copy, Paste, Delete, and Rename above file creation, and collapsed standalone file/folder actions into a clean "New" submenu.
- **Uniform Table Styling Across Exports & Preview:**
  - Added uniform soft table cell borders across WebView2 live preview, HTML export, DOCX (`reference-fastmd.docx`), ODT (`patchOdtTableBorders`), and RTF (`patchRtfTableStyling`) exports.
  - Wrapped preview/HTML content tables in `<div class="table-wrap">` to fix `border-radius` clipping issues in Chromium/WebView2.
- **AutoSave Preferences Default:** Changed default AutoSave setting to disabled (`False`) for new installations.

### Fixed
- **Editor Focus Loss After Preview Selection:** Fixed issue where selecting text inside the WebView2 live preview caused the editor to lose keyboard focus. Introduced `MarkdownEditor::restoreKeyboardFocus()` to restore native window focus prior to Qt widget focus across clicks, tab switches, and drag paths.

## [2.5.0] - 2026-07-21

### Added
- **Explorer-Style Workspace Browser:** Redesigned the Workspace panel into a persistent file browser with an explicit root directory, lazy `QFileSystemModel` tree, virtual parent folder (`..`), recent folders integration, filter mode toggles, Show All option, custom chevrons, compact path bar action buttons, and inline file/folder renaming.
- **Workspace Operations & Search:** Added recursive debounced search view with native file icons and centered empty states. Implemented full file operations including create, rename, delete, copy, cut, paste, internal move, and external Explorer drag/drop import.
- **GitHub-Style Callouts:** Render `NOTE`, `TIP`, `WARNING`, `IMPORTANT`, and `CAUTION` callout blocks consistently across live preview, HTML export, PDF export, and Pandoc exports.
- **Interactive Preview Checkboxes:** Checkboxes in the live WebView2 preview can now be clicked directly to update mapped task list items in the underlying Markdown source document without displacing the editor cursor.
- **Extended Markdown Syntax:** Added support for Footnotes (parsing, numbering, popover previews, backlinks), Definition Lists (multiline and multi-definition support), Text Highlights (`==highlight==`), Subscripts (`~sub~`), Superscripts (`^super^`), and Emojis.
- **Dynamic & Static Table of Contents:** Added `Insert > Table of Content` to compile and insert a hierarchical Table of Contents at the cursor or replace `[TOC]` / `[[TOC]]` placeholders with live heading anchors.
- **Format Document & Selection:** Added conservative Markdown-aware document and selection formatting (`Shift+Alt+F` / Edit menu / Editor context menu).
- **Mermaid Preview Context Menu:** Added preview context menu for Mermaid diagrams with options to `Copy Image` (PNG), `Copy SVG`, `Save Image As...`, and `Save SVG As...`.
- **Editor Context Menu & Terminal Access:** Added `Copy File Path`, `Reveal in Explorer`, and `Open Containing Folder in Terminal` (launches Windows Terminal `wt.exe` or Command Prompt `cmd.exe`) to the editor context menu and Workspace tree.
- **Chrome-Like Tab Dragging:** Added cross-window tab dragging, tab detachment into new app windows, and `Close Tabs to the Left` in the tab context menu.
- **Plain Text & Association Modes:** Added support for Plain Text mode for non-Markdown files (`.txt`, `.text`, `.json`, `.toml`) which hides preview and disables formatting. Unsupported/binary files open in the OS default application.

### Changed
- **Portable HTML Exports:** Embedded KaTeX CSS, JS, and font resources directly into exported HTML documents for complete offline portability.
- **Preferences Dialog Reorganization:** Grouped preferences into clear General, Editor, and File Associations tabs, replacing the AutoSave delay numeric box with a styled combo box (8 presets, 2s default).
- **Session Startup Preference:** Changed the default for "Restore previous session on startup" to unchecked for new installations while preserving existing saved preferences.

### Fixed
- **Workspace Tree Selection & Auto-Close:** Retained file selection in the tree view after renaming files from the Workspace panel and automatically closed open tabs when underlying files were deleted.
- **Checklist Cursor & Task Spacing:** Kept cursor at the end of the line when inserting checklist items and normalized task list item spacing across light/dark themes, preview, HTML, PDF, and print.
- **Math & Extension Compatibility:** Protected inline and display math expressions inside blockquotes and headers from accidental Markdown syntax extension processing.

## [2.3.0] - 2026-07-19

### Added
- **Insert TOC Command:** Added `Insert > Table of Content` to dynamically compile and insert a hierarchical Table of Contents at the cursor, or automatically replace standalone `[TOC]` or `[[TOC]]` placeholders. Works for both Markdown editor insertion and HTML/PDF exports.
- **PDF Outlines & Bookmarks:** Integrated native PDF outline (bookmarks) generation into the browser-driven PDF export pipeline, mapping document headings (`<h1>`-`<h6>`) directly into standard PDF readers.

### Changed
- **Staged Startup & Performance:** Refactored recovery scanning and session restoration to run asynchronously after the first paint, eliminating synchronous I/O bottlenecks during app startup.
- **Settings Caching:** Restored tabs now reuse a single cached settings query, avoiding redundant registry reads on launch.
- **Outline Lazy Init:** Deferred Outline panel tree compilation until first requested to speed up main window construction.

### Fixed
- **Scroll Sync Alignment:** Prevented the preview from performing unnecessary line-centering steps when restored tabs are saved at the absolute top of the document.
- **Workspace Header:** Corrected bottom border matching and removed a 4px layout inset to align the tree model properly in the light theme.
- **Preview Dark Flash:** Resolved the white flash artifact when opening new panels or tabs in the dark theme.
- **Visuals & Layout:** Made the first text line flush with the top edge for a cleaner typing view, adjusted print margins/borders for headings, and resolved constraints for the Find/Replace dialog.

## [2.2.0] - 2026-07-18

### Added
- **Mode-Aware Printing:** Printing now respects the active editor mode. Markdown documents are rendered and printed via the parser, while Plain Text files are printed exactly as typed in a `<pre>` block using the editor's monospace font.
- **Unified Print Pipeline:** Integrated WebView2 print pipeline across Ctrl+P, File -> Print, toolbar print, and the preview context menu.
- **Page Setup Integration:** Renamed "PDF Page Setup" to "Page Setup" to govern page size, orientation, and margins for both PDF exports and live printing.
- **Editing Commands:** Added `Duplicate Line` and `Delete Line` commands to the editor.

### Changed
- **Architectural Cleanup:** Cleaned up project structure and split large source files:
  - Split `ExportManager` into separate translation units for Markdown, PDF, and Pandoc exports.
  - Split `MainWindow` implementation by responsibility (File, Export, Session, AutoSave, and Tabs).
  - Extracted workspace, recent files, and update checker modules into independent components.
  - Extracted editor formatting actions and pure text transforms into dedicated files.

### Fixed
- **Keyboard Shortcuts:** Resolved conflicts between keyboard shortcuts for `Save As`, `Save All`, and `Strikethrough`.
- **Print Headers:** Blanked the print HTML document title using a zero-width space to prevent browser-native print dialogs from showing placeholder headers.

## [2.1.5] - 2026-07-18

### Added
- **Editor Utilities:** Added UPPERCASE, lowercase, Proper Case, Sentence case, empty-line removal, whitespace trimming, and Markdown-aware Join Wrapped Lines commands.
- **Current Line Highlight:** Added an optional current-line highlight with a General Preferences setting.
- **Preview Source Navigation:** Double-click mapped rendered content to flash the corresponding source line and return focus to the editor. Restored sessions now synchronize the preview to the editor's position after rendering.

### Changed
- Reorganized Edit and File menus, including Convert Case and Whitespace submenus and the Open Recent menu placement.
- Added source-line metadata to the live-preview renderer while keeping exported HTML generation unchanged.

### Fixed
- Preserved source-line mapping through multi-line math preprocessing and corrected mapping for empty blocks.
- Ensured preview-to-editor highlighting works across tabs and restores native Windows keyboard focus for immediate typing.

## [2.1.0] - 2026-07-17

### Added
- **Tab Context Menu & Rename:** Added double-click tab renaming (untitled documents invoke Save As). Added a tab context menu with Rename, Close, Close Other Tabs, Close Tabs to the Right, Copy Path, and Reveal in Explorer.
- **Go To Line:** Added Go To Line (`Ctrl+G`) with input validation and a temporary line highlight animation to locate the destination.
- **Smart URL Paste:** Automatically converts a pasted URL into a Markdown link when text is selected, or inserts a Markdown link template when nothing is selected.
- **Default Editor Mode Preference:** Added a preference to choose the default editor mode (Markdown or Plain Text) for new documents.
- **Word Wrap Persistence:** Remembers Word Wrap settings per file individually and restores them when files are reopened.
- **Reset Application Settings:** Added an option to reset all application preferences (stored via QSettings) and restore a clean first-run state.
- **Keyboard Shortcuts Dialog:** Added a dedicated reference dialog for keyboard shortcuts.

### Changed
- **Dark Theme Improvements:** Polished native dialog chrome, splitter colors, scrollbar rendering, active tab styling, outline popup, and moving the outline button into the WebView2 page to eliminate resize/hover flicker.

### Fixed
- **Syntax Highlighting & Theme Switch:** Fixed an issue where changing the theme re-ran syntax highlighting and triggered false file-change notifications on closing tabs.

### Chore
- Bumped application, installer, MSIX, and release-publishing version to 2.1.0.

## [2.0.5] - 2026-07-16

### Added
- **Auto Save:** Named documents are saved asynchronously after a configurable debounce delay, enabled by default with a 2-second delay.
- **Crash Recovery:** Private recovery snapshots preserve untitled documents and unsaved changes after unexpected shutdowns, with an optional restore flow on next launch.
- **Save State Feedback:** Added subtle icon-only unsaved, saving, and saved indicators to the status bar.

### Changed
- **Preferences:** Added a dedicated Auto Save settings tab and refined the tab layout and order.
- **Document Indicators:** Dirty document titles now use a spaced `* ` marker consistently in tabs and the window title.
- **Workspace Layout:** Refined workspace columns, header alignment, corner toggle placement, and tab layout behavior.
- **Menu Transitions:** Adjusted menu animation timing for smoother native desktop interaction.

## [2.0.2] - 2026-07-15

### Added
- **Selection Statistics:** Added real-time selection statistics displaying character and word counts of the selected text in the status bar (e.g. `Selection: 95 chars, 16 words`), dynamically visible only when there is a selection.

### Changed
- **Menu Transitions:** Reduced menu fade-in/slide animation duration from 130ms to 100ms for snappier transitions.
- **Microsoft Store Review Protocol:** Updated the `Rate on Microsoft Store` action to open the `ms-windows-store://review/` protocol directly, launching the rating/review interface within the Microsoft Store app instead of a web browser.

### Fixed
- **Tab Bar Layout Flicker:** Removed asynchronous `QTimer` layout update queue in `TabWidget` and switched to synchronous `updateTabBarLayout()` calls during tab and resize lifecycle events, completely eliminating visual flicker on tab open/close.

### Chore
- Bumped application, installer, MSIX, and release-publishing version to 2.0.2.

## [2.0.1] - 2026-07-15

### Fixed
- **Splitter between the editor and preview:** fixed the splitter unable to be grasped


## [2.0.0] - 2026-07-14

### Added

- **Edge WebView2 Live Preview:** Upgraded the Markdown preview from Qt's `QTextBrowser` to Microsoft Edge WebView2 runtime. Features browser-grade rendering, including rich CSS tables, checkboxes, task lists, and syntax-highlighted code blocks. Previews load lazily and asynchronously.
- **File Export Formats:** Added File > Export As submenu supporting export to EPUB, Plain Text, RTF, ODT, and LaTeX via Pandoc.
- **Scroll Sync:** Fractional scroll synchronization replaces scrollbar-based scrolling to align preview and editor.
- **Offline Mermaid Rendering:** Render Mermaid fenced code blocks as diagrams fully offline in the preview. Includes a modal diagram viewer with panning and wheel zoom when a diagram is clicked.

### Changed

- **Unified Export Styling:** Refreshed preview typography and applied the FastMD visual identity to Pandoc exports with bundled EPUB CSS, LaTeX headers, and RTF styling.
- **Editor/Preview Splitter:** Refined the splitter between the editor and preview with a custom transparent handle and double-click-to-reset behavior.
- **Snappy UI Transitions:** Snappier dropdown menu and context-menu fade-in animation duration reduced to 130ms. Snappy tooltips with wake-up delay reduced to 300ms.
- **Math Renderer Layout:** Refactored math engine to a dedicated `MathRenderer` class and improved formulas/symbols rendering for the PDF printing fallback engine.

### Fixed

- **WebView2 Shortcuts and Focus:** Forwarded key and scroll wheel shortcuts (Ctrl+F, Ctrl+H, Ctrl+S, Ctrl+P, Ctrl+Alt+Tab, etc.) and zoom out of WebView2 preview back to Qt host. Fixed focus issues clicking back to the editor.
- **Centered ODT Math:** Handled Word ODT import display math alignment via style references.
- **Windows Jump List Recent Files:** Fixed opened documents not appearing under Recent Files in the taskbar Jump List for installed builds. Documents are now tagged with FastMD's AppUserModelID when registered with Windows, and FastMD registers itself as a Windows application capable of handling Markdown and text files (ProgID, file associations, capabilities, and `RegisteredApplications` entry), with shell associations refreshed automatically after install so no logoff/restart is needed.
  - This fully resolves Recent Files for the **Inno Setup installer** and the portable build. For the **MSIX package**, Windows requires `.md`/`.markdown`/`.txt` to be set as FastMD's default file type via *Settings → Apps → Default Apps* before Recent Files will populate — packaged apps aren't permitted to set their own file-type defaults silently, so this step can't be automated from within the app.

### Chore

- Bumped the application, installer, MSIX, and release-publishing version to 2.0.0.

## [1.8.1] - 2026-07-13

### Changed

- **Application Menus:** Aligned keyboard shortcuts in a consistent right-hand column across File, Edit, View, Theme, Help, Recent Files, and editor context menus.
- **Menu Icons:** Improved icon and check-indicator spacing in light and dark themes, including optical vertical correction for Help-menu icons.
- **Editor Context Menu:** Reused the shared menu alignment implementation so editor context-menu shortcuts match the rest of the application.
- **Windows Packaging:** Added and tracked the generated Windows Store package artwork used by the release pipeline.

### Chore

- Bumped the application, installer, MSIX, and release-publishing version to 1.8.1.

## [1.8.0] - 2026-07-12

### Added

- **Outline Improvements:** Added searchable, pinnable document outline behavior with improved selection tracking and inline controls.
- **Find/Replace Selection:** Opening Find or Replace now pre-fills the search field from the current editor selection.
- **DOCX Math Export:** DOCX export now passes original Markdown to Pandoc with TeX math extensions so equations become editable native Word OMML equations.
- **Windows Dark Title Bar:** The native Windows title bar now follows FastMD's dark/light theme, including explicit caption colors.
- **MSIX Packaging:** Added scaled splash-screen assets and package validation for Windows MSIX builds.
- **Single-Instance Opening:** File launches from Explorer, Open With, drag-and-drop, the Jump List, and the command line now forward paths to the existing FastMD window.
- **New Window:** Added an explicit File → New Window command with `Ctrl+Shift+N`.
- **Find and Replace Workflow:** Added compact Find and Replace tabs with selection prefill, case-sensitive and whole-word search, and conventional navigation shortcuts.
- **Editor Context Menu:** Added synchronized Word Wrap and Toggle Preview controls to the editor context menu.

### Changed

- **HTML/PDF Export:** Added document titles based on the source filename, improved heading sizing and spacing, and increased blockquote text contrast.
- **Toolbar Layout:** Extended toolbar borders across the full window and tightened the editor-mode button width.
- **Tab bar**: enhance TabBar styling with custom scroller and button hover effects
- **Find and Replace Layout:** Rebuilt the dialog around a shared grid with aligned fields, compact action buttons, responsive spacing, and improved status messaging.
- **Dialog Tab Styling:** Vertically centered Preferences and Find/Replace tab labels while preserving the themed active-tab indicator.
- **Version:** Bumped the application and packaging version to 1.8.0.

## [1.7.6] - 2026-07-06

### Added

- **File Drag-and-Drop:** Dropping local files onto the app window now opens them directly, while editor drops keep their existing markdown/image behavior.
- **Reopen Last Recent File:** Added `Ctrl+Shift+T` to reopen the most recently used file from the Recent Files list.

### Changed

- **Table Shortcut:** Moved table insertion to `Ctrl+Alt+T` so it no longer conflicts with reopening the last recent file.
- **Heading Highlighting:** Updated the editor syntax colors so heading levels 1-3 use the blue heading style.

### Fixed

- **Update Dialog Download:** Restored the update dialog's direct download selection so setup executables are preferred again when a newer version is available.

## [1.7.5] - 2026-07-04

### Added

- **Rectangular Selection:** Added editor support for column-style selection so multi-line blocks can be edited as a rectangle.

### Changed

- **Word Wrap Controls:** Added a Word Wrap toggle under the View menu with persisted state, synchronized tab settings, and Shift+mouse-wheel horizontal scrolling when wrapping is disabled.
- **Markdown Highlighting:** Refined the editor's syntax colors for headings, links, images, inline code, fenced code blocks, strikethrough, lists, blockquotes, and horizontal rules.
- **Preview Styling:** Tuned fenced code block spacing and code font handling in the preview/export HTML pipeline so wrapped and non-wrapped views render more consistently.

### Fixed

- **Preview Code Blocks:** Adjusted preview code block margins to avoid the extra blank-band effect while keeping fenced blocks visually compact.

## [1.7.3] - 2026-07-03

### Fixed

- **Preview Code Blocks:** Removed the extra blank band at the bottom of fenced code blocks in the in-app preview by trimming the preserved terminal newline inside the rendered code block.

## [1.7.2] - 2026-07-02

### Changed

- **LaTeX Math Rendering:** Expanded inline math rendering with support for spacing macros (`\ `, `\,`, `\;`, `\:`, `\!`) and comprehensive set of LaTeX commands including Greek letters, mathematical operators, and symbols. Improved command parsing to correctly handle macro arguments and ambiguous command boundaries.

### Fixed

- **Copy Behavior:** Fixed QPlainTextEdit's default copy to prevent placing HTML-formatted content on the clipboard, which caused extra paragraph margins to render as blank lines in applications that prefer the HTML flavor.

## [1.7.1] - 2026-07-01

### Changed

- **DOCX Task Lists:** Improved DOCX export so Markdown task lists render as readable checkbox markers in Word.
- **Find and Replace UI:** Refined the Find/Replace dialog into tabbed Find and Replace modes with clearer labels.
- **Toolbar Print Action:** Added a dedicated Print button to the toolbar alongside the existing print command.

## [1.7.0] - 2026-07-01

### Added

- **Outline Navigator:** Anchor a small desktop-style Outline edge button to the upper-right edge of the preview. Displays a tree representing the Markdown document's heading hierarchy (`#`–`######`) with filtering support, current editor cursor position highlighting, and synchronized scroll-to-heading.
- **Export DOCX via Pandoc:** Wire up Export DOCX in the File menu, toolbar, and `Ctrl+Shift+D`. Pandoc is executed as an external process, falling back to PATH if the bundled executable is not found.
- **Paste Image Support:** Allow pasting bitmap images from clipboard or file Explorer. Automatically save files to an `images/` directory next to the saved Markdown file, or a temporary folder for unsaved documents (which are migrated automatically upon save).
- **Open Exported HTML Dialog:** Added a non-intrusive confirmation dialog after exporting HTML to immediately open the HTML in the browser.
- **Open Source Licenses Dialog:** Replaced the Help menu's "About Qt" with a consolidated "Open Source Licenses..." dialog showing licensing details for FastMD, Qt, Pandoc, md4c, and KaTeX.

### Fixed

- **Toolbar Sizing:** Prevented toolbar from wrapping and fixed native overflow menu behavior.

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
