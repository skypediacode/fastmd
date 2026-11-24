#pragma once

#include <QObject>
#include <QStringList>

class AlignedMenu;

// Owns the recently-opened-workspace-folders list and the "Recent Folders"
// submenu that displays it. Mirrors RecentFilesManager but for directories,
// with no Windows Jump List integration. MainWindow/WorkspacePanelController
// react to folderOpenRequested()/changed() to switch the workspace root and
// persist the list via QSettings.
class RecentFoldersManager : public QObject {
    Q_OBJECT
public:
    explicit RecentFoldersManager(AlignedMenu* menu, QObject* parent = nullptr);

    QStringList folders() const { return m_folders; }
    // Stores normalized paths without touching the filesystem. Stale paths are
    // pruned when the user opens the menu.
    void setFolders(const QStringList& folders);

    // Moves (or inserts) path to the front of the list, trims to the max
    // size, and rebuilds the menu.
    void add(const QString& path);

    void clear();

    AlignedMenu* menu() const { return m_menu; }

signals:
    // Emitted when the user picks an entry from the menu.
    void folderOpenRequested(const QString& path);
    // Emitted whenever the list changes, so MainWindow can persist it.
    void changed();

private:
    void rebuildMenu();
    void validateFolders();

    AlignedMenu* m_menu;
    QStringList  m_folders;
};
