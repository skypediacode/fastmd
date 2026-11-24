#pragma once
#include <QGraphicsView>
#include <QString>
#include <QSize>
#include <QPixmap>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QTimer;
class ImageViewerToolbar;

// Native, Qt-only read-only image viewer for FastMD's Image editor mode.
// No WebView2 involvement -- QGraphicsView/QGraphicsPixmapItem give pan and
// zoom for free. SVG is handled by its own WebView2-backed viewer mode
// instead (EditorMode::Svg, see PreviewWidget), not by this class: Qt's SVG
// plugin cannot render foreignObject or script, so a real browser engine is
// the only way to match what SVG authors actually expect to see.
//
// Rotation is a view transform only: it never touches the decoded pixmap, the
// file, or DocumentModel, is not persisted in the session, and resets on every
// load. An image tab can therefore never become dirty.
class ImageViewer : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent = nullptr);

    bool load(const QString& path);
    void setDarkMode(bool dark);
    void print();

public slots:
    void zoomIn();
    void zoomOut();
    void resetZoom();    // fit to window
    void actualSize();   // 1:1
    void rotateRight();  // +90 degrees, view only
    void copyImageFile(); // copies the source file itself (file/URL MIME), not the decoded pixmap

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyZoom();
    void applyFit();
    void showError(const QString& message);
    void clearError();
    void updateDragMode();

    void ensureToolbar();
    void layoutToolbar();
    void syncToolbar();
    void revealToolbar();
    void fadeOutToolbar();

    // The on-screen orientation, for Copy Image and Print. Rotation is a view
    // transform, but what those two produce should match what the user sees.
    QPixmap orientedPixmap() const;

    QGraphicsScene*       m_scene = nullptr;
    QGraphicsPixmapItem*  m_item  = nullptr;
    QString m_path;
    QSize   m_naturalSize;
    bool    m_fitToWindow  = true;
    double  m_zoomFactor   = 1.0;  // on-screen scale of the view transform
    int     m_rotation     = 0;    // 0 / 90 / 180 / 270, view transform only
    bool    m_dark         = false;
    QLabel* m_errorLabel   = nullptr;

    ImageViewerToolbar*     m_toolbar        = nullptr;
    QGraphicsOpacityEffect* m_toolbarOpacity = nullptr;
    QPropertyAnimation*     m_toolbarFade    = nullptr;
    QTimer*                 m_toolbarHideTimer = nullptr;
    bool                    m_toolbarHovered = false;
};
