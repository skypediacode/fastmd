#include "PreviewWidget.h"
#include "ExportManager.h"
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileInfo>
#include <QUrl>
#include <QToolButton>
#include <QResizeEvent>
#include "IconHelper.h"
#include <QPropertyAnimation>
#include <QAbstractScrollArea>
#include <QScrollBar>

OutlineEdgeButton::OutlineEdgeButton(QWidget* parent)
    : QToolButton(parent), m_anim(new QPropertyAnimation(this, "expandWidth", this))
{
    setIcon(IconHelper::materialIconOutlined(QChar(0xE8EF), QColor(0x77, 0x77, 0x77), 32)); // format_list_bulleted outlined
    setText(tr("Outline"));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setCursor(Qt::PointingHandCursor);
    
    // Narrow edge handle styling
    setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  border: 1px solid #cccccc;"
        "  border-radius: 6px;"
        "  background: rgba(255,255,255,0.9);"
        "  padding: 4px 10px;"
        "  color: #333333;"
        "  font-size: 16px;"
        "}"
        "QToolButton:hover { background: rgba(240,240,240,1.0); }"
    ));
    setIconSize(QSize(24, 24));
    QFont f = font();
    f.setPointSize(11);
    f.setWeight(QFont::DemiBold);
    setFont(f);

    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    setFixedWidth(40);
    setFixedHeight(40);
}

void OutlineEdgeButton::setPopupOpen(bool open)
{
    m_popupOpen = open;
    updateState();
}

void OutlineEdgeButton::setExpandWidth(int w)
{
    m_expandWidth = w;
    setFixedWidth(w);
    updatePosition();
}

void OutlineEdgeButton::updatePosition()
{
    if (auto* parentArea = qobject_cast<QAbstractScrollArea*>(parentWidget())) {
        int scrollBarWidth = parentArea->verticalScrollBar()->isVisible() ? parentArea->verticalScrollBar()->width() : 0;
        int padding = 16;
        move(parentArea->width() - width() - scrollBarWidth - padding, padding);
    }
}

void OutlineEdgeButton::updateState()
{
    bool expanded = m_popupOpen || underMouse();
    int targetWidth = expanded ? 110 : 44;
    
    m_anim->stop();
    m_anim->setStartValue(m_expandWidth);
    m_anim->setEndValue(targetWidth);
    m_anim->start();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void OutlineEdgeButton::enterEvent(QEnterEvent* event)
#else
void OutlineEdgeButton::enterEvent(QEvent* event)
#endif
{
    QToolButton::enterEvent(event);
    updateState();
}

void OutlineEdgeButton::leaveEvent(QEvent* event)
{
    QToolButton::leaveEvent(event);
    updateState();
}


PreviewWidget::PreviewWidget(QWidget* parent)
    : QTextBrowser(parent)
{
    setOpenExternalLinks(true);
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);

    m_outlineBtn = new OutlineEdgeButton(this);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QTextBrowser::resizeEvent(event);
    if (m_outlineBtn) {
        m_outlineBtn->updatePosition();
    }
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
    if (m_outlineBtn) {
        if (m_dark) {
            m_outlineBtn->setIcon(IconHelper::materialIcon(QChar(0xE8EF), QColor(0xaa, 0xaa, 0xaa), 24));
            m_outlineBtn->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid #444444; border-radius: 4px; background: rgba(45,45,45,0.8); }"
                "QToolButton:hover { background: rgba(65,65,65,0.9); }"
            ));
        } else {
            m_outlineBtn->setIcon(IconHelper::materialIcon(QChar(0xE8EF), QColor(0x77, 0x77, 0x77), 24));
            m_outlineBtn->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid #cccccc; border-radius: 4px; background: rgba(255,255,255,0.8); }"
                "QToolButton:hover { background: rgba(230,230,230,0.9); }"
            ));
        }
    }
    refresh();
}

void PreviewWidget::refresh()
{
    int pos = verticalScrollBar()->value();
    m_refreshing = true;
    setHtml(ExportManager::buildFullHtml(m_bodyHtml, m_dark, m_baseFontSize, false, false, true));
    verticalScrollBar()->setValue(pos);
    m_refreshing = false;
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
