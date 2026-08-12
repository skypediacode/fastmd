# FastMD: The Lightning-Fast, Native Markdown Text Editor

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](https://microsoft.com/windows)
[![Microsoft Store](https://img.shields.io/endpoint?url=https%3A%2F%2Fmicrosoft-store-badge.fly.dev%2Fapi%2Frating%3FstoreId%3D9n4dsd4wnz9d%26market%3DUS&style=flat-square&label=Microsoft+Store&color=blue&logo=Windows)](https://apps.microsoft.com/detail/9n4dsd4wnz9d)
![FastMD Screenshot](screenshot.png)

**FastMD** is an ultra-fast, native, and distraction-free Markdown Text Editor. It is currently available on Windows. Built from the ground up with **C++20** and **Qt 6 Widgets**, FastMD focuses on one thing: providing the absolute best typing performance and a frictionless writing experience.

If you are tired of sluggish, memory-heavy Electron apps and want a Markdown Text Editor that launches instantly and respects your system resources, FastMD is built for you.

## 🚀 The FastMD Philosophy: Speed Over Bloat

We believe that your writing tool should stay out of your way. Our core philosophy is simple:
**Speed > Simplicity > Stability > Maintainability > Features.**

To guarantee that typing performance remains inviolable, FastMD relies on strict architectural boundaries. **You will never see the following in FastMD:**

- ❌ Electron, Node.js, or a **bundled** Chromium/browser engine
- ❌ Qt WebEngine (which would embed Chromium into the app)
- ❌ Heavy Database backends (SQLite)
- ❌ Bloated Project Explorers or Sidebars
- ❌ Cloud sync, AI features, or plugins

The editor, UI, and startup path are 100% native C++/Qt. The Markdown **preview**
is rendered by the **Evergreen Microsoft Edge WebView2 runtime** — the browser
engine that already ships with Windows. Nothing is bundled: no Chromium is shipped,
the executable stays tiny, and the WebView2 process is created lazily and
asynchronously *after* the window is already on screen, so startup stays instant.

Just you, your words, and blistering fast performance.

## ✨ Key Features

### 🌟 Standout Capabilities (What Typical Editors Lack)

- 🧪 **Chemistry Typesetting with `mhchem` & Visual Chemistry Toolbar:** Native `\ce{...}` chemical formula & equation rendering ($\ce{H2O}$, $\ce{SO4^2-}$, reactions $\ce{A -> B}$), paired with a dedicated visual **Chemistry Toolbar** for 1-click insertion of chemical arrows, reaction states, charges, and bond notations.
- 🧮 **Visual Math Toolbar with Live KaTeX Previews:** Categorized symbol palette (matrices, integrals, fractions, Greek letters, operators, relations) with real KaTeX previews directly on every toolbar button. Supports inline `$…$`, `\(…\)` and display `$$…$$`, `\[…\]` delimiters.
- 🧠 **Interactive Offline Mind Maps (`F10`):** Instantly render any Markdown document as an interactive mind map. Double-clicking any mind map node jumps to its source line in the editor. Export mind maps directly to self-contained HTML, PDF, DOCX, or SVG.
- 🎯 **Line-Level Precise Bi-Directional Preview Sync:** Double-click any element in the live preview (including individual lines of multi-line paragraphs, list items, headings, or callouts) to jump directly to the exact source line. Ticking task list checkboxes in the preview automatically rewrites the Markdown source.
- 🧹 **Auto Format & Table Column Prettifying (`Shift+Alt+F`):** Auto-aligns Markdown table pipes into neat ASCII columns and repairs stale list numbering without affecting output. Includes a visual table size grid picker (`Ctrl+Alt+T`).
- 📢 **GitHub Callouts & Document Extras:** Native blockquote callouts (`[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, `[!CAUTION]`), `[TOC]` dynamic table of contents, Footnotes (`[^1]`), Definition lists (`Term : Def`), PDF page breaks (`Ctrl+Enter`), text highlight (`==text==`), sub/superscript (`H~2~O`, `x^2^`), and YAML front matter metadata auto-enriching HTML/PDF exports.
- 🖼️ **Native Image, SVG, PDF & HTML Viewers:** Built-in raster image viewer with 1:1 zoom, fit-to-window, pan, and rotation; native SVG viewer; and read-only PDF and HTML preview modes — all without relying on Electron or Qt WebEngine.

### 🖼️ Screenshots & Gallery

Explore high-resolution interface screenshots in our dedicated [Screenshots Showcase](https://skypediacode.github.io/fastmd/screenshots.html) (or `screenshots.html`).

### 🗂️ All-in-One Feature Index (Find Any Feature Instantly)

| Category | Supported Features & Capabilities |
| :--- | :--- |
| 🧪 **Math & Science** | LaTeX Math (`$inline$`, `$$display$$`, `\(...\)`, `\[...\]`), KaTeX rendering, `mhchem` chemistry (`\ce{H2O}`, `\ce{SO4^2-}`, reactions), **Visual Math Toolbar** (1-click symbol previews), **Visual Chemistry Toolbar** (arrows, states, charges). |
| 📊 **Diagrams & Mind Maps** | **Interactive Mind Maps (`F10`)** with node-to-source jump, Mermaid diagrams (`flowchart`, `sequenceDiagram`, `gantt`, `erDiagram`, `classDiagram`), click-to-zoom viewer, export to SVG/PNG. |
| 🎯 **Live Preview & Sync** | **Line-level bi-directional sync** (double-click preview paragraph/heading/list to jump to source line), interactive preview checkboxes, Reading Mode (`F11`), synchronized scrolling. |
| 🧹 **Editing & Auto-Format** | **Auto Format Document (`Shift+Alt+F`)** with ASCII table pipe alignment & list numbering repairs, Table grid picker (`Ctrl+Alt+T`), Command Palette (`F1`), 190+ programming language syntax highlighting. |
| 📢 **Callouts & Rich Text** | GitHub Callouts (`[!NOTE]`, `[!TIP]`, `[!WARNING]`, `[!IMPORTANT]`, `[!CAUTION]`), Footnotes (`[^1]`), Definition lists (`Term : Def`), Text highlight (`==text==`), Sub/Superscript (`H~2~O`, `x^2^`), Dynamic `[TOC]`, PDF Page Breaks (`Ctrl+Enter`), YAML Front Matter. |
| 📄 **Export & Printing** | PDF export (with heading bookmarks), Word (`.docx`), self-contained HTML (offline CSS/fonts), EPUB, RTF, ODT, LaTeX (`.tex`), mode-aware printing, Page Setup control. |
| 📂 **Workspace & Viewers** | Workspace file tree (`F8`) with drag-and-drop & search, cross-window tab dragging, session restore, built-in **Image Viewer** (1:1 zoom, pan, rotate 90°), built-in PDF & HTML read-only viewers. |

### 📊 By The Numbers (FastMD vs. Electron)

While Electron editors **bundle** an entire Chromium browser plus a Node.js runtime into every app, FastMD ships neither. The editor is native C++/Qt, and the preview reuses the **shared** Edge WebView2 runtime already present on Windows (created on demand, off the startup path). Here is a rough comparison of what that means for your system:

| Metric                   | Typical Electron Editor | FastMD (C++ / Qt)          | Difference    |
| ------------------------ | ----------------------- | -------------------------- | ------------- |
| **Base Executable Size** | 100 MB – 150 MB         | **~2 MB** (no bundled browser) | ~50x+ smaller |
| **Chromium shipped**     | Yes (per app)           | **No** (shared OS runtime) | —             |
| **Cold Startup (window visible)** | 2.0s – 4.0s    | **~0.4–0.5s**              | ~5–8x faster  |
| **Idle RAM Usage**       | 500 MB – 1000 MB        | **~250 MB** (incl. shared WebView2 preview) | ~2–4x lighter |

_Numbers are approximate. FastMD's RAM figure is the whole process tree including the WebView2 preview processes; the native editor core alone is far smaller, and no WebView2 is created until the preview is first shown._

### ⚡ Native Performance & Low Memory Footprint

By leveraging native **C++20** and the **Qt 6 Widgets** framework (rather than QML or Qt WebEngine), FastMD achieves near-instant startup times. The window appears and is fully interactive before the WebView2 preview engine is even initialized — WebView2 is created asynchronously in the background, so it never blocks the editor or startup.

### 📝 Blazing Fast Markdown Parsing

FastMD utilizes the [MD4C](https://github.com/mity/md4c) parser—a highly optimized, CommonMark-compliant C library. Whether you're opening a 10-line note or a 10,000-line manuscript, rendering is instantaneous. Features like tables, strikethroughs, and tasklists are fully supported out of the box.

### 🖨️ Seamless HTML & PDF Export

Exports are regenerated from your raw Markdown through the same MD4C + KaTeX pipeline the preview uses, so what you see closely matches what you export.

- **PDF Export:** Configurable page sizes, margins, fonts, and automatic image scaling.
- **HTML Export:** Clean, self-contained output perfect for publishing.

### 🎨 Clean, Distraction-Free UI

- **Light & Dark Themes:** Beautifully crafted, high-contrast themes that switch instantly.
- **Material Icons Toolbar:** A crisp, Notepad++ style lightweight toolbar utilizing embedded Material Icons.
- **Tabbed Interface:** Effortlessly manage multiple files with an intuitive tabbed workspace.

## Command line (Microsoft Store)

The installation from Microsoft Store registers the `fastmd` App Execution Alias. After installing it,
launch FastMD from Command Prompt, PowerShell, or Windows Terminal:

```bat
fastmd
fastmd .
fastmd README.md
fastmd file1.md file2.md
fastmd C:\Project
fastmd . file.md
fastmd *.md
```

Files open as tabs in the existing FastMD window. Folder arguments set the
Workspace root; when a folder and files are supplied together, the supplied
folder remains the Workspace root. Wildcards are expanded by FastMD, so they
also work in Command Prompt, which does not expand them itself.

## 📥 Downloads & Installation

- **Microsoft Store:** Get FastMD on the <a href="https://apps.microsoft.com/store/detail/9N4DSD4WNZ9D?cid=DevShareMCLPCS" target="_blank" rel="noopener">Microsoft Store</a>.
- **Windows Installer:** <a href="FastMD-Setup-Latest.exe" target="_blank" rel="noopener">FastMD-Setup-2.5.2.exe</a> — Standard Windows setup installer package included directly in this repository.
- **Portable Version:** <a href="FastMD-Portable.zip" target="_blank" rel="noopener">FastMD-Portable.zip</a> — Standalone portable archive (no installation required) included directly in this repository.

## ℹ️ About This Repository

This repository contains the official website pages (GitHub Pages), terms of service, privacy policy, and open-source license documentation for **FastMD**.

> **Note:** The source code for FastMD is closed-source / proprietary.

## 📜 License

See `licenses.html` for third-party open-source component licenses utilized by FastMD.

---

_Stop waiting on your editor. Start writing with FastMD._
