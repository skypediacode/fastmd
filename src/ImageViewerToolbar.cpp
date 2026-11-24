#include "ImageViewerToolbar.h"
#include "IconHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QEvent>

namespace {
constexpr int kButtonSize = 39;
constexpr int kIconPx     = 24;
constexpr int kZoomIconPx = 28;   // zoom_in/zoom_out have more em-box padding than their neighbours

// materialIconTight centres on the ink bounding box. For zoom_in/zoom_out that
// box spans lens *and* handle, and the handle hangs below the lens -- so
// balancing the box leaves the lens, which is what the eye reads as the icon,
// sitting high next to the vertically symmetric crop_free and rotate_right.
// Nudge them back down onto the row's optical centre.
constexpr int kZoomGlyphYOffset = 3;
constexpr int kBarHeight  = 51;
constexpr int kZoomLabelWidth = 69;   // fixed so "5%" and "2000%" do not resize the bar
constexpr int kInnerPad   = 6;
constexpr int kTightGap   = 2;
constexpr int kGroupGap   = 6;

// Material Icons codepoints.
constexpr char16_t kGlyphCopyImage = 0xE14D;   // content_copy
constexpr char16_t kGlyphZoomOut = 0xE900;
constexpr char16_t kGlyphZoomIn  = 0xE8FF;
constexpr char16_t kGlyphFit     = 0xE3C2;   // crop_free
constexpr char16_t kGlyphRotate  = 0xE41A;   // rotate_right
}

ImageViewerToolbar::ImageViewerToolbar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("imageViewerToolbar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(kInnerPad, kInnerPad, kInnerPad, kInnerPad);
    row->setSpacing(kTightGap);

    m_copyImageButton = addButton(QChar(kGlyphCopyImage), tr("Copy Image"), false, kIconPx);
    row->addWidget(m_copyImageButton);

    
    m_rotateButton = addButton(QChar(kGlyphRotate),
                               tr("Rotate 90%1 Right").arg(QChar(0x00B0)), false, kIconPx);
    row->addWidget(m_rotateButton);

    row->addSpacing(kGroupGap);
    row->addWidget(addSeparator());
    row->addSpacing(kGroupGap);

    m_zoomOutButton = addButton(QChar(kGlyphZoomOut), tr("Zoom Out (Ctrl+-)"), false, kZoomIconPx,
                                kZoomGlyphYOffset);
    row->addWidget(m_zoomOutButton);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
    m_zoomLabel->setObjectName(QStringLiteral("imageViewerZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_zoomLabel);

    m_zoomInButton = addButton(QChar(kGlyphZoomIn), tr("Zoom In (Ctrl+=)"), false, kZoomIconPx,
                               kZoomGlyphYOffset);
    row->addWidget(m_zoomInButton);

    row->addSpacing(kGroupGap);
    row->addWidget(addSeparator());
    row->addSpacing(kGroupGap);

    m_fitButton = addButton(QChar(kGlyphFit), tr("Fit to Window (Ctrl+0)"), true, kIconPx);
    row->addWidget(m_fitButton);

    m_actualButton = addButton(QChar(), tr("Actual Size (100%)"), true, kIconPx);
    row->addWidget(m_actualButton);

    row->addSpacing(kGroupGap);
    // row->addWidget(addSeparator());
    // row->addSpacing(kGroupGap);


    connect(m_copyImageButton, &QToolButton::clicked, this, &ImageViewerToolbar::copyImageRequested);
    connect(m_rotateButton,  &QToolButton::clicked, this, &ImageViewerToolbar::rotateRequested);
    connect(m_zoomOutButton, &QToolButton::clicked, this, &ImageViewerToolbar::zoomOutRequested);
    connect(m_zoomInButton,  &QToolButton::clicked, this, &ImageViewerToolbar::zoomInRequested);
    connect(m_fitButton,     &QToolButton::clicked, this, &ImageViewerToolbar::fitRequested);
    connect(m_actualButton,  &QToolButton::clicked, this, &ImageViewerToolbar::actualSizeRequested);

    applyFixedMetrics();
    updateIcons();
}

QToolButton* ImageViewerToolbar::addButton(QChar glyph, const QString& tooltip, bool checkable,
                                           int iconPx, int opticalYOffset)
{
    auto* button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setToolTip(tooltip);
    button->setCheckable(checkable);
    m_glyphButtons.append({button, glyph, iconPx, opticalYOffset});
    return button;
}

QWidget* ImageViewerToolbar::addSeparator()
{
    auto* separator = new QWidget(this);
    separator->setObjectName(QStringLiteral("imageViewerToolbarSeparator"));
    separator->setAttribute(Qt::WA_StyledBackground, true);
    m_separators.append(separator);
    return separator;
}

void ImageViewerToolbar::applyFixedMetrics()
{
    // qApp->setStyleSheet() repolishes every widget and drops programmatically
    // set fixed sizes, so these have to be restated on every style change --
    // the same hazard FindOverlay documents.
    for (const GlyphButton& entry : std::as_const(m_glyphButtons)) {
        entry.button->setFixedSize(kButtonSize, kButtonSize);
        entry.button->setIconSize(QSize(entry.iconPx, entry.iconPx));
    }
    for (QWidget* separator : std::as_const(m_separators))
        separator->setFixedSize(1, kButtonSize - 9);
    if (m_zoomLabel)
        m_zoomLabel->setFixedSize(kZoomLabelWidth, kButtonSize);
    setFixedHeight(kBarHeight);
    adjustSize();
}

void ImageViewerToolbar::updateIcons()
{
    const QColor iconColor = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x42, 0x42, 0x42);
    for (const GlyphButton& entry : std::as_const(m_glyphButtons)) {
        if (entry.glyph.isNull())
            continue;
        entry.button->setIcon(IconHelper::materialIconTight(entry.glyph, iconColor, entry.iconPx,
                                                           entry.opticalYOffset));
    }
    if (m_actualButton)
        m_actualButton->setIcon(IconHelper::textIcon(QStringLiteral("1:1"), iconColor, kIconPx, true));
}

void ImageViewerToolbar::setDarkMode(bool dark)
{
    m_dark = dark;
    updateIcons();
    applyFixedMetrics();
}

void ImageViewerToolbar::setZoomPercent(int percent)
{
    const QString text = QStringLiteral("%1%").arg(percent);
    if (m_zoomLabel->text() != text)
        m_zoomLabel->setText(text);
}

void ImageViewerToolbar::setFitActive(bool active)
{
    if (m_fitButton->isChecked() != active) {
        const QSignalBlocker blocker(m_fitButton);
        m_fitButton->setChecked(active);
    }
}

void ImageViewerToolbar::setActualSizeActive(bool active)
{
    if (m_actualButton->isChecked() != active) {
        const QSignalBlocker blocker(m_actualButton);
        m_actualButton->setChecked(active);
    }
}

void ImageViewerToolbar::setZoomLimits(bool canZoomIn, bool canZoomOut)
{
    m_zoomInButton->setEnabled(canZoomIn);
    m_zoomOutButton->setEnabled(canZoomOut);
}

void ImageViewerToolbar::setCopyImageEnabled(bool enabled)
{
    m_copyImageButton->setEnabled(enabled);
}

void ImageViewerToolbar::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    emit hoverChanged(true);
}

void ImageViewerToolbar::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    emit hoverChanged(false);
}

void ImageViewerToolbar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange)
        applyFixedMetrics();
}
