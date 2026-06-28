#include "DocumentModel.h"
#include <QFileInfo>

DocumentModel::DocumentModel(int untitledIndex)
    : m_untitledIndex(untitledIndex)
{}

QString DocumentModel::displayName() const
{
    if (m_filePath.isEmpty())
        return QStringLiteral("Untitled %1").arg(m_untitledIndex);
    return QFileInfo(m_filePath).fileName();
}

EditorMode DocumentModel::modeForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("txt"))
        return EditorMode::PlainText;
    return EditorMode::Markdown;
}
