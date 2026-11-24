#include "RecentFilesManager.h"
#include "AlignedMenu.h"

#include <QAction>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <appmodel.h>
#include <QDir>
#include <string>
#endif

static const int MAX_RECENT = 20;

RecentFilesManager::RecentFilesManager(AlignedMenu* menu, QObject* parent)
    : QObject(parent), m_menu(menu)
{
    rebuildMenu();
}

void RecentFilesManager::setFiles(const QStringList& files)
{
    m_files = files;
    rebuildMenu();
}

void RecentFilesManager::add(const QString& path)
{
    m_files.removeAll(path);
    m_files.prepend(path);
    while (m_files.size() > MAX_RECENT)
        m_files.removeLast();
    rebuildMenu();
    emit changed();

#ifdef Q_OS_WIN
    // The taskbar Jump List (and the icon it renders) is keyed by the process'
    // AppUserModelID. It must match what main() decided:
    //   - Packaged (MSIX): the OS assigns the package AUMID, which already owns
    //     icon assets from the manifest. We must NOT override it with a custom
    //     string AppID here - doing so files the recent docs under an
    //     unregistered AUMID that has no icon, which is why the app icon shows
    //     up blank in the Jump List. Use SHARD_SHELLITEM so the shell attaches
    //     the entry to the running process' real (package) AUMID.
    //   - Unpackaged: main() pins an explicit "Skypedia.FastMD" AUMID (kept in
    //     sync with the shortcut), so we register under the same AppID.
    static const bool isPackaged = [] {
        UINT32 len = 0;
        return GetCurrentPackageFullName(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    }();

    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    IShellItem* shellItem = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(
            nativePath.c_str(), nullptr, IID_PPV_ARGS(&shellItem)))) {
        if (isPackaged) {
            SHAddToRecentDocs(SHARD_SHELLITEM, shellItem);
        } else {
            SHARDAPPIDINFO recentInfo{};
            recentInfo.psi = shellItem;
            recentInfo.pszAppID = L"Skypedia.FastMD";
            SHAddToRecentDocs(SHARD_APPIDINFO, &recentInfo);
        }
        shellItem->Release();
    } else {
        SHAddToRecentDocs(SHARD_PATHW, reinterpret_cast<const void*>(nativePath.c_str()));
    }
#endif
}

void RecentFilesManager::rename(const QString& oldPath, const QString& newPath)
{
    const int idx = m_files.indexOf(oldPath);
    if (idx >= 0) {
        m_files[idx] = newPath;
        rebuildMenu();
        emit changed();
    }
}

void RecentFilesManager::clear()
{
    m_files.clear();
    rebuildMenu();
    emit changed();
}

void RecentFilesManager::rebuildMenu()
{
    m_menu->clear();
    for (const QString& p : m_files) {
        QAction* a = m_menu->addAction(p, this, [this, p]() { emit fileOpenRequested(p); });
        a->setData(p);
    }
    if (m_files.isEmpty()) {
        m_menu->addAction(tr("(empty)"))->setEnabled(false);
    } else {
        m_menu->addSeparator();
        m_menu->addAction(tr("Clear Recent Files"), this, &RecentFilesManager::clear);
    }
}
