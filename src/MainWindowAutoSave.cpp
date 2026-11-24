#include "MainWindow.h"
#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "RecoveryManager.h"
#include "DocumentModel.h"
#include "MainWindowTextIO.h"

#include <QTimer>
#include <QThread>
#include <QSaveFile>
#include <memory>

// ---------------------------------------------------------------------------
void MainWindow::scheduleAutoSave(DocumentModel* model)
{
    if (!model || m_autoSaveSuspended.contains(model)
        || (!m_autoSaveEnabled && !m_crashRecoveryEnabled) || !model->isDirty()
        || model->isBundled())
        return;
    QTimer* timer = m_autoSaveTimers.value(model);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, model]() { onAutoSaveTimeout(model); });
        m_autoSaveTimers.insert(model, timer);
    }
    timer->start(m_autoSaveDelayMs);
}

// ---------------------------------------------------------------------------
void MainWindow::onAutoSaveTimeout(DocumentModel* model)
{
    if (!model || m_autoSaveSuspended.contains(model)
        || !m_editRevisions.contains(model) || !model->isDirty()
        || m_savingDocuments.contains(model))
        return;
    TabPage* page = nullptr;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model == model) {
            page = m_tabs->pageAt(i);
            break;
        }
    }
    if (!page)
        return;
    const quint64 revision = m_editRevisions.value(model);
    // Auto Save must write exactly what is in the editor. Replacing or
    // normalizing the buffer as part of saving can invalidate its undo stack
    // and can make the on-disk text differ from what the user is editing.
    const QString text = page->editor->toPlainText();
    if (m_crashRecoveryEnabled)
        writeRecoverySnapshot(model, text, revision);
    if (model->filePath().isEmpty() || !m_autoSaveEnabled)
        return;
    // Never let Auto Save silently overwrite a file that changed externally
    // while this tab has unsaved edits; the conflict dialog resumes Auto
    // Save once the user picks Reload or Keep My Version.
    if (model->hasExternalConflict())
        return;
    autoSaveDocument(model, text, revision);
}

// ---------------------------------------------------------------------------
void MainWindow::writeRecoverySnapshot(DocumentModel* model, const QString& text, quint64 revision)
{
    Q_UNUSED(revision);
    RecoverySnapshot snapshot;
    snapshot.key = RecoveryManager::keyFor(model->filePath(), model->recoveryId());
    snapshot.path = model->filePath();
    snapshot.content = text;
    snapshot.lineEnding = model->lineEnding();
    snapshot.encoding = model->encoding();
    snapshot.mode = model->editorMode();
    snapshot.displayName = model->displayName();
    snapshot.selectedDefinitionName = model->selectedDefinitionName();
    snapshot.automaticallyDetectedName = model->automaticallyDetectedName();
    snapshot.languageSource = model->languageSelectionSource();
    snapshot.hasLastNonMarkdownSelection = model->hasLastNonMarkdownSelection();
    if (snapshot.hasLastNonMarkdownSelection) {
        const NonMarkdownLanguageSelection last = model->lastNonMarkdownSelection();
        snapshot.lastNonMarkdownDefinitionName = last.definitionName;
        snapshot.lastNonMarkdownSource = last.source;
    }
    QThread* thread = QThread::create([snapshot]() { RecoveryManager::write(snapshot); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, model, revision]() {
        if (m_editRevisions.contains(model) && m_editRevisions.value(model) == revision
            && !model->isDirty())
            cleanupRecoveryFor(model);
    });
    thread->start();
}

// ---------------------------------------------------------------------------
void MainWindow::autoSaveDocument(DocumentModel* model, const QString& text, quint64 revision)
{
    const QString path = model->filePath();
    const QByteArray bytes = encodeWithLineEnding(text, model->lineEnding(), model->encoding());
    const auto saveState = model->saveState();
    const quint64 saveTicket = DocumentSaveCoordinator::beginWrite(saveState);
    m_ignoredFileChanges.arm(path, saveTicket);
    m_savingDocuments.insert(model);
    auto result = std::make_shared<DocumentSaveCoordinator::WriteResult>(
        DocumentSaveCoordinator::WriteResult::Failed);
    QThread* thread = QThread::create([path, bytes, saveState, saveTicket, result]() {
        *result = DocumentSaveCoordinator::writeLatestInBackground(
            path, bytes, saveState, saveTicket);
    });
    // Self-delete independently of the completion handler below: that handler is
    // dropped if this window is destroyed while the write is still in flight.
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this,
            [this, model, revision, path, saveTicket, bytes, result]() {
        m_savingDocuments.remove(model);
        if (*result != DocumentSaveCoordinator::WriteResult::Success)
            m_ignoredFileChanges.clear(path, saveTicket);
        if (!m_editRevisions.contains(model))
            return;
        if (*result == DocumentSaveCoordinator::WriteResult::Success) {
            rememberFileContents(path, bytes);
        }
        if (*result == DocumentSaveCoordinator::WriteResult::Success
            && m_editRevisions.value(model) == revision) {
            model->setDirty(false);
            for (int i = 0; i < m_tabs->count(); ++i) {
                if (m_tabs->pageAt(i) && m_tabs->pageAt(i)->model == model) {
                    m_tabs->pageAt(i)->editor->document()->setModified(false);
                    m_tabs->refreshTitle(i);
                    break;
                }
            }
            cleanupRecoveryFor(model);
        } else if (model->isDirty()) {
            scheduleAutoSave(model);
        }
    });
    thread->start();
}

// ---------------------------------------------------------------------------
void MainWindow::cleanupRecoveryFor(DocumentModel* model)
{
    if (model)
        RecoveryManager::remove(RecoveryManager::keyFor(model->filePath(), model->recoveryId()));
}
