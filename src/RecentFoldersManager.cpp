#include "RecentFoldersManager.h"
#include "AlignedMenu.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>

#include <algorithm>

static const int MAX_RECENT_FOLDERS = 12;

RecentFoldersManager::RecentFoldersManager(AlignedMenu* menu, QObject* parent)
    : QObject(parent), m_menu(menu)
{
    connect(m_menu, &QMenu::aboutToShow, this, [this]() { validateFolders(); });
    rebuildMenu();
}

void RecentFoldersManager::setFolders(const QStringList& folders)
{
    m_folders.clear();
    for (const QString& p : folders) {
        const QFileInfo info(p);
        const QString cleanPath = QDir::cleanPath(info.absoluteFilePath());
        if (!std::any_of(m_folders.cbegin(), m_folders.cend(), [&cleanPath](const QString& existing) {
                return existing.compare(cleanPath, Qt::CaseInsensitive) == 0;
            })) {
            m_folders.append(cleanPath);
        }
        if (m_folders.size() == MAX_RECENT_FOLDERS)
            break;
    }
    rebuildMenu();
}

void RecentFoldersManager::validateFolders()
{
    const auto previous = m_folders;
    m_folders.erase(std::remove_if(m_folders.begin(), m_folders.end(), [](const QString& path) {
        const QFileInfo info(path);
        return !info.isDir() || !info.isReadable();
    }), m_folders.end());
    if (m_folders == previous)
        return;
    rebuildMenu();
    emit changed();
}

void RecentFoldersManager::add(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isDir() || !info.isReadable())
        return;
    const QString cleanPath = QDir::cleanPath(info.absoluteFilePath());
    m_folders.erase(std::remove_if(m_folders.begin(), m_folders.end(), [&cleanPath](const QString& existing) {
        return existing.compare(cleanPath, Qt::CaseInsensitive) == 0;
    }), m_folders.end());
    m_folders.prepend(cleanPath);
    while (m_folders.size() > MAX_RECENT_FOLDERS)
        m_folders.removeLast();
    rebuildMenu();
    emit changed();
}

void RecentFoldersManager::clear()
{
    m_folders.clear();
    rebuildMenu();
    emit changed();
}

void RecentFoldersManager::rebuildMenu()
{
    m_menu->clear();
    for (const QString& p : m_folders) {
        QAction* a = m_menu->addAction(p, this, [this, p]() { emit folderOpenRequested(p); });
        a->setData(p);
    }
    if (m_folders.isEmpty()) {
        m_menu->addAction(tr("(empty)"))->setEnabled(false);
    } else {
        m_menu->addSeparator();
        m_menu->addAction(tr("Clear Recent Folders"), this, &RecentFoldersManager::clear);
    }
}
