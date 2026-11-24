#include "SearchableComboBox.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QStyleFactory>
#include <QStyleOption>
#include <QVBoxLayout>

namespace {

// The 1px QSS border plus an inset that keeps square row highlights clear of
// the popup's rounded corners.
constexpr int kInset = 5;
constexpr int kSpacing = 4;
constexpr int kAnchorGap = 2;

// Case-insensitive substring match over the display text. A plain
// QString::contains per row: no QRegularExpression is compiled per keystroke.
class SearchFilterModel final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setNeedle(const QString& needle, bool firstRowPinned)
    {
        if (needle == m_needle && firstRowPinned == m_firstRowPinned)
            return;
        beginFilterChange();
        m_needle = needle;
        m_firstRowPinned = firstRowPinned;
        endFilterChange();
    }

    bool isPinned(int sourceRow) const { return m_firstRowPinned && sourceRow == 0; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (m_needle.isEmpty() || isPinned(sourceRow))
            return true;
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        return index.data(Qt::DisplayRole).toString().contains(m_needle, Qt::CaseInsensitive);
    }

private:
    QString m_needle;
    bool m_firstRowPinned = false;
};

} // namespace

// Permanently a Qt::Popup (Rule 11), parented to the combo so the
// `QComboBox#compactCombo QAbstractItemView` rules style its list exactly like
// every other compact dropdown's.
class SearchableComboPopup final : public QWidget
{
public:
    explicit SearchableComboPopup(SearchableComboBox* combo)
        : QWidget(combo, Qt::Popup), m_combo(combo)
    {
        setObjectName(QStringLiteral("searchableComboPopup"));
        setAttribute(Qt::WA_TranslucentBackground, true);

        auto* layout = new QVBoxLayout(this);
        // place() owns the geometry. The default constraint would pin the
        // window's minimum to the previous list height until the next layout
        // pass, so a shrinking popup kept its old height as a blank band.
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        layout->setContentsMargins(kInset, kInset, kInset, kInset);
        layout->setSpacing(kSpacing);

        search = new QLineEdit(this);
        search->setObjectName(QStringLiteral("searchableComboSearch"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);

        m_model = new SearchFilterModel(this);
        m_model->setSourceModel(combo->model());

        list = new QListView(this);
        list->setFrameShape(QFrame::NoFrame);
        list->setFocusPolicy(Qt::NoFocus);
        list->setUniformItemSizes(true);
        list->setSpacing(0);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setMouseTracking(true);
        list->setFont(combo->font());
        list->setItemDelegate(new CompactComboItemDelegate(combo->rowHeight(), list));
        // As in GitBranchPopup: the native Windows 11 style paints its own
        // current-item indicator that the ::item QSS cannot suppress.
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
            fusion->setParent(list);
            list->setStyle(fusion);
        }
        list->setModel(m_model);
        layout->addWidget(list);

        connect(search, &QLineEdit::textChanged, this, [this](const QString& text) { applyFilter(text); });
        connect(list, &QAbstractItemView::clicked, this, [this](const QModelIndex&) { commitCurrent(); });
        // The stock popup's current row follows the mouse, so Enter after a
        // hover picks the hovered row there too.
        connect(list, &QAbstractItemView::entered, list, &QAbstractItemView::setCurrentIndex);
        search->installEventFilter(this);
    }

    void open()
    {
        setAttribute(Qt::WA_NoMouseReplay, false);
        search->setPlaceholderText(m_combo->m_placeholder);
        search->setAccessibleName(m_combo->m_placeholder);
        {
            const QSignalBlocker blocker(search);
            search->clear();
        }
        m_model->setNeedle(QString(), m_combo->m_firstRowPinned);
        m_placed = false;
        place();
        show();
        search->setFocus(Qt::PopupFocusReason);
        selectRow(m_model->mapFromSource(m_combo->model()->index(m_combo->currentIndex(), 0)),
                  QAbstractItemView::PositionAtCenter);
    }

    void applyFilter(const QString& text)
    {
        m_model->setNeedle(text.trimmed(), m_combo->m_firstRowPinned);
        QModelIndex target;
        if (text.trimmed().isEmpty()) {
            target = m_model->mapFromSource(m_combo->model()->index(m_combo->currentIndex(), 0));
        } else {
            // The best match, not the pinned row that is listed regardless.
            for (int row = 0; row < m_model->rowCount() && !target.isValid(); ++row) {
                const QModelIndex index = m_model->index(row, 0);
                if (!m_model->isPinned(m_model->mapToSource(index).row()))
                    target = index;
            }
        }
        if (!target.isValid())
            target = m_model->index(0, 0);
        place();
        // A cleared search returns to the selection centered, as on open.
        selectRow(target, text.trimmed().isEmpty() ? QAbstractItemView::PositionAtCenter
                                                   : QAbstractItemView::PositionAtTop);
    }

    QLineEdit* search = nullptr;
    QListView* list = nullptr;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != search || event->type() != QEvent::KeyPress)
            return QWidget::eventFilter(watched, event);

        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (key->modifiers() & Qt::AltModifier) {
                close();
                return true;
            }
            [[fallthrough]];
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            // Navigation goes to the list while typing stays in the field.
            QCoreApplication::sendEvent(list, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitCurrent();
            return true;
        case Qt::Key_Escape:
        case Qt::Key_F4:
            close();
            return true;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return true;
        default:
            return QWidget::eventFilter(watched, event);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        // A press on the combo itself closes the popup; without this Qt would
        // replay it to the combo, which would open the popup again at once.
        if (!rect().contains(event->position().toPoint())) {
            const QRect comboRect(m_combo->mapToGlobal(QPoint(0, 0)), m_combo->size());
            if (comboRect.contains(event->globalPosition().toPoint()))
                setAttribute(Qt::WA_NoMouseReplay);
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }

private:
    void selectRow(const QModelIndex& index, QAbstractItemView::ScrollHint hint)
    {
        list->setCurrentIndex(index);
        if (index.isValid()) {
            list->doItemsLayout();
            list->scrollTo(index, hint);
        }
    }

    void commitCurrent()
    {
        const QModelIndex index = list->currentIndex();
        if (index.isValid())
            m_combo->commitRow(m_model->mapToSource(index).row());
    }

    // Sizes the list to its rows, capped at maxVisibleItems() and at the room
    // on the side of the combo chosen when the popup opened; the side stays
    // put while the filter shrinks or grows the list.
    void place()
    {
        // The search field's QSS padding is part of its size hint only once
        // it is polished, which the first open does before the first show.
        ensurePolished();
        const int rowHeight = m_combo->rowHeight();
        const int wanted = qBound(1, m_model->rowCount(), qMax(1, m_combo->maxVisibleItems()));
        const int chrome = 2 * kInset + search->sizeHint().height() + kSpacing;

        const QPoint comboTopLeft = m_combo->mapToGlobal(QPoint(0, 0));
        const QScreen* screen = m_combo->screen();
        const QRect avail = screen ? screen->availableGeometry() : QRect(comboTopLeft, QSize(4096, 4096));
        const int below = avail.bottom() + 1 - (comboTopLeft.y() + m_combo->height() + kAnchorGap);
        const int above = comboTopLeft.y() - kAnchorGap - avail.top();
        if (!m_placed) {
            // Decided on the full list, before any filtering.
            const int full = chrome + qBound(1, m_model->rowCount(), qMax(1, m_combo->maxVisibleItems())) * rowHeight;
            m_above = full > below && (full <= above || above > below);
            m_placed = true;
        }
        const int room = m_above ? above : below;
        const int rows = qBound(1, (room - chrome) / rowHeight, wanted);

        list->setFixedHeight(rows * rowHeight);
        const int height = chrome + rows * rowHeight;
        const int width = m_combo->width();
        int x = comboTopLeft.x();
        if (x + width > avail.right() + 1)
            x = avail.right() + 1 - width;
        x = qMax(x, avail.left());
        const int y = m_above ? comboTopLeft.y() - kAnchorGap - height
                              : comboTopLeft.y() + m_combo->height() + kAnchorGap;
        setGeometry(x, y, width, height);
    }

    SearchableComboBox* m_combo;
    SearchFilterModel* m_model = nullptr;
    bool m_placed = false;
    bool m_above = false;
};

SearchableComboBox::SearchableComboBox(QWidget* parent, int rowHeight)
    : CompactComboBox(parent, rowHeight)
{
}

SearchableComboBox::~SearchableComboBox() = default;

void SearchableComboBox::setSearchPlaceholderText(const QString& text)
{
    m_placeholder = text;
}

void SearchableComboBox::setFirstRowPinned(bool pinned)
{
    m_firstRowPinned = pinned;
}

void SearchableComboBox::showPopup()
{
    if (count() == 0)
        return;
    if (!m_popup)
        m_popup = new SearchableComboPopup(this);
    m_popup->open();
}

void SearchableComboBox::hidePopup()
{
    if (m_popup && m_popup->isVisible())
        m_popup->close();
    QComboBox::hidePopup();
}

QWidget* SearchableComboBox::searchPopup() const { return m_popup; }
QLineEdit* SearchableComboBox::searchField() const { return m_popup ? m_popup->search : nullptr; }
QListView* SearchableComboBox::searchList() const { return m_popup ? m_popup->list : nullptr; }

void SearchableComboBox::commitRow(int sourceRow)
{
    hidePopup();
    if (sourceRow < 0 || sourceRow >= count())
        return;
    setCurrentIndex(sourceRow);
    emit activated(sourceRow);
    emit textActivated(itemText(sourceRow));
    setFocus(Qt::PopupFocusReason);
}
