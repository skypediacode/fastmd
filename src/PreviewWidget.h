#pragma once
#include <QWidget>
#include <QString>
#include <QRect>
#include "ExportManager.h"
#include "PreviewPatch.h"

class QLabel;

// ---------------------------------------------------------------------------
// PreviewWidget — Markdown preview rendered by Microsoft Edge WebView2.
//
// The window is shown immediately with a themed blank background; the WebView2
// environment + controller are created asynchronously (lazily, on first show)
// so startup is never blocked. A reusable HTML shell is loaded once and content
// is updated incrementally by posting web messages (no full page reloads).
//
// The public surface mirrors the former QTextBrowser-based widget so the rest
// of the app is unchanged, except that scroll position is expressed as a
// fraction (0..1) instead of via QScrollBar (WebView2 has no QScrollBar).
// ---------------------------------------------------------------------------
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    enum class Presentation { Preview, MindMap };

    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    static void shutdownSharedEnvironment();

    // `preparedHtml`, when non-null, is the result of prepareBodyForBrowser(html)
    // computed off the UI thread by MainWindow's conversion worker. Pass it
    // whenever it is already available; otherwise it is computed here on demand.
    void setBodyHtml(const QString& html, const QString& preparedHtml = QString());
    // A conversion result from the worker (see buildPreviewBodyUpdate()). Sends
    // only its patch when the patch was computed against exactly the body this
    // page holds; otherwise the whole body, as setBodyHtml() would.
    void applyBodyUpdate(const PreviewBodyUpdate& update);
    // The body the page was last sent with its block split, or null when it is
    // not known (not yet posted, page reloading, or the split disagreed with
    // the browser). Workers diff new conversions against it.
    PreviewBlocksPtr postedBlocks() const { return m_postedBlocks; }
    void setPresentation(Presentation presentation);
    Presentation presentation() const { return m_presentation; }
    bool isMindMap() const { return m_presentation == Presentation::MindMap; }
    void setMindMapMarkdown(const QString& markdown);
    void requestMindMapSvg();
    // Rescales and re-centers the map so the whole tree fits the panel, the
    // same framing the first render produces. Panning/zooming is otherwise
    // preserved across edits, so this is the only way back to a known view.
    void fitMindMap();

    // Pure string transform (page-break marker + <img src> rewriting to the
    // fastmd.doc virtual host). Touches no widget state, so it is safe to run on
    // a worker thread — see MainWindow::updateCurrentHtmlAsync().
    static QString prepareBodyForBrowser(QString body);
    void setBasePath(const QString& path);
    void showReadOnlyFile(const QString& path);
    void printReadOnlyFile();
    bool isHtmlViewer() const { return m_readOnlyKind == ReadOnlyKind::Html; }
    bool isSvgViewer() const { return m_readOnlyKind == ReadOnlyKind::Svg; }
    void htmlZoomIn();
    void htmlZoomOut();
    void htmlZoomByWheel(double deltaY);
    void resetHtmlZoom();
    void setDarkMode(bool dark);
    bool isRefreshing() const { return m_refreshing; }
    void setBaseFontSize(int size);
    // The Preview font family (empty: the stylesheet's own). A change restyles
    // the page and so reflows it; an unchanged family posts nothing.
    void setBodyFontFamily(const QString& family);
    // Document Spacing (line, paragraph, list). Sent to the page only on a change,
    // or on ready when non-default.
    void setDocumentSpacing(const DocumentSpacing::Settings& spacing);
    void ensureInitialized();

    // Moves real keyboard focus into the WebView2 child. A plain Qt setFocus()
    // is not enough -- the page lives in a native child window, so focus has to
    // be handed over through the controller. Used when Reading Mode hides the
    // Editor and the Preview becomes the only thing there is to drive.
    void focusContent();

    // Searching the *rendered* page, for Reading Mode -- where the Editor is
    // hidden and FindOverlay/FindController (which search the Markdown source
    // and highlight it in a QPlainTextEdit) have nothing to act on.
    //
    // This shows the runtime's own find bar, which is the same one `.html` and
    // `.svg` viewer tabs already get; those reach it through
    // AreBrowserAcceleratorKeysEnabled(TRUE), which the Markdown preview cannot
    // use because it would hand Ctrl+P/Ctrl+S/F5 to Chromium and break the
    // shortcut forwarding in AcceleratorKeyPressed. Driving the Find API
    // directly gives the same bar from FastMD's own Ctrl+F with the accelerator
    // keys left off.
    //
    // `showFindBar` returns false if the installed Evergreen runtime predates
    // the Find API, so the caller can fall back to the editor-side Find. An
    // empty `term` is legal: the bar opens without searching.
    bool showFindBar(const QString& term = {});
    bool findNextMatch();
    bool findPreviousMatch();
    void stopFind();

    // Searching a PDF tab for FindOverlay: the same Find API with the runtime's
    // bar suppressed, reporting through documentFindChanged(). The term is
    // remembered and searched again whenever the file is (re)loaded. An empty
    // term is legal: the session is open with nothing to look for.
    //
    // There are no case or whole-word options: the PDF engine ignores
    // IsCaseSensitive and ShouldMatchWord (measured on runtime 155).
    bool startDocumentFind(const QString& term);
    void stopDocumentFind();

    void prepareForReparent();
    // Kicks off WebView2 environment creation without needing any PreviewWidget
    // instance to exist yet (the environment is a process-wide singleton -- see
    // requestEnvironment() in PreviewWidget.cpp). Static so MainWindow can call
    // it as early as possible during startup, before session restore has
    // decided which tab/preview will end up active.
    static void warmEnvironment();

    // Scroll position as a fraction of the scrollable range (0..1). Replaces the
    // old verticalScrollBar() access used for scroll sync and session restore.
    void   setScrollFraction(double f);
    void   smoothScrollToFraction(double f);
    double lastScrollFraction() const { return m_lastScrollFraction; }

    // Outline / heading navigation: scroll to an element id or anchor name.
    void scrollToAnchor(const QString& id);

    // Scrolls to the block whose data-src-start/data-src-end (see
    // MarkdownPreviewRenderer) best matches `line` (containing range, else
    // nearest), positioning it near the vertical center of the viewport. No-op
    // if the shell isn't ready yet -- callers drive this off previewRendered().
    // Used for the one-time Editor -> Preview sync after session restore (see
    // MainWindow::restoreTabState); scrollbar-fraction restore doesn't work
    // there because Editor/Preview content heights differ substantially.
    void scrollToSourceLine(int line, bool flash = false);

    // The Outline button lives inside the WebView2 page itself (see
    // resources/preview/preview.{html,css,js}) rather than as a separate
    // native Qt overlay widget — its hover-expand animation is then
    // composited by the browser like any other page content, with no native
    // HWND resize/repaint involved.
    //
    // Forces the button's expanded/highlighted state while the (natively
    // hosted) outline popup is open.
    void setOutlinePopupOpen(bool open);

    // The button's on-screen rect, for positioning the outline popup under
    // it. Computed from fixed constants (top/right padding 24, height 40,
    // expanded width 118) that mirror #outline-btn's CSS — keep both in sync.
    QRect outlineButtonGeometry() const;

    // App-level commands that make sense while the preview (not the editor)
    // has keyboard focus. WebView2 hosts a genuine native child window, so its
    // key events never reach Qt's shortcut system on their own — the Impl
    // forwards the keys below via ICoreWebView2Controller::AcceleratorKeyPressed
    // and emits commandRequested() instead of letting them fall on the floor.
    enum class Command {
        Find, Replace, FindNext, FindPrevious,
        Save, SaveAs, SaveAll, Print,
        ExportHtml, ExportDocx, ExportPdf,
        Preferences, ZoomIn, ZoomOut, CloseTab,
        TogglePreview, ToggleReadingMode, ToggleMindMap, CommandPalette, WorkspaceSearch,
        FindInFiles, ReplaceInFiles,
        SwitchEditorMode, ToggleWordWrap,
        Back, Forward, FormatDocument, RefreshPreview,
        CloseDocumentFind,
    };


public slots:
    void refreshPreview();
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);

    // Copy the current Preview text selection / select all Preview content.
    // Implemented via WebView2 ExecuteScript rather than routing through Qt,
    // since the page's selection lives entirely inside the native WebView2
    // child window.
    void copy();
    void selectAll();
    void copyMermaidImage(const QString& pngDataUrl);
    void copyMermaidSvg(const QString& svg);
    void saveMermaidImageAs(const QString& pngDataUrl);
    void saveMermaidSvgAs(const QString& svg);
    void saveMindMapSvgAs(const QString& svg, const QString& initialPath);

signals:
    void zoomRequested(int delta);
    void previewRendered();          // emitted when the latest content is displayed
    void scrolled(double fraction);  // emitted when the user scrolls the preview
    void commandRequested(PreviewWidget::Command cmd);  // forwarded app shortcut
    void outlineRequested();         // the in-page Outline button was clicked

    // The user right-clicked inside the Preview. `globalPos` is already mapped
    // to screen coordinates; Mermaid payloads are empty for ordinary content.
    void contextMenuRequested(const QPoint& globalPos, bool hasSelection, int sourceLine,
                              const QString& mermaidSvg, const QString& mermaidPngDataUrl);

    // Preview-to-Editor source highlighting (see resources/preview/preview.js):
    // emitted when the user double-clicks mapped content in the Preview.
    void previewFlashRequested(int line);
    void taskToggleRequested(int startLine, int endLine, bool checked);
    void editorFocusRequested();
    void linkRequested(const QString& url);
    // An empty URL means the pointer left the link. The host owns the status
    // presentation so the WebView page never needs a competing hover tooltip.
    void linkHoverChanged(const QString& url);
    void mindMapSvgReady(const QString& svg);
    // `activeIndex` is zero-based, -1 when no match is active.
    void documentFindChanged(int matchCount, int activeIndex);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    class Impl;                       // WebView2 COM plumbing (Pimpl, Windows-only)
    Impl* d = nullptr;

    QString m_bodyHtml;
    QString m_mindMapMarkdown;
    // prepareBodyForBrowser(m_bodyHtml), cached: the shell can be re-pushed
    // (on 'ready', after a reparent) without redoing the rewrite pass.
    QString m_preparedBodyHtml;
    // Block split of m_preparedBodyHtml, when known.
    PreviewBlocksPtr m_bodyBlocks;
    // The JSON fields of the whole-body message, when the worker built them.
    // Dropped once posted: a re-push on 'ready' is rare and rebuilds them.
    QString m_fullFields;
    // What the page holds as far as patches are concerned: the blocks of the
    // last message posted, the seq that message carried, and the seq of the
    // last whole-body message (a resync request older than it is answered).
    PreviewBlocksPtr m_postedBlocks;
    long long m_postedSeq = 0;
    long long m_lastFullSeq = 0;
    QString m_basePath;
    QString m_readOnlyFilePath;
    // Cached classification of m_readOnlyFilePath; see setReadOnlyFilePath().
    enum class ReadOnlyKind { Other, Html, Svg };
    ReadOnlyKind m_readOnlyKind = ReadOnlyKind::Other;
    bool    m_bodyNeedsRefresh = true;
    Presentation m_presentation = Presentation::Preview;
    bool    m_initializationRequested = false;
    bool    m_dark = false;
    int     m_baseFontSize = FastMdDefaults::PreviewFontSize;
    QString m_bodyFontFamily;
    DocumentSpacing::Settings m_documentSpacing;
    bool    m_refreshing = false;
    double  m_lastScrollFraction = 0.0;
    double  m_lastPostedScrollFraction = 0.0;
    bool    m_scrollFractionPosted = false;
    // FindNext/FindPrevious silently do nothing without a session, so callers
    // need to know whether Start() has run before routing F3 here.
    bool    m_findSessionActive = false;
    bool    m_documentFindActive = false;
    QString m_documentFindTerm;
    QLabel* m_fallbackLabel = nullptr;

    bool startFindSession(const QString& term, bool suppressBar);

    void setReadOnlyFilePath(const QString& path);
    void pushContent();               // sends current body to the shell (if ready)
    void adoptBody(const QString& html, const QString& prepared, PreviewBlocksPtr blocks,
                   const QString& fullFields);
    void pushTheme();                 // sends current theme/font size to the shell
    void pushBodyFont();              // sends the font stack; only on change or ready
    void pushDocumentSpacing();       // sends the spacing variables; only on change or ready
    void updateBrowserAccelerators();
    // True for any standalone read-only document opened via showReadOnlyFile()
    // (HTML or SVG) as opposed to the live Markdown preview shell -- the two
    // behave identically for context menus, accelerator keys, and zoom.
    bool isReadOnlyDocumentViewer() const { return isHtmlViewer() || isSvgViewer(); }
    void showFallback(const QString& message);
    void handleWebMessage(const QString& json);

    friend class Impl;
};
