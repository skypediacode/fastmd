#include "WorkspaceSearchPanel.h"
#include "DarkTheme.h"

#include "AlignedMenu.h"
#include "IconHelper.h"
#include "ThemePalette.h"
#include "ThemedMessageBox.h"
#include "WorkspaceSearchFiles.h"

#include <QAbstractItemView>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <utility>

using namespace WorkspaceTextSearch;

namespace {

constexpr int kRowHeight = 28;
constexpr int kButtonSize = 24;
constexpr int kIconPx = 16;
constexpr int kChevronWidth = 18;
constexpr int kBorder = 1;
constexpr int kInnerPad = 6;
constexpr int kRowGap = 6;
constexpr int kTightGap = 2;
constexpr int kGroupGap = 4;

constexpr ushort kIconChevronRight = 0xE5CC;
constexpr ushort kIconExpandMore = 0xE5CF;
constexpr ushort kIconFindReplace = 0xE881;
constexpr ushort kIconDoneAll = 0xE877;
constexpr ushort kIconClose = 0xE5CD;

// Result tree geometry. The file name and every match preview start at the same
// x: chevron column + gap, measured from the row's left edge, never from the
// text itself, so the alignment survives any font, DPI or path length.
constexpr int kLeftPad = 8;
constexpr int kChevronColumn = 16;
constexpr int kTextGap = 4;
constexpr int kRightPad = 8;
constexpr int kActionSize = 20;
constexpr int kFileRowMinHeight = 24;
constexpr int kMatchRowMinHeight = 22;

enum Role {
    KindRole = Qt::UserRole + 1,
    PathRole,
    LineRole,
    ColumnRole,
    LengthRole,
    PreviewRole,
    PreviewStartRole,
    PreviewLengthRole,
    CountRole,
    DirRole,
    CapturesRole,
};
enum Kind { FileRow = 0, MatchRow = 1 };

void repolish(QWidget* widget)
{
    if (!widget || !widget->style())
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// QFontMetrics::elidedText() compares against the exact (fractional) text width,
// while every budget here is built from the rounded horizontalAdvance(), so text
// that fits exactly was being elided ("the" drawn as "t..."). Only elide text
// whose advance genuinely exceeds the budget.
QString fitText(const QFontMetrics& metrics, const QString& text, Qt::TextElideMode mode, int width)
{
    return metrics.horizontalAdvance(text) <= width ? text : metrics.elidedText(text, mode, width);
}

QRect actionRectFor(const QRect& row)
{
    return QRect(row.right() - kRightPad - kActionSize + 1,
                 row.center().y() - kActionSize / 2, kActionSize, kActionSize);
}

QString relativeDirectory(const QString& root, const QString& path)
{
    const QString dir = QFileInfo(path).absolutePath();
    const QString relative = QDir(root).relativeFilePath(dir);
    return relative == QLatin1String(".") ? QString() : QDir::toNativeSeparators(relative);
}

}  // namespace

// ---------------------------------------------------------------------------
class SearchResultDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setDark(bool dark)
    {
        m_dark = dark;
        m_iconsReady = false;
    }
    void setActionsEnabled(bool enabled) { m_actionsEnabled = enabled; }
    bool actionsEnabled() const { return m_actionsEnabled; }
    void setReplacePreview(bool active, const QString& replacement, bool regex, bool preserveCase)
    {
        m_previewActive = active;
        m_replacement = replacement;
        m_regexMode = regex;
        m_preserveCase = preserveCase;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool file = index.data(KindRole).toInt() == FileRow;
        const int text = QFontMetrics(resultFont(option.font, file ? 0 : -1)).height();
        return QSize(0, std::max(file ? kFileRowMinHeight : kMatchRowMinHeight,
                                 text + (file ? 8 : 6)));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& base,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem option(base);
        option.font = resultFont(base.font);
        ensureIcons();
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing);
        const QRect row = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool file = index.data(KindRole).toInt() == FileRow;

        const QColor text = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x22, 0x22, 0x22);
        QColor muted = text;
        muted.setAlphaF(0.6f);

        ThemeCard::paintRow(*painter, row, m_dark, selected, hovered);

        const int textX = row.left() + kLeftPad + kChevronColumn + kTextGap;
        const bool showAction = m_actionsEnabled && (hovered || selected);
        const int rightEdge = row.right() - kRightPad
            - (showAction ? kActionSize + kTextGap : 0);

        if (file) {
            const bool open = option.state & QStyle::State_Open;
            const QIcon& chevron = open ? m_chevronOpen : m_chevronClosed;
            chevron.paint(painter,
                          QRect(row.left() + kLeftPad, row.center().y() - kIconPx / 2 + 2,
                                kIconPx, kIconPx),
                          Qt::AlignCenter);

            QFont bold = resultFont(base.font);
            bold.setWeight(QFont::DemiBold);
            const QFontMetrics boldMetrics(bold);
            const int count = index.data(CountRole).toInt();

            int badgeWidth = 0;
            QFont countFont = option.font;
            countFont.setPointSizeF(std::max(6.0, option.font.pointSizeF() * 0.8 - 0.75));
            const QFontMetrics countMetrics(countFont);
            const QString countText = QString::number(count);
            if (!showAction) {
                badgeWidth = std::max(kActionSize, countMetrics.horizontalAdvance(countText) + 10);
                const QRect badge(row.right() - kRightPad - badgeWidth + 1,
                                  row.center().y() - 8, badgeWidth, 16);
                QColor fill = text;
                fill.setAlphaF(0.14f);
                painter->setPen(Qt::NoPen);
                painter->setBrush(fill);
                painter->drawRoundedRect(badge, 8, 8);
                painter->setPen(text);
                painter->setFont(countFont);
                painter->drawText(badge, Qt::AlignCenter, countText);
            }

            const int avail = std::max(0, rightEdge - (badgeWidth ? badgeWidth + kTextGap : 0) - textX);
            const QString name = index.data(Qt::DisplayRole).toString();
            const QString elidedName = fitText(boldMetrics, name, Qt::ElideMiddle, avail);
            painter->setFont(bold);
            painter->setPen(text);
            const QRect nameRect(textX, row.top(), avail, row.height());
            painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, elidedName);

            const QString dir = index.data(DirRole).toString();
            const int nameWidth = boldMetrics.horizontalAdvance(elidedName);
            const int dirAvail = avail - nameWidth - 8;
            if (!dir.isEmpty() && dirAvail > 24) {
                painter->setFont(option.font);
                painter->setPen(muted);
                const QString elidedDir = fitText(QFontMetrics(option.font), dir, Qt::ElideLeft, dirAvail);
                painter->drawText(QRect(textX + nameWidth + 8, row.top(), dirAvail, row.height()),
                                  Qt::AlignVCenter | Qt::AlignLeft, elidedDir);
            }
        } else {
            option.font = resultFont(base.font, -1);
            paintMatch(painter, option, index, textX, rightEdge, text);
        }

        if (showAction) {
            const QIcon& icon = file ? m_replaceAll : m_replace;
            icon.paint(painter, actionRectFor(row), Qt::AlignCenter);
        }
        painter->restore();
    }

private:
    // The rows read at a larger size than the surrounding panel text: file names
    // and previews are the content of this view, not chrome. Scaling the inherited
    // font keeps the panel's own family and its DPI behaviour.
    static QFont resultFont(const QFont& base, int deltaPx = 0)
    {
        QFont font = base;
        constexpr qreal kScale = 1.2;
        if (font.pointSizeF() > 0)
            font.setPointSizeF(font.pointSizeF() * kScale);
        else if (font.pixelSize() > 0)
            font.setPixelSize(qRound(font.pixelSize() * kScale));
        if (deltaPx != 0) {
            if (font.pixelSize() > 0)
                font.setPixelSize(std::max(1, font.pixelSize() + deltaPx));
            else
                font.setPointSizeF(std::max(1.0, font.pointSizeF() + deltaPx * 72.0 / 96.0));
        }
        return font;
    }

    void paintMatch(QPainter* painter, const QStyleOptionViewItem& option,
                    const QModelIndex& index, int textX, int rightEdge, const QColor& text) const
    {
        const QString preview = index.data(PreviewRole).toString();
        const int start = std::clamp(index.data(PreviewStartRole).toInt(), 0, int(preview.size()));
        const int length = std::clamp(index.data(PreviewLengthRole).toInt(), 0,
                                      int(preview.size()) - start);
        const QFontMetrics fm(option.font);
        const int avail = std::max(0, rightEdge - textX);

        const QString before = preview.left(start);
        const QString hit = preview.mid(start, length);
        const QString after = preview.mid(start + length);

        // Replace preview: the old match and its exact replacement (the shared
        // expansion, run on the stored captures) sit side by side. Display only.
        QString added;
        if (m_previewActive) {
            added = WorkspaceTextSearch::expandReplacementFromCaptures(
                m_replacement, index.data(CapturesRole).toStringList(), m_regexMode,
                m_preserveCase);
            added.replace(QLatin1Char('\n'), QLatin1Char(' '))
                .replace(QLatin1Char('\t'), QLatin1Char(' '));
        }

        // The match (and its replacement) keep full width; the text around it
        // gives way, the leading side first, so they are never what is elided.
        const int hitWidth = std::min(fm.horizontalAdvance(hit), avail);
        const int addedWidth = std::min(fm.horizontalAdvance(added), std::max(0, avail - hitWidth));
        const int remaining = std::max(0, avail - hitWidth - addedWidth);
        const int beforeWidth = fm.horizontalAdvance(before);
        const int afterWidth = fm.horizontalAdvance(after);
        int beforeBudget = beforeWidth;
        int afterBudget = afterWidth;
        if (beforeWidth + afterWidth > remaining) {
            beforeBudget = std::min(beforeWidth, std::max(remaining / 3, remaining - afterWidth));
            afterBudget = remaining - beforeBudget;
        }

        const QString drawBefore = fitText(fm, before, Qt::ElideLeft, beforeBudget);
        const QString drawHit = fitText(fm, hit, Qt::ElideRight, hitWidth);
        const QString drawAdded = fitText(fm, added, Qt::ElideRight, addedWidth);
        const QString drawAfter = fitText(fm, after, Qt::ElideRight, afterBudget);

        const QRect row = option.rect;
        int x = textX;
        painter->setFont(option.font);
        painter->setPen(text);
        painter->drawText(QRect(x, row.top(), beforeBudget + 2, row.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, drawBefore);
        x += fm.horizontalAdvance(drawBefore);

        const int shownHit = fm.horizontalAdvance(drawHit);
        const QRect hitRect(x, row.center().y() - fm.height() / 2, shownHit, fm.height());
        QColor fill;
        if (m_previewActive) {
            fill = m_dark ? QColor(0x6b, 0x2b, 0x2b, 200) : QColor(0xfb, 0xc9, 0xc9, 220);
        } else {
            // The editor's own "other match" find colours, so a hit reads the
            // same in the results and in the document it opens.
            fill = m_dark ? QColor(0x62, 0x33, 0x15, 180) : QColor(0xff, 0xe9, 0xa8, 220);
        }
        painter->fillRect(hitRect, fill);
        if (m_previewActive) {
            QFont struck = option.font;
            struck.setStrikeOut(true);
            painter->setFont(struck);
        }
        painter->drawText(hitRect, Qt::AlignVCenter | Qt::AlignLeft, drawHit);
        painter->setFont(option.font);
        x += shownHit;

        if (m_previewActive && !drawAdded.isEmpty()) {
            const int shownAdded = fm.horizontalAdvance(drawAdded);
            const QRect addedRect(x, row.center().y() - fm.height() / 2, shownAdded, fm.height());
            painter->fillRect(addedRect, m_dark ? QColor(0x2a, 0x5a, 0x33, 210)
                                                : QColor(0xc4, 0xec, 0xc4, 230));
            painter->drawText(addedRect, Qt::AlignVCenter | Qt::AlignLeft, drawAdded);
            x += shownAdded;
        }

        painter->drawText(QRect(x, row.top(), std::max(0, textX + avail - x), row.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, drawAfter);
    }

    void ensureIcons() const
    {
        if (m_iconsReady)
            return;
        const QColor normal = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x42, 0x42, 0x42);
        m_chevronOpen = IconHelper::materialIconTight(QChar(kIconExpandMore), normal, kIconPx, 1);
        m_chevronClosed = IconHelper::materialIconTight(QChar(kIconChevronRight), normal, kIconPx, 1);
        m_replace = IconHelper::materialIconTight(QChar(kIconFindReplace), normal, kIconPx);
        m_replaceAll = IconHelper::materialIconTight(QChar(kIconDoneAll), normal, kIconPx);
        m_iconsReady = true;
    }

    bool m_dark = false;
    bool m_actionsEnabled = false;
    bool m_previewActive = false;
    bool m_regexMode = false;
    bool m_preserveCase = false;
    QString m_replacement;
    mutable bool m_iconsReady = false;
    mutable QIcon m_chevronOpen, m_chevronClosed;
    mutable QIcon m_replace, m_replaceAll;

};

// ---------------------------------------------------------------------------
class SearchResultsView : public QTreeView {
public:
    explicit SearchResultsView(WorkspaceSearchPanel* panel)
        : QTreeView(panel), m_panel(panel)
    {
        setObjectName(QStringLiteral("workspaceSearchResults"));
        setHeaderHidden(true);
        setRootIsDecorated(false);
        setIndentation(0);
        setUniformRowHeights(false);
        setExpandsOnDoubleClick(false);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setMouseTracking(true);
        setFrameShape(QFrame::NoFrame);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_MacShowFocusRect, false);
    }

    void setActionsEnabled(bool enabled)
    {
        m_actionsEnabled = enabled;
        viewport()->update();
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const QModelIndex index = indexAt(event->position().toPoint());
            if (index.isValid()) {
                if (m_actionsEnabled
                    && actionRectFor(visualRect(index)).contains(event->position().toPoint())) {
                    setCurrentIndex(index);
                    m_panel->replaceAtIndex(index);
                    event->accept();
                    return;
                }
                QTreeView::mouseReleaseEvent(event);
                m_panel->activateIndex(index);
                return;
            }
        }
        QTreeView::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        const QModelIndex index = currentIndex();
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && index.isValid()) {
            m_panel->activateIndex(index);
            return;
        }
        if (event->key() == Qt::Key_1
            && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
            && index.isValid()) {
            m_panel->replaceAtIndex(index);
            return;
        }
        QTreeView::keyPressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        m_panel->showContextMenu(viewport()->mapFromGlobal(event->globalPos()));
        event->accept();
    }

    bool viewportEvent(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const QModelIndex index = indexAt(help->pos());
            if (index.isValid()) {
                if (m_actionsEnabled && actionRectFor(visualRect(index)).contains(help->pos())) {
                    QToolTip::showText(help->globalPos(),
                        index.data(KindRole).toInt() == FileRow
                            ? tr("Replace All in File") : tr("Replace (Ctrl+Shift+1)"), this);
                } else {
                    QToolTip::showText(help->globalPos(),
                        QDir::toNativeSeparators(index.data(PathRole).toString()), this);
                }
                return true;
            }
            QToolTip::hideText();
            return true;
        }
        return QTreeView::viewportEvent(event);
    }

private:
    WorkspaceSearchPanel* m_panel;
    bool m_actionsEnabled = false;
};

// ---------------------------------------------------------------------------
WorkspaceSearchPanel::WorkspaceSearchPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workspaceSearchPanel"));
    buildUi();
    applyFixedMetrics();
    updateIcons();

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);
    connect(m_searchDebounce, &QTimer::timeout, this, &WorkspaceSearchPanel::startSearch);

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(500);
    connect(m_refreshDebounce, &QTimer::timeout, this, &WorkspaceSearchPanel::refreshDirtyFiles);

    connect(&m_watcher, &QFutureWatcher<RunSummary>::finished, this, [this]() {
        m_running = false;
        if (m_restartPending) {
            m_restartPending = false;
            startSearch();
            return;
        }
        const RunSummary summary = m_watcher.result();
        finishSearch(m_generation, summary);
    });
    connect(&m_replaceWatcher, &QFutureWatcher<ReplaceOutcome>::finished, this, [this]() {
        m_replacing = false;
        setEnabled(true);
        const ReplaceOutcome outcome = m_replaceWatcher.result();
        reportOutcome(outcome);
        m_keepNoteOnce = true;
        startSearch();
    });
}

WorkspaceSearchPanel::~WorkspaceSearchPanel()
{
    // The workers post queued calls back to this object; wait for them rather
    // than leave them a dangling receiver.
    if (m_cancel)
        m_cancel->storeRelaxed(1);
    m_watcher.disconnect(this);
    m_replaceWatcher.disconnect(this);
    m_watcher.waitForFinished();
    m_replaceWatcher.waitForFinished();
}

static QToolButton* makeSearchButton(QWidget* parent, const QString& objectName, bool checkable)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setIconSize(QSize(kIconPx, kIconPx));
    return button;
}

static QWidget* makeSearchSeparator(QWidget* parent)
{
    auto* line = new QWidget(parent);
    line->setObjectName(QStringLiteral("findOverlaySeparator"));
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setFixedWidth(1);
    line->setFixedHeight(16);
    return line;
}

void WorkspaceSearchPanel::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // The controls reuse FindOverlay's object names, so the field, its border,
    // focus and invalid states and the option buttons come from the same QSS
    // rules and cannot drift from the document Find they are meant to match.
    auto* inputArea = new QWidget(this);
    m_grid = new QGridLayout(inputArea);
    m_grid->setContentsMargins(8, 8, 8, 6);
    m_grid->setHorizontalSpacing(kTightGap);
    m_grid->setVerticalSpacing(0);

    m_chevronButton = makeSearchButton(inputArea, QStringLiteral("findOverlayChevron"), true);
    m_chevronButton->setFixedWidth(kChevronWidth);
    m_chevronButton->setToolTip(tr("Toggle Replace (Ctrl+Shift+H)"));
    connect(m_chevronButton, &QToolButton::toggled, this, &WorkspaceSearchPanel::setReplaceExpanded);
    m_grid->addWidget(m_chevronButton, 0, 0, 3, 1);

    m_queryRow = new QWidget(inputArea);
    m_queryRow->setObjectName(QStringLiteral("findOverlayInputRow"));
    m_queryRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* queryLayout = new QHBoxLayout(m_queryRow);
    queryLayout->setContentsMargins(kBorder + 6, kBorder, kBorder + 3, kBorder);
    queryLayout->setSpacing(kTightGap);

    m_queryEdit = new QLineEdit(m_queryRow);
    m_queryEdit->setObjectName(QStringLiteral("findOverlayEdit"));
    m_queryEdit->setFrame(false);
    m_queryEdit->setPlaceholderText(tr("Search in Files"));
    m_queryEdit->installEventFilter(this);
    connect(m_queryEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        m_transientNote.clear();
        scheduleSearch();
    });
    queryLayout->addWidget(m_queryEdit, 1);

    m_queryClearButton = new QToolButton(m_queryRow);
    m_queryClearButton->setObjectName(QStringLiteral("workspaceSearchClearButton"));
    m_queryClearButton->setIconSize(QSize(12, 12));
    m_queryClearButton->setFixedSize(18, 18);
    m_queryClearButton->setAutoRaise(true);
    m_queryClearButton->setFocusPolicy(Qt::NoFocus);
    m_queryClearButton->setCursor(Qt::PointingHandCursor);
    m_queryClearButton->setToolTip(tr("Clear Search"));
    m_queryClearButton->setVisible(false);
    connect(m_queryClearButton, &QToolButton::clicked, this, [this] {
        m_queryEdit->clear();
        m_queryEdit->setFocus(Qt::OtherFocusReason);
    });
    connect(m_queryEdit, &QLineEdit::textChanged, m_queryClearButton,
            [this](const QString& text) { m_queryClearButton->setVisible(!text.isEmpty()); });
    queryLayout->addWidget(m_queryClearButton, 0, Qt::AlignVCenter);

    queryLayout->addSpacing(kGroupGap - kTightGap);
    QWidget* querySeparator = makeSearchSeparator(m_queryRow);
    m_separators.append(querySeparator);
    queryLayout->addWidget(querySeparator, 0, Qt::AlignVCenter);
    queryLayout->addSpacing(kGroupGap - kTightGap);

    m_matchCaseButton = makeSearchButton(m_queryRow, QStringLiteral("findOverlayOption"), true);
    m_wholeWordButton = makeSearchButton(m_queryRow, QStringLiteral("findOverlayOption"), true);
    m_regexButton = makeSearchButton(m_queryRow, QStringLiteral("findOverlayOption"), true);
    m_matchCaseButton->setToolTip(tr("Match Case (Alt+C)"));
    m_wholeWordButton->setToolTip(tr("Match Whole Word (Alt+W)"));
    m_regexButton->setToolTip(tr("Use Regular Expression (Alt+R)"));
    for (QToolButton* button : {m_matchCaseButton, m_wholeWordButton, m_regexButton}) {
        queryLayout->addWidget(button, 0, Qt::AlignVCenter);
        connect(button, &QToolButton::toggled, this, [this]() {
            m_transientNote.clear();
            scheduleSearch();
        });
    }
    m_grid->addWidget(m_queryRow, 0, 2);

    m_replaceRow = new QWidget(inputArea);
    m_replaceRow->setObjectName(QStringLiteral("findOverlayInputRow"));
    m_replaceRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(kBorder + 6, kBorder, kBorder + 3, kBorder);
    replaceLayout->setSpacing(kTightGap);

    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setObjectName(QStringLiteral("findOverlayEdit"));
    m_replaceEdit->setFrame(false);
    m_replaceEdit->setPlaceholderText(tr("Replace"));
    m_replaceEdit->installEventFilter(this);
    replaceLayout->addWidget(m_replaceEdit, 1);

    replaceLayout->addSpacing(kGroupGap - kTightGap);
    QWidget* replaceSeparator = makeSearchSeparator(m_replaceRow);
    m_separators.append(replaceSeparator);
    replaceLayout->addWidget(replaceSeparator, 0, Qt::AlignVCenter);
    replaceLayout->addSpacing(kGroupGap - kTightGap);

    m_preserveCaseButton = makeSearchButton(m_replaceRow, QStringLiteral("findOverlayOption"), true);
    m_preserveCaseButton->setToolTip(tr("Preserve Case (Alt+P)"));
    replaceLayout->addWidget(m_preserveCaseButton, 0, Qt::AlignVCenter);

    m_replaceAllButton = makeSearchButton(m_replaceRow, QStringLiteral("findOverlayAction"), false);
    m_replaceAllButton->setToolTip(tr("Replace All (Ctrl+Alt+Enter)"));
    connect(m_replaceAllButton, &QToolButton::clicked, this,
            &WorkspaceSearchPanel::replaceAllInWorkspace);
    replaceLayout->addWidget(m_replaceAllButton, 0, Qt::AlignVCenter);
    m_grid->addWidget(m_replaceRow, 2, 2);

    m_grid->setColumnMinimumWidth(1, kInnerPad - 2 * kTightGap);
    m_grid->setColumnStretch(2, 1);
    m_grid->setRowMinimumHeight(0, kRowHeight);
    m_replaceRow->setVisible(false);
    outer->addWidget(inputArea);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("workspaceSearchStatus"));
    m_statusLabel->setContentsMargins(10, 2, 8, 4);
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    outer->addWidget(m_statusLabel);

    m_model = new QStandardItemModel(this);
    m_results = new SearchResultsView(this);
    m_delegate = new SearchResultDelegate(m_results);
    m_results->setModel(m_model);
    m_results->setItemDelegate(m_delegate);
    connect(m_replaceEdit, &QLineEdit::textChanged, this, [this] { updateReplacePreview(); });
    connect(m_preserveCaseButton, &QToolButton::toggled, this, [this] { updateReplacePreview(); });
    connect(m_results, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        m_collapsed.remove(pathKey(index.data(PathRole).toString()));
    });
    connect(m_results, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        m_collapsed.insert(pathKey(index.data(PathRole).toString()));
    });
    outer->addWidget(m_results, 1);

    const auto addShortcut = [this](const QKeySequence& sequence, auto handler) {
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_C), [this] { m_matchCaseButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_W), [this] { m_wholeWordButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_R), [this] { m_regexButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_P), [this] {
        if (m_replaceExpanded)
            m_preserveCaseButton->toggle();
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return),
                [this] { replaceAllInWorkspace(); });

    updateActionStates();
}

void WorkspaceSearchPanel::applyFixedMetrics()
{
    // qApp->setStyleSheet() repolishes every widget and drops programmatic fixed
    // sizes, so they are re-asserted on StyleChange (FindOverlay does the same).
    m_queryRow->setFixedHeight(kRowHeight);
    m_replaceRow->setFixedHeight(kRowHeight);
    m_chevronButton->setFixedSize(kChevronWidth,
                                  m_replaceExpanded ? kRowHeight * 2 + kRowGap : kRowHeight);
    m_chevronButton->setIconSize(QSize(kIconPx, kIconPx));
    for (QToolButton* button : {m_matchCaseButton, m_wholeWordButton, m_regexButton,
                                m_preserveCaseButton, m_replaceAllButton}) {
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setIconSize(QSize(kIconPx, kIconPx));
    }
    m_queryClearButton->setFixedSize(18, 18);
    m_queryClearButton->setIconSize(QSize(12, 12));
    for (QWidget* separator : m_separators)
        separator->setFixedSize(1, 16);
    m_grid->setRowMinimumHeight(1, m_replaceExpanded ? kRowGap : 0);
}

void WorkspaceSearchPanel::updateIcons()
{
    const QColor color = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x42, 0x42, 0x42);
    m_chevronButton->setIcon(IconHelper::materialIconTight(
        QChar(m_replaceExpanded ? kIconExpandMore : kIconChevronRight), color, kIconPx, 2));
    m_queryClearButton->setIcon(IconHelper::materialIcon(
        QChar(kIconClose), m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44), 12));
    m_matchCaseButton->setIcon(IconHelper::textIcon(QStringLiteral("Aa"), color, kIconPx, true, 0, QStringLiteral("ab")));
    m_wholeWordButton->setIcon(IconHelper::textIcon(QStringLiteral("ab"), color, kIconPx, true));
    m_regexButton->setIcon(IconHelper::textIcon(QStringLiteral(".*"), color, kIconPx, true, 0, QStringLiteral("ab")));
    m_preserveCaseButton->setIcon(IconHelper::textIcon(QStringLiteral("AB"), color, kIconPx, true));
    m_replaceAllButton->setIcon(IconHelper::materialIconTight(QChar(kIconDoneAll), color, kIconPx));
}

void WorkspaceSearchPanel::setDarkMode(bool dark)
{
    m_dark = dark;
    m_delegate->setDark(dark);
    m_results->setStyleSheet(QStringLiteral(
        "QTreeView#workspaceSearchResults { background: %1; color: %2; border: none; outline: none; }")
        .arg(ThemePalette::color(ThemePalette::Surface, dark).name(),
             dark ? QStringLiteral("#cccccc") : QStringLiteral("#222222")));
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel#workspaceSearchStatus { color: %1; background: transparent; }")
        .arg(dark ? QStringLiteral("#a0a0a0") : QStringLiteral("#6e6e6e")));
    updateIcons();
    applyFixedMetrics();
    m_results->viewport()->update();
}

void WorkspaceSearchPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange)
        applyFixedMetrics();
}

void WorkspaceSearchPanel::focusQuery()
{
    m_queryEdit->setFocus(Qt::ShortcutFocusReason);
    m_queryEdit->selectAll();
}

QString WorkspaceSearchPanel::queryText() const
{
    return m_queryEdit->text();
}

void WorkspaceSearchPanel::setQueryText(const QString& text)
{
    if (m_queryEdit->text() == text)
        return;
    m_queryEdit->setText(text);
}

void WorkspaceSearchPanel::setReplaceExpanded(bool expanded)
{
    if (m_replaceExpanded == expanded && m_chevronButton->isChecked() == expanded)
        return;
    m_replaceExpanded = expanded;
    m_replaceRow->setVisible(expanded);
    if (m_chevronButton->isChecked() != expanded) {
        const QSignalBlocker blocker(m_chevronButton);
        m_chevronButton->setChecked(expanded);
    }
    m_delegate->setActionsEnabled(expanded);
    m_results->setActionsEnabled(expanded);
    updateReplacePreview();
    updateIcons();
    applyFixedMetrics();
    updateActionStates();
}

void WorkspaceSearchPanel::updateReplacePreview()
{
    // Uses the executed query's regex flag: that is what produced the captures.
    m_delegate->setReplacePreview(m_replaceExpanded && !m_replaceEdit->text().isEmpty(),
                                  m_replaceEdit->text(), m_executed.options.regex,
                                  m_preserveCaseButton->isChecked());
    m_results->viewport()->update();
}

void WorkspaceSearchPanel::updateActionStates()
{
    m_replaceAllButton->setEnabled(m_hasResults && !m_running && !m_replacing
                                   && m_model->rowCount() > 0);
}

// ---------------------------------------------------------------------------
void WorkspaceSearchPanel::setRootPath(const QString& root)
{
    if (root == m_root)
        return;
    m_root = root;
    cancelSearch();
    m_dirtyPaths.clear();
    m_collapsed.clear();
    clearResults();
    setStatus(QString());
    if (!m_queryEdit->text().isEmpty())
        setStatus(tr("Press Enter to search this folder."));
}

Query WorkspaceSearchPanel::currentQuery() const
{
    Query query;
    query.text = m_queryEdit->text();
    query.options.matchCase = m_matchCaseButton->isChecked();
    query.options.wholeWord = m_wholeWordButton->isChecked();
    query.options.regex = m_regexButton->isChecked();
    return query;
}

void WorkspaceSearchPanel::scheduleSearch()
{
    m_searchDebounce->start();
}

void WorkspaceSearchPanel::cancelSearch()
{
    m_searchDebounce->stop();
    m_restartPending = false;
    if (m_cancel)
        m_cancel->storeRelaxed(1);
}

void WorkspaceSearchPanel::clearResults()
{
    m_model->removeRows(0, m_model->rowCount());
    m_hasResults = false;
    m_capped = false;
    m_skipped = 0;
    updateActionStates();
}

void WorkspaceSearchPanel::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
    m_statusLabel->setVisible(!text.isEmpty());
}

void WorkspaceSearchPanel::updateInvalidState(const QString& error)
{
    m_error = error;
    const bool invalid = !error.isEmpty();
    if (m_queryRow->property("invalid").toBool() != invalid) {
        m_queryRow->setProperty("invalid", invalid);
        repolish(m_queryRow);
    }
    m_queryEdit->setToolTip(error);
}

void WorkspaceSearchPanel::startSearch()
{
    m_searchDebounce->stop();
    if (m_replacing)
        return;
    if (m_root.isEmpty()) {
        setStatus(tr("Open a folder to search in it."));
        return;
    }

    const Query query = currentQuery();
    if (query.text.isEmpty()) {
        cancelSearch();
        updateInvalidState(QString());
        if (!m_running)
            clearResults();
        m_executed = Query();
        setStatus(QString());
        return;
    }

    QString error;
    const QRegularExpression re = buildRegex(query, &error);
    if (!re.isValid()) {
        // The previous results stay: a half-typed pattern must not wipe them.
        updateInvalidState(error);
        setStatus(tr("Invalid regular expression: %1").arg(error));
        return;
    }
    updateInvalidState(QString());

    if (m_running) {
        m_restartPending = true;
        m_cancel->storeRelaxed(1);
        return;
    }

    ++m_generation;
    clearResults();
    if (m_keepNoteOnce)
        m_keepNoteOnce = false;
    else
        m_transientNote.clear();
    m_executed = query;
    m_executedRegex = re;
    updateReplacePreview();
    m_hasResults = true;
    setStatus(tr("Searching..."));

    const QString rootKey = pathKey(m_root);
    QHash<QString, QString> snapshots;
    if (m_openEditors) {
        for (const OpenEditor& open : m_openEditors()) {
            if (!open.editor || open.editor->isReadOnly() || open.path.isEmpty())
                continue;
            if (!pathKey(open.path).startsWith(rootKey + QLatin1Char('/')))
                continue;
            snapshots.insert(pathKey(open.path), open.editor->toPlainText());
        }
    }

    m_cancel = std::make_shared<QAtomicInt>(0);
    const auto cancel = m_cancel;
    const quint64 generation = m_generation;
    const QString root = m_root;
    const QPointer<WorkspaceSearchPanel> guard(this);
    m_running = true;
    updateActionStates();

    m_watcher.setFuture(QtConcurrent::run([guard, cancel, generation, root, re, snapshots]() {
        QList<FileResult> batch;
        QElapsedTimer sinceFlush;
        sinceFlush.start();
        const auto flush = [&]() {
            if (batch.isEmpty() || !guard)
                return;
            const QList<FileResult> copy = batch;
            batch.clear();
            QMetaObject::invokeMethod(guard.data(), [guard, generation, copy]() {
                if (guard)
                    guard->appendBatch(generation, copy);
            }, Qt::QueuedConnection);
            sinceFlush.restart();
        };

        const TextLoader loader = [](const QString& path, QString* text) {
            WorkspaceSearchFiles::LoadedText file;
            if (!WorkspaceSearchFiles::load(path, &file, kMaxFileBytes))
                return false;
            *text = std::move(file.text);
            return true;
        };
        const RunSummary summary = WorkspaceTextSearch::run(
            root, re, snapshots, loader,
            [cancel]() { return cancel->loadRelaxed() != 0; },
            [&](const FileResult& result) {
                batch.append(result);
                if (batch.size() >= 40 || sinceFlush.elapsed() >= 60)
                    flush();
            });
        flush();
        return summary;
    }));
}

void WorkspaceSearchPanel::appendBatch(quint64 generation, const QList<FileResult>& batch)
{
    if (generation != m_generation)
        return;
    for (const FileResult& result : batch)
        addFileGroup(result);
    updateSummaryStatus();
}

void WorkspaceSearchPanel::finishSearch(quint64 generation, const RunSummary& summary)
{
    if (generation != m_generation)
        return;
    m_capped = summary.capped;
    m_skipped = summary.skipped;
    updateSummaryStatus();
    updateActionStates();
}

void WorkspaceSearchPanel::updateSummaryStatus()
{
    int files = 0;
    int matches = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        ++files;
        matches += m_model->item(i)->data(CountRole).toInt();
    }

    QString text;
    if (m_running) {
        text = matches == 0 ? tr("Searching...")
                            : tr("Searching... %1 results so far").arg(matches);
    } else if (matches == 0) {
        text = tr("No results found.");
    } else {
        text = (matches == 1 ? tr("1 result") : tr("%1 results").arg(matches))
            + QLatin1Char(' ')
            + (files == 1 ? tr("in 1 file") : tr("in %1 files").arg(files));
        if (m_capped)
            text += tr(" (limited to %1)").arg(kMaxTotalMatches);
    }
    if (!m_transientNote.isEmpty())
        text += QStringLiteral("  |  ") + m_transientNote;
    setStatus(text);
    updateActionStates();
}

// ---------------------------------------------------------------------------
static void fillFileItem(QStandardItem* file, const FileResult& result, const QString& root)
{
    file->setEditable(false);
    file->setData(FileRow, KindRole);
    file->setData(result.path, PathRole);
    file->setData(int(result.matches.size()), CountRole);
    file->setData(relativeDirectory(root, result.path), DirRole);
    file->setText(QFileInfo(result.path).fileName());

    for (const Match& match : result.matches) {
        auto* item = new QStandardItem;
        item->setEditable(false);
        item->setData(MatchRow, KindRole);
        item->setData(result.path, PathRole);
        item->setData(match.line, LineRole);
        item->setData(match.column, ColumnRole);
        item->setData(match.length, LengthRole);
        item->setData(match.preview, PreviewRole);
        item->setData(match.previewStart, PreviewStartRole);
        item->setData(match.previewLength, PreviewLengthRole);
        item->setData(match.captures, CapturesRole);
        file->appendRow(item);
    }
}

QStandardItem* WorkspaceSearchPanel::addFileGroup(const FileResult& result)
{
    auto* file = new QStandardItem;
    fillFileItem(file, result, m_root);
    m_model->appendRow(file);
    m_results->setExpanded(file->index(), !m_collapsed.contains(pathKey(result.path)));
    return file;
}

QStandardItem* WorkspaceSearchPanel::findFileItem(const QString& path) const
{
    const QString key = pathKey(path);
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (pathKey(item->data(PathRole).toString()) == key)
            return item;
    }
    return nullptr;
}

QPlainTextEdit* WorkspaceSearchPanel::editorFor(const QString& path) const
{
    if (!m_openEditors)
        return nullptr;
    const QString key = pathKey(path);
    for (const OpenEditor& open : m_openEditors()) {
        if (open.editor && pathKey(open.path) == key)
            return open.editor;
    }
    return nullptr;
}

bool WorkspaceSearchPanel::currentLineText(const QString& path, int line, QString* text) const
{
    if (QPlainTextEdit* editor = editorFor(path)) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            return false;
        *text = block.text();
        return true;
    }
    WorkspaceSearchFiles::LoadedText file;
    if (!WorkspaceSearchFiles::load(path, &file, kMaxFileBytes))
        return false;
    qsizetype start = 0;
    for (int i = 0; i < line; ++i) {
        const qsizetype next = file.text.indexOf(QLatin1Char('\n'), start);
        if (next < 0)
            return false;
        start = next + 1;
    }
    qsizetype end = file.text.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
        end = file.text.size();
    *text = file.text.mid(start, end - start);
    return true;
}

void WorkspaceSearchPanel::notifyDocumentEdited(const QString& path)
{
    if (!m_hasResults || m_root.isEmpty() || m_running || m_replacing)
        return;
    if (!pathKey(path).startsWith(pathKey(m_root) + QLatin1Char('/')))
        return;
    m_dirtyPaths.insert(path);
    m_refreshDebounce->start();
}

void WorkspaceSearchPanel::refreshDirtyFiles()
{
    const QSet<QString> paths = std::exchange(m_dirtyPaths, {});
    for (const QString& path : paths)
        refreshFile(path);
}

void WorkspaceSearchPanel::refreshFile(const QString& path)
{
    if (!m_hasResults || !m_executedRegex.isValid() || m_running)
        return;

    QString text;
    bool readable = false;
    if (QPlainTextEdit* editor = editorFor(path)) {
        text = editor->toPlainText();
        readable = true;
    } else {
        WorkspaceSearchFiles::LoadedText file;
        readable = WorkspaceSearchFiles::load(path, &file, kMaxFileBytes);
        if (readable)
            text = std::move(file.text);
    }

    FileResult result;
    result.path = path;
    if (readable)
        searchText(text, m_executedRegex, kMaxTotalMatches, &result.matches);

    QStandardItem* existing = findFileItem(path);
    if (result.matches.isEmpty()) {
        if (existing)
            m_model->removeRow(existing->row());
    } else if (existing) {
        existing->removeRows(0, existing->rowCount());
        fillFileItem(existing, result, m_root);
        m_results->setExpanded(existing->index(), !m_collapsed.contains(pathKey(path)));
    } else {
        addFileGroup(result);
    }
    updateSummaryStatus();
}

// ---------------------------------------------------------------------------
void WorkspaceSearchPanel::activateIndex(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    if (index.data(KindRole).toInt() == FileRow) {
        m_results->setExpanded(index, !m_results->isExpanded(index));
        return;
    }

    const QString path = index.data(PathRole).toString();
    const int line = index.data(LineRole).toInt();
    const int column = index.data(ColumnRole).toInt();
    const int length = index.data(LengthRole).toInt();

    QString lineText;
    if (!currentLineText(path, line, &lineText)
        || !replacementForMatch(lineText, column, length, m_executedRegex, m_executed,
                                QString(), nullptr)) {
        // The document changed underneath the result: refresh it instead of
        // sending the caret to text that is no longer a match.
        m_transientNote = tr("That result was out of date and has been refreshed.");
        refreshFile(path);
        return;
    }
    emit openMatchRequested(path, line, column, length);
}

void WorkspaceSearchPanel::showContextMenu(const QPoint& viewportPos)
{
    const QModelIndex index = m_results->indexAt(viewportPos);
    if (!index.isValid())
        return;
    m_results->setCurrentIndex(index);
    const bool file = index.data(KindRole).toInt() == FileRow;

    AlignedMenu menu(this);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    QAction* open = menu.addAction(file ? tr("Expand / Collapse") : tr("Open"));
    QAction* replace = menu.addAction(file ? tr("Replace All in File") : tr("Replace"));
    replace->setEnabled(m_replaceExpanded);
    if (!file)
        replace->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1));
    QAction* chosen = menu.exec(m_results->viewport()->mapToGlobal(viewportPos));
    if (chosen == open)
        activateIndex(index);
    else if (chosen == replace)
        replaceAtIndex(index);
}

// ---------------------------------------------------------------------------
Query WorkspaceSearchPanel::replaceQuery() const
{
    Query query = m_executed;
    query.options.preserveCase = m_preserveCaseButton->isChecked();
    return query;
}

WorkspaceSearchPanel::ReplaceOutcome WorkspaceSearchPanel::replaceInEditor(
    QPlainTextEdit* editor, const QList<Target>& requested)
{
    ReplaceOutcome outcome;
    outcome.files = 1;
    if (!editor || editor->isReadOnly()) {
        outcome.skipped = int(requested.size());
        outcome.failedFiles = 1;
        return outcome;
    }

    QList<Target> targets = requested;
    std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) {
        return a.line != b.line ? a.line > b.line : a.column > b.column;
    });

    const QString replacement = m_replaceEdit->text();
    const Query query = replaceQuery();
    QTextDocument* document = editor->document();
    // One edit block on one cursor: a single Ctrl+Z restores the whole file, and
    // later (earlier-in-document) offsets stay valid because we go back to front.
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (const Target& target : targets) {
        const QTextBlock block = document->findBlockByNumber(target.line);
        QString expanded;
        if (!block.isValid()
            || !replacementForMatch(block.text(), target.column, target.length, m_executedRegex,
                                    query, replacement, &expanded)) {
            ++outcome.skipped;
            continue;
        }
        cursor.setPosition(block.position() + target.column);
        cursor.setPosition(block.position() + target.column + target.length,
                           QTextCursor::KeepAnchor);
        cursor.insertText(expanded);
        ++outcome.replaced;
    }
    cursor.endEditBlock();

    if (outcome.replaced > 0 && m_editedHook)
        m_editedHook(editor);
    return outcome;
}

static WorkspaceSearchPanel::ReplaceOutcome replaceInClosedFile(
    const QString& path, const Query& query, const QRegularExpression& re,
    const QString& replacement, const WorkspaceSearchPanel::Target* one)
{
    WorkspaceSearchPanel::ReplaceOutcome outcome;
    outcome.files = 1;
    WorkspaceSearchFiles::LoadedText file;
    if (!WorkspaceSearchFiles::load(path, &file, kMaxFileBytes)) {
        outcome.failedFiles = 1;
        return outcome;
    }

    QString result;
    int replaced = 0;
    if (one) {
        if (replaceOne(file.text, one->line, one->column, one->length, re, query, replacement,
                       &result))
            replaced = 1;
        else
            outcome.skipped = 1;
    } else {
        replaced = replaceAll(file.text, re, query, replacement, &result);
    }
    if (replaced == 0)
        return outcome;

    file.text = result;
    if (!WorkspaceSearchFiles::save(path, file)) {
        outcome.failedFiles = 1;
        return outcome;
    }
    outcome.replaced = replaced;
    return outcome;
}

WorkspaceSearchPanel::ReplaceOutcome WorkspaceSearchPanel::replaceOneMatch(const QString& path,
                                                                            const Target& target)
{
    if (QPlainTextEdit* editor = editorFor(path))
        return replaceInEditor(editor, {target});
    return replaceInClosedFile(path, replaceQuery(), m_executedRegex, m_replaceEdit->text(),
                               &target);
}

WorkspaceSearchPanel::ReplaceOutcome WorkspaceSearchPanel::replaceAllInFile(const QString& path)
{
    if (QPlainTextEdit* editor = editorFor(path)) {
        // Fresh matches from the live buffer, not the stored rows: whatever the
        // document holds now is what the query means for this file.
        QList<Match> matches;
        searchText(editor->toPlainText(), m_executedRegex, std::numeric_limits<int>::max(),
                   &matches);
        QList<Target> targets;
        targets.reserve(matches.size());
        for (const Match& match : matches)
            targets.append({match.line, match.column, match.length});
        if (targets.isEmpty())
            return {};
        return replaceInEditor(editor, targets);
    }
    return replaceInClosedFile(path, replaceQuery(), m_executedRegex, m_replaceEdit->text(),
                               nullptr);
}

void WorkspaceSearchPanel::replaceAtIndex(const QModelIndex& index)
{
    if (!index.isValid() || !m_replaceExpanded || !m_hasResults || m_running || m_replacing)
        return;
    const QString path = index.data(PathRole).toString();
    ReplaceOutcome outcome;
    if (index.data(KindRole).toInt() == FileRow) {
        outcome = replaceAllInFile(path);
    } else {
        outcome = replaceOneMatch(path, {index.data(LineRole).toInt(),
                                         index.data(ColumnRole).toInt(),
                                         index.data(LengthRole).toInt()});
    }
    reportOutcome(outcome);
    refreshFile(path);
}

void WorkspaceSearchPanel::replaceAllInWorkspace()
{
    if (!m_hasResults || m_running || m_replacing || m_model->rowCount() == 0)
        return;

    QStringList paths;
    int total = 0;
    int closed = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const QStandardItem* item = m_model->item(i);
        paths.append(item->data(PathRole).toString());
        total += item->data(CountRole).toInt();
        if (!editorFor(paths.last()))
            ++closed;
    }

    if (paths.size() > 1) {
        const QString question = tr("Replace %1 occurrences across %2 files?")
                                     .arg(total).arg(paths.size());
        const QString detail = closed > 0
            ? tr("Files that are not open in an editor are written to disk and cannot be undone.")
            : QString();
        if (themedQuestion(this, tr("Replace in Files"), question,
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No, detail)
            != QMessageBox::Yes)
            return;
    }

    m_uiOutcome = {};
    QStringList closedPaths;
    for (const QString& path : paths) {
        if (editorFor(path))
            m_uiOutcome.add(replaceAllInFile(path));
        else
            closedPaths.append(path);
    }

    if (closedPaths.isEmpty()) {
        reportOutcome(m_uiOutcome);
        m_keepNoteOnce = true;
        startSearch();
        return;
    }

    m_replacing = true;
    setEnabled(false);
    setStatus(tr("Replacing..."));
    const Query query = replaceQuery();
    const QRegularExpression re = m_executedRegex;
    const QString replacement = m_replaceEdit->text();
    m_replaceWatcher.setFuture(QtConcurrent::run([closedPaths, query, re, replacement]() {
        ReplaceOutcome outcome;
        for (const QString& path : closedPaths)
            outcome.add(replaceInClosedFile(path, query, re, replacement, nullptr));
        return outcome;
    }));
}

void WorkspaceSearchPanel::reportOutcome(const ReplaceOutcome& outcome)
{
    ReplaceOutcome total = outcome;
    if (m_replacing || m_uiOutcome.files > 0) {
        total.add(m_uiOutcome);
        m_uiOutcome = {};
    }

    QString note;
    if (total.replaced > 0) {
        note = (total.replaced == 1 ? tr("Replaced 1 occurrence") : tr("Replaced %1 occurrences").arg(total.replaced))
            + QLatin1Char(' ')
            + (total.files == 1 ? tr("in 1 file") : tr("in %1 files").arg(total.files));
    } else {
        note = tr("Nothing was replaced");
    }
    if (total.skipped > 0)
        note += tr("; %1 out of date").arg(total.skipped);
    if (total.failedFiles > 0)
        note += tr("; %1 file(s) could not be written").arg(total.failedFiles);
    m_transientNote = note;
    updateSummaryStatus();
}

void WorkspaceSearchPanel::updateFocusState()
{
    const auto sync = [](QWidget* row, QLineEdit* edit) {
        const bool focused = edit->hasFocus();
        if (row->property("focused").toBool() == focused)
            return;
        row->setProperty("focused", focused);
        repolish(row);
    };
    sync(m_queryRow, m_queryEdit);
    sync(m_replaceRow, m_replaceEdit);
}

bool WorkspaceSearchPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_queryEdit || watched == m_replaceEdit) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            QTimer::singleShot(0, this, &WorkspaceSearchPanel::updateFocusState);
        } else if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            const auto focusResults = [this]() {
                if (m_model->rowCount() == 0)
                    return;
                m_results->setFocus(Qt::TabFocusReason);
                if (!m_results->currentIndex().isValid())
                    m_results->setCurrentIndex(m_model->index(0, 0));
            };
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && watched == m_queryEdit) {
                startSearch();
                return true;
            }
            // Search is a persistent view, not a popup: Esc clears the focused
            // field when it has text, otherwise hands focus on to the results.
            if (key->key() == Qt::Key_Escape) {
                auto* edit = static_cast<QLineEdit*>(watched);
                if (!edit->text().isEmpty()) {
                    edit->clear();
                    return true;
                }
                focusResults();
                return true;
            }
            if (key->key() == Qt::Key_Down && watched == m_queryEdit
                && key->modifiers() == Qt::NoModifier && m_model->rowCount() > 0) {
                focusResults();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
