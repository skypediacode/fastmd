#include "PreviewWidget.h"
#include "ExportManager.h"
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileInfo>
#include <QUrl>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QTextBrowser(parent)
{
    setOpenExternalLinks(true);
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
}

void PreviewWidget::setBodyHtml(const QString& html)
{
    m_bodyHtml = html;
    refresh();
}

void PreviewWidget::setBasePath(const QString& path)
{
    if (m_basePath == path)
        return;

    m_basePath = path;
    if (!path.isEmpty()) {
        QFileInfo fi(path);
        setSearchPaths(QStringList() << fi.absolutePath());
    } else {
        setSearchPaths(QStringList());
    }
}

void PreviewWidget::setDarkMode(bool dark)
{
    m_dark = dark;
    refresh();
}

void PreviewWidget::refresh()
{
    int pos = verticalScrollBar()->value();
    setHtml(ExportManager::buildFullHtml(m_bodyHtml, m_dark, m_baseFontSize));
    verticalScrollBar()->setValue(pos);
}

void PreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta > 0) {
            emit zoomRequested(1);
        } else if (delta < 0) {
            emit zoomRequested(-1);
        }
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

void PreviewWidget::zoomIn(int range)
{
    m_baseFontSize += range;
    refresh();
}

void PreviewWidget::zoomOut(int range)
{
    m_baseFontSize -= range;
    if (m_baseFontSize < FastMdDefaults::MinimumPreviewFontSize) {
        m_baseFontSize = FastMdDefaults::MinimumPreviewFontSize;
    }
    refresh();
}
