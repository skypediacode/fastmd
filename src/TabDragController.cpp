#include "TabDragController.h"

#include "MainWindow.h"
#include "TabWidget.h"
#include "DocumentModel.h"
#include "MarkdownEditor.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QTabBar>
#include <QTimer>

namespace {
class DragPreview final : public QWidget {
public:
    DragPreview(TabPage* page, const QSize& size, QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(size);
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowOpacity(0.92);
        m_title = page->model->displayName();
        if (page->model->isDirty())
            m_title.prepend(QStringLiteral("* "));
        switch (page->model->editorMode()) {
        case EditorMode::Markdown: m_mode = tr("Markdown"); break;
        case EditorMode::Pdf:      m_mode = tr("PDF"); break;
        case EditorMode::Html:     m_mode = tr("HTML"); break;
        case EditorMode::Image:    m_mode = tr("Image"); break;
        case EditorMode::Svg:      m_mode = tr("SVG"); break;
        default:                   m_mode = tr("Code/Text"); break;
        }
        m_excerpt = page->editor->toPlainText().split(QLatin1Char('\n')).mid(0, 8).join(QLatin1Char('\n'));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor bg = dark ? QColor(0x12, 0x13, 0x14) : QColor(250, 250, 250);
        const QColor header = dark ? QColor(0x19, 0x1a, 0x1b) : QColor(240, 240, 240);
        const QColor border = dark ? QColor(0x3a, 0x3b, 0x3c) : QColor(180, 180, 180);
        p.setBrush(bg);
        p.setPen(QPen(border, 1));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(header);
        p.drawRoundedRect(QRect(1, 1, width() - 2, 44), 8, 8);
        p.drawRect(1, 23, width() - 2, 22);
        p.setPen(palette().color(QPalette::Text));
        QFont titleFont = font();
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.drawText(QRect(16, 7, width() - 115, 30), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        QFont modeFont = font();
        modeFont.setPointSizeF(qMax(7.0, modeFont.pointSizeF() - 1.0));
        p.setFont(modeFont);
        p.drawText(QRect(width() - 88, 7, 72, 30), Qt::AlignVCenter | Qt::AlignRight, m_mode);
        p.setPen(dark ? QColor(205, 205, 205) : QColor(68, 68, 68));
        QFont body = font();
        body.setFamily(QStringLiteral("Consolas"));
        body.setPointSizeF(qMax(8.0, body.pointSizeF()));
        p.setFont(body);
        p.drawText(QRect(16, 58, width() - 32, height() - 72), Qt::TextWordWrap | Qt::AlignTop, m_excerpt);
    }
private:
    QString m_title;
    QString m_mode;
    QString m_excerpt;
};
}

TabDragController* TabDragController::instance()
{
    static TabDragController* controller = new TabDragController(qApp);
    return controller;
}

TabDragController::TabDragController(QObject* parent) : QObject(parent) {}

void TabDragController::cancelForWindow(MainWindow* window)
{
    if (window && (m_sourceWindow == window || m_destinationTabs && m_destinationTabs->window() == window))
        cancelDrag();
}

void TabDragController::beginDrag(MainWindow* source, TabWidget* tabs, TabPage* page,
                                  const QPoint& globalPress, const QPoint& globalPosition)
{
    if (m_state != State::Idle || !source || !tabs || !page)
        return;
    m_sourceWindow = source;
    m_sourceTabs = tabs;
    m_page = page;
    m_originalIndex = tabs->indexOf(page);
    if (m_originalIndex < 0)
        return;
    const QRect tabRect = tabs->tabBar()->tabRect(m_originalIndex);
    m_dragOffset = globalPress - tabs->tabBar()->mapToGlobal(tabRect.topLeft());
    m_dragOffset.setX(qBound(12, m_dragOffset.x(), 200));
    m_dragOffset.setY(qBound(8, m_dragOffset.y(), 32));
    QScreen* screen = QGuiApplication::screenAt(globalPosition);
    const QRect available = screen ? screen->availableGeometry() : QRect(globalPosition, QSize(1200, 750));
    const QSize previewSize(qBound(420, source->width() / 2, qMin(620, available.width() - 24)),
                            qBound(260, source->height() / 2, qMin(420, available.height() - 24)));
    m_preview = new DragPreview(page, previewSize);
    m_sourceTabs->tabBar()->grabMouse(Qt::ClosedHandCursor);
    qApp->installEventFilter(this);
    updateDrag(globalPosition);
}

bool TabDragController::eventFilter(QObject*, QEvent* event)
{
    if (m_state == State::Idle)
        return false;
    if (event->type() == QEvent::MouseMove) {
        updateDrag(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        finishDrag(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        cancelDrag();
        return true;
    }
    return false;
}

TabWidget* TabDragController::tabWidgetAt(const QPoint& globalPosition, MainWindow** window) const
{
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* candidate = qobject_cast<MainWindow*>(topLevel);
        if (!candidate || !candidate->isVisible())
            continue;
        TabWidget* tabs = candidate->tabWidget();
        if (tabs && tabs->tabBar()->rect().contains(tabs->tabBar()->mapFromGlobal(globalPosition))) {
            if (window)
                *window = candidate;
            return tabs;
        }
    }
    if (window)
        *window = nullptr;
    return nullptr;
}

int TabDragController::insertionIndex(TabWidget* tabs, const QPoint& globalPosition) const
{
    QTabBar* bar = tabs->tabBar();
    const QPoint pos = bar->mapFromGlobal(globalPosition);
    int index = bar->tabAt(pos);
    if (index < 0)
        return tabs->count();
    return pos.x() > bar->tabRect(index).center().x() ? index + 1 : index;
}

void TabDragController::updateDrag(const QPoint& globalPosition)
{
    if (!m_sourceWindow || !m_sourceTabs || !m_page) {
        cancelDrag();
        return;
    }
    MainWindow* destinationWindow = nullptr;
    TabWidget* hit = tabWidgetAt(globalPosition, &destinationWindow);
    if (hit && hit != m_sourceTabs) {
        m_state = State::HoveringDestination;
        m_destinationTabs = hit;
        m_destinationIndex = insertionIndex(hit, globalPosition);
        m_sourceTabs->setDragVisual(m_page, -1, false);
        hit->setDragVisual(m_page, m_destinationIndex, true);
        m_preview->hide();
        if (m_hoverWindow != destinationWindow && m_raisedWindow != destinationWindow) {
            m_hoverWindow = destinationWindow;
            m_hoverTimer.restart();
        } else if (m_hoverWindow == destinationWindow && m_hoverTimer.elapsed() >= 250) {
            destinationWindow->raise();
            destinationWindow->activateWindow();
            m_raisedWindow = destinationWindow;
            m_hoverWindow = nullptr;
        }
        return;
    }

    const QRect sourceRect(m_sourceTabs->tabBar()->mapToGlobal(QPoint()), m_sourceTabs->tabBar()->size());
    const QRect leaveRect = sourceRect;
    const QRect returnRect = sourceRect.adjusted(-24, -20, 24, 20);
    const bool inSourceZone = hit == m_sourceTabs
        || (m_sourceDetached ? returnRect.contains(globalPosition) : leaveRect.contains(globalPosition));
    m_destinationTabs.clear();
    m_hoverWindow.clear();
    m_raisedWindow.clear();
    if (inSourceZone) {
        m_sourceDetached = false;
        m_state = State::ReorderingInSource;
        const int insert = insertionIndex(m_sourceTabs, globalPosition);
        const int current = m_sourceTabs->indexOf(m_page);
        int target = insert;
        if (target > current)
            --target;
        target = qBound(0, target, m_sourceTabs->count() - 1);
        m_sourceTabs->moveTabPage(current, target);
        m_sourceTabs->setDragVisual(m_page, insertionIndex(m_sourceTabs, globalPosition), false);
        if (m_preview)
            m_preview->hide();
        return;
    }

    m_sourceDetached = true;
    m_state = State::Detaching;
    m_sourceTabs->setDragVisual(m_page, -1, false);
    if (m_preview) {
        m_preview->move(globalPosition - m_dragOffset);
        m_preview->show();
    }
}

void TabDragController::finishDrag(const QPoint& globalPosition)
{
    // Snapshot the session, then tear it down *before* any tab surgery. This
    // runs inside the application event filter, with the mouse still grabbed by
    // the source tab bar, and the transfer can empty and destroy that very
    // window. Releasing the grab and uninstalling the filter first leaves the
    // drag owning nothing inside the doomed window, and the now-Idle state stops
    // events raised during the transfer from re-entering the drag machinery.
    const State state = m_state;
    const QPointer<MainWindow> sourceWindow = m_sourceWindow;
    const QPointer<TabWidget> destinationTabs = m_destinationTabs;
    TabPage* const page = m_page;
    const int destinationIndex = m_destinationIndex;
    const QPoint dragOffset = m_dragOffset;
    clearVisuals();

    QPointer<MainWindow> activateTarget;
    if (state == State::HoveringDestination && destinationTabs) {
        auto* target = qobject_cast<MainWindow*>(destinationTabs->window());
        if (target && sourceWindow) {
            sourceWindow->transferTabTo(target, page, destinationIndex);
            activateTarget = target;
        }
    } else if (state == State::Detaching && sourceWindow) {
        QScreen* screen = QGuiApplication::screenAt(globalPosition);
        const QRect available = screen ? screen->availableGeometry() : QRect(globalPosition, QSize(1200, 750));
        QSize sourceSize = sourceWindow->isMaximized()
            ? sourceWindow->normalGeometry().size() : sourceWindow->size();
        if (!sourceSize.isValid())
            sourceSize = QSize(1200, 750);
        // A dragged tab should become a useful working window, not inherit a
        // maximized source window's full available-screen geometry.
        const QSize maximum(qMax(800, available.width() * 85 / 100),
                            qMax(500, available.height() * 85 / 100));
        const QSize size(qMin(sourceSize.width(), qMin(maximum.width(), available.width())),
                         qMin(sourceSize.height(), qMin(maximum.height(), available.height())));
        QPoint topLeft = globalPosition - dragOffset;
        topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - size.width() + 1));
        topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - size.height() + 1));
        MainWindow* target = MainWindow::createAuxiliaryWindow(MainWindow::StartupMode::TransferTarget,
                                                                 QRect(topLeft, size));
        sourceWindow->transferTabTo(target, page, 0);
        activateTarget = target;
    }

    // Releasing the source tab bar's mouse grab can cause Windows to restore
    // focus to that original top-level window. Activate only after the grab
    // and drag preview are gone, on the next event-loop turn, so a detached
    // tab reliably leaves its new window foregrounded and ready for typing.
    if (activateTarget) {
        QTimer::singleShot(0, activateTarget, [activateTarget]() {
            if (!activateTarget)
                return;
            activateTarget->raise();
            activateTarget->activateWindow();
            if (MarkdownEditor* editor = activateTarget->tabWidget()->currentEditor())
                editor->restoreKeyboardFocus();
        });
    }
}

void TabDragController::cancelDrag()
{
    if (m_sourceTabs && m_page) {
        const int current = m_sourceTabs->indexOf(m_page);
        if (current >= 0)
            m_sourceTabs->moveTabPage(current, m_originalIndex);
        m_sourceTabs->setCurrentIndex(m_originalIndex);
    }
    clearVisuals();
}

void TabDragController::clearVisuals()
{
    if (m_sourceTabs)
        m_sourceTabs->clearDragVisual();
    if (m_destinationTabs)
        m_destinationTabs->clearDragVisual();
    if (m_sourceTabs && m_sourceTabs->tabBar()->mouseGrabber())
        m_sourceTabs->tabBar()->releaseMouse();
    qApp->removeEventFilter(this);
    if (m_preview) {
        m_preview->hide();
        m_preview->deleteLater();
        m_preview = nullptr;
    }
    m_state = State::Idle;
    m_sourceWindow.clear();
    m_sourceTabs.clear();
    m_destinationTabs.clear();
    m_hoverWindow.clear();
    m_page = nullptr;
    m_originalIndex = -1;
    m_destinationIndex = -1;
    m_sourceDetached = false;
}
