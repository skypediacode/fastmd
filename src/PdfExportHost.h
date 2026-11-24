#pragma once
#include <QString>
#include "ExportManager.h"

// ---------------------------------------------------------------------------
// PdfExportHost — renders PDF exports through a dedicated, persistent,
// off-screen WebView2 host instead of shelling out to a second Chromium-family
// process (headless Chrome/Edge). The exported page is the exact same
// buildPreviewHtml() shell the live preview and Browser Preview use, so the
// PDF keeps its 1:1 visual parity with what the user sees on screen.
//
// The host owns one WebView2 environment + controller, created lazily on the
// first export and reused for every export after that -- the environment
// creation cost (profile init, disk/shader/code cache) is paid once per
// process lifetime instead of once per export (a fresh headless Chrome
// process) or, worse, racing the live preview's own WebView2 startup.
//
// All WebView2 calls happen on a dedicated background thread with its own COM
// STA + Qt event loop, entirely separate from both the UI thread's shared
// preview environment and the calling export-pool worker thread. renderToPdf()
// blocks the calling thread until the PDF is written (or the operation fails/
// times out) via a synchronous cross-thread call, so it drops in as a
// same-shaped replacement for the old QProcess-based wait.
// ---------------------------------------------------------------------------
class PdfExportHost {
public:
    // Renders the page at `navigateUrl` (a file:// URL to a temporary HTML
    // file -- see exportPdfViaWebView2() in ExportManagerPdf.cpp) to
    // `outputPath`, honoring `options`' page size/margins/orientation. Waits
    // for the page's own "rendering finished" signal (KaTeX/Mermaid/
    // highlight.js settled -- see preview.js's reportRendered()) before
    // printing, bounded by a generous timeout so a broken diagram can never
    // hang an export indefinitely. With `documentOutline`, the page's h1-h6
    // also become PDF bookmarks when the runtime supports it; a runtime that
    // does not still produces the PDF, just without them. Returns false and
    // fills `errorMessage` on any failure (environment/controller creation,
    // navigation, or the print itself); safe to call from any thread.
    static bool renderToPdf(const QString& navigateUrl, const QString& outputPath,
                            const ExportManager::PdfExportOptions& options,
                            bool documentOutline, QString* errorMessage = nullptr);

    // Tears down the host thread, controller and environment. Call once at
    // shutdown (see main.cpp), after the application event loop has stopped.
    static void shutdown();
};
