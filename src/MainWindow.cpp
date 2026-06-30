#include "MainWindow.h"
#include "StartupProfiler.h"

#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "FindReplaceDialog.h"
#include "PreferencesDialog.h"
#include "ExportManager.h"
#include "DocumentModel.h"
#include "Stylesheet.h"
#include "IconHelper.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSettings>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileSystemWatcher>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QScrollBar>
#include <QTextCursor>
#include <QSplitter>
#include <QPalette>
#include <QStyle>
#include <QRegularExpression>
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
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDialog>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QShortcut>
#include <functional>

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
    }

protected:
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
            close();
            cb(r, c);
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
};

static const int MAX_RECENT = 20;

namespace {

static void applyPdfExportOptions(QPrinter& printer, const ExportManager::PdfExportOptions& options)
{
    printer.setPageSize(options.pageSize);
    printer.setPageOrientation(options.orientation);
    printer.setPageMargins(options.marginsMm, QPageLayout::Millimeter);
}

static ExportManager::PdfExportOptions pdfExportOptionsFromPrinter(const QPrinter& printer, int fontSize)
{
    ExportManager::PdfExportOptions options;
    options.pageSize = printer.pageLayout().pageSize();
    options.orientation = printer.pageLayout().orientation();
    options.marginsMm = printer.pageLayout().margins(QPageLayout::Millimeter);
    options.fontSize = fontSize;
    return options;
}

static int countWordsInText(QStringView text)
{
    int words = 0;
    bool inWord = false;
    for (QChar ch : text) {
        if (ch.isSpace()) {
            inWord = false;
            continue;
        }
        if (!inWord) {
            ++words;
            inWord = true;
        }
    }
    return words;
}


static QString detectLineEnding(const QByteArray& data)
{
    if (data.contains("\r\n"))
        return QStringLiteral("\r\n");
    if (data.contains('\r'))
        return QStringLiteral("\r");
    return QStringLiteral("\n");
}

static QString normalizeLoadedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

static QString decodeUtf8Text(const QByteArray& raw)
{
    QString text = QString::fromUtf8(raw);
    if (!text.isEmpty() && text.front() == QChar(0xFEFF))
        text.removeFirst();
    return text;
}

static QByteArray encodeWithLineEnding(QString text, const QString& lineEnding)
{
    if (!lineEnding.isEmpty() && lineEnding != QStringLiteral("\n")) {
        text.replace(QStringLiteral("\n"), lineEnding);
    }
    return text.toUtf8();
}

static QString normalizeMarkdownSpacing(const QString& text)
{
    QStringList lines = text.split('\n');
    QStringList out;
    out.reserve(lines.size());

    bool inFence = false;
    QChar fenceChar;
    int blankRun = 0;

    for (const QString& line : lines) {
        int pos = 0;
        while (pos < line.size() && (line.at(pos) == ' ' || line.at(pos) == '\t'))
            ++pos;

        int fenceCount = 0;
        if (pos < line.size() && (line.at(pos) == '`' || line.at(pos) == '~')) {
            fenceChar = line.at(pos);
            while (pos + fenceCount < line.size() && line.at(pos + fenceCount) == fenceChar)
                ++fenceCount;
        }

        if (fenceCount >= 3) {
            if (!inFence) {
                inFence = true;
            } else if (line.at(pos) == fenceChar) {
                inFence = false;
            }
            blankRun = 0;
            out.append(line);
            continue;
        }

        if (inFence) {
            out.append(line);
            continue;
        }

        if (line.trimmed().isEmpty()) {
            ++blankRun;
            if (blankRun == 1)
                out.append(QString());
            continue;
        }

        blankRun = 0;
        out.append(line);
    }

    return out.join('\n');
}

static bool readTextFile(const QString& path, QString* content, QString* lineEnding)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = f.readAll();
    if (lineEnding)
        *lineEnding = detectLineEnding(raw);
    if (content) {
        *content = decodeUtf8Text(raw);
        content->replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        content->replace(QLatin1Char('\r'), QLatin1Char('\n'));
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("FastMD"));
    setMinimumSize(800, 500);
    resize(1200, 750);

    FASTMD_MARK("  ctor: begin");
    m_watcher    = new QFileSystemWatcher(this);
    FASTMD_MARK("  ctor: watcher");
    // m_findDialog is created lazily on first Find/Replace — see findDialog().
    FASTMD_MARK("  ctor: findDialog (deferred)");

    createWorkspacePanel();
    FASTMD_MARK("  ctor: createWorkspacePanel");
    createMenus();
    FASTMD_MARK("  ctor: createMenus");
    createToolbar();
    FASTMD_MARK("  ctor: createToolbar");
    createStatusBar();
    FASTMD_MARK("  ctor: createStatusBar");
    readSettings();
    FASTMD_MARK("  ctor: readSettings");
    setWorkspaceTreeVisible(m_workspaceTreeVisible, false);
    applyTheme(m_theme);
    FASTMD_MARK("  ctor: applyTheme");

    connect(m_tabs, &TabWidget::editorActivated, this, &MainWindow::onEditorActivated);
    connect(m_tabs, &TabWidget::tabClosed,       this, &MainWindow::onTabCloseRequested);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileChanged);

    setAcceptDrops(true);

    if (m_restoreSessionOnStartup) {
        QTimer::singleShot(0, this, [this]() { restorePreviousSession(); });
    } else {
        m_tabs->addNewTab();
        FASTMD_MARK("  ctor: first tab created");
        updateWorkspaceTreeRoot();
        syncCurrentTabUi();
        FASTMD_MARK("  ctor: first tab UI synced");
    }
}

// ---------------------------------------------------------------------------
void MainWindow::createWorkspacePanel()
{
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(false);

    m_workspacePanel = new QWidget(m_mainSplitter);
    auto* panelLayout = new QVBoxLayout(m_workspacePanel);
    panelLayout->setContentsMargins(4, 4, 0, 4);
    panelLayout->setSpacing(0);

    m_workspaceTree = new QTreeView(m_workspacePanel);
    m_workspaceTree->setHeaderHidden(true);
    m_workspaceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_workspaceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_workspaceTree->setUniformRowHeights(true);
    m_workspaceTree->setAnimated(false);
    m_workspaceTree->setTextElideMode(Qt::ElideMiddle);
    m_workspaceTree->setIndentation(16);
    m_workspaceTree->setRootIsDecorated(true);
    m_workspaceTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // The QFileSystemModel is NOT created here: constructing it spins up a
    // background file-info-gatherer thread and initializes the Windows shell
    // icon provider (~100 ms), yet the workspace tree is hidden by default.
    // It is created lazily by ensureWorkspaceModel() the first time the tree
    // is actually shown. The tree's signal handlers below all null-guard the
    // model, so they are safe before it exists.
    connect(m_workspaceTree, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !m_workspaceModel || m_workspaceModel->isDir(index))
            return;
        openFilePath(m_workspaceModel->filePath(index));
    });
    connect(m_workspaceTree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !m_workspaceModel)
            return;
        rememberWorkspaceExpansion(m_workspaceModel->filePath(index), true);
    });
    connect(m_workspaceTree, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !m_workspaceModel)
            return;
        rememberWorkspaceExpansion(m_workspaceModel->filePath(index), false);
    });

    panelLayout->addWidget(m_workspaceTree, 1);

    m_workspaceToggle = new QToolButton(this);
    m_workspaceToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_workspaceToggle->setAutoRaise(true);
    m_workspaceToggle->setCheckable(true);
    m_workspaceToggle->setCursor(Qt::PointingHandCursor);
    m_workspaceToggle->setFixedHeight(28);
    m_workspaceToggle->setToolTip(tr("Workspace Tree"));
    connect(m_workspaceToggle, &QToolButton::toggled, this, [this](bool checked) {
        setWorkspaceTreeVisible(checked);
    });

    m_tabs = new TabWidget(m_mainSplitter);
    m_tabs->setCornerWidget(m_workspaceToggle, Qt::TopLeftCorner);
    m_workspaceToggle->installEventFilter(m_tabs);

    m_mainSplitter->addWidget(m_workspacePanel);
    m_mainSplitter->addWidget(m_tabs);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({ 220, 980 });
    setCentralWidget(m_mainSplitter);
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------
void MainWindow::createMenus()
{
    // ---- File ----
    QMenu* file = menuBar()->addMenu(tr("&File"));

    auto mkAct = [&](QMenu* m, const QString& text, QKeySequence sc, auto* recv, auto slot) -> QAction* {
        auto* a = new QAction(text, this);
        if (!sc.isEmpty()) a->setShortcut(sc);
        connect(a, &QAction::triggered, recv, slot);
        m->addAction(a);
        return a;
    };

    auto* actNew   = mkAct(file, tr("&New"),        QKeySequence::New,    this, &MainWindow::newFile);
    auto* actOpen  = mkAct(file, tr("&Open..."),    QKeySequence::Open,   this, &MainWindow::openFile);
    file->addSeparator();
    auto* actSave    = mkAct(file, tr("&Save"),         QKeySequence::Save,                            this, &MainWindow::saveFile);
    auto* actSaveAs  = mkAct(file, tr("Save &As…"),     QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),  this, &MainWindow::saveFileAs);
    auto* actSaveAll = mkAct(file, tr("Save A&ll"),     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::saveAll);
    file->addSeparator();
    mkAct(file, tr("Close &Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        if (m_tabs->count() > 0)
            onTabCloseRequested(m_tabs->currentIndex());
    });
    file->addSeparator();

    m_recentMenu = file->addMenu(tr("&Recent Files"));
    mkAct(file, tr("Clear Recent Files"), {}, this, &MainWindow::clearRecentFiles);
    file->addSeparator();

    mkAct(file, tr("Export &HTML…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this, &MainWindow::doExportHtml);
    mkAct(file, tr("Export &PDF…"),  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::doExportPdf);
    mkAct(file, tr("PDF Page Set&up…"), {}, this, &MainWindow::editPdfPageSetup);
    file->addSeparator();
    mkAct(file, tr("Preferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, &MainWindow::openPreferences);
    file->addSeparator();
    mkAct(file, tr("E&xit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);

    Q_UNUSED(actNew); Q_UNUSED(actOpen); Q_UNUSED(actSave); Q_UNUSED(actSaveAll); Q_UNUSED(actSaveAs);

    // ---- Edit ----
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    // Placeholder actions wired to editor below
    auto* actUndo  = new QAction(tr("&Undo"),  this); actUndo->setShortcut(QKeySequence::Undo);   edit->addAction(actUndo);
    auto* actRedo  = new QAction(tr("&Redo"),  this); actRedo->setShortcut(QKeySequence::Redo);   edit->addAction(actRedo);
    edit->addSeparator();
    auto* actCut   = new QAction(tr("Cu&t"),   this); actCut->setShortcut(QKeySequence::Cut);     edit->addAction(actCut);
    auto* actCopy  = new QAction(tr("&Copy"),  this); actCopy->setShortcut(QKeySequence::Copy);   edit->addAction(actCopy);
    auto* actPaste = new QAction(tr("&Paste"), this); actPaste->setShortcut(QKeySequence::Paste); edit->addAction(actPaste);
    edit->addSeparator();
    mkAct(edit, tr("&Find…"),    QKeySequence::Find,    this, &MainWindow::openFind);
    mkAct(edit, tr("&Replace…"), QKeySequence::Replace, this, &MainWindow::openReplace);

    // wire undo/redo/cut/copy/paste to the active editor
    auto wireEdit = [this](QAction* a, auto fn) {
        connect(a, &QAction::triggered, this, [this, fn]() {
            if (m_activeEditor) (m_activeEditor->*fn)();
        });
    };
    wireEdit(actUndo,  &QPlainTextEdit::undo);
    wireEdit(actRedo,  &QPlainTextEdit::redo);
    wireEdit(actCut,   &QPlainTextEdit::cut);
    wireEdit(actCopy,  &QPlainTextEdit::copy);
    wireEdit(actPaste, &QPlainTextEdit::paste);

    // ---- View ----
    QMenu* view = menuBar()->addMenu(tr("&View"));

    m_actTogglePreview = view->addAction(tr("&Preview"), this, &MainWindow::togglePreview);
    m_actTogglePreview->setCheckable(true);
    m_actTogglePreview->setChecked(true);
    m_actTogglePreview->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));

    view->addSeparator();
    QAction* actZoomIn = view->addAction(tr("Zoom In"), this, &MainWindow::zoomIn);
    actZoomIn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    QAction* actZoomOut = view->addAction(tr("Zoom Out"), this, &MainWindow::zoomOut);
    actZoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));

    view->addSeparator();
    QMenu* themeMenu = view->addMenu(tr("&Theme"));
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
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates ..."), this, &MainWindow::checkForUpdates);
    help->addSeparator();
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
    help->addAction(tr("About FastMD"), this, [this] {
        QMessageBox::about(this, tr("About FastMD"),
            tr("<p style=\"margin: 0; margin-bottom: 4px;\"><b>FastMD</b> v1.6.1</p>"
               "<p style=\"margin: 0; margin-bottom: 6px;\">A lightweight Markdown Text Editor built with C++20 and Qt 6.</p>"
               "<p style=\"margin: 0; margin-bottom: 4px;\">Copyright &copy; 2026 Skypedia</p>"
               "<p style=\"margin: 0;\">Licensed under the MIT License. This software is provided "
               "\"as is\", without warranty of any kind.</p>"));
    });
}

// ---------------------------------------------------------------------------
void MainWindow::createToolbar()
{
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->setIconSize(QSize(24, 24));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Tooltip helper
    auto tip = [](const QString& t, QKeySequence sc) -> QString {
        return sc.isEmpty() ? t : t + QStringLiteral("  (") + sc.toString(QKeySequence::NativeText) + QStringLiteral(")");
    };

    // Register a Material-icon action and add to the toolbar
    auto addMat = [&](QChar code, const QString& tooltip, QKeySequence sc,
                      auto* recv, auto slot, bool bindShortcut = true) -> QAction* {
        auto* a = new QAction(tooltip, this);
        a->setToolTip(tip(tooltip, sc));
        if (!sc.isEmpty() && bindShortcut) a->setShortcut(sc);
        connect(a, &QAction::triggered, recv, slot);
        m_toolbar->addAction(a);
        m_tbIcons.append({a, false, code, {}});
        return a;
    };

    // Register a text-icon action (H1, H2, H3)
    auto addTxt = [&](const QString& text, const QString& tooltip, QKeySequence sc,
                      void (MarkdownEditor::*slot)(), bool bindShortcut = true) -> QAction* {
        auto* a = new QAction(tooltip, this);
        a->setToolTip(tip(tooltip, sc));
        if (!sc.isEmpty() && bindShortcut) a->setShortcut(sc);
        connect(a, &QAction::triggered, this, [this, slot]() {
            if (m_activeEditor) (m_activeEditor->*slot)();
        });
        m_toolbar->addAction(a);
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
    // note_add=0xE89C  folder_open=0xE2C8  save=0xE161 picture_as_pdf=0xE415 public=0xE80B
    addMat(QChar(0xE89C), tr("New"), QKeySequence::New, this, &MainWindow::newFile, false);

    // Open button + chevron as a paired widget (avoids QToolButton stylesheet hacks)
    {
        auto* container = new QWidget(m_toolbar);
        auto* hbox = new QHBoxLayout(container);
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(0);

        m_openButton = new QToolButton(container);
        m_openButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_openButton->setIconSize(QSize(24, 24));
        m_openButton->setAutoRaise(true);
        m_openButton->setToolTip(tip(tr("Open"), QKeySequence::Open));
        m_openButton->setStyleSheet("QToolButton { border-top-right-radius: 0px; border-bottom-right-radius: 0px; padding-right: 2px; }");
        connect(m_openButton, &QToolButton::clicked, this, &MainWindow::openFile);
        hbox->addWidget(m_openButton);

        m_recentChevron = new QToolButton(container);
        m_recentChevron->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_recentChevron->setIconSize(QSize(20, 20));
        m_recentChevron->setAutoRaise(true);
        m_recentChevron->setFixedWidth(24);
        m_recentChevron->setStyleSheet("QToolButton { border-top-left-radius: 0px; border-bottom-left-radius: 0px; min-width: 24px; padding-left: 2px; padding-right: 2px; }");
        m_recentChevron->setToolTip(tr("Recent Files"));
        connect(m_recentChevron, &QToolButton::clicked, this, [this]() {
            m_recentMenu->popup(
                m_recentChevron->mapToGlobal(QPoint(0, m_recentChevron->height())));
        });
        hbox->addWidget(m_recentChevron);

        m_toolbar->addWidget(container);
    }
    addMat(QChar(0xE161), tr("Save"),      QKeySequence::Save,                            this, &MainWindow::saveFile, false);
    // addMat(QChar(0xE18F), tr("Save All"),  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::saveAll, false);
    addMat(QChar(0xE80B), tr("Export HTML"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this, &MainWindow::doExportHtml, false);
    addMat(QChar(0xE415), tr("Export PDF"),  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::doExportPdf, false);
    addMat(QChar(0xE89E), tr("Preview in Browser"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P), this, &MainWindow::doPreviewBrowser, true);
    m_toolbar->addSeparator();

    // ── Inline formatting ─────────────────────────────────────
    // format_bold=0xE238  format_italic=0xE23C  format_strikethrough=0xE257
    addFmtMat(QChar(0xE238), tr("Bold"),          QKeySequence(Qt::CTRL | Qt::Key_B),         &MarkdownEditor::fmtBold);
    addFmtMat(QChar(0xE23C), tr("Italic"),        QKeySequence(Qt::CTRL | Qt::Key_I),         &MarkdownEditor::fmtItalic);
    addFmtMat(QChar(0xE257), tr("Strikethrough"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), &MarkdownEditor::fmtStrike);
    m_toolbar->addSeparator();

    // ── Headings (text icons) ─────────────────────────────────
    addTxt(QStringLiteral("H1"), tr("Heading 1"), QKeySequence(Qt::CTRL | Qt::Key_1), &MarkdownEditor::fmtH1);
    addTxt(QStringLiteral("H2"), tr("Heading 2"), QKeySequence(Qt::CTRL | Qt::Key_2), &MarkdownEditor::fmtH2);
    addTxt(QStringLiteral("H3"), tr("Heading 3"), QKeySequence(Qt::CTRL | Qt::Key_3), &MarkdownEditor::fmtH3);
    m_toolbar->addSeparator();

    // ── Block formatting ──────────────────────────────────────
    // format_list_bulleted=0xE241  format_list_numbered=0xE242
    // checklist=0xE6B1  format_quote=0xE244
    addFmtMat(QChar(0xE241), tr("Bullet list"),   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U), &MarkdownEditor::fmtBulletList);
    addFmtMat(QChar(0xE242), tr("Numbered list"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), &MarkdownEditor::fmtNumberList);
    addFmtMat(QChar(0xE6B1), tr("Checklist"),     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), &MarkdownEditor::fmtChecklist);
    addFmtMat(QChar(0xE244), tr("Blockquote"),    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), &MarkdownEditor::fmtQuote);
    m_toolbar->addSeparator();

    // ── Inline code, table, link, image ──────────────────────
    // code=0xE86F  border_all=0xE228  link=0xE157  image=0xE3F4
    addFmtMat(QChar(0xE86F), tr("Inline code"),  QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), &MarkdownEditor::fmtCode);
    // ── Math ──────────────────────────────────────────────────
    // functions=0xE24A  integration=0xF768 (fallback: calculate=0xEA5F)
    addFmtMat(QChar(0xEA5F), tr("Inline math"),  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), &MarkdownEditor::fmtMathInline);
    addFmtMat(QChar(0xE24A), tr("Block math"),   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), &MarkdownEditor::fmtMathBlock);
    
    // Table button — shows a size picker popup
    {
        m_tableButton = new QToolButton(m_toolbar);
        m_tableButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_tableButton->setIconSize(QSize(24, 24));
        m_tableButton->setAutoRaise(true);
        m_tableButton->setToolTip(tip(tr("Table"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T)));

        auto showTablePicker = [this]() {
            if (!m_activeEditor) return;
            auto* picker = new TableSizePicker([this](int rows, int cols) {
                if (m_activeEditor) m_activeEditor->fmtTable(rows, cols);
            }, m_tableButton);
            QPoint pos = m_tableButton->mapToGlobal(QPoint(0, m_tableButton->height()));
            picker->move(pos);
            picker->show();
        };

        connect(m_tableButton, &QToolButton::clicked, this, showTablePicker);

        // Keyboard shortcut
        m_tableShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), this);
        connect(m_tableShortcut, &QShortcut::activated, this, showTablePicker);

        m_toolbar->addWidget(m_tableButton);
    }
    addFmtMat(QChar(0xE157), tr("Link"),         QKeySequence(Qt::CTRL | Qt::Key_K),         &MarkdownEditor::fmtLink);
    m_formatActions.append(
        addMat(QChar(0xE3F4), tr("Image"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this, [this]() {
            if (m_activeEditor) {
                TabPage* pg = m_tabs->currentPage();
                m_activeEditor->fmtImage(pg ? pg->model->filePath() : QString());
            }
        }, true));
    m_toolbar->addSeparator();

    // m_toolbar->addSeparator();

    // ── Find / Preview ────────────────────────────────────────
    // search=0xE8B6  find_replace=0xE881  visibility=0xE8F4  open_in_new=0xE89E
    addMat(QChar(0xE8B6), tr("Find"), QKeySequence::Find, this, &MainWindow::openFind, false);
    // Shortcut shown in the tooltip only; QKeySequence::Replace is already bound
    // to the Edit ▸ Replace menu action (avoids an ambiguous-shortcut warning).
    addMat(QChar(0xE881), tr("Find and Replace"), QKeySequence::Replace, this, &MainWindow::openReplace, false);


    m_actToolbarPreview = new QAction(this);
    m_actToolbarPreview->setCheckable(true);
    m_actToolbarPreview->setChecked(true);
    m_actToolbarPreview->setToolTip(tip(tr("Toggle preview"), QKeySequence(Qt::CTRL | Qt::Key_P)));
    // Shortcut omitted here to prevent ambiguous shortcut error with View -> Preview
    connect(m_actToolbarPreview, &QAction::triggered, this, &MainWindow::togglePreview);
    m_toolbar->addAction(m_actToolbarPreview);
    m_tbIcons.append({m_actToolbarPreview, false, QChar(0xE8F4), {}});

    m_toolbar->addSeparator();

    m_actModeToggle = new QAction(this);
    m_actModeToggle->setCheckable(true);
    m_actModeToggle->setChecked(true);
    m_actModeToggle->setText(tr("Markdown"));
    m_actModeToggle->setToolTip(tip(tr("Switch editor mode"), QKeySequence()));
    connect(m_actModeToggle, &QAction::triggered, this, &MainWindow::setEditorMode);
    m_toolbar->addAction(m_actModeToggle);
    m_tbIcons.append({m_actModeToggle, false, QChar(0xE873), {}});

    if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(m_actModeToggle))) {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setProperty("modeToggle", true);
        button->setAutoRaise(false);
        button->setCursor(Qt::PointingHandCursor);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::updateToolbarIcons(bool dark)
{
    QColor iconColor = dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
    constexpr int sz = 24;
    for (const TbIcon& def : std::as_const(m_tbIcons)) {
        if (def.isText)
            def.action->setIcon(IconHelper::textIcon(def.text, iconColor, sz));
        else
            def.action->setIcon(IconHelper::materialIcon(def.code, iconColor, sz));
    }
    if (m_tableButton)
        m_tableButton->setIcon(IconHelper::materialIcon(QChar(0xE228), iconColor, sz));
    if (m_openButton)
        m_openButton->setIcon(IconHelper::materialIcon(QChar(0xE2C8), iconColor, sz));
    if (m_recentChevron)
        m_recentChevron->setIcon(IconHelper::materialIcon(QChar(0xE5C5), iconColor, 20));
    if (m_workspaceToggle) {
        updateWorkspaceToggleIcon();
    }
}

// ---------------------------------------------------------------------------
void MainWindow::createStatusBar()
{
    m_lblPosition = new QLabel(tr("Line: 1, Col: 1"));
    m_lblWords    = new QLabel(tr("Words: 0"));
    m_lblMode     = new QLabel(tr("Markdown"));
    m_lblEncoding = new QLabel(tr("UTF-8"));

    statusBar()->addPermanentWidget(m_lblWords);
    statusBar()->addPermanentWidget(m_lblMode);
    statusBar()->addPermanentWidget(m_lblEncoding);
    statusBar()->addWidget(m_lblPosition);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
void MainWindow::applyTheme(const QString& theme)
{
    m_theme = theme;
    bool dark = false;

    if (theme == QStringLiteral("dark")) {
        dark = true;
    } else if (theme == QStringLiteral("system")) {
        dark = (QApplication::palette().color(QPalette::Window).lightness() < 128);
    }

    // Apply QSS stylesheet
    qApp->setStyleSheet(dark ? AppStyle::dark() : AppStyle::light());

    // Sync content area palette so Qt's internal painting matches
    QPalette p = QApplication::style()->standardPalette();
    if (dark) {
        p.setColor(QPalette::Window,          QColor(0x2d, 0x2d, 0x2d));
        p.setColor(QPalette::WindowText,      QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Base,            QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::AlternateBase,   QColor(0x2a, 0x2a, 0x2a));
        p.setColor(QPalette::Text,            QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Button,          QColor(0x3c, 0x3c, 0x3c));
        p.setColor(QPalette::ButtonText,      QColor(0xd4, 0xd4, 0xd4));
        p.setColor(QPalette::Highlight,       QColor(0x26, 0x4f, 0x78));
        p.setColor(QPalette::HighlightedText, Qt::white);
    }
    QApplication::setPalette(p);

    m_tabs->setDarkMode(dark);
    updateToolbarIcons(dark);

    for (QAction* a : m_themeGroup->actions())
        a->setChecked(a->text().toLower().contains(theme));

    if (m_workspaceToggle && m_activeEditor) {
        m_workspaceToggle->setFixedWidth(m_activeEditor->lineNumberWidth());
    }

    if (m_tabs)
        QTimer::singleShot(0, m_tabs, &TabWidget::updateTabBarLayout);
}

void MainWindow::setTheme(const QString& theme)
{
    applyTheme(theme);
    writeSettings();
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------
void MainWindow::newFile()
{
    m_tabs->addNewTab();
    syncCurrentTabUi();
    updateWorkspaceTreeRoot();
}

void MainWindow::openFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open Files"),
        m_lastOpenFolder,
        tr("Markdown (*.md *.markdown *.txt);;All Files (*)"));

    for (const QString& p : paths)
        openFilePath(p);
}

void MainWindow::openFilePath(const QString& path)
{
    const EditorMode targetMode = DocumentModel::modeForPath(path);

    // check if already open
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* pg = m_tabs->pageAt(i)) {
            if (pg->model->filePath() == path) {
                pg->model->setEditorMode(targetMode);
                m_tabs->setCurrentIndex(i);
                m_htmlDirty = true;
                m_previewDirty = true;
                syncCurrentTabUi();
                updateWorkspaceTreeRoot();
                return;
            }
        }
    }

    if (addTabFromFile(path, true, true, true, nullptr) < 0)
        return;

    m_lastOpenFolder = QFileInfo(path).absolutePath();
    updateWindowTitle();
    syncCurrentTabUi();
    updateWorkspaceTreeRoot();
}

int MainWindow::addTabFromFile(const QString& path, bool activate, bool addToRecent,
                               bool showError, const SessionTabState* state)
{
    QString content;
    QString lineEnding;
    if (!loadFileText(path, &content, &lineEnding)) {
        if (showError) {
            QMessageBox::warning(this, tr("Open Failed"),
                tr("Cannot read file:\n%1").arg(path));
        }
        return -1;
    }

    int idx = m_tabs->addNewTab(path, activate);
    if (TabPage* pg = m_tabs->pageAt(idx)) {
        pg->model->setLineEnding(lineEnding);
        pg->editor->setPlainText(content);
        pg->editor->document()->setModified(false);
        pg->model->setDirty(false);
        m_tabs->refreshTitle(idx);
        if (state)
            restoreTabState(pg, *state);
    }

    if (addToRecent)
        this->addToRecent(path);
    watchFile(path);

    return idx;
}

bool MainWindow::loadFileText(const QString& path, QString* content, QString* lineEnding) const
{
    return readTextFile(path, content, lineEnding);
}

void MainWindow::restoreTabState(TabPage* page, const SessionTabState& state)
{
    if (!page)
        return;

    const bool markdown = state.mode == EditorMode::Markdown;
    page->model->setEditorMode(state.mode);
    page->editor->setMarkdownMode(markdown);
    page->model->setMarkdownPreviewVisible(markdown ? state.previewVisible : false);
    page->preview->setVisible(markdown && state.previewVisible);

    if (markdown) {
        page->preview->setBasePath(page->model->filePath());
        page->preview->setBodyHtml(ExportManager::markdownToHtml(page->editor->toPlainText()));
        page->preview->verticalScrollBar()->setValue(qMax(0, state.previewScroll));
    }

    QTimer::singleShot(0, page->editor, [this, editor = page->editor, cursorPos = state.cursorPos, editorScroll = state.editorScroll]() {
        if (!editor)
            return;
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(qBound(0, cursorPos, editor->toPlainText().size()));
        editor->setTextCursor(cursor);
        editor->verticalScrollBar()->setValue(qMax(0, editorScroll));
        if (editor == m_activeEditor)
            onCursorPositionChanged();
    });
}

void MainWindow::restorePreviousSession()
{
    if (!m_restoreSessionOnStartup) {
        if (m_tabs->count() == 0)
            m_tabs->addNewTab();
        updateWorkspaceTreeRoot();
        syncCurrentTabUi();
        return;
    }

    QVector<int> restoredMap(m_sessionTabs.size(), -1);
    int restoredCount = 0;
    bool needsManualActivation = false;

    {
        QSignalBlocker blockTabs(m_tabs);
        for (int i = 0; i < m_sessionTabs.size(); ++i) {
            const SessionTabState& state = m_sessionTabs.at(i);
            if (state.path.isEmpty() || !QFile::exists(state.path))
                continue;

            const int idx = addTabFromFile(state.path, false, false, false, &state);
            if (idx >= 0) {
                restoredMap[i] = idx;
                ++restoredCount;
            }
        }

        int activeIndex = -1;
        if (m_sessionActiveTab >= 0 && m_sessionActiveTab < restoredMap.size())
            activeIndex = restoredMap.at(m_sessionActiveTab);
        if (activeIndex < 0) {
            for (int idx : restoredMap) {
                if (idx >= 0) {
                    activeIndex = idx;
                    break;
                }
            }
        }
        if (activeIndex < 0)
            activeIndex = 0;

        if (restoredCount > 0) {
            m_tabs->setCurrentIndex(activeIndex);
            needsManualActivation = true;
        }
    }

    if (restoredCount == 0) {
        m_tabs->addNewTab();
    } else if (needsManualActivation) {
        if (TabPage* pg = m_tabs->currentPage()) {
            onEditorActivated(pg->editor, pg->preview);
        }
    }
    updateWorkspaceTreeRoot();
}

bool MainWindow::saveDocument(int tabIndex, bool forceDialog)
{
    TabPage* pg = m_tabs->pageAt(tabIndex);
    if (!pg) return false;

    const QString oldPath = pg->model->filePath();
    QString path = oldPath;

    if (forceDialog || path.isEmpty()) {
        const QString defaultExt = pg->model->editorMode() == EditorMode::PlainText
            ? QStringLiteral(".txt")
            : QStringLiteral(".md");
        QString initialDir;
        if (!m_defaultSaveFolder.isEmpty())
            initialDir = m_defaultSaveFolder;
        else if (!m_lastSaveFolder.isEmpty())
            initialDir = m_lastSaveFolder;
        const QString initialPath = path.isEmpty()
            ? (initialDir.isEmpty() ? QString() : initialDir + QStringLiteral("/"))
              + pg->model->displayName() + defaultExt
            : path;
        path = QFileDialog::getSaveFileName(
            this, tr("Save File"),
            initialPath,
            tr("Text/Markdown (*.txt *.md *.markdown);;All Files (*)"));
        if (path.isEmpty()) return false;
    }

    if (!oldPath.isEmpty() && m_watcher->files().contains(oldPath))
        m_watcher->removePath(oldPath);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (!oldPath.isEmpty())
            watchFile(oldPath);
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Cannot write file:\n%1").arg(path));
        return false;
    }

    QString text = pg->editor->toPlainText();
    if (pg->model->editorMode() == EditorMode::Markdown)
        text = normalizeMarkdownSpacing(text);

    if (text != pg->editor->toPlainText()) {
        QTextCursor cursor = pg->editor->textCursor();
        const int oldPos = cursor.position();
        const int oldAnchor = cursor.anchor();
        pg->editor->setPlainText(text);
        QTextCursor updated = pg->editor->textCursor();
        updated.setPosition(qMin(oldAnchor, text.size()));
        updated.setPosition(qMin(oldPos, text.size()), QTextCursor::KeepAnchor);
        pg->editor->setTextCursor(updated);
    }

    const QByteArray bytes = encodeWithLineEnding(text, pg->model->lineEnding());
    if (f.write(bytes) != bytes.size()) {
        if (!oldPath.isEmpty())
            watchFile(oldPath);
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Cannot write file:\n%1").arg(path));
        return false;
    }
    f.close();

    pg->model->setFilePath(path);
    pg->editor->setDocumentPath(path);
    m_lastSaveFolder = QFileInfo(path).absolutePath();
    pg->model->setDirty(false);
    m_tabs->refreshTitle(tabIndex);
    addToRecent(path);
    m_ignoredFileChanges.removeAll(path);
    watchFile(path);
    updateWindowTitle();
    updateWorkspaceTreeRoot();

    if (tabIndex == m_tabs->currentIndex()) {
        updatePreview();
    }
    
    return true;
}

void MainWindow::saveFile()
{
    saveDocument(m_tabs->currentIndex(), false);
}

void MainWindow::saveAll()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        saveDocument(i, false);
}

void MainWindow::saveFileAs()
{
    saveDocument(m_tabs->currentIndex(), true);
}

QString MainWindow::exportInitialPath(const QString& extension) const
{
    TabPage* pg = m_tabs->currentPage();
    const QString docPath = pg ? pg->model->filePath() : QString();

    // Saved document: same folder, same base name, swapped extension.
    if (!docPath.isEmpty()) {
        const QFileInfo fi(docPath);
        return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + extension;
    }

    // Unsaved: use the tab title in the default (or last used) export folder.
    const QString name = pg ? pg->model->displayName() : QStringLiteral("Untitled");
    QString dir = !m_defaultSaveFolder.isEmpty() ? m_defaultSaveFolder
                : m_lastExportFolder;
    return (dir.isEmpty() ? QString() : dir + QLatin1Char('/')) + name + extension;
}

void MainWindow::doExportHtml()
{
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export HTML"), exportInitialPath(QStringLiteral(".html")),
        tr("HTML (*.html *.htm);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();
    // Regenerate from raw markdown via the KaTeX path — never the preview body.
    if (!ExportManager::exportHtml(markdown, path, m_theme == QStringLiteral("dark")))
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write %1").arg(path));
}

void MainWindow::doExportPdf()
{
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), exportInitialPath(QStringLiteral(".pdf")),
        tr("PDF (*.pdf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();

    TabPage* pg = m_tabs->currentPage();
    QString docPath = pg ? pg->model->filePath() : QString();

    // exportPdf surfaces its own (specific) error dialogs for the browser path.
    ExportManager::exportPdf(markdown, path, docPath, m_pdfExportOptions, this);
}

void MainWindow::editPdfPageSetup()
{
    QPrinter printer(QPrinter::HighResolution);
    applyPdfExportOptions(printer, m_pdfExportOptions);

    QPageSetupDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("PDF Page Setup"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_pdfExportOptions = pdfExportOptionsFromPrinter(printer, m_pdfExportOptions.fontSize);
    writeSettings();
}

void MainWindow::openPreferences()
{
    PreferencesDialog dlg(this);
    dlg.setRestoreSessionOnStartup(m_restoreSessionOnStartup);
    dlg.setCloseAppOnLastTabClose(m_closeAppOnLastTabClose);
    dlg.setDefaultSaveFolder(m_defaultSaveFolder);
    if (dlg.exec() == QDialog::Accepted) {
        m_restoreSessionOnStartup = dlg.restoreSessionOnStartup();
        m_closeAppOnLastTabClose = dlg.closeAppOnLastTabClose();
        m_defaultSaveFolder = dlg.defaultSaveFolder();
        writeSettings();
    }
}

void MainWindow::doPreviewBrowser()
{
    if (!m_activeEditor) return;
    // Regenerate from raw markdown via the KaTeX path so the browser renders real
    // LaTeX math, not the in-app Unicode fallback.
    const QString markdown = m_activeEditor->toPlainText();
    QString tempPath = QDir::tempPath() + QStringLiteral("/fastmd_preview.html");
    if (ExportManager::exportHtml(markdown, tempPath, m_theme == QStringLiteral("dark"))) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::zoomIn()
{
    if (m_activeEditor) m_activeEditor->zoomIn(1);
    if (m_activePreview) m_activePreview->zoomIn(1);
    if (m_activeEditor) {
        QSettings s;
        s.setValue(QStringLiteral("editorFontSize"), m_activeEditor->font().pixelSize());
    }
}

void MainWindow::zoomOut()
{
    if (m_activeEditor) m_activeEditor->zoomOut(1);
    if (m_activePreview) m_activePreview->zoomOut(1);
    if (m_activeEditor) {
        QSettings s;
        s.setValue(QStringLiteral("editorFontSize"), m_activeEditor->font().pixelSize());
    }
}

// ---------------------------------------------------------------------------
// Editor connection
// ---------------------------------------------------------------------------
void MainWindow::connectEditor(MarkdownEditor* editor, PreviewWidget* preview)
{
    if (!editor) return;
    connect(editor, &MarkdownEditor::contentReady,
            this,   &MainWindow::onContentReady);
    connect(editor, &QPlainTextEdit::cursorPositionChanged,
            this,   &MainWindow::onCursorPositionChanged);
    connect(editor, &MarkdownEditor::zoomRequested,
            this, [this](int delta) { if (delta > 0) zoomIn(); else zoomOut(); });
    if (preview) {
        connect(preview, &PreviewWidget::zoomRequested,
                this, [this](int delta) { if (delta > 0) zoomIn(); else zoomOut(); });

        // Bidirectional proportional scroll sync
        connect(editor->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this](int) {
            if (m_scrollSyncing || !m_activeEditor || !m_activePreview) return;
            QScrollBar* src = m_activeEditor->verticalScrollBar();
            QScrollBar* dst = m_activePreview->verticalScrollBar();
            int srcMax = src->maximum();
            if (srcMax == 0) return;
            m_scrollSyncing = true;
            dst->setValue(qRound(double(src->value()) / srcMax * dst->maximum()));
            m_scrollSyncing = false;
        });
        connect(preview->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this](int) {
            if (m_scrollSyncing || !m_activeEditor || !m_activePreview) return;
            if (m_activePreview->isRefreshing()) return;
            QScrollBar* src = m_activePreview->verticalScrollBar();
            QScrollBar* dst = m_activeEditor->verticalScrollBar();
            int srcMax = src->maximum();
            if (srcMax == 0) return;
            m_scrollSyncing = true;
            dst->setValue(qRound(double(src->value()) / srcMax * dst->maximum()));
            m_scrollSyncing = false;
        });
    }
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
        int idx = m_tabs->currentIndex();
        m_tabs->markDirty(idx, modified);
        updateWindowTitle();
    });
    if (m_findDialog)
        m_findDialog->setEditor(editor);
}

FindReplaceDialog* MainWindow::findDialog()
{
    if (!m_findDialog) {
        m_findDialog = new FindReplaceDialog(this);
        if (m_activeEditor)
            m_findDialog->setEditor(m_activeEditor);
    }
    return m_findDialog;
}

void MainWindow::disconnectEditor()
{
    if (m_activeEditor) {
        disconnect(m_activeEditor, nullptr, this, nullptr);
        disconnect(m_activeEditor->document(), nullptr, this, nullptr);
        disconnect(m_activeEditor->verticalScrollBar(), nullptr, this, nullptr);
    }
    if (m_activePreview) {
        disconnect(m_activePreview, nullptr, this, nullptr);
        disconnect(m_activePreview->verticalScrollBar(), nullptr, this, nullptr);
    }
}

void MainWindow::onEditorActivated(MarkdownEditor* editor, PreviewWidget* preview)
{
    disconnectEditor();
    m_activeEditor  = editor;
    m_activePreview = preview;
    m_htmlDirty = true;
    m_previewDirty = true;
    connectEditor(editor, preview);
    onCursorPositionChanged();
    updateWordCount();
    updateWindowTitle();
    syncCurrentTabUi();
    updateWorkspaceTreeRoot();

    if (m_lineNumberWidthConn) {
        disconnect(m_lineNumberWidthConn);
        m_lineNumberWidthConn = {};
    }

    if (editor && m_workspaceToggle) {
        const int w = editor->lineNumberWidth();
        m_workspaceToggle->setFixedWidth(w);
        m_lineNumberWidthConn = connect(editor, &MarkdownEditor::lineNumberWidthChanged, this, [this](int width) {
            if (!m_workspaceToggle)
                return;
            m_workspaceToggle->setFixedWidth(width);
            if (m_tabs)
                m_tabs->updateTabBarLayout();
        });
        if (m_tabs)
            m_tabs->updateTabBarLayout();
    }
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
void MainWindow::onContentReady()
{
    if (!m_activeEditor)
        return;

    const QString text = m_activeEditor->toPlainText();
    m_htmlDirty = true;
    m_previewDirty = true;
    updateWordCount(text);

    if (isPreviewVisible())
        updatePreview();
}

void MainWindow::updatePreview()
{
    if (!m_activeEditor || !m_activePreview) return;
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->editorMode() != EditorMode::Markdown || !pg->preview->isVisible())
        return;

    updateCurrentHtml();

    if (pg && pg->model) {
        m_activePreview->setBasePath(pg->model->filePath());
    }

    m_activePreview->setBodyHtml(m_currentHtml);
    m_previewDirty = false;
}

void MainWindow::updateCurrentHtml()
{
    if (!m_activeEditor || !m_htmlDirty)
        return;

    m_currentHtml = ExportManager::markdownToHtml(m_activeEditor->toPlainText());
    m_htmlDirty = false;
}

void MainWindow::togglePreview()
{
    setPreviewVisible(!isPreviewVisible());
}

void MainWindow::setPreviewVisible(bool visible, bool persist)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->editorMode() != EditorMode::Markdown)
        return;

    pg->model->setMarkdownPreviewVisible(visible);
    if (persist) {
        m_markdownPreviewVisible = visible;
        writeSettings();
    }

    syncCurrentTabUi();
    if (visible && m_previewDirty)
        updatePreview();
}

void MainWindow::setEditorMode(bool markdown)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg)
        return;

    const EditorMode mode = markdown ? EditorMode::Markdown : EditorMode::PlainText;
    if (pg->model->editorMode() == mode) {
        syncCurrentTabUi();
        return;
    }

    pg->model->setEditorMode(mode);
    pg->editor->setMarkdownMode(markdown);
    if (!markdown) {
        pg->preview->setVisible(false);
    }

    m_previewDirty = true;
    syncCurrentTabUi();
    if (markdown && isPreviewVisible())
        updatePreview();
}

void MainWindow::syncCurrentTabUi()
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg)
        return;

    const bool markdown = pg->model->editorMode() == EditorMode::Markdown;
    const bool previewVisible = markdown && pg->model->markdownPreviewVisible();

    if (m_lblMode)
        m_lblMode->setText(markdown ? tr("Markdown") : tr("Plain Text"));

    if (m_actModeToggle) {
        QSignalBlocker blocker(m_actModeToggle);
        m_actModeToggle->setChecked(markdown);
        m_actModeToggle->setText(markdown ? tr("Markdown") : tr("Plain Text"));
    }

    if (m_toolbar && m_actModeToggle) {
        if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(m_actModeToggle))) {
            button->setProperty("modeToggle", true);
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
    }

    if (m_actTogglePreview) {
        QSignalBlocker blocker(m_actTogglePreview);
        m_actTogglePreview->setChecked(previewVisible);
        m_actTogglePreview->setEnabled(markdown);
    }
    if (m_actToolbarPreview) {
        QSignalBlocker blocker(m_actToolbarPreview);
        m_actToolbarPreview->setChecked(previewVisible);
        m_actToolbarPreview->setEnabled(markdown);
    }

    // Markdown-syntax formatting actions are meaningless on .txt files: gray
    // them out (and their shortcuts) in Plain Text mode, re-enable in Markdown.
    for (QAction* a : m_formatActions) {
        if (a) a->setEnabled(markdown);
    }
    if (m_tableButton)
        m_tableButton->setEnabled(markdown);
    if (m_tableShortcut)
        m_tableShortcut->setEnabled(markdown);

    if (m_activeEditor)
        m_activeEditor->setMarkdownMode(markdown);

    if (pg->preview)
        pg->preview->setVisible(previewVisible);

    if (markdown && previewVisible && m_previewDirty)
        updatePreview();
}

// ---------------------------------------------------------------------------
// Status bar updates
// ---------------------------------------------------------------------------
void MainWindow::onCursorPositionChanged()
{
    if (!m_activeEditor) return;
    QTextCursor c = m_activeEditor->textCursor();
    int line = c.blockNumber() + 1;
    int col  = c.positionInBlock() + 1;
    m_lblPosition->setText(tr("Line: %1, Col: %2").arg(line).arg(col));
}

void MainWindow::onTextChanged()
{
    updateWordCount();
}

void MainWindow::updateWordCount(const QString& text)
{
    if (!m_activeEditor) return;
    int words = 0;
    if (text.isNull()) {
        const QString src = m_activeEditor->toPlainText();
        words = countWordsInText(QStringView(src));
    } else {
        words = countWordsInText(QStringView(text));
    }
    m_lblWords->setText(tr("Words: %1").arg(words));
}

bool MainWindow::isPreviewVisible() const
{
    TabPage* pg = m_tabs->currentPage();
    return pg && pg->model->editorMode() == EditorMode::Markdown
        && pg->model->markdownPreviewVisible();
}

void MainWindow::updateWindowTitle()
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg) {
        setWindowTitle(QStringLiteral("FastMD"));
        return;
    }
    QString name = pg->model->displayName();
    if (pg->model->isDirty()) name.prepend('*');
    setWindowTitle(name + QStringLiteral(" — FastMD"));
}

// ---------------------------------------------------------------------------
// Close handling
// ---------------------------------------------------------------------------
bool MainWindow::confirmClose(int tabIndex)
{
    TabPage* pg = m_tabs->pageAt(tabIndex);
    if (!pg || !pg->model->isDirty()) return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(
        tr("'%1' has unsaved changes.\nSave before closing?")
            .arg(pg->model->displayName()));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    if (auto* layout = qobject_cast<QGridLayout*>(box.layout())) {
        layout->setContentsMargins(16, 24, 16, 24);
        layout->setHorizontalSpacing(14);
        layout->setVerticalSpacing(6);
    }

    const auto btn = static_cast<QMessageBox::StandardButton>(box.exec());

    if (btn == QMessageBox::Save)
        return saveDocument(tabIndex, false);
    if (btn == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::onTabCloseRequested(int index)
{
    if (!confirmClose(index)) return;
    m_tabs->closeTabAt(index);
    if (m_tabs->count() == 0) {
        if (m_closeAppOnLastTabClose) {
            close();
            return;
        } else {
            newFile();
        }
    }
    updateWindowTitle();
    updateWorkspaceTreeRoot();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!confirmClose(i)) {
            event->ignore();
            return;
        }
    }
    writeSettings();
    event->accept();
}

// ---------------------------------------------------------------------------
// Find / Replace
// ---------------------------------------------------------------------------
void MainWindow::openFind()
{
    findDialog()->openFind();
}

void MainWindow::openReplace()
{
    findDialog()->openReplace();
}

// ---------------------------------------------------------------------------
// Recent files
// ---------------------------------------------------------------------------
void MainWindow::addToRecent(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > MAX_RECENT)
        m_recentFiles.removeLast();
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    for (const QString& p : m_recentFiles) {
        QAction* a = m_recentMenu->addAction(p, this, &MainWindow::openRecentFile);
        a->setData(p);
    }
    if (m_recentFiles.isEmpty()) {
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
    } else {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(tr("Clear Recent Files"), this, &MainWindow::clearRecentFiles);
    }
}

void MainWindow::openRecentFile()
{
    QAction* a = qobject_cast<QAction*>(sender());
    if (a) openFilePath(a->data().toString());
}

void MainWindow::clearRecentFiles()
{
    m_recentFiles.clear();
    rebuildRecentMenu();
    writeSettings();
}

// ---------------------------------------------------------------------------
// Workspace tree
// ---------------------------------------------------------------------------
QString MainWindow::workspaceRootForTabs() const
{
    QString fallbackRoot;

    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* pg = m_tabs->pageAt(i);
        if (!pg)
            continue;

        const QString path = pg->model->filePath();
        if (path.isEmpty())
            continue;

        const QString root = QFileInfo(path).absolutePath();
        if (i == m_tabs->currentIndex())
            return root;
        if (fallbackRoot.isEmpty())
            fallbackRoot = root;
    }

    if (fallbackRoot.isEmpty()) {
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    return fallbackRoot;
}

void MainWindow::rememberWorkspaceExpansion(const QString& path, bool expanded)
{
    if (path.isEmpty())
        return;

    if (expanded) {
        if (!m_workspaceExpandedPaths.contains(path))
            m_workspaceExpandedPaths.append(path);
    } else {
        m_workspaceExpandedPaths.removeAll(path);
    }
}

void MainWindow::restoreWorkspaceExpandedState()
{
    if (!m_workspaceTree || !m_workspaceModel || m_workspaceRootPath.isEmpty())
        return;

    QStringList paths = m_workspaceExpandedPaths;
    paths.removeDuplicates();
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
        if (a.size() != b.size())
            return a.size() < b.size();
        return a < b;
    });

    const QDir rootDir(m_workspaceRootPath);
    for (const QString& path : paths) {
        if (path.isEmpty())
            continue;
        const QString rel = rootDir.relativeFilePath(path);
        if (rel.startsWith(QStringLiteral("..")))
            continue;

        const QModelIndex idx = m_workspaceModel->index(path);
        if (idx.isValid())
            m_workspaceTree->expand(idx);
    }
}

void MainWindow::setWorkspaceTreeVisible(bool visible, bool persist)
{
    m_workspaceTreeVisible = visible;

    if (m_workspaceToggle) {
        const QSignalBlocker blocker(m_workspaceToggle);
        m_workspaceToggle->setChecked(visible);
    }

    updateWorkspaceToggleIcon();

    if (visible) {
        // First reveal lazily builds the filesystem model and sets the root.
        updateWorkspaceTreeRoot();
    } else if (m_workspacePanel) {
        m_workspacePanel->setVisible(false);
    }

    if (visible && !m_workspaceRootPath.isEmpty())
        QTimer::singleShot(0, this, [this]() { restoreWorkspaceExpandedState(); });

    if (persist)
        writeSettings();
}

void MainWindow::ensureWorkspaceModel()
{
    if (m_workspaceModel)
        return;

    m_workspaceModel = new QFileSystemModel(this);
    m_workspaceModel->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
    m_workspaceModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_workspaceModel->setReadOnly(true);
    m_workspaceTree->setModel(m_workspaceModel);
    m_workspaceTree->setSortingEnabled(true);
    m_workspaceTree->sortByColumn(0, Qt::AscendingOrder);

    connect(m_workspaceModel, &QFileSystemModel::directoryLoaded, this, [this](const QString& /*path*/) {
        if (m_workspaceTreeVisible) {
            restoreWorkspaceExpandedState();
        }
    });
}

void MainWindow::updateWorkspaceTreeRoot()
{
    if (!m_workspacePanel || !m_workspaceTree)
        return;

    const QString root = workspaceRootForTabs();
    if (root.isEmpty()) {
        m_workspaceRootPath.clear();
        m_workspacePanel->setVisible(false);
        return;
    }

    const bool rootChanged = (root != m_workspaceRootPath);
    m_workspaceRootPath = root;
    m_workspacePanel->setVisible(m_workspaceTreeVisible);

    // Only build the (expensive) filesystem model once the tree is visible.
    if (!m_workspaceTreeVisible)
        return;
    ensureWorkspaceModel();

    const QModelIndex rootIndex = m_workspaceModel->setRootPath(root);
    m_workspaceTree->setRootIndex(rootIndex);
    QModelIndex selectionIndex = rootIndex;
    if (TabPage* pg = m_tabs->currentPage()) {
        const QString currentPath = pg->model->filePath();
        if (!currentPath.isEmpty()) {
            const QString rel = QDir(root).relativeFilePath(currentPath);
            if (!rel.startsWith(QStringLiteral(".."))) {
                const QModelIndex currentIndex = m_workspaceModel->index(currentPath);
                if (currentIndex.isValid())
                    selectionIndex = currentIndex;
            }
        }
    }
    m_workspaceTree->setCurrentIndex(selectionIndex);

    if (rootChanged || m_workspaceTreeVisible)
        QTimer::singleShot(0, this, [this]() { restoreWorkspaceExpandedState(); });
}

void MainWindow::updateWorkspaceToggleIcon()
{
    if (!m_workspaceToggle)
        return;
    bool dark = false;
    if (m_theme == QStringLiteral("dark")) {
        dark = true;
    } else if (m_theme == QStringLiteral("system")) {
        dark = (QApplication::palette().color(QPalette::Window).lightness() < 128);
    }
    QColor iconColor = dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
    const QChar code = m_workspaceTreeVisible ? QChar(0xE2C8) : QChar(0xE2C7);
    m_workspaceToggle->setIcon(IconHelper::materialIcon(code, iconColor, 20));
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile()) openFilePath(url.toLocalFile());
}

// ---------------------------------------------------------------------------
// File system watcher (auto reload)
// ---------------------------------------------------------------------------
void MainWindow::watchFile(const QString& path)
{
    if (!path.isEmpty() && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
}

void MainWindow::onFileChanged(const QString& path)
{
    if (m_ignoredFileChanges.removeOne(path)) {
        if (QFile::exists(path) && !m_watcher->files().contains(path))
            m_watcher->addPath(path);
        return;
    }

    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* pg = m_tabs->pageAt(i);
        if (!pg || pg->model->filePath() != path) continue;

        auto btn = QMessageBox::question(
            this, tr("File Changed"),
            tr("'%1' was modified externally.\nReload?").arg(path),
            QMessageBox::Yes | QMessageBox::No);

        if (btn == QMessageBox::Yes) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray raw = f.readAll();
                pg->model->setLineEnding(detectLineEnding(raw));
                pg->editor->setPlainText(normalizeLoadedText(decodeUtf8Text(raw)));
                pg->model->setDirty(false);
                m_tabs->refreshTitle(i);
                updateWindowTitle();
            }
        }
        // re-add path since watcher may stop watching deleted/replaced files
        if (QFile::exists(path) && !m_watcher->files().contains(path))
            m_watcher->addPath(path);
        break;
    }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void MainWindow::readSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    m_recentFiles = s.value(QStringLiteral("recentFiles")).toStringList();
    m_theme = s.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    m_markdownPreviewVisible = s.value(QStringLiteral("previewVisible"), true).toBool();
    m_workspaceTreeVisible = s.value(QStringLiteral("workspaceTreeVisible"), false).toBool();
    m_restoreSessionOnStartup = s.value(QStringLiteral("restoreSessionOnStartup"), false).toBool();
    m_closeAppOnLastTabClose = s.value(QStringLiteral("closeAppOnLastTabClose"), true).toBool();
    m_defaultSaveFolder = s.value(QStringLiteral("defaultSaveFolder")).toString();
    m_lastSaveFolder    = s.value(QStringLiteral("lastSaveFolder")).toString();
    m_lastOpenFolder    = s.value(QStringLiteral("lastOpenFolder")).toString();
    m_lastExportFolder  = s.value(QStringLiteral("lastExportFolder")).toString();

    m_sessionTabs.clear();
    const int sessionCount = s.beginReadArray(QStringLiteral("session/tabs"));
    m_sessionTabs.reserve(sessionCount);
    for (int i = 0; i < sessionCount; ++i) {
        s.setArrayIndex(i);
        SessionTabState state;
        state.path = s.value(QStringLiteral("path")).toString();
        state.cursorPos = s.value(QStringLiteral("cursorPos"), 0).toInt();
        state.editorScroll = s.value(QStringLiteral("editorScroll"), 0).toInt();
        state.previewScroll = s.value(QStringLiteral("previewScroll"), 0).toInt();
        state.mode = static_cast<EditorMode>(
            s.value(QStringLiteral("mode"), static_cast<int>(EditorMode::Markdown)).toInt());
        state.previewVisible = s.value(QStringLiteral("previewVisible"), true).toBool();
        if (!state.path.isEmpty())
            m_sessionTabs.append(state);
    }
    s.endArray();
    m_sessionActiveTab = s.value(QStringLiteral("session/activeTab"), -1).toInt();

    const auto pageSizeId = static_cast<QPageSize::PageSizeId>(
        s.value(QStringLiteral("pdf/pageSizeId"), static_cast<int>(QPageSize::A4)).toInt());
    if (pageSizeId == QPageSize::Custom) {
        const QSizeF pageSizeMm(
            s.value(QStringLiteral("pdf/customWidthMm"), 210.0).toDouble(),
            s.value(QStringLiteral("pdf/customHeightMm"), 297.0).toDouble());
        m_pdfExportOptions.pageSize = QPageSize(
            pageSizeMm,
            QPageSize::Millimeter,
            s.value(QStringLiteral("pdf/customPageName"), tr("Custom")).toString());
    } else {
        m_pdfExportOptions.pageSize = QPageSize(pageSizeId);
    }
    m_pdfExportOptions.orientation = static_cast<QPageLayout::Orientation>(
        s.value(QStringLiteral("pdf/orientation"), static_cast<int>(QPageLayout::Portrait)).toInt());
    m_pdfExportOptions.marginsMm = QMarginsF(
        s.value(QStringLiteral("pdf/marginLeftMm"), 20.0).toDouble(),
        s.value(QStringLiteral("pdf/marginTopMm"), 20.0).toDouble(),
        s.value(QStringLiteral("pdf/marginRightMm"), 20.0).toDouble(),
        s.value(QStringLiteral("pdf/marginBottomMm"), 20.0).toDouble());
    m_pdfExportOptions.fontSize = s.value(QStringLiteral("pdf/fontSize"), 12).toInt();
    rebuildRecentMenu();
    const QByteArray splitterState = s.value(QStringLiteral("workspaceSplitterState")).toByteArray();
    if (!splitterState.isEmpty() && m_mainSplitter)
        m_mainSplitter->restoreState(splitterState);
}

// ---------------------------------------------------------------------------
// Update check
// ---------------------------------------------------------------------------
void MainWindow::checkForUpdates()
{
    if (!m_networkManager)
        m_networkManager = new QNetworkAccessManager(this);

    statusBar()->showMessage(tr("Checking for updates..."));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/skypediacode/fastmd/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("FastMD/") + QApplication::applicationVersion());
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        statusBar()->clearMessage();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Update Check Failed"),
                tr("Could not check for updates:\n%1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            QMessageBox::warning(this, tr("Update Check Failed"),
                tr("Received an unexpected response from the server."));
            return;
        }

        const QJsonObject obj = doc.object();
        QString tagName = obj.value(QStringLiteral("tag_name")).toString().trimmed();
        if (tagName.startsWith('v', Qt::CaseInsensitive))
            tagName.removeFirst();

        if (tagName.isEmpty()) {
            QMessageBox::warning(this, tr("Update Check Failed"),
                tr("Could not parse the release information."));
            return;
        }

        const QString currentVersion = QApplication::applicationVersion();

        auto compareVersions = [](const QString& a, const QString& b) -> int {
            auto parts = [](const QString& v) {
                QList<int> r;
                for (const QString& s : v.split('.'))
                    r.append(s.toInt());
                while (r.size() < 3) r.append(0);
                return r;
            };
            const QList<int> pa = parts(a), pb = parts(b);
            for (int i = 0; i < 3; ++i) {
                if (pa[i] < pb[i]) return -1;
                if (pa[i] > pb[i]) return  1;
            }
            return 0;
        };

        if (compareVersions(currentVersion, tagName) >= 0) {
            QMessageBox::information(this, tr("Up to Date"),
                tr("<b>FastMD</b> is up to date.<br><br>"
                   "Installed version: <b>%1</b>").arg(currentVersion));
            return;
        }

        // Newer version available
        const QString releaseTitle = obj.value(QStringLiteral("name")).toString();
        const QString releaseNotes = obj.value(QStringLiteral("body")).toString();
        const QString releaseUrl   = obj.value(QStringLiteral("html_url")).toString();

        QString downloadUrl;
        const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& asset : assets) {
            const QJsonObject a = asset.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (name.endsWith(QStringLiteral("Setup.exe"), Qt::CaseInsensitive)) {
                downloadUrl = a.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }

        auto* dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(tr("Update Available"));
        dlg->setMinimumWidth(480);

        auto* vbox = new QVBoxLayout(dlg);
        vbox->setSpacing(12);
        vbox->setContentsMargins(20, 20, 20, 20);

        auto* headerLabel = new QLabel(
            tr("<b>A new version of FastMD is available!</b><br><br>"
               "Current version:&nbsp;<b>%1</b><br>"
               "Latest version:&nbsp;&nbsp;<b>%2</b>")
            .arg(currentVersion, tagName), dlg);
        headerLabel->setWordWrap(true);
        vbox->addWidget(headerLabel);

        if (!releaseTitle.isEmpty()) {
            auto* titleLabel = new QLabel(
                QStringLiteral("<b>") + releaseTitle.toHtmlEscaped() + QStringLiteral("</b>"), dlg);
            titleLabel->setWordWrap(true);
            vbox->addWidget(titleLabel);
        }

        if (!releaseNotes.isEmpty()) {
            auto* notesBox = new QTextBrowser(dlg);
            notesBox->setPlainText(releaseNotes);
            notesBox->setReadOnly(true);
            notesBox->setMaximumHeight(200);
            vbox->addWidget(notesBox);
        }

        auto* hbox = new QHBoxLayout;
        hbox->addStretch();

        if (!releaseUrl.isEmpty()) {
            auto* btnGitHub = new QPushButton(tr("View on GitHub"), dlg);
            const QString capturedUrl = releaseUrl;
            connect(btnGitHub, &QPushButton::clicked, this, [capturedUrl]() {
                QDesktopServices::openUrl(QUrl(capturedUrl));
            });
            hbox->addWidget(btnGitHub);
        }

        if (!downloadUrl.isEmpty()) {
            auto* btnDownload = new QPushButton(tr("Download"), dlg);
            btnDownload->setDefault(true);
            const QString capturedDownload = downloadUrl;
            connect(btnDownload, &QPushButton::clicked, this, [capturedDownload]() {
                QDesktopServices::openUrl(QUrl(capturedDownload));
            });
            hbox->addWidget(btnDownload);
        }

        auto* btnClose = new QPushButton(tr("Close"), dlg);
        connect(btnClose, &QPushButton::clicked, dlg, &QDialog::close);
        hbox->addWidget(btnClose);

        vbox->addLayout(hbox);
        dlg->exec();
    });
}

// ---------------------------------------------------------------------------
void MainWindow::writeSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    s.setValue(QStringLiteral("theme"), m_theme);
    s.setValue(QStringLiteral("previewVisible"), m_markdownPreviewVisible);
    s.setValue(QStringLiteral("workspaceTreeVisible"), m_workspaceTreeVisible);
    s.setValue(QStringLiteral("restoreSessionOnStartup"), m_restoreSessionOnStartup);
    s.setValue(QStringLiteral("closeAppOnLastTabClose"), m_closeAppOnLastTabClose);
    s.setValue(QStringLiteral("defaultSaveFolder"), m_defaultSaveFolder);
    s.setValue(QStringLiteral("lastSaveFolder"),    m_lastSaveFolder);
    s.setValue(QStringLiteral("lastOpenFolder"),    m_lastOpenFolder);
    s.setValue(QStringLiteral("lastExportFolder"),  m_lastExportFolder);
    if (m_mainSplitter && m_workspacePanel && m_workspacePanel->isVisible())
        s.setValue(QStringLiteral("workspaceSplitterState"), m_mainSplitter->saveState());
    if (m_activeEditor) {
        int ps = m_activeEditor->font().pixelSize();
        if (ps > 0) s.setValue(QStringLiteral("editorFontSize"), ps);
    }
    s.setValue(QStringLiteral("pdf/pageSizeId"), static_cast<int>(m_pdfExportOptions.pageSize.id()));
    if (m_pdfExportOptions.pageSize.id() == QPageSize::Custom) {
        const QSizeF sizeMm = m_pdfExportOptions.pageSize.size(QPageSize::Millimeter);
        s.setValue(QStringLiteral("pdf/customWidthMm"), sizeMm.width());
        s.setValue(QStringLiteral("pdf/customHeightMm"), sizeMm.height());
        s.setValue(QStringLiteral("pdf/customPageName"), m_pdfExportOptions.pageSize.name());
    }
    s.setValue(QStringLiteral("pdf/orientation"), static_cast<int>(m_pdfExportOptions.orientation));
    s.setValue(QStringLiteral("pdf/marginLeftMm"), m_pdfExportOptions.marginsMm.left());
    s.setValue(QStringLiteral("pdf/marginTopMm"), m_pdfExportOptions.marginsMm.top());
    s.setValue(QStringLiteral("pdf/marginRightMm"), m_pdfExportOptions.marginsMm.right());
    s.setValue(QStringLiteral("pdf/marginBottomMm"), m_pdfExportOptions.marginsMm.bottom());
    s.setValue(QStringLiteral("pdf/fontSize"), m_pdfExportOptions.fontSize);

    s.remove(QStringLiteral("session/tabs"));
    s.beginWriteArray(QStringLiteral("session/tabs"));
    int sessionIndex = 0;
    int activeIndex = -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* pg = m_tabs->pageAt(i);
        if (!pg || pg->model->filePath().isEmpty())
            continue;

        s.setArrayIndex(sessionIndex);
        s.setValue(QStringLiteral("path"), pg->model->filePath());
        s.setValue(QStringLiteral("cursorPos"), pg->editor->textCursor().position());
        s.setValue(QStringLiteral("editorScroll"), pg->editor->verticalScrollBar()->value());
        s.setValue(QStringLiteral("previewScroll"), pg->preview->verticalScrollBar()->value());
        s.setValue(QStringLiteral("mode"), static_cast<int>(pg->model->editorMode()));
        s.setValue(QStringLiteral("previewVisible"), pg->model->markdownPreviewVisible());
        if (i == m_tabs->currentIndex())
            activeIndex = sessionIndex;
        ++sessionIndex;
    }
    s.endArray();
    s.setValue(QStringLiteral("session/activeTab"), activeIndex);
}
