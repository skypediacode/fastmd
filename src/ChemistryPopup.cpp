#include "ChemistryPopup.h"
#include "ChemistrySymbols.h"
#include "IconHelper.h"
#include "ToolbarSvgButton.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
#include <QKeyEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QScreen>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QShowEvent>
#include <QtMath>

namespace {
constexpr int kContentMargin = 10;
constexpr int kScrollbarAllowance = 16;
constexpr int kRowSpacing = 6;
constexpr int kSvgRowHeight = 48;
constexpr int kSvgHorizontalPadding = 12;
}

ChemistryPopup::ChemistryPopup(bool dark, std::function<void(const QString&)> callback,
                                QWidget* parent, std::function<bool(const QPoint&)> onOutsideClick,
                                QVector<ChemistrySection> sections, bool verticalSvgRows,
                                std::function<void(const QPoint&)> onPointerMove)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_onOutsideClick(std::move(onOutsideClick))
    , m_sections(std::move(sections))
    , m_verticalSvgRows(verticalSvgRows)
    , m_dark(dark)
    , m_callback(std::move(callback))
    , m_onPointerMove(std::move(onPointerMove))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::StrongFocus);

    m_opacityAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_geomAnim = new QPropertyAnimation(this, "geometry", this);
    m_geomAnim->setEasingCurve(QEasingCurve::OutCubic);

    buildContent(dark, m_callback);

    // Qt::Popup already grabs mouse/keyboard, but a click landing on a native
    // child window (e.g. the WebView2 preview) can bypass that grab -- see
    // MathSymbolPopup for the same reasoning.
    qApp->installEventFilter(this);
    m_filterInstalled = true;
}

void ChemistryPopup::buildContent(bool dark, std::function<void(const QString&)> callback)
{
    setObjectName(QStringLiteral("chemistryPopup"));

    if (m_verticalSvgRows) {
        buildSvgRows();
        return;
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("chemScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(scrollArea);

    auto* content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("chemContent"));
    auto* vbox = new QVBoxLayout(content);
    vbox->setContentsMargins(kContentMargin, 6, kContentMargin, 10);
    vbox->setSpacing(2);

    const QVector<ChemistrySection>& sections = m_sections.isEmpty() ? chemistrySections() : m_sections;

    const int availableWidth = m_popupWidth - kContentMargin * 2 - kScrollbarAllowance;

    QWidget* rowWidget = nullptr;
    QHBoxLayout* rowLayout = nullptr;
    int rowWidth = 0;

    auto flushRow = [&]() {
        if (rowLayout) {
            rowLayout->addStretch(1);
            rowLayout = nullptr;
            rowWidget = nullptr;
            rowWidth = 0;
        }
    };

    for (int s = 0; s < sections.size(); ++s) {
        const ChemistrySection& section = sections.at(s);

        if (s > 0) {
            auto* separatorRow = new QWidget(content);
            separatorRow->setObjectName(QStringLiteral("chemSeparator"));
            separatorRow->setFixedHeight(9);
            auto* separatorLayout = new QVBoxLayout(separatorRow);
            separatorLayout->setContentsMargins(0, 4, 0, 4);
            separatorLayout->setSpacing(0);
            auto* separatorLine = new QFrame(separatorRow);
            separatorLine->setObjectName(QStringLiteral("chemSeparatorLine"));
            separatorLine->setFixedHeight(1);
            separatorLayout->addWidget(separatorLine);
            vbox->addWidget(separatorRow);
        }

        auto* heading = new QLabel(section.title, content);
        heading->setObjectName(QStringLiteral("chemSectionHeading"));
        vbox->addWidget(heading);

        for (const ChemistryItem& item : section.items) {
            const QString latex = item.insertLatex;
            const QString tooltip = item.tooltip.isEmpty()
                ? item.insertLatex
                : item.tooltip + QStringLiteral("\n") + item.insertLatex;

            if (item.compact) {
                auto* btn = new QToolButton(content);
                btn->setObjectName(QStringLiteral("chemCompactBtn"));
                btn->setIcon(IconHelper::generatedChemistrySvgIcon(item.id,
                    dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x22, 0x22, 0x22)));
                btn->setIconSize(QSize(44, 34));
                btn->setToolTip(tooltip);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFocusPolicy(Qt::NoFocus);
                btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                QFont f = btn->font();
                f.setPointSizeF(f.pointSizeF() + 1.5);
                btn->setFont(f);

                const int w = qMax(54, btn->sizeHint().width());
                if (!rowLayout || (rowWidth > 0 && rowWidth + kRowSpacing + w > availableWidth)) {
                    flushRow();
                    rowWidget = new QWidget(content);
                    rowLayout = new QHBoxLayout(rowWidget);
                    rowLayout->setContentsMargins(0, 0, 0, 0);
                    rowLayout->setSpacing(kRowSpacing);
                    vbox->addWidget(rowWidget);
                    rowWidth = 0;
                }
                rowLayout->addWidget(btn);
                rowWidth += (rowWidth > 0 ? kRowSpacing : 0) + w;

                QObject::connect(btn, &QToolButton::clicked, this, [this, callback, latex]() {
                    auto cb = callback;
                    close();
                    cb(latex);
                });
            } else {
                flushRow();
                auto* btn = new QPushButton(content);
                btn->setObjectName(QStringLiteral("chemFullBtn"));
                btn->setIcon(IconHelper::generatedChemistrySvgIcon(item.id,
                    dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x22, 0x22, 0x22)));
                btn->setIconSize(QSize(220, 36));
                btn->setToolTip(tooltip);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFocusPolicy(Qt::NoFocus);
                btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                QFont f = btn->font();
                f.setPointSizeF(f.pointSizeF() + 1.5);
                btn->setFont(f);
                vbox->addWidget(btn);

                QObject::connect(btn, &QPushButton::clicked, this, [this, callback, latex]() {
                    auto cb = callback;
                    close();
                    cb(latex);
                });
            }
        }
        flushRow();
    }

    scrollArea->setWidget(content);

    m_desiredHeight = content->sizeHint().height() + 2;
    resize(m_popupWidth, qBound(m_minHeight, m_desiredHeight, m_maxHeight));
}

void ChemistryPopup::buildSvgRows()
{
    const QVector<ChemistrySection>& sections = m_sections.isEmpty() ? chemistrySections() : m_sections;

    const int svgHeight = ToolbarSvgButton::iconHeight();
    int widestIcon = 0;
    int itemCount = 0;
    for (const ChemistrySection& section : sections) {
        itemCount += section.items.size();
        for (const ChemistryItem& item : section.items)
            widestIcon = qMax(widestIcon,
                               qRound(IconHelper::mathSymbolAspectRatio(item.id) * svgHeight));
    }
    const int rowWidth = widestIcon + 2 * kSvgHorizontalPadding;
    m_popupWidth = rowWidth + 2;
    m_desiredHeight = itemCount * kSvgRowHeight + 2 * kContentMargin + 2;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("chemScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(scrollArea);

    auto* content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("chemContent"));
    content->setMinimumWidth(rowWidth);
    auto* rows = new QVBoxLayout(content);
    rows->setContentsMargins(0, kContentMargin, 0, kContentMargin);
    rows->setSpacing(0);

    const QColor iconColor = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x22, 0x22, 0x22);
    for (const ChemistrySection& section : sections) {
        for (const ChemistryItem& item : section.items) {
            auto* button = new ToolbarSvgButton(IconHelper::generatedChemistrySvgIcon(item.id, iconColor),
                                                IconHelper::mathSymbolAspectRatio(item.id), false,
                                                content, false, true);
            button->setObjectName(QStringLiteral("chemSvgRow"));
            button->setFixedHeight(kSvgRowHeight);
            button->setIconAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            button->setIconHorizontalPadding(kSvgHorizontalPadding);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setToolTip(item.tooltip.isEmpty() ? item.insertLatex
                : item.tooltip + QStringLiteral("\n") + item.insertLatex);
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::NoFocus);
            rows->addWidget(button);
            const QString latex = item.insertLatex;
            QObject::connect(button, &QToolButton::clicked, this, [this, latex]() {
                auto cb = m_callback;
                close();
                cb(latex);
            });
        }
    }
    scrollArea->setWidget(content);

    m_desiredHeight = content->sizeHint().height() + 2;
    resize(m_popupWidth, m_desiredHeight);
}

void ChemistryPopup::adjustGeometryToScreen(QToolButton* button)
{
    if (!button)
        return;

    QScreen* screen = button->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenRect = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QPoint btnTopLeft = button->mapToGlobal(QPoint(0, 0));
    const QPoint btnBottomLeft = button->mapToGlobal(QPoint(0, button->height()));

    const int desired = m_verticalSvgRows
        ? m_desiredHeight
        : qBound(m_minHeight, m_desiredHeight, m_maxHeight);

    const int availBelow = screenRect.bottom() - btnBottomLeft.y() + 1;
    const int availAbove = btnTopLeft.y() - screenRect.top();

    int targetY;
    int targetHeight;

    if (desired <= availBelow) {
        targetY = btnBottomLeft.y();
        targetHeight = desired;
    } else if (desired <= availAbove) {
        targetY = btnTopLeft.y() - desired;
        targetHeight = desired;
    } else if (availBelow >= availAbove) {
        targetY = btnBottomLeft.y();
        targetHeight = m_verticalSvgRows
            ? qMax(kSvgRowHeight + 2 * kContentMargin + 2,
                   ((availBelow - 2 * kContentMargin - 2) / kSvgRowHeight) * kSvgRowHeight
                       + 2 * kContentMargin + 2)
            : qMax(m_minHeight, availBelow);
    } else {
        targetHeight = m_verticalSvgRows
            ? qMax(kSvgRowHeight + 2 * kContentMargin + 2,
                   ((availAbove - 2 * kContentMargin - 2) / kSvgRowHeight) * kSvgRowHeight
                       + 2 * kContentMargin + 2)
            : qMax(m_minHeight, availAbove);
        targetY = btnTopLeft.y() - targetHeight;
    }

    const bool needsScrollbar = targetHeight < desired;
    const int width = m_popupWidth + (needsScrollbar ? kScrollbarAllowance : 0);

    int targetX = btnTopLeft.x();
    if (targetX + width > screenRect.right())
        targetX = screenRect.right() - width + 1;
    if (targetX < screenRect.left())
        targetX = screenRect.left();

    resize(width, targetHeight);
    move(targetX, targetY);
}

void ChemistryPopup::paintEvent(QPaintEvent*)
{
    const QColor background = m_dark ? QColor(0x20, 0x21, 0x22) : QColor(0xff, 0xff, 0xff);
    const QColor border = m_dark ? QColor(0x2a, 0x2b, 0x2c) : QColor(0xd4, 0xd4, 0xd4);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    painter.fillPath(outline, background);
    painter.setPen(border);
    painter.drawPath(outline);
}

void ChemistryPopup::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void ChemistryPopup::hideEvent(QHideEvent* event)
{
    if (m_filterInstalled) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    QWidget::hideEvent(event);
    emit closed();
}

void ChemistryPopup::showEvent(QShowEvent* event)
{
    const QRect targetRect = geometry();
    if (targetRect.height() > 10) {
        QRect startRect = targetRect;
        startRect.setTop(targetRect.top() - 12);
        startRect.setBottom(targetRect.top() + static_cast<int>(targetRect.height() * 0.85));

        setGeometry(startRect);
        setWindowOpacity(0.0);

        m_opacityAnim->stop();
        m_opacityAnim->setDuration(150);
        m_opacityAnim->setStartValue(0.0);
        m_opacityAnim->setEndValue(1.0);
        m_opacityAnim->start();

        m_geomAnim->stop();
        m_geomAnim->setDuration(150);
        m_geomAnim->setStartValue(startRect);
        m_geomAnim->setEndValue(targetRect);
        m_geomAnim->start();
    }
    QWidget::showEvent(event);
}

bool ChemistryPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove && isVisible() && m_onPointerMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        m_onPointerMove(me->globalPosition().toPoint());
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = me->globalPosition().toPoint();
        if (!geometry().contains(globalPos) && isVisible()) {
            if (m_onOutsideClick && m_onOutsideClick(globalPos))
                return true;
            close();
        }
    }
    return QWidget::eventFilter(watched, event);
}
