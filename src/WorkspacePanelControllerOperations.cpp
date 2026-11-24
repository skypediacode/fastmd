#include "WorkspacePanelController.h"
#include "WorkspaceFileOps.h"
#include "WorkspaceNavigationModel.h"
#include "WorkspaceTreeFilterModel.h"
#include "WorkspaceTreeView.h"
#include "IconHelper.h"
#include "AlignedMenu.h"
#include "MenuCommandBar.h"
#include "ThemedMessageBox.h"
#include "DocumentModel.h"
#include "SyntaxDefinitionResolver.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QModelIndex>
#include <QMessageBox>
#include <QMenu>
#include <QKeySequence>
#include "CommandShortcutRegistry.h"
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace
{
    struct CopyResult {
        QString lastCopied;
        QStringList failed;
    };
    constexpr ushort kIconCollapseAll = 0xE5D6;
    constexpr ushort kIconExpandAll = 0xE5D7;
    constexpr ushort kIconFileOpen = 0xEAF3;
    constexpr ushort kIconWorkspaceRoot = 0xEA9B;
    constexpr ushort kIconRename = 0xE9A2;
    constexpr ushort kIconCopy = 0xE14D;
    constexpr ushort kIconReveal = 0xE895;
    constexpr ushort kIconTerminal = 0xEB8E;
    constexpr ushort kIconNewFile = 0xE89C;
    constexpr ushort kIconNewTextFile = 0xE873;
    constexpr ushort kIconNewFolder = 0xE2CC;

    bool opensInternally(const QString& path)
    {
        const EditorMode mode = DocumentModel::modeForPath(path);
        return isViewerMode(mode) || SyntaxDefinitionResolver::isLikelyEditableTextFile(path);
    }

    // "Open" already loads the file as editable source text unless it is a
    // Pdf/Html/Image viewer file or an unsupported/binary file that falls
    // back to the OS default application -- only those cases benefit from
    // the "Open as Source Code" escape hatch.
    bool opensAsSourceAlready(const QString& path)
    {
        return !isViewerMode(DocumentModel::modeForPath(path))
            && SyntaxDefinitionResolver::isLikelyEditableTextFile(path);
    }

    bool defaultApplicationIsFastMd(const QString& path)
    {
#ifdef Q_OS_WIN
        const QString extension = QStringLiteral(".") + QFileInfo(path).suffix().toLower();
        if (extension == QStringLiteral("."))
            return false;

        const QString userChoiceKey = QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\")
            + extension + QStringLiteral("\\UserChoice");
        QSettings userChoice(userChoiceKey, QSettings::NativeFormat);
        if (userChoice.value(QStringLiteral("ProgId")).toString().compare(
                QStringLiteral("FastMD.Document"), Qt::CaseInsensitive) == 0) {
            return true;
        }

        QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\") + extension,
                          QSettings::NativeFormat);
        return classes.value(QString()).toString().compare(
            QStringLiteral("FastMD.Document"), Qt::CaseInsensitive) == 0;
#else
        Q_UNUSED(path);
        return false;
#endif
    }
}

QStringList WorkspacePanelController::selectedPaths() const
{
    QStringList paths;
    if (!m_workspaceTree || !m_workspaceModel || !m_navigationModel)
        return paths;
    const QModelIndexList selected = m_workspaceTree->selectionModel()
                                         ? m_workspaceTree->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    for (const QModelIndex &proxyIndex : selected)
    {
        if (m_navigationModel->isParentEntry(proxyIndex))
            continue;
        const QModelIndex srcIndex = sourceIndexForView(proxyIndex);
        if (srcIndex.isValid())
            paths << m_workspaceModel->filePath(srcIndex);
    }
    return paths;
}

QString WorkspacePanelController::targetDirForNewItem() const
{
    const QStringList selected = selectedPaths();
    if (!selected.isEmpty())
    {
        const QFileInfo info(selected.first());
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
    return m_workspaceRootPath;
}

void WorkspacePanelController::beginInlineEditFor(const QString &path, int retriesLeft)
{
    // New File is available before the startup tree build has finished. Editing
    // waits for the next turn: the first load would close it again.
    if (m_workspaceTree && !m_workspaceModel)
    {
        ensureModel();
        updateRoot();
        QTimer::singleShot(20, this, [this, path, retriesLeft]()
                           { beginInlineEditFor(path, retriesLeft); });
        return;
    }
    if (!m_workspaceModel || !m_navigationModel || path.isEmpty())
        return;
    const QModelIndex srcIndex = m_workspaceModel->index(path);
    const QModelIndex proxyIndex = srcIndex.isValid() ? viewIndexForSource(srcIndex) : QModelIndex();
    if (!proxyIndex.isValid())
    {
        if (retriesLeft > 0)
        {
            QTimer::singleShot(20, this, [this, path, retriesLeft]()
                               { beginInlineEditFor(path, retriesLeft - 1); });
        }
        return;
    }
    if (proxyIndex.parent().isValid())
        m_workspaceTree->expand(proxyIndex.parent());
    m_workspaceTree->setCurrentIndex(proxyIndex);
    m_workspaceTree->scrollTo(proxyIndex);
    m_workspaceTree->edit(proxyIndex);
}

void WorkspacePanelController::revealPath(const QString &path, int retriesLeft)
{
    if (!m_workspaceModel || !m_navigationModel || path.isEmpty())
        return;

    const QModelIndex srcIndex = m_workspaceModel->index(path);
    const QModelIndex proxyIndex = srcIndex.isValid() ? viewIndexForSource(srcIndex) : QModelIndex();
    if (!proxyIndex.isValid())
    {
        m_pendingRevealPath = path;
        if (retriesLeft > 0)
        {
            QTimer::singleShot(20, this, [this, path, retriesLeft]()
                               { revealPath(path, retriesLeft - 1); });
        }
        return;
    }

    m_pendingRevealPath.clear();
    for (QModelIndex ancestor = proxyIndex.parent(); ancestor.isValid(); ancestor = ancestor.parent())
        m_workspaceTree->expand(ancestor);
    if (m_workspaceTree->selectionModel())
        m_workspaceTree->selectionModel()->setCurrentIndex(
            proxyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    else
        m_workspaceTree->setCurrentIndex(proxyIndex);
    m_workspaceTree->scrollTo(proxyIndex);
    m_workspaceTree->setFocus(Qt::OtherFocusReason);
}

void WorkspacePanelController::createNewFile()
{
    const QString created = WorkspaceFileOps::createFile(targetDirForNewItem());
    if (!created.isEmpty())
        beginInlineEditFor(created);
}

void WorkspacePanelController::createNewTextFile()
{
    const QString created = WorkspaceFileOps::createFile(targetDirForNewItem(), QStringLiteral("Untitled.txt"));
    if (!created.isEmpty())
        beginInlineEditFor(created);
}

void WorkspacePanelController::createNewFolder()
{
    const QString created = WorkspaceFileOps::createFolder(targetDirForNewItem());
    if (!created.isEmpty())
        beginInlineEditFor(created);
}

void WorkspacePanelController::beginRenameSelected()
{
    const QStringList selected = selectedPaths();
    if (selected.size() == 1)
        beginInlineEditFor(selected.first());
}

void WorkspacePanelController::duplicateSelected()
{
    const QStringList selected = selectedPaths();
    if (selected.size() != 1)
        return;

    // copyPathsAsync() is also the non-cut paste path, so Duplicate shares
    // its collision handling, background transfer, refresh, selection, and
    // error reporting for both files and folders.
    const QString& sourcePath = selected.first();
    copyPathsAsync({sourcePath}, QFileInfo(sourcePath).absolutePath(), tr("Duplicate"));
}

void WorkspacePanelController::deleteSelected()
{
    const QStringList selected = selectedPaths();
    if (selected.isEmpty())
        return;

    const QString message = selected.size() == 1
                                ? tr("Move \"%1\" to the Recycle Bin?").arg(QFileInfo(selected.first()).fileName())
                                : tr("Move %1 items to the Recycle Bin?").arg(selected.size());
    const auto answer = themedQuestion(m_workspacePanel, tr("Delete"), message,
                                       QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No), QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    QStringList failed;
    QStringList deleted;
    for (const QString &path : selected)
    {
        if (!WorkspaceFileOps::deletePath(path))
            failed << QFileInfo(path).fileName();
        else
            deleted << path;
    }
    if (!deleted.isEmpty())
        emit pathsDeleted(deleted);
    if (!failed.isEmpty())
    {
        themedWarning(m_workspacePanel, tr("Delete"),
                      tr("These items could not be deleted:\n%1").arg(failed.join(QStringLiteral("\n"))));
    }
}

void WorkspacePanelController::copySelected(bool cut)
{
    const QStringList selected = selectedPaths();
    if (selected.isEmpty())
        return;
    m_clipboardPaths = selected;
    m_clipboardIsCut = cut;

    auto *mimeData = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(selected.size());
    for (const QString &path : selected)
        urls.append(QUrl::fromLocalFile(path));
    mimeData->setUrls(urls);
    m_settingSystemClipboard = true;
    QApplication::clipboard()->setMimeData(mimeData);
    m_settingSystemClipboard = false;
}

void WorkspacePanelController::copySelection()
{
    copySelected(false);
}

void WorkspacePanelController::pasteSelection()
{
    // Files already paste beside themselves because targetDirForNewItem()
    // returns their parent. A selected folder normally denotes a paste target,
    // except when it is itself the only Workspace item on the clipboard: that
    // Ctrl+C / Ctrl+V sequence means duplicate the folder beside itself.
    const QStringList selected = selectedPaths();
    if (selected.size() == 1 && m_clipboardPaths.size() == 1 &&
        QFileInfo(selected.first()).isDir() &&
        QFileInfo(selected.first()).absoluteFilePath().compare(
            QFileInfo(m_clipboardPaths.first()).absoluteFilePath(),
            Qt::CaseInsensitive) == 0) {
        pasteIntoTarget(QFileInfo(selected.first()).absolutePath());
        return;
    }
    pasteIntoTarget(targetDirForNewItem());
}

void WorkspacePanelController::pasteIntoTarget(const QString &targetDir)
{
    if (targetDir.isEmpty())
        return;

    QStringList paths = m_clipboardPaths;
    bool isCut = m_clipboardIsCut;
    if (paths.isEmpty())
    {
        const QMimeData *mimeData = QApplication::clipboard()->mimeData();
        if (mimeData && mimeData->hasUrls())
            for (const QUrl &url : mimeData->urls())
                if (url.isLocalFile())
                    paths.append(url.toLocalFile());
        isCut = false;
    }
    if (paths.isEmpty())
        return;

    if (!isCut) {
        copyPathsAsync(paths, targetDir, tr("Paste"));
        return;
    }

    QStringList pastedPaths;
    QStringList failed;
    for (const QString &sourcePath : paths)
    {
        const QString pastedPath = WorkspaceFileOps::moveToDirectory(sourcePath, targetDir);
        if (pastedPath.isEmpty())
            failed << QFileInfo(sourcePath).fileName();
        else
            pastedPaths.append(pastedPath);
    }

    if (isCut)
    {
        m_clipboardPaths.clear();
        m_clipboardIsCut = false;
    }
    if (!pastedPaths.isEmpty())
    {
        ensureModel();
        updateRoot();
        revealPath(pastedPaths.constLast());
    }
    reportFailedTransfers(isCut ? tr("Move") : tr("Paste"), failed);
}

void WorkspacePanelController::importExternalPaths(const QStringList &sourcePaths, const QString &destDir)
{
    if (destDir.isEmpty() || sourcePaths.isEmpty())
        return;

    copyPathsAsync(sourcePaths, destDir, tr("Copy"));
}

void WorkspacePanelController::copyPathsAsync(const QStringList& sourcePaths,
                                              const QString& destDir,
                                              const QString& failureTitle)
{
    if (m_transferRunning)
        return;
    m_transferRunning = true;
    m_transferCancel = std::make_shared<WorkspaceFileOps::WorkspaceCopyCancellation>();
    const auto cancel = m_transferCancel;
    auto* watcher = new QFutureWatcher<CopyResult>(this);
    connect(watcher, &QFutureWatcher<CopyResult>::finished, this,
            [this, watcher, failureTitle] {
        const CopyResult result = watcher->result();
        watcher->deleteLater();
        m_transferRunning = false;
        m_transferCancel.reset();

        if (!result.lastCopied.isEmpty()) {
            ensureModel();
            updateRoot();
            revealPath(result.lastCopied);
        }
        reportFailedTransfers(failureTitle, result.failed);
    });
    watcher->setFuture(QtConcurrent::run([sourcePaths, destDir, cancel] {
        CopyResult result;
        for (const QString& sourcePath : sourcePaths) {
            if (cancel->isCancelled())
                break;
            const QString copied = WorkspaceFileOps::copyToDirectory(
                sourcePath, destDir, cancel.get());
            if (copied.isEmpty())
                result.failed << QFileInfo(sourcePath).fileName();
            else
                result.lastCopied = copied;
        }
        return result;
    }));
}

void WorkspacePanelController::reportFailedTransfers(const QString &title, const QStringList &failedNames)
{
    if (failedNames.isEmpty())
        return;
    themedWarning(m_workspacePanel, title,
                  tr("These items could not be copied or moved:\n%1")
                      .arg(failedNames.join(QStringLiteral("\n"))));
}

void WorkspacePanelController::openInExplorer(const QString &path)
{
    WorkspaceFileOps::revealInExplorer(path, QFileInfo(path).isDir());
}

void WorkspacePanelController::onItemRenamed(const QString &oldPath, const QString &newPath)
{
    const QString oldPrefix = oldPath + QLatin1Char('/');
    for (QString &path : m_workspaceExpandedPaths)
    {
        if (path == oldPath)
            path = newPath;
        else if (path.startsWith(oldPrefix))
            path = newPath + path.mid(oldPath.length());
    }
    // QFileSystemModel observes the external filesystem rename asynchronously.
    // Defer until the delegate has closed its editor, then let revealPath()
    // resolve the new model index now or retry after the refresh.
    m_pendingRevealPath = newPath;
    QTimer::singleShot(0, this, [this, newPath]() { revealPath(newPath); });
    emit fileRenamed(oldPath, newPath);
}

void WorkspacePanelController::showContextMenu(const QPoint &globalPos, const QModelIndex &proxyIndex)
{
    if (!m_workspaceModel || !m_navigationModel)
        return;
    if (m_navigationModel->isParentEntry(proxyIndex))
        return;

    // Right-clicking a row that isn't already part of the selection selects
    // just that row first (matches Explorer). Keep the clicked row current
    // even when it belongs to a multi-selection, so later keyboard actions
    // follow that row.
    if (proxyIndex.isValid() && m_workspaceTree->selectionModel() &&
        !m_workspaceTree->selectionModel()->isSelected(proxyIndex))
    {
        m_workspaceTree->selectionModel()->select(
            proxyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    if (proxyIndex.isValid() && m_workspaceTree->selectionModel())
        m_workspaceTree->selectionModel()->setCurrentIndex(
            proxyIndex, QItemSelectionModel::NoUpdate);

    const QStringList selected = selectedPaths();
    const QColor color = iconColor();
    AlignedMenu menu(m_workspacePanel);
    menu.setObjectName(QStringLiteral("workspaceContextMenu"));
    menu.setMinimumWidth(220);
    menu.setCompactShortcutLayout(true);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    // Clipboard/selection commands live in the top command bar only, so these
    // are parented to the menu (deleted with it) but never added to it.
    QAction *actCut = new QAction(tr("Cut"), &menu);
    actCut->setShortcut(QKeySequence::Cut);
    actCut->setEnabled(!selected.isEmpty());
    connect(actCut, &QAction::triggered, this, [this]()
            { copySelected(true); });

    QAction *actCopy = new QAction(tr("Copy"), &menu);
    actCopy->setShortcut(QKeySequence::Copy);
    actCopy->setEnabled(!selected.isEmpty());
    connect(actCopy, &QAction::triggered, this, [this]()
            { copySelected(false); });

    QAction *actPaste = new QAction(tr("Paste"), &menu);
    actPaste->setShortcut(QKeySequence::Paste);
    const QMimeData *clipboardData = QApplication::clipboard()->mimeData();
    actPaste->setEnabled(!m_clipboardPaths.isEmpty() ||
                         (clipboardData && clipboardData->hasUrls()));
    connect(actPaste, &QAction::triggered, this, &WorkspacePanelController::pasteSelection);

    QAction *actSelectAll = new QAction(tr("Select All"), &menu);
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    actSelectAll->setEnabled(m_workspaceTree->model() &&
                             m_workspaceTree->model()->rowCount(m_workspaceTree->rootIndex()) > 0);
    connect(actSelectAll, &QAction::triggered, this, [this]()
            { m_workspaceTree->setFocus(); m_workspaceTree->selectAll(); });

    QAction *actDelete = new QAction(tr("Delete"), &menu);
    actDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    actDelete->setEnabled(!selected.isEmpty());
    connect(actDelete, &QAction::triggered, this, &WorkspacePanelController::deleteSelected);

    MenuCommandBar *commandBar = MenuCommandBar::create(&menu);
    commandBar->setIconColor(color);
    commandBar->setCommands(actCut, actCopy, actPaste, actSelectAll, actDelete);
    commandBar->attachTo(&menu);

    if (!selected.isEmpty())
    {
        const bool openFolder = selected.size() == 1 && QFileInfo(selected.first()).isDir();
        bool folderExpanded = false;
        if (openFolder)
        {
            const QModelIndex srcIndex = m_workspaceModel->index(selected.first());
            const QModelIndex folderIndex = srcIndex.isValid() ? viewIndexForSource(srcIndex) : QModelIndex();
            folderExpanded = folderIndex.isValid() && m_workspaceTree->isExpanded(folderIndex);
        }
        QAction *actOpen = menu.addAction(
            IconHelper::materialMenuIcon(
                QChar(openFolder
                          ? (folderExpanded ? kIconCollapseAll : kIconExpandAll)
                          : kIconFileOpen),
                color),
            openFolder ? (folderExpanded ? tr("Fold") : tr("Unfold")) : tr("Open"));
        connect(actOpen, &QAction::triggered, this, [this, selected]()
                {
            if (selected.size() == 1 && QFileInfo(selected.first()).isDir()) {
                const QModelIndex srcIndex = m_workspaceModel->index(selected.first());
                const QModelIndex proxyIndex = srcIndex.isValid() ? viewIndexForSource(srcIndex) : QModelIndex();
                if (proxyIndex.isValid())
                    m_workspaceTree->setExpanded(proxyIndex, !m_workspaceTree->isExpanded(proxyIndex));
                return;
            }
            for (const QString& path : selected) {
                if (!QFileInfo(path).isDir())
                    emit fileActivated(path);
            } });

        if (selected.size() == 1 && !QFileInfo(selected.first()).isDir()
            && !opensAsSourceAlready(selected.first())) {
            QAction* actOpenAsSource = menu.addAction(
                IconHelper::materialMenuIcon(QChar(kIconFileOpen), color), tr("Open as Source Code"));
            connect(actOpenAsSource, &QAction::triggered, this, [this, path = selected.first()]() {
                emit textEditorRequested(path);
            });
        }

        if (selected.size() == 1 && opensInternally(selected.first())
            && !defaultApplicationIsFastMd(selected.first())) {
            QAction* actExternal = menu.addAction(
                IconHelper::materialMenuIcon(QChar(kIconFileOpen), color),
                tr("Open with Default Application"));
            connect(actExternal, &QAction::triggered, this, [this, path = selected.first()]() {
                emit externalViewerRequested(path);
            });
        }

        if (selected.size() == 1 && QFileInfo(selected.first()).isDir())
        {
            const QString dirPath = selected.first();
            const QString canonicalSelected = QDir(dirPath).canonicalPath();
            const QString canonicalRoot = QDir(m_workspaceRootPath).canonicalPath();
            if (!canonicalSelected.isEmpty() && canonicalSelected.compare(canonicalRoot, Qt::CaseInsensitive) != 0)
            {
                QAction *actSetRoot = menu.addAction(
                    IconHelper::materialMenuIcon(QChar(kIconWorkspaceRoot), color),
                    tr("Set as Workspace Root"));
                connect(actSetRoot, &QAction::triggered, this, [this, dirPath]()
                        { setRoot(dirPath); });
            }
        }
        menu.addSeparator();
    }

    QAction *actRename = menu.addAction(IconHelper::materialMenuIcon(QChar(kIconRename), color), tr("Rename"));
    actRename->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::WorkspaceRename));
    actRename->setEnabled(selected.size() == 1);
    connect(actRename, &QAction::triggered, this, &WorkspacePanelController::beginRenameSelected);

    QAction *actDuplicate = menu.addAction(
        IconHelper::materialMenuIcon(QChar(kIconCopy), color), tr("Duplicate"));
    actDuplicate->setEnabled(selected.size() == 1);
    connect(actDuplicate, &QAction::triggered, this, &WorkspacePanelController::duplicateSelected);

    menu.addSeparator();

    QMenu *newMenu = menu.addMenu(IconHelper::materialMenuIcon(QChar(kIconNewFile), color), tr("New"));
    newMenu->setObjectName(QStringLiteral("workspaceContextMenu"));

    QAction *actNewFile = newMenu->addAction(
        IconHelper::materialMenuIcon(QChar(kIconNewFile), color), tr("Markdown File"));
    connect(actNewFile, &QAction::triggered, this, &WorkspacePanelController::createNewFile);

    QAction *actNewTextFile = newMenu->addAction(
        IconHelper::materialMenuIcon(QChar(kIconNewTextFile), color), tr("Text File"));
    connect(actNewTextFile, &QAction::triggered, this, &WorkspacePanelController::createNewTextFile);

    QAction *actNewFolder = newMenu->addAction(
        IconHelper::materialMenuIcon(QChar(kIconNewFolder), color), tr("Folder"));
    connect(actNewFolder, &QAction::triggered, this, &WorkspacePanelController::createNewFolder);

    menu.addSeparator();

    const QModelIndex itemIndex = sourceIndexForView(proxyIndex);
    const QString itemPath = itemIndex.isValid() ? m_workspaceModel->filePath(itemIndex) : QString();
    QAction *actCopyPath = menu.addAction(IconHelper::materialMenuIcon(QChar(kIconCopy), color), tr("Copy Path"));
    actCopyPath->setEnabled(!itemPath.isEmpty());
    connect(actCopyPath, &QAction::triggered, this, [itemPath]()
            { QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(itemPath).absoluteFilePath())); });

    QAction *actReveal = menu.addAction(IconHelper::materialMenuIcon(QChar(kIconReveal), color), tr("Reveal in Explorer"));
    actReveal->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::FileRevealInExplorer));
    actReveal->setEnabled(!itemPath.isEmpty());
    connect(actReveal, &QAction::triggered, this, [this, itemPath]()
            { openInExplorer(itemPath); });


    QAction *actTerminal = menu.addAction(IconHelper::materialMenuIcon(QChar(kIconTerminal), color), tr("Open Folder in Terminal"));
    actTerminal->setEnabled(!itemPath.isEmpty());
    connect(actTerminal, &QAction::triggered, this, [this, itemPath]() {
        const QFileInfo info(itemPath);
        emit terminalRequested(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    });

    menu.exec(globalPos);
}
