#include "MainWindow.h"
#include "DarkTheme.h"
#include "LanguageSelectorPopup.h"
#include "StartupProfiler.h"
#include "CommandShortcutRegistry.h"

#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "MarkdownToc.h"
#include "PreviewWidget.h"
#include "FindController.h"
#include "FindOverlay.h"
#include "GoToLineDialog.h"
#include "PreferencesDialog.h"
#include "RecoveryManager.h"
#include "ExportManager.h"
#include "ExportThreadPool.h"
#include "LicensesDialog.h"
#include "AboutDialog.h"
#include "ShortcutsDialog.h"
#include "DocumentModel.h"
#include "CommandPaletteLauncher.h"
#include "CommandPalettePopup.h"
#include "SyntaxDefinitionResolver.h"
#include "SyntaxHighlightingRepository.h"
#include <QDateTime>
#include "Stylesheet.h"
#include "ApplicationTheme.h"
#include "IconHelper.h"
#include "MathToolbar.h"
#include "ChemistryToolbar.h"
#include "ToolbarHoverAnimation.h"
#include "ToolbarOverflow.h"
#include "MatrixInsertDialog.h"
#include "OutlinePopup.h"
#include "AlignedMenu.h"
#include "WindowTitleBar.h"
#include "NativeWindowFrame.h"
#include "RecentFilesManager.h"
#include "RecentFoldersManager.h"
#include "TextTransforms.h"
#include "WorkspacePanelController.h"
#include "SourceControlPanel.h"
#include "WorkspaceFileOps.h"
#include "GitTools.h"
#include "TabDragController.h"
#include "ThemedSplitter.h"
#include "ThemePalette.h"
#include "TerminalPanel.h"
#include "DetachedTerminalWindow.h"
#include "UpdateChecker.h"
#include "ThemedMessageBox.h"

#include <QMenuBar>
#include <QMenu>
#include <QContextMenuEvent>
#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QLabel>
#include <QTreeView>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSettings>
#include <QThread>
#include <QSaveFile>
#include <QStandardPaths>
#include <memory>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QScrollBar>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextCursor>
#include <algorithm>
#include <cstdio>
#include <utility>
#include <QSplitter>
#include <QPalette>
#include <QStyle>
#include <QRegularExpression>
#include <QPainter>
#include <QTransform>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QToolButton>
#include <QTimer>
#include <QStandardPaths>
#include <QPageSetupDialog>
#include <QPrinter>
#include <QSignalBlocker>
#include <QGridLayout>
#include <QFileSystemModel>
#include <QTreeView>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <appmodel.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>
#endif
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QHideEvent>
#include <QShortcut>
#include <QInputDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QShowEvent>
#include <QPointer>
#include <functional>
#include <vector>

#ifdef Q_OS_WIN
// Not static: also called from MainWindowExport.cpp's ExportSuccessDialog and
// from DetachedTerminalWindow.
// Returns whether the DWM attributes were accepted, so callers can tell an
// effective application from one that needs a retry once the window is native.
//
// `includeCaptionColor` defaults to true for every ordinary caller, all of
// which have a real, visible native caption to color. DetachedTerminalWindow
// passes false: its WM_NCCALCSIZE handler suppresses the caption area down to
// zero height, so DWMWA_CAPTION_COLOR/DWMWA_CAPTION_TEXT_COLOR have no caption
// left to paint -- but DWM still folded that dark caption color into its drop
// shadow, making the shadow read as an oversized dark halo specifically in
// dark mode (light mode's near-white caption color happened to disappear
// against a light desktop). Leaving those two attributes at their default
// (unset) avoids that while attribute 19/20's dark chrome still applies.
bool setWindowsTitleBarDarkMode(QWidget* window, bool dark, bool includeCaptionColor = true)
{
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL enabled = dark ? TRUE : FALSE;

    // Attribute 20 is used by current Windows 10/11 builds. Attribute 19 is
    // retained for older builds that exposed the dark-mode setting under it.
    bool ok = true;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))))
        ok = SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)));

    if (includeCaptionColor) {
        const COLORREF captionColor = dark ? RGB(0x19, 0x1a, 0x1b) : RGB(0xf7, 0xf7, 0xf7);
        const COLORREF textColor = dark ? RGB(0xbf, 0xbf, 0xbf) : RGB(0x20, 0x20, 0x20);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
    }

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return ok;
}

#endif

// ---------------------------------------------------------------------------
// TableSizePicker — lightweight popup for choosing table dimensions
// ---------------------------------------------------------------------------
class TableSizePicker : public QWidget {
public:
    explicit TableSizePicker(std::function<void(int, int)> callback, QWidget* parent = nullptr)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
        , m_cb(std::move(callback))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        updateSize();

        // Qt::Popup already grabs mouse/keyboard, but a click landing on a
        // native child window (e.g. the WebView2 preview) can bypass that
        // grab. Watching every application mouse press guarantees the popup
        // still closes.
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }

protected:
    void hideEvent(QHideEvent* event) override
    {
        if (m_filterInstalled) {
            qApp->removeEventFilter(this);
            m_filterInstalled = false;
        }
        QWidget::hideEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (!geometry().contains(me->globalPosition().toPoint()) && isVisible())
                close();
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        // Background + border
        p.fillRect(rect(), palette().window());
        p.setPen(palette().mid().color());
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const QColor activeColor  = palette().highlight().color();
        const QColor activeBorder = activeColor.darker(160);
        const QColor idleColor    = palette().base().color();
        const QColor idleBorder   = palette().mid().color();

        for (int r = 0; r < m_maxRows; ++r) {
            for (int c = 0; c < m_maxCols; ++c) {
                bool active = (r < m_hovRow) && (c < m_hovCol);
                p.fillRect(cellRect(r, c), active ? activeColor : idleColor);
                p.setPen(active ? activeBorder : idleBorder);
                p.drawRect(cellRect(r, c));
            }
        }

        // Label
        QString label = (m_hovRow > 0 && m_hovCol > 0)
            ? QString("%1 \xC3\x97 %2 Table").arg(m_hovRow).arg(m_hovCol)
            : tr("Insert Table");
        p.setPen(palette().text().color());
        QRect lr(m_pad, m_pad + m_maxRows * (m_cell + m_gap) - m_gap + 6,
                 width() - 2 * m_pad, m_labelH);
        p.drawText(lr, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        int row = 0, col = 0;
        for (int r = 0; r < m_maxRows && row == 0; ++r)
            for (int c = 0; c < m_maxCols && col == 0; ++c)
                if (cellRect(r, c).contains(e->pos())) { row = r + 1; col = c + 1; }
        if (row != m_hovRow || col != m_hovCol) { m_hovRow = row; m_hovCol = col; update(); }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_hovRow > 0 && m_hovCol > 0) {
            auto cb = m_cb;
            int r = m_hovRow, c = m_hovCol;
            cb(r, c);
            QTimer::singleShot(0, this, &QWidget::close);
        }
    }

private:
    static constexpr int m_maxRows = 8;
    static constexpr int m_maxCols = 8;
    static constexpr int m_cell    = 22;
    static constexpr int m_gap     = 2;
    static constexpr int m_pad     = 8;
    static constexpr int m_labelH  = 22;

    QRect cellRect(int row, int col) const
    {
        return QRect(m_pad + col * (m_cell + m_gap),
                     m_pad + row * (m_cell + m_gap),
                     m_cell, m_cell);
    }

    void updateSize()
    {
        int w = 2 * m_pad + m_maxCols * m_cell + (m_maxCols - 1) * m_gap;
        int h = 2 * m_pad + m_maxRows * m_cell + (m_maxRows - 1) * m_gap + 6 + m_labelH;
        setFixedSize(w, h);
    }

    std::function<void(int, int)> m_cb;
    int m_hovRow = 0, m_hovCol = 0;
    bool m_filterInstalled = false;
};


namespace {



// Wraps raw editor text in a <pre> block for the WebView2/browser print pipeline,
// so Plain Text documents print exactly as typed (no Markdown parsing) while still
// getting the same Chromium print preview as Markdown documents.










} // namespace

void MainWindow::openUrl(const QString& urlStr)
{
    QDesktopServices::openUrl(QUrl(urlStr));
}

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent, StartupMode mode, StartupSettings startupSettings,
                       bool forceNotepadMode)
    : QMainWindow(parent), m_startupMode(mode), m_startupSettings(std::move(startupSettings))
{
    setWindowTitle(QStringLiteral("FastMD"));
    // The narrowest the window may usefully get is about the width of the
    // Command Palette (see CommandPaletteLauncher.h) -- reuse that constant
    // rather than inventing a second magic number. Height is derived as a
    // fixed ratio of that width rather than a second hand-picked constant.
    static constexpr int kMinWindowWidth = CommandPalette::PreferredWidth;
    static constexpr int kMinWindowHeight = kMinWindowWidth * 2 / 3;
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);
    resize(1200, 750);
    FASTMD_MARK("  ctor: begin");
    // m_findOverlay is created lazily on first Find/Replace — see
    // ensureFindOverlay(). m_findController is headless and cheap, so it is
    // constructed eagerly: F3/Shift+F3 must work without the overlay ever
    // having been shown.
    m_findController = new FindController(this);
    connect(m_findController, &FindController::navigationRequested,
            this, &MainWindow::recordNavigationDeparture);
    FASTMD_MARK("  ctor: findController");

    createWorkspacePanel();
    FASTMD_MARK("  ctor: createWorkspacePanel");
    createTitleBar();
    FASTMD_MARK("  ctor: createTitleBar");
    createMenus();
    FASTMD_MARK("  ctor: createMenus");
    createTitleBarLayoutToggles();
    createToolbar();
    FASTMD_MARK("  ctor: createToolbar");
    createStatusBar();
    FASTMD_MARK("  ctor: createStatusBar");
    readSettings();
    FASTMD_MARK("  ctor: readSettings");
    // --notepad reuses the same deferred restore completeStartup() already
    // performs for a persisted Notepad Mode session, so it goes through the
    // identical apply path (once an active tab exists) instead of a second
    // one; it does not bypass or duplicate setNotepadMode().
    if (forceNotepadMode)
        m_restoreNotepadModeOnStartup = true;
    // Only the visibility flag and the panel's reserved splitter geometry are
    // established here. Building the tree UI and the QFileSystemModel (a
    // file-info-gatherer thread, the shell icon provider, and a directory
    // population) is a priority-0 task, so the default configuration no longer
    // pays for it before first paint.
    m_workspacePanelCtrl->setVisibleDeferringBuild(m_workspaceTreeVisible);
    if (m_workspaceTreeVisible) {
        FASTMD_MARK("  ctor: workspace panel visible-init");
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([this]() {
                // Re-reads the visibility flag, so a panel the user toggled
                // off between first paint and this turn stays off.
                m_workspacePanelCtrl->materializeAfterFirstPaint();
                syncWorkspaceHeaderHeight();
                FASTMD_MARK("  task: workspace panel materialized");
            }, 0);
        });
    }
    // Enabling spell check spawns the platform spell-check factory and reads
    // the installed language list, so a session that had it on pays for that
    // after first paint rather than before it. A session with it off - the
    // default - enqueues nothing at all.
    if (m_spellCheckEnabled) {
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([this]() {
                FASTMD_MARK("  task: spell check activation begin");
                applySpellCheckState(true, m_spellCheckLanguage);
                FASTMD_MARK("  task: spell check activated");
            }, 3);
        });
    }

    syncSidebarModeActions();
    // Applied directly rather than through setStatusBarVisible(): that path
    // persists settings, which must not run mid-construction.
    if (!m_statusBarVisible && statusBar())
        statusBar()->setVisible(false);
    if (m_mathToolbarVisible) {
        ensureMathToolbar();
        m_mathToolbar->containerWidget()->setVisible(true);
        FASTMD_MARK("  ctor: math toolbar");
    }
    if (m_chemistryToolbarVisible) {
        ensureChemistryToolbar();
        m_chemistryToolbar->containerWidget()->setVisible(true);
        FASTMD_MARK("  ctor: chemistry toolbar");
    }
    if (m_actMathToolbar)
        m_actMathToolbar->setChecked(m_mathToolbarVisible);
    if (m_actChemistryToolbar)
        m_actChemistryToolbar->setChecked(m_chemistryToolbarVisible);
    // Only the primary window restores a terminal (see the startup task
    // below), so only there does the action start out checked.
    if (m_actTerminal)
        m_actTerminal->setChecked(m_terminalVisible && m_startupMode == StartupMode::Normal);
    applyTheme(m_theme);
    FASTMD_MARK("  ctor: applyTheme");

    connect(m_tabs, &TabWidget::editorActivated, this, &MainWindow::onEditorActivated);
    connect(m_tabs, &TabWidget::tabClosed,       this, &MainWindow::onTabCloseRequested);
    connect(m_tabs, &TabWidget::outlineRequested,this, &MainWindow::showOutlinePopup);
    connect(m_tabs, &TabWidget::tabRenameRequested,    this, &MainWindow::renameTab);
    connect(m_tabs, &TabWidget::tabContextMenuRequested, this, &MainWindow::showTabContextMenu);
    connect(m_tabs, &TabWidget::tabBarContextMenuRequested, this, &MainWindow::showTabBarContextMenu);
    connect(m_tabs, &TabWidget::newTabRequested, this, &MainWindow::newFile);
    connect(m_tabs, &TabWidget::diffZoomChanged, this, &MainWindow::setDiffZoomDelta);
    connect(m_tabs, &TabWidget::tabDragStarted, this,
            [this](TabPage* page, QPoint press, QPoint position) {
                TabDragController::instance()->beginDrag(this, m_tabs, page, press, position);
            });

    setAcceptDrops(true);

    if (m_startupMode != StartupMode::TransferTarget) {
        // Enqueued first so it lands in the priority-0 bucket ahead of session
        // restore below: environment creation is a fully async COM call (the
        // actual browser process spin-up happens off-thread), so starting it
        // before restorePreviousSession's synchronous file I/O lets the two
        // overlap instead of the preview's cold start only beginning once
        // restore -- and whichever tab restore decides is active -- is done.
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([] { PreviewWidget::warmEnvironment(); }, 0);
        });
        // Pays the headless export browser's first-launch cost (profile init,
        // disk/shader/code cache against the persistent export profile) in the
        // background at startup, so a user's first PDF/PNG export doesn't pay
        // it instead -- see ExportManager::warmExportBrowser(). Measured this
        // cost at ~20s cold vs ~0.3-0.5s once warm on a real machine -- but
        // only when it raced the WebView2 environment above: WebView2 alone
        // spins up ~7-12 processes, and starting a *second* Chromium-family
        // cold-start (this one) into that same burst is what inflated 0.4s to
        // 20s, not the export browser's own bootstrap cost. Giving it a few
        // seconds' head start (well after firstPainted, session restore, and
        // WebView2's own environment creation have all had a chance to start
        // and settle) is what actually avoids the stall; queuing it as just
        // another same-instant startup task does not. Dispatched onto
        // exportThreadPool() (not this timer's UI thread) because it blocks on
        // a subprocess for up to 60s.
        QTimer::singleShot(5000, this, [] {
            exportThreadPool().start([] { ExportManager::warmExportBrowser(); });
        });
    }
    if (m_startupMode == StartupMode::Normal && m_restoreSessionOnStartup) {
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([this]() {
                restorePreviousSession();
                syncWorkspaceHeaderHeight();
            }, 0);
        });
    } else if (m_startupMode != StartupMode::TransferTarget) {
        openInitialTab();
        FASTMD_MARK("  ctor: first tab created");
        m_workspacePanelCtrl->updateRoot();
        syncCurrentTabUi();
        syncWorkspaceHeaderHeight();
        FASTMD_MARK("  ctor: first tab UI synced");
        completeStartup();
    } else {
        completeStartup();
    }
    connect(this, &MainWindow::firstPainted, this, [this]() {
        if (m_activePreview)
            enqueueStartupTask([this]() {
                // Re-check: the tab owning this preview can be dragged to
                // another window before the queued task runs.
                if (m_activePreview)
                    m_activePreview->ensureInitialized();
            }, 1);
    });
    // Restoring an open terminal must stay off the construction/first-paint
    // path. The panel, WebView2 child, and ConPTY sessions all remain lazy,
    // and restoreTerminalPanel() neither focuses the terminal nor activates a
    // detached window -- the window is interactive by the time this runs, so
    // either would take keystrokes the user has already started typing.
    //
    // Bucket 3 puts it behind session restore (restoreNextSessionTab(), also
    // bucket 3): documents come back before terminals do. The StartupMode
    // fence is the same one inspectRecoveryFiles() uses below -- without it
    // File > New Window and a detached tab drag would each build their own
    // panel, WebView2 renderer, and set of shells.
    if (m_terminalVisible && m_startupMode == StartupMode::Normal) {
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([this]() {
                FASTMD_MARK("  task: terminal restore begin");
                restoreTerminalPanel();
                FASTMD_MARK("  task: terminal restore dispatched");
            }, 3);
        });
    }
    // Recovery inspection belongs to the primary window only: auxiliary windows
    // (File > New Window, and the target of a detached tab drag) would re-scan
    // the same snapshots and prompt a second time.
    if (m_startupMode == StartupMode::Normal) {
        connect(this, &MainWindow::firstPainted, this, [this]() {
            enqueueStartupTask([this]() {
                inspectRecoveryFiles();
                FASTMD_MARK("  task: recovery inspection dispatched");
            }, 3);
            // Maintenance, not persistence: once per session, not once per
            // writeSettings() (see pruneWordWrapSettings()).
            enqueueStartupTask([this]() {
                pruneWordWrapSettings();
                FASTMD_MARK("  task: word-wrap maintenance dispatched");
            }, 3);
            // Bucket 3 so both run after session restore/openInitialTab
            // (bucket 0) and after any command-line/shell file open
            // (bucket 2, see main.cpp) -- an explicitly requested document
            // is already open and active by the time either of these decides
            // what to do with the startup placeholder. Cleanup runs first:
            // it must run whether or not What's New has anything to show
            // (see discardStrayStartupPlaceholder()), and it is a no-op once
            // maybeShowWhatsNew() has already consumed the placeholder.
            enqueueStartupTask([this]() { discardStrayStartupPlaceholder(); }, 3);
            enqueueStartupTask([this]() { maybeShowWhatsNew(); }, 3);
        });
    }
    if (m_startupMode != StartupMode::TransferTarget) {
        enqueueStartupTask([] { (void)syntaxRepository(); }, 4);
    }
}

MainWindow::~MainWindow()
{
    m_chemistryToolbar.reset();
    m_mathToolbar.reset();
}

void MainWindow::enqueueStartupTask(std::function<void()> task, int priority)
{
    if (!task)
        return;
    const int index = qBound(0, priority, 4);
    m_startupTasks[index].append(std::move(task));
    if (m_firstPainted && !m_startupTaskScheduled) {
        m_startupTaskScheduled = true;
        QTimer::singleShot(0, this, &MainWindow::runNextStartupTask);
    }
}

void MainWindow::runNextStartupTask()
{
    if (m_isResetting)
        return;
    m_startupTaskScheduled = false;
    for (auto& tasks : m_startupTasks) {
        if (tasks.isEmpty())
            continue;
        std::function<void()> task = tasks.takeFirst();
        task();
        if (!m_startupTaskScheduled) {
            m_startupTaskScheduled = true;
            QTimer::singleShot(0, this, &MainWindow::runNextStartupTask);
        }
        return;
    }
}

MainWindow* MainWindow::createAuxiliaryWindow(StartupMode mode, const QRect& geometry)
{
    auto* window = new MainWindow(nullptr, mode, StartupSettings::load());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowIcon(qApp->windowIcon());
    if (geometry.isValid())
        window->setGeometry(geometry);
    window->show();
    return window;
}

QString MainWindow::documentPathKey(const QString& path)
{
    return DocumentModel::pathKeyFor(path);
}

MainWindow::OpenDocumentRef MainWindow::findOpenDocument(const QString& path, const TabPage* exclude)
{
    const QString key = documentPathKey(path);
    if (key.isEmpty())
        return {};
    DocumentModel* const model = DocumentModel::openDocumentForKey(key);
    if (!model || (exclude && model == exclude->model))
        return {};
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (page && page->model == model)
                return { window, page, i };
        }
    }
    return {};
}

MainWindow::OpenDocumentRef MainWindow::findOpenBundledDocument(const QString& kind)
{
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (page && page->model && page->model->isBundled() && page->model->bundledKind() == kind)
                return { window, page, i };
        }
    }
    return {};
}

void MainWindow::activateOpenDocument(const OpenDocumentRef& document)
{
    if (!document.window || !document.page || document.index < 0)
        return;
    document.window->m_tabs->setCurrentIndex(document.index);
    document.window->m_htmlDirty = true;
    document.window->m_previewDirty = true;
    document.window->syncCurrentTabUi();
    document.window->m_workspacePanelCtrl->updateRoot();
    if (document.window->isMinimized()) {
        if (document.window->windowState() & Qt::WindowMaximized)
            document.window->showMaximized();
        else
            document.window->showNormal();
    }
    document.window->raise();
    document.window->activateWindow();
    // A hidden QPlainTextEdit (Reading Mode, or a viewer tab) cannot hold native
    // focus, so handing it over would leave the window with nothing focused.
    if (document.page->editor->isVisible())
        document.page->editor->restoreKeyboardFocus();
    else
        document.window->focusReadingModePreview();
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);
    if (!m_firstPainted) {
        m_firstPainted = true;
        emit firstPainted();
        if (!m_startupTaskScheduled && std::any_of(std::begin(m_startupTasks), std::end(m_startupTasks),
                                                    [](const auto& tasks) { return !tasks.isEmpty(); })) {
            m_startupTaskScheduled = true;
            QTimer::singleShot(0, this, &MainWindow::runNextStartupTask);
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::createWorkspacePanel()
{
    // The cards (Workspace, Editor, Preview, Terminal) sit on the window's
    // canvas: this splitter's margins are the canvas around them and its
    // transparent gap handle is the canvas between Workspace and Editor.
    auto* mainSplitter = new ThemedSplitter(Qt::Horizontal, ThemedSplitter::HandleStyle::Gap,
                                            ThemeCard::Gap, this);
    mainSplitter->setHandleHitSlop(ThemeCard::HitSlop);
    m_mainSplitter = mainSplitter;
    m_mainSplitter->setContentsMargins(ThemeCard::Gap, 2, ThemeCard::Gap, 0);
    m_mainSplitter->setChildrenCollapsible(false);

    m_workspacePanelCtrl = new WorkspacePanelController(
        m_mainSplitter,
        [this]() -> QString {
            TabPage* pg = m_tabs->currentPage();
            return pg ? pg->model->filePath() : QString();
        },
        [this]() -> bool {
            bool dark = false;
            if (m_theme == QStringLiteral("dark")) {
                dark = true;
            } else if (m_theme == QStringLiteral("system")) {
                dark = (QApplication::palette().color(QPalette::Window).lightness() < 128);
            }
            return dark;
        },
        this);
    // Read-only: the selection is copied out of the editor, never edited or cleared.
    m_workspacePanelCtrl->setSearchSeedProvider([this]() -> QString {
        if (!m_activeEditor)
            return {};
        return TextTransforms::singleLineSearchSeed(m_activeEditor->textCursor().selectedText());
    });
    connect(m_workspacePanelCtrl, &WorkspacePanelController::fileActivated, this,
            [this](const QString& path) {
        openFilePath(path);
    });
    connect(m_workspacePanelCtrl, &WorkspacePanelController::externalViewerRequested, this,
            [](const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    connect(m_workspacePanelCtrl, &WorkspacePanelController::textEditorRequested, this,
            &MainWindow::openFileAsText);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::terminalRequested, this,
            &MainWindow::openTerminalAtWorkspace);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::fileRenamed, this, &MainWindow::onWorkspaceFileRenamed);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::pathsDeleted, this, &MainWindow::onWorkspacePathsDeleted);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::visibilityChanged, this,
            [this](bool visible) {
                // Notepad Mode hides the panel with persist=false, so this is
                // only the user's underlying Workspace preference.
                m_workspaceTreeVisible = visible;
                scheduleWriteSettings();
            });
    connect(m_workspacePanelCtrl, &WorkspacePanelController::showAllChanged, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::optionalColumnsChanged, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::columnLayoutChanged, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::sortChanged, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::outlineExpandedChanged, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::outlineHeadingActivated, this,
            [this](int line) {
        if (!m_activeEditor)
            return;
        QTextCursor cursor(m_activeEditor->document()->findBlockByNumber(line));
        m_activeEditor->setTextCursor(cursor);
        m_activeEditor->centerCursor();
        m_activeEditor->setFocus();
    });
    connect(m_workspacePanelCtrl, &WorkspacePanelController::rootChanged, this, [this](const QString& newRoot) {
        m_workspacePanelCtrl->recentFoldersManager()->add(newRoot);
        syncCurrentTabUi();
        updateWindowTitle();
    });
    connect(m_workspacePanelCtrl->recentFoldersManager(), &RecentFoldersManager::changed, this, &MainWindow::scheduleWriteSettings);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::sourceControlPanelCreated,
            this, &MainWindow::connectSourceControlPanel);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::searchPanelCreated,
            this, &MainWindow::connectSearchPanel);

    m_tabs = new TabWidget(m_mainSplitter);

    // The Terminal panel lives below the editor only -- not below the Workspace
    // panel -- so it hangs off a vertical splitter nested in the main one, the
    // way VS Code keeps a full-height sidebar beside a bottom panel.
    //
    // Built eagerly even though the terminal itself is lazy: a bare QSplitter
    // with one child costs nothing, whereas introducing it later would mean
    // reparenting m_tabs, and reparenting a PreviewWidget destroys and recreates
    // its WebView2 (see PreviewWidget::prepareForReparent).
    auto* editorTerminalSplitter = new ThemedSplitter(Qt::Vertical, ThemedSplitter::HandleStyle::Gap,
                                                      ThemeCard::Gap, m_mainSplitter);
    editorTerminalSplitter->setHandleHitSlop(ThemeCard::HitSlop);
    m_editorTerminalSplitter = editorTerminalSplitter;
    m_editorTerminalSplitter->setObjectName(QStringLiteral("editorTerminalSplitter"));
    m_editorTerminalSplitter->setChildrenCollapsible(false);
    m_editorTerminalSplitter->addWidget(m_tabs);
    m_editorTerminalSplitter->setStretchFactor(0, 1);
    m_workspacePanelCtrl->setOutlineReferenceWidget(m_editorTerminalSplitter);

    m_mainSplitter->addWidget(m_workspacePanelCtrl->panelWidget());
    m_mainSplitter->addWidget(m_editorTerminalSplitter);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({ 380, 800 });

    if (m_mainSplitter->count() > 1 && m_mainSplitter->handle(1)) {
        m_mainSplitter->handle(1)->installEventFilter(this);
    }
}

void MainWindow::ensureOutlinePopup()
{
    if (m_outlinePopup)
        return;

    m_outlinePopup = new OutlinePopup(this);
    connect(m_outlinePopup, &OutlinePopup::navigationRequested,
            this, &MainWindow::recordNavigationDeparture);
    connect(m_outlinePopup, &OutlinePopup::popupHidden, this, [this]() {
        if (TabPage* page = m_tabs->currentPage()) {
            if (page->preview)
                page->preview->setOutlinePopupOpen(false);
        }
    });
}

QString MainWindow::currentWorkspaceFolder() const
{
    if (m_workspacePanelCtrl) {
        const QFileInfo workspace(m_workspacePanelCtrl->rootPath());
        if (workspace.isDir())
            return workspace.absoluteFilePath();
    }

    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const QFileInfo file(page && page->model ? page->model->filePath() : QString());
    return file.isFile() ? file.absolutePath() : QString();
}

void MainWindow::copyCurrentFilePath()
{
    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const QString path = page && page->model ? page->model->filePath() : QString();
    if (path.isEmpty()) {
        themedInformation(this, tr("Copy Path"), tr("Save the current file before copying its path."));
        return;
    }
    QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void MainWindow::revealCurrentFileInExplorer()
{
    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const QString path = page && page->model ? page->model->filePath() : QString();
    if (!QFileInfo(path).isFile()) {
        themedInformation(this, tr("Reveal in Explorer"),
                          tr("Save the current file before revealing it in Explorer."));
        return;
    }
    WorkspaceFileOps::revealFileInExplorer(path);
}

void MainWindow::openCurrentFolderInTerminal()
{
    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const QString path = page && page->model ? page->model->filePath() : QString();
    if (!QFileInfo(path).isFile()) {
        themedInformation(this, tr("Open Folder in Terminal"),
                          tr("Save the current file before opening its folder in Terminal."));
        return;
    }
    openTerminalAtFile(path);
}

void MainWindow::openCurrentFolderInExternalTerminal()
{
    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const QString path = page && page->model ? page->model->filePath() : QString();
    const QFileInfo file(path);
    if (!file.isFile()) {
        themedInformation(this, tr("Open Active File Folder in External Terminal"),
                          tr("Save the current file before opening its folder in Terminal."));
        return;
    }
    if (!WorkspaceFileOps::openFolderInTerminal(file.absolutePath())) {
        themedWarning(this, tr("Open Active File Folder in External Terminal"),
                      tr("Could not open a terminal in this folder."));
    }
}

namespace {

void showGitNotInstalled(MainWindow* window)
{
    QDialog dialog(window);
    dialog.setWindowTitle(QObject::tr("Git is not installed"));
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(12);
    auto* messageRow = new QHBoxLayout;
    messageRow->setSpacing(14);
    auto* icon = new QLabel(&dialog);
    icon->setPixmap(dialog.style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(40, 40));
    icon->setFixedSize(40, 40);
    messageRow->addWidget(icon, 0, Qt::AlignTop);
    auto* message = new QLabel(QObject::tr("Git for Windows is required to use this feature."), &dialog);
    message->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 500;"));
    messageRow->addWidget(message);
    layout->addLayout(messageRow);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
    auto* download = new QPushButton(QObject::tr("Download Git"), &dialog);
    download->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(download);
    layout->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(download, &QPushButton::clicked, &dialog, &QDialog::accept);

#ifdef Q_OS_WIN
    setWindowsTitleBarDarkMode(&dialog, dialog.palette().color(QPalette::Window).lightness() < 128);
#endif
    dialog.setFixedSize(dialog.sizeHint());
    if (dialog.exec() == QDialog::Accepted)
        MainWindow::openUrl(Urls::GitForWindowsDownload);
}

void showGitToolUnavailable(MainWindow* window, const QString& tool)
{
    themedWarning(window, QObject::tr("%1 could not be launched").arg(tool),
                  QObject::tr("Git for Windows is installed, but %1 could not be launched.").arg(tool));
}

} // namespace

void MainWindow::openGitChanges()
{
    const QString folder = currentWorkspaceFolder();
    if (folder.isEmpty()) {
        themedInformation(this, tr("Open Git Changes"),
                          tr("Open a folder or save the current file first."));
        return;
    }

    switch (GitTools::openChanges(folder)) {
    case GitTools::LaunchResult::Launched: return;
    case GitTools::LaunchResult::GitNotInstalled: showGitNotInstalled(this); return;
    case GitTools::LaunchResult::ToolUnavailable: showGitToolUnavailable(this, tr("Git GUI")); return;
    }
}

void MainWindow::viewGitHistory()
{
    const QString folder = currentWorkspaceFolder();
    if (folder.isEmpty()) {
        themedInformation(this, tr("View Git History"),
                          tr("Open a folder or save the current file first."));
        return;
    }

    switch (GitTools::openHistory(folder)) {
    case GitTools::LaunchResult::Launched: return;
    case GitTools::LaunchResult::GitNotInstalled: showGitNotInstalled(this); return;
    case GitTools::LaunchResult::ToolUnavailable: showGitToolUnavailable(this, tr("Gitk")); return;
    }
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void MainWindow::createTitleBar()
{
    m_titleBar = new WindowTitleBar(this);
    m_menuBar = m_titleBar->menuBar();
    // Occupies exactly the row QMainWindow gave the menu bar, so the toolbar
    // container below it neither moves nor changes size.
    setMenuWidget(m_titleBar);

    connect(m_titleBar, &WindowTitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(m_titleBar, &WindowTitleBar::maximizeRestoreRequested,
            this, &MainWindow::toggleMaximizedState);
    connect(m_titleBar, &WindowTitleBar::closeRequested, this, [this]() { close(); });
#ifdef Q_OS_WIN
    connect(m_titleBar, &WindowTitleBar::systemMenuRequested,
            this, &MainWindow::showWindowSystemMenu);
#endif
    m_titleBar->setMaximized(isMaximized());
    m_titleBar->setWindowActive(isActiveWindow());
}

// VS Code's title-bar layout controls. Each one mirrors an existing action, so
// menus, shortcuts and these buttons all drive (and show) the same state.
void MainWindow::createTitleBarLayoutToggles()
{
    if (!m_titleBar)
        return;
    m_titleBar->addLayoutToggle(m_actToggleWorkspace, QStringLiteral("layout-sidebar-left"),
                                QStringLiteral("layout-sidebar-left-off"), tr("Toggle Workspace Panel"),
                                CommandShortcutRegistry::defaultShortcut(CommandId::ViewWorkspacePanel));
    m_titleBar->addLayoutToggle(m_actTogglePreview, QStringLiteral("layout-sidebar-right"),
                                QStringLiteral("layout-sidebar-right-off"), tr("Toggle Preview"));
    m_titleBar->addLayoutToggle(m_actNotepadMode, QStringLiteral("layout-centered"),
                                QStringLiteral("layout-centered-off"), tr("Toggle Notepad Mode"));
}

void MainWindow::syncTitleBarIconAlignment()
{
    if (!m_titleBar || !m_toolbar || !m_actToolbarNew)
        return;
    auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(m_actToolbarNew));
    if (!button)
        return;

    // The toolbar's own layout has to have run for the button to have a
    // position; it has not at the point createToolbar() finishes, and the
    // application style sheet can still change the toolbar's insets on a later
    // polish, which is why this is re-run from applyTheme() as well.
    if (auto* layout = m_toolbar->layout())
        layout->activate();
    if (button->width() <= 0)
        return;

    // Icon-only tool buttons center their icon in the button, so the icon box
    // starts here -- but a Material Icons glyph does not fill that box. Its
    // side bearing has to come off too, or the two pictures still read as
    // misaligned even though their boxes agree.
    const QSize iconSize = m_toolbar->iconSize();
    const QPixmap icon = button->icon().pixmap(iconSize, button->devicePixelRatioF());
    if (icon.isNull())
        return;  // Icons are coloured in by updateToolbarIcons(); it re-runs this.
    const int inkLeftInButton = (button->width() - iconSize.width()) / 2
                              + WindowTitleBar::opaqueLeftInset(icon);
    const QPoint inkLeftInWindow = button->mapTo(this, QPoint(inkLeftInButton, 0));
    m_titleBar->setAppIconInkLeft(m_titleBar->mapFrom(this, inkLeftInWindow).x());
}

void MainWindow::toggleMaximizedState()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (!m_titleBar)
        return;
    // Minimizing also clears Qt's maximized bit, so only a restored/maximized
    // window may rewrite the glyph -- otherwise minimizing a maximized window
    // would leave it showing "Maximize" when it comes back maximized.
    if (event->type() == QEvent::WindowStateChange && !isMinimized())
        m_titleBar->setMaximized(isMaximized());
    else if (event->type() == QEvent::ActivationChange)
        m_titleBar->setWindowActive(isActiveWindow());
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return QMainWindow::nativeEvent(eventType, message, result);

    auto* msg = static_cast<MSG*>(message);
    switch (msg->message) {
    case WM_NCCALCSIZE:
        // Applied unconditionally, including before createTitleBar() has run:
        // the suppressed caption is a property of the window, not of whether
        // its replacement exists yet.
        if (msg->wParam) {
            NativeWindowFrame::applyBorderlessNcCalcSize(
                msg->hwnd, reinterpret_cast<void*>(msg->lParam));
            *result = 0;
            return true;
        }
        break;

    case WM_NCHITTEST:
        *result = titleBarHitTest(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        return true;

    case WM_SETCURSOR: {
        // The resize band along the top of the window is client space now, so
        // Qt would paint an arrow there. Answer with the resize cursor before
        // a drag has begun; everything else falls through to Qt. lParam's low
        // word is the hit-test result this window just returned, so no second
        // classification (and no GetWindowRect per mouse move) is needed.
        if (reinterpret_cast<HWND>(msg->wParam) != msg->hwnd)
            break;
        const wchar_t* cursorId = nullptr;
        switch (LOWORD(msg->lParam)) {
        case HTTOP:
        case HTBOTTOM:      cursorId = IDC_SIZENS; break;
        case HTLEFT:
        case HTRIGHT:       cursorId = IDC_SIZEWE; break;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT: cursorId = IDC_SIZENWSE; break;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:  cursorId = IDC_SIZENESW; break;
        default: break;
        }
        if (cursorId) {
            SetCursor(LoadCursor(nullptr, cursorId));
            *result = TRUE;
            return true;
        }
        break;
    }

    // Maximize uses HTMAXBUTTON for Snap Layout, and Close uses HTCLOSE so the
    // right resize band and fractional-DPI rounding cannot take its edge.
    // Neither receives Qt mouse events; drive their button states here.
    case WM_NCMOUSEMOVE:
        if (m_titleBar) {
            const bool onMaximize = (msg->wParam == HTMAXBUTTON);
            const bool onClose = (msg->wParam == HTCLOSE);
            m_titleBar->setMaximizeButtonHovered(onMaximize);
            m_titleBar->setCloseButtonHovered(onClose);
            if (!onMaximize)
                m_titleBar->setMaximizeButtonPressed(false);
            if (!onClose)
                m_titleBar->setCloseButtonPressed(false);
            if ((onMaximize || onClose) && !m_trackingNcMouseLeave) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT,
                                       msg->hwnd, 0 };
                TrackMouseEvent(&track);
                m_trackingNcMouseLeave = true;
            }
            if (onMaximize || onClose) {
                *result = 0;
                return true;
            }
        }
        break;

    case WM_NCMOUSELEAVE:
        m_trackingNcMouseLeave = false;
        m_maximizeButtonPressed = false;
        m_closeButtonPressed = false;
        if (m_titleBar) {
            m_titleBar->setMaximizeButtonHovered(false);
            m_titleBar->setMaximizeButtonPressed(false);
            m_titleBar->setCloseButtonHovered(false);
            m_titleBar->setCloseButtonPressed(false);
        }
        break;

    case WM_NCLBUTTONDOWN:
        if (msg->wParam == HTCLOSE && m_titleBar) {
            m_closeButtonPressed = true;
            m_titleBar->setCloseButtonPressed(true);
            *result = 0;
            return true;
        }
        if (msg->wParam == HTMAXBUTTON && m_titleBar) {
            // Swallowed so DefWindowProc does not treat the press as the start
            // of a caption drag; the toggle happens on release, like a button.
            m_maximizeButtonPressed = true;
            m_titleBar->setMaximizeButtonPressed(true);
            *result = 0;
            return true;
        }
        break;

    case WM_NCRBUTTONUP:
        // DefWindowProc answers a right click on HTCAPTION by posting
        // WM_CONTEXTMENU, and *that* is what opens the system menu -- but Qt's
        // Windows plugin consumes WM_CONTEXTMENU to synthesise a
        // QContextMenuEvent, so the menu never appeared. Open it here instead,
        // from the message DefWindowProc would have acted on.
        if (msg->wParam == HTCAPTION) {
            showSystemMenuAtScreenPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            *result = 0;
            return true;
        }
        break;

    case WM_NCLBUTTONUP: {
        // Released anywhere: a press that wandered off the button must not
        // leave it stuck looking pressed.
        const bool completesButtonPress = m_maximizeButtonPressed;
        const bool completesClosePress = m_closeButtonPressed;
        m_maximizeButtonPressed = false;
        m_closeButtonPressed = false;
        if (m_titleBar) {
            m_titleBar->setMaximizeButtonPressed(false);
            m_titleBar->setCloseButtonPressed(false);
        }
        // Only a release that completes a press *on the button* toggles. A
        // caption double-click maximizes through DefWindowProc and its own
        // button-up arrives afterwards; if the cursor then happens to sit over
        // the maximize button in its new position, toggling here would restore
        // the window again and the double-click would look like it did nothing.
        if (completesButtonPress && msg->wParam == HTMAXBUTTON && m_titleBar) {
            toggleMaximizedState();
            *result = 0;
            return true;
        }
        if (completesClosePress && msg->wParam == HTCLOSE && m_titleBar) {
            *result = 0;
            close();
            return true;
        }
        break;
    }

    default:
        break;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

qintptr MainWindow::titleBarHitTest(int screenX, int screenY)
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    NativeWindowFrame::ResizeEdge edge = NativeWindowFrame::ResizeEdge::None;
    if (!IsZoomed(hwnd)) {
        edge = NativeWindowFrame::hitTestResizeBorder(
            QRect(windowRect.left, windowRect.top,
                  windowRect.right - windowRect.left, windowRect.bottom - windowRect.top),
            NativeWindowFrame::frameThicknessX(hwnd),
            NativeWindowFrame::frameThicknessY(hwnd),
            QPoint(screenX, screenY));
    }

    bool inTitleBar = false;
    TitleBarLayout::Zone zone = TitleBarLayout::Zone::Client;
    if (m_titleBar && m_titleBar->isVisible()) {
        POINT clientPt{ screenX, screenY };
        ScreenToClient(hwnd, &clientPt);
        const qreal dpr = devicePixelRatioF();
        const QPoint windowLocal(qFloor(clientPt.x / dpr), qFloor(clientPt.y / dpr));
        const QPoint local = m_titleBar->mapFrom(this, windowLocal);
        if (screenX >= windowRect.left && screenX < windowRect.right
            && screenY >= windowRect.top && screenY < windowRect.bottom
            && local.x() >= 0) {
            // Include the right resize border and any final physical pixel
            // beyond the bar's rounded logical edge in the last button.
            const QPoint hitLocal(qMin(local.x(), m_titleBar->width() - 1),
                                  qMax(local.y(), 0));
            const TitleBarLayout::Zone hitZone = m_titleBar->zoneAt(hitLocal);
            const bool inBarRows = local.y() >= 0 && local.y() < m_titleBar->height();
            const bool inTopResizeBand = local.y() < 0
                && (edge == NativeWindowFrame::ResizeEdge::Top
                    || edge == NativeWindowFrame::ResizeEdge::TopRight);
            if (inBarRows || (inTopResizeBand && hitZone == TitleBarLayout::Zone::Close)) {
                inTitleBar = true;
                zone = hitZone;
            }
        }
    }

    // A restored window still needs its native top-right resize corner. Below
    // that corner, Close owns the right border; maximized windows have no
    // resize corner, so Close reaches the physical top-right pixel there.
    if (edge == NativeWindowFrame::ResizeEdge::TopRight)
        return HTTOPRIGHT;

    if (inTitleBar && zone == TitleBarLayout::Zone::Close)
        return HTCLOSE;

    // The other controls only overlap the top resize band.
    if (inTitleBar && zone != TitleBarLayout::Zone::Caption)
        edge = NativeWindowFrame::withoutTopEdge(edge);

    if (edge != NativeWindowFrame::ResizeEdge::None)
        return NativeWindowFrame::hitTestCodeForEdge(edge);

    if (inTitleBar) {
        switch (zone) {
        case TitleBarLayout::Zone::Caption:
            return HTCAPTION;
        case TitleBarLayout::Zone::Maximize:
            return HTMAXBUTTON;
        default:
            // Minimize and the app/menu controls stay ordinary client widgets.
            break;
        }
    }
    return HTCLIENT;
}

void MainWindow::showWindowSystemMenu(const QPoint& windowPos)
{
    // Converted against this window's own native client origin rather than
    // through mapToGlobal(): one rounding, in one coordinate space, which is
    // what keeps it correct on a secondary monitor at a fractional scale.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    POINT origin{ clientRect.left, clientRect.top };
    ClientToScreen(hwnd, &origin);
    const qreal dpr = devicePixelRatioF();
    showSystemMenuAtScreenPos(origin.x + qRound(windowPos.x() * dpr),
                              origin.y + qRound(windowPos.y() * dpr));
}

void MainWindow::showSystemMenuAtScreenPos(int screenX, int screenY)
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu)
        return;

    const bool maximized = IsZoomed(hwnd);
    auto setEnabled = [menu](UINT item, bool enabled) {
        EnableMenuItem(menu, item,
                       UINT(MF_BYCOMMAND | (enabled ? MF_ENABLED : (MF_DISABLED | MF_GRAYED))));
    };
    setEnabled(SC_RESTORE, maximized);
    setEnabled(SC_MOVE, !maximized);
    setEnabled(SC_SIZE, !maximized);
    setEnabled(SC_MINIMIZE, true);
    setEnabled(SC_MAXIMIZE, !maximized);
    setEnabled(SC_CLOSE, true);
    SetMenuDefaultItem(menu, UINT(-1), FALSE);

    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenX, screenY, 0, hwnd, nullptr);
    if (command)
        PostMessage(hwnd, WM_SYSCOMMAND, command, 0);
}
#endif

void MainWindow::createMenus()
{
    // ---- File ----
    auto* file = new AlignedMenu(tr("&File"), this);
    m_menuBar->addMenu(file);

    auto mkAct = [&](QMenu* m, const QString& text, QKeySequence sc, auto* recv, auto slot) -> QAction* {
        auto* a = new QAction(text, this);
        if (!sc.isEmpty()) a->setShortcut(sc);
        a->setToolTip(sc.isEmpty()
            ? text
            : text + QStringLiteral("  (") + sc.toString(QKeySequence::NativeText) + QStringLiteral(")"));
        connect(a, &QAction::triggered, recv, slot);
        m->addAction(a);
        return a;
    };

    auto* actNew   = mkAct(file, tr("&New"),        CommandShortcutRegistry::defaultShortcut(CommandId::FileNew), this, &MainWindow::newFile);
    // "New" already means "new tab in this window", so Ctrl+T is a second
    // shortcut on it rather than a duplicate command.
    actNew->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::FileNew));
    m_actNewTab = actNew;
    auto* actNewWindow = mkAct(file, tr("New &Window"), CommandShortcutRegistry::defaultShortcut(CommandId::FileNewWindow), this, &MainWindow::newWindow);
    auto* actOpen  = mkAct(file, tr("&Open..."),    CommandShortcutRegistry::defaultShortcut(CommandId::FileOpen), this, &MainWindow::openFile);
    
    m_recentMenu = new AlignedMenu(tr("&Open Recent"), this);
    file->addMenu(m_recentMenu);
    m_recentFilesMgr = new RecentFilesManager(m_recentMenu, this);
    connect(m_recentFilesMgr, &RecentFilesManager::fileOpenRequested, this,
            [this](const QString& path) { openFilePath(path); });
    connect(m_recentFilesMgr, &RecentFilesManager::changed, this, &MainWindow::scheduleWriteSettings);
    // mkAct(file, tr("Clear Recent Files"), {}, this, &MainWindow::clearRecentFiles);
    mkAct(file, tr("Open &Folder..."), CommandShortcutRegistry::defaultShortcut(CommandId::FileOpenFolder), this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Open Folder"), m_workspacePanelCtrl->rootPath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty())
            m_workspacePanelCtrl->setRoot(dir);
    });
    // Reuses the same RecentFoldersManager menu shown under the Workspace
    // panel's "Open Folder" toolbar button, so both stay in sync from one
    // source of truth instead of maintaining a second recent-folders list.
    m_workspacePanelCtrl->recentFoldersManager()->menu()->setTitle(tr("Open Recent &Folders"));
    file->addMenu(m_workspacePanelCtrl->recentFoldersManager()->menu());
    file->addSeparator();
    auto* actSave    = mkAct(file, tr("&Save"),         CommandShortcutRegistry::defaultShortcut(CommandId::FileSave), this, &MainWindow::saveFile);
    auto* actSaveAs  = mkAct(file, tr("Save &As…"),     CommandShortcutRegistry::defaultShortcut(CommandId::FileSaveAs), this, &MainWindow::saveFileAs);
    auto* actSaveAll = mkAct(file, tr("Save A&ll"),     CommandShortcutRegistry::defaultShortcut(CommandId::FileSaveAll), this, &MainWindow::saveAll);
    file->addSeparator();
    mkAct(file, tr("Close &Tab"), CommandShortcutRegistry::defaultShortcut(CommandId::FileCloseTab), this, [this] {
        if (m_tabs->count() > 0)
            onTabCloseRequested(m_tabs->currentIndex());
    });
    m_actReopenClosedTab = mkAct(file, tr("Reopen &Closed Tab"),
                                 CommandShortcutRegistry::defaultShortcut(CommandId::FileReopenClosedTab),
                                 this, &MainWindow::reopenClosedTab);
    m_actReopenClosedTab->setEnabled(false);
    file->addSeparator();
    mkAct(file, tr("Import as &Markdown…"), {}, this, &MainWindow::importAsMarkdown);
    m_actPreviewBrowser = mkAct(file, tr("Preview in Browser"),
                                CommandShortcutRegistry::defaultShortcut(CommandId::FilePreviewBrowser),
                                this, &MainWindow::doPreviewBrowser);
    m_actExportPdf  = mkAct(file, tr("Export &PDF…"),  CommandShortcutRegistry::defaultShortcut(CommandId::FileExportPdf), this, &MainWindow::doExportPdf);
    m_actExportDocx = mkAct(file, tr("Export &DOCX…"), CommandShortcutRegistry::defaultShortcut(CommandId::FileExportDocx), this, &MainWindow::doExportDocx);
    m_actExportHtml = mkAct(file, tr("Export &HTML…"), CommandShortcutRegistry::defaultShortcut(CommandId::FileExportHtml), this, &MainWindow::doExportHtml);

    auto* exportAsMenu = new AlignedMenu(tr("Export &As"), this);
    file->addMenu(exportAsMenu);
    mkAct(exportAsMenu, tr("&EPUB…"),               {}, this, &MainWindow::doExportEpub);
    mkAct(exportAsMenu, tr("Plain &Text…"),         {}, this, &MainWindow::doExportPlainText);
    mkAct(exportAsMenu, tr("&Rich Text (.rtf)…"),   {}, this, &MainWindow::doExportRtf);
    mkAct(exportAsMenu, tr("&OpenDocument (.odt)…"),{}, this, &MainWindow::doExportOdt);
    mkAct(exportAsMenu, tr("&LaTeX (.tex)…"),       {}, this, &MainWindow::doExportLatex);

    file->addSeparator();
    mkAct(file, tr("Page Set&up…"), {}, this, &MainWindow::editPdfPageSetup);
    mkAct(file, tr("Heade&r && Footer…"), {}, this, &MainWindow::editHeaderFooter);
    m_actPrint = mkAct(file, tr("&Print"), CommandShortcutRegistry::defaultShortcut(CommandId::FilePrint), this, &MainWindow::printDocument);
    file->addSeparator();
    mkAct(file, tr("Preferences..."), CommandShortcutRegistry::defaultShortcut(CommandId::FilePreferences), this, &MainWindow::openPreferences);
    file->addSeparator();
    mkAct(file, tr("E&xit"), CommandShortcutRegistry::defaultShortcut(CommandId::FileExit), qApp, &QApplication::closeAllWindows);

    Q_UNUSED(actNewWindow); Q_UNUSED(actNew); Q_UNUSED(actOpen); Q_UNUSED(actSave); Q_UNUSED(actSaveAll); Q_UNUSED(actSaveAs);

    // ---- Edit ----
    auto* edit = new AlignedMenu(tr("&Edit"), this);
    m_menuBar->addMenu(edit);
    // Placeholder actions wired to editor below
    auto* actUndo  = new QAction(tr("&Undo"),  this); actUndo->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditUndo));   edit->addAction(actUndo);
    auto* actRedo  = new QAction(tr("&Redo"),  this); actRedo->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditRedo));   edit->addAction(actRedo);
    edit->addSeparator();
    auto* actCut   = new QAction(tr("Cu&t"),   this); actCut->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditCut));     edit->addAction(actCut);
    auto* actCopy  = new QAction(tr("&Copy"),  this); actCopy->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditCopy));   edit->addAction(actCopy);
    auto* actPaste = new QAction(tr("&Paste"), this); actPaste->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditPaste)); edit->addAction(actPaste);
    auto* actDelete = new QAction(tr("&Delete"), this); actDelete->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditDelete)); edit->addAction(actDelete);
    auto* actSelectAll = new QAction(tr("Select &All"), this); actSelectAll->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::EditSelectAll)); edit->addAction(actSelectAll);
    edit->addSeparator();
    m_actBack = mkAct(edit, tr("Back"), CommandShortcutRegistry::defaultShortcut(CommandId::EditBack), this, &MainWindow::navigateBack);
    m_actForward = mkAct(edit, tr("Forward"), CommandShortcutRegistry::defaultShortcut(CommandId::EditForward), this, &MainWindow::navigateForward);
    m_actBack->setEnabled(false);
    m_actForward->setEnabled(false);
    edit->addSeparator();
    mkAct(edit, tr("&Find…"),    CommandShortcutRegistry::defaultShortcut(CommandId::EditFind),    this, [this] {
        QWidget *focus = QApplication::focusWidget();
        QWidget *workspace = m_workspacePanelCtrl ? m_workspacePanelCtrl->panelWidget() : nullptr;
        if (workspace && focus && (focus == workspace || workspace->isAncestorOf(focus))) {
            // The filename row is not on screen in Search mode; Find there means
            // the Search in Files query the user is already looking at.
            if (m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::Search)
                m_workspacePanelCtrl->openSearch(false, false);
            else
                m_workspacePanelCtrl->focusSearch();
            return;
        }
        openFind();
    });
    mkAct(edit, tr("&Replace…"), CommandShortcutRegistry::defaultShortcut(CommandId::EditReplace), this, &MainWindow::openReplace);
    mkAct(edit, tr("Find &Next"), CommandShortcutRegistry::defaultShortcut(CommandId::EditFindNext), this, &MainWindow::findNext);
    mkAct(edit, tr("Find Previous"), CommandShortcutRegistry::defaultShortcut(CommandId::EditFindPrevious), this, &MainWindow::findPrevious);
    mkAct(edit, tr("&Go To Line…"), CommandShortcutRegistry::defaultShortcut(CommandId::EditGoToLine), this, &MainWindow::openGoToLine);
    edit->addSeparator();
    mkAct(edit, tr("Find in Files…"), CommandShortcutRegistry::defaultShortcut(CommandId::EditFindInFiles), this,
          [this] { openWorkspaceSearch(false); });
    mkAct(edit, tr("Replace in Files…"), CommandShortcutRegistry::defaultShortcut(CommandId::EditReplaceInFiles), this,
          [this] { openWorkspaceSearch(true); });

    edit->addSeparator();

    // ---- Format ----
    // These are real actions (rather than toolbar-only proxies) so the menu,
    // command palette and toolbar all invoke exactly the same edit operation.
    auto* formatMenu = new AlignedMenu(tr("F&ormat"), this);
    formatMenu->setActionIconsVisible(false);
    m_menuBar->addMenu(formatMenu);
    m_actHighlight = mkAct(formatMenu, tr("Highlight"),
                           CommandShortcutRegistry::defaultShortcut(CommandId::FormatHighlight), this, [this] {
        if (m_activeEditor) m_activeEditor->fmtHighlight();
    });
    QAction* subscript = mkAct(formatMenu, tr("Subscript"),
                               CommandShortcutRegistry::defaultShortcut(CommandId::FormatSubscript), this, [this] {
        if (m_activeEditor) m_activeEditor->fmtSubscript();
    });
    QAction* superscript = mkAct(formatMenu, tr("Superscript"),
                                 CommandShortcutRegistry::defaultShortcut(CommandId::FormatSuperscript), this, [this] {
        if (m_activeEditor) m_activeEditor->fmtSuperscript();
    });
    formatMenu->addSeparator();
    m_actAlignLeft = mkAct(formatMenu, tr("Align Left"), {}, this, [this] {
        if (m_activeEditor) m_activeEditor->fmtAlignLeft();
    });
    m_actAlignCenter = mkAct(formatMenu, tr("Align Center"), {}, this, [this] {
        if (m_activeEditor) m_activeEditor->fmtAlignCenter();
    });
    m_actAlignRight = mkAct(formatMenu, tr("Align Right"), {}, this, [this] {
        if (m_activeEditor) m_activeEditor->fmtAlignRight();
    });
    m_actAlignJustify = mkAct(formatMenu, tr("Justify"), {}, this, [this] {
        if (m_activeEditor) m_activeEditor->fmtAlignJustify();
    });
    m_formatActions << m_actHighlight << subscript << superscript
                    << m_actAlignLeft << m_actAlignCenter << m_actAlignRight << m_actAlignJustify;
    formatMenu->addSeparator();
    auto* actUpdateListNumbers = formatMenu->addAction(tr("Update List Numbers"));
    auto* actUpdateSectionNumbers = formatMenu->addAction(tr("Update Section Numbers"));
    formatMenu->addSeparator();
    m_actFormatDocument = mkAct(formatMenu, tr("Auto Format Document"),
                                 CommandShortcutRegistry::defaultShortcut(CommandId::FormatAutoFormatDocument), this, [this] {
        if (!m_activeEditor)
            return;
        if (m_activeEditor->textCursor().hasSelection())
            m_activeEditor->formatSelection();
        else
            m_activeEditor->formatDocument();
    });

    // Keep insertion commands in their own top-level menu.  The actions stay
    // shared with the toolbar and command palette, so their shortcuts and
    // command identities remain unchanged.
    auto* insertMenu = new AlignedMenu(tr("&Insert"), this);
    m_menuBar->addMenu(insertMenu);
    auto* dateTimeMenu = new AlignedMenu(tr("Date && Time"), this);
    // Preserve the established command-palette category/IDs while grouping
    // the shorter visible labels under this Insert submenu.
    dateTimeMenu->setProperty("commandPaletteCategory", QStringLiteral("Insert"));
    // No default shortcut: F5/Shift+F5 became the canonical, context-aware
    // Refresh shortcut (View > Refresh) -- see m_actRefresh below. The feature
    // itself stays fully available from this menu and the Command Palette.
    auto* actDateShort = mkAct(dateTimeMenu, tr("Short Format"), {}, this, [this] {
        if (m_activeEditor)
            m_activeEditor->insertPlainText(QLocale::system().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
    });
    actDateShort->setProperty("commandPaletteTitle", tr("Date & Time (Short Format)"));
    auto* actDateLong = mkAct(dateTimeMenu, tr("Long Format"), {}, this, [this] {
        if (m_activeEditor)
            m_activeEditor->insertPlainText(QLocale::system().toString(QDateTime::currentDateTime(), QLocale::LongFormat));
    });
    actDateLong->setProperty("commandPaletteTitle", tr("Date & Time (Long Format)"));
    auto* actDateCustom = mkAct(dateTimeMenu, tr("Custom..."), {}, this, [this] {
        if (!m_activeEditor)
            return;
        bool accepted = false;
        const QString format = QInputDialog::getText(
            this, tr("Custom Date & Time"), tr("Format:"), QLineEdit::Normal,
            QStringLiteral("yyyy-MM-dd HH:mm:ss"), &accepted);
        if (accepted && !format.isEmpty())
            m_activeEditor->insertPlainText(QDateTime::currentDateTime().toString(format));
    });
    actDateCustom->setProperty("commandPaletteTitle", tr("Date & Time (Custom...)"));
    m_actInsertInlineCode = mkAct(insertMenu, tr("Inline Code"), {}, this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtCode();
    });
    m_actInsertInlineCode->setProperty("commandPaletteTitle", tr("Insert Inline Code"));
    insertMenu->removeAction(m_actInsertInlineCode);
    m_actInsertCodeBlock = mkAct(insertMenu, tr("Code Block"),
                                  CommandShortcutRegistry::defaultShortcut(CommandId::InsertCodeBlock), this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtCodeBlock();
    });
    m_actInsertCodeBlock->setToolTip(tr("Code Block (Ctrl+`)"));
    m_actInsertCodeBlock->setProperty("commandPaletteTitle", tr("Insert Code Block"));
    insertMenu->removeAction(m_actInsertCodeBlock);
    auto* linksMenu = new AlignedMenu(tr("Links"), this);
    linksMenu->setProperty("commandPaletteCategory", QStringLiteral("Insert"));
    m_actInsertInlineLink = mkAct(linksMenu, tr("Insert Inline Link..."),
                                  CommandShortcutRegistry::defaultShortcut(CommandId::InsertInlineLink), this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtLink();
    });
    m_actInsertInlineLink->setProperty("commandPaletteTitle", tr("Insert Inline Link"));
    auto* actInsertReferenceLink = mkAct(linksMenu, tr("Insert Reference Link..."),
                                         CommandShortcutRegistry::defaultShortcut(CommandId::InsertReferenceLink), this, [this] {
        if (!m_activeEditor)
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Insert Reference Link"));
        dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();
        auto* label = new QLineEdit(&dialog);
        auto* url = new QLineEdit(&dialog);
        auto* title = new QLineEdit(&dialog);
        label->setObjectName(QStringLiteral("referenceLinkInput"));
        url->setObjectName(QStringLiteral("referenceLinkInput"));
        title->setObjectName(QStringLiteral("referenceLinkInput"));
        label->setText(QStringLiteral("[") + m_activeEditor->nextNumericReferenceLabel() + QStringLiteral("]"));
        url->setMinimumWidth(300);
        form->addRow(tr("Reference label:"), label);
        form->addRow(tr("URL:"), url);
        form->addRow(tr("Optional title:"), title);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Insert"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, label, url] {
            QString numericLabel = label->text().trimmed();
            if (numericLabel.startsWith(QLatin1Char('[')) && numericLabel.endsWith(QLatin1Char(']')))
                numericLabel = numericLabel.mid(1, numericLabel.size() - 2).trimmed();
            bool validNumber = false;
            if (numericLabel.toLongLong(&validNumber) <= 0 || !validNumber) {
                label->setFocus();
                return;
            }
            if (url->text().trimmed().isEmpty()) {
                url->setFocus();
                return;
            }
            dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.setFixedSize(dialog.sizeHint());
        url->setFocus();
        url->setCursorPosition(0);
        if (dialog.exec() == QDialog::Accepted)
            m_activeEditor->insertNumberedReferenceLink(label->text(), url->text(), title->text());
    });
    actInsertReferenceLink->setProperty("commandPaletteTitle", tr("Insert Numbered Reference Link"));
    auto* footnotesMenu = new AlignedMenu(tr("Footnotes"), this);
    // Retain the former top-level Insert palette category and command IDs.
    footnotesMenu->setProperty("commandPaletteCategory", QStringLiteral("Insert"));
    auto* actFootnote = mkAct(footnotesMenu, tr("Footnote"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertFootnote), this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtFootnote();
    });
    auto* actNamedFootnote = mkAct(footnotesMenu, tr("Named Footnote..."), CommandShortcutRegistry::defaultShortcut(CommandId::InsertNamedFootnote), this, [this] {
        if (!m_activeEditor)
            return;
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, tr("Named Footnote"), tr("Footnote name:"), QLineEdit::Normal,
            QString(), &accepted);
        if (accepted && !name.trimmed().isEmpty())
            m_activeEditor->fmtNamedFootnote(name);
    });
    auto* calloutMenu = new AlignedMenu(tr("Callouts"), this);
    // Keep the established command-palette category/IDs after shortening the
    // visible Insert-menu label.
    calloutMenu->setProperty("commandPaletteCategory", QStringLiteral("Insert Callouts"));
    const auto addCalloutAction = [this, &mkAct, calloutMenu](const QString& label,
                                                               const QString& type) {
        return mkAct(calloutMenu, label, {}, this, [this, type] {
            if (m_activeEditor)
                m_activeEditor->insertCallout(type);
        });
    };
    auto* actCalloutNote = addCalloutAction(tr("Note"), QStringLiteral("NOTE"));
    auto* actCalloutTip = addCalloutAction(tr("Tip"), QStringLiteral("TIP"));
    auto* actCalloutImportant = addCalloutAction(tr("Important"), QStringLiteral("IMPORTANT"));
    auto* actCalloutWarning = addCalloutAction(tr("Warning"), QStringLiteral("WARNING"));
    auto* actCalloutCaution = addCalloutAction(tr("Caution"), QStringLiteral("CAUTION"));
    auto* actHorizontalRule = m_actHorizontalRule = mkAct(insertMenu, tr("Horizontal Rule"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHorizontalRule), this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtHorizontalRule();
    });
    insertMenu->removeAction(actHorizontalRule);
    auto* actPageBreak = m_actPageBreak = mkAct(insertMenu, tr("Page Break"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertPageBreak), this, [this] {
        if (m_activeEditor)
            m_activeEditor->fmtPageBreak();
    });
    insertMenu->removeAction(actPageBreak);
    // Qt reports the main Enter key as Key_Return and the numeric keypad
    // Enter key as Key_Enter. Keep both shortcuts on this single action so
    // they trigger the same page-break insertion path.
    actPageBreak->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::InsertPageBreak));
    m_formatActions << m_actInsertInlineCode << m_actInsertCodeBlock << m_actInsertInlineLink
                    << actInsertReferenceLink
                    << actFootnote << actNamedFootnote << actCalloutNote << actCalloutTip
                    << actCalloutImportant << actCalloutWarning << actCalloutCaution
                    << actHorizontalRule << actPageBreak;
    auto* insertTocMenu = new AlignedMenu(tr("Table of Content"), this);
    insertTocMenu->setProperty("commandPaletteCategory", QStringLiteral("Insert Table of Content"));
    insertMenu->addAction(actHorizontalRule);
    insertMenu->addAction(actPageBreak);
    insertMenu->addSeparator();
    insertMenu->addAction(m_actInsertInlineCode);
    insertMenu->addAction(m_actInsertCodeBlock);
    insertMenu->addSeparator();
    insertMenu->addMenu(linksMenu);
    insertMenu->addMenu(footnotesMenu);
    insertMenu->addMenu(calloutMenu);
    insertMenu->addMenu(dateTimeMenu);
    insertMenu->addMenu(insertTocMenu);
    auto* actInsertToc = mkAct(insertTocMenu, tr("&Static Markdown TOC"), {}, this, [this] {
        if (m_activeEditor)
            m_activeEditor->insertTableOfContents();
    });
    auto* actInsertDynamicToc = mkAct(insertTocMenu, tr("&Dynamic ([TOC])"), {}, this, [this] {
        if (m_activeEditor)
            m_activeEditor->insertDynamicTableOfContents();
    });
    // Separated from the Markdown insertion commands above: this one converts an
    // external document (AnyDoc) rather than emitting Markdown syntax.
    insertMenu->addSeparator();
    auto* actInsertContentFromFile = mkAct(insertMenu, tr("Content from File…"), {},
                                           this, &MainWindow::insertContentFromFile);
    auto* lineMenu = new AlignedMenu(tr("&Line"), this);
    edit->addMenu(lineMenu);
    mkAct(lineMenu, tr("&Duplicate Line"), CommandShortcutRegistry::defaultShortcut(CommandId::EditDuplicateLine), this, [this] {
        if (m_activeEditor) m_activeEditor->duplicateLine();
    });
    mkAct(lineMenu, tr("Delete L&ine"), CommandShortcutRegistry::defaultShortcut(CommandId::EditDeleteLine), this, [this] {
        if (m_activeEditor) m_activeEditor->deleteLine();
    });
    mkAct(lineMenu, tr("Move Line Up"), CommandShortcutRegistry::defaultShortcut(CommandId::EditMoveLineUp), this, [this] {
        if (m_activeEditor) m_activeEditor->moveLinesUp();
    });
    mkAct(lineMenu, tr("Move Line Down"), CommandShortcutRegistry::defaultShortcut(CommandId::EditMoveLineDown), this, [this] {
        if (m_activeEditor) m_activeEditor->moveLinesDown();
    });

    lineMenu->addSeparator();
    auto* actSortLines            = lineMenu->addAction(tr("Sort Lines"));
    auto* actReverseLines         = lineMenu->addAction(tr("Reverse Lines"));
    auto* actRemoveDuplicateLines = lineMenu->addAction(tr("Remove Duplicate Lines"));
    auto* actUniqueSortLines      = lineMenu->addAction(tr("Unique + Sort"));
    auto* actJoinLines            = lineMenu->addAction(tr("Join Lines"));
    auto* actSplitLines           = lineMenu->addAction(tr("Split Lines…"));

    // The same three actions drive the status-bar line-ending menu, so both
    // entry points share one checked state and one conversion path.
    auto* eolMenu = new AlignedMenu(tr("EOL Conversion"), this);
    edit->addMenu(eolMenu);
    auto* eolGroup = new QActionGroup(this);
    const auto mkEolAct = [this, eolMenu, eolGroup](const QString& text, const QString& lineEnding) {
        QAction* action = eolMenu->addAction(text);
        action->setCheckable(true);
        eolGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, lineEnding] { applyDocumentLineEnding(lineEnding); });
        return action;
    };
    m_actEolCrlf = mkEolAct(tr("Windows (CRLF)"), windowsLineEnding());
    m_actEolLf   = mkEolAct(tr("Unix (LF)"), unixLineEnding());
    m_actEolCr   = mkEolAct(tr("Mac (CR)"), classicMacLineEnding());
    m_actEolCrlf->setProperty("commandPaletteTitle", tr("Convert EOL to Windows (CRLF)"));
    m_actEolLf->setProperty("commandPaletteTitle", tr("Convert EOL to Unix (LF)"));
    m_actEolCr->setProperty("commandPaletteTitle", tr("Convert EOL to Mac (CR)"));

    // Spell Check. These are the same QAction objects the status-bar popup
    // shows, so there is one checked state and one code path per command.
    m_spellCheckMenu = new AlignedMenu(tr("Spell Chec&k"), this);
    edit->addMenu(m_spellCheckMenu);
    m_actSpellEnable = m_spellCheckMenu->addAction(tr("Enable Spell Check"));
    m_actSpellEnable->setCheckable(true);
    connect(m_actSpellEnable, &QAction::triggered, this, [this](bool checked) {
        applySpellCheckState(checked, m_spellCheckLanguage);
    });
    m_spellLanguageMenu = new AlignedMenu(tr("Language"), this);
    m_spellCheckMenu->addMenu(m_spellLanguageMenu);
    m_spellCheckMenu->addSeparator();
    m_actSpellFixAll = m_spellCheckMenu->addAction(tr("Fix All Misspellings..."));
    connect(m_actSpellFixAll, &QAction::triggered, this, &MainWindow::fixAllMisspellings);
    m_spellCheckMenu->addSeparator();
    m_actSpellSettings = m_spellCheckMenu->addAction(tr("Spell Check Settings..."));
    // The spell-check settings live on the Editor page, so this opens there.
    connect(m_actSpellSettings, &QAction::triggered, this, &MainWindow::openEditorPreferences);
    // The language list is read from Windows, which must not happen on the
    // startup path; it is filled in on the first reveal of either surface.
    connect(m_spellLanguageMenu, &QMenu::aboutToShow, this, [this]() {
        rebuildSpellLanguageMenu();
    });


    auto* convertCaseMenu = new AlignedMenu(tr("Convert &Case"), this);
    edit->addMenu(convertCaseMenu);
    auto* actCaseUpper    = convertCaseMenu->addAction(tr("UPPERCASE"));
    auto* actCaseLower    = convertCaseMenu->addAction(tr("lowercase"));
    auto* actCaseProper   = convertCaseMenu->addAction(tr("Proper Case"));
    auto* actCaseSentence = convertCaseMenu->addAction(tr("Sentence case"));

    auto* whitespaceMenu = new AlignedMenu(tr("&Whitespace"), this);
    edit->addMenu(whitespaceMenu);
    auto* actJoinWrappedLines = whitespaceMenu->addAction(tr("Join Wrapped Lines"));
    auto* actRemoveEmptyLines = whitespaceMenu->addAction(tr("Remove Empty Lines"));
    auto* actTrimLeading      = whitespaceMenu->addAction(tr("Trim Leading Spaces"));
    auto* actTrimTrailing     = whitespaceMenu->addAction(tr("Trim Trailing Spaces"));
    auto* actTrimBoth         = whitespaceMenu->addAction(tr("Trim Both"));
    auto* actNormalizeWhitespace = whitespaceMenu->addAction(tr("Normalize Whitespace"));
    auto* actTabsToSpaces        = whitespaceMenu->addAction(tr("Convert Tabs to Spaces"));
    auto* actSpacesToTabs        = whitespaceMenu->addAction(tr("Convert Spaces to Tabs"));
    auto* actSmartQuotes         = whitespaceMenu->addAction(tr("Convert Smart Quotes to Straight Quotes"));
    auto* actRemoveInvisible     = whitespaceMenu->addAction(tr("Remove Invisible Characters"));


    // wire undo/redo/cut/copy/paste to the active editor
    auto wireEdit = [this](QAction* a, auto fn) {
        connect(a, &QAction::triggered, this, [this, fn]() {
            if (m_activeEditor) (m_activeEditor->*fn)();
        });
    };
    wireEdit(actUndo,  &QPlainTextEdit::undo);
    wireEdit(actRedo,  &QPlainTextEdit::redo);
    wireEdit(actCut,   &MarkdownEditor::cut);
    connect(actDelete, &QAction::triggered, this, [this]() {
        if (!m_activeEditor)
            return;
        QTextCursor cursor = m_activeEditor->textCursor();
        cursor.removeSelectedText();
    });
    wireEdit(actSelectAll, &QPlainTextEdit::selectAll);
    auto workspaceHasFocus = [this]() {
        return m_workspacePanelCtrl && m_workspacePanelCtrl->hasWorkspaceFocus();
    };
    connect(actCopy, &QAction::triggered, this, [this, workspaceHasFocus]() {
        if (workspaceHasFocus())
            m_workspacePanelCtrl->copySelection();
        else if (m_activeEditor)
            m_activeEditor->copy();
    });
    connect(actPaste, &QAction::triggered, this, [this, workspaceHasFocus]() {
        if (workspaceHasFocus())
            m_workspacePanelCtrl->pasteSelection();
        else if (m_activeEditor)
            m_activeEditor->paste();
    });

    wireEdit(actCaseUpper,    &MarkdownEditor::convertCaseUpper);
    wireEdit(actCaseLower,    &MarkdownEditor::convertCaseLower);
    wireEdit(actCaseProper,   &MarkdownEditor::convertCaseProper);
    wireEdit(actCaseSentence, &MarkdownEditor::convertCaseSentence);
    wireEdit(actRemoveEmptyLines, &MarkdownEditor::removeEmptyLines);
    wireEdit(actTrimLeading,      &MarkdownEditor::trimLeadingSpaces);
    wireEdit(actTrimTrailing,     &MarkdownEditor::trimTrailingSpaces);
    wireEdit(actTrimBoth,         &MarkdownEditor::trimBothSpaces);
    wireEdit(actJoinWrappedLines, &MarkdownEditor::joinWrappedLines);
    wireEdit(actSortLines,            &MarkdownEditor::sortLines);
    wireEdit(actReverseLines,         &MarkdownEditor::reverseLines);
    wireEdit(actRemoveDuplicateLines, &MarkdownEditor::removeDuplicateLines);
    wireEdit(actUniqueSortLines,      &MarkdownEditor::uniqueSortLines);
    wireEdit(actJoinLines,            &MarkdownEditor::joinLines);
    wireEdit(actSplitLines,           &MarkdownEditor::splitLines);
    wireEdit(actNormalizeWhitespace,  &MarkdownEditor::normalizeWhitespace);
    wireEdit(actTabsToSpaces,         &MarkdownEditor::convertTabsToSpaces);
    wireEdit(actSpacesToTabs,         &MarkdownEditor::convertSpacesToTabs);
    wireEdit(actSmartQuotes,          &MarkdownEditor::convertSmartQuotesToStraight);
    wireEdit(actRemoveInvisible,      &MarkdownEditor::removeInvisibleCharacters);
    wireEdit(actUpdateListNumbers, &MarkdownEditor::updateListNumbers);
    wireEdit(actUpdateSectionNumbers, &MarkdownEditor::updateSectionNumbers);
    m_formatActions << actUpdateListNumbers << actUpdateSectionNumbers;

    connect(insertMenu, &QMenu::aboutToShow, this, [this, actDateShort, actDateLong, actDateCustom, actFootnote,
                                                     actNamedFootnote, actCalloutNote, actCalloutTip,
                                                     actCalloutImportant, actCalloutWarning, actCalloutCaution,
                                                     actHorizontalRule, actPageBreak,
                                                     actInsertToc, actInsertDynamicToc,
                                                     actInsertContentFromFile] {
        const bool hasEditor = m_activeEditor != nullptr;
        for (QAction* action : {actDateShort, actDateLong, actDateCustom})
            action->setEnabled(hasEditor);
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        const bool markdownMode = model && model->editorMode() == EditorMode::Markdown;
        for (QAction* action : {actFootnote, actNamedFootnote, actCalloutNote, actCalloutTip,
                                actCalloutImportant, actCalloutWarning, actCalloutCaution,
                                actHorizontalRule, actPageBreak})
            action->setEnabled(hasEditor && markdownMode);
        actInsertToc->setEnabled(hasEditor && markdownMode
            && MarkdownToc::hasHeadings(m_activeEditor->toPlainText()));
        actInsertDynamicToc->setEnabled(hasEditor && markdownMode);
        actInsertContentFromFile->setEnabled(hasEditor && model && !model->isViewer()
            && !m_activeEditor->isReadOnly());
    });

    connect(edit, &QMenu::aboutToShow, this, [this, convertCaseMenu, whitespaceMenu, lineMenu, eolMenu,
                                                actUpdateListNumbers, actUpdateSectionNumbers] {
        const bool hasEditor = m_activeEditor != nullptr;
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        convertCaseMenu->setEnabled(hasEditor);
        whitespaceMenu->setEnabled(hasEditor);
        lineMenu->setEnabled(hasEditor);
        eolMenu->setEnabled(model && !model->isViewer());
        const bool markdownMode = model && model->editorMode() == EditorMode::Markdown;
        actUpdateListNumbers->setEnabled(markdownMode);
        actUpdateSectionNumbers->setEnabled(markdownMode);
    });

    // ---- View ----
    auto* view = new AlignedMenu(tr("&View"), this);
    m_menuBar->addMenu(view);

    m_actWordWrap = view->addAction(tr("&Word Wrap"), this, &MainWindow::setWordWrap);
    m_actWordWrap->setCheckable(true);
    m_actWordWrap->setChecked(m_wordWrapEnabled);
    m_actWordWrap->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewWordWrap));

    m_actDocumentSummary = view->addAction(tr("Document &Statistics"), this, &MainWindow::showDocumentSummary);

    m_actMathToolbar = new QAction(tr("&Math Toolbar"), this);
    connect(m_actMathToolbar, &QAction::toggled, this, [this](bool visible) {
        m_mathToolbarVisible = visible;
        if (visible)
            ensureMathToolbar();
        if (m_mathToolbar)
            m_mathToolbar->containerWidget()->setVisible(visible);
        writeSettings();
    });
    m_actMathToolbar->setCheckable(true);
    m_actMathToolbar->setChecked(m_mathToolbarVisible);
    m_actChemistryToolbar = new QAction(tr("&Chemistry Toolbar"), this);
    connect(m_actChemistryToolbar, &QAction::toggled, this, [this](bool visible) {
        m_chemistryToolbarVisible = visible;
        if (visible)
            ensureChemistryToolbar();
        if (m_chemistryToolbar) m_chemistryToolbar->containerWidget()->setVisible(visible);
        writeSettings();
    });
    m_actChemistryToolbar->setCheckable(true);
    m_actChemistryToolbar->setChecked(m_chemistryToolbarVisible);

    m_toolbarContextMenu = new AlignedMenu(this);
    static_cast<AlignedMenu*>(m_toolbarContextMenu)->setShowAnimationStyle(AlignedMenu::ContextMenu);
    static_cast<AlignedMenu*>(m_toolbarContextMenu)->setShowAnimationDuration(80);
    m_toolbarContextMenu->addAction(m_actMathToolbar);
    m_toolbarContextMenu->addAction(m_actChemistryToolbar);

    auto* toolbarsMenu = new AlignedMenu(tr("&Toolbars"), this);
    view->addMenu(toolbarsMenu);
    toolbarsMenu->addAction(m_actMathToolbar);
    toolbarsMenu->addAction(m_actChemistryToolbar);

    view->addSeparator();

    m_actStatusBar = view->addAction(tr("Status &Bar"), this, [this](bool visible) {
        setStatusBarVisible(visible);
    });
    m_actStatusBar->setCheckable(true);
    m_actStatusBar->setChecked(m_statusBarVisible);

    // Canonical, context-aware Refresh. Workspace-tree focus routes to
    // refreshActiveContext(); Preview and Source Control each answer F5
    // themselves (WebView2 accelerator forwarding and a local
    // WidgetWithChildrenShortcut respectively) and take priority over this
    // window-wide action while they have focus, the same way they already
    // did over the old Insert > Date & Time F5 binding.
    m_actRefresh = view->addAction(tr("&Refresh"), this, &MainWindow::refreshActiveContext);
    m_actRefresh->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewRefresh));
    // Hidden from the View menu itself (no discoverable use for it there yet).
    // NOTE: this does NOT keep the shortcut alive, despite what this comment
    // used to claim -- Qt disables a hidden QAction and drops it from the
    // shortcut map, so the window-wide F5 has never actually fired. F5 still
    // works where it matters because the Preview (WebView2 accelerator
    // forwarding) and the Source Control panel (its own
    // Qt::WidgetWithChildrenShortcut) each answer it themselves.
    //
    // Reviving this one is not a matter of un-hiding it: with the panel
    // focused both bindings would match, and Qt dispatches an ambiguous
    // shortcut to nobody, which would silently kill the panel's Refresh. One
    // of the two bindings has to go. tests/MenuShortcutContextTest.cpp pins
    // both behaviors.
    m_actRefresh->setVisible(false);

    m_actTerminal = view->addAction(tr("&Terminal"), this, &MainWindow::toggleTerminal);
    m_actTerminal->setCheckable(true);
    m_actTerminal->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewTerminal));

    // Source Control and Workspace Panel share the same sidebar slot and are
    // mutually exclusive modes of it; syncSidebarModeActions() -- driven by
    // the sidebar's own visibilityChanged/panelModeChanged signals -- is the
    // single place that keeps both checkmarks honest, from F7/F8, these menu
    // entries, or the sidebar's own header-row mode toggle alike.
    m_actSourceControl = view->addAction(tr("&Source Control"), this, [this](bool checked) {
        if (!m_workspacePanelCtrl)
            return;
        if (checked) {
            showSourceControl();
        } else if (m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::SourceControl) {
            m_workspacePanelCtrl->setVisible(false);
        }
    });
    m_actSourceControl->setCheckable(true);
    m_actSourceControl->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewSourceControl));

    m_actWorkspacePanel = view->addAction(tr("&Workspace Panel"), this, [this](bool checked) {
        if (!m_workspacePanelCtrl)
            return;
        if (checked) {
            if (isNotepadModeActive())
                setNotepadMode(false);
            if (!m_workspacePanelCtrl->isVisible())
                m_workspacePanelCtrl->setVisible(true);
            m_workspacePanelCtrl->setPanelMode(WorkspacePanelController::PanelMode::Workspace);
        } else if (m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::Workspace) {
            m_workspacePanelCtrl->setVisible(false);
        }
    });
    m_actWorkspacePanel->setCheckable(true);
    m_actWorkspacePanel->setChecked(m_workspaceTreeVisible);
    m_actWorkspacePanel->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewWorkspacePanel));

    // The title bar's Workspace control. Unlike F8 it never switches the
    // panel's mode: it shows or hides the sidebar as it is, Source Control or
    // Search included. Window-owned and in no menu, like Focus File Search.
    m_actToggleWorkspace = new QAction(tr("Toggle Workspace Panel"), this);
    m_actToggleWorkspace->setCheckable(true);
    m_actToggleWorkspace->setChecked(m_workspaceTreeVisible);
    connect(m_actToggleWorkspace, &QAction::triggered, this, &MainWindow::toggleWorkspacePanel);
    // Deliberately not added to the View menu: this is an implementation-focused
    // command that should not clutter it. It must NOT be hidden with
    // QAction::setVisible(false), which is how this was done before -- Qt
    // disables a hidden action and drops its shortcut from the shortcut map, so
    // Shift+F1 silently stopped working (tests/MenuShortcutContextTest.cpp
    // pins both halves of that behavior). Owned by the window and added to it
    // instead, which is what gives the shortcut its Qt::WindowShortcut context;
    // MainWindowCommands.cpp registers it with the Command Palette explicitly,
    // since the palette harvests the menu bar and would no longer see it.
    m_actFocusFileSearch = new QAction(tr("Focus File Search"), this);
    connect(m_actFocusFileSearch, &QAction::triggered, this, [this] {
        if (!m_workspacePanelCtrl)
            return;
        if (isNotepadModeActive())
            setNotepadMode(false);
        m_workspacePanelCtrl->setPanelMode(WorkspacePanelController::PanelMode::Workspace);
        if (!m_workspacePanelCtrl->isVisible())
            m_workspacePanelCtrl->setVisible(true);
        m_workspacePanelCtrl->focusSearch();
    });
    m_actFocusFileSearch->setShortcut(
        CommandShortcutRegistry::defaultShortcut(CommandId::ViewFocusFileSearch));
    addAction(m_actFocusFileSearch);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::visibilityChanged,
            this, &MainWindow::syncSidebarModeActions);
    connect(m_workspacePanelCtrl, &WorkspacePanelController::panelModeChanged,
            this, &MainWindow::syncSidebarModeActions);
    syncSidebarModeActions();

    m_actTogglePreview = view->addAction(tr("&Preview Panel"), this, &MainWindow::togglePreview);
    m_actTogglePreview->setCheckable(true);
    m_actTogglePreview->setChecked(true);
    m_actTogglePreview->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewTogglePreview));

    m_actMindMap = view->addAction(tr("View as Mind Map"), this, &MainWindow::toggleMindMap);
    m_actMindMap->setCheckable(true);
    m_actMindMap->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewMindMap));

    m_actReadingMode = view->addAction(tr("&Reading Mode"), this, &MainWindow::toggleReadingMode);
    m_actReadingMode->setCheckable(true);
    m_actReadingMode->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewReadingMode));

    m_actNotepadMode = view->addAction(tr("&Notepad Mode"), this, &MainWindow::toggleNotepadMode);
    m_actNotepadMode->setCheckable(true);
    m_actNotepadMode->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewNotepadMode));

    m_actGoToPreview = new QAction(tr("Go to Preview"), this);
    m_actGoToPreview->setIcon(
        IconHelper::materialMenuIcon(QChar(0xE8F4), palette().color(QPalette::Text), 16));
    connect(m_actGoToPreview, &QAction::triggered, this, &MainWindow::goToPreview);


    // m_actOutline = view->addAction(tr("&Outline"), this, &MainWindow::showOutlinePopup);
    // m_actOutline->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));

    m_actCommandPalette = new QAction(tr("&Command Palette…"), this);
    connect(m_actCommandPalette, &QAction::triggered,
            this, &MainWindow::openCommandPalette);
    m_actCommandPalette->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewCommandPalette));
    addAction(m_actCommandPalette);

    view->addSeparator();
    QAction* actZoomIn = view->addAction(tr("Zoom In"), this, &MainWindow::zoomIn);
    actZoomIn->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::ViewZoomIn));
    QAction* actZoomOut = view->addAction(tr("Zoom Out"), this, &MainWindow::zoomOut);
    actZoomOut->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::ViewZoomOut));
    QAction* actResetZoom = view->addAction(tr("Reset Zoom"), this, &MainWindow::resetZoom);
    actResetZoom->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewResetZoom));

    view->addSeparator();
    auto* themeMenu = new AlignedMenu(tr("&Theme"), this);
    view->addMenu(themeMenu);
    m_themeGroup = new QActionGroup(this);

    auto addTheme = [&](const QString& label, const QString& key) {
        QAction* a = themeMenu->addAction(label, this, [this, key]{ setTheme(key); });
        a->setCheckable(true);
        a->setChecked(m_theme == key);
        m_themeGroup->addAction(a);
        return a;
    };
    addTheme(tr("Light"),  QStringLiteral("light"));
    addTheme(tr("Dark"),   QStringLiteral("dark"));
    addTheme(tr("System"), QStringLiteral("system"));

    // ---- Help ----
    auto* help = new AlignedMenu(tr("&Help"), this);
    m_menuBar->addMenu(help);
    m_actCheckUpdates = help->addAction(tr("Check for Updates..."), this, &MainWindow::checkForUpdates);
    m_actRateStore = help->addAction(tr("Rate on Microsoft Store"), this, []() {
        openUrl(Urls::RateStore);
    });
    m_actShortcuts = help->addAction(tr("Keyboard Shortcuts..."), this, [this] {
        auto* dlg = new ShortcutsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    m_actUserGuide = help->addAction(tr("User Guide"), this, &MainWindow::openUserGuide);
    help->addSeparator();

    m_actGithubRepo = help->addAction(tr("GitHub Repository"), this, []() {
        openUrl(Urls::GitHubRepo);
    });
    m_actReportIssue = help->addAction(tr("Report an Issue"), this, []() {
        openUrl(Urls::ReportIssue);
    });
    help->addSeparator();

    m_actLicenses = help->addAction(tr("Third-Party Licenses..."), this, [this] {
        LicensesDialog dlg(this);
        dlg.exec();
    });
    m_actReleaseNotes = help->addAction(tr("Release Notes..."), this, &MainWindow::openReleaseNotes);
    m_actAbout = help->addAction(tr("About FastMD"), this, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });
}

// ---------------------------------------------------------------------------
void MainWindow::createToolbar()
{
    auto* rootWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(rootWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(rootWidget);
    FASTMD_MARK("    toolbar: central widget installed");

    // The menu bar row is WindowTitleBar now (see createTitleBar()); its own
    // even 32px height keeps everything below it off a half device pixel under
    // fractional scaling, which is why that constant lives there and not here.

    auto* tbContainer = new QWidget(rootWidget);
    tbContainer->setObjectName(QStringLiteral("toolbarContainer"));
    tbContainer->setFixedHeight(44);
    auto* tbLayout = new QHBoxLayout(tbContainer);
    // The launcher's right edge lines up with the cards' right edge below it.
    tbLayout->setContentsMargins(0, 0, ThemeCard::Gap, 0);
    tbLayout->setSpacing(0);
    rootLayout->addWidget(tbContainer, 0);

    // Math/Chemistry toolbars are hidden by default and built lazily by
    // ensureMathToolbar()/ensureChemistryToolbar() (first enable, or here in
    // the constructor once readSettings() shows they were left visible) so a
    // normal launch never pays for their icon catalogs/raster variants.
    m_toolbarRootWidget = rootWidget;
    m_toolbarRootLayout = rootLayout;

    m_mainSplitter->setParent(rootWidget);
    rootLayout->addWidget(m_mainSplitter, 1);
    FASTMD_MARK("    toolbar: splitter reparented");

    m_toolbar = new QToolBar(tr("Main"), tbContainer);
    m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
    FASTMD_MARK("    toolbar: QToolBar constructed");
    m_toolbar->installEventFilter(this);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setContextMenuPolicy(Qt::DefaultContextMenu);
    m_toolbar->setIconSize(QSize(24, 24));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // The first button's hover box lines up with the cards' left edge; 4 + 36 +
    // 4 fills the 44px row.
    m_toolbar->setContentsMargins(ThemeCard::Gap, 4, 0, 4);
    FASTMD_MARK("    toolbar: QToolBar configured");
    
    m_overflowBtn = ToolbarOverflow::extensionButton(m_toolbar);
    ToolbarOverflow::installMenu(m_toolbar, m_overflowBtn);
    ToolbarOverflow::updateExtensionButton(m_overflowBtn, false, 24);
    FASTMD_MARK("    toolbar: overflow configured");

    tbLayout->addWidget(m_toolbar, 1);
    // Keeps the "More" overflow button (whichever item ends up rightmost in
    // the toolbar) from sitting flush against the command palette box.
    tbLayout->addSpacing(8);

    // The launcher never collapses into the toolbar's "More" overflow: the
    // toolbar takes all the stretch, so shrinking the window pushes icons into
    // the overflow while the palette box keeps its own (shrinking) slot.
    m_commandLauncher = new CommandPaletteLauncher(tbContainer);
    m_commandLauncher->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(m_commandLauncher, &CommandPaletteLauncher::activated,
            this, &MainWindow::openCommandPalette);
    tbLayout->addWidget(m_commandLauncher, 0);
    FASTMD_MARK("    toolbar: command launcher constructed");

    // Tooltip helper
    auto tip = [](const QString& t, QKeySequence sc) -> QString {
        return sc.isEmpty() ? t : t + QStringLiteral("  (") + sc.toString(QKeySequence::NativeText) + QStringLiteral(")");
    };

    // Register a Material-icon action and add to the toolbar
    auto addMat = [&](QChar code, const QString& tooltip, QKeySequence sc,
                      auto* recv, auto slot, bool bindShortcut = true) -> QAction* {
        auto* a = new QAction(tooltip, this);
        a->setToolTip(tip(tooltip, sc));
        // Some toolbar actions intentionally do not bind their shortcut: the
        // matching menu action already owns it. Keep the key available to
        // AlignedMenu when this button moves into the toolbar overflow menu.
        if (!sc.isEmpty())
            a->setProperty("alignedMenuShortcut", sc.toString(QKeySequence::NativeText));
        if (!sc.isEmpty() && bindShortcut) a->setShortcut(sc);
        connect(a, &QAction::triggered, recv, slot);
        m_toolbar->addAction(a);
        if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(a)))
            button->setAccessibleName(tooltip);
        m_tbIcons.append({a, false, code, {}});
        return a;
    };

    // Keep keyboard-only headings in this registry too so the palette
    // harvests them just like the visible heading buttons.
    auto addTxt = [&](const QString& text, const QString& tooltip, QKeySequence sc,
                      void (MarkdownEditor::*slot)(), bool bindShortcut = true,
                      int yOffset = 0, bool toolbarButton = true) -> QAction* {
        auto* a = new QAction(tooltip, this);
        a->setToolTip(tip(tooltip, sc));
        if (yOffset != 0)
            a->setProperty("toolbarIconYOffset", yOffset);
        if (!sc.isEmpty() && bindShortcut) a->setShortcut(sc);
        connect(a, &QAction::triggered, this, [this, slot]() {
            if (m_activeEditor) (m_activeEditor->*slot)();
        });
        if (toolbarButton) {
            m_toolbar->addAction(a);
            if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(a)))
                button->setAccessibleName(tooltip);
        } else {
            addAction(a);
        }
        m_tbIcons.append({a, true, {}, text});
        m_formatActions.append(a);
        return a;
    };

    // Convenience: Material-icon formatting button wired to the active editor
    auto addFmtMat = [&](QChar code, const QString& tooltip, QKeySequence sc,
                         void (MarkdownEditor::*slot)(), bool bindShortcut = true) -> QAction* {
        QAction* a = addMat(code, tooltip, sc, this, [this, slot]() {
            if (m_activeEditor) (m_activeEditor->*slot)();
        }, bindShortcut);
        m_formatActions.append(a);
        return a;
    };

    // ── File ops ─────────────────────────────────────────────
    // note_add=0xE89C  folder_open=0xE2C8  save=0xE161 print=0xE8AD picture_as_pdf=0xE415 public=0xE80B
    m_actToolbarNew = addMat(QChar(0xE89C), tr("New"), CommandShortcutRegistry::defaultShortcut(CommandId::FileNew), this, &MainWindow::newFile, false);

    // One split Open control keeps file and workspace navigation together
    // without duplicating either in the application toolbar.
    {
        auto* actOpen = addMat(QChar(0xE2C8), tr("Open"), CommandShortcutRegistry::defaultShortcut(CommandId::FileOpen), this, &MainWindow::openFile, false);
        m_openButton = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(actOpen));
        m_toolbarOpenMenu = new AlignedMenu(this);
        m_toolbarOpenMenu->setObjectName(QStringLiteral("topMenuBarMenu"));
        m_toolbarOpenMenu->addAction(tr("Open..."), this, &MainWindow::openFile);

        auto addRecentMirror = [this](const QString& title, QMenu* source) {
            auto* mirror = new AlignedMenu(title, m_toolbarOpenMenu);
            connect(mirror, &QMenu::aboutToShow, this, [mirror, source]() {
                mirror->clear();
                for (QAction* sourceAction : source->actions()) {
                    if (sourceAction->isSeparator()) {
                        mirror->addSeparator();
                        continue;
                    }
                    QAction* mirrorAction = mirror->addAction(sourceAction->icon(), sourceAction->text());
                    mirrorAction->setEnabled(sourceAction->isEnabled());
                    connect(mirrorAction, &QAction::triggered, sourceAction, &QAction::trigger);
                }
            });
            m_toolbarOpenMenu->addMenu(mirror);
        };
        addRecentMirror(tr("Recent Files"), m_recentMenu);
        m_toolbarOpenMenu->addSeparator();
        m_toolbarOpenMenu->addAction(tr("Open Folder..."), this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, tr("Open Folder"), m_workspacePanelCtrl->rootPath(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!dir.isEmpty())
                m_workspacePanelCtrl->setRoot(dir);
        });
        addRecentMirror(tr("Recent Folders"), m_workspacePanelCtrl->recentFoldersManager()->menu());

        if (m_openButton) {
            m_openButton->setObjectName(QStringLiteral("toolbarOpenButton"));
            m_openButton->setMenu(m_toolbarOpenMenu);
            m_openButton->setPopupMode(QToolButton::MenuButtonPopup);
        }
    }
    addMat(QChar(0xE161), tr("Save"),      CommandShortcutRegistry::defaultShortcut(CommandId::FileSave), this, &MainWindow::saveFile, false);
    addMat(QChar(0xE8AD), tr("Print"),     CommandShortcutRegistry::defaultShortcut(CommandId::FilePrint), this, &MainWindow::printDocument, false);
    addMat(QChar(0xE415), tr("Export PDF"),  CommandShortcutRegistry::defaultShortcut(CommandId::FileExportPdf), this, &MainWindow::doExportPdf, false);
    addMat(QChar(0xE873), tr("Export DOCX"), CommandShortcutRegistry::defaultShortcut(CommandId::FileExportDocx), this, &MainWindow::doExportDocx, false);
    addMat(QChar(0xF1C5), tr("Preview in Browser"),
           CommandShortcutRegistry::defaultShortcut(CommandId::FilePreviewBrowser), this,
           &MainWindow::doPreviewBrowser, false);
    m_toolbar->addSeparator();
    m_actToolbarCopyPath = addMat(QChar(0xE14D), tr("Copy Path"), QKeySequence(), this,
                                 &MainWindow::copyCurrentFilePath, false);
    // 5/6 of the box is the 30px glyph that read correctly at 150%.
    m_actToolbarCopyPath->setProperty("toolbarGlyphScale", 5.0 / 6.0);
    m_actToolbarCopyPath->setProperty("commandPaletteCategory", tr("File"));
    m_actToolbarReveal = addMat(QChar(0xE895), tr("Reveal Active File in Explorer"), QKeySequence(), this,
                                &MainWindow::revealCurrentFileInExplorer, false);
    m_actToolbarTerminal = addMat(QChar(0xEB8E), tr("Open Active File Folder in Terminal"), QKeySequence(), this,
                                  &MainWindow::openCurrentFolderInTerminal, false);
    m_actToolbarReveal->setProperty("commandPaletteCategory", tr("File"));
    m_actToolbarTerminal->setProperty("commandPaletteCategory", tr("File"));
    m_toolbar->addSeparator();

    // ── Inline formatting ─────────────────────────────────────
    // format_bold=0xE238  format_italic=0xE23F  format_strikethrough=0xE257
    addFmtMat(QChar(0xE238), tr("Bold"),          CommandShortcutRegistry::defaultShortcut(CommandId::FormatBold), &MarkdownEditor::fmtBold);
    addFmtMat(QChar(0xE23F), tr("Italic"),        CommandShortcutRegistry::defaultShortcut(CommandId::FormatItalic), &MarkdownEditor::fmtItalic);
    addFmtMat(QChar(0xE257), tr("Strikethrough"), CommandShortcutRegistry::defaultShortcut(CommandId::FormatStrikethrough), &MarkdownEditor::fmtStrike);
    if (m_actHighlight) {
        m_actHighlight->setToolTip(tip(tr("Highlight"), m_actHighlight->shortcut()));
        m_toolbar->addAction(m_actHighlight);
        if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(m_actHighlight)))
            button->setAccessibleName(tr("Highlight"));
        // 2/3 of the box is the 24px glyph that read correctly at 150%. At a
        // full-box em the pencil's underline bar overflows the icon at every
        // scale factor, so the bar is clipped at the bottom and sides.
        m_actHighlight->setProperty("toolbarGlyphScale", 2.0 / 3.0);
        // The action belongs to Format, so all UI surfaces dispatch through
        // one command rather than a toolbar proxy.
        m_tbIcons.append({m_actHighlight, false, QChar(0xE22B), {}});
    }
    m_toolbar->addSeparator();

    // ── Headings (text icons) ─────────────────────────────────
    addTxt(QStringLiteral("H1"), tr("Heading 1"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading1), &MarkdownEditor::fmtH1, true, -2);
    addTxt(QStringLiteral("H2"), tr("Heading 2"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading2), &MarkdownEditor::fmtH2, true, -2);
    addTxt(QStringLiteral("H3"), tr("Heading 3"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading3), &MarkdownEditor::fmtH3, true, -2);
    m_toolbar->addSeparator();
    addTxt(QStringLiteral("H4"), tr("Heading 4"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading4), &MarkdownEditor::fmtH4, true, 0, false);
    addTxt(QStringLiteral("H5"), tr("Heading 5"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading5), &MarkdownEditor::fmtH5, true, 0, false);
    addTxt(QStringLiteral("H6"), tr("Heading 6"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertHeading6), &MarkdownEditor::fmtH6, true, 0, false);
    // format_align_left/center/right/justify. The shared Format-menu actions
    // retain their command identities while the toolbar exposes them through
    // one icon-enabled dropdown.
    {
        auto* alignmentAction = new QAction(tr("Alignment"), this);
        alignmentAction->setToolTip(tr("Alignment"));
        alignmentAction->setProperty("commandPaletteHidden", true);
        // This glyph uses nearly its full em square, unlike its neighboring
        // toolbar symbols. Render it one step smaller for a matching ink height.
        alignmentAction->setProperty("toolbarIconSize", 22);
        m_toolbar->addAction(alignmentAction);
        m_tbIcons.append({alignmentAction, false, QChar(0xE236), {}});

        auto* alignmentMenu = new ToolbarDropdownMenu(this);
        alignmentMenu->setActionIconsVisible(true);
        alignmentMenu->addAction(m_actAlignLeft);
        alignmentMenu->addAction(m_actAlignCenter);
        alignmentMenu->addAction(m_actAlignRight);
        alignmentMenu->addAction(m_actAlignJustify);
        for (const auto [action, icon] : {
                 std::pair{m_actAlignLeft, QChar(0xE236)},
                 std::pair{m_actAlignCenter, QChar(0xE234)},
                 std::pair{m_actAlignRight, QChar(0xE237)},
                 std::pair{m_actAlignJustify, QChar(0xE235)}})
            m_tbIcons.append({action, false, icon, {}});

        if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(alignmentAction))) {
            button->setObjectName(QStringLiteral("toolbarAlignmentButton"));
            button->setAccessibleName(tr("Alignment"));
            button->setMenu(alignmentMenu);
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }

    // ── Block formatting ──────────────────────────────────────
    // format_list_bulleted=0xE241  format_list_numbered=0xE242
    // checklist=0xE6B1  format_quote=0xE244
    addFmtMat(QChar(0xE241), tr("Bullet list"),   CommandShortcutRegistry::defaultShortcut(CommandId::InsertBulletList), &MarkdownEditor::fmtBulletList);
    addFmtMat(QChar(0xE242), tr("Numbered list"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertNumberList), &MarkdownEditor::fmtNumberList);
    addFmtMat(QChar(0xE6B1), tr("Checklist"),     CommandShortcutRegistry::defaultShortcut(CommandId::InsertChecklist), &MarkdownEditor::fmtChecklist);
    addFmtMat(QChar(0xE244), tr("Blockquote"),    CommandShortcutRegistry::defaultShortcut(CommandId::InsertBlockquote), &MarkdownEditor::fmtQuote);
    // horizontal_rule=0xF108. This is a glyph, rather than a toolbar
    // separator, so the insertion command remains clearly discoverable.
    auto* toolbarHorizontalRule = new QAction(tr("Horizontal Rule"), this);
    toolbarHorizontalRule->setToolTip(tr("Insert Horizontal Rule"));
    toolbarHorizontalRule->setProperty(
        "alignedMenuShortcut", m_actHorizontalRule->shortcut().toString(QKeySequence::NativeText));
    connect(toolbarHorizontalRule, &QAction::triggered,
            m_actHorizontalRule, &QAction::trigger);
    m_toolbar->addAction(toolbarHorizontalRule);
    if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(toolbarHorizontalRule)))
        button->setAccessibleName(tr("Insert Horizontal Rule"));
    m_tbIcons.append({toolbarHorizontalRule, false, QChar(0xF108), {}});
    m_formatActions.append(toolbarHorizontalRule);
    m_toolbar->addSeparator();

    // ── Code block, table, link, image ───────────────────────
    // code=0xE86F  border_all=0xE228  link=0xE157  image=0xE3F4
    // Keep the menu action icon-free. The toolbar proxy preserves that menu
    // presentation while triggering the same fenced-block action.
    auto* toolbarCodeBlock = new QAction(m_actInsertCodeBlock->text(), this);
    toolbarCodeBlock->setToolTip(m_actInsertCodeBlock->toolTip());
    toolbarCodeBlock->setProperty("alignedMenuShortcut",
                                  m_actInsertCodeBlock->shortcut().toString(QKeySequence::NativeText));
    toolbarCodeBlock->setProperty("commandPaletteTitle",
                                  m_actInsertCodeBlock->property("commandPaletteTitle"));
    toolbarCodeBlock->setEnabled(m_actInsertCodeBlock->isEnabled());
    connect(toolbarCodeBlock, &QAction::triggered,
            m_actInsertCodeBlock, &QAction::trigger);
    connect(m_actInsertCodeBlock, &QAction::changed, toolbarCodeBlock, [toolbarCodeBlock, this] {
        toolbarCodeBlock->setEnabled(m_actInsertCodeBlock->isEnabled());
    });
    m_toolbar->addAction(toolbarCodeBlock);
    if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(toolbarCodeBlock)))
        button->setAccessibleName(tr("Code Block"));
    m_tbIcons.append({toolbarCodeBlock, false, QChar(0xE86F), {}});
    // ── Math ──────────────────────────────────────────────────
    // The individual actions stay separate so their existing commands,
    // shortcuts, undo behavior and palette entries are unchanged.  The main
    // toolbar presents them from one calculator-icon dropdown instead.
    {
        auto createMathAction = [this, &tip](const QString& title, QKeySequence shortcut,
                                       void (MarkdownEditor::*slot)(), QChar icon) {
            auto* action = new QAction(title, this);
            action->setShortcut(shortcut);
            action->setToolTip(tip(title, shortcut));
            connect(action, &QAction::triggered, this, [this, slot] {
                if (m_activeEditor)
                    (m_activeEditor->*slot)();
            });
            m_formatActions.append(action);
            // These actions are no longer direct toolbar buttons, but they
            // remain in this registry so Command Palette harvesting and theme
            // icon refresh keep their established behavior.
            m_tbIcons.append({action, false, icon, {}});
            return action;
        };
        auto* inlineMath = createMathAction(tr("Inline math"),
            CommandShortcutRegistry::defaultShortcut(CommandId::InsertInlineMath), &MarkdownEditor::fmtMathInline,
            QChar(0xEA5F));
        auto* blockMath = createMathAction(tr("Block math"),
            CommandShortcutRegistry::defaultShortcut(CommandId::InsertBlockMath), &MarkdownEditor::fmtMathBlock,
            QChar(0xE24A));

        auto* mathAction = new QAction(tr("Insert Math"), this);
        mathAction->setToolTip(tr("Insert Math"));
        mathAction->setProperty("commandPaletteHidden", true);
        m_toolbar->addAction(mathAction);
        m_tbIcons.append({mathAction, false, QChar(0xEA5F), {}});

        auto* mathMenu = new ToolbarDropdownMenu(this);
        mathMenu->addAction(inlineMath);
        mathMenu->addAction(blockMath);
        mathMenu->addSeparator();
        auto* moreMath = mathMenu->addAction(tr("More Math..."));
        connect(moreMath, &QAction::triggered, this, [this] {
            // This is deliberately one-way: choosing More Math never hides a
            // toolbar that is already visible.
            if (m_actMathToolbar && !m_actMathToolbar->isChecked())
                m_actMathToolbar->trigger();
        });

        if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(mathAction))) {
            button->setObjectName(QStringLiteral("toolbarMathButton"));
            button->setAccessibleName(tr("Insert Math"));
            button->setMenu(mathMenu);
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }
    
    // Table button — shows a size picker popup
    {
        auto* actTable = addMat(QChar(0xE228), tr("Table"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertTable), this, [this]() {
            if (!m_activeEditor) return;
            auto* picker = new TableSizePicker([this](int rows, int cols) {
                if (m_activeEditor) m_activeEditor->fmtTable(rows, cols);
            }, this);
            QPoint pos = QCursor::pos();
            if (m_tableButton && m_tableButton->isVisible()) {
                pos = m_tableButton->mapToGlobal(QPoint(0, m_tableButton->height()));
            }
            picker->move(pos);
            picker->show();
        }, true);
        
        m_formatActions.append(actTable);
        m_tableButton = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(actTable));
    }
    auto* toolbarLink = new QAction(tr("Link"), this);
    toolbarLink->setToolTip(tr("Insert Inline Link"));
    toolbarLink->setProperty("commandPaletteTitle",
                             m_actInsertInlineLink->property("commandPaletteTitle"));
    toolbarLink->setEnabled(m_actInsertInlineLink->isEnabled());
    connect(toolbarLink, &QAction::triggered, m_actInsertInlineLink, &QAction::trigger);
    connect(m_actInsertInlineLink, &QAction::changed, toolbarLink, [toolbarLink, this] {
        toolbarLink->setEnabled(m_actInsertInlineLink->isEnabled());
    });
    m_toolbar->addAction(toolbarLink);
    if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(toolbarLink)))
        button->setAccessibleName(tr("Insert Inline Link"));
    m_tbIcons.append({toolbarLink, false, QChar(0xE157), {}});
    m_formatActions.append(toolbarLink);

    // Split Image control: default click/shortcut inserts a Markdown image
    // template; the dropdown also offers browsing for a real image file.
    // Mirrors the Open button's AlignedMenu split-button pattern.
    {
        auto* actImage = addMat(QChar(0xE3F4), tr("Insert Markdown Image"), CommandShortcutRegistry::defaultShortcut(CommandId::InsertImage), this, [this]() {
            if (m_activeEditor)
                m_activeEditor->fmtInsertImageTemplate();
        }, true);
        actImage->setProperty("commandPaletteTitle", tr("Image"));
        m_formatActions.append(actImage);
        m_imageButton = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(actImage));

        m_toolbarImageMenu = new ToolbarDropdownMenu(this);
        m_toolbarImageMenu->addAction(actImage);
        m_toolbarImageMenu->addAction(tr("Browse Image..."), this, [this]() {
            if (m_activeEditor) {
                TabPage* pg = m_tabs->currentPage();
                m_activeEditor->fmtImage(pg ? pg->model->filePath() : QString());
            }
        });

        if (m_imageButton) {
            m_imageButton->setObjectName(QStringLiteral("toolbarImageButton"));
            m_imageButton->setMenu(m_toolbarImageMenu);
            m_imageButton->setPopupMode(QToolButton::InstantPopup);
        }
    }
    auto* toolbarPageBreak = new QAction(tr("Page Break"), this);
    toolbarPageBreak->setToolTip(tip(tr("Page Break"), m_actPageBreak->shortcut()));
    toolbarPageBreak->setProperty(
        "alignedMenuShortcut", m_actPageBreak->shortcut().toString(QKeySequence::NativeText));
    connect(toolbarPageBreak, &QAction::triggered, m_actPageBreak, &QAction::trigger);
    m_toolbar->addAction(toolbarPageBreak);
    m_tbIcons.append({toolbarPageBreak, false, QChar(0xEACA), {}});
    m_formatActions.append(m_actPageBreak);
    m_formatActions.append(toolbarPageBreak);
    m_toolbar->addSeparator();

    // m_toolbar->addSeparator();

    // ── Find / Preview ────────────────────────────────────────
    // search=0xE8B6  find_replace=0xE881  visibility=0xE8F4  open_in_new=0xE89E
    addMat(QChar(0xE8B6), tr("Find"), CommandShortcutRegistry::defaultShortcut(CommandId::EditFind), this, &MainWindow::openFind, false);
    // Shortcut shown in the tooltip only; QKeySequence::Replace is already bound
    // to the Edit ▸ Replace menu action (avoids an ambiguous-shortcut warning).
    addMat(QChar(0xE881), tr("Find and Replace"), CommandShortcutRegistry::defaultShortcut(CommandId::EditReplace), this, &MainWindow::openReplace, false);

    m_toolbar->addSeparator();

    m_actModeToggle = new QAction(this);
    m_actModeToggle->setCheckable(true);
    m_actModeToggle->setChecked(true);
    m_actModeToggle->setText(tr("Markdown"));
    m_actModeToggle->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewSwitchEditorMode));
    m_actModeToggle->setToolTip(tip(tr("Switch editor mode"), m_actModeToggle->shortcut()));
    connect(m_actModeToggle, &QAction::triggered, this, &MainWindow::setEditorMode);
    m_toolbar->addAction(m_actModeToggle);
    m_tbIcons.append({m_actModeToggle, false, QChar(0xE873), {}});

    if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(m_actModeToggle))) {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setProperty("modeToggle", true);
        button->setAutoRaise(false);
        button->setCursor(Qt::PointingHandCursor);
    }
    ToolbarHoverAnimation::install(m_toolbar, ThemeCard::ButtonRadius);
    // Every toolbar item exists by now, so the first button's position is
    // well defined once its layout has been activated.
    syncTitleBarIconAlignment();
    FASTMD_MARK("    toolbar: constructed");
}

// ---------------------------------------------------------------------------
void MainWindow::updateToolbarIcons(bool dark)
{
    QColor iconColor = dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
    constexpr int sz = 24;
    for (const TbIcon& def : std::as_const(m_tbIcons)) {
        const int iconSize = def.action->property("toolbarIconSize").toInt();
        const int size = iconSize > 0 ? iconSize : sz;
        const double glyphScale = def.action->property("toolbarGlyphScale").toDouble();
        const int yOffset = def.action->property("toolbarIconYOffset").toInt();
        if (def.isText)
            def.action->setIcon(IconHelper::textIcon(def.text, iconColor, size, false, yOffset));
        else if (glyphScale > 0.0)
            def.action->setIcon(IconHelper::materialIconScaled(def.code, iconColor, glyphScale));
        else if (yOffset != 0)
            def.action->setIcon(IconHelper::materialIconTight(def.code, iconColor, size, yOffset));
        else
            def.action->setIcon(IconHelper::materialIcon(def.code, iconColor, size));
    }
    if (m_tableButton)
        m_tableButton->setIcon(IconHelper::materialIcon(QChar(0xE228), iconColor, sz));
    if (m_openButton)
        m_openButton->setIcon(IconHelper::materialIcon(QChar(0xE2C8), iconColor, sz));
    if (m_overflowBtn) {
        ToolbarOverflow::updateExtensionButton(m_overflowBtn, dark, sz);
    }
    if (m_workspacePanelCtrl) {
        m_workspacePanelCtrl->updateToggleIcon(dark);
    }

    if (m_actCheckUpdates)
        m_actCheckUpdates->setIcon(IconHelper::materialMenuIcon(QChar(0xE923), iconColor, 16));
    if (m_actRateStore)
        m_actRateStore->setIcon(IconHelper::materialMenuIcon(QChar(0xE838), iconColor, 16));
    if (m_actGithubRepo)
        m_actGithubRepo->setIcon(IconHelper::materialMenuIcon(QChar(0xE89E), iconColor, 16));
    if (m_actReportIssue)
        m_actReportIssue->setIcon(IconHelper::materialMenuIcon(QChar(0xE868), iconColor, 16));
    if (m_actLicenses)
        m_actLicenses->setIcon(IconHelper::materialMenuIcon(QChar(0xE873), iconColor, 16));
    if (m_actReleaseNotes)
        m_actReleaseNotes->setIcon(IconHelper::materialMenuIcon(QChar(0xe889), iconColor, 16));
    if (m_actShortcuts)
        m_actShortcuts->setIcon(IconHelper::materialMenuIcon(QChar(0xE312), iconColor, 16));
    if (m_actUserGuide)
        m_actUserGuide->setIcon(IconHelper::materialMenuIcon(QChar(0xEA19), iconColor, 16));
    if (m_actAbout)
        m_actAbout->setIcon(IconHelper::materialMenuIcon(QChar(0xE88E), iconColor, 16));
    if (m_actGoToPreview)
        m_actGoToPreview->setIcon(IconHelper::materialMenuIcon(QChar(0xE8F4), iconColor, 16));
}

// ---------------------------------------------------------------------------
void MainWindow::ensureMathToolbar()
{
    if (m_mathToolbar || !m_toolbarRootWidget || !m_toolbarRootLayout)
        return;
    const bool dark = m_theme == QStringLiteral("dark")
        || (m_theme == QStringLiteral("system")
            && QApplication::palette().color(QPalette::Window).lightness() < 128);
    m_mathToolbar = std::make_unique<MathToolbar>(m_toolbarRootWidget, dark);
    m_mathToolbar->containerWidget()->installEventFilter(this);
    m_mathToolbar->setMatrixButtonHandler([this]() { openMatrixInsertDialog(); });
    // Always directly under the main toolbar, above Chemistry/the splitter,
    // regardless of construction order.
    m_toolbarRootLayout->insertWidget(1, m_mathToolbar->containerWidget(), 0);
    m_mathToolbar->updateIcons(dark);
    m_mathToolbar->setActiveEditor(m_activeEditor);
}

// ---------------------------------------------------------------------------
void MainWindow::ensureChemistryToolbar()
{
    if (m_chemistryToolbar || !m_toolbarRootWidget || !m_toolbarRootLayout)
        return;
    const bool dark = m_theme == QStringLiteral("dark")
        || (m_theme == QStringLiteral("system")
            && QApplication::palette().color(QPalette::Window).lightness() < 128);
    m_chemistryToolbar = std::make_unique<ChemistryToolbar>(m_toolbarRootWidget, dark);
    m_chemistryToolbar->containerWidget()->installEventFilter(this);
    // Right before the splitter, i.e. after the Math toolbar if it exists.
    const int splitterIndex = m_toolbarRootLayout->indexOf(m_mainSplitter);
    m_toolbarRootLayout->insertWidget(splitterIndex >= 0 ? splitterIndex : 1,
        m_chemistryToolbar->containerWidget(), 0);
    m_chemistryToolbar->updateIcons(dark);
    m_chemistryToolbar->setActiveEditor(m_activeEditor);
}

QWidget* MainWindow::toolbarAt(const QPoint& globalPos) const
{
    const QList<QWidget*> toolbars{
        m_toolbar,
        m_mathToolbar ? m_mathToolbar->containerWidget() : nullptr,
        m_chemistryToolbar ? m_chemistryToolbar->containerWidget() : nullptr
    };

    for (QWidget* toolbar : toolbars) {
        if (toolbar && toolbar->isVisible()
            && toolbar->rect().contains(toolbar->mapFromGlobal(globalPos)))
            return toolbar;
    }
    return nullptr;
}

bool MainWindow::showToolbarContextMenu(const QPoint& globalPos)
{
    QWidget* toolbar = toolbarAt(globalPos);
    QWidget* target = QApplication::widgetAt(globalPos);
    if (!toolbar || !m_toolbarContextMenu || target != toolbar
        || toolbar->childAt(toolbar->mapFromGlobal(globalPos)))
        return false;

    m_actMathToolbar->setChecked(m_mathToolbarVisible);
    m_actChemistryToolbar->setChecked(m_chemistryToolbarVisible);
    m_toolbarContextMenu->popup(globalPos);
    return true;
}

// ---------------------------------------------------------------------------
void MainWindow::createStatusBar()
{
    m_lblPosition = new QLabel(tr("Line: 1, Col: 1"));
    // A plain QLabel/QToolButton's effective layout minimum is its full-content
    // sizeHint() (Qt only honors an explicitly set minimumSize() when it is
    // greater than zero -- qSmartMinSize() ignores an explicit 0, which is
    // indistinguishable from never having called setMinimumWidth() at all).
    // That full-content floor across every status bar segment was the main
    // thing keeping the whole main window from reaching a narrow minimum
    // size. Give each an explicit near-zero minimum (1px) so the status bar
    // can clip their text under pressure; normal-width layouts, where there
    // is room to honor each label's sizeHint(), are unaffected.
    m_lblPosition->setMinimumWidth(1);
    m_lblExportStatus = new QLabel;
    m_lblExportStatus->setObjectName(QStringLiteral("exportStatus"));
    // Keep a stable slot beside Line/Column. A fixed reservation means export
    // feedback never makes the link/position/selection widgets jump around.
    m_lblExportStatus->setFixedWidth(210);
    m_lblExportStatus->setMinimumWidth(1);
    m_lblExportStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblExportStatus->setTextFormat(Qt::PlainText);
    m_lblExportStatus->setAccessibleName(tr("Export status"));
    m_lblSelection = new QLabel(QString());
    m_lblSelection->hide();
    m_lblLinkTarget = new QLabel;
    m_lblLinkTarget->setObjectName(QStringLiteral("linkTargetStatus"));
    // Unlike QStatusBar::showMessage()'s private label, this is styled by the
    // same QStatusBar QLabel rule as Line/Col and Characters/Words. Give it
    // the spare normal-widget space and let it clip long destinations.
    m_lblLinkTarget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_lblLinkTarget->setMinimumWidth(0);
    m_lblLinkTarget->setTextFormat(Qt::PlainText);
    m_lblLinkTarget->hide();
    m_lblWords    = new QLabel(tr("Characters: 0     Words: 0"));
    m_lblWords->setMinimumWidth(1);
    m_lblMode     = new QToolButton;
    m_lblMode->setObjectName(QStringLiteral("languageStatusButton"));
    m_lblMode->setText(tr("Markdown"));
    m_lblMode->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_lblMode->setAutoRaise(true);
    m_lblMode->setCursor(Qt::PointingHandCursor);
    m_lblMode->setAccessibleName(tr("Language selector"));
    m_lblMode->setMinimumWidth(1);
    connect(m_lblMode, &QToolButton::clicked, this, &MainWindow::openLanguageSelector);
    connect(this, &MainWindow::documentLanguageChanged,
            this, &MainWindow::onDocumentLanguageChanged);

    // Both byte-format indicators reuse the language button's look so the
    // status bar keeps one style of clickable segment.
    const auto makeStatusButton = [this](const QString& objectName, const QString& text,
                                         const QString& accessibleName, void (MainWindow::*slot)()) {
        auto* button = new QToolButton;
        button->setObjectName(objectName);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(accessibleName);
        button->setMinimumWidth(1);
        connect(button, &QToolButton::clicked, this, slot);
        return button;
    };
    m_lblLineEnding = makeStatusButton(QStringLiteral("lineEndingStatusButton"),
                                       lineEndingLabel(windowsLineEnding()),
                                       tr("Line ending selector"), &MainWindow::openLineEndingMenu);
    m_lblEncoding = makeStatusButton(QStringLiteral("encodingStatusButton"),
                                     textEncodingLabel(TextEncoding::Utf8),
                                     tr("Encoding selector"), &MainWindow::openEncodingMenu);
    m_lblSpell = makeStatusButton(QStringLiteral("spellStatusButton"),
                                  tr("Spell: Off"),
                                  tr("Spell check selector"), &MainWindow::openSpellCheckMenu);
    // No save-state indicator here: the document tab's "* " marker already
    // shows unsaved changes and clears only once a write has succeeded.
    statusBar()->addPermanentWidget(m_lblWords);
    statusBar()->addPermanentWidget(m_lblMode);
    statusBar()->addPermanentWidget(m_lblLineEnding);
    statusBar()->addPermanentWidget(m_lblEncoding);
    statusBar()->addWidget(m_lblPosition);
    statusBar()->addWidget(m_lblSpell);
    statusBar()->addWidget(m_lblExportStatus);
    statusBar()->addWidget(m_lblLinkTarget, 1);
    statusBar()->addWidget(m_lblSelection);

    // Windows opened after an update started must still show its state.
    if (UpdateChecker* updater = UpdateChecker::existingInstance())
        attachUpdateChecker(updater);
}

void MainWindow::setExportStatus(const QString& message, int timeoutMs)
{
    if (!m_lblExportStatus)
        return;
    m_lblExportStatus->setText(message);
    const quint64 generation = ++m_exportStatusGeneration;
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, generation]() {
            if (generation == m_exportStatusGeneration && m_lblExportStatus)
                m_lblExportStatus->clear();
        });
    }
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
void MainWindow::applyTheme(const QString& theme)
{
    m_theme = theme;

    const bool alreadyApplied = qApp->property("fastmd.applicationTheme").toString() == theme;
    // A window's first applyTheme() must find main()'s application already in
    // place; if it does not, readSettings()'s theme diverged from the one
    // main() used and we silently repolish every widget a second time. Later
    // calls (a real theme switch) legitimately re-apply.
    Q_ASSERT_X(alreadyApplied || m_themeEverApplied, "MainWindow::applyTheme",
               "startup re-applied the application stylesheet: theme value diverged from main()");
    m_themeEverApplied = true;
    bool dark = alreadyApplied ? ApplicationTheme::isDark() : ApplicationTheme::apply(theme);

    // A theme change is one of the things that can change how a document renders
    // without editing it, so every tab's cached "preview is current" state is
    // dropped (see previewUpToDate()).
    invalidateRenderedPreviews();

    // ThemedSplitter's handle color is plain C++, not QSS, so it needs an
    // explicit nudge on every theme change.
    if (auto* splitter = dynamic_cast<ThemedSplitter*>(m_mainSplitter))
        splitter->setDarkMode(dark);
    if (auto* splitter = dynamic_cast<ThemedSplitter*>(m_editorTerminalSplitter))
        splitter->setDarkMode(dark);
    if (m_terminalPanel)
        m_terminalPanel->setDarkMode(dark);
    if (m_detachedTerminalWindow)
        m_detachedTerminalWindow->setWindowChromeDarkMode(dark);

#ifdef Q_OS_WIN
    // Each application does up to 4 DwmSetWindowAttribute calls plus a
    // frame-changing SetWindowPos, so the deferred repeat only runs when the
    // first attempt could not have stuck: either it was rejected, or the window
    // was not native yet (winId() has just created the HWND, and DWM ignores
    // attributes on a handle the compositor has not seen).
    // false: WM_NCCALCSIZE suppresses this window's caption down to zero
    // height (see nativeEvent()), so DWMWA_CAPTION_COLOR has no caption left
    // to paint -- but DWM still folds that color into the drop shadow, which
    // reads as an oversized dark halo in dark mode. Attribute 19/20's dark
    // chrome still applies. Same reasoning as DetachedTerminalWindow.
    const bool wasNative = internalWinId() != 0;
    const bool applied = setWindowsTitleBarDarkMode(this, dark, false);
    if (!applied || !wasNative) {
        QTimer::singleShot(0, this, [this, dark] {
            setWindowsTitleBarDarkMode(this, dark, false);
        });
    }
#endif

    if (m_matrixDialog)
        m_matrixDialog->setDarkMode(dark);
    m_tabs->setDarkMode(dark);
    if (m_outlinePopup)
        m_outlinePopup->setDarkMode(dark);
    if (m_findOverlay)
        m_findOverlay->setDarkMode(dark);
    if (m_titleBar)
        m_titleBar->setDarkMode(dark);
    if (m_commandLauncher)
        m_commandLauncher->setDarkMode(dark);
    if (m_commandPalette)
        m_commandPalette->setDarkMode(dark);
    // The Source Control panel and any open diff tabs are re-themed by the
    // Workspace panel controller and TabWidget respectively.
    updateToolbarIcons(dark);
    // After updateToolbarIcons(), never before: the measurement reads the
    // first toolbar button's icon, which that call is what produces. It also
    // has to re-run here because qApp->setStyleSheet() repolishes every
    // widget, which can move the button.
    syncTitleBarIconAlignment();
    if (m_mathToolbar)
        m_mathToolbar->updateIcons(dark);
    if (m_chemistryToolbar)
        m_chemistryToolbar->updateIcons(dark);

    for (QAction* a : m_themeGroup->actions())
        a->setChecked(a->text().toLower().contains(theme));

    // qApp->setStyleSheet() above repolishes every widget: the language
    // status QToolButton must be pinned to the same height as its QLabel
    // siblings (QSS padding is otherwise applied consistently, but the native
    // style still reserves extra vertical space for a QToolButton), and the
    // fix must be re-applied after every stylesheet (re)application, not just
    // once at construction, or the repolish silently reverts it.
    if (m_lblWords) {
        const int statusRowHeight = m_lblWords->sizeHint().height();
        for (QToolButton* button : { m_lblMode, m_lblLineEnding, m_lblEncoding, m_lblSpell }) {
            if (button)
                button->setFixedHeight(statusRowHeight);
        }
        // QStatusBar::reformat() lays its item row out with a fixed 3px of
        // spacing above it and 2px below -- room for a sunken frame this
        // canvas-flat bar no longer draws -- so every segment sat half a pixel
        // low. One bottom margin on the bar itself evens the two out.
        statusBar()->setContentsMargins(0, 0, 0, 1);
    }

    if (m_tabs) {
        QTimer::singleShot(0, m_tabs, &TabWidget::updateTabBarLayout);
        // Deferred so the tab bar has repolished under the QSS just applied above.
        QTimer::singleShot(0, this, [this] { syncWorkspaceHeaderHeight(); });
    }
}

void MainWindow::syncWorkspaceHeaderHeight()
{
    if (!m_workspacePanelCtrl || !m_tabs)
        return;
    // The tab bar has no meaningful sizeHint until it actually has a tab, which
    // (on session-restore startup) can happen after this runs — callers that
    // run before any tab exists should call this again once one is created.
    if (m_tabs->count() == 0)
        return;
    // The tree's column header keeps its own height: it no longer follows the
    // taller document tab row, so the Workspace layout is unchanged by it.
    constexpr int kWorkspaceColumnHeaderHeight = 31;
    m_workspacePanelCtrl->setHeaderHeight(kWorkspaceColumnHeaderHeight);
}

void MainWindow::setTheme(const QString& theme)
{
    ApplicationTheme::apply(theme);
    applyTheme(theme);
    writeSettings();
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------


















// Shared implementation for the "Export As" submenu formats, all of which go
// through Pandoc. `pandocFormat` is passed as Pandoc's `-t` writer when
// non-empty; otherwise Pandoc infers the writer from the file extension.











// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Editor connection
// ---------------------------------------------------------------------------
void MainWindow::connectEditor(MarkdownEditor* editor, PreviewWidget* preview)
{
    if (!editor) return;
    editor->setContextMenuActions(m_actWordWrap, m_actTogglePreview, m_actGoToPreview);
    connect(editor, &MarkdownEditor::contentReady,
            this,   &MainWindow::onContentReady);
    // Both signals are emitted for an edit that moves the caret and changes the
    // selection, so they are coalesced into one update per event-loop turn
    // instead of running the whole handler (which can count the words in the
    // selection) twice.
    connect(editor, &QPlainTextEdit::cursorPositionChanged,
            this,   &MainWindow::scheduleCursorPositionUpdate);
    connect(editor, &QPlainTextEdit::selectionChanged,
            this,   &MainWindow::scheduleCursorPositionUpdate);
    connect(editor, &MarkdownEditor::zoomRequested,
            this, [this](int delta) { if (delta > 0) zoomIn(); else zoomOut(); });
    connect(editor, &MarkdownEditor::terminalRequested,
            this, &MainWindow::openTerminalAtFile);
    if (preview) {
        connect(preview, &PreviewWidget::zoomRequested,
                this, [this](int delta) { if (delta > 0) zoomIn(); else zoomOut(); });

        connect(preview, &PreviewWidget::contextMenuRequested,
                this, &MainWindow::showPreviewContextMenu);
        connect(preview, &PreviewWidget::linkHoverChanged, this,
                [this, preview](const QString& url) {
                    // A hidden/inactive tab can emit a final mouse-leave while
                    // WebView2 is handing native focus around. Only its active
                    // Preview may borrow the status bar.
                    if (preview != m_activePreview || !statusBar())
                        return;
                    if (url.isEmpty()) {
                        if (m_lblLinkTarget) {
                            m_lblLinkTarget->clear();
                            m_lblLinkTarget->hide();
                        }
                        return;
                    }
                    if (m_lblLinkTarget) {
                        m_lblLinkTarget->setText(url);
                        m_lblLinkTarget->show();
                    }
                });
        connect(preview, &PreviewWidget::linkRequested, this, [this](const QString& rawUrl) {
            const QUrl url(rawUrl);
            QString path = url.isLocalFile() ? url.toLocalFile() : QString();
            if (path.isEmpty() && url.host().compare(QStringLiteral("fastmd.doc"), Qt::CaseInsensitive) == 0) {
                const QString base = m_tabs && m_tabs->currentModel()
                    ? m_tabs->currentModel()->filePath() : QString();
                if (!base.isEmpty())
                    path = QDir(QFileInfo(base).absolutePath()).filePath(url.path(QUrl::FullyDecoded));
            }
            if (!path.isEmpty() && QFileInfo::exists(path)
                && SyntaxDefinitionResolver::isEditableTextFile(path)) {
                recordNavigationDeparture();
                openFilePath(path);
            } else {
                QDesktopServices::openUrl(url);
            }
        });
        connect(preview, &PreviewWidget::editorFocusRequested,
                this, [this, editor] {
            // A hidden Editor (Reading Mode) cannot take native focus, and
            // handing it over anyway would leave the window with no focused
            // control and kill keyboard scrolling in the Preview.
            if (!editor || editor != m_activeEditor || !editor->isVisible()) return;
            editor->restoreKeyboardFocus();
        });
        connect(preview, &PreviewWidget::taskToggleRequested,
                this, [this, editor](int start, int end, bool checked) {
            togglePreviewTask(start, end, checked);
            if (editor == m_activeEditor && editor->isVisible()) {
                editor->activateWindow();
                editor->restoreKeyboardFocus();
            }
        });

        // Keyboard shortcuts pressed while the preview (a native WebView2 child
        // window) has OS focus never reach Qt's QShortcutMap on their own; the
        // preview forwards the ones that don't depend on editor state here.
        connect(preview, &PreviewWidget::commandRequested,
                this, [this](PreviewWidget::Command cmd) {
            using Command = PreviewWidget::Command;
            switch (cmd) {
            case Command::Find:        openFind(); break;
            case Command::Replace:     openReplace(); break;
            case Command::FindNext:    findNext(); break;
            case Command::FindPrevious: findPrevious(); break;
            case Command::CloseDocumentFind:
                if (m_findOverlay)
                    m_findOverlay->closeOverlay();
                break;
            case Command::Save:        saveFile(); break;
            case Command::SaveAs:      saveFileAs(); break;
            case Command::SaveAll:     saveAll(); break;
            case Command::Print:       printDocument(); break;
            case Command::ExportHtml:  doExportHtml(); break;
            case Command::ExportDocx:  doExportDocx(); break;
            case Command::ExportPdf:   doExportPdf(); break;
            case Command::Preferences: openPreferences(); break;
            case Command::ZoomIn:      zoomIn(); break;
            case Command::ZoomOut:     zoomOut(); break;
            case Command::CloseTab:
                if (m_tabs->count() > 0) onTabCloseRequested(m_tabs->currentIndex());
                break;
            case Command::TogglePreview:
                togglePreview();
                break;
            case Command::ToggleReadingMode:
                if (m_actReadingMode && m_actReadingMode->isEnabled())
                    toggleReadingMode();
                break;
            case Command::ToggleMindMap:
                toggleMindMap();
                break;
            case Command::CommandPalette:
                openCommandPalette();
                break;
            case Command::WorkspaceSearch:
                if (m_workspacePanelCtrl) {
                    if (isNotepadModeActive()) setNotepadMode(false);
                    m_workspacePanelCtrl->setPanelMode(WorkspacePanelController::PanelMode::Workspace);
                    if (!m_workspacePanelCtrl->isVisible()) m_workspacePanelCtrl->setVisible(true);
                    m_workspacePanelCtrl->focusSearch();
                }
                break;
            case Command::FindInFiles:
                openWorkspaceSearch(false);
                break;
            case Command::ReplaceInFiles:
                openWorkspaceSearch(true);
                break;
            case Command::SwitchEditorMode:
                if (m_actModeToggle) m_actModeToggle->trigger();
                break;
            case Command::ToggleWordWrap:
                if (m_actWordWrap) m_actWordWrap->trigger();
                break;
            case Command::Back:    navigateBack(); break;
            case Command::Forward: navigateForward(); break;
            case Command::FormatDocument:
                if (m_actFormatDocument && m_actFormatDocument->isEnabled())
                    m_actFormatDocument->trigger();
                break;
            case Command::RefreshPreview:
                refreshActivePreview();
                break;
            }
        });

        // Bidirectional proportional scroll sync. The WebView2 preview has no
        // QScrollBar, so its position is expressed as a fraction (0..1).
        connect(editor->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this](int) {
            if (m_scrollSyncing || !m_activeEditor || !m_activePreview) return;
            QScrollBar* src = m_activeEditor->verticalScrollBar();
            int srcMax = src->maximum();
            if (srcMax == 0) return;
            m_scrollSyncing = true;
            m_activePreview->setScrollFraction(double(src->value()) / srcMax);
            m_scrollSyncing = false;
        });
        connect(preview, &PreviewWidget::scrolled,
                this, [this](double fraction) {
            if (m_scrollSyncing || !m_activeEditor || !m_activePreview) return;
            if (m_activePreview->isRefreshing()) return;
            QScrollBar* dst = m_activeEditor->verticalScrollBar();
            m_scrollSyncing = true;
            dst->setValue(qRound(fraction * dst->maximum()));
            m_scrollSyncing = false;
        });
    }
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
        int idx = m_tabs->currentIndex();
        m_tabs->markDirty(idx, modified);
        updateWindowTitle();
    });
    connect(editor, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    editor->installEventFilter(this);
    if (m_matrixDialog)
        m_matrixDialog->setEditor(editor);
}


void MainWindow::disconnectEditor()
{
    if (m_activeEditor) {
        m_activeEditor->removeEventFilter(this);
        disconnect(m_activeEditor, nullptr, this, nullptr);
        disconnect(m_activeEditor->document(), nullptr, this, nullptr);
        disconnect(m_activeEditor->verticalScrollBar(), nullptr, this, nullptr);
    }
    if (m_activePreview) {
        disconnect(m_activePreview, nullptr, this, nullptr);
    }
}

bool MainWindow::event(QEvent* event)
{
    // Returning from another application (or from the editor) is exactly when
    // the working tree is most likely to have changed, which is why Source
    // Control has no file watcher and never polls.
    if (event->type() == QEvent::WindowActivate && m_sourceControl
        && m_workspacePanelCtrl
        && m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::SourceControl
        && m_workspacePanelCtrl->isVisible()) {
        m_sourceControl->refresh();
    }
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* anchor = nullptr;
        if (watched == m_languagePopup)
            anchor = m_lblMode;
        else if (watched == m_lineEndingMenu)
            anchor = m_lblLineEnding;
        else if (watched == m_encodingMenu)
            anchor = m_lblEncoding;
        if (anchor)
            rememberStatusBarPopupDismissal(anchor);
    }

    if (event->type() == QEvent::ContextMenu
        && (watched == m_toolbar
            || (m_mathToolbar && watched == m_mathToolbar->containerWidget())
            || (m_chemistryToolbar && watched == m_chemistryToolbar->containerWidget()))) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        if (contextEvent->reason() == QContextMenuEvent::Mouse) {
            const QPoint globalPos = contextEvent->globalPos();
            if (showToolbarContextMenu(globalPos))
                return true;
        }
    }

    if (m_mainSplitter && m_mainSplitter->count() > 1 && watched == m_mainSplitter->handle(1) && event->type() == QEvent::MouseButtonDblClick) {
        // Reset to default 380px width
        int total = m_mainSplitter->width();
        int handleW = m_mainSplitter->handleWidth();
        m_mainSplitter->setSizes({ 380, total - 380 - handleW });
        return true;
    }

    // Esc while the editor has focus also closes the find overlay. Does not
    // consume the event -- MarkdownEditor::keyPressEvent still gets Escape
    // afterward for its own snippet-tab-stop handling, which must keep
    // priority (see find-replace-plan.md).
    if (watched == m_activeEditor && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && m_findOverlay && m_findOverlay->isVisible())
            m_findOverlay->closeOverlay();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onEditorActivated(MarkdownEditor* editor, PreviewWidget* preview)
{
    if (m_lblLinkTarget) {
        m_lblLinkTarget->clear();
        m_lblLinkTarget->hide();
    }
    if (!m_restoringNavigation && m_activeEditor && m_activeEditor != editor)
        recordNavigationDeparture();
    disconnectEditor();
    m_activeEditor  = editor;
    m_activePreview = preview;
    if (editor && editor->spellCheckActive())
        editor->invalidateSpellCheck();
    ++m_htmlGeneration;
    // m_currentHtml belongs to the tab we are leaving, so the HTML cache is
    // always invalidated. The *preview* only needs rebuilding when this tab's
    // document or render epoch moved since its preview was last built —
    // switching away and back therefore costs no conversion at all.
    m_htmlDirty = true;
    m_previewDirty = !previewUpToDate(m_tabs ? m_tabs->currentPage() : nullptr);
    connectEditor(editor, preview);
    // Outside connectEditor() because that returns early for tabs with no
    // editor; the controller must still be re-pointed so stale highlights and
    // matches do not survive.
    syncFindTarget();
    if (m_mathToolbar)
        m_mathToolbar->setActiveEditor(editor);
    if (m_chemistryToolbar)
        m_chemistryToolbar->setActiveEditor(editor);
    if (m_outlinePopup && m_outlinePopup->isVisible())
        m_outlinePopup->setEditor(editor, m_tabs->currentModel());
    if (m_workspacePanelCtrl)
        m_workspacePanelCtrl->setOutlineEditor(editor);
    // The new tab is a different document, so neither the cached selection range
    // nor the cached word count describes it.
    m_lastSelectionStart = -1;
    m_lastSelectionEnd = -1;
    onCursorPositionChanged();
    updateNavigationActions();
    // Kept synchronous rather than debounced: a tab switch is user-initiated and
    // infrequent, and the status bar must not show the previous document's count.
    updateWordCount();
    updateWindowTitle();
    syncCurrentTabUi();
    m_workspacePanelCtrl->updateRoot();
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------



// ---------------------------------------------------------------------------
// Preview-render benchmark (Phase 1 / Phase 3). See MainWindow.h.
// ---------------------------------------------------------------------------






// ---------------------------------------------------------------------------
// Status bar updates
// ---------------------------------------------------------------------------










// ---------------------------------------------------------------------------
// Close handling
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Tab rename (double-click or context menu "Rename...")
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tab context menu (right-click)
// ---------------------------------------------------------------------------

// Right-click menu for the Preview pane. WebView2's own context menu is
// disabled host-side (see PreviewWidget::Impl::setup); this exposes the same
// commands the File menu already offers instead of duplicating their logic.
// Export/Print reuse the exact File-menu QAction instances so enabled state,
// shortcuts, and translations stay in sync automatically.

void MainWindow::closeEvent(QCloseEvent* event)
{
    TabDragController::instance()->cancelForWindow(this);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!confirmClose(i)) {
            for (int j = 0; j < m_tabs->count(); ++j) {
                if (TabPage* page = m_tabs->pageAt(j)) {
                    m_autoSaveSuspended.remove(page->model);
                    scheduleAutoSave(page->model);
                }
            }
            event->ignore();
            return;
        }
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (TabPage* page = m_tabs->pageAt(i)) {
            DocumentSaveCoordinator::invalidate(page->model->saveState());
            m_ignoredFileChanges.clear(page->model->filePath());
            cleanupRecoveryFor(page->model);
        }
    // Fold a detached terminal back into this window before it goes away, so
    // shutdown terminates its sessions the same way a docked terminal's do
    // rather than leaving the floating window to close on its own later.
    //
    // This runs only once every tab-close prompt has been accepted and
    // shutdown is certain. Folding back first would mean a cancelled quit
    // silently docked the terminal -- and TerminalPanel::setDetached(false)
    // would have persisted that as the user's layout.
    if (m_detachedTerminalWindow) {
        // Fold back down the *hidden* branch, the same reason
        // closeTerminalPanel() does: with m_terminalVisible still set,
        // reattachTerminalPanel() lays the panel out in a window that is
        // closing -- a visible dock and a focus grab on the way out, plus a
        // "has docked layout" flag that would make writeSettings() below
        // persist a panel height the user never chose. The flag is restored
        // immediately, since the terminal *was* open and should come back.
        const bool terminalWasVisible = m_terminalVisible;
        m_terminalVisible = false;
        m_detachedTerminalWindow->close();
        m_terminalVisible = terminalWasVisible;
        // The fold-back wrote "docked"; the window was left detached, and
        // that is what the next session should restore.
        QSettings().setValue(QStringLiteral("terminal/detached"), true);
    }
    writeSettings();
    event->accept();
}

// ---------------------------------------------------------------------------
// Find / Replace
// ---------------------------------------------------------------------------




// ---------------------------------------------------------------------------
// Recent files
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Workspace tree
// ---------------------------------------------------------------------------
// Document text changes
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// File system watcher (auto reload)
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Update check
// ---------------------------------------------------------------------------
void MainWindow::checkForUpdates()
{
    UpdateChecker* updater = UpdateChecker::instance();
    switch (updater->state()) {
    case UpdateChecker::State::Idle:
        updater->checkForUpdates(this);
        break;
    case UpdateChecker::State::Ready:
        updater->restartToUpdate(this);
        break;
    case UpdateChecker::State::Checking:
    case UpdateChecker::State::Downloading:
    case UpdateChecker::State::Installing:
        break;
    }
}

void MainWindow::attachUpdateChecker(UpdateChecker* updater)
{
    if (!updater || m_updateCheckerAttached)
        return;
    m_updateCheckerAttached = true;
    connect(updater, &UpdateChecker::stateChanged, this, &MainWindow::syncUpdateStatus);
    syncUpdateStatus();
}

void MainWindow::syncUpdateStatus()
{
    const UpdateChecker* updater = UpdateChecker::existingInstance();
    const UpdateChecker::State state = updater ? updater->state() : UpdateChecker::State::Idle;

    if (m_actCheckUpdates) {
        switch (state) {
        case UpdateChecker::State::Idle:
            m_actCheckUpdates->setText(tr("Check for Updates..."));
            break;
        case UpdateChecker::State::Checking:
            m_actCheckUpdates->setText(tr("Checking for Updates..."));
            break;
        case UpdateChecker::State::Downloading:
            m_actCheckUpdates->setText(tr("Downloading Update..."));
            break;
        case UpdateChecker::State::Ready:
            m_actCheckUpdates->setText(tr("Restart to Update..."));
            break;
        case UpdateChecker::State::Installing:
            m_actCheckUpdates->setText(tr("Installing Update..."));
            break;
        }
        m_actCheckUpdates->setEnabled(state == UpdateChecker::State::Idle
                                      || state == UpdateChecker::State::Ready);
    }

    const bool showIndicator = state == UpdateChecker::State::Downloading
                               || state == UpdateChecker::State::Ready;
    if (!showIndicator) {
        if (m_updateStatusButton)
            m_updateStatusButton->hide();
        return;
    }

    if (!m_updateStatusButton) {
        m_updateStatusButton = new QToolButton;
        m_updateStatusButton->setObjectName(QStringLiteral("updateStatusButton"));
        m_updateStatusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_updateStatusButton->setAutoRaise(true);
        m_updateStatusButton->setCursor(Qt::PointingHandCursor);
        m_updateStatusButton->setAccessibleName(tr("Update status"));
        m_updateStatusButton->setMinimumWidth(1);
        connect(m_updateStatusButton, &QToolButton::clicked, this, &MainWindow::onUpdateStatusClicked);
        statusBar()->insertPermanentWidget(0, m_updateStatusButton);
    }

    const bool ready = state == UpdateChecker::State::Ready;
    if (ready) {
        m_updateStatusButton->setText(tr("Restart to Update"));
        m_updateStatusButton->setToolTip(
            tr("The update is downloaded. Click to restart FastMD and finish installing it."));
    } else {
        m_updateStatusButton->setText(
            tr("Downloading update %1%").arg(qRound(updater->downloadProgress() * 100.0)));
        m_updateStatusButton->setToolTip(
            tr("FastMD is downloading an update in the background. Click to cancel."));
    }
    if (m_updateStatusButton->property("ready").toBool() != ready) {
        m_updateStatusButton->setProperty("ready", ready);
        m_updateStatusButton->style()->unpolish(m_updateStatusButton);
        m_updateStatusButton->style()->polish(m_updateStatusButton);
    }
    m_updateStatusButton->show();
}

void MainWindow::onUpdateStatusClicked()
{
    UpdateChecker* updater = UpdateChecker::existingInstance();
    if (!updater)
        return;
    if (updater->state() == UpdateChecker::State::Ready) {
        updater->restartToUpdate(this);
    } else if (updater->state() == UpdateChecker::State::Downloading) {
        if (themedQuestion(this, tr("Cancel Update"), tr("Stop downloading the FastMD update?"))
                == QMessageBox::Yes
            && updater->state() == UpdateChecker::State::Downloading) {
            updater->cancelDownload();
        }
    }
}

// ---------------------------------------------------------------------------
