#include "MainWindow.h"
#include "FontPreferences.h"
#include "TerminalVisibilityPreferences.h"
#include "StartupProfiler.h"
#include "TerminalPanel.h"
#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "FindController.h"
#include "RecoveryManager.h"
#include "ExportManager.h"
#include "DocumentModel.h"
#include "ThemedMessageBox.h"
#include "RecentFilesManager.h"
#include "WorkspacePanelController.h"
#include "ThemedSplitter.h"
#include "RecentFoldersManager.h"
#include "SourceControlPanel.h"
#include "GitDiffView.h"
#include "MainWindowTextIO.h"
#include "SyntaxDefinitionResolver.h"
#include "ImageFormats.h"
#include "WhatsNew.h"

#include <QCoreApplication>
#include <QSettings>
#include <QApplication>
#include <QSignalBlocker>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <QPointer>
#include <QTextCursor>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>
#include <QFileSystemWatcher>
#include <QScrollBar>
#include <QSplitter>
#include <QPageSize>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QMetaObject>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------
// Shared by pre-language-feature session tabs and version-1 recovery
// snapshots, which both carry only the coarse EditorMode. Per
// language-indicator-plan.md section 6: a saved mode that agrees with the
// filename's own language becomes Automatic; a disagreement (Markdown mode on
// a non-Markdown filename, or the old Code/Text mode on a Markdown filename)
// becomes an explicit Manual choice, which also clears the previous hidden
// inconsistency where a Code/Text tab still ran the Markdown highlighter.
void MainWindow::migrateLegacyDocumentLanguage(TabPage* page, EditorMode savedMode)
{
    if (!page)
        return;

    const auto detected = SyntaxDefinitionResolver::autoDetect(page->model->filePath());
    const QString detectedName = detected.isValid() ? detected.name() : QString();
    page->model->setAutomaticallyDetectedName(detectedName);

    const bool detectedIsMarkdown = SyntaxDefinitionResolver::isMarkdownDefinitionName(detectedName);
    const bool savedIsMarkdown = savedMode == EditorMode::Markdown;
    if (savedIsMarkdown == detectedIsMarkdown)
        setDocumentLanguage(page, detectedName, LanguageSelectionSource::Automatic);
    else if (savedIsMarkdown)
        setDocumentLanguage(page, SyntaxDefinitionResolver::fastMdMarkdownDefinitionName(),
                            LanguageSelectionSource::Manual);
    else
        setDocumentLanguage(page, QString(), LanguageSelectionSource::Manual);
}

// ---------------------------------------------------------------------------
MainWindow::SessionTabState MainWindow::captureTabState(const TabPage* page) const
{
    SessionTabState state;
    if (!page || !page->model)
        return state;

    state.path = page->model->filePath();
    if (page->editor) {
        state.cursorPos = page->editor->textCursor().position();
        state.editorScroll = page->editor->verticalScrollBar()->value();
    }
    if (page->preview)
        state.previewScroll = int(page->preview->lastScrollFraction() * 10000);
    state.mode = page->model->editorMode();
    state.previewVisible = page->model->markdownPreviewVisible();
    state.presentation = page->model->markdownPresentation();

    state.hasLanguageState = true;
    state.selectedDefinitionName = page->model->selectedDefinitionName();
    state.automaticallyDetectedName = page->model->automaticallyDetectedName();
    state.languageSource = page->model->languageSelectionSource();
    if (page->model->hasLastNonMarkdownSelection()) {
        const NonMarkdownLanguageSelection last = page->model->lastNonMarkdownSelection();
        state.hasLastNonMarkdownSelection = true;
        state.lastNonMarkdownDefinitionName = last.definitionName;
        state.lastNonMarkdownSource = last.source;
    }
    return state;
}

// ---------------------------------------------------------------------------
void MainWindow::restoreTabState(TabPage* page, const SessionTabState& state)
{
    if (!page)
        return;

    if (page->model->editorMode() == EditorMode::Image) {
        // addNewTab() already created/loaded the ImageViewer for this mode;
        // zoom/pan state is deliberately not persisted, so nothing else to do.
        return;
    }

    if (page->model->editorMode() == EditorMode::Pdf || page->model->editorMode() == EditorMode::Html
        || page->model->editorMode() == EditorMode::Svg) {
        page->editor->setVisible(false);
        page->preview->setVisible(true);
        page->preview->showReadOnlyFile(page->model->filePath());
        return;
    }

    if (state.hasLanguageState) {
        page->model->setAutomaticallyDetectedName(state.automaticallyDetectedName);
        if (state.hasLastNonMarkdownSelection) {
            page->model->setLastNonMarkdownSelection(
                { state.lastNonMarkdownDefinitionName, state.lastNonMarkdownSource });
        }
        // A manually restored definition that no longer exists (e.g. after a
        // dependency upgrade removed it) falls back to Auto Detect.
        QString nameToApply = state.selectedDefinitionName;
        LanguageSelectionSource sourceToApply = state.languageSource;
        const bool isMarkdownName = SyntaxDefinitionResolver::isMarkdownDefinitionName(nameToApply);
        if (!nameToApply.isEmpty() && !isMarkdownName
            && !SyntaxDefinitionResolver::definitionForStoredName(nameToApply).isValid()) {
            nameToApply = state.automaticallyDetectedName;
            sourceToApply = LanguageSelectionSource::Automatic;
        }
        setDocumentLanguage(page, nameToApply, sourceToApply);
    } else {
        migrateLegacyDocumentLanguage(page, state.mode);
    }

    const bool markdown = page->model->editorMode() == EditorMode::Markdown;
    page->model->setMarkdownPreviewVisible(markdown ? state.previewVisible : false);
    page->model->setMarkdownPresentation(markdown ? state.presentation : MarkdownPresentation::Preview);
    page->preview->setPresentation(page->model->markdownPresentation() == MarkdownPresentation::MindMap
        ? PreviewWidget::Presentation::MindMap : PreviewWidget::Presentation::Preview);
    page->preview->setVisible(markdown && state.previewVisible);

    // Do not render preview HTML here: most restored tabs are inactive and
    // invisible, so parsing Markdown for each one is wasted startup work.
    // onEditorActivated()/syncCurrentTabUi() render the active tab's preview
    // once it is actually selected; other tabs render on first activation.
    if (markdown) {
        page->preview->setBasePath(page->model->filePath());
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

    // One-time Editor -> Preview sync so the Preview opens already scrolled to
    // roughly where the Editor is, instead of at the top. Scrollbar-fraction
    // restore (the old state.previewScroll) doesn't work here: Editor and
    // Preview content heights differ substantially, and it also races the
    // Preview's actual layout (WebView2 renders asynchronously) -- which is
    // why double-click source highlighting could look desynced/slow right
    // after startup, since the Preview simply wasn't showing the same part of
    // the document as the Editor yet.
    //
    // previewRendered() fires once content is pushed, mermaid/highlight/KaTeX
    // have run, and layout has settled (see preview.js's reportRendered()) --
    // i.e. exactly when data-src-start/-end mapping and scrollHeight are both
    // trustworthy. Qt::SingleShotConnection limits this to the first render
    // only; later edits must not re-trigger it. At the document top, leave the
    // Preview at its natural top position rather than centering the editor
    // viewport's middle line. Only wired up for tabs whose Preview is actually
    // visible, so an invisible tab's still-uncreated WebView2 controller is
    // never forced into existing early (previewRendered would otherwise never
    // fire for it, per PreviewWidget's lazy-init model).
    if (markdown && state.previewVisible && state.editorScroll > 0) {
        QPointer<PreviewWidget> preview(page->preview);
        connect(page->preview, &PreviewWidget::previewRendered, page->editor,
                [this, editor = page->editor, preview]() {
            if (!editor || !preview)
                return;
            const QTextCursor centerCursor =
                editor->cursorForPosition(editor->viewport()->rect().center());
            const int line = centerCursor.blockNumber() + 1;
            const bool wasSyncing = m_scrollSyncing;
            m_scrollSyncing = true;
            preview->scrollToSourceLine(line);
            m_scrollSyncing = wasSyncing;
        }, Qt::SingleShotConnection);
    }
}

// ---------------------------------------------------------------------------
void MainWindow::restorePreviousSession()
{
    FASTMD_MARK("  task: session restore begin");
    if (!m_restoreSessionOnStartup) {
        if (m_tabs->count() == 0)
            openInitialTab();
        m_workspacePanelCtrl->updateRoot();
        syncCurrentTabUi();
        completeStartup();
        return;
    }

    m_pendingSessionTabIndices.clear();
    m_pendingRestoredWatchPaths.clear();
    int activeStateIndex = -1;
    QSet<QString> restoredKeys;
    for (int i = 0; i < m_sessionTabs.size(); ++i) {
        const SessionTabState& state = m_sessionTabs.at(i);
        if (state.path.isEmpty() || !QFile::exists(state.path))
            continue;
        const QString key = DocumentModel::pathKeyFor(state.path);
        if (key.isEmpty() || restoredKeys.contains(key))
            continue;
        restoredKeys.insert(key);
        if (i == m_sessionActiveTab)
            activeStateIndex = i;
        else
            m_pendingSessionTabIndices.append(i);
    }
    if (activeStateIndex < 0 && !m_pendingSessionTabIndices.isEmpty())
        activeStateIndex = m_pendingSessionTabIndices.takeFirst();

    if (activeStateIndex < 0) {
        openInitialTab();
    } else {
        loadSessionTab(activeStateIndex, true);
        return;
    }
    m_workspacePanelCtrl->updateRoot();
    completeStartup();
    if (!m_pendingSessionTabIndices.isEmpty())
        enqueueStartupTask([this]() { restoreNextSessionTab(); }, 3);
}

void MainWindow::restoreNextSessionTab()
{
    if (m_pendingSessionTabIndices.isEmpty())
        return;

    const int stateIndex = m_pendingSessionTabIndices.takeFirst();
    loadSessionTab(stateIndex, false);
}

void MainWindow::loadSessionTab(int stateIndex, bool active)
{
    if (stateIndex < 0 || stateIndex >= m_sessionTabs.size())
        return;
    const SessionTabState state = m_sessionTabs.at(stateIndex);
    const QString suffix = QFileInfo(state.path).suffix();
    const bool readOnly = suffix.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0
        || ImageFormats::isImageExtension(suffix);
    // Never post the result straight at `this` from the worker: this window can
    // be destroyed while the read is in flight, and an event posted to a freed
    // QObject crashes in sendPostedEvents. A queued signal/slot connection is
    // torn down safely when the receiver dies; a raw pointer is not.
    struct LoadedTab {
        EditableTextFileLoadResult result = EditableTextFileLoadResult::Success;
        QString content;
        QString lineEnding;
        TextEncoding encoding = TextEncoding::Utf8;
        qint64 fileSize = -1;
        QDateTime lastModified;
    };
    auto loaded = std::make_shared<LoadedTab>();
    if (active)
        FASTMD_MARK("    session: active file read dispatched");
    QThread* worker = QThread::create([loaded, path = state.path, readOnly]() {
        loaded->result = readOnly
            ? EditableTextFileLoadResult::Success
            : loadEditableTextFile(path, &loaded->content, &loaded->lineEnding, &loaded->encoding);
        const QFileInfo fileInfo(path);
        loaded->fileSize = fileInfo.exists() ? fileInfo.size() : -1;
        loaded->lastModified = fileInfo.exists() ? fileInfo.lastModified() : QDateTime();
    });
    connect(worker, &QThread::finished, this, [this, loaded, stateIndex, active]() {
        restoreLoadedSessionTab(stateIndex, active, loaded->result,
                                std::move(loaded->content), std::move(loaded->lineEnding),
                                loaded->encoding, loaded->fileSize, loaded->lastModified);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::restoreLoadedSessionTab(int stateIndex, bool active, EditableTextFileLoadResult result,
                                         QString content, QString lineEnding, TextEncoding encoding,
                                         qint64 fileSize, QDateTime lastModified)
{
    if (stateIndex < 0 || stateIndex >= m_sessionTabs.size())
        return;
    const SessionTabState& state = m_sessionTabs.at(stateIndex);
    int index = -1;
    if (result == EditableTextFileLoadResult::Success) {
        const QString suffix = QFileInfo(state.path).suffix();
        const bool readOnly = suffix.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0
            || suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
            || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0;
        const QString* text = readOnly ? nullptr : &content;
        const QString* ending = readOnly ? nullptr : &lineEnding;
        const TextEncoding* loadedEncoding = readOnly ? nullptr : &encoding;
        index = addTabFromFile(state.path, active, false, false, &state, text, ending, loadedEncoding);
        if (index >= 0)
            m_pendingRestoredWatchPaths.append({ state.path, fileSize, lastModified });
    }
    if (active) {
        if (index >= 0) {
            m_startupActiveTabWidget = m_tabs->widget(index);
            if (TabPage* pg = m_tabs->currentPage()) {
                const QFileInfo fileInfo(pg->model->filePath());
                if (fileInfo.isFile())
                    m_workspacePanelCtrl->setRoot(fileInfo.absolutePath());
            }
        } else if (m_tabs->count() == 0) {
            m_tabs->addNewTab();
        }
        m_workspacePanelCtrl->updateRoot();
        completeStartup();
        FASTMD_MARK("    session: active tab ready");
        if (!m_pendingSessionTabIndices.isEmpty())
            enqueueStartupTask([this]() { restoreNextSessionTab(); }, 3);
        else {
            attachRestoredFileWatchers();
            FASTMD_MARK("  task: session restore complete");
        }
        return;
    }

    if (index >= 0) {
        const int targetIndex = qMin(stateIndex, m_tabs->count() - 1);
        m_tabs->moveTabPage(index, targetIndex);
        if (m_startupActiveTabWidget) {
            for (int i = 0; i < m_tabs->count(); ++i) {
                TabPage* page = m_tabs->pageAt(i);
                if (page && page->splitter == m_startupActiveTabWidget) {
                    m_tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
    }
    if (!m_pendingSessionTabIndices.isEmpty())
        enqueueStartupTask([this]() { restoreNextSessionTab(); }, 3);
    else {
        attachRestoredFileWatchers();
        FASTMD_MARK("  task: session restore complete");
    }
}

void MainWindow::attachRestoredFileWatchers()
{
    if (m_pendingRestoredWatchPaths.isEmpty())
        return;

    QStringList paths;
    QStringList changedPaths;
    for (const RestoredWatchPath& pending : std::as_const(m_pendingRestoredWatchPaths)) {
        const QFileInfo current(pending.path);
        if (!current.isFile())
            continue;
        paths.append(pending.path);
        if (current.size() != pending.fileSize || current.lastModified() != pending.lastModified)
            changedPaths.append(pending.path);
    }
    m_pendingRestoredWatchPaths.clear();
    if (paths.isEmpty())
        return;

    QFileSystemWatcher* watcher = ensureFileWatcher();
    QStringList missing;
    for (const QString& path : std::as_const(paths)) {
        if (!watcher->files().contains(path))
            missing.append(path);
    }
    if (!missing.isEmpty())
        watcher->addPaths(missing);
    for (const QString& path : std::as_const(changedPaths))
        onFileChanged(path);
}

// ---------------------------------------------------------------------------
void MainWindow::inspectRecoveryFiles()
{
    if (!m_crashRecoveryEnabled)
        return;
    // Result handed back over a queued connection, not posted at a raw `this` --
    // see loadSessionTab().
    struct Inspection {
        QList<RecoverySnapshot> usable;
        QStringList obsoleteKeys;
    };
    auto inspection = std::make_shared<Inspection>();
    QThread* worker = QThread::create([inspection]() {
        for (const RecoverySnapshot& snapshot : RecoveryManager::readAll()) {
            if (!snapshot.path.isEmpty() && QFile::exists(snapshot.path)) {
                QString current;
                QString ending;
                if (readTextFile(snapshot.path, &current, &ending) && current == snapshot.content) {
                    inspection->obsoleteKeys.append(snapshot.key);
                    continue;
                }
            }
            inspection->usable.append(snapshot);
        }
    });
    connect(worker, &QThread::finished, this, [this, inspection]() {
        applyRecoveryInspection(std::move(inspection->usable), std::move(inspection->obsoleteKeys));
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::applyRecoveryInspection(QList<RecoverySnapshot> usable, QStringList obsoleteKeys)
{
    for (const QString& key : obsoleteKeys)
        RecoveryManager::remove(key);
    if (usable.isEmpty())
        return;
    if (themedQuestion(this, tr("Recover Documents"),
            tr("FastMD found %1 recoverable document(s) from an unexpected shutdown. Restore them now?")
                .arg(usable.size()), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
    {
        for (const RecoverySnapshot& snapshot : usable)
            RecoveryManager::remove(snapshot.key);
        return;
    }

    for (const RecoverySnapshot& snapshot : usable) {
        int index = -1;
        if (!snapshot.path.isEmpty() && QFile::exists(snapshot.path)) {
            for (int i = 0; i < m_tabs->count(); ++i)
                if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model->filePath() == snapshot.path)
                    index = i;
            if (index < 0)
                index = addTabFromFile(snapshot.path, false, false, false);
        }
        if (index < 0)
            index = m_tabs->addNewTab({}, false);
        if (TabPage* page = m_tabs->pageAt(index)) {
            page->model->setLineEnding(snapshot.lineEnding);
            page->model->setEncoding(snapshot.encoding);
            page->editor->setPlainText(snapshot.content);
            if (snapshot.hasLanguageState) {
                if (snapshot.hasLastNonMarkdownSelection) {
                    page->model->setLastNonMarkdownSelection(
                        { snapshot.lastNonMarkdownDefinitionName, snapshot.lastNonMarkdownSource });
                }
                QString nameToApply = snapshot.selectedDefinitionName;
                LanguageSelectionSource sourceToApply = snapshot.languageSource;
                if (!nameToApply.isEmpty() && !SyntaxDefinitionResolver::isMarkdownDefinitionName(nameToApply)
                    && !SyntaxDefinitionResolver::definitionForStoredName(nameToApply).isValid()) {
                    nameToApply = snapshot.automaticallyDetectedName;
                    sourceToApply = LanguageSelectionSource::Automatic;
                }
                page->model->setAutomaticallyDetectedName(snapshot.automaticallyDetectedName);
                setDocumentLanguage(page, nameToApply, sourceToApply);
            } else {
                // Version-1 snapshot: derive compatible state from its path
                // and saved mode, same migration rule as legacy session tabs.
                migrateLegacyDocumentLanguage(page, snapshot.mode);
            }
            page->editor->document()->setModified(true);
            page->model->setDirty(true);
            m_editRevisions[page->model] = 1;
            m_tabs->refreshTitle(index);
            scheduleAutoSave(page->model);
        }
        RecoveryManager::remove(snapshot.key);
    }
    m_tabs->setCurrentIndex(m_tabs->count() - 1);
    syncCurrentTabUi();
}

// ---------------------------------------------------------------------------
void MainWindow::readSettings()
{
    const StartupSettings& s = m_startupSettings;
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    m_recentFilesMgr->setFiles(s.value(QStringLiteral("recentFiles")).toStringList());
    m_theme = s.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    m_markdownPreviewVisible = s.value(QStringLiteral("previewVisible"), true).toBool();
    m_wordWrapEnabled = s.value(QStringLiteral("wordWrapEnabled"), true).toBool();
    m_workspaceTreeVisible = s.value(QStringLiteral("workspaceTreeVisible"), true).toBool();
    m_terminalVisible = TerminalVisibilityPreferences::read(
        s.value(QLatin1String(TerminalVisibilityPreferences::Key)));
    m_restoreNotepadModeOnStartup = s.value(QStringLiteral("notepadMode"), false).toBool();
    m_statusBarVisible = s.value(QStringLiteral("statusBarVisible"), true).toBool();
    m_welcomeShown = s.value(QStringLiteral("app/welcomeShown"), false).toBool();
    // Fresh install vs. upgrade vs. repeat launch of the same version --
    // computed once, here, from the version this install last ran (empty on
    // a never-before-recorded install) against the version actually running
    // now. m_lastRunVersion is advanced immediately: a later failure to
    // display What's New (see maybeShowWhatsNew()) must not turn into a
    // retry loop on every subsequent launch. Restricted to the primary
    // window: File > New Window and tab-drag transfer targets share this
    // process and must not re-decide or re-show anything.
    {
        const QString previousVersion = s.value(QStringLiteral("app/lastRunVersion")).toString();
        const QString currentVersion = QCoreApplication::applicationVersion();
        if (m_startupMode == StartupMode::Normal
            && WhatsNew::classifyVersionTransition(m_welcomeShown, previousVersion, currentVersion)
                == WhatsNew::VersionTransition::Upgrade) {
            m_whatsNewVersionToShow = currentVersion;
        }
        m_lastRunVersion = currentVersion;
    }
    m_mathToolbarVisible = s.value(QStringLiteral("mathToolbarVisible"), false).toBool();
    m_chemistryToolbarVisible = s.value(QStringLiteral("chemistryToolbarVisible"), false).toBool();
    m_restoreSessionOnStartup = s.value(QStringLiteral("restoreSessionOnStartup"), false).toBool();
    m_closeAppOnLastTabClose = s.value(QStringLiteral("closeAppOnLastTabClose"), true).toBool();
    m_autoSaveEnabled = s.value(QStringLiteral("autoSave/enabled"), false).toBool();
    m_autoSaveDelayMs = s.value(QStringLiteral("autoSave/delayMs"), 2000).toInt();
    m_crashRecoveryEnabled = s.value(QStringLiteral("autoSave/crashRecovery"), true).toBool();
    m_openPdfExternally = s.value(QStringLiteral("files/openPdfExternally"), false).toBool();
    m_openHtmlExternally = s.value(QStringLiteral("files/openHtmlExternally"), false).toBool();
    m_openImagesExternally = s.value(QStringLiteral("files/openImagesExternally"), false).toBool();
    m_showExportSuccessDialog = s.value(QStringLiteral("export/showSuccessDialog"), true).toBool();
    m_defaultSaveFolder = s.value(QStringLiteral("defaultSaveFolder")).toString();
    m_defaultNewDocMode = static_cast<EditorMode>(
        s.value(QStringLiteral("defaultNewDocMode"), static_cast<int>(EditorMode::Markdown)).toInt());
    m_defaultEditorFontSize = s.value(
        QStringLiteral("editorFontSize"), FastMdDefaults::EditorFontSize).toInt();
    m_diffZoomDelta = s.value(QStringLiteral("gitDiffZoomDelta"), 0).toInt();
    m_tabs->setDiffZoomDelta(m_diffZoomDelta);
    m_highlightCurrentLineEnabled = s.value(QStringLiteral("editor/highlightCurrentLine"), true).toBool();
    // Off by default. The language is validated against what Windows actually
    // has installed the first time any spell-check surface is used, never here
    // - resolving it would spawn the platform factory on the startup path.
    m_spellCheckEnabled = s.value(QStringLiteral("spellCheck/enabled"), false).toBool();
    m_spellCheckLanguage = s.value(QStringLiteral("spellCheck/language")).toString();
    m_renderSingleLineBreaks = s.value(QStringLiteral("editor/renderSingleLineBreaks"), true).toBool();
    // A plain string: whether the family is installed is the browser's concern,
    // never checked here on the startup path.
    m_previewFontFamily = FontPreferences::normalizedFamily(s.value(FontPreferences::PreviewKey).toString());
    m_tabs->setPreviewFontFamily(m_previewFontFamily);
    m_documentSpacing = DocumentSpacing::load([&s](QLatin1StringView key) { return s.value(QString(key)); });
    m_tabs->setDocumentSpacing(m_documentSpacing);

    {
        FindController::Options findOptions;
        findOptions.matchCase = s.value(QStringLiteral("find/matchCase"), false).toBool();
        findOptions.wholeWord = s.value(QStringLiteral("find/wholeWord"), false).toBool();
        findOptions.regex     = s.value(QStringLiteral("find/regex"), false).toBool();
        findOptions.preserveCase = s.value(QStringLiteral("find/preserveCase"), false).toBool();
        m_findController->setOptions(findOptions);
    }
    m_tabs->setCurrentLineHighlightEnabled(m_highlightCurrentLineEnabled);
    m_lastSaveFolder    = s.value(QStringLiteral("lastSaveFolder")).toString();
    m_lastOpenFolder    = s.value(QStringLiteral("lastOpenFolder")).toString();
    m_lastExportFolder  = s.value(QStringLiteral("lastExportFolder")).toString();

    m_workspacePanelCtrl->recentFoldersManager()->setFolders(
        s.value(QStringLiteral("workspace/recentFolders")).toStringList());
    m_workspacePanelCtrl->setExpandedPaths(s.value(QStringLiteral("workspace/expandedPaths")).toStringList());
    m_workspacePanelCtrl->setShowAll(s.value(QStringLiteral("workspace/showAll"), false).toBool(), false);
    m_workspacePanelCtrl->setOptionalColumnsVisible(
        s.value(QStringLiteral("workspace/fileTypeColumnVisible"), false).toBool(),
        s.value(QStringLiteral("workspace/dateModifiedColumnVisible"), false).toBool(),
        s.value(QStringLiteral("workspace/sizeColumnVisible"), false).toBool(), false);
    m_workspacePanelCtrl->setSort(
        s.value(QStringLiteral("workspace/sortColumn"), 2).toInt(),
        static_cast<Qt::SortOrder>(s.value(QStringLiteral("workspace/sortOrder"),
                                            static_cast<int>(Qt::AscendingOrder)).toInt()), false);
    m_workspacePanelCtrl->setColumnLayoutState(
        s.value(QStringLiteral("workspace/columnLayoutState")).toByteArray());
    m_workspacePanelCtrl->setOutlineSplitterState(
        s.value(QStringLiteral("workspace/outlineSplitterState")).toByteArray());
    m_workspacePanelCtrl->setOutlineExpanded(
        s.value(QStringLiteral("workspace/outlineExpanded"), false).toBool(), false);
    const QString workspaceRoot = s.value(QStringLiteral("workspace/rootPath")).toString();
    m_workspacePanelCtrl->setRoot(
        (!workspaceRoot.isEmpty() && QDir(workspaceRoot).exists())
            ? workspaceRoot
            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        false,
        s.value(QStringLiteral("workspace/showAll"), false).toBool());

    m_sessionTabs.clear();
    const QList<QVariantMap>& sessionTabs = s.sessionTabs();
    const int sessionCount = sessionTabs.size();
    m_sessionTabs.reserve(sessionCount);
    for (int i = 0; i < sessionCount; ++i) {
        const QVariantMap& savedTab = sessionTabs.at(i);
        SessionTabState state;
        state.path = savedTab.value(QStringLiteral("path")).toString();
        state.cursorPos = savedTab.value(QStringLiteral("cursorPos"), 0).toInt();
        state.editorScroll = savedTab.value(QStringLiteral("editorScroll"), 0).toInt();
        state.previewScroll = savedTab.value(QStringLiteral("previewScroll"), 0).toInt();
        state.mode = static_cast<EditorMode>(
            savedTab.value(QStringLiteral("mode"), static_cast<int>(EditorMode::Markdown)).toInt());
        state.previewVisible = savedTab.value(QStringLiteral("previewVisible"), true).toBool();
        state.presentation = static_cast<MarkdownPresentation>(
            savedTab.value(QStringLiteral("presentation"), static_cast<int>(MarkdownPresentation::Preview)).toInt());
        state.hasLanguageState = savedTab.contains(QStringLiteral("selectedDefinitionName"));
        if (state.hasLanguageState) {
            state.selectedDefinitionName = savedTab.value(QStringLiteral("selectedDefinitionName")).toString();
            state.automaticallyDetectedName = savedTab.value(QStringLiteral("automaticallyDetectedName")).toString();
            state.languageSource = static_cast<LanguageSelectionSource>(
                savedTab.value(QStringLiteral("languageSource"), static_cast<int>(LanguageSelectionSource::Automatic)).toInt());
            state.hasLastNonMarkdownSelection = savedTab.contains(QStringLiteral("lastNonMarkdownDefinitionName"));
            if (state.hasLastNonMarkdownSelection) {
                state.lastNonMarkdownDefinitionName =
                    savedTab.value(QStringLiteral("lastNonMarkdownDefinitionName")).toString();
                state.lastNonMarkdownSource = static_cast<LanguageSelectionSource>(
                    savedTab.value(QStringLiteral("lastNonMarkdownSource"),
                                   static_cast<int>(LanguageSelectionSource::Automatic)).toInt());
            }
        }
        if (!state.path.isEmpty())
            m_sessionTabs.append(state);
    }
    m_sessionActiveTab = s.value(QStringLiteral("session/activeTab"), -1).toInt();

    const RestoredTabSettings defaultTabSettings {
        s.value(QStringLiteral("splitterState")).toByteArray(),
        m_markdownPreviewVisible,
        m_wordWrapEnabled,
        m_defaultEditorFontSize
    };
    m_tabs->setDefaultTabSettings(defaultTabSettings, m_defaultNewDocMode);
    m_restoredTabSettings.clear();
    m_restoredTabSettings.reserve(m_sessionTabs.size());
    for (const SessionTabState& sessionTab : m_sessionTabs) {
        RestoredTabSettings tabSettings = defaultTabSettings;
        const QVariant cachedWrap = s.value(QStringLiteral("WordWrap/") + sessionTab.path);
        if (cachedWrap.isValid())
            tabSettings.wordWrapEnabled = cachedWrap.toBool();
        m_restoredTabSettings.insert(sessionTab.path, tabSettings);
    }

    m_pdfExportOptions = ExportManager::pdfExportOptionsFromSettings(
        [&s](const QString& key, const QVariant& defaultValue) { return s.value(key, defaultValue); });
    const QByteArray splitterState = s.value(QStringLiteral("workspaceSplitterState")).toByteArray();
    if (!splitterState.isEmpty() && m_mainSplitter) {
        m_mainSplitter->restoreState(splitterState);
        // restoreState() resets the handle width to whatever an older saved
        // session encoded (or the native style's default) — reassert it, same
        // as TabWidget::addNewTab() does for the editor/preview splitter.
        if (auto* splitter = dynamic_cast<ThemedSplitter*>(m_mainSplitter))
            splitter->reassertHandleWidth();
    }
}

RestoredTabSettings MainWindow::defaultTabSettingsForPath(const QString& path) const
{
    RestoredTabSettings settings {
        m_startupSettings.value(QStringLiteral("splitterState")).toByteArray(),
        m_markdownPreviewVisible,
        m_wordWrapEnabled,
        m_defaultEditorFontSize
    };
    if (!path.isEmpty()) {
        const auto currentWrap = m_wordWrapByPath.constFind(path);
        if (currentWrap != m_wordWrapByPath.cend()) {
            settings.wordWrapEnabled = currentWrap.value();
        } else if (!m_wordWrapUnsetPaths.contains(path)) {
            const QString key = QStringLiteral("WordWrap/") + path;
            QVariant savedWrap = m_startupSettings.value(key);
            if (!savedWrap.isValid()) {
                if (!m_lazyWordWrapSettings)
                    m_lazyWordWrapSettings = new QSettings(const_cast<MainWindow*>(this));
                savedWrap = m_lazyWordWrapSettings->value(key);
            }
            if (savedWrap.isValid()) {
                settings.wordWrapEnabled = savedWrap.toBool();
                m_wordWrapByPath.insert(path, settings.wordWrapEnabled);
            } else {
                // Negative result: remembered so repeated lookups (one per tab,
                // one per window per writeSettings()) never re-hit the registry.
                m_wordWrapUnsetPaths.insert(path);
            }
        }
    }
    return settings;
}

// ---------------------------------------------------------------------------
void MainWindow::scheduleWriteSettings()
{
    if (m_isResetting)
        return;
    if (!m_writeSettingsTimer) {
        m_writeSettingsTimer = new QTimer(this);
        m_writeSettingsTimer->setSingleShot(true);
        m_writeSettingsTimer->setInterval(500);
        connect(m_writeSettingsTimer, &QTimer::timeout, this, &MainWindow::writeSettings);
    }
    m_writeSettingsTimer->start();
}

// ---------------------------------------------------------------------------
void MainWindow::writeSettings()
{
    if (m_isResetting)
        return;
    if (m_writeSettingsTimer)
        m_writeSettingsTimer->stop();

    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("recentFiles"), m_recentFilesMgr->files());
    s.setValue(QStringLiteral("theme"), m_theme);
    s.setValue(QStringLiteral("previewVisible"), m_markdownPreviewVisible);
    s.setValue(QStringLiteral("wordWrapEnabled"), m_wordWrapEnabled);
    // Keep every window's in-memory defaults aligned with persisted settings.
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        if (auto* window = qobject_cast<MainWindow*>(topLevel); window && window->m_tabs)
            window->m_tabs->setDefaultTabSettings(window->defaultTabSettingsForPath(),
                                                  window->m_defaultNewDocMode);
    }
    // This is the user's Workspace choice, not Notepad Mode's temporary
    // suppression of the panel.
    s.setValue(QStringLiteral("workspaceTreeVisible"), m_workspaceTreeVisible);
    // terminal/visible is deliberately not part of this broad settings sweep.
    // It is the terminal feature's own persisted intent, and is written only
    // by setTerminalVisible() when the user actually shows or hides it.  The
    // sweep also runs for unrelated startup and window/session activity; using
    // a window-local snapshot here used to let that activity overwrite a
    // restored terminal preference before the deferred terminal restore ran.
    // Terminal sessions are runtime state.  A previous version persisted this
    // count and recreated that many shells after restart; remove the legacy
    // value while writing settings so upgrades cannot retain it.
    s.remove(QStringLiteral("terminal/sessionCount"));
    s.setValue(QStringLiteral("notepadMode"), m_notepadMode);
    s.setValue(QStringLiteral("statusBarVisible"), m_statusBarVisible);
    s.setValue(QStringLiteral("app/welcomeShown"), m_welcomeShown);
    if (!m_lastRunVersion.isEmpty())
        s.setValue(QStringLiteral("app/lastRunVersion"), m_lastRunVersion);
    s.setValue(QStringLiteral("mathToolbarVisible"), m_mathToolbarVisible);
    s.setValue(QStringLiteral("chemistryToolbarVisible"), m_chemistryToolbarVisible);
    s.setValue(QStringLiteral("restoreSessionOnStartup"), m_restoreSessionOnStartup);
    s.setValue(QStringLiteral("closeAppOnLastTabClose"), m_closeAppOnLastTabClose);
    s.setValue(QStringLiteral("autoSave/enabled"), m_autoSaveEnabled);
    s.setValue(QStringLiteral("autoSave/delayMs"), m_autoSaveDelayMs);
    s.setValue(QStringLiteral("autoSave/crashRecovery"), m_crashRecoveryEnabled);
    s.setValue(QStringLiteral("files/openPdfExternally"), m_openPdfExternally);
    s.setValue(QStringLiteral("files/openHtmlExternally"), m_openHtmlExternally);
    s.setValue(QStringLiteral("files/openImagesExternally"), m_openImagesExternally);
    s.setValue(QStringLiteral("export/showSuccessDialog"), m_showExportSuccessDialog);
    s.setValue(QStringLiteral("defaultSaveFolder"), m_defaultSaveFolder);
    s.setValue(QStringLiteral("defaultNewDocMode"), static_cast<int>(m_defaultNewDocMode));
    s.setValue(QStringLiteral("editor/highlightCurrentLine"), m_highlightCurrentLineEnabled);
    s.setValue(QStringLiteral("spellCheck/enabled"), m_spellCheckEnabled);
    s.setValue(QStringLiteral("spellCheck/language"), m_spellCheckLanguage);
    {
        const FindController::Options findOptions = m_findController->options();
        s.setValue(QStringLiteral("find/matchCase"), findOptions.matchCase);
        s.setValue(QStringLiteral("find/wholeWord"), findOptions.wholeWord);
        s.setValue(QStringLiteral("find/regex"), findOptions.regex);
        s.setValue(QStringLiteral("find/preserveCase"), findOptions.preserveCase);
    }
    s.setValue(QStringLiteral("lastSaveFolder"),    m_lastSaveFolder);
    s.setValue(QStringLiteral("lastOpenFolder"),    m_lastOpenFolder);
    s.setValue(QStringLiteral("lastExportFolder"),  m_lastExportFolder);
    s.setValue(QStringLiteral("workspace/rootPath"), m_workspacePanelCtrl->rootPath());
    s.setValue(QStringLiteral("workspace/recentFolders"), m_workspacePanelCtrl->recentFoldersManager()->folders());
    s.setValue(QStringLiteral("workspace/expandedPaths"), m_workspacePanelCtrl->expandedPaths());
    s.setValue(QStringLiteral("workspace/dateModifiedColumnVisible"),
               m_workspacePanelCtrl->dateModifiedColumnVisible());
    s.setValue(QStringLiteral("workspace/fileTypeColumnVisible"),
               m_workspacePanelCtrl->fileTypeColumnVisible());
    s.setValue(QStringLiteral("workspace/sizeColumnVisible"),
               m_workspacePanelCtrl->sizeColumnVisible());
    s.setValue(QStringLiteral("workspace/columnLayoutState"),
               m_workspacePanelCtrl->columnLayoutState());
    s.setValue(QStringLiteral("workspace/sortColumn"), m_workspacePanelCtrl->sortColumn());
    s.setValue(QStringLiteral("workspace/sortOrder"), static_cast<int>(m_workspacePanelCtrl->sortOrder()));
    s.setValue(QStringLiteral("workspace/outlineExpanded"), m_workspacePanelCtrl->outlineExpanded());
    // The controller caches only valid expanded Workspace geometry. Persist
    // that snapshot even if Workspace/Outline is now hidden or collapsed; do
    // not sample live geometry or erase a saved split before first reveal.
    const QByteArray outlineState = m_workspacePanelCtrl->outlineSplitterState();
    if (!outlineState.isEmpty())
        s.setValue(QStringLiteral("workspace/outlineSplitterState"), outlineState);
    if (m_mainSplitter && m_workspacePanelCtrl && m_workspacePanelCtrl->panelWidget()->isVisible())
        s.setValue(QStringLiteral("workspaceSplitterState"), m_mainSplitter->saveState());
    // Same "only while it is on screen" rule as the workspace splitter: closing
    // the panel already persists its height, but quitting with it open would
    // otherwise lose a height the user had just dragged.
    if (m_terminalPanel && m_terminalPanel->isVisible())
        persistDockedTerminalHeight();
    // The Source Control panel owns its own section/splitter keys; it has no
    // close event of its own now that it is docked.
    if (m_sourceControl)
        m_sourceControl->persistState();
    // The diff panes' split ratio is one shared preference, not per tab, so the
    // first open diff document speaks for all of them.
    for (int i = 0; i < m_tabs->count(); ++i) {
        TabPage* page = m_tabs->pageAt(i);
        if (page && page->diffView) {
            s.setValue(QStringLiteral("git/diffSplitter"), page->diffView->saveSplitterState());
            break;
        }
    }
    if (m_activeEditor) {
        int ps = m_activeEditor->font().pixelSize();
        if (ps > 0) {
            m_defaultEditorFontSize = ps;
            s.setValue(QStringLiteral("editorFontSize"), ps);
        }
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
    HeaderFooter::save(m_pdfExportOptions.headerFooter,
                       [&s](const QString& key, const QVariant& value) { s.setValue(key, value); });

    s.remove(QStringLiteral("session/tabs"));
    s.beginWriteArray(QStringLiteral("session/tabs"));
    int sessionIndex = 0;
    int activeIndex = -1;
    const QWidget* activeWindow = QApplication::activeWindow();
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<MainWindow*>(topLevel);
        if (!window || !window->m_tabs)
            continue;
        for (int i = 0; i < window->m_tabs->count(); ++i) {
            TabPage* pg = window->m_tabs->pageAt(i);
            if (!pg || pg->model->filePath().isEmpty())
                continue;

            const SessionTabState state = window->captureTabState(pg);
            s.setArrayIndex(sessionIndex);
            s.setValue(QStringLiteral("path"), state.path);
            s.setValue(QStringLiteral("cursorPos"), state.cursorPos);
            s.setValue(QStringLiteral("editorScroll"), state.editorScroll);
            s.setValue(QStringLiteral("previewScroll"), state.previewScroll);
            s.setValue(QStringLiteral("mode"), static_cast<int>(state.mode));
            s.setValue(QStringLiteral("previewVisible"), state.previewVisible);
            s.setValue(QStringLiteral("presentation"), static_cast<int>(state.presentation));
            s.setValue(QStringLiteral("selectedDefinitionName"), state.selectedDefinitionName);
            s.setValue(QStringLiteral("automaticallyDetectedName"), state.automaticallyDetectedName);
            s.setValue(QStringLiteral("languageSource"), static_cast<int>(state.languageSource));
            if (state.hasLastNonMarkdownSelection) {
                s.setValue(QStringLiteral("lastNonMarkdownDefinitionName"), state.lastNonMarkdownDefinitionName);
                s.setValue(QStringLiteral("lastNonMarkdownSource"), static_cast<int>(state.lastNonMarkdownSource));
            }
            if (window == activeWindow && i == window->m_tabs->currentIndex())
                activeIndex = sessionIndex;
            ++sessionIndex;
        }
    }
    s.endArray();
    s.setValue(QStringLiteral("session/activeTab"), activeIndex);

}

// Pruning remembered per-path word-wrap choices is maintenance, not part of
// persisting the current state: it used to run inside every writeSettings() and
// stat *every* remembered path (the old cap bounded removals, not stat calls,
// so 500 remembered paths meant 500 synchronous stats per write, and a stale
// UNC or mapped-drive path could block for seconds). It now runs as one
// low-priority startup task per session. The number of stats is bounded, the
// persisted starting offset eventually covers the whole set, and the filesystem
// checks run on a worker so a stale UNC path cannot freeze the post-paint UI.
void MainWindow::pruneWordWrapSettings()
{
    if (m_isResetting)
        return;

    QSettings s;
    const QString offsetKey = QStringLiteral("maintenance/wordWrapPruneOffset");
    const int savedOffset = s.value(offsetKey, 0).toInt();
    s.beginGroup(QStringLiteral("WordWrap"));
    const QStringList wordWrapKeys = s.allKeys();
    s.endGroup();
    if (wordWrapKeys.isEmpty())
        return;

    constexpr int kMaxWordWrapStatsPerPrune = 64;
    const int total = wordWrapKeys.size();
    const int checks = std::min(total, kMaxWordWrapStatsPerPrune);
    const int offset = qBound(0, savedOffset, total - 1);
    QStringList pathsToCheck;
    pathsToCheck.reserve(checks);
    for (int i = 0; i < checks; ++i) {
        pathsToCheck.append(wordWrapKeys.at((offset + i) % total));
    }
    s.setValue(offsetKey, (offset + checks) % total);

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [watcher]() {
                const QStringList missingPaths = watcher->result();
                watcher->deleteLater();
                if (missingPaths.isEmpty())
                    return;
                QSettings settings;
                for (const QString& path : missingPaths)
                    settings.remove(QStringLiteral("WordWrap/") + path);
            });
    watcher->setFuture(QtConcurrent::run([pathsToCheck]() {
        QStringList missingPaths;
        for (const QString& path : pathsToCheck) {
            if (!QFileInfo(path).exists())
                missingPaths.append(path);
        }
        return missingPaths;
    }));
}
