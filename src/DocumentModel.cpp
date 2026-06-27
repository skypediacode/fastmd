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
