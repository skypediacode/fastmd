#pragma once
#include <QApplication>
#include <QComboBox>
#include <QListView>
#include <QPainter>
#include <QStyledItemDelegate>

// FastMD's standard dropdown: the QSS in AppStyle keyed to the `compactCombo`
// object name, a fixed-height item delegate, and a hand-painted chevron (QSS
// cannot express it, so `::down-arrow { image: none; }` clears the native one).
// Every dialog that needs a combo box uses this rather than styling its own,
// which is what keeps Preferences and Fix All Misspellings identical.
class CompactComboItemDelegate final : public QStyledItemDelegate
{
public:
    explicit CompactComboItemDelegate(int rowHeight, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_rowHeight(rowHeight) {}

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(m_rowHeight);
        return size;
    }

private:
    int m_rowHeight;
};

class CompactComboBox : public QComboBox
{
public:
    explicit CompactComboBox(QWidget* parent = nullptr, int rowHeight = 32)
        : QComboBox(parent), m_rowHeight(rowHeight)
    {
        setObjectName(QStringLiteral("compactCombo"));
        setMaxVisibleItems(8);
        // Keep QComboBox's purpose-built QComboBoxListView.  A replacement
        // plain QListView misses the popup-specific resize/paint handling and
        // can expose stale rows while the top-level popup is being shown.
        auto* listView = qobject_cast<QListView*>(view());
        Q_ASSERT(listView);
        listView->setFont(font());
        listView->setUniformItemSizes(true);
        listView->setSpacing(0);
        listView->setItemDelegate(new CompactComboItemDelegate(m_rowHeight, listView));
        listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }

    int rowHeight() const { return m_rowHeight; }

protected:
    void showPopup() override
    {
        if (auto* listView = qobject_cast<QListView*>(view())) {
            listView->setFixedWidth(width());
            // QComboBox sizes the container for maxVisibleItems() rows; a
            // smaller cap here left the difference as blank bands above and
            // below the list.
            listView->setMaximumHeight(maxVisibleItems() * m_rowHeight + 2);
            listView->setMinimumHeight(m_rowHeight);
        }
        // Give Qt the final constraints before it computes the popup geometry.
        // Also suppress Qt's 150 ms UI_AnimateCombo scroll effect for this
        // popup.  On Windows that effect exposes successively clipped list
        // rows, followed by an empty viewport frame, before the real view is
        // painted; the current/first visible item consequently flashes.  The
        // application setting is restored immediately, so other widgets keep
        // the user's system animation preference.
        const bool comboAnimationEnabled = QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
        if (comboAnimationEnabled)
            QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
        QComboBox::showPopup();
        if (comboAnimationEnabled)
            QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);

        // QComboBox queues the final container update.  Without the scroll
        // effect masking that update, Windows can compose the popup before
        // every row has been repainted (most visibly when row 0 is current).
        // Complete the viewport paint in this event so the first composed
        // frame cannot contain a stale or blank row.
        if (auto* listView = qobject_cast<QListView*>(view()); listView && listView->isVisible()) {
            listView->doItemsLayout();
            listView->window()->repaint();
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        QComboBox::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        QPen pen(palette().color(group, QPalette::Text));
        pen.setWidthF(1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        const qreal centerX = width() - 16.0;
        const qreal centerY = height() / 2.0 - 1.0;
        painter.drawLine(QPointF(centerX - 4.0, centerY - 1.5),
                         QPointF(centerX, centerY + 2.5));
        painter.drawLine(QPointF(centerX, centerY + 2.5),
                         QPointF(centerX + 4.0, centerY - 1.5));
    }

private:
    int m_rowHeight;
};
