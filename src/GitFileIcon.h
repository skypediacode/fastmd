#pragma once

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>

// Shared Windows file-icon resolver for every Git file row. Git paths are
// repository-relative, so resolve them before asking QFileIconProvider just
// as the Workspace panel does for its absolute search results.
inline QIcon gitFileIconForPath(const QString& path, const QString& root)
{
    static QFileIconProvider provider;
    static QHash<QString, QIcon> cache;

    const QFileInfo fileInfo(QDir(root).filePath(path));
    const QString suffix = fileInfo.suffix().toLower();
    const auto it = cache.constFind(suffix);
    if (it != cache.constEnd())
        return it.value();

    // One shell lookup per distinct suffix instead of one per Git row.
    const QIcon icon = provider.icon(fileInfo);
    cache.insert(suffix, icon);
    return icon;
}
