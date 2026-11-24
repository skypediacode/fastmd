#include "DocumentModel.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace {

QHash<QString, DocumentModel*>& openDocuments()
{
    static QHash<QString, DocumentModel*> documents;
    return documents;
}

}

DocumentModel::DocumentModel(int untitledIndex)
    : m_untitledIndex(untitledIndex)
{}

DocumentModel::~DocumentModel()
{
    if (!m_pathKey.isEmpty() && openDocuments().value(m_pathKey) == this)
        openDocuments().remove(m_pathKey);
}

void DocumentModel::setFilePath(const QString& path)
{
    if (m_filePath == path)
        return;

    const QString previousKey = m_pathKey;
    m_filePath = path;
    m_pathKey = pathKeyFor(path);
    updatePathRegistry(previousKey);
}

void DocumentModel::setSelectedDefinitionName(const QString& name)
{
    m_selectedDefinitionName = name;
    if (!isViewerMode(m_mode))
        m_mode = (name == QStringLiteral("FastMD Markdown")) ? EditorMode::Markdown : EditorMode::PlainText;
}

DocumentModel* DocumentModel::openDocumentForKey(const QString& pathKey)
{
    return openDocuments().value(pathKey, nullptr);
}

void DocumentModel::updatePathRegistry(const QString& previousKey)
{
    auto& documents = openDocuments();
    if (!previousKey.isEmpty() && documents.value(previousKey) == this)
        documents.remove(previousKey);
    if (!m_pathKey.isEmpty())
        documents.insert(m_pathKey, this);
}

QString DocumentModel::displayName() const
{
    if (m_filePath.isEmpty()) {
        if (!m_displayNameOverride.isEmpty())
            return m_displayNameOverride;
        return QStringLiteral("Untitled %1").arg(m_untitledIndex);
    }
    return QFileInfo(m_filePath).fileName();
}

QString DocumentModel::pathKeyFor(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();
    return QDir::cleanPath(canonical).toCaseFolded();
}

EditorMode DocumentModel::modeForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("pdf"))
        return EditorMode::Pdf;
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm"))
        return EditorMode::Html;
    // SVG can embed arbitrary HTML/CSS (via foreignObject) and script, which a
    // native raster viewer can never render correctly -- WebView2 renders it
    // exactly like Chrome does, so it gets its own viewer mode rather than
    // falling into EditorMode::Image below.
    if (suffix == QStringLiteral("svg"))
        return EditorMode::Svg;
    if (ImageFormats::isImageExtension(suffix))
        return EditorMode::Image;
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")
        || suffix == QStringLiteral("mdown") || suffix == QStringLiteral("mkd"))
        return EditorMode::Markdown;
    return EditorMode::PlainText;
}
