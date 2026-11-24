#include "GitBranchSelector.h"

#include "IconHelper.h"

#include <QApplication>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QScreen>
#include <QStyleFactory>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {

// Material Icons codepoints.
constexpr ushort kIconBranch  = 0xE0B6;  // call_split
constexpr ushort kIconChevron = 0xE5C5;  // arrow_drop_down

constexpr int kPadH        = 6;
constexpr int kGlyphSize   = 14;
constexpr int kChevronSize = 16;
constexpr int kGap         = 4;
constexpr int kMinTextWidth = 48;
constexpr int kButtonHeight = 22;

constexpr int RoleName       = Qt::UserRole + 1;
constexpr int RoleRemote     = Qt::UserRole + 2;
constexpr int RoleSelectable = Qt::UserRole + 3;
constexpr int RoleSearchText = Qt::UserRole + 4;
constexpr int RoleCurrent    = Qt::UserRole + 5;

constexpr ushort kIconCheck = 0xE5CA;  // check
constexpr int kCheckGutter  = 14;

}

// ---------------------------------------------------------------------------
// GitBranchButton

GitBranchButton::GitBranchButton(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gitBranchButton"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rebuildGlyphs();
}

void GitBranchButton::rebuildGlyphs()
{
    const QColor color = m_dark ? QColor(0xc8, 0xc8, 0xc8) : QColor(0x5a, 0x5a, 0x5a);
    const qreal dpr = devicePixelRatioF();
    const auto render = [&](ushort code, int size) {
        QPixmap pm = IconHelper::materialIcon(QChar(code), color, size)
                         .pixmap(QSize(size, size), dpr);
        pm.setDevicePixelRatio(dpr);
        return pm;
    };
    m_branchGlyph = render(kIconBranch, kGlyphSize);
    m_chevronGlyph = render(kIconChevron, kChevronSize);
}

QFont GitBranchButton::labelFont() const
{
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::Medium);
    return f;
}

void GitBranchButton::setBranchText(const QString& text)
{
    if (m_text == text)
        return;
    m_text = text;
    setVisible(!text.isEmpty());
    updateGeometry();
    update();
}

void GitBranchButton::setMaxTextWidth(int width)
{
    const int clamped = qMax(0, width);
    if (m_maxTextWidth == clamped)
        return;
    m_maxTextWidth = clamped;
    updateGeometry();
    update();
}

void GitBranchButton::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    rebuildGlyphs();
    update();
}

QSize GitBranchButton::sizeHint() const
{
    // Round the fractional advance up: a floored integer width is short by a
    // fraction of a pixel, which was enough to elide a name that fits.
    int textWidth = m_text.isEmpty()
        ? 0 : int(std::ceil(QFontMetricsF(labelFont()).horizontalAdvance(m_text)));
    if (m_maxTextWidth > 0)
        textWidth = qMin(textWidth, m_maxTextWidth);
    return QSize(kPadH * 2 + kGlyphSize + kGap + textWidth + kChevronSize,
                 kButtonHeight);
}

QSize GitBranchButton::minimumSizeHint() const
{
    return QSize(qMin(sizeHint().width(),
                      kPadH * 2 + kGlyphSize + kGap + kMinTextWidth + kChevronSize),
                 kButtonHeight);
}

void GitBranchButton::paintEvent(QPaintEvent*)
{
    if (m_text.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_hovered || m_pressed || hasFocus()) {
        const int alpha = m_pressed ? (m_dark ? 34 : 26) : (m_dark ? 22 : 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_dark ? QColor(255, 255, 255, alpha)
                                : QColor(0, 0, 0, alpha));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    }

    const QColor color = m_dark ? QColor(0xc8, 0xc8, 0xc8) : QColor(0x5a, 0x5a, 0x5a);

    const int x = kPadH + kGlyphSize + kGap;
    painter.drawPixmap(QPoint(kPadH, (height() - kGlyphSize) / 2), m_branchGlyph);

    const int chevronLeft = width() - kPadH - kChevronSize;
    const QRect textRect(x, 0, qMax(0, chevronLeft - x), height());
    painter.setFont(labelFont());
    painter.setPen(color);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(labelFont()).elidedText(m_text, Qt::ElideMiddle,
                                                          textRect.width()));

    painter.drawPixmap(QPoint(chevronLeft, (height() - kChevronSize) / 2),
                       m_chevronGlyph);
}

void GitBranchButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    update();
}

void GitBranchButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    if (wasPressed && rect().contains(event->pos()))
        emit clicked();
}

void GitBranchButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void GitBranchButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QWidget::leaveEvent(event);
}

void GitBranchButton::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter) {
        emit clicked();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// GitBranchPopup

GitBranchPopup::GitBranchPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("gitBranchPopup"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("gitBranchPopupSearch"));
    m_search->setPlaceholderText(tr("Search branches..."));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("gitBranchPopupTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(8);
    m_tree->setMinimumHeight(220);
    // The current branch is marked in a decoration gutter rather than with a
    // text prefix: a proportional font makes "✓ " and its blank stand-in
    // different widths, which shifted the checked-out branch's name right of
    // every other one. Every branch row carries an icon of this exact size --
    // a transparent one when it is not current -- so all names share an X.
    m_tree->setIconSize(QSize(kCheckGutter, kCheckGutter));
    QFont treeFont = m_tree->font();
    treeFont.setPixelSize(QFontInfo(treeFont).pixelSize() + 1);
    m_tree->setFont(treeFont);
    // Same reason as LanguageSelectorPopup: the native Windows 11 style paints
    // its own current-item indicator that our ::item QSS cannot suppress.
    if (QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        fusionStyle->setParent(m_tree);
        m_tree->setStyle(fusionStyle);
    }
    layout->addWidget(m_tree);

    connect(m_search, &QLineEdit::textChanged, this, &GitBranchPopup::applyFilter);
    // Single click selects; only a double-click checks out, matching the
    // Changes and Graph lists.
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) { activateItem(item); });

    m_search->installEventFilter(this);
    m_tree->installEventFilter(this);
}

QIcon GitBranchPopup::checkIcon()
{
    if (m_checkIcon.isNull()) {
        // The tree's QSS colour reaches its palette once it has been polished.
        m_tree->ensurePolished();
        m_checkIcon = IconHelper::materialIcon(QChar(kIconCheck),
                                               m_tree->palette().color(QPalette::Text),
                                               kCheckGutter);
    }
    return m_checkIcon;
}

QIcon GitBranchPopup::blankIcon()
{
    if (m_blankIcon.isNull()) {
        const qreal dpr = devicePixelRatioF();
        QPixmap pm(QSize(kCheckGutter, kCheckGutter) * dpr);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(dpr);
        m_blankIcon = QIcon(pm);
    }
    return m_blankIcon;
}

void GitBranchPopup::setBranches(const QVector<GitBranchRef>& branches)
{
    m_tree->clear();

    QTreeWidgetItem* sections[2] = { nullptr, nullptr };
    const QString titles[2] = { tr("LOCAL"), tr("REMOTE") };

    for (const GitBranchRef& branch : branches) {
        const int group = branch.remote ? 1 : 0;
        if (!sections[group]) {
            auto* section = new QTreeWidgetItem(m_tree, { titles[group] });
            section->setFlags(section->flags() & ~Qt::ItemIsSelectable);
            QFont sectionFont = section->font(0);
            sectionFont.setBold(true);
            section->setFont(0, sectionFont);
            section->setData(0, RoleSelectable, false);
            sections[group] = section;
        }

        auto* item = new QTreeWidgetItem(sections[group]);
        item->setData(0, RoleName, branch.name);
        item->setData(0, RoleRemote, branch.remote);
        item->setData(0, RoleSelectable, true);
        item->setData(0, RoleCurrent, branch.current);
        item->setData(0, RoleSearchText, branch.name.toLower());
        item->setText(0, branch.name);
        item->setIcon(0, branch.current ? checkIcon() : blankIcon());
        item->setToolTip(0, branch.current
            ? tr("%1 (current branch)").arg(branch.name) : branch.name);
        item->setData(0, Qt::AccessibleTextRole, branch.current
            ? tr("%1, current branch").arg(branch.name) : branch.name);

        QFont f = item->font(0);
        f.setBold(branch.current);
        item->setFont(0, f);
    }

    m_tree->expandAll();
}

void GitBranchPopup::popupUnder(QWidget* anchor, QWidget* rightAlignTo)
{
    if (!anchor)
        return;

    m_search->clear();
    applyFilter(QString());

    // Wide enough for realistic branch names without ever outgrowing the
    // sidebar it drops out of.
    QWidget* reference = rightAlignTo ? rightAlignTo : anchor;
    const int width = qBound(240, reference->width(), 420);
    resize(width, sizeHint().height());

    // Map only the widgets' own top-left corners: mapToGlobal is unreliable
    // for points on or past a widget's edge under per-screen scaling.
    const QPoint anchorTopLeft = anchor->mapToGlobal(QPoint(0, 0));
    const QPoint referenceTopLeft = reference->mapToGlobal(QPoint(0, 0));

    QPoint pos(referenceTopLeft.x() + reference->width() - this->width(),
               anchorTopLeft.y() + anchor->height() + 2);
    if (const QScreen* screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        if (pos.x() + this->width() > avail.right())
            pos.setX(avail.right() - this->width());
        if (pos.x() < avail.left())
            pos.setX(avail.left());
        // No room below: flip above the selector rather than off-screen.
        if (pos.y() + height() > avail.bottom())
            pos.setY(qMax(avail.top(), anchorTopLeft.y() - height() - 2));
    }
    move(pos);
    show();
    m_search->setFocus();
}

void GitBranchPopup::applyFilter(const QString& text)
{
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* section = m_tree->topLevelItem(i);
        bool anyVisible = false;
        for (int c = 0; c < section->childCount(); ++c) {
            QTreeWidgetItem* child = section->child(c);
            const bool match = needle.isEmpty()
                || child->data(0, RoleSearchText).toString().contains(needle);
            child->setHidden(!match);
            anyVisible = anyVisible || match;
        }
        section->setHidden(!anyVisible);
    }
}

void GitBranchPopup::activateItem(QTreeWidgetItem* item)
{
    if (!item || !item->data(0, RoleSelectable).toBool())
        return;

    // Choosing the branch already checked out is a no-op, not an error.
    if (!item->data(0, RoleCurrent).toBool()) {
        emit branchActivated(item->data(0, RoleName).toString(),
                             item->data(0, RoleRemote).toBool());
    }
    close();
}

void GitBranchPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool GitBranchPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int key = keyEvent->key();

    if (key == Qt::Key_Escape) {
        close();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        activateItem(m_tree->currentItem());
        return true;
    }
    if (watched == m_search && (key == Qt::Key_Down || key == Qt::Key_Up)) {
        m_tree->setFocus();
        if (!m_tree->currentItem()) {
            for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem* section = m_tree->topLevelItem(i);
                if (section->isHidden())
                    continue;
                for (int c = 0; c < section->childCount(); ++c) {
                    if (!section->child(c)->isHidden()) {
                        m_tree->setCurrentItem(section->child(c));
                        return true;
                    }
                }
            }
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
