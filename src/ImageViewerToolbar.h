#pragma once
#include <QWidget>
#include <QList>

class QLabel;
class QToolButton;

// Compact floating control bar for ImageViewer. A plain child widget of the
// viewer (not a top-level popup): ImageViewer renders with Qt, so there is no
// native child window to paint above -- the reason FindOverlay has to be a
// Qt::Tool window does not apply here.
//
// Purely a view. It owns no zoom state; ImageViewer pushes the current values
// in and receives intent back through these signals.
class ImageViewerToolbar : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewerToolbar(QWidget* parent);

    void setDarkMode(bool dark);
    void setZoomPercent(int percent);
    void setFitActive(bool active);
    void setActualSizeActive(bool active);
    void setZoomLimits(bool canZoomIn, bool canZoomOut);
    void setCopyImageEnabled(bool enabled);

signals:
    void copyImageRequested();
    void zoomOutRequested();
    void zoomInRequested();
    void fitRequested();
    void actualSizeRequested();
    void rotateRequested();
    // The auto-hide timer must not run while the pointer rests on the bar
    // itself -- moves over the toolbar never reach the viewer's viewport.
    void hoverChanged(bool hovered);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QToolButton* addButton(QChar glyph, const QString& tooltip, bool checkable, int iconPx,
                           int opticalYOffset = 0);
    QWidget* addSeparator();
    void applyFixedMetrics();
    void updateIcons();

    // Per-button icon size: Material glyphs do not all use the same share of
    // their em box, so a single size makes the magnifiers read smaller than
    // crop_free / rotate_right sitting beside them.
    struct GlyphButton {
        QToolButton* button = nullptr;
        QChar        glyph;
        int          iconPx = 0;
        int          opticalYOffset = 0;
    };

    QList<GlyphButton> m_glyphButtons;
    QList<QWidget*>    m_separators;
    QToolButton* m_copyImageButton = nullptr;
    QToolButton* m_zoomOutButton = nullptr;
    QToolButton* m_zoomInButton  = nullptr;
    QToolButton* m_fitButton     = nullptr;
    QToolButton* m_actualButton  = nullptr;
    QToolButton* m_rotateButton  = nullptr;
    QLabel*      m_zoomLabel     = nullptr;
    bool         m_dark          = false;
};
