# 1. Release Notes / What's New History

## v3.7.4
- Render tables directly after paragraph text, including tables inside list items.
- See refreshed screenshots and updated third-party notices.

## v3.7.3
- Adjust line, paragraph, and list spacing to suit your reading style.
- Enjoy clearer, more consistent Markdown headings in Preview, print, and PDF.
- Get to work sooner with a more responsive Workspace at startup.
- Keep currency amounts such as `$5.00` readable as text in Preview and exports.
- Render single-dollar math only when it follows Markdown delimiter rules and stays within its block.
- Updated the bundled Pandoc export and AnyDoc import tools.

## v3.7.2
- Fixed duplicate paste when using the integrated Terminal's right-click menu.
- Improved the detached Terminal toolbar button order.

## v3.7.1
- Search PDF documents with match counts and previous/next navigation.
- Use the familiar Find bar positioned inside the PDF viewer.


## v3.7.0

Fixed a crash that could occur when closing FastMD while the Source Control panel was active.

## v3.6.9
- Choose editor, preview, and print/PDF fonts with searchable font preferences.
- Read Vietnamese text more clearly with improved font rendering.
- Open the relevant Preferences setting directly from Settings commands.

## v3.6.8
- Open Terminal URLs, files, folders, and file locations with Ctrl+Click.
- Follow relative Terminal links using the shell's reported working directory.
- Keep link handling safe: unsupported local files are revealed in Explorer, never launched.

## v3.6.7
- Identify the active tab more easily with a softer, subtle shadow.
- Reorder tabs with clearer drag feedback and no blue marker across the dragged tab.

## v3.6.6
- Navigate PDF exports with bookmarks generated from Markdown headings.
- Enjoy the same document layout with or without PDF bookmarks.

## v3.6.5
- Apply Markdown Heading 4, 5, and 6 with `Ctrl+4`, `Ctrl+5`, and `Ctrl+6`.
- Find all six heading levels in the Command Palette and Shortcuts dialog.

## v3.6.4
- Find and run Terminal commands from the Command Palette, including shell profile, session, font, and theme controls.
- Start working sooner with a faster first-window launch.

## v3.6.3
- Restart to Update now reopens FastMD after a Microsoft Store update.
- A downloaded update you postpone now installs when you close FastMD.
- Syntax highlighting covers more code languages, including Ruby, Rust, Kotlin, TSX, JSX, Lua, and Perl.
- Pandoc updated to the latest version 3.12.1

## v3.6.2
- Switch the Workspace panel, Preview, and Notepad Mode from the title bar with new layout buttons.
- Enjoy a cleaner toolbar and document tabs with refined corner styling.

## v3.6.1
- Download Microsoft Store updates in the background, then restart when you're ready to install them.
- See update progress, cancel a download, and keep your work safe through the restart.
- Enjoy a refined tab bar, toolbar, and Workspace with more consistent spacing and styling.

## v3.6.0
- Add customizable headers and footers to printed documents and PDF exports, with useful presets and page number tokens.
- Keep Print and PDF output consistent, including file names, document titles, dates, and page counts.
- FastMD's Microsoft Store promotional trailer has been refreshed.

## v3.5.9
- Fixed toolbar and menu icons clipping at different Windows display scales.
- Improved reliability of Workspace panel persistence checks.

## v3.5.8
- Render ChatGPT-copied math subscripts correctly while preserving TeX examples and code.
- Insert Page Breaks at the caret and remove each marker cleanly with Backspace or Delete.
- Start Search in Files from selected text, and use updated Save As and Save All shortcuts.

## v3.5.7
- Fixed escaped LaTeX equations so math renders correctly in Markdown, Preview, and exports.
- Improved math handling across lists, quotes, tables, headings, and incremental Preview updates.

## v3.5.6
- Keep Alt+mouse column selections active while typing, pasting, or deleting across multiple lines.
- Preserve alignment on tabs and short lines, with each multi-line edit handled as one undo step.

## v3.5.5
- Page Breaks now work reliably beside other Markdown blocks, with accurate Preview source navigation.
- Fixed Preview checks for soft line breaks and expanded coverage for incremental rendering and source mapping.

## v3.5.4
- Open large Markdown documents more smoothly with responsive, time-sliced syntax highlighting. Large-file handling is now several times faster overall, with some critical operations improving by roughly 7–11×.
- See faster, more reliable live Preview updates in large documents, including when edits shift lines or arrive during rendering.

## v3.5.3
- Export Markdown to PDF from the command line with `fastmd input.md -o output.pdf`, including options to prevent accidental overwrites.
- Get more reliable PDF exports, including from read-only files and documents with relative resources.
- Use Terminal Escape reliably in console applications and keep Workspace search state when switching panels.

## v3.5.2
- Enjoy a refreshed Light and Dark theme across the editor, Preview, tabs, Workspace, and Terminal.
- Select text by clicking, double-clicking, or dragging in the line-number gutter.
- Benefit from clearer panel spacing, tab styling, and splitter controls.

## v3.5.1
- Find text across your workspace, including unsaved documents, with flexible search options and clear, grouped results.
- Replace matches in one file or across your workspace, preview changes, and preserve the original text's casing.
- Enjoy smoother Workspace search controls and more reliable keyboard shortcuts.

## v3.5.0
- Fixed a cursor issue that could leave the pointing-hand cursor active across the app after opening Preview or Terminal.

## v3.4.9
- Use quick, clearly underlined keyboard shortcuts in action dialogs.
- Get a clear warning if FastMD cannot open an external terminal in the current folder.
- Enjoy more consistent sidebar actions and Terminal themes in docked and detached windows.

## v3.4.8
- Close the window reliably from the custom title bar, including at the right screen edge.
- Resize restored windows from the top-right corner as expected.

## v3.4.7
- Insert bullet and numbered lists with the updated `Ctrl+Shift+8` and `Ctrl+Shift+7` shortcuts.
- Page breaks now display correctly in the in-app Preview.

## v3.4.6
- Enjoy a cleaner custom title bar with native Windows snapping, resizing, system menus, and polished caption controls.
- Browse Source Control history with improved multi-file actions and natural file-type sorting.
- Benefit from more reliable keyboard shortcuts, editor focus handling, and KaTeX preview parsing.

## v3.4.5
- Duplicate Workspace files and folders, with more predictable paste behavior.
- Start directly in Notepad Mode with the new `--notepad` launch option.
- Enjoy refined Terminal layout and clearer command shortcut consistency.

## v3.4.4
- Browse the active document's headings in the collapsible Workspace Outline and jump to any section.
- Rename Terminal sessions with F2 or double-click, and use Shift+F1 to open Workspace file search.
- Enjoy more consistent Workspace, Terminal, and Source Control panel sizing, headers, and resizing behavior.

## v3.4.3
- Rename Terminal sessions with F2 or double-click, and use Shift+F1 to jump straight to Workspace file search.
- Open recent folders directly from the Workspace folder arrow, with cleaner search-result sizing and narrow-panel behavior.
- Closing a single Terminal session is now immediate, while multi-session closes keep a clear confirmation option.

## v3.4.2
- Fixed the Terminal's default cursor so it displays correctly outside text-selection interactions.
- Preserved the docked editor/Terminal split ratio while resizing or reattaching the Terminal.

## v3.4.1
- Hide the Terminal panel without closing active sessions, or close all sessions with a clear confirmation option.
- Improved Store rating prompts so they appear reliably without duplicate or overly frequent requests.
- Better support for narrow windows, plus refined editor active-line and tab-bar visuals.

## v3.4.0
- Added a native multi-session Terminal with profiles, themes, font controls, and folder-aware launch actions.
- Detach, minimize, maximize, resize, and reattach the Terminal while keeping active sessions running.
- Added Markdown text highlighting and an optional export-completion dialog.
- Improved startup responsiveness and Terminal reliability across sessions, themes, and shell exits.

## v3.3.3
- Refresh Preview and Source Control with scoped F5 commands and discoverable context-menu actions.
- Sort Source Control changes by name, path, or status, with your choice remembered.
- Use Word Wrap in read-only Source Control diff tabs.
- Fixed stale Preview content after external changes reload in background tabs.

## v3.3.2
- Added an **All Supported Files** default filter to the Open dialog for Markdown, text, PDF, HTML, and supported image files.
- Opening a file no longer changes the active Workspace root unexpectedly.
- Fixed long Markdown tables splitting incorrectly in PDF export and printing; table headers now repeat across pages.
- Improved Source Control status colors and compacted file-action icons for clearer selected-row feedback.

## v3.3.1
- Fixed **Edit with FastMD** not appearing when right-clicking empty space in a folder in Windows File Explorer.
- Folder-background actions now correctly resolve the folder currently being browsed.

## v3.3.0
- Added Windows on Arm (Arm64) support with a native Microsoft Store package.
- Added an editable Welcome document for first launch and an automatic What's New tab after updates.
- Improved Source Control refresh behavior and dark-theme visual consistency.
- Improved dark-theme details across dialogs, status-bar separators, and Source Control selections.
- Added architecture-specific AnyDoc packaging and native dependency validation.

## v3.2.0
- Added Workspace Source Control (F7 key) with Git staged changes, read-only diff tabs, history graph, branch switching, and sync actions.
- Added native Windows spell checking with suggestions, personal dictionary support, and Fix All Misspellings.
- Added powerful line-editing commands, custom Split Lines delimiters, and Insert Content from File (support multiple filetypes).
- Improved reliability of asynchronous export, save, Git, and workspace operations.

## v3.1.2
- Added direct `S`, `D`, and `C` shortcuts to the Unsaved Changes dialog.
- Fixed status-bar language, line-ending, and encoding menus reopening after dismissal.
- Moved Auto Format Document into the dedicated Format menu.

## v3.1.1
- Added **Notepad Mode** (F12) for a focused editor-only view, with Command Palette and shortcut-dialog support and state restored across launches.
- Reorganized View commands and streamlined the main toolbar while keeping Reading Mode and Mind Map available from menus, shortcuts, and the Command Palette.

## v3.1.0
- Added Markdown alignment tools and a Format menu for left, center, right, and justified text.
- Improved preview and PDF export responsiveness with incremental rendering and asynchronous WebView2-based export.
- Added Store rating prompts and expanded auto-save delay options.

## v3.0.6
- Added searchable Command Palette commands for multiple commands.
- Improved editor alignment of the code block.

## v3.0.5
- Added a dedicated Insert menu with clearer groups for code, links, footnotes, callouts, date/time, and table-of-contents commands.
- Added numbered Markdown reference-link insertion with source-safe rendering and improved editor guidance.
- Added Preview in Browser to the File menu.
- Added preview link destinations to the native status bar and improved toolbar overflow shortcut labels.

## v3.0.4
- Added Reading Mode with rendered-preview find, direct activation without a split-layout flash, and toolbar/Command Palette access.
- Added a bundled offline User Guide that opens read-only in Reading Mode.
- Restored the Mind Map toolbar action and improved checked-toolbar hover feedback.
- Kept the F1 Command Palette shortcut while removing its redundant View-menu entry.

## v3.0.3
- Added Insert Callout commands for GitHub-style Note, Tip, Important, Warning, and Caution blocks, with Command Palette support.
- Added a toolbar button for inserting horizontal rules with Markdown-safe spacing.
- Improved preview source navigation so transformed content, callouts, Mermaid, and highlighted code retain accurate source-line mapping.

## v3.0.2
- Added line-ending and text-encoding indicators with per-document format controls.
- Improved update safety by protecting unsaved work before installation.
- Added numeric-keypad Ctrl+Enter support for page breaks and polished the About dialog layout.

## v3.0.1
- Added an Outline tab to Go To for searching and jumping through document headings.
- Improved outline navigation with hierarchical headings, cursor-aware selection, and keyboard navigation.
- Polished Go To line-field sizing and alignment across Windows display scales.

## v3.0.0
- Added F8 to toggle the Workspace panel and included it in the Keyboard Shortcuts dialog.
- Added toolbar buttons to reveal the current file in Explorer and open its folder in Terminal.
- Fixed a bug in the undo/redo history.

## v2.9.9
- Preview text navigation now highlights the exact Markdown source line for multi-line paragraphs, lists, and headings.

## v2.9.8
- Import supported Office, OpenDocument, RTF, EPUB, CSV, and text-based PDF files as Markdown with the new native converter.
- Imported documents are saved beside the source with collision-safe naming, while embedded images become accessible alt text.
- Improved editing and folder-operation shortcuts, zoom controls, Markdown spacing, and File menu organization.

## v2.9.7
- Added a theme-aware “+” button and empty-area menu for creating or reopening tabs.
- Reopen Closed Tab now restores tab order, cursor and scroll position, language, preview state, and unsaved untitled documents.
- Added Ctrl+T as an additional New Tab shortcut and improved shortcut listing.

## v2.9.6
- Restored the F1 shortcut for opening the Command Palette.
- Fixed false language detection and badges on unlabeled fenced code blocks.
- Fixed overlapping text in the Microsoft Store update dialog.

## v2.9.5
- Added offline mind maps with source navigation, fit-to-window, smooth wheel zoom, and interactive HTML export.
- Added source-based SVG, PDF, and DOCX mind-map exports with KaTeX and relative-image support.
- Added a read-only SVG viewer with zoom and printing, plus fixes for export failures, source mapping, and Workspace icons.

## v2.9.0
- Faster startup through deferred Workspace and optional initialization work.
- More responsive editing and preview updates, especially for large documents and fenced code.
- Faster Workspace filtering and filesystem operations with improved settings reliability.
- Smaller packaged installation by removing unused runtime components.

## v2.8.6
- Added an in-app Release Notes dialog and a Keyboard Shortcuts entry for the Command Palette.
- Improved Markdown ordered-list formatting and made the Workspace open by default for new installations, sorted by file type.
- Improved Microsoft Store update handling with clearer progress guidance, completion verification, and Restart Now/Later controls.
- Renamed Document Summary to Document Statistics and polished shortcut-dialog row spacing.

## v2.8.5
- Added a fast F1 command palette with fuzzy search and a toolbar launcher.
- Made large-document preview refreshes asynchronous and incremental for a more responsive editor.
- Added Markdown table prettifying to Auto Format Document and improved Workspace and toolbar interactions.
- Fixed asset-drop file-change conflicts, reset-settings restart behavior, print settings restoration, and redundant Open as Source actions.

## v2.8.4
- Added YAML front matter parsing for PDF and HTML exports to automatically populate document title, author, description, and keyword metadata while hiding raw header blocks from preview and exports.
- Added Enter key support in the Workspace tree panel to expand/collapse folders or open files.
- Improved external file-change detection when external tools and AI agents save files using temporary file replacements.
- Enhanced "Reveal in File Explorer" to scroll target files into view and polished update installation progress bars.

## v2.8.3
- Added a Notepad-style command bar to Editor and Workspace context menus for quick Cut, Copy, Paste, Select All, and Delete actions.
- Added smooth fade-and-slide opening animations and eased hover transitions across context menus throughout the app.
- Removed the post-update startup dialog for a quieter launch experience; manual update checking remains available.
- Fixed the Auto Format Document keyboard shortcut when text is selected.

## v2.8.2
- Added Insert commands for date/time, footnotes, horizontal rules, and page breaks, plus subscript/superscript toolbar actions and shortcuts.
- Added an internal image-viewer with floating toolbar with zoom controls, Fit to Window, Actual Size, rotate, and Copy Image.
- Moved the Outline button to the preview's top-right corner and fixed the Find/Replace overlay to stay correctly positioned when toggling the preview.
- Improved Workspace renaming with conflict resolution, faster external file-change detection, and thicker/rounder scrollbars across the app.

## v2.8.0
- Added the “Edit with FastMD” Windows File Explorer command for files and folders, including unsupported files opened as source text.
- Added document statistics, Find/Replace result flashing, editor navigation history, per-file language selection, and improved tab dragging and Workspace file handling.
- Unified live preview and exports with fenced-code language badges, improved preview refresh/scroll behavior, and corrected Mermaid print/PDF margins.
- Improved startup performance, notation icon rendering, menu transitions, and single-instance/tab/workspace reliability.

## v2.7.0
- Added Math and Chemistry toolbars with categorized notation pickers, matrices, calculus, accents, and chemistry symbols.
- Added configurable visible Markdown soft breaks across preview, printing, and exports.
- Improved toolbar overflow, hover interactions, math insertion, startup performance, and the update experience.
- Fixed single-instance forwarding, WebView2 viewer initialization, toolbar popup issues, and dollar-delimited math parsing.

## v2.6.3
- Added Preview “Go to Source” navigation and VS Code-style line movement shortcuts.
- Improved Workspace rename editing and fixed inline footnote preview text loss.

## v2.6.2
- Added preview syntax highlighting for 192 programming languages.
- Added offline KaTeX support for chemical formulas and equations in preview and exports.
- Fixed existing-window activation so its current minimized or restored state is preserved correctly.

## v2.6.1
Fixed an issue where Markdown and fenced-code syntax highlighting was missing in Microsoft Store/MSIX installations.

## v2.6.0
- Added KSyntaxHighlighting-powered syntax highlighting for Markdown and fenced code blocks.
- Added a dedicated Workspace Home Folder with configurable navigation.
- Added persistent Workspace metadata columns and Explorer-style natural sorting.
- Improved WebView2 preview performance by reducing unnecessary refreshes.
- Added per-workspace “Show All” filter settings.

## v2.5.2
FastMD 2.5.2 adds native PDF & HTML viewing, interactive viewer controls, system viewer preferences, and streamlined context menus.
- View `.pdf`, `.html`, and `.htm` files directly in FastMD
- PDF viewer follows FastMD's light/dark theme.
- HTML viewer supports Zoom, Print, Copy, and Select All.
- Exported PDF and HTML files can open directly in FastMD.
- Refined tab context menu styling.

## v2.5.1
FastMD 2.5.1 improves file handling, Workspace management, and productivity.
- Smarter external file reloads with conflict detection.
- Recycle Bin deletion for Workspace files/folders (**Delete** key supported).
- Added **New Plain Text File** and reorganized the Workspace **New** menu.
- Improved table rendering across preview and exports.
- Added **Reveal in Explorer**, **Open Folder in Terminal**, and **Copy Path**.
- Fixed editor focus after using the live preview.

## v2.5.0
FastMD 2.5.0 introduces an Explorer-style persistent Workspace browser, GitHub-style callouts, interactive preview task checkboxes, footnote & definition list support, dynamic Table of Contents, document formatting, and cross-window tab dragging.
- Redesigned Workspace panel as a persistent file browser with root directory selection, lazy filesystem tree, debounced recursive search, path bar action buttons, and full file operations (create, rename, delete, copy/cut, paste, drag-and-drop move, external import).
- Native rendering for GitHub-style Markdown callouts (NOTE, TIP, WARNING, IMPORTANT, CAUTION) across live preview, HTML export, and PDF export.
- Interactive task list checkboxes in the live WebView2 preview that update the underlying Markdown source document in real-time.
- Extended Markdown syntax support including Footnotes, Definition Lists, Table of Contents (`Insert > Table of Content`), Text Highlights (`==highlight==`), Sub/Superscripts (`~sub~`, `^super^`), and Emojis.
- Editor formatting tools ("Format Document" / "Format Selection"), file context menu actions (Copy Path, Reveal in Explorer, Open in Terminal), and preview context menu for Mermaid diagrams (Copy & Save PNG/SVG).
- Cross-window tab dragging and tab detachment into independent windows with single open tab enforcement per file path.
- Plain Text file support for `.txt`, `.json`, `.toml`, etc., with automatic preview suppression and formatting controls.

## v2.3.0
FastMD 2.3.0 adds a dynamic Table of Contents (TOC) generator, native PDF outline bookmarks, and staged startup performance optimizations.
- Generate and insert a dynamic Table of Contents at the cursor, or automatically replace standalone [TOC] placeholders.
- Preserve document heading structures as clickable outline bookmarks in exported PDF files.
- Staged recovery scanning and tab session restoration to speed up application startup.
- Cache settings queries to optimize tab loading performance.
- Deferred Outline panel initialization until first use.

## v2.2.0
FastMD 2.2.0 adds mode-aware printing, Page Setup integration, new Duplicate and Delete Line commands, and an internal architectural cleanup.
- Printing now honors the active mode: Markdown renders normally, while Plain Text files print exactly as typed in a monospace font.
- Unified print preview workflow across shortcut (Ctrl+P), menus, toolbar, and the preview context menu.
- Page Setup (formerly PDF Page Setup) configuration now controls both PDF export and local print margins and orientation.
- Added Duplicate Line and Delete Line editor commands.
- Resolved keyboard shortcut conflicts for Save As, Save All, and Strikethrough.

## v2.1.5
FastMD 2.1.5 adds Markdown-aware editing tools, current-line highlighting, and faster navigation between the live preview and source editor.
- Convert selected text to UPPERCASE, lowercase, Proper Case, or Sentence case.
- Clean up whitespace, remove empty lines, and join wrapped Markdown paragraphs.
- Optionally highlight the current editor line from General Preferences.
- Double-click preview content to highlight its source line in the editor.
- Restore the preview near the editor's position when reopening a session.

## v2.1.0
FastMD 2.1.0 adds Go To Line dialog with animation, tab rename and context menu, smart URL pasting, per-file word wrap, and keyboard shortcuts reference.
- Go To Line dialog with temporary highlight.
- Tab rename and tab context menu (Rename, Close Others, Copy Path, Reveal in Explorer).
- Smart URL paste auto-creates Markdown links.
- Default editor mode preference for new files.
- Remember Word Wrap setting per file.
- Reset application settings action in Preferences.
- Keyboard shortcuts reference dialog.
- Theme polishing and dark mode UI fixes.

## v2.0.5
FastMD 2.0.5 adds Auto Save, crash recovery, and several usability improvements.
- Auto Save after a short idle delay (enabled by default).
- Crash Recovery restores unsaved changes after unexpected shutdowns.
- Icon-only save status (unsaved, saving, saved).
- Improved workspace layout, dirty markers, and Preferences.

## v2.0.2 & 2.0.1
FastMD 2.0.2 and 2.0.1 improves editing responsiveness and usability.
- Selection character and word counts in the status bar.
- Fixed tab bar flickering.
- Faster menu animations (130 ms → 100 ms).
- "Rate on Microsoft Store" now opens directly in the Store app.
- Fixed the editor/preview splitter being difficult to grab.

## v2.0.0
FastMD 2.0.0 introduces the new Edge WebView2 preview, expanded export support, and major UI improvements.
- Edge WebView2 Markdown preview with rich Markdown rendering.
- Offline Mermaid diagrams with zoom and pan.
- Lazy preview initialization for fast startup.
- Seamless preview keyboard shortcuts and Ctrl+wheel zoom.
- New Export As formats: EPUB, TXT, RTF, ODT, and LaTeX.
- Improved math rendering with KaTeX and better export support.
- Faster tooltips and menu animations.
- Refreshed preview typography and export styling.
- Improved editor/preview splitter.
- Fixed Windows Jump List Recent Files.

## v1.8.1
FastMD 1.8.1 makes application menus clearer and more consistent:
- Keyboard shortcuts now align in a consistent right-hand column across the File, Edit, View, Theme, Help, Recent Files, and editor context menus.
- Menu icons and check indicators are better aligned in both light and dark themes.
- Help-menu icons include optical vertical positioning for easier scanning.
- Editor context-menu shortcuts now use the same alignment behavior as the rest of the application.

## v1.8.0
FastMD 1.8.0 adds numerous improvements to the app to provide a smoother, more productive editing experience:
- Single-instance file opening: files opened from Explorer, Open With, drag-and-drop, Recent Files, or the command line now open in the existing FastMD window.
- New Window command for opening an independent FastMD window when needed.
- Redesigned Find and Replace dialog with compact Find and Replace tabs, selection-based search field filling, Case sensitive and Whole word options, and improved keyboard shortcuts.
- Find and Replace shortcuts: Ctrl+F, Ctrl+H, F3, Shift+F3, Enter, and Shift+Enter.
- New editor context-menu controls for Word Wrap and Toggle Preview, synchronized with the View menu and toolbar.
- Improved Markdown outline navigation, preview styling, export layout, and Windows packaging.
- Refined Preferences and Find/Replace tab styling for cleaner, vertically centered labels in light and dark themes.
- Continued support for Markdown and plain text editing, live preview, PDF/DOCX/HTML export, printing, workspaces, tabs, and light/dark themes.

---

## 2. Package & Identity Details

| Field | Value |
| :--- | :--- |
| **Package Name** | `skypediacode.FastMDMarkdownandTextEditor` |
| **Package Display Name** | `FastMD: Markdown and Text Editor` |
| **Publisher Name** | `CN=6B167262-0869-40FF-A221-EE00F958F1FB` |
| **Publisher Display Name** | `skypediacode` |
| **Package Description** | `FastMD is a fast, lightweight Markdown and Text Editor.` |
| **Silent Install Arguments** | `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-` |
| **Installation Location** | `C:\FastMDMSIX` |
| **Save Location** | `E:\myApps\fastmd\dist\FastMD-3.3.2.0.msix` |
| **Website URL** | https://skypediacode.github.io/fastmd/ |
| **Privacy Policy URL** | https://skypediacode.github.io/fastmd/privacy-policy.html |
| **Support Email** | skypediacode@gmail.com |
| **Contact Email** | skypediacode@gmail.com |

---

## 3. Notes for Certification

FastMD is a lightweight offline Markdown and plain text editor for Windows.
- No account or sign-in is required.
- No internet connection is required.
- No activation, license key, or test account is needed.

The application stores documents locally on the user's computer and does not require any external services.
Note: the application bundles Pandoc to support document export (PDF/DOCX/HTML). No additional software installation is required.

---

## 4. Store Listing

### Short Description
FastMD combines Notepad speed with professional Markdown editing. Enjoy interactive live preview, Explorer workspace, built-in Git source control & Terminal, PDF/HTML viewing, and 100% native x64 & ARM64 performance on Windows.

### Full Description
FastMD brings Notepad speed to professional Markdown editing.

📝 Markdown, Code & Plain Text: Syntax highlighting, math, chemistry, Mermaid diagrams, tables of contents, and more.
🖨️ Professional Export & Print: High-quality printing with page setup. Export to PDF, DOCX, HTML, EPUB, and more.
📂 Explorer-Style Workspace: Tabs, drag and drop, file management, and workspace-wide search.
🔎 Built-in File Viewers: View PDFs, HTML, images, and SVG without leaving FastMD.
🖥️ Git & Integrated Terminal: Manage source control and run commands without switching apps.
⌨️ Keyboard Productivity: Shortcuts, multi-cursor editing, Command Palette, and quick navigation.
💚 Completely Free: No ads. No subscriptions. No distractions.
⚡ Fast. Lightweight. Native ARM64 & x64: Quick startup, responsive typing, and native x64 & ARM64 support.

Write faster. Preview instantly. Export beautifully.

### Product Features
- Fast, lightweight native Windows Markdown and plain text editor
- Native support for both Windows on Arm (ARM64) and x64 architectures
- Built-in Workspace Source Control (Git) with staged changes, diff viewer, branch management, and commit history
- Open, edit, and save Markdown, plain text, and code files
- Instant side-by-side live preview with synchronized scrolling
- Click preview content to navigate to its exact Markdown source line
- GitHub-flavored Markdown with tables, callouts, task lists, footnotes, and more
- Offline KaTeX math and chemical formula rendering
- Offline Mermaid diagrams with zoom, pan, and image export
- Offline interactive mind maps with source navigation and HTML export
- Syntax highlighting for Markdown and 190+ programming languages
- Export to PDF, DOCX, HTML, EPUB, ODT, RTF, LaTeX, and plain text
- PDF export with configurable page setup and heading bookmarks
- High-fidelity printing for Markdown and plain text documents
- Workspace Explorer with search and full file operations
- Multiple tabs with safe cross-window tab dragging
- Find and Replace, document statistics, and Outline navigation
- Markdown formatting, table, math, and chemistry insertion toolbars
- Auto Save, crash recovery, and session restoration
- Built-in read-only PDF, HTML, SVG, and image viewers
- Per-document language, line-ending, encoding, and word-wrap controls
- Dark and light themes; fully offline with no account or subscription

### Additional Information / Keywords
`text editor`, `native markdown editor`, `windows on arm`, `markdown pdf`, `markdown print`, `markdown editor`, `fast markdown editor`

### Screenshot Captions
- **Caption 1 (`DesktopScreenshotCaption1`):** Edit Markdown, plain text, and source code with a clean, responsive editor and real-time formatting tools.
- **Caption 2 (`DesktopScreenshotCaption2`):** See your document update instantly with synchronized scrolling, click-to-source navigation, math, Mermaid, and more.
- **Caption 3 (`DesktopScreenshotCaption3`):** Export to PDF, DOCX, HTML, EPUB, LaTeX, and more with high-fidelity formatting and built-in printing support.
- **Caption 4 (`DesktopScreenshotCaption4`):** Organize projects with an Explorer-style workspace featuring search, drag & drop, file management, and multi-tab editing.
- **Caption 5 (`DesktopScreenshotCaption6`):** Write GitHub-flavored Markdown or edit hundreds of text and source code file types with built-in syntax highlighting.
- **Caption 6 (`DesktopScreenshotCaption5`):** Stay productive with powerful keyboard shortcuts, document formatting, navigation tools, and an integrated Markdown toolbar.
- **Caption 7 (`DesktopScreenshotCaption7`):** Open PDF and HTML documents directly inside FastMD with zoom, search, printing, and seamless workflow integration.
- **Caption 8 (`DesktopScreenshotCaption8`):** Create documents once and export them to multiple professional formats without compromising layout or quality.
- **Caption 9 (`DesktopScreenshotCaption9`):** Manage Git without leaving your editor. Stage changes, review diffs, commit updates, browse history, and switch branches. All built directly into FastMD.
- **Caption 10 (`DesktopScreenshotCaption10`):** FastMD is equipped with a built-in terminal featuring multiple tabs, flexible docked and undocked modes, and seamless Light and Dark theme support.
- **Caption 11 (`DesktopScreenshotCaption11`):** Enjoy instant startup, low memory usage, pure native x64 & ARM64 Windows performance, and no ads or subscriptions.

---

## 5. Legal & Developer Info

### Copyright and Trademark Info
© 2026 Skypedia. All rights reserved.

FastMD is proprietary software © 2026 Skypedia. All rights reserved.

This software is provided "as is", without warranty of any kind, express or implied.

### Applicable License Terms
FastMD's public documentation and release information are available at:
https://github.com/skypediacode/fastmd

### Developed By
Skypedia

---

## 6. Archived Descriptions

### Version A
FastMD includes the Microsoft Visual C++ runtime.
FastMD is a fast, lightweight Markdown & Text Editor for Windows.

Looking for a fast, clean editor that simply gets out of your way?
Whether you're writing notes, editing README files, creating documentation, or working with plain text, FastMD helps you stay focused with a responsive, distraction-free experience.

Why you'll love FastMD:
⚡ Fast & Lightweight: Starts instantly, uses very little memory, and stays responsive even with large files.
💚 Private & Focused: No ads. No subscriptions. Just writing.
📝 Markdown & Plain Text: Open, edit, and save Markdown (.md) and text files with ease.
📄 Export & Print: Export to PDF, HTML, and Microsoft Word (.docx), or print your documents directly.
👀 Live Markdown Preview: See exactly how your Markdown will look while you write.
⌨️ Made for Productivity: Multiple tabs, workspace support, keyboard shortcuts, syntax highlighting, a built-in Markdown toolbar, and a clean, distraction-free interface.

### Version B
FastMD includes the Microsoft Visual C++ Runtime.
FastMD is a fast, lightweight Markdown & Text Editor with Notepad speed and Pro Markdown features.

Why you'll love FastMD:
⚡ Fast & Lightweight: Starts instantly, uses very little memory, and has zero bloat.
👀 Live Markdown Preview: See exactly how your Markdown will look as you write.
📄 Export & Print: Print documents directly or export to PDF/DOCX with bookmarks, Table of Content.
⌨️ Intuitive UX: Familiar keyboard shortcuts, multiple tabs, workspace support, and more.
💚 Free. No ads. No subscriptions.

### Version C
FastMD includes the Microsoft Visual C++ Runtime.
FastMD combines Notepad speed with professional Markdown features.

Write faster. Preview instantly. Export beautifully.
⚡ Instant startup & lightweight performance
👀 Interactive Preview with bi-directional sync, synchronized scrolling, click-to-source navigation, and zoom
📝 GitHub-flavored Markdown with TOC, LaTeX/KaTeX math, Mermaid diagrams, Smart Tables, code syntax highlighting, Callouts, Footnotes, and more
📂 Workspace Explorer, multi-tab editing, and cross-window tab dragging
📄 High-fidelity print and export to PDF, DOCX, HTML, EPUB, ODT, RTF, LaTeX, and more
💻 Edit Markdown and hundreds of text-based source code file types with built-in syntax highlighting
📑 Built-in PDF & HTML viewers
💚 No ads. No subscriptions. Just writing.

### Nov 2026:
FastMD combines Notepad speed with professional Markdown features.

📝 GitHub-flavored Markdown withLaTeX/KaTeX math, Mermaid diagrams, TOC, Smart Tables, code syntax highlighting, Callouts, Footnotes, and more
🚀 Native ARM64 & x64: native performance on both Intel/AMD and Windows on ARM with maximum battery efficiency.
🌿 Workspace Source Control: Built-in Git support with staged changes, read-only diff tabs, history graph, branch switching, and sync actions.
🖥️ Built-in Terminal: Run local shells without leaving FastMD.
📂 Workspace Explorer, multi-tab editing, and cross-window tab dragging
📄 High-fidelity print and export to PDF, DOCX, HTML, EPUB, ODT, RTF, LaTeX, and more
💚 Free. No ads. No subscriptions. Just writing.
⚡ Instant startup & lightweight performance
👀 Interactive Preview with bi-directional sync, synchronized scrolling, click-to-source navigation, and zoom
💻 Edit Markdown and hundreds of text-based source code file types with built-in syntax highlighting
📑 Built-in PDF & HTML viewers

Write faster. Preview instantly. Export beautifully.

FastMD includes the Microsoft Visual C++ runtime.

