#include "MainWindow.h"

#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "FindReplaceDialog.h"
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
#include <QSplitter>
#include <QPalette>
#include <QStyle>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QToolButton>
#include <QTimer>
#include <QPageSetupDialog>
#include <QPrinter>

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

} // namespace

// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("FastMD"));
    setMinimumSize(800, 500);
    resize(1200, 750);

    m_tabs = new TabWidget(this);
    setCentralWidget(m_tabs);

    m_findDialog = new FindReplaceDialog(this);
    m_watcher    = new QFileSystemWatcher(this);

    createMenus();
    createToolbar();
    createStatusBar();
    readSettings();
    applyTheme(m_theme);

    connect(m_tabs, &TabWidget::editorActivated, this, &MainWindow::onEditorActivated);
    connect(m_tabs, &TabWidget::tabClosed,       this, &MainWindow::onTabCloseRequested);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileChanged);

    setAcceptDrops(true);

    // open with one blank tab
    m_tabs->addNewTab();
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
    auto* actSave   = mkAct(file, tr("&Save"),      QKeySequence::Save,   this, &MainWindow::saveFile);
    auto* actSaveAs = mkAct(file, tr("Save &As…"),  QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
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
    mkAct(file, tr("E&xit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);

    Q_UNUSED(actNew); Q_UNUSED(actOpen); Q_UNUSED(actSave); Q_UNUSED(actSaveAs);

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
    help->addAction(tr("About FastMD"), this, [this] {
        QMessageBox::about(this, tr("About FastMD"),
            tr("<b>FastMD</b> v0.1.0<br>"
               "A lightweight Markdown editor built with C++20 and Qt 6.<br><br>"
               "Copyright &copy; 2026 Skypedia<br><br>"
               "Licensed under the MIT License. This software is provided "
               "\"as is\", without warranty of any kind."));
    });
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
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
        return a;
    };

    // Convenience: Material-icon formatting button wired to the active editor
    auto addFmtMat = [&](QChar code, const QString& tooltip, QKeySequence sc,
                         void (MarkdownEditor::*slot)(), bool bindShortcut = true) -> QAction* {
        return addMat(code, tooltip, sc, this, [this, slot]() {
            if (m_activeEditor) (m_activeEditor->*slot)();
        }, bindShortcut);
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
    addMat(QChar(0xE161), tr("Save"),      QKeySequence::Save, this, &MainWindow::saveFile, false);
    addMat(QChar(0xE80B), tr("Export HTML"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this, &MainWindow::doExportHtml, false);
    addMat(QChar(0xE415), tr("Export PDF"),  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::doExportPdf, false);
    addMat(QChar(0xE89E), tr("Preview in Browser"), {}, this, &MainWindow::doPreviewBrowser, false);
    m_toolbar->addSeparator();

    // ── Inline formatting ─────────────────────────────────────
    // format_bold=0xE238  format_italic=0xE23C  format_strikethrough=0xE257
    addFmtMat(QChar(0xE238), tr("Bold"),          QKeySequence(Qt::CTRL | Qt::Key_B),         &MarkdownEditor::fmtBold);
    addFmtMat(QChar(0xE23C), tr("Italic"),        QKeySequence(Qt::CTRL | Qt::Key_I),         &MarkdownEditor::fmtItalic);
    addFmtMat(QChar(0xE257), tr("Strikethrough"), {},                                          &MarkdownEditor::fmtStrike);
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
    addFmtMat(QChar(0xE6B1), tr("Checklist"),     {},                                              &MarkdownEditor::fmtChecklist);
    addFmtMat(QChar(0xE244), tr("Blockquote"),    {},                                              &MarkdownEditor::fmtQuote);
    m_toolbar->addSeparator();

    // ── Inline code, table, link, image ──────────────────────
    // code=0xE86F  border_all=0xE228  link=0xE157  image=0xE3F4
    addFmtMat(QChar(0xE86F), tr("Inline code"),  QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), &MarkdownEditor::fmtCode);
    addFmtMat(QChar(0xE228), tr("Table"),        {},                                          &MarkdownEditor::fmtTable);
    addFmtMat(QChar(0xE157), tr("Link"),         QKeySequence(Qt::CTRL | Qt::Key_K),         &MarkdownEditor::fmtLink);
    addMat(QChar(0xE3F4), tr("Image"), {}, this, [this]() {
        if (m_activeEditor) {
            TabPage* pg = m_tabs->currentPage();
            m_activeEditor->fmtImage(pg ? pg->model->filePath() : QString());
        }
    }, false);
    m_toolbar->addSeparator();

    // ── Find / Preview ────────────────────────────────────────
    // search=0xE8B6  visibility=0xE8F4  open_in_new=0xE89E
    addMat(QChar(0xE8B6), tr("Find / Replace"), QKeySequence::Find, this, &MainWindow::openFind, false);


    auto* actPreview = new QAction(this);
    actPreview->setCheckable(true);
    actPreview->setChecked(true);
    actPreview->setToolTip(tip(tr("Toggle preview"), QKeySequence(Qt::CTRL | Qt::Key_P)));
    // Shortcut omitted here to prevent ambiguous shortcut error with View -> Preview
    connect(actPreview, &QAction::toggled, this, [this](bool on) {
        m_actTogglePreview->setChecked(on);
        togglePreview();
    });
    connect(m_actTogglePreview, &QAction::toggled, actPreview, &QAction::setChecked);
    m_toolbar->addAction(actPreview);
    m_tbIcons.append({actPreview, false, QChar(0xE8F4), {}});
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
    if (m_openButton)
        m_openButton->setIcon(IconHelper::materialIcon(QChar(0xE2C8), iconColor, sz));
    if (m_recentChevron)
        m_recentChevron->setIcon(IconHelper::materialIcon(QChar(0xE5C5), iconColor, 20));
}

// ---------------------------------------------------------------------------
void MainWindow::createStatusBar()
{
    m_lblPosition = new QLabel(tr("Line: 1, Col: 1"));
    m_lblWords    = new QLabel(tr("Words: 0"));
    m_lblEncoding = new QLabel(tr("UTF-8"));

    statusBar()->addPermanentWidget(m_lblEncoding);
    statusBar()->addPermanentWidget(m_lblWords);
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
}

void MainWindow::openFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open Markdown Files"),
        QString(),
        tr("Markdown (*.md *.markdown *.txt);;All Files (*)"));

    for (const QString& p : paths)
        openFilePath(p);
}

void MainWindow::openFilePath(const QString& path)
{
    // check if already open
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* pg = m_tabs->pageAt(i)) {
            if (pg->model->filePath() == path) {
                m_tabs->setCurrentIndex(i);
                return;
            }
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open Failed"),
            tr("Cannot read file:\n%1").arg(path));
        return;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();

    int idx = m_tabs->addNewTab(path);
    if (TabPage* pg = m_tabs->pageAt(idx)) {
        pg->editor->setPlainText(content);
        pg->editor->document()->setModified(false);
        pg->model->setDirty(false);
        m_tabs->refreshTitle(idx);
    }
    addToRecent(path);
    watchFile(path);
    updateWindowTitle();
}

bool MainWindow::saveDocument(int tabIndex, bool forceDialog)
{
    TabPage* pg = m_tabs->pageAt(tabIndex);
    if (!pg) return false;

    const QString oldPath = pg->model->filePath();
    QString path = oldPath;

    if (forceDialog || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Markdown File"),
            path.isEmpty() ? pg->model->displayName() + QStringLiteral(".md") : path,
            tr("Markdown (*.md *.markdown);;All Files (*)"));
        if (path.isEmpty()) return false;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Cannot write file:\n%1").arg(path));
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << pg->editor->toPlainText();
    f.close();

    pg->model->setFilePath(path);
    pg->model->setDirty(false);
    m_tabs->refreshTitle(tabIndex);
    addToRecent(path);
    if (!oldPath.isEmpty() && oldPath != path)
        m_watcher->removePath(oldPath);
    m_ignoredFileChanges.removeAll(path);
    m_ignoredFileChanges.append(path);
    watchFile(path);
    updateWindowTitle();
    
    if (tabIndex == m_tabs->currentIndex()) {
        updatePreview();
    }
    
    return true;
}

void MainWindow::saveFile()
{
    saveDocument(m_tabs->currentIndex(), false);
}

void MainWindow::saveFileAs()
{
    saveDocument(m_tabs->currentIndex(), true);
}

void MainWindow::doExportHtml()
{
    updateCurrentHtml();
    if (m_currentHtml.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export HTML"), {},
        tr("HTML (*.html *.htm);;All Files (*)"));
    if (path.isEmpty()) return;
    if (!ExportManager::exportHtml(m_currentHtml, path, m_theme == QStringLiteral("dark")))
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write %1").arg(path));
}

void MainWindow::doExportPdf()
{
    updateCurrentHtml();
    if (m_currentHtml.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), {},
        tr("PDF (*.pdf);;All Files (*)"));
    if (path.isEmpty()) return;
    
    TabPage* pg = m_tabs->currentPage();
    QString docPath = pg ? pg->model->filePath() : QString();
    
    if (!ExportManager::exportPdf(m_currentHtml, path, docPath, m_pdfExportOptions, this))
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write %1").arg(path));
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

void MainWindow::doPreviewBrowser()
{
    if (!m_activeEditor) return;
    updateCurrentHtml();
    const QString& html = m_currentHtml;
    QString tempPath = QDir::tempPath() + QStringLiteral("/fastmd_preview.html");
    if (ExportManager::exportHtml(html, tempPath, m_theme == QStringLiteral("dark"))) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::zoomIn()
{
    if (m_activeEditor) m_activeEditor->zoomIn(1);
    if (m_activePreview) m_activePreview->zoomIn(1);
}

void MainWindow::zoomOut()
{
    if (m_activeEditor) m_activeEditor->zoomOut(1);
    if (m_activePreview) m_activePreview->zoomOut(1);
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
    }
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
        int idx = m_tabs->currentIndex();
        m_tabs->markDirty(idx, modified);
        updateWindowTitle();
    });
    m_findDialog->setEditor(editor);
}

void MainWindow::disconnectEditor()
{
    if (m_activeEditor) {
        disconnect(m_activeEditor, nullptr, this, nullptr);
        disconnect(m_activeEditor->document(), nullptr, this, nullptr);
    }
    if (m_activePreview) {
        disconnect(m_activePreview, nullptr, this, nullptr);
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
    if (editor && isPreviewVisible())
        updatePreview();
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

    updateCurrentHtml();

    TabPage* pg = m_tabs->currentPage();
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
    TabPage* pg = m_tabs->currentPage();
    if (!pg) return;
    const bool showPreview = !pg->preview->isVisible();
    pg->preview->setVisible(showPreview);
    if (showPreview && m_previewDirty)
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
    return pg && pg->preview && pg->preview->isVisible();
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

    auto btn = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("'%1' has unsaved changes.\nSave before closing?")
            .arg(pg->model->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

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
        close();
        return;
    }
    updateWindowTitle();
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
    m_findDialog->openFind();
}

void MainWindow::openReplace()
{
    m_findDialog->openReplace();
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
    if (m_recentFiles.isEmpty())
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
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
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                in.setEncoding(QStringConverter::Utf8);
                pg->editor->setPlainText(in.readAll());
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
}

void MainWindow::writeSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("geometry"),    saveGeometry());
    s.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    s.setValue(QStringLiteral("theme"),       m_theme);
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
}
