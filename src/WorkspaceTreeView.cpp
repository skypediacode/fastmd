#include "WorkspaceTreeView.h"
#include "DarkTheme.h"
#include "ThemePalette.h"
#include "IconHelper.h"
#include "FastScrollBar.h"

#include <QWheelEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QCursor>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeySequence>
#include <QUrl>
#include <QProxyStyle>
#include <QPainter>
#include <QStyleOption>
#include <QFontMetrics>

namespace {

// Replaces the platform style's default expand/collapse branch decoration
// (which varies by style/theme and can be inconsistent or barely visible)
// with a flat Material-icon chevron matching Explorer: a right-pointing
// chevron when collapsed, a down-pointing one when expanded, nothing at all
// for leaf rows (no dotted tree lines). Doesn't need Q_OBJECT (no new
// signals/slots), so it lives entirely in this .cpp with no moc involved.
class WorkspaceBranchStyle : public QProxyStyle {
public:
    void setColorProvider(std::function<QColor()> provider) { m_colorProvider = std::move(provider); }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                        QPainter* painter, const QWidget* widget) const override
    {
        if (element == QStyle::PE_PanelItemViewItem || element == QStyle::PE_PanelItemViewRow)
            return;

        if (element != QStyle::PE_IndicatorBranch) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        if (!(option->state & QStyle::State_Children))
            return;

        const bool open = option->state & QStyle::State_Open;
        const QColor color = m_colorProvider ? m_colorProvider() : QColor(0x44, 0x44, 0x44);

        const QFontMetrics rowMetrics(widget ? widget->font() : QFont());
        QFont font(IconHelper::materialFontFamily());
        font.setPixelSize(qMax(1, static_cast<int>(rowMetrics.height() * 0.9)));

        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        painter->setFont(font);
        painter->setPen(color);
        const QRect glyphRect = option->rect.adjusted(0, 0, 4, 0);
        painter->drawText(glyphRect, Qt::AlignRight | Qt::AlignVCenter,
                           open ? QString(QChar(0xE5CF)) : QString(QChar(0xE5CC)));
        painter->restore();
    }

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const override
    {
        if (element == QStyle::CE_ItemViewItem) {
            if (auto* opt = qstyleoption_cast<const QStyleOptionViewItem*>(option)) {
                QStyleOptionViewItem subOpt = *opt;
                subOpt.state &= ~QStyle::State_HasFocus;
                subOpt.state &= ~QStyle::State_Selected;
                QProxyStyle::drawControl(element, &subOpt, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

private:
    std::function<QColor()> m_colorProvider;
};

}

WorkspaceTreeView::WorkspaceTreeView(QWidget* parent)
    : QTreeView(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setAcceptDrops(true);

    auto* branchStyle = new WorkspaceBranchStyle();
    branchStyle->setParent(this);
    m_branchStyle = branchStyle;
    setStyle(m_branchStyle);

    // Qt's style sheets are documented as unsupported for widgets using a
    // custom QStyle subclass (see QWidget::setStyle()'s docs), and that
    // extends to this view's own scrollbars: with m_branchStyle installed,
    // Qt's CSS engine never polishes this subtree, so the scrollbars would
    // render with fully native chrome no matter what Stylesheet.h says.
    // Sidestep the CSS engine entirely, the same way MarkdownEditor's
    // scrollbars already do: FastScrollBar paints its own rounded thumb
    // directly instead of going through style()/QSS.
    setVerticalScrollBar(new FastScrollBar(Qt::Vertical, this));
    setHorizontalScrollBar(new FastScrollBar(Qt::Horizontal, this));
}

void WorkspaceTreeView::paintEvent(QPaintEvent* event)
{
    // indexAt() walks the view's layout, so resolving the hovered row inside
    // drawRow() would repeat that work for every visible row. The cursor
    // cannot move within a single repaint, so resolve it once here instead.
    m_hoveredRow = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
    QTreeView::paintEvent(event);
}

QColor WorkspaceTreeView::selectionForeground() const
{
    return ThemePalette::color(ThemePalette::Text, m_darkTheme);
}

void WorkspaceTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    const bool selected = selectionModel() && selectionModel()->isRowSelected(index.row(), index.parent());

    const bool hovered = m_hoveredRow.isValid() &&
                         m_hoveredRow.row() == index.row() &&
                         m_hoveredRow.parent() == index.parent();

    // One continuous surface across the visible viewport, through the Explorer
    // overlay area; the branch and icon remain content painted on top of it.
    ThemeCard::paintRow(*painter, QRect(0, option.rect.top(), viewport()->width(), option.rect.height()),
                        m_darkTheme, selected, hovered, option.state & QStyle::State_Active);

    // The QSS item backgrounds are transparent; this call paints only the
    // branch/icon/text content over the controlled highlight above.
    QTreeView::drawRow(painter, option, index);
}

void WorkspaceTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                     const QModelIndex& index) const
{
    if (index.isValid() && model()) {
        const bool hasCustomVisibility = static_cast<bool>(m_branchVisibilityProvider);
        if (hasCustomVisibility && !m_branchVisibilityProvider(index))
            return;

        // QFileSystemModel can report State_Children before its directory
        // listing has finished, and can keep doing so for directories whose
        // entries are all filtered out. Only retain the chevron while there
        // are visible rows or the model still has work to fetch.
        const bool hasVisibleChildren = model()->rowCount(index) > 0;
        const bool stillLoading = model()->canFetchMore(index);
        if (!hasCustomVisibility && !hasVisibleChildren && !stillLoading)
            return;
    }
    QTreeView::drawBranches(painter, rect, index);
}

void WorkspaceTreeView::setBranchVisibilityProvider(
    std::function<bool(const QModelIndex&)> provider)
{
    m_branchVisibilityProvider = std::move(provider);
    viewport()->update();
}

void WorkspaceTreeView::setDarkTheme(bool dark)
{
    if (m_darkTheme == dark)
        return;
    m_darkTheme = dark;
    viewport()->update();
}

void WorkspaceTreeView::setBranchIconColorProvider(std::function<QColor()> provider)
{
    static_cast<WorkspaceBranchStyle*>(m_branchStyle)->setColorProvider(std::move(provider));
}

void WorkspaceTreeView::wheelEvent(QWheelEvent* event)
{
    if ((event->modifiers() & Qt::ShiftModifier) && horizontalScrollBar()->isVisible()) {
        const int delta = event->pixelDelta().y() != 0
            ? event->pixelDelta().y()
            : event->angleDelta().y() / 3;
        if (delta != 0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
            event->accept();
            return;
        }
    }
    QTreeView::wheelEvent(event);
}

void WorkspaceTreeView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        emit pasteRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QModelIndex idx = currentIndex();
        if (idx.isValid() && model()) {
            if (model()->hasChildren(idx) || model()->canFetchMore(idx)) {
                setExpanded(idx, !isExpanded(idx));
            }

            // QTreeView does not reliably emit activated() for Enter after
            // this view intercepts the key. The controller opens files and
            // deliberately ignores directories after the toggle above.
            emit activated(idx);
            event->accept();
            return;
        }
    }
    QTreeView::keyPressEvent(event);
}

void WorkspaceTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = event->reason() == QContextMenuEvent::Keyboard
        ? currentIndex() : indexAt(event->pos());
    emit contextMenuRequested(event->globalPos(), index);
}

void WorkspaceTreeView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else if (event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void WorkspaceTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->source() == this) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else if (event->mimeData()->hasUrls()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void WorkspaceTreeView::dropEvent(QDropEvent* event)
{
    if (event->source() == this) {
        emit internalDropRequested(indexAt(event->position().toPoint()));
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (!event->mimeData()->hasUrls())
        return;

    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    if (paths.isEmpty())
        return;

    emit externalPathsDropped(paths, indexAt(event->position().toPoint()));
    event->setDropAction(Qt::CopyAction);
    event->accept();
}
