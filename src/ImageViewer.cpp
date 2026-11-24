#include "ImageViewer.h"
#include "DarkTheme.h"
#include "AlignedMenu.h"
#include "ImageViewerToolbar.h"
#include "WorkspaceFileOps.h"
#include "ThemedMessageBox.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsOpacityEffect>
#include <QImageReader>
#include <QImage>
#include <QColor>
#include <QLabel>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QTimer>
#include <QPropertyAnimation>
#include <QPrintDialog>
#include <QPrinter>
#include <QPainter>

#include <cmath>

namespace {
constexpr qint64 kMaxPixels = 80'000'000;   // ~80 MP decode budget
constexpr int    kMaxEdge   = 12000;        // downsample target for oversized images
constexpr double kMinZoom   = 0.05;
constexpr double kMaxZoom   = 20.0;

constexpr int kToolbarBottomInset = 18;
constexpr int kToolbarIdleTimeout = 2000;   // ms of pointer inactivity before fading out
constexpr int kToolbarFadeIn      = 110;
constexpr int kToolbarFadeOut     = 180;
}

ImageViewer::ImageViewer(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFrameShape(QFrame::NoFrame);
    setObjectName(QStringLiteral("imageViewer"));
    setDragMode(QGraphicsView::NoDrag);
}

// Built on first reveal, not in the constructor. TabWidget::addNewTab()
// constructs an ImageViewer for every image tab, including ones restored by the
// session before the main window is interactive -- so an eager toolbar would
// put eight widgets, an opacity effect, an animation and five icons on the
// startup path for tabs the user may never open. First reveal happens in
// showEvent(), i.e. only once the tab is actually displayed.
void ImageViewer::ensureToolbar()
{
    if (m_toolbar)
        return;

    // Parented to the view frame rather than the viewport: QGraphicsView pans
    // by calling viewport()->scroll(), which drags the viewport's child widgets
    // along with the content. A frame child stays put.
    m_toolbar = new ImageViewerToolbar(this);
    m_toolbar->hide();

    m_toolbarOpacity = new QGraphicsOpacityEffect(m_toolbar);
    m_toolbarOpacity->setOpacity(1.0);
    m_toolbar->setGraphicsEffect(m_toolbarOpacity);

    // Cached rather than rebuilt per fade: per-invocation construction of
    // these is a documented source of visible stutter in this codebase.
    m_toolbarFade = new QPropertyAnimation(m_toolbarOpacity, "opacity", this);
    connect(m_toolbarFade, &QPropertyAnimation::finished, this, [this]() {
        if (m_toolbarOpacity->opacity() <= 0.01)
            m_toolbar->hide();
    });

    m_toolbarHideTimer = new QTimer(this);
    m_toolbarHideTimer->setSingleShot(true);
    m_toolbarHideTimer->setInterval(kToolbarIdleTimeout);
    connect(m_toolbarHideTimer, &QTimer::timeout, this, &ImageViewer::fadeOutToolbar);

    connect(m_toolbar, &ImageViewerToolbar::copyImageRequested,   this, &ImageViewer::copyImageFile);
    connect(m_toolbar, &ImageViewerToolbar::zoomInRequested,      this, &ImageViewer::zoomIn);
    connect(m_toolbar, &ImageViewerToolbar::zoomOutRequested,     this, &ImageViewer::zoomOut);
    connect(m_toolbar, &ImageViewerToolbar::fitRequested,         this, &ImageViewer::resetZoom);
    connect(m_toolbar, &ImageViewerToolbar::actualSizeRequested,  this, &ImageViewer::actualSize);
    connect(m_toolbar, &ImageViewerToolbar::rotateRequested,      this, &ImageViewer::rotateRight);
    connect(m_toolbar, &ImageViewerToolbar::hoverChanged, this, [this](bool hovered) {
        m_toolbarHovered = hovered;
        if (hovered)
            m_toolbarHideTimer->stop();
        else
            m_toolbarHideTimer->start();
    });

    m_toolbar->setDarkMode(m_dark);
}

bool ImageViewer::load(const QString& path)
{
    clearError();
    m_path = path;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize headerSize = reader.size();
    if (headerSize.isValid() && static_cast<qint64>(headerSize.width()) * headerSize.height() > kMaxPixels) {
        reader.setScaledSize(headerSize.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        showError(reader.errorString().isEmpty() ? tr("Unable to display this image.") : reader.errorString());
        return false;
    }

    m_naturalSize = headerSize.isValid() ? headerSize : image.size();
    m_scene->clear();
    m_item = m_scene->addPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(m_item->boundingRect());
    m_fitToWindow = true;
    m_rotation = 0;
    applyFit();
    updateDragMode();
    // Deliberately no revealToolbar() here: load() runs inside
    // TabWidget::addNewTab(), i.e. on the session-restore startup path, and
    // revealing would construct the toolbar for tabs that are never displayed.
    // showEvent() announces it when the tab actually becomes visible; a reload
    // of an already-visible tab keeps its existing bar, resynced by applyFit().
    return true;
}

void ImageViewer::setDarkMode(bool dark)
{
    m_dark = dark;
    setBackgroundBrush(dark ? DarkTheme::color(DarkTheme::Editor) : QColor(0xf9, 0xf9, 0xf9));
    if (m_errorLabel) {
        m_errorLabel->setStyleSheet(dark
            ? QStringLiteral("color:#bbbbbb; background:transparent;")
            : QStringLiteral("color:#555555; background:transparent;"));
    }
    if (m_toolbar) {
        m_toolbar->setDarkMode(dark);
        layoutToolbar();
    }
}

void ImageViewer::print()
{
    if (!m_item)
        return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter(&printer);
    const QPixmap pixmap = orientedPixmap();
    const QRect pageRect = printer.pageRect(QPrinter::DevicePixel).toRect();
    QSize scaled = pixmap.size();
    scaled.scale(pageRect.size(), Qt::KeepAspectRatio);
    const QRect target(pageRect.x() + (pageRect.width() - scaled.width()) / 2,
                        pageRect.y() + (pageRect.height() - scaled.height()) / 2,
                        scaled.width(), scaled.height());
    painter.drawPixmap(target, pixmap, pixmap.rect());
}

void ImageViewer::zoomIn()
{
    m_fitToWindow = false;
    m_zoomFactor = qBound(kMinZoom, m_zoomFactor * 1.25, kMaxZoom);
    applyZoom();
    revealToolbar();
}

void ImageViewer::zoomOut()
{
    m_fitToWindow = false;
    m_zoomFactor = qBound(kMinZoom, m_zoomFactor / 1.25, kMaxZoom);
    applyZoom();
    revealToolbar();
}

void ImageViewer::resetZoom()
{
    m_fitToWindow = true;
    applyFit();
    revealToolbar();
}

void ImageViewer::actualSize()
{
    m_fitToWindow = false;
    m_zoomFactor = 1.0;
    applyZoom();
    revealToolbar();
}

void ImageViewer::rotateRight()
{
    if (!m_item)
        return;
    m_rotation = (m_rotation + 90) % 360;
    if (m_fitToWindow)
        applyFit();
    else
        applyZoom();
    revealToolbar();
}

void ImageViewer::applyFit()
{
    if (!m_item)
        return;
    QTransform rotated;
    rotated.rotate(m_rotation);
    setTransform(rotated);

    QSize natural = m_naturalSize;
    if (m_rotation % 180 != 0)
        natural.transpose();

    if (natural.width() <= viewport()->width() && natural.height() <= viewport()->height()) {
        // Never upscale small images past 100%.
        m_zoomFactor = 1.0;
    } else {
        fitInView(m_item, Qt::KeepAspectRatio);
        // m11 alone is 0 at 90/270 degrees; the on-screen scale is the length
        // of the transformed unit x-axis.
        m_zoomFactor = std::hypot(transform().m11(), transform().m12());
    }
    updateDragMode();
    syncToolbar();
}

void ImageViewer::applyZoom()
{
    if (!m_item)
        return;
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_zoomFactor, m_zoomFactor);
    setTransform(t);
    updateDragMode();
    syncToolbar();
}

QPixmap ImageViewer::orientedPixmap() const
{
    if (!m_item)
        return {};
    const QPixmap source = m_item->pixmap();
    if (m_rotation == 0)
        return source;
    return source.transformed(QTransform().rotate(m_rotation), Qt::SmoothTransformation);
}

void ImageViewer::copyImageFile()
{
    if (m_path.isEmpty() || !QFileInfo::exists(m_path)) {
        themedWarning(this, tr("Copy Image"),
                     tr("The image file no longer exists:\n%1").arg(m_path));
        syncToolbar();
        return;
    }

    // Mirrors WorkspacePanelController::copySelected -- a file/URL list, not
    // the decoded pixmap, so pasting elsewhere (Explorer, Smart Paste) behaves
    // like copying the file itself rather than an in-memory bitmap.
    auto* mimeData = new QMimeData;
    mimeData->setUrls({ QUrl::fromLocalFile(m_path) });
    qApp->clipboard()->setMimeData(mimeData);
}

void ImageViewer::updateDragMode()
{
    if (!m_item) {
        setDragMode(QGraphicsView::NoDrag);
        return;
    }
    const QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();
    const bool exceedsViewport = m_item->boundingRect().width() > viewRect.width()
        || m_item->boundingRect().height() > viewRect.height();
    setDragMode(exceedsViewport ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
}

void ImageViewer::layoutToolbar()
{
    if (!m_toolbar)
        return;
    const QRect area = viewport()->geometry();
    const QSize bar = m_toolbar->sizeHint();
    m_toolbar->resize(bar);
    const int x = area.x() + (area.width() - bar.width()) / 2;
    const int y = area.bottom() - bar.height() - kToolbarBottomInset + 1;
    m_toolbar->move(qMax(area.x(), x), qMax(area.y(), y));
}

void ImageViewer::syncToolbar()
{
    if (!m_toolbar)
        return;
    m_toolbar->setZoomPercent(qRound(m_zoomFactor * 100.0));
    m_toolbar->setFitActive(m_fitToWindow);
    m_toolbar->setActualSizeActive(!m_fitToWindow && qFuzzyCompare(m_zoomFactor, 1.0));
    m_toolbar->setZoomLimits(m_zoomFactor < kMaxZoom, m_zoomFactor > kMinZoom);
    m_toolbar->setCopyImageEnabled(m_item != nullptr && QFileInfo::exists(m_path));
}

void ImageViewer::revealToolbar()
{
    // Nothing to control in the error state, and the bar would sit on top of
    // the message.
    if (!m_item)
        return;

    ensureToolbar();

    // Fast path for the common caller, mouseMoveEvent: while the bar is already
    // fully faded in there is nothing to re-sync or re-animate, and restarting
    // the animation on every move event would churn once per mouse tick during
    // a pan. Postponing the hide is all that is needed.
    if (m_toolbar->isVisible() && m_toolbarFade->state() != QAbstractAnimation::Running
        && m_toolbarOpacity->opacity() >= 1.0) {
        if (!m_toolbarHovered)
            m_toolbarHideTimer->start();
        return;
    }

    syncToolbar();
    layoutToolbar();
    if (!m_toolbar->isVisible()) {
        m_toolbarOpacity->setOpacity(0.0);
        m_toolbar->show();
        m_toolbar->raise();
    }
    m_toolbarFade->stop();
    m_toolbarFade->setDuration(kToolbarFadeIn);
    m_toolbarFade->setStartValue(m_toolbarOpacity->opacity());
    m_toolbarFade->setEndValue(1.0);
    m_toolbarFade->start();

    if (!m_toolbarHovered)
        m_toolbarHideTimer->start();
}

void ImageViewer::fadeOutToolbar()
{
    if (!m_toolbar || !m_toolbar->isVisible() || m_toolbarHovered)
        return;
    m_toolbarFade->stop();
    m_toolbarFade->setDuration(kToolbarFadeOut);
    m_toolbarFade->setStartValue(m_toolbarOpacity->opacity());
    m_toolbarFade->setEndValue(0.0);
    m_toolbarFade->start();
}

void ImageViewer::showError(const QString& message)
{
    m_scene->clear();
    m_item = nullptr;
    m_rotation = 0;
    if (m_toolbar) {
        m_toolbarHideTimer->stop();
        m_toolbarFade->stop();
        m_toolbar->hide();
        // A hidden widget gets no Leave, so a stale "hovered" would suppress
        // the idle timer for the rest of the tab's life.
        m_toolbarHovered = false;
    }
    if (!m_errorLabel) {
        m_errorLabel = new QLabel(this);
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
    }
    m_errorLabel->setText(message);
    m_errorLabel->setGeometry(viewport()->rect());
    m_errorLabel->show();
    setDarkMode(m_dark);
}

void ImageViewer::clearError()
{
    if (m_errorLabel)
        m_errorLabel->hide();
}

void ImageViewer::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        m_fitToWindow = false;
        const double factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        m_zoomFactor = qBound(kMinZoom, m_zoomFactor * factor, kMaxZoom);
        applyZoom();
        revealToolbar();
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_errorLabel)
        m_errorLabel->setGeometry(viewport()->rect());
    if (m_fitToWindow)
        applyFit();
    else
        updateDragMode();
    layoutToolbar();
}

void ImageViewer::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);
    revealToolbar();
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_item) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    // Toggle Fit <-> Actual Size. AnchorUnderMouse is already the transform
    // anchor, so 1:1 keeps the clicked point under the cursor.
    if (m_fitToWindow)
        actualSize();
    else
        resetZoom();
    event->accept();
}

void ImageViewer::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    // load() runs in TabWidget::addNewTab() before the tab is ever displayed,
    // so its reveal would expire unseen. Announce the bar when the tab is
    // actually shown, and again on every switch back to it.
    //
    // Deliberately no enterEvent/leaveEvent override: QAbstractScrollArea
    // forwards its viewport's mouse events but not Enter/Leave, so those would
    // be dead code. Entering the viewer always produces a mouse move, which
    // reveals; leaving is covered by the idle timer already running.
    revealToolbar();
}

void ImageViewer::contextMenuEvent(QContextMenuEvent* event)
{
    AlignedMenu menu(this);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);
    QAction* zoomInAct = menu.addAction(tr("Zoom In"));
    QAction* zoomOutAct = menu.addAction(tr("Zoom Out"));
    QAction* fitAct = menu.addAction(tr("Fit to Window"));
    QAction* actualAct = menu.addAction(tr("Actual Size"));
    QAction* rotateAct = menu.addAction(tr("Rotate 90%1 Right").arg(QChar(0x00B0)));
    menu.addSeparator();
    QAction* copyImageAct = menu.addAction(tr("Copy Image"));
    QAction* copyPathAct = menu.addAction(tr("Copy Path"));
    QAction* revealAct = menu.addAction(tr("Reveal in Explorer"));
    QAction* openExternalAct = menu.addAction(tr("Open with Default Application"));

    zoomInAct->setEnabled(m_item != nullptr);
    zoomOutAct->setEnabled(m_item != nullptr);
    fitAct->setEnabled(m_item != nullptr);
    actualAct->setEnabled(m_item != nullptr);
    rotateAct->setEnabled(m_item != nullptr);
    copyImageAct->setEnabled(m_item != nullptr);

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == zoomInAct) zoomIn();
    else if (chosen == zoomOutAct) zoomOut();
    else if (chosen == fitAct) resetZoom();
    else if (chosen == actualAct) actualSize();
    else if (chosen == rotateAct) rotateRight();
    else if (chosen == copyImageAct && m_item) qApp->clipboard()->setPixmap(orientedPixmap());
    else if (chosen == copyPathAct) qApp->clipboard()->setText(m_path);
    else if (chosen == revealAct) WorkspaceFileOps::revealFileInExplorer(m_path);
    else if (chosen == openExternalAct) QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
}
