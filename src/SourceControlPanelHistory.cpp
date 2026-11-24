#include "SourceControlPanel.h"

#include "AlignedMenu.h"
#include "FastScrollBar.h"
#include "GitBranchSelector.h"
#include "GitFileIcon.h"
#include "GitRunner.h"
#include "GitStatusStyle.h"
#include "ThemePalette.h"
#include "ReadOnlyItemViewCursor.h"
#include "ThemedMessageBox.h"

#include <QLocale>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {

constexpr int kCommitRowHeight = 28;
constexpr int kChildRowHeight  = 22;
constexpr int kLaneWidth       = 12;
constexpr int kMaxLanes        = 6;
constexpr int kGraphTextInset  = 16;
constexpr int kCommitTextInset = 12;
constexpr int kChildIndent     = GitUiLayout::kFileRowLeadingInset;

// Commit tooltips: subjects longer than this wrap inside a table of this
// pixel width instead of stretching the tooltip across the whole screen.
constexpr int kTooltipWrapChars = 80;
constexpr int kTooltipMaxWidth  = 320;

// A stable, readable colour per lane. Deliberately small and fixed: a hashed
// palette makes the same branch change colour as history scrolls.
QColor laneColor(int lane, bool dark)
{
    static const QColor light[] = {
        QColor(0x00, 0x78, 0xd4), QColor(0x2d, 0x7d, 0x46), QColor(0xb5, 0x3a, 0x28),
        QColor(0x8f, 0x6b, 0x1f), QColor(0x6b, 0x3f, 0xa0), QColor(0x0e, 0x77, 0x86),
    };
    static const QColor dim[] = {
        QColor(0x4c, 0xab, 0xff), QColor(0x73, 0xc9, 0x91), QColor(0xe5, 0x74, 0x64),
        QColor(0xe2, 0xc0, 0x8d), QColor(0xb1, 0x8a, 0xe0), QColor(0x4d, 0xc0, 0xd0),
    };
    const int index = qBound(0, lane, 5);
    return dark ? dim[index] : light[index];
}

int laneCountFor(const QVariantList& before, const QVariantList& after, int lane)
{
    int count = qBound(1, lane + 1, kMaxLanes);
    auto includeLanes = [&count](const QVariantList& lanes) {
        for (const QVariant& value : lanes)
            count = qMax(count, qBound(1, value.toInt() + 1, kMaxLanes));
    };
    includeLanes(before);
    includeLanes(after);
    return count;
}

QString relativeAge(const QDateTime& when)
{
    if (!when.isValid())
        return QString();
    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    if (seconds < 60)          return SourceControlPanel::tr("just now");
    if (seconds < 3600)        return SourceControlPanel::tr("%1m ago").arg(seconds / 60);
    if (seconds < 86400)       return SourceControlPanel::tr("%1h ago").arg(seconds / 3600);
    if (seconds < 86400 * 30)  return SourceControlPanel::tr("%1d ago").arg(seconds / 86400);
    if (seconds < 86400 * 365) return SourceControlPanel::tr("%1mo ago").arg(seconds / (86400 * 30));
    return SourceControlPanel::tr("%1y ago").arg(seconds / (86400 * 365));
}

QString fileNameOf(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QString relativeDirectory(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? path.left(slash) : QString();
}

}

// Draws the lane gutter plus the commit text. Child (file) rows are indented
// past the gutter and drawn plainly.
class GitGraphDelegate final : public QStyledItemDelegate {
public:
    explicit GitGraphDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setDarkMode(bool dark) { m_dark = dark; }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        // Commit rows and their file children are different heights, which
        // is why the history tree cannot use setUniformRowHeights().
        size.setHeight(index.parent().isValid() ? kChildRowHeight : kCommitRowHeight);
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const bool isChild = index.parent().isValid();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        ThemeCard::paintRow(*painter, option.rect, m_dark, selected && !isChild, hovered);

        const QColor textColor = m_dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x1f, 0x1f, 0x1f);
        const QColor dimColor = m_dark ? QColor(0x8c, 0x8c, 0x8c) : QColor(0x77, 0x77, 0x77);

        QRect content = option.rect;

        if (isChild) {
            // Files are ordinary rows immediately below their commit.  Carry
            // the commit's outgoing lanes through them, so expansion never
            // punches a card-shaped hole in the graph.
            const QModelIndex parent = index.parent();
            const QVariantList outgoing = parent.data(GitItemRoles::After).toList();
            // Reserve only the lanes that visibly continue through this
            // commit. A fixed six-lane gutter made a simple linear history
            // feel detached from the compact Changes list above it.
            const int laneCount = laneCountFor({}, outgoing, 0);
            const int gutterWidth = kLaneWidth * laneCount;
            auto laneX = [&](int lane) {
                return option.rect.left() + GitUiLayout::kGraphLaneOriginInset
                    + qBound(0, lane, kMaxLanes - 1) * kLaneWidth;
            };
            for (const QVariant& value : outgoing) {
                painter->setPen(QPen(laneColor(value.toInt(), m_dark), 1.5));
                painter->drawLine(laneX(value.toInt()), option.rect.top(),
                                  laneX(value.toInt()), option.rect.bottom());
            }

            // A selected file still has a clear focus cue, but it is a slim
            // accent rather than a full-width selected/card background.
            if (selected) {
                const QColor accent = m_dark ? QColor(0x4c, 0xab, 0xff)
                                             : QColor(0x00, 0x78, 0xd4);
                painter->fillRect(QRect(option.rect.left() + gutterWidth + 6,
                                        option.rect.top() + 3, 2,
                                        option.rect.height() - 6), accent);
            }

            // Files form one compact hierarchy level below their commit.
            content.adjust(gutterWidth + kGraphTextInset + kChildIndent
                               + GitUiLayout::kGraphContentInset,
                           0, -8, 0);
            const QString path = index.data(GitItemRoles::Path).toString();
            const auto state = static_cast<GitFileState>(index.data(GitItemRoles::State).toInt());
            const QString letter(GitRepository::statusLetter(state));
            const int letterWidth = option.fontMetrics.horizontalAdvance(letter) + 8;

            painter->setFont(gitFileStatusFont(option.font));
            painter->setPen(gitFileStateColor(state, m_dark));
            painter->drawText(QRect(content.right() - letterWidth, content.top(),
                                    letterWidth, content.height()),
                              Qt::AlignVCenter | Qt::AlignRight, letter);
            painter->setFont(option.font);
            content.setRight(content.right() - letterWidth - 4);

            const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (!icon.isNull()) {
                const QRect iconRect(content.left(), content.top() + (content.height() - 16) / 2,
                                     16, 16);
                icon.paint(painter, iconRect);
                content.setLeft(iconRect.right() + 6);
            }

            const QString name = fileNameOf(path);
            const QString dir = relativeDirectory(path);
            const int nameWidth = option.fontMetrics.horizontalAdvance(name);
            const QRect nameRect(content.left(), content.top(),
                                 qMin(nameWidth, content.width()), content.height());
            painter->setPen(textColor);
            painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);

            if (!dir.isEmpty() && content.width() > nameWidth + 12) {
                const QRect dirRect(nameRect.right() + 8, content.top(),
                                    content.right() - nameRect.right() - 8, content.height());
                painter->setPen(dimColor);
                painter->drawText(dirRect, Qt::AlignVCenter | Qt::AlignLeft,
                                  option.fontMetrics.elidedText(dir, Qt::ElideMiddle,
                                                                dirRect.width()));
            }
            painter->restore();
            return;
        }

        // --- graph gutter ---
        const int lane = index.data(GitItemRoles::Lane).toInt();
        const QVariantList before = index.data(GitItemRoles::Before).toList();
        const QVariantList after = index.data(GitItemRoles::After).toList();
        const int gutterWidth = kLaneWidth * laneCountFor(before, after, lane);

        auto laneX = [&](int l) {
            return option.rect.left() + GitUiLayout::kGraphLaneOriginInset
                + qBound(0, l, kMaxLanes - 1) * kLaneWidth;
        };
        // The mathematical row centre looks a little high next to Segoe UI's
        // text glyphs, so apply a 1px optical correction to the graph node.
        const int midY = option.rect.center().y() + 1;

        painter->setPen(QPen(Qt::NoPen));
        for (const QVariant& value : before) {
            const int l = value.toInt();
            painter->setPen(QPen(laneColor(l, m_dark), 1.5));
            painter->drawLine(laneX(l), option.rect.top(), laneX(l), midY);
        }
        for (const QVariant& value : after) {
            const int l = value.toInt();
            painter->setPen(QPen(laneColor(l, m_dark), 1.5));
            painter->drawLine(laneX(l), midY, laneX(l), option.rect.bottom());
        }

        // A merge fans out: connect this commit's dot to any lane it opened.
        for (const QVariant& value : after) {
            const int l = value.toInt();
            if (l == lane)
                continue;
            bool wasOpen = false;
            for (const QVariant& b : before) {
                if (b.toInt() == l) {
                    wasOpen = true;
                    break;
                }
            }
            if (!wasOpen) {
                painter->setPen(QPen(laneColor(l, m_dark), 1.5));
                painter->drawLine(laneX(lane), midY, laneX(l), midY);
            }
        }

        // Mirror of the fan-out: a lane that ends here must join this
        // commit's dot instead of stopping as a dangling vertical.
        for (const QVariant& value : before) {
            const int l = value.toInt();
            if (l == lane)
                continue;
            bool staysOpen = false;
            for (const QVariant& a : after) {
                if (a.toInt() == l) {
                    staysOpen = true;
                    break;
                }
            }
            if (!staysOpen) {
                painter->setPen(QPen(laneColor(l, m_dark), 1.5));
                painter->drawLine(laneX(l), midY, laneX(lane), midY);
            }
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(laneColor(lane, m_dark));
        painter->drawEllipse(QPoint(laneX(lane), midY), 4, 4);

        // Keep the metadata anchored at the same right edge while bringing
        // only the commit message a few pixels closer to the graph gutter.
        content.setLeft(option.rect.left() + gutterWidth + kCommitTextInset
                        + GitUiLayout::kGraphContentInset);
        content.setRight(content.right() - 8);

        const QString age = relativeAge(index.data(Qt::UserRole + 12).toDateTime());
        const QString author = index.data(Qt::UserRole + 13).toString();
        const QString meta = age.isEmpty() ? author
                                           : QStringLiteral("%1 · %2").arg(author, age);
        const int metaWidth = option.fontMetrics.horizontalAdvance(meta) + 12;

        if (content.width() > metaWidth + 60) {
            painter->setPen(dimColor);
            painter->drawText(QRect(content.right() - metaWidth, content.top(),
                                    metaWidth, content.height()),
                              Qt::AlignVCenter | Qt::AlignRight, meta);
            content.setRight(content.right() - metaWidth - 6);
        }

        painter->setPen(textColor);
        painter->drawText(content, Qt::AlignVCenter | Qt::AlignLeft,
                          option.fontMetrics.elidedText(index.data(GitItemRoles::Subject).toString(),
                                                        Qt::ElideRight, content.width()));
        painter->restore();
    }

private:
    bool m_dark = false;
};

// ---------------------------------------------------------------------------
// Section construction

QWidget* SourceControlPanel::buildGraphSection()
{
    auto* section = new QWidget(this);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Unlike Source Control, history is always present. A label-based header
    // avoids an affordance that cannot do anything and keeps its contents
    // optically centred within the header band.
    // 36px matches TerminalPanel::kHeaderHeight and the Workspace/Outline
    // header (see WorkspacePanelController::buildOutlineSection) so all three
    // read as the same header component at runtime.
    auto* headerRow = new QWidget(section);
    headerRow->setObjectName(QStringLiteral("gitGraphHeader"));
    headerRow->setFixedHeight(36);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0 + GitUiLayout::kGraphContentInset, 0, 6, 0);
    headerLayout->setSpacing(4);

    auto* title = new QLabel(tr("GRAPH"), headerRow);
    title->setObjectName(QStringLiteral("gitSectionTitle"));
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    headerLayout->addWidget(title, 0, Qt::AlignLeft | Qt::AlignVCenter);

    headerLayout->addStretch(1);

    // The branch lives on the far right of this row as a compact selector.
    m_branchButton = new GitBranchButton(headerRow);
    m_branchButton->setDarkMode(m_dark);
    m_branchButton->hide();
    connect(m_branchButton, &GitBranchButton::clicked,
            this, &SourceControlPanel::openBranchPopup);
    headerLayout->addWidget(m_branchButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_historyList = new QTreeWidget(section);
    m_historyList->setObjectName(QStringLiteral("gitHistoryList"));
    m_historyList->setHeaderHidden(true);
    m_historyList->setRootIsDecorated(false);
    m_historyList->setIndentation(0);
    m_historyList->setAnimated(false);
    m_historyList->setMouseTracking(true);
    setReadOnlyItemViewCursor(m_historyList);
    m_historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_historyList->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_historyList));
    m_historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyList->setContextMenuPolicy(Qt::CustomContextMenu);

    // Load the next history batch automatically when the user approaches the
    // end of the list; keep the batch size out of the visible header.
    connect(m_historyList->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        const QScrollBar* bar = m_historyList->verticalScrollBar();
        if (!bar || bar->maximum() <= 0 || !m_snapshot.moreCommits ||
            value < bar->maximum() - 120)
            return;
        loadMoreCommits();
    });

    m_graphDelegate = new GitGraphDelegate(m_historyList);
    m_graphDelegate->setDarkMode(m_dark);
    m_historyList->setItemDelegate(m_graphDelegate);

    connect(m_historyList, &QTreeWidget::itemSelectionChanged,
            this, &SourceControlPanel::onCommitSelectionChanged);
    // A commit click toggles its child-file rows. A child click only selects;
    // opening its diff requires the same deliberate double-click as Changes.
    connect(m_historyList, &QTreeWidget::itemClicked,
            this, &SourceControlPanel::onGraphItemClicked);
    connect(m_historyList, &QTreeWidget::itemDoubleClicked,
            this, &SourceControlPanel::onGraphItemDoubleClicked);
    connect(m_historyList, &QTreeWidget::customContextMenuRequested,
            this, &SourceControlPanel::showHistoryContextMenu);

    layout->addWidget(headerRow);
    layout->addWidget(m_historyList, 1);
    return section;
}

void SourceControlPanel::applyGraphDarkMode(bool dark)
{
    if (m_graphDelegate)
        m_graphDelegate->setDarkMode(dark);
    if (m_branchButton)
        m_branchButton->setDarkMode(dark);
}

// ---------------------------------------------------------------------------
// Population

void SourceControlPanel::populateHistory(const GitSnapshot& snapshot)
{
    const QString previouslySelected = m_selectedCommitHash;

    m_historyList->clear();
    QTreeWidgetItem* toSelect = nullptr;

    for (const GitCommit& commit : snapshot.commits) {
        auto* item = new QTreeWidgetItem(m_historyList);
        item->setData(0, GitItemRoles::Kind, CommitRow);
        item->setData(0, GitItemRoles::Hash, commit.hash);
        item->setData(0, GitItemRoles::Subject, commit.subject);
        item->setData(0, GitItemRoles::Lane, commit.lane);
        item->setData(0, GitItemRoles::Author, commit.author);
        item->setData(0, GitItemRoles::Date, commit.date);
        item->setData(0, GitItemRoles::Loaded, false);
        // First parent only: that is what `git show` diffs against, and it is
        // the hash the tab title names on the left of the arrow.
        item->setData(0, GitItemRoles::Parent,
                      commit.parents.isEmpty() ? QString() : commit.parents.first());

        QVariantList before;
        for (int lane : commit.lanesBefore)
            before.append(lane);
        QVariantList after;
        for (int lane : commit.lanesAfter)
            after.append(lane);
        item->setData(0, GitItemRoles::Before, before);
        item->setData(0, GitItemRoles::After, after);

        // Rich text so the metadata line can be set apart from the subject.
        QString meta;
        const QString age = relativeAge(commit.date);
        if (commit.date.isValid()) {
            const QString stamp = QLocale::system().toString(commit.date.toLocalTime(),
                                                             QLocale::ShortFormat);
            meta = age.isEmpty() ? QStringLiteral("%1 · ").arg(stamp.toHtmlEscaped())
                                 : QStringLiteral("%1 (%2) · ").arg(age.toHtmlEscaped(),
                                                                    stamp.toHtmlEscaped());
        }
        meta += QStringLiteral("%1 · %2")
            .arg(commit.author.toHtmlEscaped(), commit.shortHash.toHtmlEscaped());
        // A subject that fits is kept on one line (white-space:pre), because
        // Qt otherwise re-wraps short rich-text tooltips at its own narrow
        // width. A long one is bounded instead: Qt's rich text has no
        // max-width, but it does honour a table's width attribute, and inside
        // a cell the text wraps normally.
        // Two paragraphs, not a <br>: Qt's rich text carries a margin on
        // QTextBlockFormat, so only a real block boundary can space the
        // metadata line away from the commit message.
        const QString body =
            QStringLiteral("<p style='font-weight:600; margin:0 0 6px 0'>%1</p>"
                           "<p style='margin:0'>%2</p>")
            .arg(meta, commit.subject.toHtmlEscaped());
        item->setToolTip(0, commit.subject.size() > kTooltipWrapChars
            ? QStringLiteral("<table width='%1'><tr><td>%2</td></tr></table>")
                  .arg(kTooltipMaxWidth).arg(body)
            : QStringLiteral("<div style='white-space:pre'>%1</div>").arg(body));

        if (!previouslySelected.isEmpty() && commit.hash == previouslySelected)
            toSelect = item;
    }

    if (toSelect) {
        const QSignalBlocker blocker(m_historyList);
        m_historyList->setCurrentItem(toSelect);
        onCommitExpanded(toSelect);
        toSelect->setExpanded(true);
    }
}

// ---------------------------------------------------------------------------
// Commit selection

void SourceControlPanel::onCommitSelectionChanged()
{
    QTreeWidgetItem* item = m_historyList->currentItem();
    if (!item)
        return;

    if (item->data(0, GitItemRoles::Kind).toInt() != CommitRow)
        return;

    m_selectedCommitHash = item->data(0, GitItemRoles::Hash).toString();
    m_selectedCommitParent = item->data(0, GitItemRoles::Parent).toString();

}

void SourceControlPanel::onCommitExpanded(QTreeWidgetItem* item)
{
    if (!item || item->data(0, GitItemRoles::Loaded).toBool())
        return;

    const QString hash = item->data(0, GitItemRoles::Hash).toString();
    const QString root = m_root;
    if (hash.isEmpty() || root.isEmpty())
        return;

    if (const auto cached = m_commitFilesCache.constFind(hash);
        cached != m_commitFilesCache.cend()) {
        populateCommitFiles(item, *cached, root);
        return;
    }
    if (m_commitFilesLoading.contains(hash)) {
        item->setData(0, GitItemRoles::Loaded, true);
        return;
    }

    // Mark the row while the request is in flight so repeated expand/click
    // signals do not launch duplicate `git show` processes. The item itself
    // cannot be captured: any refresh clears and rebuilds the tree. Completion
    // locates the immutable commit again by hash instead.
    item->setData(0, GitItemRoles::Loaded, true);
    m_commitFilesLoading.insert(hash);

    auto* watcher = new QFutureWatcher<GitRunner::Result>(this);
    connect(watcher, &QFutureWatcher<GitRunner::Result>::finished, this,
            [this, watcher, root, hash] {
        const GitRunner::Result result = watcher->result();
        watcher->deleteLater();

        if (m_root != root)
            return;
        m_commitFilesLoading.remove(hash);

        QTreeWidgetItem* target = nullptr;
        for (int i = 0; i < m_historyList->topLevelItemCount(); ++i) {
            QTreeWidgetItem* candidate = m_historyList->topLevelItem(i);
            if (candidate->data(0, GitItemRoles::Hash).toString() == hash) {
                target = candidate;
                break;
            }
        }
        if (!target)
            return;

        if (!result.ok()) {
            target->setData(0, GitItemRoles::Loaded, false);
            setStatusMessage(result.err.isEmpty() ? tr("Could not read the commit.")
                                                   : result.err, true);
            return;
        }

        const QVector<GitFileEntry> files = GitRepository::parseNameStatus(result.out);
        m_commitFilesCache.insert(hash, files);
        // A refresh may have rebuilt the row and a second request may already
        // have populated it. Never append the same immutable children twice.
        if (target->childCount() == 0)
            populateCommitFiles(target, files, root);
    });
    watcher->setFuture(QtConcurrent::run([root, hash] {
        return GitRunner::run(
            root, { QStringLiteral("show"), QStringLiteral("--name-status"),
                    QStringLiteral("-z"), QStringLiteral("--format="), hash });
    }));
}

void SourceControlPanel::populateCommitFiles(QTreeWidgetItem* item,
                                              const QVector<GitFileEntry>& files,
                                              const QString& root)
{
    if (!item)
        return;
    for (const GitFileEntry& file : files) {
        auto* child = new QTreeWidgetItem(item);
        child->setData(0, GitItemRoles::Kind, CommitFileRow);
        child->setData(0, GitItemRoles::Path, file.path);
        child->setData(0, GitItemRoles::State, static_cast<int>(file.state));
        child->setIcon(0, gitFileIconForPath(file.path, root));
        child->setToolTip(0, file.path);
    }
    item->setData(0, GitItemRoles::Loaded, true);
}

void SourceControlPanel::onGraphItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    const int kind = item->data(0, GitItemRoles::Kind).toInt();
    if (kind == CommitRow) {
        if (item->isExpanded()) {
            item->setExpanded(false);
            return;
        }

        // Only one commit stays expanded, or the list becomes a wall of files.
        for (int i = 0; i < m_historyList->topLevelItemCount(); ++i) {
            QTreeWidgetItem* other = m_historyList->topLevelItem(i);
            if (other != item && other->isExpanded())
                other->setExpanded(false);
        }

        onCommitExpanded(item);
        item->setExpanded(true);
        return;
    }

}

void SourceControlPanel::onGraphItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item || item->data(0, GitItemRoles::Kind).toInt() != CommitFileRow)
        return;

    QTreeWidgetItem* parent = item->parent();
    if (parent) {
        showCommitDiff(parent->data(0, GitItemRoles::Hash).toString(),
                       parent->data(0, GitItemRoles::Parent).toString(),
                       item->data(0, GitItemRoles::Path).toString());
    }
}

void SourceControlPanel::showCommitDiff(const QString& hash, const QString& parentHash,
                                        const QString& path)
{
    if (m_root.isEmpty() || hash.isEmpty() || path.isEmpty())
        return;

    GitDiffSpec spec;
    spec.repoRoot = m_root;
    spec.path = path;
    spec.source = GitDiffSpec::Commit;
    spec.commitHash = hash;
    spec.parentHash = parentHash;
    emit diffRequested(spec);
}

void SourceControlPanel::loadMoreCommits()
{
    if (!m_snapshot.moreCommits || m_moreCommitsRequested || m_refreshRunning)
        return;
    m_moreCommitsRequested = true;
    m_commitLimit += 100;
    scheduleRefresh();
}

void SourceControlPanel::showHistoryContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_historyList->itemAt(pos);
    const bool isCommit = item && item->data(0, GitItemRoles::Kind).toInt() == CommitRow;

    // Read what the menu can copy before exec(): the menu runs a nested event
    // loop, and `item` belongs to a list a refresh landing inside it would
    // clear. The hold keeps that refresh out too, but the values are taken
    // here so nothing downstream depends on the row still existing.
    const QString hash = isCommit ? item->data(0, GitItemRoles::Hash).toString() : QString();
    const QString subject = isCommit ? item->data(0, GitItemRoles::Subject).toString() : QString();
    RefreshHold hold(this);

    AlignedMenu menu(m_historyList);
    menu.setObjectName(QStringLiteral("gitContextMenu"));
    menu.setMinimumWidth(200);
    menu.setCompactShortcutLayout(true);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    // Refresh is always offered, even on empty graph space -- it is the only
    // discoverable entry point for a manual refresh now that F5 is not bound
    // here. Copy Hash/Copy Message only make sense over a commit row.
    // Checkout, branch, tag, cherry-pick and the rest stay in the terminal.
    QAction* refreshAction = menu.addAction(tr("Refresh"));
    // Display-only, same as showFileListContextMenu()'s -- the real binding is
    // the panel's own WidgetWithChildrenShortcut F5.
    refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    QAction* copyHash = nullptr;
    QAction* copyMessage = nullptr;
    if (isCommit) {
        menu.addSeparator();
        copyHash = menu.addAction(tr("Copy Commit Hash"));
        copyMessage = menu.addAction(tr("Copy Commit Message"));
    }

    // copyHash/copyMessage are null on empty graph space (isCommit == false),
    // and menu.exec() also returns null when the menu is dismissed without a
    // choice -- without the null guards below, dismissing the menu there would
    // match `chosen == copyHash` and fire "Copy Commit Hash" on nothing.
    QAction* chosen = menu.exec(m_historyList->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    } else if (chosen == refreshAction) {
        refresh();
    } else if (chosen == copyHash) {
        QApplication::clipboard()->setText(hash);
        setStatusMessage(tr("Commit hash copied."));
    } else if (chosen == copyMessage) {
        QApplication::clipboard()->setText(subject);
        setStatusMessage(tr("Commit message copied."));
    }
}

// ---------------------------------------------------------------------------
// Branch selector
//
// A switcher, not branch management: it lists what is there, and hands one
// name back to the same runLocal()/scheduleRefresh() path every other local
// write in this panel uses. Creating, deleting and renaming branches stay in
// the terminal, as does anything that would rewrite history.

void SourceControlPanel::openBranchPopup()
{
    // Qt closed the popup before the button saw this press, so the click that
    // dismissed it must not reopen it.
    if (m_branchPopupDismissedByButton) {
        m_branchPopupDismissedByButton = false;
        return;
    }
    if (m_root.isEmpty() || m_busy || m_localRunning || !m_branchButton)
        return;

    if (!m_branchPopup) {
        m_branchPopup = new GitBranchPopup(this);
        connect(m_branchPopup, &GitBranchPopup::branchActivated,
                this, &SourceControlPanel::checkoutBranch);
        connect(m_branchPopup, &GitBranchPopup::destroyed, this, [this] {
            m_branchPopupDismissedByButton = false;
        });
        m_branchPopup->installEventFilter(this);
    }

    m_branchPopup->setBranches(m_snapshot.branches);
    m_branchPopup->popupUnder(m_branchButton, this);
}

void SourceControlPanel::checkoutBranch(const QString& name, bool remote)
{
    if (m_root.isEmpty() || name.isEmpty() || m_busy || m_localRunning)
        return;

    QStringList args { QStringLiteral("checkout") };
    if (remote) {
        // "origin/dev" -> a local "dev" tracking it. Plain `git checkout dev`
        // is git's own DWIM for exactly this and creates the tracking branch
        // when no local one exists yet; if one does, it simply switches to it.
        const int slash = name.indexOf(QLatin1Char('/'));
        const QString local = slash >= 0 ? name.mid(slash + 1) : name;
        if (local.isEmpty())
            return;
        args << local;
    } else {
        args << name;
    }

    // Nothing is stashed, discarded or reset first, by design: if git refuses
    // because local changes would be overwritten, runLocal() shows git's own
    // message in the panel's status line and the worktree is untouched.
    const QString branch = args.constLast();
    runLocal(args, {}, [this, branch] {
        setStatusMessage(tr("Switched to branch \"%1\".").arg(branch));
        emit worktreeChanged();
        scheduleRefresh();
    });
}

// ---------------------------------------------------------------------------
// Stash operations

void SourceControlPanel::stashCurrentChanges(bool includeUntracked)
{
    if (m_root.isEmpty() || m_busy || m_localRunning)
        return;

    // The dialog is a nested event loop; hold the refresh so the rows and
    // snapshot this action started from are still the ones it acts on.
    RefreshHold hold(this);

    QDialog dialog(this);
    dialog.setWindowTitle(includeUntracked ? tr("Stash Changes Including Untracked")
                                            : tr("Stash Changes"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("Stash message (optional):"), &dialog));
    auto* messageEdit = new QLineEdit(&dialog);
    messageEdit->setPlaceholderText(tr("WIP"));
    layout->addWidget(messageEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QStringList args { QStringLiteral("stash"), QStringLiteral("push") };
    if (includeUntracked)
        args << QStringLiteral("--include-untracked");
    const QString message = messageEdit->text().trimmed();
    if (!message.isEmpty())
        args << QStringLiteral("-m") << message;

    runLocal(args, {}, [this] {
        setStatusMessage(tr("Changes stashed."));
        emit worktreeChanged();
        scheduleRefresh();
    });
}
