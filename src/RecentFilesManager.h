#pragma once

#include <QObject>
#include <QStringList>

class AlignedMenu;

// Owns the recently-opened-files list and the "Open Recent" submenu that
// displays it, including Windows Jump List integration. MainWindow owns the
// instance and reacts to fileOpenRequested()/changed() to open files and
// persist the list via QSettings.
class RecentFilesManager : public QObject {
    Q_OBJECT
public:
    explicit RecentFilesManager(AlignedMenu* menu, QObject* parent = nullptr);

    QStringList files() const { return m_files; }
    void setFiles(const QStringList& files);

    // Moves (or inserts) path to the front of the list, trims to the max
    // size, rebuilds the menu, and registers the path with the Windows
    // Jump List.
    void add(const QString& path);

    // Renames an existing entry in place (no reordering), used when a tab is
    // renamed/saved-as before add() is also called with the new path.
    void rename(const QString& oldPath, const QString& newPath);

    void clear();

    AlignedMenu* menu() const { return m_menu; }

signals:
    // Emitted when the user picks an entry from the menu.
    void fileOpenRequested(const QString& path);
    // Emitted whenever the list changes, so MainWindow can persist it.
    void changed();

private:
    void rebuildMenu();

    AlignedMenu* m_menu;
    QStringList  m_files;
};
