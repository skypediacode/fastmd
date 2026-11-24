#include "MainWindow.h"
#include "FontPreferences.h"
#include "TerminalVisibilityPreferences.h"
#include "SourceControlPanel.h"
#include "WorkspaceSearchPanel.h"
#include "GitDiffView.h"
#include "GitRunner.h"
#include "TabWidget.h"
#include "WindowTitleBar.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "ImageViewer.h"
#include "ImageFormats.h"
#include "FindController.h"
#include "FindOverlay.h"
#include "GoToLineDialog.h"
#include "MatrixInsertDialog.h"
#include "PreferencesDialog.h"
#include "SpellChecker.h"
#include "ExportManager.h"
#include "DocumentModel.h"
#include "OutlinePopup.h"
#include "RecentFilesManager.h"
#include "WorkspacePanelController.h"
#include "WorkspaceFileOps.h"
#include "ThemedMessageBox.h"
#include "StartupProfiler.h"
#include "SyntaxDefinitionResolver.h"
#include "LanguageSelectorPopup.h"
#include "DocumentSummary.h"
#include "DocumentSummaryDialog.h"
#include "AlignedMenu.h"
#include "TerminalLinks.h"
#include "TerminalPanel.h"
#include "DetachedTerminalWindow.h"
#include "ThemedSplitter.h"
#include "ApplicationTheme.h"

#include <QSettings>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QScrollBar>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QProcess>
#include <QVariant>
#include <QCoreApplication>
#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include "CommandShortcutRegistry.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QDesktopServices>
#include <QClipboard>
#include <QMessageBox>
#include <QGridLayout>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextCursor>
#include <QSignalBlocker>
#include <QPalette>
#include <QPushButton>
#include <QSplitter>
#include <QStyleOption>
#include <algorithm>
#include <cstdio>
#include <numeric>

void MainWindow::discardTabForDuplicate(int tabIndex)
{
    TabPage* page = m_tabs->pageAt(tabIndex);
    if (!page)
        return;
    DocumentModel* model = page->model;
    m_autoSaveSuspended.remove(model);
    DocumentSaveCoordinator::invalidate(model->saveState());
    m_ignoredFileChanges.clear(model->filePath());
    forgetFileContents(model->filePath());
    if (QTimer* timer = m_autoSaveTimers.take(model))
        timer->deleteLater();
    m_editRevisions.remove(model);
    m_renderedRevisions.remove(model);
    m_savingDocuments.remove(model);
    m_conflictDiskBytes.remove(model);
    model->setExternalConflict(false);
    cleanupRecoveryFor(model);
    m_tabs->closeTabAt(tabIndex);
    if (m_tabs->count() > 0) {
        m_tabs->setCurrentIndex(qMin(tabIndex, m_tabs->count() - 1));
        syncCurrentTabUi();
        updateWindowTitle();
        m_workspacePanelCtrl->updateRoot();
    }
}

// A tab transfer runs while Qt is still delivering the drag's mouse release into
// this window's tab bar, and QApplication::notify keeps walking that widget's
// parents afterwards. Destroying the window inline would pull that chain out
// from under it, so the last-tab close waits for the next event-loop turn.
void MainWindow::closeLaterIfEmpty()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_tabs && m_tabs->count() == 0)
            close();
    });
}

void MainWindow::transferTabTo(MainWindow* target, TabPage* page, int insertIndex)
{
    if (!target || !page || target == this)
        return;
    const int sourceIndex = m_tabs->indexOf(page);
    if (sourceIndex < 0)
        return;

    const QString pageKey = page->model->pathKey();
    if (!pageKey.isEmpty()) {
        int existingIndex = -1;
        TabPage* existing = nullptr;
        for (int i = 0; i < target->m_tabs->count(); ++i) {
            TabPage* candidate = target->m_tabs->pageAt(i);
            if (candidate && candidate->model->pathKey() == pageKey) {
                existing = candidate;
                existingIndex = i;
                break;
            }
        }
        if (existing) {
            const QString draggedText = page->editor->toPlainText();
            const QString existingText = existing->editor->toPlainText();
            const bool sameContent = draggedText == existingText;
            const bool draggedDirty = page->model->isDirty();
            const bool existingDirty = existing->model->isDirty();
            if (sameContent || !draggedDirty) {
                discardTabForDuplicate(sourceIndex);
                activateOpenDocument({ target, existing, existingIndex });
                closeLaterIfEmpty();
                return;
            }
            if (!existingDirty) {
                target->discardTabForDuplicate(existingIndex);
            } else {
                ThemedMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(tr("Duplicate File Conflict"));
                box.setText(tr("'%1' has unsaved changes in both windows.\n\n"
                               "Choose which version to keep.")
                                .arg(page->model->displayName()));
                auto* keepExisting = box.addButton(tr("Keep Existing"), QMessageBox::AcceptRole);
                auto* keepDragged = box.addButton(tr("Keep Dragged"), QMessageBox::DestructiveRole);
                auto* cancel = box.addButton(QMessageBox::Cancel);
                bindDialogKey(&box, keepExisting, Qt::Key_E);
                bindDialogKey(&box, keepDragged, Qt::Key_D);
                bindDialogKey(&box, cancel, Qt::Key_C);
                box.exec();
                if (box.clickedButton() == cancel)
                    return;
                if (box.clickedButton() == keepExisting) {
                    discardTabForDuplicate(sourceIndex);
                    activateOpenDocument({ target, existing, existingIndex });
                    closeLaterIfEmpty();
                    return;
                }
                if (box.clickedButton() == keepDragged)
                    target->discardTabForDuplicate(existingIndex);
            }
        }
    }

    disconnectEditor();
    const QString path = page->model->filePath();
    if (!path.isEmpty() && m_watcher && m_watcher->files().contains(path))
        m_watcher->removePath(path);

    DocumentModel* model = page->model;
    QTimer* timer = m_autoSaveTimers.take(model);
    const int remaining = timer && timer->isActive() ? timer->remainingTime() : -1;
    if (timer) {
        timer->stop();
        timer->setParent(target);
        disconnect(timer, nullptr, this, nullptr);
        connect(timer, &QTimer::timeout, target, [target, model]() { target->onAutoSaveTimeout(model); });
        target->m_autoSaveTimers.insert(model, timer);
    }
    if (m_editRevisions.contains(model))
        target->m_editRevisions.insert(model, m_editRevisions.take(model));
    // Not carried over: the target window has its own render epoch, so let the
    // moved tab re-render once there rather than trusting a foreign epoch.
    m_renderedRevisions.remove(model);
    const bool saveWasInFlight = m_savingDocuments.remove(model);
    if (saveWasInFlight) {
        DocumentSaveCoordinator::invalidate(model->saveState());
        m_ignoredFileChanges.clear(path);
    }
    // External-conflict state (and its paused Auto Save) doesn't carry
    // across windows cleanly, so drop it; the watcher in the target window
    // will re-detect a real mismatch on the next external write if needed.
    m_conflictDiskBytes.remove(model);
    model->setExternalConflict(false);

    TabPage* moved = m_tabs->takeTabPage(sourceIndex);
    target->m_tabs->insertTabPage(moved, insertIndex, true);
    if (!path.isEmpty())
        target->watchFile(path);
    if (timer && remaining >= 0)
        timer->start(qMax(1, remaining));
    if (saveWasInFlight)
        target->scheduleAutoSave(model);

    target->m_htmlDirty = true;
    target->m_previewDirty = true;
    target->syncCurrentTabUi();
    target->m_workspacePanelCtrl->updateRoot();
    target->updateWindowTitle();

    if (m_tabs->count() > 0) {
        m_tabs->setCurrentIndex(qMin(sourceIndex, m_tabs->count() - 1));
        syncCurrentTabUi();
        m_workspacePanelCtrl->updateRoot();
        updateWindowTitle();
    } else {
        closeLaterIfEmpty();
    }
}

// ---------------------------------------------------------------------------
void MainWindow::openPreferences()
{
    showPreferences(std::nullopt);
}

void MainWindow::openEditorPreferences()
{
    showPreferences(PreferencesTargets::Id::SpellCheck);
}

void MainWindow::showPreferences(std::optional<PreferencesTargets::Id> target)
{
    PreferencesDialog dlg(this);
    connect(&dlg, &PreferencesDialog::applicationReset, this, &MainWindow::onApplicationReset);
    dlg.setRestoreSessionOnStartup(m_restoreSessionOnStartup);
    dlg.setCloseAppOnLastTabClose(m_closeAppOnLastTabClose);
    dlg.setDefaultSaveFolder(m_defaultSaveFolder);
    dlg.setOpenPdfExternally(m_openPdfExternally);
    dlg.setOpenHtmlExternally(m_openHtmlExternally);
    dlg.setOpenImagesExternally(m_openImagesExternally);
    dlg.setShowExportSuccessDialog(m_showExportSuccessDialog);
    dlg.setDefaultNewDocMode(m_defaultNewDocMode);
    dlg.setAutoSaveEnabled(m_autoSaveEnabled);
    dlg.setAutoSaveDelayMs(m_autoSaveDelayMs);
    dlg.setCrashRecoveryEnabled(m_crashRecoveryEnabled);
    dlg.setHighlightCurrentLineEnabled(m_highlightCurrentLineEnabled);
    dlg.setRenderSingleLineBreaksEnabled(m_renderSingleLineBreaks);
    // Opening Preferences is one of the three surfaces allowed to trigger the
    // installed-language enumeration.
    ensureSpellCheckResolved();
    QVector<QPair<QString, QString>> spellLanguages;
    for (const SpellLanguage& language : SpellChecker::instance().availableLanguages())
        spellLanguages.append({ language.tag, language.displayName });
    dlg.setAvailableSpellLanguages(spellLanguages);
    dlg.setSpellCheckLanguage(m_spellCheckLanguage);
    dlg.setSpellCheckEnabled(m_spellCheckEnabled);
    dlg.setEditorFontFamily(ApplicationTheme::editorFontFamily());
    dlg.setPreviewFontFamily(m_previewFontFamily);
    dlg.setPrintFontFamily(m_pdfExportOptions.fontFamily);
    dlg.setDocumentSpacing(m_documentSpacing);
    // After the setters, which decide what is enabled and visible.
    if (target)
        dlg.revealTarget(*target);
    if (dlg.exec() == QDialog::Accepted) {
        m_restoreSessionOnStartup = dlg.restoreSessionOnStartup();
        m_closeAppOnLastTabClose = dlg.closeAppOnLastTabClose();
        m_defaultSaveFolder = dlg.defaultSaveFolder();
        m_openPdfExternally = dlg.openPdfExternally();
        m_openHtmlExternally = dlg.openHtmlExternally();
        m_openImagesExternally = dlg.openImagesExternally();
        m_showExportSuccessDialog = dlg.showExportSuccessDialog();
        m_defaultNewDocMode = dlg.defaultNewDocMode();
        m_autoSaveEnabled = dlg.autoSaveEnabled();
        m_autoSaveDelayMs = dlg.autoSaveDelayMs();
        m_crashRecoveryEnabled = dlg.crashRecoveryEnabled();
        m_highlightCurrentLineEnabled = dlg.highlightCurrentLineEnabled();
        const bool renderSingleLineBreaksChanged =
            m_renderSingleLineBreaks != dlg.renderSingleLineBreaksEnabled();
        m_renderSingleLineBreaks = dlg.renderSingleLineBreaksEnabled();
        m_tabs->setCurrentLineHighlightEnabled(m_highlightCurrentLineEnabled);
        for (QTimer* timer : m_autoSaveTimers)
            timer->setInterval(m_autoSaveDelayMs);
        // applySpellCheckState() persists on its own, so this must run before
        // the writeSettings() below rather than being folded into it.
        applySpellCheckState(dlg.spellCheckEnabled(), dlg.spellCheckLanguage());
        applyFontPreferences(dlg.editorFontFamily(), dlg.previewFontFamily(), dlg.printFontFamily());
        applyDocumentSpacing(dlg.documentSpacing());
        writeSettings();

        if (renderSingleLineBreaksChanged) {
            // Propagate the renderer option process-wide, but only rebuild each
            // window's active preview. Hidden tabs are invalidated and render
            // lazily when selected; synchronously converting every tab here can
            // freeze Preferences on large restored sessions and is immediately
            // duplicated by the revision gate on the next tab switch.
            for (QWidget* topLevel : QApplication::topLevelWidgets()) {
                auto* window = qobject_cast<MainWindow*>(topLevel);
                if (!window || !window->m_tabs)
                    continue;
                window->m_renderSingleLineBreaks = m_renderSingleLineBreaks;
                window->invalidateRenderedPreviews();
                window->m_htmlDirty = true;
                window->m_previewDirty = true;
                window->updatePreview();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Each surface is applied, propagated to every window, and persisted only when
// its family actually changed: an Editor change reinstalls the application
// stylesheet and a Preview change reflows every live preview, so an unchanged
// family must cost nothing. An empty family is stored as no key at all, which is
// exactly what an install that never touched this feature has.
void MainWindow::applyFontPreferences(const QString& editorFamily, const QString& previewFamily,
                                      const QString& printFamily)
{
    const QString preview = FontPreferences::normalizedFamily(previewFamily);
    const QString print = FontPreferences::normalizedFamily(printFamily);
    QSettings s;
    const auto persist = [&s](QLatin1StringView key, const QString& value) {
        if (value.isEmpty())
            s.remove(key);
        else
            s.setValue(key, value);
    };

    if (ApplicationTheme::applyEditorFontFamily(editorFamily))
        persist(FontPreferences::EditorKey, ApplicationTheme::editorFontFamily());

    const bool previewChanged = preview != m_previewFontFamily;
    const bool printChanged = print != m_pdfExportOptions.fontFamily;
    if (previewChanged)
        persist(FontPreferences::PreviewKey, preview);
    if (printChanged)
        persist(FontPreferences::PrintKey, print);
    if (!previewChanged && !printChanged)
        return;
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        if (previewChanged) {
            window->m_previewFontFamily = preview;
            window->m_tabs->setPreviewFontFamily(preview);
        }
        if (printChanged)
            window->m_pdfExportOptions.fontFamily = print;
    }
}

// ---------------------------------------------------------------------------
// Persisted only on a real change, the default as no key at all (an install that
// never touched this has none). Every window's previews restyle at once: a
// change posts three CSS variables, never content, so nothing is re-rendered.
void MainWindow::applyDocumentSpacing(const DocumentSpacing::Settings& requested)
{
    const DocumentSpacing::Settings spacing = DocumentSpacing::normalized(requested);
    if (spacing == m_documentSpacing)
        return;
    QSettings s;
    const auto persist = [&s](QLatin1StringView key, int value, int defaultValue) {
        if (value == defaultValue)
            s.remove(key);
        else
            s.setValue(key, value);
    };
    persist(DocumentSpacing::LineKey, spacing.line, DocumentSpacing::DefaultLine);
    persist(DocumentSpacing::ParagraphKey, spacing.paragraph, DocumentSpacing::DefaultParagraph);
    persist(DocumentSpacing::ListKey, spacing.list, DocumentSpacing::DefaultList);
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        window->m_documentSpacing = spacing;
        window->m_tabs->setDocumentSpacing(spacing);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::onApplicationReset(bool restartNow)
{
    m_isResetting = true;
    if (restartNow)
        restartApplication();
}

// ---------------------------------------------------------------------------
bool MainWindow::restartApplication()
{
    if (!close())
        return false;

    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   { QStringLiteral("--restart") });
}

// ---------------------------------------------------------------------------
// Derived on demand rather than cached alongside m_activeEditor/m_activePreview:
// the viewer dies with its tab's splitter, and a stale cached pointer would
// silently redirect editor zoom into freed memory.
ImageViewer* MainWindow::currentImageViewer() const
{
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    return page ? page->imageViewer : nullptr;
}

// ---------------------------------------------------------------------------
// Same reasoning as currentImageViewer(): derived on demand rather than cached.
GitDiffView* MainWindow::currentDiffView() const
{
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    return page ? page->diffView : nullptr;
}

// ---------------------------------------------------------------------------
void MainWindow::zoomIn()
{
    if (ImageViewer* viewer = currentImageViewer()) {
        viewer->zoomIn();
        return;
    }
    if (currentDiffView()) {
        setDiffZoomDelta(m_diffZoomDelta + 1);
        return;
    }
    if (!m_activeEditor)
        return;
    setEditorZoomLevel(m_activeEditor->font().pixelSize() + 1);
}

// ---------------------------------------------------------------------------
void MainWindow::zoomOut()
{
    if (ImageViewer* viewer = currentImageViewer()) {
        viewer->zoomOut();
        return;
    }
    if (currentDiffView()) {
        setDiffZoomDelta(m_diffZoomDelta - 1);
        return;
    }
    if (!m_activeEditor)
        return;
    setEditorZoomLevel(qMax(FastMdDefaults::MinimumPreviewFontSize,
                            m_activeEditor->font().pixelSize() - 1));
}

// ---------------------------------------------------------------------------
void MainWindow::resetZoom()
{
    if (ImageViewer* viewer = currentImageViewer()) {
        viewer->resetZoom();
        return;
    }
    if (currentDiffView()) {
        setDiffZoomDelta(0);
        return;
    }
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    if (page && (page->model->editorMode() == EditorMode::Html || page->model->editorMode() == EditorMode::Svg)
        && m_activePreview) {
        m_activePreview->resetHtmlZoom();
        return;
    }
    setEditorZoomLevel(FastMdDefaults::EditorFontSize);
}

// ---------------------------------------------------------------------------
void MainWindow::setEditorZoomLevel(int pixelSize)
{
    m_defaultEditorFontSize = pixelSize;
    QSettings settings;
    settings.setValue(QStringLiteral("editorFontSize"), pixelSize);

    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        window->m_tabs->setDefaultTabSettings(window->defaultTabSettingsForPath(),
                                              window->m_defaultNewDocMode);
        window->invalidateRenderedPreviews();

        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (!page || !page->model || !page->editor || !page->preview
                || isViewerMode(page->model->editorMode())) {
                continue;
            }

            QFont font = page->editor->font();
            font.setPixelSize(pixelSize);
            page->editor->setFont(font);
            page->preview->setBaseFontSize(pixelSize);
        }
    }
}

// ---------------------------------------------------------------------------
// Git diff tabs share one zoom level across every diff document in every
// window — unlike editable-document zoom, this is deliberately not tied to
// EditorFontSize/MinimumPreviewFontSize: it is a delta from each pane's own
// base font (see GitDiffPane::setZoomDelta), not an absolute pixel size.
void MainWindow::setDiffZoomDelta(int delta)
{
    m_diffZoomDelta = delta;
    QSettings settings;
    settings.setValue(QStringLiteral("gitDiffZoomDelta"), delta);

    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        window->m_diffZoomDelta = delta;
        window->m_tabs->setDiffZoomDelta(delta);

        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (page && page->diffView)
                page->diffView->setZoomDelta(delta);
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::ensureFindOverlay()
{
    if (m_findOverlay)
        return;

    m_findOverlay = new FindOverlay(m_findController, this);
    syncFindTarget();
    const bool dark = (m_theme == QStringLiteral("dark"))
        || (m_theme == QStringLiteral("system")
            && QApplication::palette().color(QPalette::Window).lightness() < 128);
    m_findOverlay->setDarkMode(dark);
}

// A PDF tab's Editor exists but is hidden and empty: searching it can only ever
// report "No results", and an invisible anchor leaves the overlay parked on the
// main window. Its Preview is the document, so it is both target and anchor.
void MainWindow::syncFindTarget()
{
    const TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    const bool pdf = page && page->model->editorMode() == EditorMode::Pdf;
    m_findController->setTarget(pdf ? nullptr : m_activeEditor,
                                pdf ? m_activePreview : nullptr);
    if (!m_findOverlay)
        return;
    m_findOverlay->setPdfMode(pdf);
    // Anchored to the editor pane itself (not the editor/preview splitter), so
    // the overlay parks at the editor's top-right corner -- Find/Replace is an
    // editing tool and should read as belonging to the editor, not floating
    // over the preview. It is a top-level window precisely so it can paint over
    // the preview's native WebView2 child -- see find-replace-plan.md.
    m_findOverlay->setAnchorWidget(pdf ? static_cast<QWidget*>(m_activePreview) : m_activeEditor);
    if (!m_activeEditor)
        m_findOverlay->closeOverlay();
}

// ---------------------------------------------------------------------------
MatrixInsertDialog* MainWindow::matrixDialog()
{
    if (!m_matrixDialog) {
        m_matrixDialog = new MatrixInsertDialog(this);
        if (m_activeEditor)
            m_matrixDialog->setEditor(m_activeEditor);
    }
    return m_matrixDialog;
}

void MainWindow::openMatrixInsertDialog()
{
    if (!m_activeEditor)
        return;
    matrixDialog()->setEditor(m_activeEditor);
    matrixDialog()->openDialog();
}

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

// ---------------------------------------------------------------------------
void MainWindow::updatePreview()
{
    if (!m_activeEditor || !m_activePreview) return;
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->editorMode() != EditorMode::Markdown || !pg->preview->isVisible())
        return;

    if (pg && pg->model) {
        m_activePreview->setBasePath(pg->model->filePath());
    }

    if (pg->model->markdownPresentation() == MarkdownPresentation::MindMap) {
        m_activePreview->setPresentation(PreviewWidget::Presentation::MindMap);
        m_activePreview->setMindMapMarkdown(m_activeEditor->toPlainText());
        if (m_firstPainted)
            m_activePreview->ensureInitialized();
        m_previewDirty = false;
        markPreviewRendered(pg);
        return;
    }

    m_activePreview->setPresentation(PreviewWidget::Presentation::Preview);

    if (m_htmlDirty) {
        // Hands the conversion to a worker for large documents; the result is
        // applied by applyConvertedHtml(). Small documents stay synchronous so
        // the very first paint and short edits keep their current behavior.
        updateCurrentHtmlAsync(pg);
        if (m_htmlDirty)
            return;                      // async: preview is pushed on completion
    }

    m_activePreview->setBodyHtml(m_currentHtml);
    if (m_firstPainted)
        m_activePreview->ensureInitialized();
    m_previewDirty = false;
    markPreviewRendered(pg);

    static bool s_firstRenderMarked = false;
    if (!s_firstRenderMarked) {
        s_firstRenderMarked = true;
        FASTMD_MARK("first preview render");
    }
}

// ---------------------------------------------------------------------------
void MainWindow::markPreviewRendered(TabPage* page)
{
    if (!page || !page->model)
        return;
    m_renderedRevisions.insert(page->model,
                               qMakePair(m_editRevisions.value(page->model, 0), m_renderEpoch));
}

bool MainWindow::previewUpToDate(TabPage* page) const
{
    if (!page || !page->model || !page->preview)
        return false;
    const auto it = m_renderedRevisions.constFind(page->model);
    if (it == m_renderedRevisions.cend())
        return false;
    return it->first == m_editRevisions.value(page->model, 0) && it->second == m_renderEpoch;
}

// ---------------------------------------------------------------------------
void MainWindow::updateCurrentHtml()
{
    if (!m_activeEditor || !m_htmlDirty)
        return;

    // KaTeX-mode preserves raw LaTeX ($…$, $$…$$, \(…\), \[…\]) so the browser
    // preview typesets real math with the bundled offline KaTeX.
    QString source = m_activeEditor->toPlainText();
    m_currentHtml = ExportManager::markdownToPreviewHtml(
        source, nullptr, m_renderSingleLineBreaks);
    m_previewSourceSnapshot = std::move(source);
    m_htmlDirty = false;
}

// ---------------------------------------------------------------------------
namespace {
// Below this the conversion costs well under a frame, so the thread hop (and
// the extra full copy of the source) is not worth it.
constexpr int kAsyncConversionMinChars = 40000;
}

void MainWindow::updateCurrentHtmlAsync(TabPage* page)
{
    if (!m_activeEditor || !m_htmlDirty || !page || !page->preview)
        return;

    QString source = m_activeEditor->toPlainText();
    if (source.size() < kAsyncConversionMinChars) {
        m_currentHtml = ExportManager::markdownToPreviewHtml(
            source, nullptr, m_renderSingleLineBreaks);
        m_previewSourceSnapshot = std::move(source);
        m_htmlDirty = false;
        return;
    }

    // One conversion in flight at a time: while it runs the debounce keeps
    // firing, and queueing every intermediate revision would just burn cores on
    // results that are stale before they arrive. The generation check makes the
    // final state converge — updatePreview() runs again on completion when the
    // document moved on.
    if (m_htmlConversionRunning)
        return;

    const quint64 generation = ++m_htmlGeneration;
    m_htmlConversionRunning = true;
    const QPointer<PreviewWidget> target(page->preview);
    const bool singleBreaks = m_renderSingleLineBreaks;
    // What the page holds now. If it still holds it when the result lands,
    // only the blocks that differ from it are sent (PreviewWidget decides).
    PreviewBlocksPtr base = page->preview->postedBlocks();

    // prepareBodyForBrowser() runs here rather than on the UI thread inside
    // PreviewWidget::pushContent(): it is a pure string pass that rebuilds the
    // whole body to rewrite every <img src>, and it is exactly as large as the
    // conversion output. So do the block split, the diff against `base` and the
    // JSON encoding of whatever will be posted: the UI thread only prepends a
    // sequence number. (`source` is captured by both lambdas, which costs two
    // QString references, not two copies — QString is implicitly shared.)
    auto* watcher = new QFutureWatcher<PreviewBodyUpdate>(this);
    connect(watcher, &QFutureWatcher<PreviewBodyUpdate>::finished, this,
            [this, watcher, generation, target, source]() {
                const PreviewBodyUpdate update = watcher->result();
                watcher->deleteLater();
                applyConvertedHtml(generation, target, source, update);
            });
    watcher->setFuture(QtConcurrent::run([source, singleBreaks, base = std::move(base)]() {
        QString html = ExportManager::markdownToPreviewHtml(source, nullptr, singleBreaks);
        QString prepared = PreviewWidget::prepareBodyForBrowser(html);
        return buildPreviewBodyUpdate(std::move(html), std::move(prepared), base);
    }));
}

void MainWindow::applyConvertedHtml(quint64 generation, const QPointer<PreviewWidget>& target,
                                    const QString& source, const PreviewBodyUpdate& update)
{
    m_htmlConversionRunning = false;

    // Superseded by a newer edit or a tab switch, or the tab went away.
    if (generation != m_htmlGeneration || !target || !m_activePreview
        || m_activePreview != target.data()) {
        if (m_htmlDirty && m_previewDirty)
            updatePreview();             // pick up whatever the current state is
        return;
    }

    m_currentHtml = update.html;
    m_previewSourceSnapshot = source;
    m_htmlDirty = false;

    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->preview != target.data() || pg->model->editorMode() != EditorMode::Markdown
        || !target->isVisible())
        return;

    target->applyBodyUpdate(update);
    if (m_firstPainted)
        target->ensureInitialized();
    m_previewDirty = false;
    markPreviewRendered(pg);

    // The document changed while we were converting: convert again from the
    // current text so the preview ends up matching the editor.
    if (m_activeEditor && m_activeEditor->toPlainText() != m_previewSourceSnapshot) {
        m_htmlDirty = true;
        m_previewDirty = true;
        updatePreview();
    }
}

void MainWindow::togglePreviewTask(int startLine, int endLine, bool checked)
{
    if (!m_activeEditor || !m_activePreview
        || m_activeEditor->toPlainText() != m_previewSourceSnapshot) {
        m_previewDirty = true;
        updatePreview();
        return;
    }

    const QString source = m_activeEditor->toPlainText();
    const QStringList lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const int first = qMax(1, startLine);
    const int last = qMin(endLine, lines.size());
    static const QRegularExpression task(
        QStringLiteral(R"(^((?:[ \t]*>[ \t]*)*[ \t]*(?:[-+*]|\d+[.)])[ \t]+\[)([ xX])(\]))"));
    int line = -1;
    int marker = -1;
    for (int i = first; i <= last; ++i) {
        const QRegularExpressionMatch match = task.match(lines.at(i - 1));
        if (match.hasMatch()) {
            line = i;
            marker = match.capturedStart(2);
            break;
        }
    }
    if (line < 0 || marker < 0)
        return;

    int absolute = marker;
    for (int i = 0; i < line - 1; ++i)
        absolute += lines.at(i).size() + 1;
    QScrollBar* scrollBar = m_activeEditor->verticalScrollBar();
    const int scrollValue = scrollBar->value();
    const bool wasSyncing = m_scrollSyncing;
    m_scrollSyncing = true;
    QTextCursor cursor = m_activeEditor->textCursor();
    const int oldPosition = cursor.position();
    const int oldAnchor = cursor.anchor();
    cursor.setPosition(absolute);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    cursor.insertText(checked ? QStringLiteral("x") : QStringLiteral(" "));
    const int maxPosition = qMax(0, m_activeEditor->document()->characterCount() - 1);
    cursor.setPosition(qMin(oldAnchor, maxPosition));
    cursor.setPosition(qMin(oldPosition, maxPosition),
                       QTextCursor::KeepAnchor);
    m_activeEditor->setTextCursor(cursor);
    scrollBar->setValue(scrollValue);
    m_scrollSyncing = wasSyncing;
}

// ---------------------------------------------------------------------------
void MainWindow::runPreviewBenchmark(const QString& file, int iters)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "[bench] cannot open %s\n", qUtf8Printable(file));
        QCoreApplication::exit(2);
        return;
    }
    const QString markdown = QString::fromUtf8(f.readAll());
    f.close();

    TabPage* pg = m_tabs->currentPage();
    if (!pg || !pg->editor || !pg->preview) {
        std::fprintf(stderr, "[bench] no active tab\n");
        QCoreApplication::exit(2);
        return;
    }
    // Force Markdown mode + preview visible so updatePreview() actually renders.
    // Goes through the centralized language operation like every other caller,
    // so the model's language state does not drift out of sync with the editor.
    setDocumentLanguage(pg, SyntaxDefinitionResolver::fastMdMarkdownDefinitionName(),
                        LanguageSelectionSource::Manual);
    setPreviewVisible(true, false);
    pg->editor->setPlainText(markdown);

    PreviewWidget* preview = pg->preview;

    // One backend-agnostic render: force a full re-parse + display, then pump
    // the event loop until previewRendered fires (sync backends fire during the
    // call and set done immediately; async backends fire from their callback).
    auto renderOnce = [this, preview]() {
        m_htmlDirty = true;
        m_previewDirty = true;
        bool done = false;
        QMetaObject::Connection c = connect(preview, &PreviewWidget::previewRendered,
                                            this, [&done]() { done = true; });
        updatePreview();
        while (!done)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
        disconnect(c);
    };

    // Warm up (JIT the markdown path, warm any caches, ensure WebView2 shell loaded).
    renderOnce();

    QList<qint64> parseUs;
    QList<qint64> totalUs;
    for (int i = 0; i < iters; ++i) {
        QElapsedTimer pt; pt.start();
        m_htmlDirty = true;
        updateCurrentHtml();               // markdown -> HTML only
        parseUs.append(pt.nsecsElapsed() / 1000);

        QElapsedTimer tt; tt.start();
        bool done = false;
        m_htmlDirty = true;                // force updateCurrentHtml to redo inside updatePreview
        QMetaObject::Connection c = connect(preview, &PreviewWidget::previewRendered,
                                            this, [&done]() { done = true; });
        updatePreview();
        while (!done)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
        disconnect(c);
        totalUs.append(tt.nsecsElapsed() / 1000);
    }

    auto median = [](QList<qint64> v) -> qint64 {
        if (v.isEmpty()) return 0;
        std::sort(v.begin(), v.end());
        return v.at(v.size() / 2);
    };
    std::fprintf(stderr, "[bench] file=%s iters=%d parse_median_us=%lld total_median_us=%lld parse_min_us=%lld total_min_us=%lld\n",
                 qUtf8Printable(QFileInfo(file).fileName()), iters,
                 (long long)median(parseUs), (long long)median(totalUs),
                 (long long)*std::min_element(parseUs.begin(), parseUs.end()),
                 (long long)*std::min_element(totalUs.begin(), totalUs.end()));
    std::fflush(stderr);
    QCoreApplication::exit(0);
}

// ---------------------------------------------------------------------------
void MainWindow::togglePreview()
{
    setPreviewVisible(!isPreviewVisible());
}

void MainWindow::toggleReadingMode()
{
    setReadingMode(!isReadingModeActive());
}

void MainWindow::toggleNotepadMode()
{
    setNotepadMode(!isNotepadModeActive());
}

// The title bar's Workspace control: the sidebar's visibility, in whatever mode
// it is in. Notepad Mode is why it is hidden, so asking for it leaves Notepad
// Mode -- which restores the layout from before -- and then shows the panel if
// that layout did not already include it.
void MainWindow::toggleWorkspacePanel()
{
    if (!m_workspacePanelCtrl)
        return;
    if (isNotepadModeActive()) {
        setNotepadMode(false);
        if (!m_workspacePanelCtrl->isVisible())
            m_workspacePanelCtrl->setVisible(true);
        return;
    }
    m_workspacePanelCtrl->setVisible(!m_workspacePanelCtrl->isVisible());
}

bool MainWindow::terminalOpensDetached()
{
    // Detached is the default position: the key is only written false when the
    // user attaches the panel, and true again whenever it is detached, closed
    // while floating, or left floating at shutdown.
    return QSettings().value(QStringLiteral("terminal/detached"), true).toBool();
}

void MainWindow::toggleTerminal()
{
    // m_terminalVisible is the persisted intent and can be true in a window
    // that has no panel yet -- before the restore task runs, or in an
    // auxiliary window, which never restores one. Toggling must follow what is
    // actually on screen, or the first press would appear to do nothing.
    // A minimized floating terminal is not on screen whatever the flags say,
    // and asking for the terminal while looking at a taskbar button means
    // "bring it back", not "close it".
    if (m_detachedTerminalWindow && m_detachedTerminalWindow->isMinimized()) {
        raiseDetachedTerminal(true);
        return;
    }
    const bool onScreen = m_terminalVisible
        && (m_terminalPanel != nullptr || m_detachedTerminalWindow != nullptr);
    if (onScreen)
        closeTerminalPanel();
    else
        setTerminalVisible(true);
}

void MainWindow::ensureTerminalPanel()
{
    if (m_terminalPanel || !m_editorTerminalSplitter)
        return;

    m_terminalPanel = new TerminalPanel(m_editorTerminalSplitter);
    m_terminalPanel->setDarkMode(ApplicationTheme::isDark());
    // Resolved per new terminal, never cached: that is what makes a workspace
    // change affect the *next* terminal while leaving running shells alone.
    m_terminalPanel->setWorkingDirectoryProvider([this] { return currentWorkspaceFolder(); });

    m_editorTerminalSplitter->addWidget(m_terminalPanel);
    // Both sides must participate in vertical growth. QSplitter uses the
    // current sizes as the effective stretch weights, so this preserves the
    // ratio established by the opening layout or by a user drag.
    m_editorTerminalSplitter->setStretchFactor(1, 1);
    // ThemedSplitter paints its handle in C++ rather than QSS, and it only
    // themes handle(1) -- which does not exist until the panel is added.
    if (auto* themed = dynamic_cast<ThemedSplitter*>(m_editorTerminalSplitter))
        themed->setDarkMode(ApplicationTheme::isDark());

    connect(m_terminalPanel, &TerminalPanel::closeRequested, this, [this] {
        closeTerminalPanel();
    });
    connect(m_terminalPanel, &TerminalPanel::hideRequested, this, [this] {
        closeTerminalPanel();
    });
    connect(m_terminalPanel, &TerminalPanel::detachRequested, this, &MainWindow::detachTerminalPanel);
    connect(m_terminalPanel, &TerminalPanel::attachRequested, this, &MainWindow::reattachTerminalPanel);
    connect(m_terminalPanel, &TerminalPanel::toggleRequested, this, &MainWindow::toggleTerminal);
    connect(m_terminalPanel, &TerminalPanel::commandPaletteRequested,
            this, &MainWindow::openCommandPalette);
    connect(m_terminalPanel, &TerminalPanel::readingModeRequested,
            this, &MainWindow::toggleReadingMode);
    connect(m_terminalPanel, &TerminalPanel::notepadModeRequested,
            this, &MainWindow::toggleNotepadMode);
    connect(m_terminalPanel, &TerminalPanel::findInFilesRequested,
            this, &MainWindow::openWorkspaceSearch);
    connect(m_terminalPanel, &TerminalPanel::fileLinkActivated,
            this, &MainWindow::openTerminalFileLink);
    connect(m_terminalPanel, &TerminalPanel::workspaceSearchRequested, this, [this] {
        if (!m_workspacePanelCtrl)
            return;
        if (isNotepadModeActive())
            setNotepadMode(false);
        m_workspacePanelCtrl->setPanelMode(WorkspacePanelController::PanelMode::Workspace);
        if (!m_workspacePanelCtrl->isVisible())
            m_workspacePanelCtrl->setVisible(true);
        m_workspacePanelCtrl->focusSearch();
    });
}

void MainWindow::openTerminalFileLink(const QString& path, int line, int column)
{
    const QString suffix = QFileInfo(path).suffix();
    const bool viewer = suffix.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0
        || ImageFormats::isImageFile(path);
    const OpenDocumentRef existing = findOpenDocument(path);
    // openFilePath()'s default path hands anything it cannot edit to the
    // shell, which would run an .exe or .bat named in terminal output. Decide
    // here instead, and pass openUnsupportedAsSource so even a file that stops
    // looking like text before it is read opens as a source tab, not a program.
    if (!existing.page
        && (TerminalLinks::isShellShortcut(path)
            || (!viewer && !SyntaxDefinitionResolver::isLikelyEditableTextFile(path)))) {
        WorkspaceFileOps::revealInExplorer(path, false);
        return;
    }

    // Detached, the Terminal is its own window; the document lands here.
    if (!existing.page || existing.window == this) {
        if (isMinimized())
            showNormal();
        raise();
        activateWindow();
    }
    openFilePath(path, true);
    if (line <= 0)
        return;
    const OpenDocumentRef opened = findOpenDocument(path);
    if (!opened.page || !opened.page->editor || opened.page->model->isViewer())
        return;
    // Positioned on the next turn, after the tab activation settles -- the
    // same order NavigationHistory restores use.
    const QPointer<MarkdownEditor> editor(opened.page->editor);
    QTimer::singleShot(0, editor, [editor, line, column] {
        if (editor)
            editor->goToLine(line, column);
    });
}

void MainWindow::persistDockedTerminalHeight()
{
    if (!m_editorTerminalSplitter || !m_terminalPanel
        || !m_terminalPanelHasDockedLayout
        || !m_terminalPanel->isVisible()
        || m_terminalPanel->parentWidget() != m_editorTerminalSplitter)
        return;

    const QList<int> sizes = m_editorTerminalSplitter->sizes();
    if (sizes.size() > 1 && sizes.at(1) > 0)
        QSettings().setValue(QStringLiteral("terminal/panelHeight"), sizes.at(1));
}

void MainWindow::closeTerminalPanel()
{
    // Closing a detached shell reparents the live panel before the shell is
    // deleted. Do that before hiding it so View > Terminal and F6 have the
    // exact same result as the panel header's Close button.
    const bool wasDetached = m_detachedTerminalWindow != nullptr;
    if (m_detachedTerminalWindow) {
        // The fold-back must land on a *hidden* panel. reattachTerminalPanel()
        // branches on m_terminalVisible, and with it still true it inserts the
        // panel into the splitter, gives it a height and shows it -- and since
        // the terminal is a native (WebView2-hosted) child window, that
        // reparent/resize hits the screen synchronously, so the terminal
        // visibly docks at the bottom of the main window for a frame before
        // setTerminalVisible(false) below takes it away again. That is the
        // "it attaches first, then disappears" flicker. Clearing the flag
        // first sends the fold-back down the hidden branch, so the panel is
        // never laid out on screen at all; it is restored immediately after so
        // setTerminalVisible() still sees the state change and persists it.
        m_terminalVisible = false;
        m_detachedTerminalWindow->close();
        m_terminalVisible = true;
    }
    setTerminalVisible(false);
    // The fold-back wrote "docked", but closing a floating terminal is not a
    // request to dock it -- only Attach is (see closeEvent(), same reason).
    if (wasDetached)
        QSettings().setValue(QStringLiteral("terminal/detached"), true);
}

void MainWindow::setTerminalVisible(bool visible, bool takeFocus)
{
    const bool changed = m_terminalVisible != visible;
    m_terminalVisible = visible;

    // This is the sole write authority for terminal/visible.  In particular,
    // do not fold it into writeSettings(): that general session/settings sweep
    // is allowed to run during startup for unrelated UI work, while terminal
    // restoration is intentionally deferred until after first paint.
    if (changed) {
        QSettings settings;
        TerminalVisibilityPreferences::write(settings, visible);
    }

    if (!visible) {
        if (m_terminalPanel) {
            // Remember the height only while it is actually on screen, so
            // hiding never persists a collapsed size.
            persistDockedTerminalHeight();
            m_terminalPanel->setVisible(false);
        }
        if (m_actTerminal) {
            const QSignalBlocker blocker(m_actTerminal);
            m_actTerminal->setChecked(false);
        }
        return;
    }

    ensureTerminalPanel();
    if (!m_terminalPanel)
        return;

    // The terminal opens in its own window unless the user last attached it:
    // terminalOpensDetached() defaults to true, and only Attach writes false.
    // Building it straight into the floating window is what restoreTerminalPanel()
    // already does -- showing it docked first lays the splitter out twice and
    // starts WebView2 against a parent HWND it is about to be reparented out of.
    if (m_detachedTerminalWindow || terminalOpensDetached()) {
        if (m_detachedTerminalWindow)
            m_terminalPanel->setVisible(true);
        else
            moveTerminalToDetachedWindow(false);
        if (!m_detachedTerminalWindow)
            return;
        raiseDetachedTerminal(takeFocus);
        if (m_actTerminal) {
            const QSignalBlocker blocker(m_actTerminal);
            m_actTerminal->setChecked(true);
        }
        m_terminalPanel->ensureInitialSession(takeFocus);
        if (takeFocus)
            m_terminalPanel->focusTerminal();
        return;
    }

    m_terminalPanel->setVisible(true);

    const int total = m_editorTerminalSplitter->height();
    const int handle = m_editorTerminalSplitter->handleWidth();
    int terminalHeight = QSettings().value(QStringLiteral("terminal/panelHeight"), 0).toInt();
    // 36 is the fixed header height, not a usable docked-panel preference.
    // Earlier builds could persist that transient allocation while creating a
    // direct-to-detached panel. Values below the normal opening minimum use
    // the default layout and are replaced when the user next closes/detaches
    // a genuinely docked panel.
    if (terminalHeight < 120)
        terminalHeight = qMax(180, total / 3);
    terminalHeight = qBound(120, terminalHeight, qMax(160, total - 160));
    m_editorTerminalSplitter->setSizes({ total - terminalHeight - handle, terminalHeight });
    m_terminalPanelHasDockedLayout = true;

    if (m_actTerminal) {
        const QSignalBlocker blocker(m_actTerminal);
        m_actTerminal->setChecked(true);
    }

    // Starting the first shell is what actually makes the panel useful; a
    // failure leaves the panel open showing why.
    m_terminalPanel->ensureInitialSession(takeFocus);
    if (takeFocus)
        m_terminalPanel->focusTerminal();
}

void MainWindow::restoreTerminalPanel()
{
    // The user may have toggled the terminal off between first paint and this
    // task; the live flag wins over the one the session was restored with.
    if (!m_terminalVisible)
        return;
    // They may equally have opened it themselves in that window -- their
    // layout stands rather than being rearranged underneath them.
    if (m_terminalPanel)
        return;

    // The docked/detached layout at last shutdown is the source of truth (see
    // closeEvent()). A detached terminal is built straight into its floating
    // window: showing it docked first would lay the splitter out twice and
    // start WebView2 against a parent HWND it is about to be reparented out of.
    if (terminalOpensDetached()) {
        ensureTerminalPanel();
        moveTerminalToDetachedWindow(false);
        if (m_terminalPanel)
            m_terminalPanel->ensureInitialSession(false);
        return;
    }
    setTerminalVisible(true, false);
}

void MainWindow::detachTerminalPanel()
{
    moveTerminalToDetachedWindow(true);
}

void MainWindow::moveTerminalToDetachedWindow(bool takeFocus)
{
    if (m_detachedTerminalWindow)
        return;
    ensureTerminalPanel();
    if (!m_terminalPanel || !m_editorTerminalSplitter)
        return;

    // Preserve a user-established docked size. A panel created directly for a
    // detached window has never had one, so its transient header-only splitter
    // allocation must not overwrite the saved preference.
    persistDockedTerminalHeight();

    // The splitter reparents the panel directly into the new window's layout
    // in one step (never parentless in between), the same precaution
    // TabWidget::insertTabPage() takes for the WebView2-hosted Preview.
    // Do not make the floating shell a child/owned window of MainWindow.
    // Windows forces an owned top-level to remain above its owner, preventing
    // either window from being brought in front of the other.
    auto* window = new DetachedTerminalWindow(m_terminalPanel);
    m_detachedTerminalWindow = window;
    m_terminalPanel->setDetached(true);
    // setParent() clears the visible state, and on the restore path the panel
    // was never shown in the splitter to begin with.
    m_terminalPanel->setVisible(true);

    connect(window, &DetachedTerminalWindow::reattachRequested, this, [this, window] {
        if (m_detachedTerminalWindow != window)
            return;
        m_detachedTerminalWindow = nullptr;
        reattachTerminalPanel();
    });

    window->show();
    if (takeFocus) {
        window->raise();
        window->activateWindow();
        m_terminalPanel->focusTerminal();
    }
}

void MainWindow::raiseDetachedTerminal(bool takeFocus)
{
    if (!m_detachedTerminalWindow)
        return;
    m_detachedTerminalWindow->show();
    if (m_detachedTerminalWindow->isMinimized()) {
        // Clearing just the minimized bit, rather than showNormal(), keeps a
        // window that was maximized when it was minimized maximized.
        m_detachedTerminalWindow->setWindowState(
            m_detachedTerminalWindow->windowState() & ~Qt::WindowMinimized);
    }
    if (!takeFocus)
        return;
    m_detachedTerminalWindow->raise();
    m_detachedTerminalWindow->activateWindow();
    if (m_terminalPanel)
        m_terminalPanel->focusTerminal();
}

void MainWindow::reattachTerminalPanel()
{
    if (m_detachedTerminalWindow) {
        // Triggers DetachedTerminalWindow::closeEvent(), which reparents the
        // panel out before this window (empty by then) self-deletes.
        m_detachedTerminalWindow->close();
        return;
    }
    if (!m_terminalPanel || !m_editorTerminalSplitter)
        return;
    if (m_terminalPanel->parentWidget() == m_editorTerminalSplitter)
        return;

    // QSplitter::insertWidget() *shows* the widget it is handed unless that
    // widget was explicitly hidden beforehand: its needShow test is
    // `isVisible() && !(w->isHidden() && w->testAttribute(WA_WState_ExplicitShowHide))`,
    // and QWidget::setParent() on a widget that was visible hides it while
    // clearing WA_WState_ExplicitShowHide -- so the splitter shows it again,
    // laid out at its size hint, before the setVisible(false) below can take
    // it away. With a native (WebView2-hosted) terminal that reparent-and-show
    // reaches the screen synchronously rather than at the next paint, which is
    // the "it docks at the bottom of the window first, then disappears"
    // flicker when a detached terminal is closed. Hiding it up front sets the
    // explicit flag (and keeps it set, since setParent() only clears it for a
    // widget that was still visible), so the panel is never shown at all on
    // the way out.
    if (!m_terminalVisible)
        m_terminalPanel->hide();
    m_terminalPanel->setParent(m_editorTerminalSplitter);
    m_editorTerminalSplitter->insertWidget(1, m_terminalPanel);
    m_editorTerminalSplitter->setStretchFactor(1, 1);
    m_terminalPanel->setDetached(false);

    if (m_terminalVisible) {
        m_terminalPanel->setVisible(true);
        const int total = m_editorTerminalSplitter->height();
        const int handle = m_editorTerminalSplitter->handleWidth();
        int terminalHeight = QSettings().value(QStringLiteral("terminal/panelHeight"), 0).toInt();
        // See the matching direct-dock path in setTerminalVisible().
        if (terminalHeight < 120)
            terminalHeight = qMax(180, total / 3);
        terminalHeight = qBound(120, terminalHeight, qMax(160, total - 160));
        m_editorTerminalSplitter->setSizes({ total - terminalHeight - handle, terminalHeight });
        m_terminalPanelHasDockedLayout = true;
        m_terminalPanel->focusTerminal();
    } else {
        m_terminalPanel->setVisible(false);
    }
}

void MainWindow::openFolderInInternalTerminal(const QString& folder)
{
    if (folder.isEmpty())
        return;

    // Detach/reattach state is left exactly as it is -- this only needs the
    // panel visible and focused, docked or floating.
    if (m_detachedTerminalWindow) {
        raiseDetachedTerminal(true);
    } else {
        setTerminalVisible(true);
    }

    if (!m_terminalPanel)
        return;

    m_terminalPanel->openNewTerminalAt(folder);
    m_terminalPanel->focusTerminal();
}

void MainWindow::revealTerminal()
{
    if (m_detachedTerminalWindow) {
        raiseDetachedTerminal(true);
        return;
    }
    // setTerminalVisible() re-applies the docked height, so it is only for a
    // panel that is not on screen yet.
    if (m_terminalVisible && m_terminalPanel && m_terminalPanel->isVisible()) {
        m_terminalPanel->focusTerminal();
        return;
    }
    setTerminalVisible(true);
}

void MainWindow::openTerminalAtFile(const QString& filePath)
{
    const QFileInfo file(filePath);
    if (file.isFile())
        openFolderInInternalTerminal(file.absolutePath());
}

void MainWindow::openTerminalAtWorkspace(const QString& workspacePath)
{
    const QFileInfo workspace(workspacePath);
    if (workspace.isDir())
        openFolderInInternalTerminal(workspace.absoluteFilePath());
}

void MainWindow::toggleMindMap()
{
    setMindMapVisible(!isMindMapVisible());
}

NavigationHistory::Location MainWindow::currentNavigationLocation() const
{
    NavigationHistory::Location location;
    if (!m_activeEditor || !m_tabs || !m_tabs->currentModel())
        return location;
    location.path = m_tabs->currentModel()->filePath();
    if (location.path.isEmpty())
        return {};
    const QTextCursor cursor = m_activeEditor->textCursor();
    location.anchor = cursor.anchor();
    location.position = cursor.position();
    location.line = cursor.blockNumber() + 1;
    if (auto* bar = m_activeEditor->verticalScrollBar())
        location.verticalScroll = bar->value();
    if (auto* bar = m_activeEditor->horizontalScrollBar())
        location.horizontalScroll = bar->value();
    return location;
}

void MainWindow::recordNavigationDeparture()
{
    if (m_restoringNavigation)
        return;
    const auto location = currentNavigationLocation();
    if (!location.path.isEmpty())
        m_navigationHistory.push(location);
    updateNavigationActions();
}

void MainWindow::restoreNavigationLocation(const NavigationHistory::Location& location)
{
    if (location.path.isEmpty() || !QFileInfo::exists(location.path))
        return;

    m_restoringNavigation = true;
    const OpenDocumentRef open = findOpenDocument(location.path);
    if (open.page) {
        activateOpenDocument(open);
    } else {
        openFilePath(location.path);
    }

    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    MarkdownEditor* editor = page ? page->editor : nullptr;
    if (!editor)
    {
        m_restoringNavigation = false;
        return;
    }
    QTimer::singleShot(0, this, [this, editor, location]() {
        if (!editor)
            return;
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(qBound(0, location.anchor, editor->toPlainText().size()));
        cursor.setPosition(qBound(0, location.position, editor->toPlainText().size()),
                           QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        if (auto* bar = editor->verticalScrollBar())
            bar->setValue(location.verticalScroll);
        if (auto* bar = editor->horizontalScrollBar())
            bar->setValue(location.horizontalScroll);
        m_lastNavigationLine = cursor.blockNumber() + 1;
        m_restoringNavigation = false;
        onCursorPositionChanged();
        updateNavigationActions();
    });
}

void MainWindow::navigateBack()
{
    const auto current = currentNavigationLocation();
    if (current.path.isEmpty())
        return;
    const auto target = m_navigationHistory.goBack(current, [](const auto& location) {
        return QFileInfo::exists(location.path);
    });
    if (target)
        restoreNavigationLocation(*target);
    updateNavigationActions();
}

void MainWindow::navigateForward()
{
    const auto current = currentNavigationLocation();
    if (current.path.isEmpty())
        return;
    const auto target = m_navigationHistory.goForward(current, [](const auto& location) {
        return QFileInfo::exists(location.path);
    });
    if (target)
        restoreNavigationLocation(*target);
    updateNavigationActions();
}

void MainWindow::updateNavigationActions()
{
    if (m_actBack)
        m_actBack->setEnabled(m_navigationHistory.canGoBack());
    if (m_actForward)
        m_actForward->setEnabled(m_navigationHistory.canGoForward());
}

void MainWindow::goToPreview()
{
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!page || !page->editor || !page->preview
        || page->model->editorMode() != EditorMode::Markdown)
        return;

    const QTextCursor cursor = page->editor->textCursor();
    recordNavigationDeparture();
    const int sourcePosition = cursor.hasSelection() ? cursor.selectionStart() : cursor.position();
    const QTextBlock block = page->editor->document()->findBlock(sourcePosition);
    if (!block.isValid())
        return;
    const int line = block.blockNumber() + 1;
    PreviewWidget* preview = page->preview;

    // A render can be pending when this action is invoked. Repeating the same
    // reverse lookup after that render keeps the jump on the new DOM without
    // doing any Markdown parsing or maintaining a second mapping.
    connect(preview, &PreviewWidget::previewRendered, this,
            [this, preview, line]() {
                if (preview == m_activePreview)
                    preview->scrollToSourceLine(line, true);
            }, Qt::SingleShotConnection);

    if (!preview->isVisible())
        setPreviewVisible(true);
    preview->ensureInitialized();
    preview->scrollToSourceLine(line, true);

    // Navigation is initiated from the editor; keep native keyboard focus and
    // the caret/selection there while the WebView2 child performs the jump.
    page->editor->restoreKeyboardFocus();
}

// ---------------------------------------------------------------------------
void MainWindow::setWordWrap(bool enabled)
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    // A diff tab has no editor on screen and no file path of its own -- its
    // wrap state lives entirely on the GitDiffView, independent of every
    // other tab's state, and is not persisted (diff tabs do not survive a
    // session restore either; see the Source Control architecture notes).
    if (pg && pg->model->editorMode() == EditorMode::Diff) {
        if (pg->diffView)
            pg->diffView->setWordWrapEnabled(enabled);
    } else if (m_activeEditor) {
        m_activeEditor->setWordWrapEnabled(enabled);
        QString path = m_activeEditor->documentPath();
        if (!path.isEmpty()) {
            m_wordWrapByPath.insert(path, enabled);
            QSettings s;
            s.setValue(QStringLiteral("WordWrap/") + path, enabled);
        } else {
            m_wordWrapEnabled = enabled;
            writeSettings();
        }
    } else {
        m_wordWrapEnabled = enabled;
        writeSettings();
    }

    if (m_actWordWrap) {
        QSignalBlocker blocker(m_actWordWrap);
        m_actWordWrap->setChecked(enabled);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::setPreviewVisible(bool visible, bool persist)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->editorMode() != EditorMode::Markdown)
        return;

    // While Reading Mode is on, it -- not the document -- is why the Preview
    // fills the tab, so hiding the Preview means leaving Reading Mode rather
    // than recording a second, contradictory preference underneath it.
    const bool leavingReadingMode = isReadingModeActive() && !visible;
    if (leavingReadingMode) {
        pg->model->setReadingModeOverride(false);
        setReadingModeFlag(false);
        stopReadingModeFind();
    }
    // Symmetrically, Notepad Mode is why the Preview is suppressed while it is
    // on, so asking to show it again means leaving Notepad Mode rather than
    // recording a preference that Notepad Mode would just keep overriding.
    if (m_notepadMode && visible)
        setNotepadMode(false);

    pg->model->setMarkdownPreviewVisible(visible);
    if (persist) {
        m_markdownPreviewVisible = visible;
        writeSettings();
    }

    syncCurrentTabUi();
    if (leavingReadingMode) {
        restoreDocumentSplitterSizes();
        if (pg->editor && pg->editor->isVisible())
            pg->editor->restoreKeyboardFocus();
    }
    if (visible && m_previewDirty)
        updatePreview();
}

// ---------------------------------------------------------------------------
// Reading Mode is window-level state, but it only *applies* to an editable
// Markdown tab: the viewer modes have no editor to hide, and Code/Text tabs
// have no preview to read. On those the flag survives untouched so switching
// back to a Markdown tab resumes reading where it left off.
bool MainWindow::isReadingModeActive() const
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || !pg->model || pg->model->editorMode() != EditorMode::Markdown)
        return false;
    // Either the window-level override (F11) or the tab's own one (the bundled
    // User Guide, which reads without changing anything for other tabs).
    return m_readingMode || pg->model->readingModeOverride();
}

void MainWindow::setReadingMode(bool enabled, bool clearTabOverride)
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;

    if (enabled) {
        if (isReadingModeActive())
            return;
        if (!pg || !pg->model || pg->model->editorMode() != EditorMode::Markdown) {
            syncCurrentTabUi();   // re-assert the actions' unchecked state
            return;
        }
        // Reading Mode and Notepad Mode both collapse the Editor/Preview split
        // to a single pane, in opposite directions -- only one may own it.
        if (m_notepadMode)
            setNotepadMode(false);
        // Captured before anything is hidden, so this is the layout the user
        // was actually looking at. Nothing here is persisted: the temporary
        // one-pane layout must never become the saved Editor/Preview split.
        m_readingModeSplitterSizes = pg->splitter ? pg->splitter->sizes() : QList<int>{};
        setReadingModeFlag(true);
        syncCurrentTabUi();
        // The Editor just lost its native focus by being hidden; hand keyboard
        // control to the Preview so scrolling works without a click.
        focusReadingModePreview();
        return;
    }

    const bool perTab = clearTabOverride && pg && pg->model && pg->model->readingModeOverride();
    if (!m_readingMode && !perTab)
        return;
    if (perTab)
        pg->model->setReadingModeOverride(false);
    setReadingModeFlag(false);
    // The find bar and its highlights belong to Reading Mode's search; leaving
    // must not strand them over a Preview that is now half the window.
    stopReadingModeFind();
    syncCurrentTabUi();
    restoreDocumentSplitterSizes();
    if (pg && pg->editor && pg->editor->isVisible())
        pg->editor->restoreKeyboardFocus();
}

void MainWindow::restoreDocumentSplitterSizes()
{
    const QList<int> saved = m_readingModeSplitterSizes;
    m_readingModeSplitterSizes.clear();

    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || !pg->splitter || saved.size() < 2)
        return;
    if (!pg->editor || !pg->editor->isVisible() || !pg->preview || !pg->preview->isVisible())
        return;

    const int total = std::accumulate(saved.cbegin(), saved.cend(), 0);
    if (total <= 0)
        return;

    // Reapplied as a ratio, not as the raw pixel sizes: the window may have
    // been resized while Reading Mode was on, and absolute sizes would then
    // restore a split the user never chose.
    const int available = pg->splitter->width() - pg->splitter->handleWidth();
    if (available <= 0)
        return;
    const int first = qBound(0, qRound(double(available) * double(saved.at(0)) / double(total)), available);
    pg->splitter->setSizes({ first, available - first });
}

void MainWindow::leaveReadingModeForEditing(bool clearTabOverride)
{
    if (isReadingModeActive()) {
        setReadingMode(false, clearTabOverride);
        return;
    }
    // Reading Mode was on but unapplied (a Code/Text or viewer tab is current).
    // Clearing the flag alone would leave a find bar open on whichever Markdown
    // tab was being read, over a Preview that is no longer full width.
    setReadingModeFlag(false);
    stopReadingModeFind();
}

void MainWindow::setReadingModeFlag(bool enabled)
{
    m_readingMode = enabled;
    if (m_tabs)
        m_tabs->setReadingMode(enabled);
}

// Notepad Mode is Reading Mode's mirror: window-level, hides the Preview
// (instead of the Editor) plus the Workspace Panel, and never touches either's
// persisted preference -- the user's real layout is neither overwritten nor
// saved while it is on.
void MainWindow::setNotepadMode(bool enabled)
{
    if (enabled == m_notepadMode)
        return;

    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;

    if (enabled) {
        // Mirrors setReadingMode(true): the two modes fight over the same
        // Editor/Preview splitter, so only one may be active at a time.
        if (isReadingModeActive())
            setReadingMode(false);

        m_notepadModeWorkspaceWasVisible = m_workspacePanelCtrl && m_workspacePanelCtrl->isVisible();
        // Captured before anything is hidden, same as Reading Mode -- and reused
        // from the same member, since the two never hide the splitter at once.
        m_readingModeSplitterSizes = pg && pg->splitter ? pg->splitter->sizes() : QList<int>{};
        if (m_workspacePanelCtrl)
            m_workspacePanelCtrl->setVisible(false, false);
        // Transient visibility changes do not emit visibilityChanged(), so
        // update the View actions without changing the saved panel preference.
        syncSidebarModeActions();
        setNotepadModeFlag(true);
        syncCurrentTabUi();
        if (pg && pg->editor && pg->editor->isVisible())
            pg->editor->restoreKeyboardFocus();
        scheduleWriteSettings();
        return;
    }

    setNotepadModeFlag(false);
    if (m_notepadModeWorkspaceWasVisible && m_workspacePanelCtrl)
        m_workspacePanelCtrl->setVisible(true, false);
    m_notepadModeWorkspaceWasVisible = false;
    syncSidebarModeActions();
    syncCurrentTabUi();
    restoreDocumentSplitterSizes();
    if (pg && pg->editor && pg->editor->isVisible())
        pg->editor->restoreKeyboardFocus();
    scheduleWriteSettings();
}

void MainWindow::setNotepadModeFlag(bool enabled)
{
    m_notepadMode = enabled;
    if (m_tabs)
        m_tabs->setNotepadMode(enabled);
}

void MainWindow::focusReadingModePreview()
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || !pg->preview || !pg->preview->isVisible())
        return;
    pg->preview->ensureInitialized();
    pg->preview->focusContent();
}

// Reading Mode is window-level, so the search it owns is torn down across every
// tab rather than only the current one.
void MainWindow::stopReadingModeFind()
{
    if (!m_tabs)
        return;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (TabPage* page = m_tabs->pageAt(i)) {
            // A PDF tab's session belongs to FindOverlay, not to Reading Mode.
            if (page->preview && page->model->editorMode() != EditorMode::Pdf)
                page->preview->stopFind();
        }
    }
}

bool MainWindow::isMindMapVisible() const
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    return pg && pg->model->editorMode() == EditorMode::Markdown
        && pg->model->markdownPresentation() == MarkdownPresentation::MindMap;
}

void MainWindow::setMindMapVisible(bool visible)
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || pg->model->editorMode() != EditorMode::Markdown)
        return;

    // Mind Map needs the Preview visible; Notepad Mode is why it wouldn't be.
    if (visible && m_notepadMode)
        setNotepadMode(false);

    pg->model->setMarkdownPresentation(visible
        ? MarkdownPresentation::MindMap : MarkdownPresentation::Preview);
    pg->model->setMarkdownPreviewVisible(true);
    m_previewDirty = true;
    syncCurrentTabUi();
    updatePreview();
    scheduleWriteSettings();
}

// ---------------------------------------------------------------------------
// Centralized language operation (language-indicator-plan.md). This is the
// single mutation path for editable Markdown/PlainText/language transitions;
// status popup, toolbar/Ctrl+M, file lifecycle, and session restoration all
// route through it so status, toolbar, menus, and preview eligibility stay
// derived from one place instead of being updated independently.
void MainWindow::setDocumentLanguage(TabPage* pg, const QString& stableDefinitionName, LanguageSelectionSource source)
{
    if (!pg)
        return;
    if (isViewerMode(pg->model->editorMode()))
        return;

    const bool wasMarkdown = pg->model->editorMode() == EditorMode::Markdown;
    const bool willBeMarkdown = SyntaxDefinitionResolver::isMarkdownDefinitionName(stableDefinitionName);
    const QString resolvedName = willBeMarkdown
        ? SyntaxDefinitionResolver::fastMdMarkdownDefinitionName()
        : stableDefinitionName;

    // Crossing into Markdown from a non-Markdown language: remember what was
    // active so toggling back out (toolbar Markdown -> Code/Text) restores it.
    if (!wasMarkdown && willBeMarkdown) {
        pg->model->setLastNonMarkdownSelection(
            { pg->model->selectedDefinitionName(), pg->model->languageSelectionSource() });
    }

    pg->model->setSelectedDefinitionName(resolvedName);
    pg->model->setLanguageSelectionSource(source);

    const KSyntaxHighlighting::Definition definition = willBeMarkdown
        ? KSyntaxHighlighting::Definition()
        : SyntaxDefinitionResolver::definitionForStoredName(resolvedName);
    pg->editor->setSyntaxDefinition(definition, willBeMarkdown);

    if (!willBeMarkdown)
        pg->preview->setVisible(false);

    m_previewDirty = true;
    // Everything user-visible (status button, toolbar, menus, preview) is
    // refreshed by onDocumentLanguageChanged() off this signal, so there is
    // exactly one place that projects language state onto the UI.
    emit documentLanguageChanged(pg->model);
}

// ---------------------------------------------------------------------------
void MainWindow::onDocumentLanguageChanged(DocumentModel* model)
{
    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || pg->model != model)
        return; // a background tab changed language; its UI syncs on activation

    syncCurrentTabUi();

    if (m_languagePopup && m_languagePopup->isVisible()) {
        if (m_lblMode && !m_lblMode->isEnabled())
            m_languagePopup->close();
        else
            m_languagePopup->setCurrentLanguage(model->selectedDefinitionName(),
                model->languageSelectionSource() == LanguageSelectionSource::Automatic);
    }

    if (pg->model->editorMode() == EditorMode::Markdown && isPreviewVisible())
        updatePreview();
}

// ---------------------------------------------------------------------------
void MainWindow::requestAutoDetectLanguage(TabPage* pg)
{
    if (!pg)
        return;
    const auto detected = SyntaxDefinitionResolver::autoDetect(pg->model->filePath());
    const QString name = detected.isValid() ? detected.name() : QString();
    pg->model->setAutomaticallyDetectedName(name);
    setDocumentLanguage(pg, name, LanguageSelectionSource::Automatic);
}

// ---------------------------------------------------------------------------
void MainWindow::refreshDocumentLanguageForPath(TabPage* pg)
{
    if (!pg)
        return;
    if (isViewerMode(pg->model->editorMode()))
        return;

    const auto detected = SyntaxDefinitionResolver::autoDetect(pg->model->filePath());
    const QString detectedName = detected.isValid() ? detected.name() : QString();
    pg->model->setAutomaticallyDetectedName(detectedName);

    // Manual selections survive Save As/rename; only the automatic-detection
    // result (used later if the user switches back to Auto Detect) refreshes.
    if (pg->model->languageSelectionSource() == LanguageSelectionSource::Automatic)
        setDocumentLanguage(pg, detectedName, LanguageSelectionSource::Automatic);
}

// ---------------------------------------------------------------------------
void MainWindow::initializeDocumentLanguage(TabPage* pg, LanguageSelectionSource source)
{
    if (!pg)
        return;
    if (isViewerMode(pg->model->editorMode()))
        return;

    pg->model->clearLastNonMarkdownSelection();
    const auto detected = SyntaxDefinitionResolver::autoDetect(pg->model->filePath());
    const QString detectedName = detected.isValid() ? detected.name() : QString();
    pg->model->setAutomaticallyDetectedName(detectedName);
    setDocumentLanguage(pg, detectedName, source);
}

// ---------------------------------------------------------------------------
void MainWindow::switchDocumentMode(TabPage* pg, bool markdown)
{
    if (!pg)
        return;
    if (isViewerMode(pg->model->editorMode()))
        return;

    const bool wasMarkdown = pg->model->editorMode() == EditorMode::Markdown;
    if (wasMarkdown == markdown)
        return;

    if (markdown) {
        setDocumentLanguage(pg, SyntaxDefinitionResolver::fastMdMarkdownDefinitionName(),
                             LanguageSelectionSource::Manual);
        return;
    }

    // Markdown -> Code/Text: restore the last non-Markdown selection;
    // otherwise auto-detect a non-Markdown language; otherwise Plain Text.
    if (pg->model->hasLastNonMarkdownSelection()) {
        const NonMarkdownLanguageSelection last = pg->model->lastNonMarkdownSelection();
        setDocumentLanguage(pg, last.definitionName, last.source);
        return;
    }

    auto detected = SyntaxDefinitionResolver::autoDetect(pg->model->filePath());
    if (SyntaxDefinitionResolver::isMarkdownDefinition(detected))
        detected = {};
    const QString name = detected.isValid() ? detected.name() : QString();
    setDocumentLanguage(pg, name, LanguageSelectionSource::Automatic);
}

// ---------------------------------------------------------------------------
void MainWindow::openLanguageSelector()
{
    if (consumeStatusBarPopupDismissal(m_lblMode))
        return;

    TabPage* pg = m_tabs->currentPage();
    if (!pg || !m_lblMode)
        return;
    if (isViewerMode(pg->model->editorMode()))
        return;

    if (!m_languagePopup) {
        m_languagePopup = new LanguageSelectorPopup(this);
        connect(m_languagePopup, &LanguageSelectorPopup::autoDetectRequested,
                this, &MainWindow::onLanguagePopupAutoDetect);
        connect(m_languagePopup, &LanguageSelectorPopup::definitionRequested,
                this, &MainWindow::onLanguagePopupDefinitionChosen);
        m_languagePopup->installEventFilter(this);
        connect(m_languagePopup, &QWidget::destroyed, this, [this] {
            m_statusBarPopupDismissalAnchor = nullptr;
        });
    }
    m_languagePopup->popup(m_lblMode, pg->model->selectedDefinitionName(),
                           pg->model->languageSelectionSource() == LanguageSelectionSource::Automatic);
}

// ---------------------------------------------------------------------------
void MainWindow::onLanguagePopupAutoDetect()
{
    requestAutoDetectLanguage(m_tabs->currentPage());
}

// ---------------------------------------------------------------------------
void MainWindow::onLanguagePopupDefinitionChosen(const QString& stableName)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg)
        return;
    setDocumentLanguage(pg, stableName, LanguageSelectionSource::Manual);
}

// ---------------------------------------------------------------------------
// The status bar sits at the bottom of the window, so its menus open upward
// out of the button. Only the anchor's own top-left is mapped (mapToGlobal is
// unreliable for points on or past a widget's edge under per-screen scaling).
void MainWindow::popupStatusBarMenu(AlignedMenu* menu, QWidget* anchor)
{
    if (!menu || !anchor)
        return;
    if (!menu->property("statusBarDismissalGuardInstalled").toBool()) {
        menu->setProperty("statusBarDismissalGuardInstalled", true);
        menu->installEventFilter(this);
        connect(menu, &QMenu::aboutToHide, this, [this, anchor] {
            rememberStatusBarPopupDismissal(anchor);
        });
    }
    const QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
    menu->popup(QPoint(topLeft.x(), topLeft.y() - menu->sizeHint().height()));
}

// ---------------------------------------------------------------------------
void MainWindow::rememberStatusBarPopupDismissal(QWidget* anchor)
{
    if (!anchor || !(QApplication::mouseButtons() & Qt::LeftButton))
        return;

    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    if (anchorRect.contains(QCursor::pos()))
        m_statusBarPopupDismissalAnchor = anchor;
}

// ---------------------------------------------------------------------------
bool MainWindow::consumeStatusBarPopupDismissal(QWidget* anchor)
{
    const bool dismissedByThisAnchor = m_statusBarPopupDismissalAnchor == anchor;
    m_statusBarPopupDismissalAnchor = nullptr;
    return dismissedByThisAnchor;
}

// ---------------------------------------------------------------------------
void MainWindow::openLineEndingMenu()
{
    if (consumeStatusBarPopupDismissal(m_lblLineEnding))
        return;

    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->isViewer() || !m_lblLineEnding)
        return;

    if (!m_lineEndingMenu) {
        // Reuses the Edit > EOL Conversion actions, so both entry points share
        // one checked state and one conversion path.
        m_lineEndingMenu = new AlignedMenu(this);
        m_lineEndingMenu->setShowAnimationStyle(AlignedMenu::ContextMenu);
        for (QAction* action : { m_actEolCrlf, m_actEolLf, m_actEolCr }) {
            if (action)
                m_lineEndingMenu->addAction(action);
        }
    }
    popupStatusBarMenu(m_lineEndingMenu, m_lblLineEnding);
}

// ---------------------------------------------------------------------------
void MainWindow::openEncodingMenu()
{
    if (consumeStatusBarPopupDismissal(m_lblEncoding))
        return;

    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->isViewer() || !m_lblEncoding)
        return;

    if (!m_encodingMenu) {
        m_encodingMenu = new AlignedMenu(this);
        m_encodingMenu->setShowAnimationStyle(AlignedMenu::ContextMenu);
        auto* group = new QActionGroup(m_encodingMenu);
        for (TextEncoding encoding : { TextEncoding::Utf8, TextEncoding::Utf8Bom,
                                       TextEncoding::Utf16Le, TextEncoding::Utf16Be }) {
            QAction* action = m_encodingMenu->addAction(textEncodingLabel(encoding));
            action->setCheckable(true);
            action->setData(static_cast<int>(encoding));
            group->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, encoding] { applyDocumentEncoding(encoding); });
            m_encodingActions.append(action);
        }
    }
    const TextEncoding current = pg->model->encoding();
    for (QAction* action : std::as_const(m_encodingActions))
        action->setChecked(static_cast<TextEncoding>(action->data().toInt()) == current);
    popupStatusBarMenu(m_encodingMenu, m_lblEncoding);
}

// ---------------------------------------------------------------------------
void MainWindow::applyDocumentLineEnding(const QString& lineEnding)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->isViewer() || pg->model->lineEnding() == lineEnding)
        return;
    pg->model->setLineEnding(lineEnding);
    markDocumentFormatChanged(pg);
}

// ---------------------------------------------------------------------------
void MainWindow::applyDocumentEncoding(TextEncoding encoding)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg || pg->model->isViewer() || pg->model->encoding() == encoding)
        return;
    pg->model->setEncoding(encoding);
    markDocumentFormatChanged(pg);
}

// ---------------------------------------------------------------------------
// A line-ending or encoding change rewrites the file's bytes, so it is a real
// unsaved modification -- but it is applied on the next save, never written
// behind the user's back, so no Auto Save is scheduled from here.
void MainWindow::markDocumentFormatChanged(TabPage* page)
{
    if (!page || !page->model || !page->editor)
        return;

    page->model->setDirty(true);
    ++m_editRevisions[page->model];
    page->editor->document()->setModified(true);
    m_tabs->refreshTitle(m_tabs->indexOf(page));
    updateWindowTitle();
    syncCurrentTabUi();
}

// ---------------------------------------------------------------------------
void MainWindow::setEditorMode(bool markdown)
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg)
        return;
    if (isViewerMode(pg->model->editorMode())) {
        syncCurrentTabUi();
        return;
    }

    switchDocumentMode(pg, markdown);
}

// ---------------------------------------------------------------------------
void MainWindow::syncCurrentTabUi()
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg) {
        if (m_actToolbarReveal)
            m_actToolbarReveal->setEnabled(false);
        if (m_actToolbarCopyPath)
            m_actToolbarCopyPath->setEnabled(false);
        if (m_actToolbarTerminal)
            m_actToolbarTerminal->setEnabled(false);
        return;
    }

    const bool markdown = pg->model->editorMode() == EditorMode::Markdown;
    const bool isImage = pg->model->editorMode() == EditorMode::Image;
    const bool isDiff = pg->model->editorMode() == EditorMode::Diff;
    const bool readOnly = isViewerMode(pg->model->editorMode());
    // Bundled content (the User Guide) is a Markdown tab in every respect except
    // that nothing may edit or save it.
    const bool bundled = pg->model->isBundled();
    const bool readingMode = (m_readingMode || pg->model->readingModeOverride())
        && markdown && !readOnly;
    // Notepad Mode is Reading Mode's mirror: it suppresses the Preview instead
    // of the Editor, and (being window-level with no per-tab override) simply
    // does not apply outside an editable Markdown tab.
    const bool notepadMode = m_notepadMode && markdown && !readOnly;
    bool previewVisible = !isImage && !isDiff
        && (readOnly || (markdown && (pg->model->markdownPreviewVisible() || readingMode) && !notepadMode));
    // Reading Mode is the only thing that hides an editable Editor; everything
    // else keeps it. The guard below is what makes "both panes hidden"
    // unrepresentable no matter which side asked to disappear.
    bool editorVisible = !readOnly && !readingMode;
    if (!readOnly && !editorVisible && !previewVisible)
        editorVisible = true;
    const auto readOnlyModeLabel = [](EditorMode mode) {
        switch (mode) {
        case EditorMode::Pdf:  return tr("PDF");
        case EditorMode::Svg:  return tr("SVG");
        case EditorMode::Diff: return tr("Diff");
        default:               return tr("HTML");
        }
    };

    if (m_lblMode) {
        if (isImage) {
            m_lblMode->setText(tr("Image"));
            m_lblMode->setEnabled(false);
            m_lblMode->setToolTip(QString());
        } else if (readOnly) {
            m_lblMode->setText(readOnlyModeLabel(pg->model->editorMode()));
            m_lblMode->setEnabled(false);
            m_lblMode->setToolTip(QString());
        } else {
            const QString languageDisplay =
                SyntaxDefinitionResolver::displayNameForStoredName(pg->model->selectedDefinitionName());
            m_lblMode->setText(languageDisplay);
            m_lblMode->setEnabled(!bundled);
            const bool automatic = pg->model->languageSelectionSource() == LanguageSelectionSource::Automatic;
            m_lblMode->setToolTip(automatic
                ? tr("%1 (Auto Detect) -- click to change").arg(languageDisplay)
                : tr("%1 (Manual) -- click to change").arg(languageDisplay));
            m_lblMode->setAccessibleDescription(automatic ? tr("Automatically detected") : tr("Manually selected"));
        }
    }

    // Line ending and encoding are per-document, so both indicators follow the
    // active tab. Viewer modes have no editable byte format to choose.
    const QString lineEnding = pg->model->lineEnding();
    if (m_lblLineEnding) {
        m_lblLineEnding->setText(lineEndingLabel(lineEnding));
        m_lblLineEnding->setEnabled(!readOnly && !bundled);
        m_lblLineEnding->setToolTip(readOnly || bundled
            ? QString()
            : tr("Line ending: %1 -- click to change").arg(lineEndingLabel(lineEnding)));
    }
    for (QAction* action : { m_actEolCrlf, m_actEolLf, m_actEolCr }) {
        if (!action)
            continue;
        QSignalBlocker blocker(action);
        action->setChecked(action == (lineEnding == classicMacLineEnding() ? m_actEolCr
                                      : lineEnding == unixLineEnding() ? m_actEolLf
                                      : m_actEolCrlf));
        // The Edit menu also disables the containing "EOL Conversion" submenu
        // on aboutToShow, but the Command Palette calls isEnabled() on these
        // actions directly without ever opening that menu, so they need their
        // own enabled state kept in sync here too.
        action->setEnabled(!readOnly && !bundled);
    }
    // Re-evaluates which tabs are eligible (a language switch can move a tab
    // in or out of the prose modes) and refreshes Fix All's enabled state.
    if (m_tabs)
        m_tabs->setSpellCheckActive(spellCheckEffectivelyOn());
    syncSpellCheckUi();

    if (m_lblEncoding) {
        const QString encodingText = textEncodingLabel(pg->model->encoding());
        m_lblEncoding->setText(encodingText);
        m_lblEncoding->setEnabled(!readOnly && !bundled);
        m_lblEncoding->setToolTip(readOnly || bundled
            ? QString()
            : tr("Encoding: %1 -- click to change").arg(encodingText));
    }

    if (m_actModeToggle) {
        QSignalBlocker blocker(m_actModeToggle);
        m_actModeToggle->setChecked(markdown);
        m_actModeToggle->setText(isImage ? tr("Image")
            : readOnly ? readOnlyModeLabel(pg->model->editorMode())
            : (markdown ? tr("Markdown") : tr("Code/Text")));
        m_actModeToggle->setEnabled(!readOnly && !bundled);
        if (!readOnly) {
            const QString shortcutText = m_actModeToggle->shortcut().toString(QKeySequence::NativeText);
            m_actModeToggle->setToolTip(tr("Switch Editor Mode (%1)").arg(shortcutText));
        }
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
    // Checked state follows the *layout*, not the raw flag, so a tab that
    // cannot read (Code/Text, PDF/HTML/Image/SVG viewers) shows the command
    // unchecked and disabled while the window-level flag waits for a Markdown
    // tab to come back.
    if (m_actReadingMode) {
        QSignalBlocker blocker(m_actReadingMode);
        m_actReadingMode->setChecked(readingMode);
        m_actReadingMode->setEnabled(markdown);
    }
    if (m_actNotepadMode) {
        // Unlike Reading Mode, this checked state is the raw window-level flag,
        // not the per-tab-gated "notepadMode": Notepad Mode also hides the
        // Workspace Panel, which applies regardless of the active tab's type.
        QSignalBlocker blocker(m_actNotepadMode);
        m_actNotepadMode->setChecked(m_notepadMode);
    }
    if (m_titleBar)
        m_titleBar->syncLayoutToggles();
    if (m_actMindMap) {
        QSignalBlocker blocker(m_actMindMap);
        m_actMindMap->setChecked(markdown && pg->model->markdownPresentation() == MarkdownPresentation::MindMap);
        m_actMindMap->setEnabled(markdown);
    }
    if (m_actWordWrap) {
        QSignalBlocker blocker(m_actWordWrap);
        m_actWordWrap->setChecked(isDiff
            ? (pg->diffView && pg->diffView->wordWrapEnabled())
            : (pg->editor && pg->editor->wordWrapEnabled()));
    }
    if (m_actToolbarReveal)
        m_actToolbarReveal->setEnabled(!pg->model->filePath().isEmpty());
    if (m_actToolbarCopyPath)
        m_actToolbarCopyPath->setEnabled(!pg->model->filePath().isEmpty());
    if (m_actToolbarTerminal)
        m_actToolbarTerminal->setEnabled(!pg->model->filePath().isEmpty());

    // Markdown-syntax formatting actions are meaningless in Plain Text mode:
    // gray them out (and their shortcuts), re-enable them in Markdown mode.
    for (QAction* a : m_formatActions) {
        if (a) a->setEnabled(markdown && !readOnly && !bundled);
    }
    if (m_actFormatDocument)
        m_actFormatDocument->setEnabled(markdown && !readOnly && !bundled);
    if (m_tableButton)
        m_tableButton->setEnabled(markdown && !readOnly && !bundled);
    if (m_actDocumentSummary)
        m_actDocumentSummary->setEnabled(!readOnly);


    // Re-assert the editor's highlighting from the document's *selected*
    // language, never from its file name: setMarkdownMode() re-resolves off
    // the path, which would silently replace a manual status-bar choice
    // (e.g. Python on an untitled file) the moment anything re-synced the UI.
    if (pg->editor && !readOnly) {
        pg->editor->setSyntaxDefinition(
            markdown ? KSyntaxHighlighting::Definition()
                     : SyntaxDefinitionResolver::definitionForStoredName(pg->model->selectedDefinitionName()),
            markdown);
    }

    if (pg->editor)
        pg->editor->setVisible(editorVisible);
    if (pg->preview) {
        if (markdown) {
            pg->preview->setPresentation(pg->model->markdownPresentation() == MarkdownPresentation::MindMap
                ? PreviewWidget::Presentation::MindMap : PreviewWidget::Presentation::Preview);
        }
        pg->preview->setVisible(previewVisible);
    }
    if (pg->diffView)
        pg->diffView->setVisible(isDiff);

    // A brand-new, untouched blank tab has nothing to render - skip the
    // pointless empty Markdown->HTML conversion (and the WebView2 demand it
    // implies) until the document actually has content or is modified.
    const bool blankPristine = markdown && m_activeEditor
        && m_activeEditor->document()->isEmpty() && pg->model && !pg->model->isDirty();

    if (markdown && previewVisible && m_previewDirty && !blankPristine)
        updatePreview();
}

// ---------------------------------------------------------------------------
void MainWindow::scheduleCursorPositionUpdate()
{
    if (m_cursorUpdatePending)
        return;
    m_cursorUpdatePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_cursorUpdatePending = false;
        onCursorPositionChanged();
    });
}

// ---------------------------------------------------------------------------
void MainWindow::onCursorPositionChanged()
{
    if (!m_activeEditor) return;
    QTextCursor c = m_activeEditor->textCursor();
    int line = c.blockNumber() + 1;
    if (!m_restoringNavigation && m_lastNavigationLine > 0
        && qAbs(line - m_lastNavigationLine) >= 20
        && !m_lastObservedNavigationLocation.path.isEmpty()) {
        m_navigationHistory.push(m_lastObservedNavigationLocation);
        updateNavigationActions();
    }
    m_lastNavigationLine = line;
    m_lastObservedNavigationLocation = currentNavigationLocation();
    int col  = c.positionInBlock() + 1;
    m_lblPosition->setText(tr("Line: %1, Col: %2").arg(line).arg(col));

    if (c.hasSelection()) {
        // Counting the words in the selection is the only expensive part of this
        // handler, and a plain caret move re-reports an unchanged selection.
        const int selStart = c.selectionStart();
        const int selEnd = c.selectionEnd();
        if (selStart != m_lastSelectionStart || selEnd != m_lastSelectionEnd) {
            m_lastSelectionStart = selStart;
            m_lastSelectionEnd = selEnd;
            const QString selText = c.selectedText();
            const int chars = selText.length();
            const int words = DocumentSummaryAnalyzer::countWords(QStringView(selText));
            m_lblSelection->setText(tr("    Selection: %1 chars, %2 words").arg(chars).arg(words));
        }
        m_lblSelection->show();
    } else {
        m_lastSelectionStart = -1;
        m_lastSelectionEnd = -1;
        m_lblSelection->hide();
    }
    if (m_actFormatDocument)
        m_actFormatDocument->setEnabled(m_activeEditor->isMarkdownMode());
}

// ---------------------------------------------------------------------------
void MainWindow::onTextChanged()
{
    // Invalidate any worker snapshot before changing the revision bookkeeping.
    // The finished callback will discard it and start one conversion from the
    // latest editor state instead of briefly accepting stale HTML.
    ++m_htmlGeneration;
    scheduleWordCount();
    if (!m_activeEditor || !m_tabs)
        return;
    m_navigationHistory.onDocumentEdited();
    if (DocumentModel* model = m_tabs->currentModel()) {
        model->setDirty(true);
        ++m_editRevisions[model];
        scheduleAutoSave(model);
        if (m_workspacePanelCtrl) {
            if (WorkspaceSearchPanel* search = m_workspacePanelCtrl->searchPanel())
                search->notifyDocumentEdited(model->filePath());
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::updateWordCount(const QString& text)
{
    if (!m_activeEditor) return;
    if (m_wordCountTimer)
        m_wordCountTimer->stop();
    int words = 0;
    if (text.isNull()) {
        const QString src = m_activeEditor->toPlainText();
        words = DocumentSummaryAnalyzer::countWords(QStringView(src));
    } else {
        words = DocumentSummaryAnalyzer::countWords(QStringView(text));
    }
    m_lastWordCount = words;
    setWordCountLabel(words);
}

void MainWindow::setWordCountLabel(int words)
{
    if (!m_activeEditor) return;
    // characterCount() is maintained by the document; unlike the word count it
    // costs no copy and no scan.
    const int characters = qMax(0, m_activeEditor->document()->characterCount() - 1);
    m_lblWords->setText(tr("Characters: %1     Words: %2").arg(characters).arg(words));
}

// Counting words means copying the whole document and scanning every character,
// which is far too expensive to do per keystroke (on a 28 k-line document that
// is an ~846 KB allocation plus a ~433 k-character scan). The character count is
// free, so it stays instantaneous while the word count trails by 300 ms.
void MainWindow::scheduleWordCount()
{
    if (!m_activeEditor) return;
    setWordCountLabel(m_lastWordCount);
    if (!m_wordCountTimer) {
        m_wordCountTimer = new QTimer(this);
        m_wordCountTimer->setSingleShot(true);
        m_wordCountTimer->setInterval(300);
        connect(m_wordCountTimer, &QTimer::timeout, this, [this]() { updateWordCount(); });
    }
    m_wordCountTimer->start();
}

// ---------------------------------------------------------------------------
void MainWindow::showDocumentSummary()
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg || !m_activeEditor)
        return;
    const EditorMode mode = pg->model->editorMode();
    if (mode != EditorMode::Markdown && mode != EditorMode::PlainText)
        return;

    const QString text = m_activeEditor->toPlainText();
    const QString selectedText = m_activeEditor->textCursor().selectedText();
    const DocumentSummary summary = DocumentSummaryAnalyzer::analyze(text, selectedText, mode);

    if (!m_documentSummaryDialog)
        m_documentSummaryDialog = new DocumentSummaryDialog(this);
    m_documentSummaryDialog->setSummary(summary);
    m_documentSummaryDialog->exec();
}

// ---------------------------------------------------------------------------
bool MainWindow::isPreviewVisible() const
{
    TabPage* pg = m_tabs->currentPage();
    // Reading Mode shows the Preview without changing the document's own
    // preference, and Notepad Mode suppresses it the same way, so the
    // effective answer -- what the Toggle Preview action reports and toggles
    // against -- has to include both.
    return pg && pg->model->editorMode() == EditorMode::Markdown
        && ((pg->model->markdownPreviewVisible() && !m_notepadMode) || isReadingModeActive());
}

// ---------------------------------------------------------------------------
void MainWindow::updateWindowTitle()
{
    TabPage* pg = m_tabs->currentPage();
    if (!pg) {
        setWindowTitle(QStringLiteral("FastMD"));
        if (m_titleBar)
            m_titleBar->setTitleText({});
        return;
    }
    QString name = pg->model->displayName();
    if (pg->model->isDirty()) name.prepend(QStringLiteral("* "));

    QString title = name;
    const QString workspaceRoot = m_workspacePanelCtrl ? m_workspacePanelCtrl->rootPath() : QString();
    if (!workspaceRoot.isEmpty()) {
        QDir workspaceDir(workspaceRoot);
        const QString workspaceName = workspaceDir.dirName();
        if (!workspaceName.isEmpty())
            title += QStringLiteral(" — ") + workspaceName;
    }
    // The custom title bar shows the same context the native caption did, but
    // without the application suffix -- the app icon two columns to its left
    // already says which program this is. Taken from this one composition so
    // the caption and the real window title can never disagree, and so no
    // second copy of the document or workspace state exists.
    if (m_titleBar)
        m_titleBar->setTitleText(title);
    title += QStringLiteral(" — FastMD");
    setWindowTitle(title);
}

// ---------------------------------------------------------------------------
bool MainWindow::confirmClose(int tabIndex)
{
    TabPage* pg = m_tabs->pageAt(tabIndex);
    if (!pg || !pg->model->isDirty()) return true;

    // Freeze the commit boundary before entering the dialog's nested event
    // loop. An autosave already inside commit is allowed to finish before the
    // choice is presented; every older or newly queued autosave then waits
    // behind this barrier until the user's decision has been linearized.
    if (QTimer* timer = m_autoSaveTimers.value(pg->model))
        timer->stop();
    const auto saveState = pg->model->saveState();
    m_autoSaveSuspended.insert(pg->model);
    DocumentSaveCoordinator::invalidate(saveState);
    QMutexLocker commitBarrier(&saveState->commitMutex);

    ThemedMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(
        tr("'%1' has unsaved changes.\nSave before closing?")
            .arg(pg->model->displayName()));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    if (auto* layout = qobject_cast<QGridLayout*>(box.layout())) {
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setHorizontalSpacing(14);
        layout->setVerticalSpacing(6);
    }

    // Let S/D/C trigger the matching button directly (no Alt needed) so the
    // dialog can be dismissed with one keystroke.
    bindDialogKey(&box, box.button(QMessageBox::Save), Qt::Key_S);
    bindDialogKey(&box, box.button(QMessageBox::Discard), Qt::Key_D);
    bindDialogKey(&box, box.button(QMessageBox::Cancel), Qt::Key_C);

    const auto btn = static_cast<QMessageBox::StandardButton>(box.exec());

    if (btn == QMessageBox::Save) {
        m_autoSaveSuspended.remove(pg->model);
        commitBarrier.unlock();
        const bool saved = saveDocument(tabIndex, false);
        if (!saved)
            scheduleAutoSave(pg->model);
        return saved;
    }
    if (btn == QMessageBox::Cancel) {
        m_autoSaveSuspended.remove(pg->model);
        commitBarrier.unlock();
        scheduleAutoSave(pg->model);
        return false;
    }
    // A completion handled by the nested dialog loop may have queued another
    // autosave. Supersede that ticket while the commit barrier is still held.
    DocumentSaveCoordinator::invalidate(saveState);
    return true;
}

// ---------------------------------------------------------------------------
bool MainWindow::confirmSaveBeforeUpdate()
{
    QList<MainWindow*> windows;
    bool hasDirtyDocument = false;
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* mainWindow = qobject_cast<MainWindow*>(topLevel);
        if (!mainWindow)
            continue;

        windows.append(mainWindow);
        for (int i = 0; i < mainWindow->m_tabs->count(); ++i) {
            if (const TabPage* page = mainWindow->m_tabs->pageAt(i);
                page && page->model && page->model->isDirty()) {
                hasDirtyDocument = true;
                break;
            }
        }
    }

    if (!hasDirtyDocument)
        return true;

    ThemedMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("There are unsaved changes in FastMD."));
    box.setInformativeText(tr("Save your changes before FastMD restarts to install the update?"));
    auto* saveButton = box.addButton(tr("Save All"), QMessageBox::AcceptRole);
    box.addButton(tr("Cancel Update"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.polishContentLayout();
    box.exec();
    if (box.clickedButton() != saveButton)
        return false;

    for (MainWindow* mainWindow : windows) {
        for (int i = 0; i < mainWindow->m_tabs->count(); ++i) {
            const TabPage* page = mainWindow->m_tabs->pageAt(i);
            if (page && page->model && page->model->isDirty()
                && !mainWindow->saveDocument(i, false)) {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
void MainWindow::onTabCloseRequested(int index)
{
    if (!confirmClose(index)) return;

    if (TabPage* pg = m_tabs->pageAt(index)) {
        DocumentSaveCoordinator::invalidate(pg->model->saveState());
        m_ignoredFileChanges.clear(pg->model->filePath());
        recordClosedTab(index, pg);
        if (QTimer* timer = m_autoSaveTimers.take(pg->model))
            timer->deleteLater();
        m_editRevisions.remove(pg->model);
        m_autoSaveSuspended.remove(pg->model);
        m_renderedRevisions.remove(pg->model);
        m_savingDocuments.remove(pg->model);
        m_conflictDiskBytes.remove(pg->model);
        pg->model->setExternalConflict(false);
        cleanupRecoveryFor(pg->model);
    }

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
    m_workspacePanelCtrl->updateRoot();
}

// ---------------------------------------------------------------------------
void MainWindow::renameTab(int index)
{
    TabPage* pg = m_tabs->pageAt(index);
    if (!pg) return;

    if (pg->model->isUntitled()) {
        // For untitled documents, delegate to Save As.
        m_tabs->setCurrentIndex(index);
        saveDocument(index, true);
        return;
    }

    const QString oldPath = pg->model->filePath();
    const QFileInfo fi(oldPath);
    const QString oldName = fi.fileName();
    const QString dir     = fi.absolutePath();

    // Inline rename dialog: ask for new filename only
    bool ok = false;
    QString newName = QInputDialog::getText(
        this, tr("Rename File"),
        tr("New file name:"),
        QLineEdit::Normal, oldName, &ok);

    if (!ok || newName.trimmed().isEmpty())
        return;
    newName = newName.trimmed();
    if (newName == oldName)
        return;

    // Validate: no path separators
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))
        || newName.contains(QLatin1Char('\0'))) {
        themedWarning(this, tr("Rename Failed"),
            tr("The file name cannot contain path separators or null characters."));
        return;
    }

    const QString newPath = dir + QLatin1Char('/') + newName;

    if (QFileInfo::exists(newPath) && newPath.compare(oldPath, Qt::CaseInsensitive) != 0) {
        themedWarning(this, tr("Rename Failed"),
            tr("A file named \"%1\" already exists.").arg(newName));
        return;
    }

    // Stop watching the old path
    if (m_watcher && m_watcher->files().contains(oldPath))
        m_watcher->removePath(oldPath);

    if (!QFile::rename(oldPath, newPath)) {
        // Restore watch on old path after failure
        watchFile(oldPath);
        themedWarning(this, tr("Rename Failed"),
            tr("Could not rename file:\n%1\n\nThe file may be locked or you may not have permission.")
                .arg(oldPath));
        return;
    }

    QSettings s;
    QVariant wrap = s.value(QStringLiteral("WordWrap/") + oldPath);
    if (wrap.isValid()) {
        s.setValue(QStringLiteral("WordWrap/") + newPath, wrap.toBool());
        s.remove(QStringLiteral("WordWrap/") + oldPath);
    } else if (pg->editor) {
        s.setValue(QStringLiteral("WordWrap/") + newPath, pg->editor->wordWrapEnabled());
    }

    // Update closed-tabs list so reopen works with the new path
    for (ClosedTabRecord& closed : m_closedTabs) {
        if (closed.state.path == oldPath)
            closed.state.path = newPath;
    }

    // Update recent files
    m_recentFilesMgr->rename(oldPath, newPath);

    // Update the model: path, editor path, title
    pg->model->setFilePath(newPath);
    pg->editor->setDocumentPath(newPath);
    refreshDocumentLanguageForPath(pg);
    m_tabs->refreshTitle(index);

    // Update workspace watcher
    m_ignoredFileChanges.clear(oldPath);
    forgetFileContents(oldPath);
    forgetFileIdentity(oldPath);
    rememberFileContents(newPath, encodeWithLineEnding(pg->editor->toPlainText(),
                                                       pg->model->lineEnding(),
                                                       pg->model->encoding()));
    watchFile(newPath);

    // Persist the new path in recent files
    m_recentFilesMgr->add(newPath);

    updateWindowTitle();
    m_workspacePanelCtrl->updateRoot();
}

void MainWindow::onWorkspaceFileRenamed(const QString& oldPath, const QString& newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty())
        return;

    const QString oldPrefix = oldPath + QLatin1Char('/');
    const int tabCount = m_tabs ? m_tabs->count() : 0;
    for (int i = 0; i < tabCount; ++i) {
        TabPage* pg = m_tabs->pageAt(i);
        if (!pg || !pg->model)
            continue;

        const QString tabPath = pg->model->filePath();
        if (tabPath.isEmpty())
            continue;

        QString targetPath;
        if (tabPath.compare(oldPath, Qt::CaseInsensitive) == 0) {
            targetPath = newPath;
        } else if (tabPath.startsWith(oldPrefix, Qt::CaseInsensitive)) {
            targetPath = newPath + tabPath.mid(oldPath.length());
        }

        if (!targetPath.isEmpty()) {
            if (m_watcher && m_watcher->files().contains(tabPath))
                m_watcher->removePath(tabPath);

            pg->model->setFilePath(targetPath);
            if (pg->editor)
                pg->editor->setDocumentPath(targetPath);
            forgetFileContents(tabPath);
            forgetFileIdentity(tabPath);
            if (pg->editor) {
                rememberFileContents(targetPath, encodeWithLineEnding(pg->editor->toPlainText(),
                                                                        pg->model->lineEnding(),
                                                                        pg->model->encoding()));
            }
            refreshDocumentLanguageForPath(pg);

            m_tabs->refreshTitle(i);
            watchFile(targetPath);
            m_recentFilesMgr->rename(tabPath, targetPath);
        }
    }

    updateWindowTitle();
}

void MainWindow::onWorkspacePathsDeleted(const QStringList& paths)
{
    QStringList deletedKeys;
    for (const QString& path : paths) {
        const QString key = documentPathKey(path);
        if (!key.isEmpty())
            deletedKeys.append(key);
    }
    if (deletedKeys.isEmpty())
        return;

    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;

        for (int i = window->m_tabs->count() - 1; i >= 0; --i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (!page || !page->model)
                continue;
            const QString tabKey = page->model->pathKey();
            const bool deleted = std::any_of(deletedKeys.cbegin(), deletedKeys.cend(),
                [&tabKey](const QString& deletedKey) {
                    return tabKey == deletedKey
                        || tabKey.startsWith(deletedKey + QLatin1Char('/'));
                });
            if (deleted)
                window->discardTabForDuplicate(i);
        }

        if (window->m_tabs->count() == 0) {
            if (window->m_closeAppOnLastTabClose)
                window->close();
            else
                window->newFile();
        }
    }
}

// ---------------------------------------------------------------------------
void MainWindow::showTabContextMenu(int index, QPoint globalPos)
{
    TabPage* pg = m_tabs->pageAt(index);
    if (!pg) return;

    AlignedMenu menu(this);
    menu.setObjectName(QStringLiteral("tabContextMenu"));
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    // ── Close ───────────────────────────────────────────────────
    QAction* actClose = menu.addAction(tr("Close"));
    connect(actClose, &QAction::triggered, this, [this, index]() {
        onTabCloseRequested(index);
    });

    // ── Close Other Tabs ─────────────────────────────────────────
    QAction* actCloseOthers = menu.addAction(tr("Close Other Tabs"));
    actCloseOthers->setEnabled(m_tabs->count() > 1);
    connect(actCloseOthers, &QAction::triggered, this, [this, index]() {
        // Capture the target by pointer so the identity check stays correct even
        // as left-side closures shift the target's numeric index downward.
        TabPage* keepPage = m_tabs->pageAt(index);
        if (!keepPage) return;
        for (int i = m_tabs->count() - 1; i >= 0; --i) {
            if (m_tabs->pageAt(i) != keepPage)
                onTabCloseRequested(i);
        }
    });


    // ── Close Tabs to the Right ───────────────────────────────────
    QAction* actCloseRight = menu.addAction(tr("Close Tabs to the Right"));
    const bool hasRight = (index < m_tabs->count() - 1);
    actCloseRight->setEnabled(hasRight);
    connect(actCloseRight, &QAction::triggered, this, [this, index]() {
        for (int i = m_tabs->count() - 1; i > index; --i)
            onTabCloseRequested(i);
    });
    
    // ── Close Tabs to the Left ────────────────────────────────────
    QAction* actCloseLeft = menu.addAction(tr("Close Tabs to the Left"));
    const bool hasLeft = (index > 0);
    actCloseLeft->setEnabled(hasLeft);
    connect(actCloseLeft, &QAction::triggered, this, [this, index]() {
        for (int i = index - 1; i >= 0; --i)
            onTabCloseRequested(i);
    });
    
    menu.addSeparator();
    // ── Rename ──────────────────────────────────────────────────
    QAction* actRename = menu.addAction(tr("Rename..."));
    connect(actRename, &QAction::triggered, this, [this, index]() {
        renameTab(index);
    });

    menu.addSeparator();

    // ── Copy Path ────────────────────────────────────────────────
    const bool hasSavedPath = !pg->model->isUntitled();
    QAction* actCopyPath = menu.addAction(tr("Copy Path"));
    actCopyPath->setEnabled(hasSavedPath);
    connect(actCopyPath, &QAction::triggered, this, [pg]() {
        QApplication::clipboard()->setText(
            QDir::toNativeSeparators(pg->model->filePath()));
    });

    // ── Reveal in Explorer ───────────────────────────────────────
    QAction* actReveal = menu.addAction(tr("Reveal in Explorer"));
    actReveal->setEnabled(hasSavedPath);
    connect(actReveal, &QAction::triggered, this, [pg]() {
        WorkspaceFileOps::revealFileInExplorer(pg->model->filePath());
    });

    if (isViewerMode(pg->model->editorMode())) {
        QAction* actExternal = menu.addAction(
            pg->model->editorMode() == EditorMode::Html ? tr("Open with External Browser") : tr("Open with Default Application"));
        connect(actExternal, &QAction::triggered, this, [pg]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(pg->model->filePath()));
        });
    }

    // ── Open Containing Folder in Terminal ───────────────────────
    QAction* actTerminal = menu.addAction(tr("Open Folder in Terminal"));
    actTerminal->setEnabled(hasSavedPath);
    connect(actTerminal, &QAction::triggered, this, [this, pg]() {
        openTerminalAtFile(pg->model->filePath());
    });

    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Right-clicking the vacant part of the tab bar (including a bar with no tabs
// at all). Deliberately holds nothing tab-specific -- it reuses the File menu's
// own actions, so the entries carry their real shortcuts and enabled state.
void MainWindow::showTabBarContextMenu(QPoint globalPos)
{
    AlignedMenu menu(this);
    menu.setObjectName(QStringLiteral("tabContextMenu"));
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    menu.addAction(m_actNewTab);
    menu.addAction(m_actReopenClosedTab);
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
void MainWindow::showPreviewContextMenu(const QPoint& globalPos, bool hasSelection, int sourceLine,
                                        const QString& mermaidSvg,
                                        const QString& mermaidPngDataUrl)
{
    if (!m_activePreview) return;
    PreviewWidget* preview = m_activePreview;

    TabPage* page = m_tabs->currentPage();
    const bool isHtmlMode = page && page->model->editorMode() == EditorMode::Html;
    const bool isSvgMode = page && page->model->editorMode() == EditorMode::Svg;
    if (isHtmlMode || isSvgMode) {
        AlignedMenu menu(this);
        menu.setObjectName(QStringLiteral("previewContextMenu"));
        menu.setCompactShortcutLayout(true);
        menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
        menu.setShowAnimationDuration(80);

        QAction* copy = menu.addAction(tr("Copy"));
        copy->setEnabled(hasSelection);
        connect(copy, &QAction::triggered, preview, &PreviewWidget::copy);
        QAction* selectAll = menu.addAction(tr("Select All"));
        selectAll->setShortcut(QKeySequence::SelectAll);
        connect(selectAll, &QAction::triggered, preview, &PreviewWidget::selectAll);

        menu.addSeparator();
        QAction* zoomIn = menu.addAction(tr("Zoom In"));
        zoomIn->setShortcut(QKeySequence::ZoomIn);
        connect(zoomIn, &QAction::triggered, preview, &PreviewWidget::htmlZoomIn);
        QAction* zoomOut = menu.addAction(tr("Zoom Out"));
        zoomOut->setShortcut(QKeySequence::ZoomOut);
        connect(zoomOut, &QAction::triggered, preview, &PreviewWidget::htmlZoomOut);
        QAction* resetZoom = menu.addAction(tr("Reset Zoom"));
        connect(resetZoom, &QAction::triggered, preview, &PreviewWidget::resetHtmlZoom);

        menu.addSeparator();
        QAction* print = menu.addAction(tr("Print"));
        print->setShortcut(QKeySequence::Print);
        connect(print, &QAction::triggered, this, &MainWindow::printDocument);

        menu.addSeparator();
        const QString path = page->model->filePath();

        QAction* copyPath = menu.addAction(tr("Copy Path"));
        connect(copyPath, &QAction::triggered, this, [path]() {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        });

        QAction* reveal = menu.addAction(tr("Reveal in Explorer"));
        connect(reveal, &QAction::triggered, this, [path]() {
            WorkspaceFileOps::revealFileInExplorer(path);
        });

        QAction* external = menu.addAction(
            isHtmlMode ? tr("Open with External Browser") : tr("Open with Default Application"));
        connect(external, &QAction::triggered, this, [path]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });

        menu.exec(globalPos);
        return;
    }

    AlignedMenu menu(this);
    menu.setObjectName(QStringLiteral("previewContextMenu"));
    menu.setCompactShortcutLayout(true);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    if (!mermaidSvg.isEmpty()) {
        QAction* actCopyImage = menu.addAction(tr("Copy Image"));
        actCopyImage->setEnabled(!mermaidPngDataUrl.isEmpty());
        connect(actCopyImage, &QAction::triggered, preview,
                [preview, mermaidPngDataUrl]() { preview->copyMermaidImage(mermaidPngDataUrl); });

        QAction* actCopySvg = menu.addAction(tr("Copy SVG"));
        connect(actCopySvg, &QAction::triggered, preview,
                [preview, mermaidSvg]() { preview->copyMermaidSvg(mermaidSvg); });

        menu.addSeparator();

        QAction* actSaveImage = menu.addAction(tr("Save Image As..."));
        actSaveImage->setEnabled(!mermaidPngDataUrl.isEmpty());
        connect(actSaveImage, &QAction::triggered, preview,
                [preview, mermaidPngDataUrl]() { preview->saveMermaidImageAs(mermaidPngDataUrl); });

        QAction* actSaveSvg = menu.addAction(tr("Save SVG As..."));
        connect(actSaveSvg, &QAction::triggered, preview,
                [preview, mermaidSvg]() { preview->saveMermaidSvgAs(mermaidSvg); });

        menu.exec(globalPos);
        return;
    }

    if (preview->isMindMap()) {
        if (m_actMindMap)
            menu.addAction(m_actMindMap);

        QAction* fit = menu.addAction(tr("Fit Mind Map to Window"));
        connect(fit, &QAction::triggered, preview, &PreviewWidget::fitMindMap);

        menu.addSeparator();

        QAction* saveSvg = menu.addAction(tr("Save Mind Map as SVG..."));
        connect(saveSvg, &QAction::triggered, this, [this, preview]() {
            if (!preview)
                return;
            const QString initialPath = exportInitialPath(QStringLiteral(".svg"));
            // The SVG arrives from the page asynchronously, so the tab owning
            // this preview can be closed or dragged away first (Rule 12): the
            // receiver is MainWindow, which keeps the connection alive, so the
            // preview must be held as a QPointer rather than a raw pointer.
            connect(preview, &PreviewWidget::mindMapSvgReady, this,
                    [target = QPointer<PreviewWidget>(preview), initialPath](const QString& svg) {
                        if (target)
                            target->saveMindMapSvgAs(svg, initialPath);
                    }, Qt::SingleShotConnection);
            preview->requestMindMapSvg();
        });

        menu.addSeparator();
        if (m_actExportPdf)  menu.addAction(m_actExportPdf);
        if (m_actExportDocx) menu.addAction(m_actExportDocx);
        if (m_actExportHtml) menu.addAction(m_actExportHtml);
        menu.exec(globalPos);
        return;
    }

    QAction* actCopy = menu.addAction(tr("Copy"));
    actCopy->setEnabled(hasSelection);
    connect(actCopy, &QAction::triggered, preview, &PreviewWidget::copy);

    if (hasSelection) {
        QAction* actGoToSource = menu.addAction(tr("Go to Source"));
        actGoToSource->setEnabled(sourceLine > 0 && page && page->editor);
        connect(actGoToSource, &QAction::triggered, this, [this, page, sourceLine]() {
            if (!page || !page->editor || sourceLine <= 0)
                return;
            // Jumping to the source means looking at the Editor, so this leaves
            // Reading Mode rather than scrolling a hidden widget.
            leaveReadingModeForEditing();
            page->editor->goToSourceLine(sourceLine);
        });
    }

    QAction* actSelectAll = menu.addAction(tr("Select All"));
    connect(actSelectAll, &QAction::triggered, preview, &PreviewWidget::selectAll);

    menu.addSeparator();
    if (m_actMindMap)
        menu.addAction(m_actMindMap);
    if (m_actExportPdf)  menu.addAction(m_actExportPdf);
    if (m_actExportDocx) menu.addAction(m_actExportDocx);
    if (m_actExportHtml) menu.addAction(m_actExportHtml);

    menu.addSeparator();
    QAction* actRefreshPreview = menu.addAction(tr("Refresh"));
    // Display-only: F5 reaches this command through PreviewWidget's WebView2
    // accelerator-key forwarding (Command::RefreshPreview), not through Qt's
    // shortcut system, since a native WebView2 child never delivers key events
    // to Qt on its own. Showing it here just labels that binding; the menu
    // itself is transient (built and destroyed around exec()), so this never
    // becomes a live window-wide F5 binding that could collide with the
    // window-wide View > Refresh action (m_actRefresh).
    actRefreshPreview->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::PreviewRefresh));
    connect(actRefreshPreview, &QAction::triggered, this, &MainWindow::refreshActivePreview);

    QAction* actOpenInBrowser = menu.addAction(tr("Open in Browser"));
    connect(actOpenInBrowser, &QAction::triggered, this, &MainWindow::doPreviewBrowser);
    if (m_actPrint) menu.addAction(m_actPrint);

    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
void MainWindow::refreshActivePreview()
{
    if (!m_activePreview) return;
    PreviewWidget* preview = m_activePreview;

    m_htmlDirty = true;
    m_previewDirty = true;
    const double previousPreviewFraction = preview->lastScrollFraction();

    // Start the navigation first. updatePreview() can itself push a
    // render, and connecting after it can consume that earlier
    // previewRendered signal before the refresh navigation has finished.
    preview->refreshPreview();
    connect(preview, &PreviewWidget::previewRendered, this,
            [this, preview, previousPreviewFraction]() {
        if (preview != m_activePreview || !m_activeEditor)
            return;
        preview->setScrollFraction(previousPreviewFraction);
    }, Qt::SingleShotConnection);
    updatePreview();
}

// ---------------------------------------------------------------------------
// The window-wide View > Refresh fallback for F5. Preview (WebView2
// accelerator forwarding to refreshActivePreview()) and Source Control (its
// own WidgetWithChildrenShortcut) each intercept F5 before it reaches this
// action while they have focus, exactly as they already did over the old
// Insert > Date & Time F5 binding -- so only Workspace-tree focus and
// everything else land here.
void MainWindow::refreshActiveContext()
{
    if (m_workspacePanelCtrl && m_workspacePanelCtrl->hasWorkspaceFocus()) {
        m_workspacePanelCtrl->updateRoot();
        return;
    }
    // No meaningful refresh for the editor or any other context: safely do
    // nothing rather than inventing a new operation.
}

// ---------------------------------------------------------------------------
void MainWindow::openFind()
{
    TabPage* page = m_tabs->currentPage();
    if (!page || !page->editor)
        return;
    // In Reading Mode the Editor is hidden, so Find searches the rendered page
    // through WebView2's own find bar instead of the Markdown source. Seeded
    // with the last editor-side term so Ctrl+F carries the search across the
    // two modes. A runtime too old for the Find API returns false and falls
    // through to the normal editor-side Find below.
    if (isReadingModeActive() && page->preview && page->preview->isVisible()) {
        page->preview->ensureInitialized();
        // Focus first: Ctrl+F can arrive from the menu or toolbar with Qt
        // holding native focus, and the find bar only receives typing if the
        // WebView2 child has it. Start() focuses the bar's input itself, so
        // this must not run afterwards or the caret lands in the page instead.
        page->preview->focusContent();
        if (page->preview->showFindBar(m_findController->term()))
            return;
    }

    // The overlay anchors to the Editor pane and the matches are highlighted in
    // it, so searching implies leaving Reading Mode.
    leaveReadingModeForEditing();
    ensureFindOverlay();
    m_findOverlay->openFind();
}

// ---------------------------------------------------------------------------
void MainWindow::openReplace()
{
    TabPage* page = m_tabs->currentPage();
    if (!page || !page->editor)
        return;
    if (page->model->editorMode() == EditorMode::Pdf) {
        openFind();
        return;
    }
    leaveReadingModeForEditing();
    ensureFindOverlay();
    m_findOverlay->openReplace();
}

// ---------------------------------------------------------------------------
// F3/Shift+F3 work without the overlay ever having been shown. With no term
// yet they open it instead; with a term that simply has no matches they stay
// silent rather than popping the overlay open on every keypress.
void MainWindow::findNext()
{
    // F3 while the Preview's find bar is up must walk *its* matches; falling
    // through would move the hidden Editor's caret with nothing to show for it.
    // With no session yet, F3 opens the bar -- the same thing it does in the
    // Editor when no term has been typed.
    if (PreviewWidget* preview = readingModeFindTarget()) {
        if (!preview->findNextMatch())
            openFind();
        return;
    }
    // A PDF's matches exist only while its search session does, and closing the
    // overlay ends that, so F3 reopens it rather than walking nothing.
    if (m_findController->term().isEmpty()
        || (m_findController->pdfPreview() && !m_findController->isActive())) {
        openFind();
        return;
    }
    m_findController->findNext();
}

// ---------------------------------------------------------------------------
void MainWindow::findPrevious()
{
    if (PreviewWidget* preview = readingModeFindTarget()) {
        if (!preview->findPreviousMatch())
            openFind();
        return;
    }
    if (m_findController->term().isEmpty()
        || (m_findController->pdfPreview() && !m_findController->isActive())) {
        openFind();
        return;
    }
    m_findController->findPrevious();
}

// The Preview to route Find at, or null when Find belongs to the Editor.
PreviewWidget* MainWindow::readingModeFindTarget() const
{
    if (!isReadingModeActive())
        return nullptr;
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!page || !page->preview || !page->preview->isVisible())
        return nullptr;
    return page->preview;
}

// ---------------------------------------------------------------------------
void MainWindow::openGoToLine()
{
    if (!m_goToLineDialog) {
        m_goToLineDialog = new GoToLineDialog(this);
        connect(m_goToLineDialog, &GoToLineDialog::navigationRequested,
                this, &MainWindow::recordNavigationDeparture);
    }

    if (m_activeEditor) {
        // Go To reveals, centers and flashes a line in the Editor -- pointless
        // while Reading Mode has it hidden.
        leaveReadingModeForEditing();
        m_goToLineDialog->setEditor(m_activeEditor);
        m_goToLineDialog->selectLineTab();
        m_goToLineDialog->show();
        m_goToLineDialog->raise();
        m_goToLineDialog->activateWindow();
    }
}

// ---------------------------------------------------------------------------
// Source Control: a mode of the Workspace panel, and diffs as document tabs.

void MainWindow::connectSourceControlPanel(SourceControlPanel* panel)
{
    if (!panel || m_sourceControl == panel)
        return;
    m_sourceControl = panel;

    connect(panel, &SourceControlPanel::openFileRequested,
            this, [this](const QString& path) { openFilePath(path); });
    // A pull, stash creation or discard can replace files that are open.
    connect(panel, &SourceControlPanel::worktreeChanged,
            this, &MainWindow::recheckOpenFilesOnDisk);
    connect(panel, &SourceControlPanel::diffRequested,
            this, &MainWindow::openGitDiff);
    connect(panel, &SourceControlPanel::refreshed,
            this, &MainWindow::refreshOpenDiffTabs);
}

void MainWindow::showSourceControl()
{
    if (!m_workspacePanelCtrl)
        return;
    // Notepad Mode hides the Workspace panel outright, so asking for Source
    // Control has to leave it first -- the same way Find leaves Reading Mode.
    if (isNotepadModeActive())
        setNotepadMode(false);
    if (!m_workspacePanelCtrl->isVisible())
        m_workspacePanelCtrl->setVisible(true);
    m_workspacePanelCtrl->setPanelMode(WorkspacePanelController::PanelMode::SourceControl);
    if (m_sourceControl)
        m_sourceControl->focusCommitMessage();
}

// ---------------------------------------------------------------------------
// Search in Files: the third mode of the Workspace panel.

void MainWindow::openWorkspaceSearch(bool expandReplace)
{
    if (!m_workspacePanelCtrl)
        return;
    // Notepad Mode hides the Workspace panel outright, so asking for Search has
    // to leave it first -- the same way Source Control does.
    if (isNotepadModeActive())
        setNotepadMode(false);
    if (!m_workspacePanelCtrl->isVisible())
        m_workspacePanelCtrl->setVisible(true);
    m_workspacePanelCtrl->openSearch(expandReplace);
}

void MainWindow::connectSearchPanel(WorkspaceSearchPanel* panel)
{
    if (!panel)
        return;

    // Every editable, path-bearing document in every window: a file is open in
    // at most one tab process-wide, and its unsaved buffer -- not the disk copy --
    // is what a search must read and a replacement must edit.
    panel->setOpenEditorsProvider([]() {
        QList<WorkspaceSearchPanel::OpenEditor> editors;
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            auto* window = qobject_cast<MainWindow*>(topLevel);
            if (!window || !window->m_tabs)
                continue;
            for (int i = 0; i < window->m_tabs->count(); ++i) {
                TabPage* page = window->m_tabs->pageAt(i);
                if (!page || !page->model || !page->editor)
                    continue;
                const DocumentModel* model = page->model;
                if (model->isViewer() || model->isBundled() || model->isDiff()
                    || model->filePath().isEmpty())
                    continue;
                editors.append({model->filePath(), page->editor});
            }
        }
        return editors;
    });
    panel->setEditedHook([](QPlainTextEdit* editor) {
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            auto* window = qobject_cast<MainWindow*>(topLevel);
            if (!window || !window->m_tabs)
                continue;
            for (int i = 0; i < window->m_tabs->count(); ++i) {
                TabPage* page = window->m_tabs->pageAt(i);
                if (page && page->editor == editor) {
                    window->noteDocumentEditedBySearch(page);
                    return;
                }
            }
        }
    });
    connect(panel, &WorkspaceSearchPanel::openMatchRequested,
            this, &MainWindow::openSearchMatch);
}

void MainWindow::noteDocumentEditedBySearch(TabPage* page)
{
    // The active tab's editor already reports through onTextChanged().
    if (!page || !page->model || !m_tabs || page == m_tabs->currentPage())
        return;
    page->model->setDirty(true);
    ++m_editRevisions[page->model];
    m_tabs->refreshTitle(m_tabs->indexOf(page));
    scheduleAutoSave(page->model);
}

void MainWindow::openSearchMatch(const QString& path, int line, int column, int length)
{
    OpenDocumentRef ref = findOpenDocument(path);
    if (ref.page && !ref.page->model->isViewer()) {
        activateOpenDocument(ref);
    } else if (ref.page || isViewerMode(DocumentModel::modeForPath(path))) {
        // A match cannot be selected in a rendered page (HTML, SVG, PDF...).
        openFileAsText(path);
    } else {
        openFilePath(path, true);
    }

    ref = findOpenDocument(path);
    if (!ref.page || !ref.page->editor || ref.page->model->isViewer() || !ref.window)
        return;
    ref.window->leaveReadingModeForEditing();

    MarkdownEditor* editor = ref.page->editor;
    QTextDocument* document = editor->document();
    const QTextBlock block = document->findBlockByNumber(line);
    if (!block.isValid())
        return;
    const int start = block.position() + qMin(column, block.length() - 1);
    const int end = qMin(start + length, block.position() + block.length() - 1);
    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
    if (ref.window != this) {
        ref.window->raise();
        ref.window->activateWindow();
    }
}

void MainWindow::syncSidebarModeActions()
{
    if (!m_workspacePanelCtrl)
        return;
    const bool visible = m_workspacePanelCtrl->isVisible();
    const bool sourceControlActive = visible &&
        m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::SourceControl;
    // Search is a third mode of the same slot: neither View action is checked
    // while it shows, so F8/F7 switch to Explorer/Source Control instead of
    // finding their own checkmark already set and doing nothing.
    const bool workspaceActive = visible &&
        m_workspacePanelCtrl->panelMode() == WorkspacePanelController::PanelMode::Workspace;

    if (m_actSourceControl && m_actSourceControl->isChecked() != sourceControlActive) {
        QSignalBlocker blocker(m_actSourceControl);
        m_actSourceControl->setChecked(sourceControlActive);
    }
    if (m_actWorkspacePanel && m_actWorkspacePanel->isChecked() != workspaceActive) {
        QSignalBlocker blocker(m_actWorkspacePanel);
        m_actWorkspacePanel->setChecked(workspaceActive);
    }
    if (m_actToggleWorkspace && m_actToggleWorkspace->isChecked() != visible) {
        QSignalBlocker blocker(m_actToggleWorkspace);
        m_actToggleWorkspace->setChecked(visible);
    }
    if (m_titleBar)
        m_titleBar->syncLayoutToggles();
}

MainWindow::OpenDocumentRef MainWindow::findOpenDiffDocument(const GitDiffSpec& spec)
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(widget);
        if (!window || !window->m_tabs)
            continue;
        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* page = window->m_tabs->pageAt(i);
            if (!page || !page->model)
                continue;
            const GitDiffSpec* open = page->model->diffSpec();
            if (open && open->matches(spec))
                return { window, page, i };
        }
    }
    return {};
}

void MainWindow::openGitDiff(const GitDiffSpec& spec)
{
    if (!spec.isValid())
        return;

    // Re-selecting the same file must re-use its tab, not stack another copy.
    const OpenDocumentRef existing = findOpenDiffDocument(spec);
    if (existing.window) {
        activateOpenDocument(existing);
        existing.window->loadGitDiffInto(existing.page);
        return;
    }

    const int index = m_tabs->addNewTab(QString(), true, nullptr, &spec);
    if (TabPage* page = m_tabs->pageAt(index))
        loadGitDiffInto(page);
    updateWindowTitle();
    syncCurrentTabUi();
}

void MainWindow::loadGitDiffInto(TabPage* page)
{
    if (!page || !page->model || !page->diffView)
        return;
    const GitDiffSpec* spec = page->model->diffSpec();
    if (!spec)
        return;

    // Rule 12: the tab can be closed while git is still running, so the target
    // is captured as a QPointer and re-checked when the result arrives.
    QPointer<GitDiffView> target = page->diffView;
    const GitDiffSpec captured = *spec;
    if (target->property("fastmdGitDiffRunning").toBool()) {
        // Coalesce refresh storms to one follow-up read. Launching overlapping
        // git processes for the same tab wastes work and creates an ordering
        // race even if stale results are later rejected.
        target->setProperty("fastmdGitDiffDirty", true);
        return;
    }
    target->setProperty("fastmdGitDiffRunning", true);
    target->setProperty("fastmdGitDiffDirty", false);
    const quint64 requestId = target->property("fastmdGitDiffRequest").toULongLong() + 1;
    target->setProperty("fastmdGitDiffRequest", QVariant::fromValue(requestId));

    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, target, requestId] {
        const QByteArray unified = watcher->result();
        watcher->deleteLater();
        if (!target)
            return;
        target->setProperty("fastmdGitDiffRunning", false);
        if (target->property("fastmdGitDiffDirty").toBool()) {
            target->setProperty("fastmdGitDiffDirty", false);
            for (int i = 0; i < m_tabs->count(); ++i) {
                TabPage* candidate = m_tabs->pageAt(i);
                if (candidate && candidate->diffView == target) {
                    loadGitDiffInto(candidate);
                    break;
                }
            }
            return;
        }
        // Defensive request identity check: only the newest request for this
        // view may paint.
        if (target->property("fastmdGitDiffRequest").toULongLong() != requestId)
            return;
        const QVector<GitDiffRow> rows = GitRepository::toSideBySide(unified);
        if (rows.isEmpty())
            target->showPlaceholder(tr("No changes to show for this file."));
        else
            target->setRows(rows);
    });
    watcher->setFuture(QtConcurrent::run([captured] {
        QStringList args;
        if (captured.source == GitDiffSpec::Commit) {
            args = { QStringLiteral("show"), QStringLiteral("--no-color"),
                     QStringLiteral("-U3"), QStringLiteral("--format="),
                     captured.commitHash, QStringLiteral("--"), captured.path };
            return GitRunner::run(captured.repoRoot, args).out;
        }

        args = { QStringLiteral("diff"), QStringLiteral("--no-color"),
                 QStringLiteral("-U3") };
        if (captured.source == GitDiffSpec::Index)
            args << QStringLiteral("--cached");
        args << QStringLiteral("--") << captured.path;
        GitRunner::Result result = GitRunner::run(captured.repoRoot, args);

        // An untracked file has no diff at all; show it as an all-added file
        // so clicking it still does something useful.
        if (result.ok() && result.out.trimmed().isEmpty()
            && captured.source == GitDiffSpec::WorkingTree) {
            const GitRunner::Result intent = GitRunner::run(
                captured.repoRoot,
                { QStringLiteral("diff"), QStringLiteral("--no-color"),
                  QStringLiteral("-U3"), QStringLiteral("--no-index"),
                  QStringLiteral("--"), QStringLiteral("/dev/null"), captured.path });
            if (!intent.out.trimmed().isEmpty())
                return intent.out;
        }
        return result.out;
    }));
}

void MainWindow::refreshOpenDiffTabs()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* page = m_tabs->pageAt(i);
        if (!page || !page->model)
            continue;
        const GitDiffSpec* spec = page->model->diffSpec();
        // A commit-to-commit diff cannot change; only the working tree and the
        // index move underneath an open tab.
        if (spec && spec->source != GitDiffSpec::Commit)
            loadGitDiffInto(page);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::recheckOpenFilesOnDisk()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* page = m_tabs->pageAt(i);
        if (!page || !page->model || page->model->isUntitled())
            continue;
        onFileChanged(page->model->filePath());
    }
}

// ---------------------------------------------------------------------------
void MainWindow::recordClosedTab(int index, const TabPage* page)
{
    if (!page || !page->model || !page->editor)
        return;
    // Bundled content is reopened from the Help menu, not from the closed-tab
    // history: restoring it would produce an ordinary, editable untitled copy.
    if (page->model->isBundled())
        return;
    // Same reasoning for a Git diff: it has no path, so bringing it back would
    // produce an empty untitled document instead of the comparison.
    if (page->model->isDiff())
        return;

    ClosedTabRecord record;
    record.index = index;
    record.state = captureTabState(page);
    record.untitled = page->model->isUntitled();
    if (record.untitled) {
        record.text = page->editor->toPlainText();
        record.dirty = page->model->isDirty();
        // A pristine, empty scratch tab carries nothing to bring back, and
        // remembering it would just push a real document out of the history.
        if (record.text.isEmpty() && !record.dirty)
            return;
        record.lineEnding = page->model->lineEnding();
        record.encoding = page->model->encoding();
    }

    m_closedTabs.append(record);
    constexpr int kMaxClosedTabs = 20;
    while (m_closedTabs.size() > kMaxClosedTabs)
        m_closedTabs.removeFirst();
    updateReopenClosedTabAction();
}

// ---------------------------------------------------------------------------
void MainWindow::updateReopenClosedTabAction()
{
    if (m_actReopenClosedTab)
        m_actReopenClosedTab->setEnabled(!m_closedTabs.isEmpty());
}

// ---------------------------------------------------------------------------
void MainWindow::reopenClosedTab()
{
    if (m_closedTabs.isEmpty())
        return;

    const ClosedTabRecord record = m_closedTabs.takeLast();
    updateReopenClosedTabAction();

    int index = -1;
    if (!record.untitled) {
        const QString path = record.state.path;
        if (path.isEmpty())
            return;
        // Reopening a file that has since been opened again (here or in another
        // window) must not produce a second editable tab for it.
        if (const OpenDocumentRef existing = findOpenDocument(path); existing.page) {
            activateOpenDocument(existing);
            return;
        }
        if (!QFileInfo::exists(path)) {
            themedWarning(this, tr("Reopen Failed"), tr("Cannot find file:\n%1").arg(path));
            return;
        }
        index = addTabFromFile(path, true, true, true, &record.state);
        if (index < 0)
            return;
        // Session restore attaches its watchers in one deferred batch; a
        // reopened tab is a normal open and watches immediately.
        watchFile(path);
        m_lastOpenFolder = QFileInfo(path).absolutePath();
    } else {
        index = m_tabs->addNewTab();
        TabPage* page = m_tabs->pageAt(index);
        if (!page)
            return;
        page->model->setLineEnding(record.lineEnding);
        page->model->setEncoding(record.encoding);
        page->editor->setPlainText(record.text);
        page->editor->document()->setModified(record.dirty);
        page->model->setDirty(record.dirty);
        page->model->setAutomaticallyDetectedName(record.state.automaticallyDetectedName);
        if (record.state.hasLastNonMarkdownSelection) {
            page->model->setLastNonMarkdownSelection(
                { record.state.lastNonMarkdownDefinitionName, record.state.lastNonMarkdownSource });
        }
        setDocumentLanguage(page, record.state.selectedDefinitionName, record.state.languageSource);
        const bool markdown = page->model->editorMode() == EditorMode::Markdown;
        page->model->setMarkdownPreviewVisible(markdown && record.state.previewVisible);
        page->model->setMarkdownPresentation(
            markdown ? record.state.presentation : MarkdownPresentation::Preview);
        m_tabs->refreshTitle(index);

        // The editor has no layout yet, so the scroll bar has no range to
        // restore into until the next event-loop turn (same reason
        // restoreTabState() defers it).
        QTimer::singleShot(0, page->editor, [editor = page->editor, cursorPos = record.state.cursorPos,
                                             scroll = record.state.editorScroll, this]() {
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(qBound(0, cursorPos, editor->toPlainText().size()));
            editor->setTextCursor(cursor);
            editor->verticalScrollBar()->setValue(qMax(0, scroll));
            if (editor == m_activeEditor)
                onCursorPositionChanged();
        });
    }

    if (index >= 0 && m_tabs->count() > 0)
        m_tabs->moveTabPage(index, qBound(0, record.index, m_tabs->count() - 1));

    updateWindowTitle();
    syncCurrentTabUi();
    // Same as openFilePath(): a tab built directly in the reading layout has a
    // hidden Editor, so the Preview is what takes keyboard control.
    if (isReadingModeActive())
        focusReadingModePreview();
    m_workspacePanelCtrl->updateRoot();
}

// ---------------------------------------------------------------------------
void MainWindow::showOutlinePopup()
{
    ensureOutlinePopup();

    if (m_outlinePopup->isVisible()) {
        m_outlinePopup->hide();
        return;
    }

    qint64 lastHideTime = m_outlinePopup->property("lastHideTime").toLongLong();
    if (QDateTime::currentMSecsSinceEpoch() - lastHideTime < 100) {
        return; // Prevents immediate reopen if the button click closed the Qt::Popup
    }

    TabPage* page = m_tabs->currentPage();
    if (!page || !page->editor || !page->model || !page->preview) return;

    m_outlinePopup->setEditor(page->editor, page->model);
    m_outlinePopup->resetPinned();

    // Position it just below the (top-right) outline button, right-aligned
    // to it, and let it fly in from there.
    const QRect btnRect = page->preview->outlineButtonGeometry();
    QPoint pos(btnRect.x(), btnRect.y() + btnRect.height() + 4);
    pos.setX(pos.x() - m_outlinePopup->width() + btnRect.width());
    page->preview->setOutlinePopupOpen(true);

    m_outlinePopup->showAt(pos);
}
