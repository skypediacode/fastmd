#include "MainWindow.h"
#include "StartupProfiler.h"

#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "RatePrompt.h"
#include "GoToLineDialog.h"
#include "PreferencesDialog.h"
#include "RecoveryManager.h"
#include "ExportManager.h"
#include "DocumentModel.h"
#include "IconHelper.h"
#include "RecentFilesManager.h"
#include "WorkspacePanelController.h"
#include "SourceControlPanel.h"
#include "ThemedMessageBox.h"
#include "MainWindowTextIO.h"
#include "SyntaxDefinitionResolver.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include "ImportManager.h"
#include "WhatsNew.h"

#include <QCoreApplication>
#include <QSplitter>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
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
#include <QTextCursor>
#include <QThread>
#include <QScrollBar>
#include <QTimer>
#include <QAbstractButton>
#include <QPushButton>
#include <QCryptographicHash>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>

static QByteArray windowsFileIdentity(const QString& path)
{
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                      FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};

    BY_HANDLE_FILE_INFORMATION info {};
    const bool succeeded = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!succeeded)
        return {};

    return QByteArray::number(info.dwVolumeSerialNumber, 16)
        + ':' + QByteArray::number(info.nFileIndexHigh, 16)
        + ':' + QByteArray::number(info.nFileIndexLow, 16);
}

// Only used by openFilePaths() below (brings the window to front when the app
// is invoked again to open a file, e.g. from Explorer's "Open with").
static void forceForeground(QWidget* widget)
{
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return;

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attached = foregroundThread != 0 && foregroundThread != currentThread
        && AttachThreadInput(foregroundThread, currentThread, TRUE);

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);

    if (attached)
        AttachThreadInput(foregroundThread, currentThread, FALSE);
}
#endif

// isDescendantOf() is only used by dragEnterEvent()/dropEvent() below.
static bool isDescendantOf(const QWidget* widget, const QWidget* ancestor)
{
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        if (w == ancestor)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void MainWindow::newFile()
{
    // A blank document has nothing to read; leaving Reading Mode here is what
    // keeps File > New / Ctrl+T / the tab bar's + button usable. Only the
    // window-level flag is dropped: the Editor being asked for belongs to the
    // tab about to be created, not to the tab currently on screen.
    leaveReadingModeForEditing(false);
    m_tabs->addNewTab();
    syncCurrentTabUi();
    m_workspacePanelCtrl->updateRoot();
}

// ---------------------------------------------------------------------------
void MainWindow::newWindow()
{
    MainWindow* window = createAuxiliaryWindow(StartupMode::NewEmptyWindow);
    window->raise();
    window->activateWindow();
}

// ---------------------------------------------------------------------------
void MainWindow::openFilePaths(const QStringList& paths)
{
    FASTMD_MARK("  task: startup paths begin");
    // A directory argument is the one deliberate exception here: passing a
    // folder on the command line / via drag-drop is itself an explicit
    // workspace-management action (equivalent to Open Folder), unlike any of
    // the file arguments alongside it.
    for (const QString& path : paths) {
        const QFileInfo pathInfo(path);
        if (!pathInfo.isDir())
            continue;

        m_workspacePanelCtrl->setRoot(pathInfo.absoluteFilePath());
        m_lastOpenFolder = pathInfo.absoluteFilePath();
    }

    for (const QString& path : paths) {
        if (QFileInfo(path).isFile())
            openFilePath(path, true);
    }

    raise();
    activateWindow();
#ifdef Q_OS_WIN
    forceForeground(this);
#endif
    FASTMD_MARK("  task: startup paths complete");
}

// ---------------------------------------------------------------------------
void MainWindow::openStartupPaths(const QStringList& paths)
{
    if (m_startupReady) {
        openFilePaths(paths);
        return;
    }

    m_pendingStartupPaths.append(paths);
    m_hasPendingStartupOpenRequest = true;
}

// ---------------------------------------------------------------------------
void MainWindow::completeStartup()
{
    m_startupReady = true;
    if (m_hasPendingStartupOpenRequest) {
        m_hasPendingStartupOpenRequest = false;
        const QStringList paths = std::exchange(m_pendingStartupPaths, {});
        openFilePaths(paths);
    }

    // Notepad Mode temporarily overrides the active tab's split and the
    // Workspace panel, so defer restoration until startup has created or
    // restored an active tab. Its independent panel preferences have already
    // been read and remain intact beneath this overlay.
    if (m_restoreNotepadModeOnStartup) {
        m_restoreNotepadModeOnStartup = false;
        setNotepadMode(true);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::openFile()
{
    // Same extension patterns as the individual filters below -- gathered once
    // so "All Supported Files" can't drift from what they actually list.
    const QString markdownPatterns = QStringLiteral("*.md *.markdown *.mdown *.mkd");
    const QString textPatterns = QStringLiteral("*.txt *.text");
    const QString pdfPatterns = QStringLiteral("*.pdf");
    const QString htmlPatterns = QStringLiteral("*.html *.htm");
    const QString imagePatterns = ImageFormats::openDialogPatterns();

    const QString allSupportedFilter = tr("All Supported Files (%1)")
        .arg(QStringList{ markdownPatterns, textPatterns, pdfPatterns, htmlPatterns, imagePatterns }
                 .join(QLatin1Char(' ')));

    // Listed first so QFileDialog defaults to it instead of hiding every other
    // openable type behind a manual filter switch.
    const QString filters = QStringList{
        allSupportedFilter,
        tr("Markdown (%1)").arg(markdownPatterns),
        tr("Plain Text (%1)").arg(textPatterns),
        tr("PDF (%1)").arg(pdfPatterns),
        tr("HTML (%1)").arg(htmlPatterns),
        ImageFormats::openDialogFilter(),
        tr("All Files (*)"),
    }.join(QStringLiteral(";;"));

    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open Files"), m_lastOpenFolder, filters);

    for (const QString& p : paths)
        openFilePath(p);
}

// ---------------------------------------------------------------------------
// The converter is built on first use, not in the constructor: it is a QObject
// plus two connections that a launch which never converts must not pay for.
// Both AnyDoc workflows share it, so the lazy construction lives here.
void MainWindow::ensureImportManager()
{
    if (m_importManager)
        return;
    m_importManager = new ImportManager(this);
    connect(m_importManager, &ImportManager::converted, this,
            [this](ImportManager::Purpose purpose, const QString& markdownPath, int droppedImages) {
                QGuiApplication::restoreOverrideCursor();
                if (purpose == ImportManager::Purpose::InsertContent) {
                    finishInsertContentFromFile(markdownPath, droppedImages);
                    return;
                }
                // Opened through the normal path, so the tab carries the
                // resolved name, is clean, and Ctrl+S / recent files /
                // the file watcher all treat it as an ordinary document.
                openFilePath(markdownPath);
                const QString name = QFileInfo(markdownPath).fileName();
                statusBar()->showMessage(droppedImages > 0
                    ? tr("Imported %1. %n embedded image(s) became alt text.", nullptr, droppedImages).arg(name)
                    : tr("Imported %1.").arg(name), 6000);
            });
    connect(m_importManager, &ImportManager::conversionFailed, this,
            [this](ImportManager::Purpose purpose, const QString& title, const QString& message) {
                QGuiApplication::restoreOverrideCursor();
                if (purpose == ImportManager::Purpose::InsertContent)
                    m_insertContentTarget.clear();
                themedWarning(this, title, message);
            });
}

// ---------------------------------------------------------------------------
void MainWindow::importAsMarkdown()
{
    ensureImportManager();
    if (m_importManager->isRunning()) {
        themedInformation(this, tr("Import in progress"),
                          tr("Wait for the current document import to finish."));
        return;
    }

    const QString inputPath = QFileDialog::getOpenFileName(
        this, tr("Import Document as Markdown"), m_lastOpenFolder,
        ImportManager::importFileFilter());
    if (inputPath.isEmpty())
        return;

    // No Save As step: the destination is derived from the source and made
    // collision-free up front, so import is one uninterrupted action.
    const QFileInfo inputInfo(inputPath);
    const QString markdownPath = ImportManager::destinationFor(inputInfo.absoluteFilePath());
    if (markdownPath.isEmpty()) {
        themedWarning(this, tr("Import failed"),
                      tr("FastMD could not find a writable folder for the imported Markdown."));
        return;
    }

    m_lastOpenFolder = inputInfo.absolutePath();
    // Only an actually launched process gets a busy cursor: start() reports its
    // own validation failures synchronously, and an override cursor set after
    // that signal had already fired would never be restored.
    if (m_importManager->start(ImportManager::Purpose::ImportDocument,
                               inputInfo.absoluteFilePath(), markdownPath))
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
}

// ---------------------------------------------------------------------------
// Text files go in verbatim; only the formats the bundled converter owns take
// the AnyDoc detour. Routing is by extension, not content: .rtf and .csv are
// plain text but are deliberately converted rather than pasted raw.
// Either way the current tab, its path and its mode are untouched beyond the
// one edit this makes.
void MainWindow::insertContentFromFile()
{
    if (!m_activeEditor || m_activeEditor->isReadOnly())
        return;

    // The Markdown/plain-text patterns this dialog advertises. Only a dialog
    // hint: the raw-text route accepts anything loadEditableTextFile() can
    // decode, which is why All Files stays available below.
    static const QStringList textPatterns = {
        QStringLiteral("*.md"), QStringLiteral("*.markdown"),
        QStringLiteral("*.txt"), QStringLiteral("*.text"),
    };
    QStringList allSupported = textPatterns;
    for (const QString& extension : ImportManager::supportedExtensions())
        allSupported.append(QStringLiteral("*.") + extension);

    const QString inputPath = QFileDialog::getOpenFileName(
        this, tr("Insert Content from File"), m_lastOpenFolder,
        tr("All Supported Files (%1)").arg(allSupported.join(QLatin1Char(' '))) + QStringLiteral(";;")
            + tr("Text and Markdown (%1)").arg(textPatterns.join(QLatin1Char(' '))) + QStringLiteral(";;")
            + ImportManager::documentFilterEntry() + QStringLiteral(";;")
            + tr("All Files (*)"));
    if (inputPath.isEmpty())
        return;

    const QFileInfo inputInfo(inputPath);
    m_lastOpenFolder = inputInfo.absolutePath();

    // Anything the converter does not own is offered to the same loader the
    // Open path uses, which validates and decodes in a single read. There is
    // no text-extension list: All Files is a legitimate choice, and
    // loadEditableTextFile() is what decides whether the bytes are text.
    if (!ImportManager::handlesExtension(inputInfo.suffix())) {
        QString content;
        const auto result = loadEditableTextFile(inputInfo.absoluteFilePath(), &content, nullptr);
        switch (result) {
        case EditableTextFileLoadResult::Success:
            insertContentAtCursor(m_activeEditor, std::move(content), 0);
            return;
        case EditableTextFileLoadResult::NotEditable:
            themedWarning(this, tr("Insert failed"),
                tr("%1 is not an editable text file, and its format is not one the "
                   "document converter supports.").arg(inputInfo.fileName()));
            return;
        case EditableTextFileLoadResult::ReadError:
            themedWarning(this, tr("Insert failed"),
                          tr("Cannot read file:\n%1").arg(inputInfo.absoluteFilePath()));
            return;
        }
        return;
    }

    ensureImportManager();
    if (m_importManager->isRunning()) {
        themedInformation(this, tr("Conversion in progress"),
                          tr("Wait for the current document conversion to finish."));
        return;
    }

    // The Markdown is read back and discarded, so it never touches the user's
    // folders the way an import destination does.
    const QString markdownPath = ImportManager::temporaryDestinationFor(inputInfo.absoluteFilePath());
    if (markdownPath.isEmpty()) {
        themedWarning(this, tr("Insert failed"),
                      tr("FastMD could not create a temporary folder for the conversion."));
        return;
    }

    m_insertContentTarget = m_activeEditor;
    if (m_importManager->start(ImportManager::Purpose::InsertContent,
                               inputInfo.absoluteFilePath(), markdownPath)) {
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        // start() reported its own failure synchronously; the failure handler
        // cleared the target, but the scratch folder is this function's to undo.
        ImportManager::removeTemporary(markdownPath);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::finishInsertContentFromFile(const QString& markdownPath, int droppedImages)
{
    const QPointer<MarkdownEditor> editor = m_insertContentTarget;
    m_insertContentTarget.clear();

    QString markdown;
    const bool read = readTextFile(markdownPath, &markdown, nullptr);
    ImportManager::removeTemporary(markdownPath);

    if (!read) {
        themedWarning(this, tr("Insert failed"),
                      tr("The converted Markdown could not be read back."));
        return;
    }
    // The tab may have closed, switched to a viewer, or become read-only while
    // the converter ran; inserting anywhere else would be worse than not at all.
    if (!editor || editor->isReadOnly() || editor != m_activeEditor) {
        themedWarning(this, tr("Insert failed"),
                      tr("The document this content was meant for is no longer being edited."));
        return;
    }

    insertContentAtCursor(editor, std::move(markdown), droppedImages);
}

// ---------------------------------------------------------------------------
// The final step both Insert routes share, so raw text and converted Markdown
// land identically.
void MainWindow::insertContentAtCursor(MarkdownEditor* editor, QString content, int droppedImages)
{
    if (!editor)
        return;

    while (content.endsWith(QLatin1Char('\n')))
        content.chop(1);
    if (content.isEmpty()) {
        statusBar()->showMessage(tr("The selected file contained no text."), 6000);
        return;
    }

    // One edit block, so replacing the selection and inserting the content
    // undo together.
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.insertText(content);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    // Reading Mode hides the Editor, and a hidden QPlainTextEdit cannot hold
    // Windows focus -- handing it over there leaves the window with nothing
    // focused and kills keyboard scrolling in the Preview.
    if (editor->isVisible())
        editor->setFocus();

    statusBar()->showMessage(droppedImages > 0
        ? tr("Inserted converted content. %n embedded image(s) became alt text.", nullptr, droppedImages)
        : tr("Inserted content."), 6000);
}

// ---------------------------------------------------------------------------
void MainWindow::openFilePath(const QString& path, bool openUnsupportedAsSource)
{
    FASTMD_MARK("    file open: begin");
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        themedWarning(this, tr("Open Failed"), tr("Cannot find file:\n%1").arg(path));
        return;
    }
    const QString suffix = fileInfo.suffix();
    const bool pdf = suffix.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    const bool html = suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0;
    // Checked before isEditableTextFile() below to avoid that function's
    // full-file read for every image; content-sniffed via canRead().
    const bool image = ImageFormats::isImageFile(path);
    const bool svg = suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0;
    // Editable-text validation happens once, inside addTabFromFile() below
    // (it falls back to opening externally on its own) - checking here too
    // would read the whole file twice for every successful open.
    if ((pdf && m_openPdfExternally) || (html && m_openHtmlExternally) || (image && m_openImagesExternally)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
        return;
    }
    if (const OpenDocumentRef existing = findOpenDocument(path); existing.page) {
        activateOpenDocument(existing);
        if (existing.page->model->editorMode() == EditorMode::Pdf
            || existing.page->model->editorMode() == EditorMode::Html
            || existing.page->model->editorMode() == EditorMode::Svg) {
            existing.page->preview->showReadOnlyFile(fileInfo.absoluteFilePath());
        } else if (existing.page->model->editorMode() == EditorMode::Image && existing.page->imageViewer) {
            existing.page->imageViewer->load(fileInfo.absoluteFilePath());
        }
        return;
    }

    // Callers that represent the user explicitly pointing FastMD at this file
    // (command-line launch, file-type-association double-click, single-instance
    // forwarding -- see openFilePaths()) never want the OS default app instead:
    // an unsupported/undecodable file opens as a raw source tab, Notepad++-style,
    // rather than silently handing it to another program.
    if (openUnsupportedAsSource && !pdf && !html && !image && !svg
        && !SyntaxDefinitionResolver::isEditableTextFile(path)) {
        openFileAsText(path);
        return;
    }

    if (addTabFromFile(path, true, true, true, nullptr) < 0)
        return;

    // Opening a file is never a workspace-management action: the Workspace
    // root changes only through explicit Open Folder / Open Workspace (or
    // another clearly intentional root change), never as a side effect of
    // opening a document. See openFilePaths() for the one deliberate
    // exception: a directory argument on the command line.
    m_lastOpenFolder = QFileInfo(path).absolutePath();
    updateWindowTitle();
    syncCurrentTabUi();
    // The new tab opened straight into the reading layout, so its Editor never
    // took focus -- give the Preview keyboard control the same way entering
    // Reading Mode does.
    if (isReadingModeActive())
        focusReadingModePreview();
    m_workspacePanelCtrl->updateRoot();
    FASTMD_MARK("    file open: complete");
}

namespace {
constexpr const char* kUserGuideBundleKind = "userGuide";
constexpr const char* kReleaseNotesBundleKind = "releaseNotes";
constexpr const char* kWhatsNewBundleKind = "whatsNew";
}

// ---------------------------------------------------------------------------
// Shared plumbing behind the User Guide, Release Notes, and What's New tabs.
// All three are Qt resources, not files beside the executable, which is what
// makes "read-only" structural rather than enforced: with no path there is
// nothing for Save to overwrite, nothing for the session to restore, and no
// watcher to arm. In every other respect it is an ordinary Markdown tab, so
// it renders, exports, prints and searches like any document.
void MainWindow::openBundledReadOnlyDocument(const QString& kind, const QString& title,
                                             const QString& markdown, bool activate)
{
    if (const OpenDocumentRef existing = findOpenBundledDocument(kind); existing.page) {
        if (activate)
            activateOpenDocument(existing);
        return;
    }

    // The tab is built *and* fully described before it is activated. Activating
    // it emits editorActivated, whose syncCurrentTabUi() would otherwise run
    // against a model that is not yet marked read-only or reading -- re-showing
    // the Editor that addNewTab() just hid, only for the sync below to hide it
    // again. TabWidget's mirror of the window flag is what gives addNewTab() the
    // reading layout, so borrow it for this one tab and put it back.
    RestoredTabSettings settings = defaultTabSettingsForPath();
    settings.markdownPreviewVisible = true;
    const bool windowReadingMode = m_readingMode;
    m_tabs->setReadingMode(true);
    const int index = m_tabs->addNewTab(QString(), false, &settings);
    m_tabs->setReadingMode(windowReadingMode);

    TabPage* page = m_tabs->pageAt(index);
    if (!page)
        return;

    page->model->setBundled(true);
    page->model->setBundledKind(kind);
    page->model->setDisplayNameOverride(title);
    // Per-tab, so the user's own Reading Mode and Editor/Preview preferences are
    // left exactly as they were for every other tab.
    page->model->setReadingModeOverride(true);
    // Filling an inactive editor also keeps the text out of onTextChanged, so
    // the document is never briefly dirty and never schedules an Auto Save.
    page->editor->setPlainText(markdown);
    page->editor->setReadOnly(true);
    page->editor->document()->setModified(false);
    m_tabs->refreshTitle(index);

    if (!activate)
        return;

    m_tabs->setCurrentIndex(index);
    updateWindowTitle();
    syncCurrentTabUi();
    focusReadingModePreview();
}

// ---------------------------------------------------------------------------
// Help > User Guide.
void MainWindow::openUserGuide()
{
    QFile guide(QStringLiteral(":/guide/user-guide.md"));
    if (!guide.open(QIODevice::ReadOnly)) {
        themedWarning(this, tr("User Guide"),
                      tr("The bundled User Guide could not be opened."));
        return;
    }
    const QString markdown = QString::fromUtf8(guide.readAll());
    guide.close();
    openBundledReadOnlyDocument(QLatin1String(kUserGuideBundleKind), tr("User Guide"), markdown, true);
}

// ---------------------------------------------------------------------------
// Help > Release Notes. Always the complete, unfiltered bundled changelog --
// unlike the automatic What's New tab, this command never limits itself to
// the running version.
void MainWindow::openReleaseNotes()
{
    QFile changelog(QStringLiteral(":/release-notes/CHANGELOG.md"));
    if (!changelog.open(QIODevice::ReadOnly)) {
        themedWarning(this, tr("Release Notes"),
                      tr("The bundled release notes could not be opened."));
        return;
    }
    const QString markdown = QString::fromUtf8(changelog.readAll());
    changelog.close();
    openBundledReadOnlyDocument(QLatin1String(kReleaseNotesBundleKind), tr("Release Notes"), markdown, true);
}

// ---------------------------------------------------------------------------
// Locates the tracked placeholder among the current tabs and reports whether
// it is still exactly as openInitialTab() left it: untitled, unedited, not
// bundled, not a diff, empty. Deliberately not
// page->editor->document()->isModified(): every freshly-constructed
// MarkdownEditor sets an internal line-height block format before
// contentsChanged() is even connected (see the comment in MarkdownEditor's
// constructor), which leaves that raw Qt flag true from construction
// regardless of whether the user typed anything. model->isDirty() is the one
// signal the rest of the app (autosave, close confirmation, session save)
// already trusts for "has this tab actually been edited", so it is the one
// used here. Returns the tab's index, or -1 if the widget is gone or no
// longer pristine.
int MainWindow::pristineStartupPlaceholderIndex(QWidget* widget) const
{
    if (!widget)
        return -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* page = m_tabs->pageAt(i);
        if (!page || page->splitter != widget)
            continue;
        DocumentModel* model = page->model;
        const bool pristine = model && page->editor
            && model->isUntitled() && !model->isDirty()
            && !model->isBundled() && !model->isDiff()
            && page->editor->toPlainText().isEmpty();
        return pristine ? i : -1;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Runs once per launch as its own deferred startup task, unconditionally --
// unlike maybeShowWhatsNew() below, this must run whether or not an update is
// pending. By this point (priority 3) session restore, command-line/shell
// file activation, and single-instance forwarding (priority 0-2) have
// already run and already activated whatever they were going to activate. If
// the tracked placeholder is no longer the active tab -- something else
// claimed it -- and it is still untouched, discard it: otherwise an explicit
// file-open launch with no What's New to show (a normal, same-version
// startup, or an update whose What's New was already shown on a prior
// launch) would leave a stray empty tab sitting behind the requested file.
// If it is still the active tab, leave it -- and still tracked -- alone:
// that is the legitimate "nothing else claimed it, show a blank tab" case,
// and maybeShowWhatsNew() (if it has anything to show) is what gets to
// replace it, not this.
void MainWindow::discardStrayStartupPlaceholder()
{
    QWidget* const widget = m_startupPlaceholderTab;
    const int index = pristineStartupPlaceholderIndex(widget);
    if (index < 0 || m_tabs->currentIndex() == index)
        return;
    m_startupPlaceholderTab.clear();
    discardTabForDuplicate(index);
}

// ---------------------------------------------------------------------------
// If openInitialTab() created the auto-created blank startup placeholder and
// it is still exactly as it was left, discards it via the same silent-
// discard path duplicate-tab handling already uses, and reports whether it
// was the active tab. Any other outcome (never created, already closed,
// moved, touched by the user, or already handled by
// discardStrayStartupPlaceholder() above) leaves it alone and returns false.
// Consumes m_startupPlaceholderTab either way, so this only ever acts once.
bool MainWindow::consumeStartupPlaceholderIfPristine()
{
    QWidget* const widget = m_startupPlaceholderTab;
    m_startupPlaceholderTab.clear();
    const int index = pristineStartupPlaceholderIndex(widget);
    if (index < 0)
        return false;
    const bool wasActive = (m_tabs->currentIndex() == index);
    discardTabForDuplicate(index);
    return wasActive;
}

// ---------------------------------------------------------------------------
// First-launch-after-update check, run once from a deferred startup task
// (never on a fresh install: readSettings() only populates
// m_whatsNewVersionToShow for VersionTransition::Upgrade).
void MainWindow::maybeShowWhatsNew()
{
    if (m_whatsNewVersionToShow.isEmpty())
        return;
    const QString version = std::exchange(m_whatsNewVersionToShow, QString());

    QFile changelog(QStringLiteral(":/release-notes/CHANGELOG.md"));
    if (!changelog.open(QIODevice::ReadOnly))
        return;
    const QString fullChangelog = QString::fromUtf8(changelog.readAll());
    changelog.close();

    const QString section = WhatsNew::extractChangelogSections(fullChangelog, version);
    // No matching "## [version]" heading -- a packaging/changelog mismatch.
    // m_lastRunVersion was already advanced in readSettings(), so this does
    // not retry on the next launch; failing silently (no dialog, no empty
    // tab) is the least intrusive choice for something the user never asked
    // to see in the first place.
    if (section.isEmpty())
        return;

    // Presentation-only: swaps the raw "## [version] - date" changelog
    // heading for a user-facing title. Help > Release Notes renders
    // fullChangelog untouched -- this rewrite never reaches that path.
    const QString markdown = WhatsNew::formatWhatsNewTitle(section, version);

    // By this point (a deferred, low-priority startup task) session
    // restore, command-line/shell file activation, and single-instance
    // forwarding have already run and already activated whatever they were
    // going to activate -- see the priority-3 comment where this task is
    // enqueued in the constructor. So: if the still-active tab is still the
    // untouched auto-created blank placeholder, nothing the user asked for
    // is being displaced -- discard the placeholder and make What's New the
    // active tab. Otherwise (an explicitly opened file, a restored
    // document, or a placeholder the user already started typing in) leave
    // it exactly where it is and open What's New in the background.
    const bool activate = consumeStartupPlaceholderIfPristine();

    openBundledReadOnlyDocument(QLatin1String(kWhatsNewBundleKind), tr("What's New"), markdown, activate);
}

// ---------------------------------------------------------------------------
// Creates the very first tab of a fresh startup. Unlike the User Guide, the
// Welcome tab is an ordinary editable untitled tab (no setBundled(), no
// read-only) pre-seeded with a short bundled sample -- Save, Save As, Auto
// Save, spell check and dirty tracking all behave exactly as they do for any
// new document. It is shown at most once per install.
void MainWindow::openInitialTab()
{
    if (m_welcomeShown) {
        const int index = m_tabs->addNewTab();
        // Tracked by widget identity (nulls itself if the tab is ever closed
        // or the window destroyed), not by title or tab position, so
        // maybeShowWhatsNew() can later tell this exact auto-created blank
        // tab apart from any blank tab the user opens themselves (Ctrl+T,
        // File > New) before it gets a chance to run.
        if (TabPage* page = m_tabs->pageAt(index))
            m_startupPlaceholderTab = page->splitter;
        return;
    }
    m_welcomeShown = true;

    QFile welcome(QStringLiteral(":/welcome/welcome.md"));
    if (!welcome.open(QIODevice::ReadOnly)) {
        m_tabs->addNewTab();
        return;
    }
    const QString markdown = QString::fromUtf8(welcome.readAll());
    welcome.close();

    // Passing non-null restoredSettings with an empty path skips addNewTab()'s
    // "seed from the user's default new-document mode" branch, leaving
    // DocumentModel's own constructor defaults (Markdown / "FastMD Markdown")
    // in effect -- the same mechanism openUserGuide() relies on above.
    RestoredTabSettings settings = defaultTabSettingsForPath();
    settings.markdownPreviewVisible = true;
    const int index = m_tabs->addNewTab(QString(), false, &settings);
    TabPage* page = m_tabs->pageAt(index);
    if (!page)
        return;

    // displayName() only consults this while the tab has no file path, so it
    // stops applying itself the moment the user does a real Save/Save As --
    // nothing to clear later.
    page->model->setDisplayNameOverride(tr("Welcome"));
    // Filled before activation, like openUserGuide(): keeps the text out of
    // onTextChanged() so the tab is never briefly dirty and never schedules an
    // Auto Save.
    page->editor->setPlainText(markdown);
    page->editor->document()->setModified(false);
    m_tabs->refreshTitle(index);
    m_tabs->setCurrentIndex(index);
}

// ---------------------------------------------------------------------------
void MainWindow::openFileAsText(const QString& path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.isFile()) {
        themedWarning(this, tr("Open Failed"), tr("Cannot find file:\n%1").arg(path));
        return;
    }

    QString content;
    QString lineEnding;
    TextEncoding encoding = TextEncoding::Utf8;
    if (!loadFileText(path, &content, &lineEnding, &encoding)) {
        themedWarning(this, tr("Open Failed"), tr("Cannot read file:\n%1").arg(path));
        return;
    }

    OpenDocumentRef document = findOpenDocument(path);
    MainWindow* owner = document.window ? document.window : this;
    TabPage* page = document.page;
    int index = document.index;
    if (!page) {
        const RestoredTabSettings tabSettings = defaultTabSettingsForPath(path);
        index = m_tabs->addNewTab(path, true, &tabSettings);
        page = m_tabs->pageAt(index);
        owner = this;
    }
    if (!page)
        return;

    // addNewTab() derives a brand-new tab's mode from the file extension
    // (DocumentModel::modeForPath), and an already-open tab may already be a
    // Pdf/Html/Image viewer -- both leave the model in a viewer mode that
    // setDocumentLanguage()/setSelectedDefinitionName() refuse to change.
    // This is the explicit "Open as Source Code" escape hatch, so force it
    // out of viewer mode before the language dance below runs.
    if (isViewerMode(page->model->editorMode())) {
        page->model->setEditorMode(EditorMode::PlainText);
        if (page->imageViewer)
            page->imageViewer->setVisible(false);
    }

    page->model->setLineEnding(lineEnding);
    page->model->setEncoding(encoding);
    page->editor->setPlainText(content);
    // Force non-Markdown here even for a Markdown-extension file: this is the
    // explicit "Open as Source Code/Code-Text" escape hatch. Both Markdown
    // definitions match "*.md", so a plain filename lookup would hand back a
    // Markdown definition and setDocumentLanguage() would put the tab right
    // back into Markdown mode -- drop that result and fall through to Plain
    // Text instead.
    page->model->clearLastNonMarkdownSelection();
    auto detected = SyntaxDefinitionResolver::resolve(path, EditorMode::PlainText);
    if (SyntaxDefinitionResolver::isMarkdownDefinition(detected))
        detected = {};
    const QString detectedName = detected.isValid() ? detected.name() : QString();
    page->model->setAutomaticallyDetectedName(detectedName);
    setDocumentLanguage(page, detectedName, LanguageSelectionSource::Automatic);
    page->editor->document()->setModified(false);
    page->model->setDirty(false);
    owner->rememberFileContents(path, encodeWithLineEnding(content, lineEnding, encoding));
    page->preview->setVisible(false);
    page->editor->setVisible(true);
    owner->m_tabs->refreshTitle(index);
    activateOpenDocument({ owner, page, index });
    owner->syncCurrentTabUi();
    owner->watchFile(path);
}

// ---------------------------------------------------------------------------
int MainWindow::addTabFromFile(const QString& path, bool activate, bool addToRecent,
                               bool showError, const SessionTabState* state,
                               const QString* loadedContent, const QString* loadedLineEnding,
                               const TextEncoding* loadedEncoding)
{
    FASTMD_MARK("      file tab: begin");
    if (const OpenDocumentRef existing = findOpenDocument(path); existing.page) {
        if (activate)
            activateOpenDocument(existing);
        return -1;
    }
    const QString suffix = QFileInfo(path).suffix();
    const bool pdf = suffix.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    const bool html = suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0;
    const bool image = ImageFormats::isImageFile(path);
    const bool svg = suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0;
    const bool readOnly = pdf || html || image || svg;
    if ((pdf && m_openPdfExternally) || (html && m_openHtmlExternally) || (image && m_openImagesExternally)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return -1;
    }
    QString content;
    QString lineEnding = windowsLineEnding();
    TextEncoding encoding = TextEncoding::Utf8;
    if (!readOnly && loadedContent) {
        content = *loadedContent;
        if (loadedLineEnding)
            lineEnding = *loadedLineEnding;
        if (loadedEncoding)
            encoding = *loadedEncoding;
    } else if (!readOnly) {
        const auto loadResult = loadEditableTextFile(path, &content, &lineEnding, &encoding);
        FASTMD_MARK("      file tab: content loaded");
        if (loadResult != EditableTextFileLoadResult::Success) {
            if (loadResult == EditableTextFileLoadResult::ReadError) {
                if (showError) {
                    themedWarning(this, tr("Open Failed"),
                        tr("Cannot read file:\n%1").arg(path));
                }
            } else {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            }
            return -1;
        }
    }

    const RestoredTabSettings* restoredSettings = nullptr;
    RestoredTabSettings tabSettings;
    if (state) {
        const auto it = m_restoredTabSettings.constFind(path);
        if (it != m_restoredTabSettings.cend())
            restoredSettings = &it.value();
    }
    // Also covers a state-driven open that is not a session restore (Reopen
    // Closed Tab), which has no prefetched entry in m_restoredTabSettings.
    if (!restoredSettings) {
        tabSettings = defaultTabSettingsForPath(path);
        restoredSettings = &tabSettings;
    }
    int idx = m_tabs->addNewTab(path, activate, restoredSettings);
    FASTMD_MARK("      file tab: widgets created");
    if (TabPage* pg = m_tabs->pageAt(idx)) {
        if (image) {
            // addNewTab() already created the ImageViewer, hid the editor and
            // preview, and decoded the file -- reloading here would decode twice.
        } else if (readOnly) {
            pg->editor->setVisible(false);
            pg->preview->setVisible(true);
            pg->preview->showReadOnlyFile(path);
        } else {
            pg->model->setLineEnding(lineEnding);
            pg->model->setEncoding(encoding);
            pg->editor->setPlainText(content);
            pg->editor->document()->setModified(false);
            pg->model->setDirty(false);
            rememberFileContents(path, encodeWithLineEnding(content, lineEnding, encoding));
        }
        m_tabs->refreshTitle(idx);
        if (state)
            restoreTabState(pg, *state);
        else if (!readOnly)
            initializeDocumentLanguage(pg, LanguageSelectionSource::Automatic);
    }

    if (addToRecent)
        this->m_recentFilesMgr->add(path);
    if (!state)
        watchFile(path);

    FASTMD_MARK("      file tab: complete");
    return idx;
}

// ---------------------------------------------------------------------------
bool MainWindow::loadFileText(const QString& path, QString* content, QString* lineEnding,
                              TextEncoding* encoding) const
{
    return readTextFile(path, content, lineEnding, encoding);
}

// ---------------------------------------------------------------------------
bool MainWindow::saveDocument(int tabIndex, bool forceDialog)
{
    TabPage* pg = m_tabs->pageAt(tabIndex);
    if (!pg) return false;
    if (isViewerMode(pg->model->editorMode()))
        return false;
    // Bundled content has no on-disk original and is never modifiable, so there
    // is nothing to write and nothing a Save As dialog could usefully offer.
    if (pg->model->isBundled())
        return false;

    const QString oldPath = pg->model->filePath();
    QString path = oldPath;

    if (forceDialog || path.isEmpty()) {
        const QString& currentLanguage = pg->model->selectedDefinitionName();
        const QString defaultExt = SyntaxDefinitionResolver::preferredSaveSuffix(currentLanguage);
        QString initialDir;
        if (!m_defaultSaveFolder.isEmpty())
            initialDir = m_defaultSaveFolder;
        else if (!m_lastSaveFolder.isEmpty())
            initialDir = m_lastSaveFolder;
        const QString initialPath = path.isEmpty()
            ? (initialDir.isEmpty() ? QString() : initialDir + QStringLiteral("/"))
              + pg->model->displayName() + defaultExt
            : path;

        // The first filter is what the dialog preselects, and the native
        // Windows dialog appends that filter's extension when the user types a
        // bare name -- so the current language's filter must lead. Markdown and
        // Plain Text have no saveFilterPatterns() of their own (they use the
        // dedicated filters below), so promote theirs instead of prepending an
        // empty one.
        const QString markdownFilter = tr("Markdown (*.md *.markdown *.mdown *.mkd)");
        const QString plainTextFilter = tr("Plain Text (*.txt *.text)");
        QStringList filters;
        const QString currentLanguagePatterns = SyntaxDefinitionResolver::saveFilterPatterns(currentLanguage);
        if (!currentLanguagePatterns.isEmpty()) {
            filters.append(QStringLiteral("%1 (%2)")
                .arg(SyntaxDefinitionResolver::displayNameForStoredName(currentLanguage), currentLanguagePatterns));
            filters.append(markdownFilter);
            filters.append(plainTextFilter);
        } else if (SyntaxDefinitionResolver::isMarkdownDefinitionName(currentLanguage)) {
            filters.append(markdownFilter);
            filters.append(plainTextFilter);
        } else {
            filters.append(plainTextFilter);
            filters.append(markdownFilter);
        }
        filters.append(tr("All Files (*)"));

        path = QFileDialog::getSaveFileName(
            this, tr("Save File"), initialPath, filters.join(QStringLiteral(";;")));
        if (path.isEmpty()) return false;

        const QString enteredFileName = QFileInfo(path).fileName();
        if (QFileInfo(path).suffix().isEmpty()
            && !SyntaxDefinitionResolver::fileNameMatchesDefinition(enteredFileName, currentLanguage)) {
            path += defaultExt;
        }
    }

    if (const OpenDocumentRef existing = findOpenDocument(path, pg); existing.page) {
        activateOpenDocument(existing);
        themedWarning(this, tr("Save As Conflict"),
            tr("'%1' is already open in another FastMD tab.\n\n"
               "Choose a different file name or close the existing tab first.")
                .arg(QFileInfo(path).fileName()));
        return false;
    }

    // Watched files get their own write ignored by processExternalFileChange()
    // so saving doesn't trigger a spurious "changed externally" conflict.
    // Only arm this for paths the watcher already tracks: a brand-new path
    // (untitled -> Save As) isn't watched yet during the write, so the
    // notification would never arrive to consume the ignore entry.
    const bool wasWatched = m_watcher && m_watcher->files().contains(path);
    if (!oldPath.isEmpty() && m_watcher && m_watcher->files().contains(oldPath))
        m_watcher->removePath(oldPath);

    if (oldPath.isEmpty() && !path.isEmpty() && !pg->editor->commitTemporaryImages(path)) {
        return false;
    }

    // Saving must never replace the editor document. QPlainTextEdit's
    // setPlainText() clears its undo/redo history, so normalizing Markdown
    // here made a save silently discard every edit before it. Persist the
    // exact buffer instead; formatting remains an explicit editor action.
    const QString text = pg->editor->toPlainText();

    const QByteArray bytes = encodeWithLineEnding(text, pg->model->lineEnding(),
                                                  pg->model->encoding());
    const auto saveState = pg->model->saveState();
    const quint64 saveTicket = DocumentSaveCoordinator::beginWrite(saveState);
    if (wasWatched)
        m_ignoredFileChanges.arm(path, saveTicket);
    if (DocumentSaveCoordinator::writeLatest(path, bytes, saveState, saveTicket)
        != DocumentSaveCoordinator::WriteResult::Success) {
        if (wasWatched)
            m_ignoredFileChanges.clear(path, saveTicket);
        if (!oldPath.isEmpty())
            watchFile(oldPath);
        themedWarning(this, tr("Save Failed"),
            tr("Cannot write file:\n%1").arg(path));
        return false;
    }
    rememberFileContents(path, bytes);
    if (oldPath != path) {
        m_ignoredFileChanges.clear(oldPath);
        forgetFileContents(oldPath);
    }

    if (oldPath.isEmpty() || oldPath != path) {
        RecoveryManager::remove(RecoveryManager::keyFor(oldPath, pg->model->recoveryId()));

        QSettings s;
        if (!path.isEmpty() && pg->editor) {
            s.setValue(QStringLiteral("WordWrap/") + path, pg->editor->wordWrapEnabled());
        }
        if (!oldPath.isEmpty() && oldPath != path) {
            s.remove(QStringLiteral("WordWrap/") + oldPath);
        }
    }
    pg->model->setFilePath(path);
    pg->editor->setDocumentPath(path);
    refreshDocumentLanguageForPath(pg);
    m_lastSaveFolder = QFileInfo(path).absolutePath();
    pg->model->setDirty(false);
    pg->editor->document()->setModified(false);
    // An explicit user-initiated save is a deliberate "keep my version"
    // decision, so it resolves any pending external-change conflict too.
    clearExternalConflict(pg->model);
    m_tabs->refreshTitle(tabIndex);
    m_recentFilesMgr->add(path);
    watchFile(path);
    updateWindowTitle();
    m_workspacePanelCtrl->updateRoot();
    if (m_sourceControl)
        m_sourceControl->refreshForSavedPath(path);

    if (tabIndex == m_tabs->currentIndex()) {
        m_htmlDirty = true;
        m_previewDirty = true;
        updatePreview();
    }

    RatePrompt::afterSuccessfulSave(this);

    return true;
}

// ---------------------------------------------------------------------------
void MainWindow::saveFile()
{
    saveDocument(m_tabs->currentIndex(), false);
}

// ---------------------------------------------------------------------------
void MainWindow::saveAll()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        saveDocument(i, false);
}

// ---------------------------------------------------------------------------
void MainWindow::saveFileAs()
{
    saveDocument(m_tabs->currentIndex(), true);
}

// ---------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;

    const QWidget* target = childAt(event->position().toPoint());
    if (target && isDescendantOf(target, m_activeEditor))
        return;

    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::dropEvent(QDropEvent* event)
{
    const QWidget* target = childAt(event->position().toPoint());
    if (target && isDescendantOf(target, m_activeEditor))
        return;

    bool openedAny = false;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile()) {
            openFilePath(url.toLocalFile());
            openedAny = true;
        }

    if (openedAny)
        event->acceptProposedAction();
}

// ---------------------------------------------------------------------------
void MainWindow::watchFile(const QString& path)
{
    if (path.isEmpty())
        return;
    QFileSystemWatcher* watcher = ensureFileWatcher();
    if (!watcher->files().contains(path))
        watcher->addPath(path);

    // Many tools save by replacing the destination with a temporary file.
    // That replacement can invalidate a file watch on Windows, while the
    // parent directory remains watchable and reports both halves of the swap.
    const QString directory = QFileInfo(path).absolutePath();
    if (!directory.isEmpty() && !watcher->directories().contains(directory))
        watcher->addPath(directory);

    rememberFileIdentity(path);
}

static QByteArray fileContentsSignature(const QByteArray& contents);

void MainWindow::rememberFileContents(const QString& path, const QByteArray& contents)
{
    if (!path.isEmpty())
        m_knownFileContents.insert(documentPathKey(path), fileContentsSignature(contents));
}

bool MainWindow::fileContentsAreKnown(const QString& path, const QByteArray& contents) const
{
    const auto it = m_knownFileContents.constFind(documentPathKey(path));
    return it != m_knownFileContents.cend() && it.value() == fileContentsSignature(contents);
}

void MainWindow::forgetFileContents(const QString& path)
{
    if (!path.isEmpty())
        m_knownFileContents.remove(documentPathKey(path));
}

void MainWindow::rememberFileIdentity(const QString& path)
{
#ifdef Q_OS_WIN
    const QByteArray identity = windowsFileIdentity(path);
    if (!path.isEmpty() && !identity.isEmpty())
        m_knownFileIdentities.insert(documentPathKey(path), identity);
#else
    Q_UNUSED(path);
#endif
}

void MainWindow::forgetFileIdentity(const QString& path)
{
    if (!path.isEmpty())
        m_knownFileIdentities.remove(documentPathKey(path));
}

QString MainWindow::renamedFilePath(const QString& oldPath) const
{
#ifdef Q_OS_WIN
    const QByteArray identity = m_knownFileIdentities.value(documentPathKey(oldPath));
    if (identity.isEmpty())
        return {};

    // QFileSystemWatcher reports the directory change but not the old/new
    // pair. Match the persisted file identity, not contents: a copy with the
    // same text must never cause this tab to change paths.
    const QFileInfo oldInfo(oldPath);
    const QFileInfoList entries = QDir(oldInfo.absolutePath()).entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    QString match;
    for (const QFileInfo& entry : entries) {
        const QString candidate = entry.absoluteFilePath();
        if (candidate == oldPath || windowsFileIdentity(candidate) != identity)
            continue;
        if (!match.isEmpty())
            return {}; // Ambiguous identity (for example, hard links): do not guess.
        match = candidate;
    }
    return match;
#else
    Q_UNUSED(oldPath);
    return {};
#endif
}

static QByteArray fileContentsSignature(const QByteArray& contents)
{
    return QCryptographicHash::hash(
        normalizeLoadedText(decodeAutoText(contents)).toUtf8(), QCryptographicHash::Sha256);
}

QFileSystemWatcher* MainWindow::ensureFileWatcher()
{
    if (!m_watcher) {
        FASTMD_MARK("file watcher construction begin");
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileChanged);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &MainWindow::onDirectoryChanged);
        FASTMD_MARK("file watcher constructed");
    }
    return m_watcher;
}

// ---------------------------------------------------------------------------
// QFileSystemWatcher can fire several notifications for a single logical
// write (atomic temp-file+rename saves, editors that touch the file twice,
// duplicate OS events). Debounce per path so we only inspect the file once
// the external writer has settled.
namespace {
constexpr int kFileChangeDebounceMs = 100;
}

void MainWindow::onFileChanged(const QString& path)
{
    QTimer* timer = m_fileChangeDebounce.value(path);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, path]() {
            if (QTimer* t = m_fileChangeDebounce.take(path))
                t->deleteLater();
            processExternalFileChange(path);
        });
        m_fileChangeDebounce.insert(path, timer);
    }
    timer->start(kFileChangeDebounceMs);
}

// ---------------------------------------------------------------------------
void MainWindow::onDirectoryChanged(const QString& path)
{
    const QString changedDirectory = QDir::cleanPath(path);
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* page = m_tabs->pageAt(i);
        if (!page || !page->model)
            continue;

        const QString filePath = page->model->filePath();
        if (filePath.isEmpty()
            || QDir::cleanPath(QFileInfo(filePath).absolutePath())
                .compare(changedDirectory, Qt::CaseInsensitive) != 0) {
            continue;
        }

        // Route through the file debounce: a direct write usually produces
        // both notifications, and an atomic replacement may produce only this
        // directory notification after its old file watch has been removed.
        onFileChanged(filePath);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::processExternalFileChange(const QString& path)
{
    if (m_ignoredFileChanges.consume(path)) {
        // Our own save (direct or auto-save) produced this notification.
        if (QFile::exists(path)) {
            if (!m_watcher || !m_watcher->files().contains(path))
                watchFile(path);
            else
                rememberFileIdentity(path);
            return;
        }

        // The save notification may arrive late. It must not swallow a later
        // Explorer deletion/rename of the same path.
    }

    TabPage* pg = nullptr;
    int idx = -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model->filePath() == path) {
            pg = m_tabs->pageAt(i);
            idx = i;
            break;
        }
    }
    if (!pg)
        return;

    if (!QFile::exists(path)) {
        // On Windows the directory notification has no old/new pair. The
        // file identity captured while watching lets us distinguish a rename
        // from a deletion without confusing a same-content copy for a move.
        const QString newPath = renamedFilePath(path);
        if (!newPath.isEmpty()) {
            onWorkspaceFileRenamed(path, newPath);
            return;
        }

        // Deleted or moved somewhere we cannot identify safely.
        if (m_watcher && m_watcher->files().contains(path))
            m_watcher->removePath(path);
        forgetFileIdentity(path);
        return;
    }

    if (pg->model->editorMode() == EditorMode::Image) {
        if (!m_watcher || !m_watcher->files().contains(path))
            watchFile(path);
        if (pg->imageViewer)
            pg->imageViewer->load(path);
        return;
    }

    if (pg->model->editorMode() == EditorMode::Pdf || pg->model->editorMode() == EditorMode::Html
        || pg->model->editorMode() == EditorMode::Svg) {
        if (!m_watcher || !m_watcher->files().contains(path))
            watchFile(path);
        pg->preview->showReadOnlyFile(path);
        return;
    }

    QByteArray raw;
    bool readSucceeded = false;
    QFile f(path);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (f.open(QIODevice::ReadOnly)) {
            raw = f.readAll();
            f.close();
            readSucceeded = true;
            break;
        }
        // File may still be held by the external writer (e.g. mid-rename);
        // give it a brief moment before retrying.
        QThread::msleep(30);
    }

    if (!m_watcher || !m_watcher->files().contains(path))
        watchFile(path);

    if (!readSucceeded) {
        onFileChanged(path);
        return;
    }

    if (fileContentsAreKnown(path, raw)) {
        clearExternalConflict(pg->model);
        return;
    }

    rememberFileContents(path, raw);

    const QString diskText = normalizeLoadedText(decodeAutoText(raw));
    const QString bufferText = normalizeLoadedText(pg->editor->toPlainText());

    if (diskText == bufferText) {
        // Metadata-only touch or an agent rewriting identical content.
        clearExternalConflict(pg->model);
        return;
    }

    if (!pg->model->isDirty() && !pg->model->hasExternalConflict()) {
        reloadDocumentSilently(idx, pg, raw);
        return;
    }

    // Real conflict: unsaved edits plus a genuine external content change.
    m_conflictDiskBytes.insert(pg->model, raw);
    const bool alreadyFlagged = pg->model->hasExternalConflict();
    pg->model->setExternalConflict(true);
    if (QTimer* t = m_autoSaveTimers.value(pg->model))
        t->stop();
    m_tabs->refreshTitle(idx);

    if (!alreadyFlagged)
        showExternalConflictDialog(pg->model, path);
}

// ---------------------------------------------------------------------------
void MainWindow::reloadDocumentSilently(int index, TabPage* page, const QByteArray& diskBytes)
{
    MarkdownEditor* ed = page->editor;
    QTextCursor cursor = ed->textCursor();
    const int pos = cursor.position();
    const int anchor = cursor.anchor();
    const int scrollV = ed->verticalScrollBar() ? ed->verticalScrollBar()->value() : 0;
    const int scrollH = ed->horizontalScrollBar() ? ed->horizontalScrollBar()->value() : 0;

    // A silent reload adopts the file's byte format too -- the buffer it
    // replaces is clean, so there is nothing of the user's to preserve.
    TextEncoding diskEncoding = TextEncoding::Utf8;
    const QString diskText = decodeAutoText(diskBytes, &diskEncoding);
    page->model->setEncoding(diskEncoding);
    page->model->setLineEnding(detectLineEnding(diskText));
    rememberFileContents(page->model->filePath(), diskBytes);
    const QString text = normalizeLoadedText(diskText);
    ed->setPlainText(text);

    QTextCursor updated = ed->textCursor();
    updated.setPosition(qMin(anchor, text.size()));
    updated.setPosition(qMin(pos, text.size()), QTextCursor::KeepAnchor);
    ed->setTextCursor(updated);
    if (ed->verticalScrollBar()) ed->verticalScrollBar()->setValue(scrollV);
    if (ed->horizontalScrollBar()) ed->horizontalScrollBar()->setValue(scrollH);

    page->model->setDirty(false);
    ed->document()->setModified(false);
    page->model->setExternalConflict(false);
    m_conflictDiskBytes.remove(page->model);
    m_tabs->refreshTitle(index);
    // The active tab's editor already bumps this via the connected
    // textChanged -> onTextChanged() chain, but a background tab's editor is
    // not connected (see connectEditor()/disconnectEditor()), so its content
    // change would otherwise go unnoticed and previewUpToDate() would report
    // a stale render as current the next time this tab is activated.
    ++m_editRevisions[page->model];
    if (index == m_tabs->currentIndex()) {
        updateWindowTitle();
        syncCurrentTabUi();
        updatePreview();
    }
}

// ---------------------------------------------------------------------------
void MainWindow::clearExternalConflict(DocumentModel* model)
{
    if (!model || !model->hasExternalConflict())
        return;
    model->setExternalConflict(false);
    m_conflictDiskBytes.remove(model);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model == model) {
            m_tabs->refreshTitle(i);
            break;
        }
    }
    if (model->isDirty())
        scheduleAutoSave(model);
}

// ---------------------------------------------------------------------------
void MainWindow::showExternalConflictDialog(DocumentModel* model, const QString& path)
{
    ThemedMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("File Changed Externally"));
    box.setText(
        tr("'%1' was modified outside FastMD, and this tab has unsaved changes.\n\n"
           "Auto Save is paused for this tab until you choose how to proceed.")
            .arg(QFileInfo(path).fileName()));
    QPushButton* reloadBtn = box.addButton(tr("Reload from Disk"), QMessageBox::DestructiveRole);
    QPushButton* keepBtn = box.addButton(tr("Keep My Version"), QMessageBox::AcceptRole);
    box.addButton(tr("Decide Later"), QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    bindDialogKey(&box, reloadBtn, Qt::Key_R);
    bindDialogKey(&box, keepBtn, Qt::Key_K);
    bindDialogKey(&box, box.buttons().constLast(), Qt::Key_L);
    box.exec();

    // The tab may have been closed (or the conflict already resolved some
    // other way) while the dialog was open.
    if (!model->hasExternalConflict())
        return;

    TabPage* pg = nullptr;
    int idx = -1;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model == model) {
            pg = m_tabs->pageAt(i);
            idx = i;
            break;
        }
    }
    if (!pg) {
        model->setExternalConflict(false);
        m_conflictDiskBytes.remove(model);
        return;
    }

    if (box.clickedButton() == reloadBtn) {
        reloadDocumentSilently(idx, pg, m_conflictDiskBytes.value(model));
    } else if (box.clickedButton() == keepBtn) {
        clearExternalConflict(model);
    }
    // "Decide Later" (or dismissing the dialog): leave the conflict state
    // and paused Auto Save exactly as-is.
}
