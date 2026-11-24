#include "ToolbarHoverAnimation.h"
#include "ThemePalette.h"

#include <QColor>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

namespace {

constexpr int kHoverInDuration = 120;
constexpr int kHoverOutDuration = 170;

// Qt's QStyleSheetStyle takes over panel painting entirely for any widget
// matched by a QSS rule that sets background/border (the app-wide QToolButton
// rule does) -- it never delegates PE_PanelButtonTool to a custom
// QStyle::drawPrimitive override, so painting there is silently a no-op.
// Driving the animation through QWidget::setStyleSheet() instead "worked" but
// forces a full CSS reparse + unpolish/repolish on every single tick, which
// visibly flashes. So the highlight is painted by a plain, unstyled sibling
// widget stacked behind the button -- cheap per-frame repaints, and it shows
// through the button's own transparent background exactly like a real panel.
class HighlightOverlay final : public QWidget {
public:
    HighlightOverlay(QWidget* parent, int radius)
        : QWidget(parent)
        , m_radius(radius)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

    void setColors(const QColor& background, const QColor& border)
    {
        if (m_background == background && m_border == border)
            return;
        m_background = background;
        m_border = border;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_background.alpha() == 0 && m_border.alpha() == 0)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(m_border.alpha() == 0 ? Qt::NoPen : QPen(m_border, 1.0));
        painter.setBrush(m_background);
        painter.drawRoundedRect(r, m_radius, m_radius);
    }

private:
    int m_radius;
    QColor m_background{ Qt::transparent };
    QColor m_border{ Qt::transparent };
};

class Animator final : public QObject {
public:
    static Animator& instance()
    {
        static QPointer<Animator> animator;
        if (!animator)
            animator = new Animator(qApp);
        return *animator;
    }

    explicit Animator(QObject* parent) : QObject(parent) {}

    void install(QWidget* toolbar, int radius)
    {
        if (!toolbar)
            return;
        for (QToolButton* button : toolbar->findChildren<QToolButton*>())
            installButton(button, radius);
    }

    void setHovered(QWidget* widget, bool hovered)
    {
        auto* button = qobject_cast<QToolButton*>(widget);
        if (!button || !m_overlays.contains(button))
            return;
        if (hovered)
            claimHover(button);
        else
            releaseHover(button);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* button = qobject_cast<QToolButton*>(watched);
        if (!button || !m_overlays.contains(button))
            return false;

        switch (event->type()) {
        case QEvent::Enter:
            claimHover(button);
            break;
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
            // MouseButtonPress: let the app stylesheet's :pressed/:checked rule
            // show through immediately instead of the hover overlay sitting on
            // top of it.
            releaseHover(button);
            break;
        case QEvent::MouseButtonRelease:
        case QEvent::DynamicPropertyChange:
        case QEvent::EnabledChange:
            if (button->underMouse() && canAnimate(button))
                claimHover(button);
            else
                releaseHover(button);
            break;
        case QEvent::Hide:
            // No transition will be visible while the button is hidden, so
            // reset immediately rather than leaving a fading overlay box
            // floating without its button.
            if (m_hoverOwner == button)
                m_hoverOwner = nullptr;
            resetImmediate(button);
            break;
        case QEvent::Show:
            syncOverlayGeometry(button);
            break;
        case QEvent::ParentChange:
        case QEvent::WindowDeactivate:
            if (m_hoverOwner == button)
                releaseHover(button);
            break;
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::ZOrderChange:
            syncOverlayGeometry(button);
            break;
        default:
            break;
        }
        return false;
    }

private:
    void installButton(QToolButton* button, int radius)
    {
        if (!button || m_overlays.contains(button))
            return;
        button->setProperty("toolbarHoverAnimated", true);
        auto* overlay = new HighlightOverlay(button->parentWidget(), radius);
        m_overlays.insert(button, overlay);
        syncOverlayGeometry(button);
        overlay->show();
        button->installEventFilter(this);
        // The overlay is a sibling of the button under the same parent, and
        // syncOverlayGeometry()'s stackUnder() moves it *ahead* of the button in
        // that parent's child list. So when the parent is destroyed the overlay
        // is deleted first, and this handler then sees an already-destroyed
        // overlay -- calling deleteLater() on it would post a DeferredDelete for
        // freed memory. QPointer makes the entry read back null in that case.
        connect(button, &QObject::destroyed, this, [this, button]() {
            if (m_hoverOwner == button)
                m_hoverOwner = nullptr;
            if (HighlightOverlay* overlay = m_overlays.take(button).data())
                overlay->deleteLater();
        });
    }

    // Ensures there is exactly one logical hover owner: any previously
    // hovered button is force-released immediately, even if its own Leave
    // event was dropped (fast pointer sweeps, popup pointer grabs, hidden/
    // disabled transitions can all cause a Leave to never arrive). The
    // previous owner must be released unconditionally, before checking
    // whether the newly entered button itself qualifies for the overlay --
    // otherwise entering a button that can't animate (checked, popup open,
    // disabled) short-circuits past releasing the real previous owner,
    // leaving it stuck at full opacity with no further event to clear it.
    void claimHover(QToolButton* button)
    {
        if (m_hoverOwner && m_hoverOwner != button)
            releaseHover(m_hoverOwner);
        if (!canAnimate(button)) {
            releaseHover(button);
            return;
        }
        m_hoverOwner = button;
        animate(button, true);
    }

    void releaseHover(QToolButton* button)
    {
        if (m_hoverOwner == button)
            m_hoverOwner = nullptr;
        animate(button, false);
    }

    void resetImmediate(QToolButton* button)
    {
        HighlightOverlay* overlay = m_overlays.value(button);
        if (!overlay)
            return;
        if (auto* backgroundAnimation = button->findChild<QVariantAnimation*>(QStringLiteral("toolbarHoverBackgroundAnimation")))
            backgroundAnimation->stop();
        if (auto* borderAnimation = button->findChild<QVariantAnimation*>(QStringLiteral("toolbarHoverBorderAnimation")))
            borderAnimation->stop();
        overlay->setColors(Qt::transparent, Qt::transparent);
    }

    void syncOverlayGeometry(QToolButton* button)
    {
        HighlightOverlay* overlay = m_overlays.value(button);
        if (!overlay)
            return;

        QRect geom = button->geometry();
        if (button->property("toolbarHoverBorderless").toBool()) {
            if (QWidget* parent = button->parentWidget()) {
                geom.setY(0);
                geom.setHeight(parent->height() - 1);
            }
        }

        overlay->setGeometry(geom);
        overlay->stackUnder(button);
    }

    static bool canAnimate(const QToolButton* button)
    {
        // A checked button still needs hover feedback.  The overlay is
        // composited beneath the button, so the selected tint remains visible
        // while the hover tint provides the second, transient state cue.
        return button->isEnabled() && !button->isDown()
            && !button->property("popupOpen").toBool()
            && !button->property("mathToolbarPopupOpen").toBool();
    }

    void animate(QToolButton* button, bool hovered)
    {
        HighlightOverlay* overlay = m_overlays.value(button);
        if (!overlay)
            return;

        auto animationFor = [this, button](const QString& name) {
            auto* animation = button->findChild<QVariantAnimation*>(name);
            if (animation)
                return animation;
            animation = new QVariantAnimation(button);
            animation->setObjectName(name);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            connect(animation, &QVariantAnimation::valueChanged, button, [this, button](const QVariant&) {
                applyColors(button);
            });
            return animation;
        };

        const QColor textColor = button->palette().color(QPalette::ButtonText);
        QColor targetBackground = textColor;
        QColor targetBorder = textColor;
        // A soft fill alone: an outline on hover reads as button chrome the
        // flat toolbar otherwise does not have.
        targetBackground.setAlphaF(hovered ? 0.09 : 0.0);
        targetBorder.setAlphaF(0.0);

        auto* backgroundAnimation = animationFor(QStringLiteral("toolbarHoverBackgroundAnimation"));
        auto* borderAnimation = animationFor(QStringLiteral("toolbarHoverBorderAnimation"));

        auto currentValueOr = [](QVariantAnimation* animation, const QColor& fallback) {
            const QVariant value = animation->currentValue();
            return value.isValid() ? value.value<QColor>() : fallback;
        };

        auto start = [hovered](QVariantAnimation* animation, const QColor& current, const QColor& target) {
            if (current == target)
                return;
            animation->stop();
            animation->setDuration(hovered ? kHoverInDuration : kHoverOutDuration);
            animation->setStartValue(current);
            animation->setEndValue(target);
            animation->start();
        };
        start(backgroundAnimation, currentValueOr(backgroundAnimation, QColor(Qt::transparent)), targetBackground);
        start(borderAnimation, currentValueOr(borderAnimation, QColor(Qt::transparent)), targetBorder);
    }

    void applyColors(QToolButton* button)
    {
        HighlightOverlay* overlay = m_overlays.value(button);
        if (!overlay)
            return;
        auto* backgroundAnimation = button->findChild<QVariantAnimation*>(QStringLiteral("toolbarHoverBackgroundAnimation"));
        auto* borderAnimation = button->findChild<QVariantAnimation*>(QStringLiteral("toolbarHoverBorderAnimation"));
        const QColor background = backgroundAnimation && backgroundAnimation->currentValue().isValid()
            ? backgroundAnimation->currentValue().value<QColor>() : QColor(Qt::transparent);
        const QColor border = borderAnimation && borderAnimation->currentValue().isValid()
            ? borderAnimation->currentValue().value<QColor>() : QColor(Qt::transparent);
        overlay->setColors(background, border);
    }

    QHash<QToolButton*, QPointer<HighlightOverlay>> m_overlays;
    QPointer<QToolButton> m_hoverOwner;
};

}

void ToolbarHoverAnimation::install(QWidget* toolbar)
{
    Animator::instance().install(toolbar, ThemeCard::ControlRadius);
}

void ToolbarHoverAnimation::install(QWidget* toolbar, int radius)
{
    Animator::instance().install(toolbar, radius);
}

void ToolbarHoverAnimation::setHovered(QWidget* button, bool hovered)
{
    Animator::instance().setHovered(button, hovered);
}
