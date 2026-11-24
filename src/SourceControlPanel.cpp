#include "SourceControlPanel.h"
#include "DarkTheme.h"
#include "StartupProfiler.h"

#include "AlignedMenu.h"
#include "FastScrollBar.h"
#include "GitBranchSelector.h"
#include "GitFileIcon.h"
#include "GitRunner.h"
#include "GitStatusStyle.h"
#include "ThemePalette.h"
#include "IconHelper.h"
#include "ReadOnlyItemViewCursor.h"
#include "ThemedMessageBox.h"
#include "ThemedSplitter.h"
#include "WorkspaceFileOps.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <functional>

namespace {

// Material Icons codepoints.
constexpr ushort kIconSync     = 0xE627;  // sync
constexpr ushort kIconOpen     = 0xF8CE;  // arrow_outward
constexpr ushort kIconStage    = 0xE145;  // add
constexpr ushort kIconUnstage  = 0xE15B;  // remove
constexpr ushort kIconDiscard  = 0xE166;  // undo
constexpr ushort kIconResolve  = 0xE5CA;  // check
constexpr ushort kIconClose    = 0xE5CD;  // close

constexpr int kRowHeight = 28;
constexpr int kRowActionSize = 22;
constexpr int kRowActionIconSize = 18;
constexpr int kCommitSplitHeight = 36;
constexpr int kCommitMenuWidth = 30;
constexpr int kSyncIconSize = 16;
constexpr int kGroupCountBadgeDiameter = 22;
// Long git output must stay a status surface, not take over the sidebar.
constexpr int kStatusAutoMaxHeight = 140;
constexpr int kStatusMinHeight = 18;
constexpr int kStatusDragMaxHeight = 400;
constexpr int kStatusCloseSize = 24;
constexpr int kStatusCloseIconSize = 16;
// Halved growth off both margins keeps the glyph optically where a 12px
// button sat while the hover square around it gets larger.
constexpr int kStatusCloseTopMargin = 8;
constexpr int kStatusCloseRightMargin = 8;
// The top edge doubles as the resize grip, so keep it clear of the text.
constexpr int kStatusGripHeight = 5;

// The status area's top edge is its own resize grip: dragging it makes room
// for long git output without adding a splitter handle that would sit in the
// layout even when there is nothing to say.
class GitStatusSurface final : public QWidget {
public:
    explicit GitStatusSurface(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }

    std::function<int()>     contentHeight;
    std::function<void(int)> heightDragged;
    std::function<void()>    resized;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (resized)
            resized();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && inGrip(event->position().toPoint())) {
            m_dragging = true;
            m_pressY = event->globalPosition().toPoint().y();
            m_startHeight = contentHeight ? contentHeight() : 0;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging) {
            if (heightDragged)
                heightDragged(m_startHeight + (m_pressY - event->globalPosition().toPoint().y()));
            event->accept();
            return;
        }
        setCursor(inGrip(event->position().toPoint()) ? Qt::SizeVerCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false;
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        if (!m_dragging)
            unsetCursor();
        QWidget::leaveEvent(event);
    }

private:
    bool inGrip(const QPoint& pos) const { return pos.y() < kStatusGripHeight; }

    bool m_dragging = false;
    int  m_pressY = 0;
    int  m_startHeight = 0;
};

class SyncContentLabel final : public QLabel {
public:
    explicit SyncContentLabel(QWidget* parent) : QLabel(parent) {}

    void setContent(const QString& title, int behind, int ahead)
    {
        m_title = title;
        m_behind = behind;
        m_ahead = ahead;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        painter.setPen(Qt::white);

        const QFont textFont = font();
        const QFontMetrics metrics(textFont);
        // materialIconTight() derives the glyph font from the paint rect
        // (rect size minus two pixels), so an 18px rect gives the intended
        // approximately 16px visible icon while keeping its ink centered.
        constexpr int iconSize = 18;
        constexpr int gap = 4;

        struct Part { int width; bool icon; QChar code; QString text; };
        QVector<Part> parts;
        parts.push_back({iconSize, true, QChar(kIconSync), {}});
        parts.push_back({metrics.horizontalAdvance(m_title), false, {}, m_title});
        if (m_behind > 0) {
            const QString value = QString::number(m_behind);
            parts.push_back({metrics.horizontalAdvance(value), false, {}, value});
            parts.push_back({iconSize, true, QChar(0xE5DB), {}});
        }
        if (m_ahead > 0) {
            const QString value = QString::number(m_ahead);
            parts.push_back({metrics.horizontalAdvance(value), false, {}, value});
            parts.push_back({iconSize, true, QChar(0xE5D8), {}});
        }

        int totalWidth = 0;
        for (const Part& part : parts)
            totalWidth += part.width;
        totalWidth += gap * qMax(0, parts.size() - 1);

        int x = qMax(0, (width() - totalWidth) / 2);
        for (const Part& part : parts) {
            if (part.icon) {
                const QIcon icon = IconHelper::materialIconTight(part.code, Qt::white, iconSize);
                const int opticalY = part.code == QChar(kIconSync) ? 0 : 1;
                icon.paint(&painter, QRect(x, (height() - iconSize) / 2 + opticalY,
                                           iconSize, iconSize));
            } else {
                // Center the actual text ink, not the font line box. This
                // matches materialIconTight()'s ink-centering and avoids the
                // lowercase title and numeric counters sitting low.
                IconHelper::drawInkCenteredText(&painter,
                                                QRect(x, 0, part.width, height()),
                                                textFont, part.text, 1);
            }
            x += part.width + gap;
        }
    }

private:
    QString m_title;
    int m_behind = 0;
    int m_ahead = 0;
};

QColor iconColorFor(bool dark)
{
    return dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
}

// Hover row actions are secondary affordances. Reuse the Command Palette's
// muted icon colours so they stay legible without competing with file names
// and status glyphs.
QColor rowActionIconColorFor(bool dark)
{
    return dark ? QColor(0xaa, 0xaa, 0xaa) : QColor(0x88, 0x88, 0x88);
}

QString relativeDirectory(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? path.left(slash) : QString();
}

QString fileNameOf(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

// Uses the same compact disclosure stroke for all file-group rows. Qt does
// not draw a branch reliably for empty forced-indicator items; painting group
// branches here therefore keeps populated and empty sections aligned alike.
class GitFileBranchTree final : public QTreeWidget {
public:
    explicit GitFileBranchTree(QWidget* parent = nullptr) : QTreeWidget(parent) {}

    void setDarkProvider(std::function<bool()> provider) { m_darkProvider = std::move(provider); }

protected:
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override
    {
        if (index.data(GitItemRoles::Kind).toInt() != 0 /* GroupRow */) {
            QTreeWidget::drawBranches(painter, rect, index);
            return;
        }

        const bool dark = m_darkProvider && m_darkProvider();
        const bool expanded = isExpanded(index);
        // Deliberately two pixels below the row's geometric center: the
        // chevron's visual center otherwise sits noticeably high beside the
        // section title. This applies to both native child and empty groups.
        const QRect arrowRect(rect.left(), rect.center().y() - 1, rect.width(), 8);
        const QPoint center = arrowRect.center();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen chevronPen(dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x1f, 0x1f, 0x1f));
        chevronPen.setWidthF(1.0);
        chevronPen.setCapStyle(Qt::RoundCap);
        chevronPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(chevronPen);
        painter->setBrush(Qt::NoBrush);
        if (expanded) {
            painter->drawLine(QPoint(center.x() - 4, center.y() - 2),
                              QPoint(center.x(), center.y() + 2));
            painter->drawLine(QPoint(center.x(), center.y() + 2),
                              QPoint(center.x() + 4, center.y() - 2));
        } else {
            painter->drawLine(QPoint(center.x() - 2, center.y() - 4),
                              QPoint(center.x() + 2, center.y()));
            painter->drawLine(QPoint(center.x() + 2, center.y()),
                              QPoint(center.x() - 2, center.y() + 4));
        }
        painter->restore();
    }

private:
    std::function<bool()> m_darkProvider;
};

}

// Paints a file row: icon, name, dimmed directory, right-aligned status
// letter. Modelled on CommandItemDelegate.
class GitFileDelegate final : public QStyledItemDelegate {
public:
    explicit GitFileDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setDarkMode(bool dark) { m_dark = dark; }
    void setInlineActionsRows(const QModelIndex& first, int firstWidth,
                              const QModelIndex& second = QModelIndex(), int secondWidth = 0)
    {
        m_firstInlineActions = first;
        m_firstActionWidth = firstWidth;
        m_secondInlineActions = second;
        m_secondActionWidth = secondWidth;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(kRowHeight);
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        ThemeCard::paintRow(*painter, option.rect, m_dark, option.state & QStyle::State_Selected,
                            option.state & QStyle::State_MouseOver);

        const int kind = index.data(GitItemRoles::Kind).toInt();
        QRect content = option.rect.adjusted(GitUiLayout::kFileRowLeadingInset, 0,
                                             -GitUiLayout::kFileRowTrailingInset, 0);

        const QColor textColor = m_dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x1f, 0x1f, 0x1f);
        const QColor dimColor = m_dark ? QColor(0x8c, 0x8c, 0x8c) : QColor(0x77, 0x77, 0x77);

        if (kind == 0 /* GroupRow */) {
            QFont font = option.font;
            font.setBold(true);
            painter->setFont(font);

            // Center on the same X-axis the file status letters draw
            // flush-right against below, rather than the badge's own
            // right edge -- see GitUiLayout::statusGlyphHalfWidth().
            const QFontMetrics statusMetrics(gitFileStatusFont(option.font));
            const int badgeCenterX = content.right() - GitUiLayout::statusGlyphHalfWidth(statusMetrics);
            const QRect badgeRect(badgeCenterX - kGroupCountBadgeDiameter / 2,
                                  content.top() + (content.height() - kGroupCountBadgeDiameter) / 2,
                                  kGroupCountBadgeDiameter, kGroupCountBadgeDiameter);
            const QColor badgeColor = m_dark ? DarkTheme::color(DarkTheme::Accent)
                                             : QColor(0x00, 0x78, 0xd4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeColor);
            painter->drawEllipse(badgeRect);

            QFont badgeFont = option.font;
            badgeFont.setBold(false);
            badgeFont.setPointSizeF(qMax(9.0, badgeFont.pointSizeF()));
            painter->setFont(badgeFont);
            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter,
                              index.data(GitItemRoles::Count).toString());

            painter->setFont(font);
            painter->setPen(textColor);
            const QRect titleRect(content.left(), content.top(),
                                  badgeRect.left() - content.left() - 8, content.height());
            painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                              option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                            Qt::ElideRight, titleRect.width()));
            painter->restore();
            return;
        }

        // VS Code keeps the status visible while a row's actions are exposed.
        // Reserve the action strip immediately before the fixed right-hand
        // status column instead of replacing that status column.
        const auto state = static_cast<GitFileState>(index.data(GitItemRoles::State).toInt());
        const QString letter(GitRepository::statusLetter(state));
        const int letterWidth = option.fontMetrics.horizontalAdvance(letter) + 8;
        // Center within the row's full content height, exactly like the
        // name/dir text below -- not a hand-picked sub-rect -- so the status
        // glyph and the hover action strip (which sizes itself to this same
        // row height in positionRowActions()) share one vertical center
        // with no separately-rounded offset to drift apart at fractional DPI.
        const QRect letterRect(content.right() - letterWidth, content.top(),
                               letterWidth, content.height());
        painter->setFont(gitFileStatusFont(option.font));
        painter->setPen(gitFileStateColor(state, m_dark));
        painter->drawText(letterRect, Qt::AlignVCenter | Qt::AlignRight, letter);
        painter->setFont(option.font);
        content.setRight(letterRect.left() - 4);

        if (m_firstInlineActions.isValid() && m_firstInlineActions == index)
            content.setRight(content.right() - m_firstActionWidth);
        else if (m_secondInlineActions.isValid() && m_secondInlineActions == index)
            content.setRight(content.right() - m_secondActionWidth);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QRect iconRect(content.left(), content.top() + (content.height() - 16) / 2,
                                 16, 16);
            icon.paint(painter, iconRect);
            content.setLeft(iconRect.right() + 6);
        }

        const QString path = index.data(GitItemRoles::Path).toString();
        const QString name = fileNameOf(path);
        const QString dir = relativeDirectory(path);

        painter->setPen(textColor);
        const int nameWidth = option.fontMetrics.horizontalAdvance(name);
        const QRect nameRect(content.left(), content.top(),
                             qMin(nameWidth, content.width()), content.height());
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
    }

private:
    bool        m_dark = false;
    // Persistent: a plain QModelIndex keeps the removed QTreeWidgetItem as its
    // internal pointer after a refresh clears the tree, and a freshly built
    // item allocated at the same address on the same row then compares equal
    // to it. QPersistentModelIndex is invalidated by the model reset instead.
    QPersistentModelIndex m_firstInlineActions;
    int         m_firstActionWidth = 0;
    QPersistentModelIndex m_secondInlineActions;
    int         m_secondActionWidth = 0;
};

// ---------------------------------------------------------------------------
// GitRowActions

GitRowActions::GitRowActions(QWidget* parent)
    : QWidget(parent)
{
    // The strip is a viewport child and can sit directly under the cursor.
    // Track movement here as well as in the tree viewport so crossing from
    // one row's icon area to another row cannot lose hover updates.
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    auto* layout = new QHBoxLayout(this);
    // This is an inline VS Code-style icon strip, rather than a row of
    // individually boxed controls.  The small right inset keeps the final
    // icon aligned with the file status column.
    layout->setContentsMargins(0, 0, 2, 0);
    // Equal breathing room between the 3 icons -- layout-driven spacing, not
    // per-button margins, so it stays uniform no matter how many actions a
    // row has.
    layout->setSpacing(4);
    // No fixed height here: positionRowActions() sizes this widget to the
    // hovered row's actual rect height (not a hardcoded row-height guess)
    // every time it is placed, and each button is added with Qt::AlignVCenter
    // below so the layout itself -- not a hand-picked margin -- centers it.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    hide();
}

void GitRowActions::setActions(const QVector<Action>& actions, bool dark)
{
    auto* layout = qobject_cast<QHBoxLayout*>(this->layout());
    while (!m_buttons.isEmpty()) {
        QToolButton* button = m_buttons.takeLast();
        layout->removeWidget(button);
        // Unparent before deleteLater(): a merely scheduled-for-deletion
        // child is still in the child list, and anything that walks it
        // would see the stale buttons alongside the new ones.
        button->setParent(nullptr);
        button->deleteLater();
    }

    const QColor color = rowActionIconColorFor(dark);
    for (const Action& action : actions) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("gitRowAction"));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setContentsMargins(0, 0, 0, 0);
        button->setFixedSize(kRowActionSize, kRowActionSize);
        button->setToolTip(action.tooltip);
        button->setProperty("gitRowActionId", action.id);
        // Paint-only: the button never receives its own mouse events (see
        // Qt::WA_TransparentForMouseEvents on this strip). SourceControlPanel does
        // all hit-testing, hover and click dispatch itself from the
        // viewport's events, using this button only for its geometry, icon
        // and tooltip text -- see the eventFilter() comment for why.
        button->setAttribute(Qt::WA_TransparentForMouseEvents);
        // An even-size icon in the even 20px hit area avoids a half-pixel
        // centre at scaled DPI. All row actions share this exact geometry.
        button->setIconSize(QSize(kRowActionIconSize, kRowActionIconSize));
        button->setIcon(IconHelper::materialIcon(QChar(action.iconCode), color,
                                                   kRowActionIconSize));
        layout->addWidget(button, 0, Qt::AlignVCenter);
        m_buttons.append(button);
    }
    // Every button is setFixedSize(), so the strip's width is fully known
    // without asking the layout: layout->sizeHint() was tried here first,
    // but on the second and later calls to setActions() on the same
    // (reused, not recreated) GitRowActions instance it intermittently
    // returned a stale width from before the old buttons were swapped out --
    // observed as low as 2px, silently shrinking the whole strip to a
    // sliver with all 3 buttons crammed inside it and nothing visible.
    // Computing it directly from the margins/spacing/button-count we just
    // built sidesteps whatever QLayout is caching and cannot go stale.
    layout->activate();
    const QMargins margins = layout->contentsMargins();
    int width = margins.left() + margins.right();
    if (!m_buttons.isEmpty())
        width += m_buttons.size() * kRowActionSize + (m_buttons.size() - 1) * layout->spacing();
    setFixedWidth(width);
}

// ---------------------------------------------------------------------------
// SourceControlPanel construction

SourceControlPanel::SourceControlPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sourceControlPanel"));
    m_dark = palette().color(QPalette::Window).lightness() < 128;

    buildUi();
    restoreState();

    m_refreshWatcher = new QFutureWatcher<GitSnapshot>(this);
    connect(m_refreshWatcher, &QFutureWatcher<GitSnapshot>::finished, this, [this] {
        m_refreshRunning = false;
        const GitSnapshot snapshot = m_refreshWatcher->result();
        // A panel action is inside a nested event loop (a confirmation box, a
        // context menu) and is still using the rows and the snapshot this
        // result would replace. Re-run once the loop is done instead.
        if (m_refreshHold > 0) {
            m_refreshDirty = true;
            return;
        }
        // Drop a result the repository has already moved past.
        if (snapshot.generation == m_generation) {
            applySnapshot(snapshot);
            // A successful network operation deliberately keeps its primary
            // action in the busy state until this authoritative snapshot has
            // replaced the pre-operation ahead/behind/file state.
            if (m_clearBusyAfterRefresh) {
                m_clearBusyAfterRefresh = false;
                setBusy(false);
            }
        }
        if (m_refreshDirty) {
            m_refreshDirty = false;
            startRefresh();
        }
    });

    m_networkWatcher = new QFutureWatcher<GitRunner::Result>(this);
    connect(m_networkWatcher, &QFutureWatcher<GitRunner::Result>::finished,
            this, &SourceControlPanel::finishNetworkOperation);

    m_localWatcher = new QFutureWatcher<GitRunner::Result>(this);
    connect(m_localWatcher, &QFutureWatcher<GitRunner::Result>::finished, this, [this] {
        const GitRunner::Result result = m_localWatcher->result();
        const bool sameRoot = m_localInitializing
            ? (m_root.isEmpty() && m_lastCheckedFolder == m_localRoot)
            : (m_localRoot == m_root);
        std::function<void()> onSuccess = std::move(m_localSuccess);
        m_localSuccess = {};
        m_localRoot.clear();
        m_localInitializing = false;
        m_localRunning = false;
        updateActionStates();
        if (!sameRoot)
            return;
        if (!result.ok()) {
            setStatusMessage(result.err.isEmpty()
                ? tr("The Git operation failed.") : result.err, true);
            scheduleRefresh();
            return;
        }
        if (onSuccess)
            onSuccess();
    });
}

SourceControlPanel::~SourceControlPanel()
{
    // QWidget::~QWidget deletes the children before QObject::~QObject drops
    // this panel's connections. The file list's model is destroyed after the
    // list's viewport and the strips parented to it, and its final reset would
    // otherwise run the reset handlers against those dead widgets.
    if (m_fileList)
        m_fileList->model()->disconnect(this);
    persistState();
    // Never leave a git child process behind, and never block shutdown on
    // one: the network watcher owns a QProcess living on a worker thread.
    m_networkCancel.cancel();
    if (m_networkWatcher && m_networkWatcher->isRunning())
        m_networkWatcher->waitForFinished();
    // Refresh workers capture only value types (root/generation/limit), never
    // this panel. Let a read already in flight finish independently instead of
    // blocking an auxiliary window close for a slow repository.
}

void SourceControlPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_mergeBanner = new QLabel(this);
    m_mergeBanner->setObjectName(QStringLiteral("gitMergeBanner"));
    m_mergeBanner->hide();
    root->addWidget(m_mergeBanner);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("gitEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_stack->addWidget(m_emptyLabel);

    m_stack->addWidget(buildNoRepoPage());
    m_stack->addWidget(buildNoGitPage());

    m_content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // One column now: the diff that used to sit beside this sidebar opens as a
    // document tab in the main area instead.
    // Same handle width/palette as the Workspace/Outline and editor/Terminal
    // splitters (see WorkspacePanelController::ensureUi and
    // MainWindow::createWorkspacePanel) so GRAPH's divider reads as the same
    // component at runtime rather than a Git-specific approximation.
    m_sidebarSplitter = new ThemedSplitter(Qt::Vertical, ThemedSplitter::HandleStyle::Divider,
                                           4, m_content);
    m_sidebarSplitter->setObjectName(QStringLiteral("gitSidebar"));
    m_sidebarSplitter->setChildrenCollapsible(false);
    m_sidebarSplitter->addWidget(buildSourceControlSection());
    m_sidebarSplitter->addWidget(buildGraphSection());
    // Both panes must participate in vertical growth, or QSplitter pins the
    // stretch-factor-0 side to an absolute pixel height that becomes
    // disproportionately small/large as the window resizes (see the identical
    // Workspace/Outline fix in WorkspacePanelController::ensureUi). Raw sizes
    // below are only the initial ~48/52% ratio; QSplitter normalizes them
    // against the splitter's actual height on first layout and never
    // reapplies them once the user drags the handle.
    m_sidebarSplitter->setStretchFactor(0, 1);
    m_sidebarSplitter->setStretchFactor(1, 1);
    m_sidebarSplitter->setSizes({ 340, 360 });

    contentLayout->addWidget(m_sidebarSplitter, 1);
    m_stack->addWidget(m_content);
    m_stack->setCurrentWidget(m_emptyLabel);

    // --- status row ---
    // Just an error/confirmation line now -- network-operation progress lives
    // entirely on the primary action button (see setBusy()/updateActionStates()),
    // so there is no separate progress bar or Cancel button here, and this
    // label collapses to zero height (hide()) rather than reserving space
    // whenever there is nothing to say.
    // Multiline git output is shown here in full -- there is deliberately no
    // tooltip duplicating it, and no dialog. Past kStatusMaxHeight the message
    // scrolls inside its own area instead of eating the sidebar, with the
    // dismiss button parked outside the scroller so it never scrolls away.
    auto* statusBar = new GitStatusSurface(this);
    m_statusBar = statusBar;
    m_statusBar->setObjectName(QStringLiteral("gitStatusBar"));
    statusBar->contentHeight = [this] { return m_statusScroll ? m_statusScroll->height() : 0; };
    statusBar->heightDragged = [this](int height) {
        m_statusUserHeight = qBound(kStatusMinHeight, height, kStatusDragMaxHeight);
        updateStatusGeometry();
    };
    statusBar->resized = [this] { updateStatusGeometry(); };
    auto* statusLayout = new QHBoxLayout(m_statusBar);
    // No right inset -- the close button is an overlay, not a column, so the
    // scroll area runs the full width and its scrollbar lands flush with the
    // right edge, below the button, matching the Graph history list's
    // scrollbar instead of being pushed left to leave room for the button.
    statusLayout->setContentsMargins(8, kStatusGripHeight, 0, 4);
    statusLayout->setSpacing(0);

    m_statusLabel = new QLabel(m_statusBar);
    m_statusLabel->setObjectName(QStringLiteral("gitStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_statusScroll = new QScrollArea(m_statusBar);
    m_statusScroll->setObjectName(QStringLiteral("gitStatusScroll"));
    m_statusScroll->setFrameShape(QFrame::NoFrame);
    m_statusScroll->setWidgetResizable(false);
    m_statusScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statusScroll->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_statusScroll));
    m_statusScroll->setWidget(m_statusLabel);
    m_statusScroll->viewport()->setAutoFillBackground(false);
    m_statusScroll->viewport()->installEventFilter(this);
    statusLayout->addWidget(m_statusScroll, 1);

    // Floated over the top-right corner rather than laid out, so it reserves
    // no column of its own.
    m_statusClose = new QToolButton(m_statusBar);
    m_statusClose->setObjectName(QStringLiteral("gitStatusClose"));
    m_statusClose->setAutoRaise(true);
    m_statusClose->setCursor(Qt::ArrowCursor);
    m_statusClose->setFixedSize(kStatusCloseSize, kStatusCloseSize);
    m_statusClose->setIconSize(QSize(kStatusCloseIconSize, kStatusCloseIconSize));
    m_statusClose->setToolTip(tr("Dismiss"));
    connect(m_statusClose, &QToolButton::clicked, this, [this] { setStatusMessage(QString()); });
    updateStatusCloseIcon();

    m_statusBar->hide();
    root->addWidget(m_statusBar);

    // These strips (and every button in them) are purely painted overlays --
    // Qt::WA_TransparentForMouseEvents on both the strip and each button
    // means the viewport is the *only* widget that ever receives mouse
    // events over a row, hover or not. See the eventFilter() comment for why
    // that single-authority design replaced per-widget enter/leave tracking.
    m_hoverActions = new GitRowActions(this);
    m_hoverActions->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Keep the current file's actions visible while another file exposes its
    // own actions on hover, matching VS Code's Source Control list.
    m_selectedActions = new GitRowActions(this);
    m_selectedActions->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Docked in the main window, these must not become window-wide bindings --
    // Ctrl+Enter belongs to the editor and F5 is the window-wide View >
    // Refresh action (MainWindow::m_actRefresh) everywhere else. F5 also has a
    // discoverable "Refresh" entry now (see showFileListContextMenu() and
    // showHistoryContextMenu()); the shortcut stays bound too, both to keep
    // it working for keyboard users and because a WidgetWithChildrenShortcut
    // here is what stops F5 falling through to that window-wide action while
    // focus is inside this panel.
    const auto addLocalShortcut = [this](const QKeySequence& key, auto slot) {
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    addLocalShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { commitChanges(false); });
    addLocalShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this] { commitChanges(false); });
    addLocalShortcut(QKeySequence(Qt::Key_F5), [this] { refresh(); });
}

QWidget* SourceControlPanel::buildNoRepoPage()
{
    auto* page = new QWidget(this);
    m_noRepoPage = page;

    auto* column = new QWidget(page);
    column->setFixedWidth(320);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(8);

    auto* title = new QLabel(tr("Source Control"), column);
    title->setObjectName(QStringLiteral("gitNoRepoTitle"));
    title->setAlignment(Qt::AlignHCenter);
    columnLayout->addWidget(title);

    m_noRepoText = new QLabel(column);
    m_noRepoText->setObjectName(QStringLiteral("gitNoRepoText"));
    m_noRepoText->setWordWrap(true);
    m_noRepoText->setAlignment(Qt::AlignHCenter);
    m_noRepoText->setText(tr(
        "The folder currently open doesn't have a Git repository. "
        "You can initialize a repository to enable source control features powered by Git."));
    columnLayout->addWidget(m_noRepoText);

    columnLayout->addSpacing(8);

    m_btnInitRepo = new QPushButton(tr("Initialize Repository"), column);
    m_btnInitRepo->setObjectName(QStringLiteral("gitInitRepoButton"));
    m_btnInitRepo->setCursor(Qt::PointingHandCursor);
    m_btnInitRepo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_btnInitRepo, &QPushButton::clicked, this, &SourceControlPanel::initializeRepository);
    columnLayout->addWidget(m_btnInitRepo, 0, Qt::AlignHCenter);

    // Center the fixed-width column horizontally, anchored to the top of the
    // page rather than vertically centered.
    auto* rowLayout = new QHBoxLayout;
    rowLayout->addStretch(1);
    rowLayout->addWidget(column);
    rowLayout->addStretch(1);

    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 36, 24, 24);
    outer->addLayout(rowLayout);
    outer->addStretch(1);

    return page;
}

// Git is never bundled, so a machine without it is a first-class state, not an
// error: the mode stays reachable and the panel says exactly what is missing.
QWidget* SourceControlPanel::buildNoGitPage()
{
    auto* page = new QWidget(this);
    m_noGitPage = page;

    auto* column = new QWidget(page);
    column->setFixedWidth(320);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(8);

    auto* title = new QLabel(tr("Source Control"), column);
    title->setObjectName(QStringLiteral("gitNoRepoTitle"));
    title->setAlignment(Qt::AlignHCenter);
    columnLayout->addWidget(title);

    auto* text = new QLabel(column);
    text->setObjectName(QStringLiteral("gitNoRepoText"));
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignHCenter);
    text->setText(tr(
        "Git must be installed to use Source Control. FastMD does not bundle Git; "
        "it uses the git found on your PATH."));
    columnLayout->addWidget(text);

    columnLayout->addSpacing(8);

    m_btnDownloadGit = new QPushButton(tr("Download Git for Windows"), column);
    m_btnDownloadGit->setObjectName(QStringLiteral("gitInitRepoButton"));
    m_btnDownloadGit->setCursor(Qt::PointingHandCursor);
    m_btnDownloadGit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_btnDownloadGit, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://git-scm.com/download/win")));
    });
    columnLayout->addWidget(m_btnDownloadGit, 0, Qt::AlignHCenter);

    auto* rowLayout = new QHBoxLayout;
    rowLayout->addStretch(1);
    rowLayout->addWidget(column);
    rowLayout->addStretch(1);

    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 36, 24, 24);
    outer->addLayout(rowLayout);
    outer->addStretch(1);

    return page;
}

QWidget* SourceControlPanel::buildSourceControlSection()
{
    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("gitSourceContent"));
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 0);
    layout->setSpacing(6);

    m_commitMessage = new QPlainTextEdit(body);
    m_commitMessage->setObjectName(QStringLiteral("gitCommitMessage"));
    m_commitMessage->setPlaceholderText(tr("Message (Ctrl+Enter to commit)"));
    m_commitMessage->setFixedHeight(62);
    m_commitMessage->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_commitMessage));
    connect(m_commitMessage, &QPlainTextEdit::textChanged,
            this, &SourceControlPanel::updateActionStates);
    layout->addWidget(m_commitMessage);

    auto* commitRow = new QWidget(body);
    auto* commitLayout = new QHBoxLayout(commitRow);
    commitLayout->setContentsMargins(0, 0, 0, 0);
    commitLayout->setSpacing(0);

    m_btnCommit = new QPushButton(tr("Commit"), commitRow);
    m_btnCommit->setObjectName(QStringLiteral("gitCommitButton"));
    m_btnCommit->setFixedHeight(kCommitSplitHeight);
    m_btnCommit->setIconSize(QSize(kSyncIconSize, kSyncIconSize));

    // Keep the sync counters in a separate custom-painted label so the
    // Material Icons glyphs can be sized and vertically aligned independently.
    m_syncLabel = new SyncContentLabel(m_btnCommit);
    m_syncLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_syncLabel->setAlignment(Qt::AlignCenter);
    m_syncLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    m_syncLabel->hide();
    auto* syncLabelLayout = new QHBoxLayout(m_btnCommit);
    syncLabelLayout->setContentsMargins(0, 0, 0, 0);
    syncLabelLayout->addWidget(m_syncLabel);

    connect(m_btnCommit, &QPushButton::clicked, this, [this] {
        // The button stays visually enabled while a network op runs (see
        // updateActionStates()) so its icon/colors don't get Qt's disabled
        // treatment -- the click is ignored here instead of via setEnabled(false).
        if (m_busy)
            return;
        switch (m_primaryAction) {
        case PrimaryGitAction::Sync:    syncChanges();       break;
        case PrimaryGitAction::Publish: pushToUpstream();    break;
        case PrimaryGitAction::Commit:  commitChanges(false); break;
        }
    });
    commitLayout->addWidget(m_btnCommit, 1);

    m_btnCommitMenu = new QToolButton(commitRow);
    m_btnCommitMenu->setObjectName(QStringLiteral("gitCommitMenuButton"));
    m_btnCommitMenu->setFixedSize(kCommitMenuWidth, kCommitSplitHeight);
    m_btnCommitMenu->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnCommitMenu->setArrowType(Qt::DownArrow);
    m_btnCommitMenu->setCursor(Qt::PointingHandCursor);
    connect(m_btnCommitMenu, &QToolButton::clicked, this, [this] {
        // exec() below is a nested event loop, and everything chosen from
        // this menu acts on the snapshot the menu was built from.
        RefreshHold hold(this);
        AlignedMenu menu(this);
        menu.setObjectName(QStringLiteral("gitContextMenu"));
        menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
        menu.setShowAnimationDuration(80);
        QAction* commitAndPush = menu.addAction(tr("Commit and Push"));
        commitAndPush->setEnabled(m_btnCommit->isEnabled());

        bool hasTracked = false;
        bool hasUntracked = false;
        for (const GitFileEntry& entry : m_snapshot.files) {
            if (entry.state == GitFileState::Untracked)
                hasUntracked = true;
            else
                hasTracked = true;
        }

        QAction* stash = menu.addAction(tr("Stash Changes…"));
        stash->setEnabled(hasTracked);
        QAction* stashIncludingUntracked = menu.addAction(tr("Stash Changes Including Untracked…"));
        stashIncludingUntracked->setEnabled(hasTracked || hasUntracked);
        // Amend is deliberately absent: it rewrites a commit that may
        // already be pushed, and this window cannot show what that costs.
        QAction* chosen = menu.exec(m_btnCommitMenu->mapToGlobal(
            QPoint(0, m_btnCommitMenu->height())));
        if (chosen == commitAndPush)
            commitChanges(true);
        else if (chosen == stash)
            stashCurrentChanges(false);
        else if (chosen == stashIncludingUntracked)
            stashCurrentChanges(true);
    });
    commitLayout->addWidget(m_btnCommitMenu);
    layout->addWidget(commitRow);

    auto* fileList = new GitFileBranchTree(body);
    fileList->setDarkProvider([this] { return m_dark; });
    m_fileList = fileList;
    m_fileList->setObjectName(QStringLiteral("gitFileList"));
    m_fileList->setHeaderHidden(true);
    m_fileList->setRootIsDecorated(true);
    m_fileList->setIndentation(10);
    m_fileList->setUniformRowHeights(true);
    m_fileList->setAnimated(false);
    m_fileList->setMouseTracking(true);
    setReadOnlyItemViewCursor(m_fileList);
    // QAbstractScrollArea delivers row mouse events to its viewport. Enabling
    // tracking only on the QTreeWidget leaves this event filter idle until a
    // button is pressed, so the contextual strip can never be revealed by a
    // normal hover.
    m_fileList->viewport()->setMouseTracking(true);
    m_fileList->viewport()->setAttribute(Qt::WA_Hover, true);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_fileList->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_fileList));
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileDelegate = new GitFileDelegate(m_fileList);
    m_fileDelegate->setDarkMode(m_dark);
    m_fileList->setItemDelegate(m_fileDelegate);
    m_fileList->viewport()->installEventFilter(this);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QTreeWidget::customContextMenuRequested,
            this, &SourceControlPanel::showFileListContextMenu);
    // m_hoverItem/m_selectedItem are raw QTreeWidgetItem pointers, so they must
    // be dropped by the model itself rather than by every caller that happens
    // to remove rows: QTreeWidget::clear() resets the model, and any later
    // use of those pointers would be a use-after-free.
    connect(m_fileList->model(), &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        hideHoverActions();
        hideSelectedActions();
    });
    connect(m_fileList->model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] {
        hideHoverActions();
        hideSelectedActions();
    });
    connect(m_fileList, &QTreeWidget::itemSelectionChanged,
            this, &SourceControlPanel::onFileSelectionChanged);
    connect(m_fileList, &QTreeWidget::itemDoubleClicked,
            this, &SourceControlPanel::onFileActivated);
    layout->addWidget(m_fileList, 1);

    return body;
}

// ---------------------------------------------------------------------------
// Repository selection and refresh

void SourceControlPanel::setRepositoryFromWorkspace(const QString& workspaceRoot)
{
    m_lastCheckedFolder = workspaceRoot;
    const QString root = GitRepository::findRoot(workspaceRoot);
    const bool sameRepo = (root == m_root && !root.isEmpty());

    if (!sameRepo) {
        // Invalidate a refresh from the previous workspace even when the new
        // workspace has no repository. scheduleRefresh() advances the
        // generation for repository-to-repository switches, but it is not
        // called for an empty root; without this increment the old snapshot
        // could arrive later and repaint the no-repository page with stale
        // files and history.
        ++m_generation;
        m_root = root;
        m_commitLimit = 100;
        m_selectedCommitHash.clear();
        m_selectedCommitParent.clear();
        m_commitFilesCache.clear();
        m_commitFilesLoading.clear();
    }
    applyRepositoryState();
}

// One place decides which page the panel shows. Git availability is probed on a
// worker exactly once per process: GitRunner::isAvailable() spawns `git
// --version` on its first call, which must never happen on the UI thread.
void SourceControlPanel::applyRepositoryState()
{
    if (!m_gitAvailable.has_value()) {
        if (!m_availabilityWatcher) {
            m_availabilityWatcher = new QFutureWatcher<bool>(this);
            connect(m_availabilityWatcher, &QFutureWatcher<bool>::finished, this, [this] {
                m_gitAvailable = m_availabilityWatcher->result();
                FASTMD_MARK("git availability probe complete");
                applyRepositoryState();
            });
            m_availabilityWatcher->setFuture(
                QtConcurrent::run([] { return GitRunner::isAvailable(); }));
        }
        showRepositoryMessage(tr("Checking for Git…"));
        return;
    }

    if (!*m_gitAvailable) {
        showNoGitState();
        return;
    }
    if (m_root.isEmpty()) {
        if (m_lastCheckedFolder.isEmpty())
            showRepositoryMessage(tr("No folder is open. Open a folder to use Source Control."));
        else
            showNoRepositoryState(m_lastCheckedFolder);
        return;
    }
    scheduleRefresh();
}

void SourceControlPanel::showRepositoryMessage(const QString& message)
{
    m_emptyLabel->setText(message);
    m_stack->setCurrentWidget(m_emptyLabel);
    if (m_branchButton)
        m_branchButton->setBranchText(QString());
    m_mergeBanner->hide();
    updateActionStates();
}

void SourceControlPanel::showNoRepositoryState(const QString& workspaceRoot)
{
    Q_UNUSED(workspaceRoot);
    m_stack->setCurrentWidget(m_noRepoPage);
    if (m_branchButton)
        m_branchButton->setBranchText(QString());
    m_mergeBanner->hide();
    updateActionStates();
}

void SourceControlPanel::showNoGitState()
{
    m_stack->setCurrentWidget(m_noGitPage);
    if (m_branchButton)
        m_branchButton->setBranchText(QString());
    m_mergeBanner->hide();
    updateActionStates();
}

void SourceControlPanel::focusCommitMessage()
{
    if (m_commitMessage && m_commitMessage->isVisible())
        m_commitMessage->setFocus(Qt::OtherFocusReason);
}

void SourceControlPanel::refresh()
{
    // The availability watcher's completion performs the initial refresh.
    // While availability is unknown, starting another worker would only block
    // on the same function-local Git probe; once Git is known missing, window
    // activation must remain entirely idle.
    if (m_gitAvailable.value_or(false))
        scheduleRefresh();
}

void SourceControlPanel::refreshForSavedPath(const QString& path)
{
    if (m_root.isEmpty() || path.isEmpty())
        return;

    const QString savedPath = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
    const QString relativePath = QDir::fromNativeSeparators(
        QDir(m_root).relativeFilePath(savedPath));
    if (relativePath == QStringLiteral("..")
        || relativePath.startsWith(QStringLiteral("../"))) {
        return;
    }

    refresh();
}

SourceControlPanel::RefreshHold::RefreshHold(SourceControlPanel* panel)
    : m_panel(panel)
{
    if (m_panel)
        ++m_panel->m_refreshHold;
}

SourceControlPanel::RefreshHold::~RefreshHold()
{
    if (m_panel)
        m_panel->releaseRefreshHold();
}

void SourceControlPanel::releaseRefreshHold()
{
    if (m_refreshHold > 0)
        --m_refreshHold;
    if (m_refreshHold > 0 || m_refreshRunning || !m_refreshDirty)
        return;
    m_refreshDirty = false;
    startRefresh();
}

void SourceControlPanel::scheduleRefresh()
{
    if (m_root.isEmpty())
        return;
    ++m_generation;
    if (m_refreshRunning || m_refreshHold > 0) {
        // Exactly one refresh in flight -- and none at all while a nested
        // event loop holds the rows; the repo may have moved on, so remember
        // to run again rather than queueing an unbounded number.
        m_refreshDirty = true;
        return;
    }
    startRefresh();
}

void SourceControlPanel::startRefresh()
{
    if (m_root.isEmpty() || m_refreshRunning)
        return;

    m_refreshRunning = true;
    const QString root = m_root;
    const int generation = m_generation;
    const int limit = m_commitLimit;

    m_refreshWatcher->setFuture(QtConcurrent::run([root, generation, limit] {
        GitSnapshot snapshot;
        snapshot.generation = generation;
        snapshot.root = root;

        if (!GitRunner::isAvailable()) {
            snapshot.gitMissing = true;
            return snapshot;
        }

        const GitRunner::Result status = GitRunner::run(
            root, { QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                    QStringLiteral("-z"), QStringLiteral("--untracked-files=all") });
        if (!status.ok()) {
            snapshot.error = status.err.isEmpty()
                ? QObject::tr("git status failed.") : status.err;
            return snapshot;
        }
        snapshot.files = GitRepository::parseStatus(status.out);

        const GitRunner::Result branch = GitRunner::run(
            root, { QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                    QStringLiteral("--branch"), QStringLiteral("-z") });
        if (branch.ok())
            snapshot.branch = GitRepository::parseBranchHeader(branch.out);

        const GitRunner::Result log = GitRunner::run(
            root, { QStringLiteral("log"),
                    QStringLiteral("--max-count=%1").arg(limit + 1),
                    QStringLiteral("--pretty=format:%H%x1f%h%x1f%an%x1f%aI%x1f%s%x1f%P") });
        if (log.ok()) {
            snapshot.commits = GitRepository::parseLog(log.out);
            if (snapshot.commits.size() > limit) {
                snapshot.commits.resize(limit);
                snapshot.moreCommits = true;
            }
            GitRepository::assignLanes(snapshot.commits);
        }

        const GitRunner::Result remotes = GitRunner::run(
            root, { QStringLiteral("remote"), QStringLiteral("-v") });
        if (remotes.ok())
            snapshot.remotes = GitRepository::parseRemotes(remotes.out);

        // The branch selector's list, read on the same worker as everything
        // else so opening the popup never runs git on the UI thread.
        const GitRunner::Result branches = GitRunner::run(
            root, { QStringLiteral("for-each-ref"),
                    QStringLiteral("--format=%(refname)%1f%(refname:short)%1f%(HEAD)"),
                    QStringLiteral("refs/heads"), QStringLiteral("refs/remotes") });
        if (branches.ok())
            snapshot.branches = GitRepository::parseBranchRefs(branches.out);

        snapshot.merging = QFileInfo::exists(root + QStringLiteral("/.git/MERGE_HEAD"));
        snapshot.valid = true;
        return snapshot;
    }));
}

void SourceControlPanel::applySnapshot(const GitSnapshot& snapshot)
{
    m_snapshot = snapshot;
    m_moreCommitsRequested = false;

    if (snapshot.gitMissing) {
        m_gitAvailable = false;
        showNoGitState();
        return;
    }

    if (!snapshot.valid) {
        showRepositoryMessage(snapshot.error.isEmpty()
            ? tr("This folder could not be read as a Git repository.")
            : snapshot.error);
        return;
    }

    m_stack->setCurrentWidget(m_content);
    updateHeader(snapshot);
    populateFiles(snapshot);
    populateHistory(snapshot);
    updateActionStates();
    // Open working-tree diff tabs may now be stale.
    emit refreshed();
}

void SourceControlPanel::updateHeader(const GitSnapshot& snapshot)
{
    const GitBranchInfo& branch = snapshot.branch;
    if (m_branchButton) {
        m_branchButton->setBranchText(branch.detached
            ? tr("detached at %1").arg(branch.branch) : branch.branch);
        m_branchButton->setToolTip(branch.detached
            ? tr("HEAD is detached — click to switch branch")
            : tr("Current branch: %1 — click to switch").arg(branch.branch));
    }
    // The list behind the popup moved with the rest of the snapshot.
    if (m_branchPopup && m_branchPopup->isVisible())
        m_branchPopup->setBranches(snapshot.branches);

    if (snapshot.merging) {
        m_mergeBanner->setText(tr("  Merging — resolve the conflicts below, then commit."));
        m_mergeBanner->show();
    } else {
        m_mergeBanner->hide();
    }

    if (!m_commitMessage->placeholderText().contains(branch.branch) && !branch.branch.isEmpty()) {
        m_commitMessage->setPlaceholderText(
            tr("Message (Ctrl+Enter to commit on \"%1\")").arg(branch.branch));
    }
}

void SourceControlPanel::populateFiles(const GitSnapshot& snapshot)
{
    // Remember the selection so a refresh triggered by staging does not
    // throw the user back to the top of the list.
    QString selectedPath;
    bool selectedStaged = false;
    if (QTreeWidgetItem* current = m_fileList->currentItem()) {
        if (current->data(0, GitItemRoles::Kind).toInt() == FileRow) {
            selectedPath = current->data(0, GitItemRoles::Path).toString();
            selectedStaged = current->data(0, GitItemRoles::Staged).toBool();
        }
    }

    hideSelectedActions();
    hideHoverActions();
    m_fileList->clear();

    struct Group {
        FileGroup id;
        QString   title;
        QVector<GitFileEntry> entries;
    };
    QVector<Group> groups {
        { ConflictGroup, tr("Merge Conflicts"), {} },
        { StagedGroup,   tr("Staged Changes"),  {} },
        { UnstagedGroup, tr("Changes"),         {} },
    };

    for (const GitFileEntry& entry : snapshot.files) {
        if (entry.state == GitFileState::Conflicted)
            groups[0].entries.append(entry);
        else if (entry.staged)
            groups[1].entries.append(entry);
        else
            groups[2].entries.append(entry);
    }

    for (Group& group : groups)
        GitRepository::sortEntries(group.entries, m_sortMode);

    const bool hasStagedOrUnstaged = !groups[StagedGroup].entries.isEmpty()
        || !groups[UnstagedGroup].entries.isEmpty();

    QTreeWidgetItem* toSelect = nullptr;
    for (const Group& group : groups) {
        // Keep both normal sections visible whenever either one has changes,
        // so the empty side still communicates a count of zero. Merge
        // Conflicts remains conditional as before.
        if (group.entries.isEmpty()
            && (group.id == ConflictGroup || !hasStagedOrUnstaged))
            continue;

        auto* groupItem = new QTreeWidgetItem(m_fileList);
        groupItem->setText(0, group.title);
        groupItem->setData(0, GitItemRoles::Kind, GroupRow);
        groupItem->setData(0, GitItemRoles::Group, group.id);
        groupItem->setData(0, GitItemRoles::Count, group.entries.size());
        groupItem->setData(0, GitItemRoles::Expanded, true);
        // An empty tree item otherwise has no branch indicator. Keep empty
        // normal sections structurally identical to populated groups,
        // including their usable expand/collapse chevrons.
        if (group.id != ConflictGroup && group.entries.isEmpty())
            groupItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        groupItem->setExpanded(true);
        groupItem->setFlags(Qt::ItemIsEnabled);

        for (const GitFileEntry& entry : group.entries) {
            auto* item = new QTreeWidgetItem(groupItem);
            item->setData(0, GitItemRoles::Kind, FileRow);
            item->setData(0, GitItemRoles::Path, entry.path);
            item->setData(0, GitItemRoles::Staged, entry.staged);
            item->setData(0, GitItemRoles::State, static_cast<int>(entry.state));
            item->setData(0, GitItemRoles::Group, group.id);
            item->setIcon(0, gitFileIconForPath(entry.path, m_root));
            if (entry.path == selectedPath && entry.staged == selectedStaged)
                toSelect = item;
        }
    }

    if (toSelect)
        m_fileList->setCurrentItem(toSelect);

}

void SourceControlPanel::setSortMode(ChangesSortMode mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    QSettings settings;
    settings.setValue(QStringLiteral("git/changesSortMode"), static_cast<int>(mode));
    // No git call needed -- the current snapshot just needs re-laying-out.
    populateFiles(m_snapshot);
}

// ---------------------------------------------------------------------------
// Selection and diff display

void SourceControlPanel::onFileSelectionChanged()
{
    QTreeWidgetItem* item = m_fileList->currentItem();
    if (!item || item->data(0, GitItemRoles::Kind).toInt() != FileRow) {
        hideSelectedActions();
        return;
    }
    // Keyboard selection only selects; mouse clicks open the diff through the
    // item's click signal below.
    updateSelectedActions(item);

    // The action strip is a viewport child, outside the delegate's selected
    // state, so it must switch to the selected-row icon contrast itself.
    // This rebuilds its buttons in place and never hides/shows the widget,
    // so the hover Leave/Enter churn that hiding-then-reshowing would cause
    // never happens in the first place.
    if (m_hoverActions && m_hoverActions->isVisible() && m_hoverTree == m_fileList)
        refreshHoverActionsAppearance();
}

// A deliberate double-click opens the diff; a single click only selects, so
// keyboard navigation and ordinary selection never spawn document tabs.
// Opening the working file itself remains available from the row's Open action.
void SourceControlPanel::onFileActivated(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item || item->data(0, GitItemRoles::Kind).toInt() != FileRow)
        return;
    showFileDiff(item->data(0, GitItemRoles::Path).toString(),
                 item->data(0, GitItemRoles::Staged).toBool());
}

// The git command itself lives with the diff document (MainWindow::openGitDiff),
// which runs it off the UI thread; this panel only names what to compare.
void SourceControlPanel::showFileDiff(const QString& path, bool staged)
{
    if (m_root.isEmpty() || path.isEmpty())
        return;

    GitDiffSpec spec;
    spec.repoRoot = m_root;
    spec.path = path;
    spec.source = staged ? GitDiffSpec::Index : GitDiffSpec::WorkingTree;
    emit diffRequested(spec);
}

// The Changes list has no per-row context menu (stage/discard already live on
// the hover action strip); this exists to give the manual refresh a
// discoverable menu entry alongside its existing F5 shortcut, plus the
// Changes ordering (persisted via m_sortMode/setSortMode()).
void SourceControlPanel::showFileListContextMenu(const QPoint& pos)
{
    RefreshHold hold(this);

    AlignedMenu menu(m_fileList);
    menu.setObjectName(QStringLiteral("gitContextMenu"));
    menu.setMinimumWidth(200);
    menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
    menu.setShowAnimationDuration(80);

    QAction* refreshAction = menu.addAction(tr("Refresh"));
    // Display-only: the real binding is the panel's own WidgetWithChildrenShortcut
    // F5 (buildSourceControlSection()), not this transient action -- setting it
    // here just labels the menu entry with the key that already works. The menu
    // and this action are rebuilt every time and only live for exec(), so it
    // never becomes a second, window-wide F5 binding.
    refreshAction->setShortcut(QKeySequence(Qt::Key_F5));

    menu.addSeparator();
    QAction* sortByName = menu.addAction(tr("Sort Changes by Name"));
    QAction* sortByPath = menu.addAction(tr("Sort Changes by Path"));
    QAction* sortByStatus = menu.addAction(tr("Sort Changes by Status"));
    QAction* sortByFileType = menu.addAction(tr("Sort Changes by File Type"));
    for (QAction* act : { sortByName, sortByPath, sortByStatus, sortByFileType })
        act->setCheckable(true);
    sortByName->setChecked(m_sortMode == ChangesSortMode::Name);
    sortByPath->setChecked(m_sortMode == ChangesSortMode::Path);
    sortByStatus->setChecked(m_sortMode == ChangesSortMode::Status);
    sortByFileType->setChecked(m_sortMode == ChangesSortMode::FileType);

    QAction* chosen = menu.exec(m_fileList->viewport()->mapToGlobal(pos));
    if (chosen == refreshAction)
        refresh();
    else if (chosen == sortByName)
        setSortMode(ChangesSortMode::Name);
    else if (chosen == sortByPath)
        setSortMode(ChangesSortMode::Path);
    else if (chosen == sortByStatus)
        setSortMode(ChangesSortMode::Status);
    else if (chosen == sortByFileType)
        setSortMode(ChangesSortMode::FileType);
}

// ---------------------------------------------------------------------------
// Hover actions

// Every action strip and every button inside it carries
// Qt::WA_TransparentForMouseEvents, so none of them can ever become "the
// widget under the mouse" -- the viewport is the single, permanent target
// for every mouse event over the file list, hover or not. That single
// authority is what makes this filter simple: there is no enter/leave
// handoff between sibling widgets to reconcile, because there is only ever
// one widget in the running. The strips exist purely to be positioned,
// painted and hit-tested by rectangle -- see setActions() in GitRowActions.
bool SourceControlPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Qt hides a Qt::Popup before the widget under the cursor sees the press,
    // so a click on the selector while the popup is open would otherwise read
    // as "close, then open again". Record that this hide was the selector's
    // own click; openBranchPopup() consumes it.
    if (watched == m_branchPopup && event->type() == QEvent::Hide) {
        m_branchPopupDismissedByButton = false;
        if (m_branchButton && (QApplication::mouseButtons() & Qt::LeftButton)) {
            const QRect buttonRect(m_branchButton->mapToGlobal(QPoint(0, 0)),
                                   m_branchButton->size());
            m_branchPopupDismissedByButton = buttonRect.contains(QCursor::pos());
        }
        return QWidget::eventFilter(watched, event);
    }

    if (m_statusScroll && watched == m_statusScroll->viewport() &&
        event->type() == QEvent::Resize) {
        updateStatusGeometry();
        return QWidget::eventFilter(watched, event);
    }

    if (!m_fileList || watched != m_fileList->viewport())
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        const QPoint pos = static_cast<QMouseEvent*>(event)->pos();
        updateHoverActions(m_fileList, pos);
        updateRowActionPointerFeedback(pos);
        break;
    }
    case QEvent::Leave: {
        // Qt::WA_TransparentForMouseEvents keeps mouse *clicks and moves*
        // routed to the viewport, but Enter/Leave synthesis is a separate,
        // more primitive "what's geometrically under the cursor" check that
        // does not honor that attribute. The instant the cursor's pixel
        // reaches a (transparent) button, the viewport still gets a real
        // Leave here even though the pointer never actually left the row
        // list. Only treat it as a real departure if the cursor is actually
        // outside the viewport now.
        const bool reallyOutside = !m_fileList->viewport()->rect().contains(
            m_fileList->viewport()->mapFromGlobal(QCursor::pos()));
        if (reallyOutside) {
            hideHoverActions();
            QToolTip::hideText();
            m_fileList->viewport()->unsetCursor();
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint pos = mouseEvent->pos();
        QTreeWidgetItem* itemAtCursor = m_fileList->itemAt(pos);
        if (mouseEvent->button() == Qt::LeftButton && itemAtCursor
            && itemAtCursor->data(0, GitItemRoles::Kind).toInt() == GroupRow
            && itemAtCursor->data(0, GitItemRoles::Count).toInt() == 0
            // The disclosure triangle is the tree's leftmost 30px. Empty
            // items have no native branch hit target, so keep this click path
            // paired with the tree's explicit empty-section chevron.
            && pos.x() <= m_fileList->indentation() * 3) {
            const bool expanded = !itemAtCursor->data(0, GitItemRoles::Expanded).toBool();
            itemAtCursor->setData(0, GitItemRoles::Expanded, expanded);
            itemAtCursor->setExpanded(expanded);
            m_fileList->viewport()->update();
            return true;
        }
        // The hold is what makes acting on a row safe: handleRowAction() can
        // open a confirmation box, and a refresh landing in that nested loop
        // would delete `item` (and every other row) underneath this call.
        auto triggerAt = [this, pos](GitRowActions* strip, QTreeWidgetItem* item) {
            if (!strip || !strip->isVisible() || !item)
                return false;
            for (QToolButton* button : strip->findChildren<QToolButton*>()) {
                const QRect rect(button->mapTo(m_fileList->viewport(), QPoint()),
                                 button->size());
                if (!rect.contains(pos))
                    continue;
                const int actionId = button->property("gitRowActionId").toInt();
                RefreshHold hold(this);
                handleRowAction(actionId, item);
                return true;
            }
            return false;
        };
        if (triggerAt(m_selectedActions, m_selectedItem)
            || triggerAt(m_hoverActions, m_hoverItem))
            return true;
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

// Cursor shape and tooltip are the one bit of native per-button feedback
// worth keeping now that the buttons themselves never receive events; both
// are resolved the same way clicks are, by testing the pointer against each
// visible strip's button rectangles.
void SourceControlPanel::updateRowActionPointerFeedback(const QPoint& viewportPos)
{
    QWidget* viewport = m_fileList->viewport();
    for (GitRowActions* strip : { m_selectedActions, m_hoverActions }) {
        if (!strip || !strip->isVisible())
            continue;
        for (QToolButton* button : strip->findChildren<QToolButton*>()) {
            const QRect rect(button->mapTo(viewport, QPoint()), button->size());
            if (!rect.contains(viewportPos))
                continue;
            viewport->setCursor(Qt::PointingHandCursor);
            if (!button->toolTip().isEmpty())
                QToolTip::showText(viewport->mapToGlobal(viewportPos), button->toolTip(), viewport, rect);
            return;
        }
    }
    viewport->unsetCursor();
    QToolTip::hideText();
}

// The strip is parented to the viewport, so its position is always in
// viewport coordinates -- never mapped through the window.
void SourceControlPanel::positionRowActions(GitRowActions* actions, QTreeWidget* tree,
                                   QTreeWidgetItem* item)
{
    const QRect rect = tree->visualItemRect(item);
    // Size the strip to the row's real, uniform rect height -- not a
    // hardcoded row-height constant -- so it is centered by construction:
    // its own height equals the row's height exactly, and Qt::AlignVCenter
    // in setActions() centers each button within that, with no separately
    // computed inset that could drift from the delegate's painted content
    // at fractional display scaling.
    actions->setFixedHeight(rect.height());
    actions->adjustSize();
    // Place against the item's painted right edge, not the viewport's
    // nominal width. This keeps the controls aligned with the delegate's
    // status column when the sidebar is resized or a scrollbar is shown.
    int actionX = rect.right() - actions->width() - 3;
    if (tree == m_fileList && item->data(0, GitItemRoles::Kind).toInt() == GroupRow) {
        // Group count badges own the far-right edge, centered on the same
        // status-column X-axis as the file status letters (see
        // GitFileDelegate::paint()); group actions appear immediately to
        // their left when the row is hovered.
        const QFontMetrics statusMetrics(gitFileStatusFont(tree->font()));
        const int badgeCenterX = rect.right() - GitUiLayout::kFileRowTrailingInset
                                  - GitUiLayout::statusGlyphHalfWidth(statusMetrics);
        const int badgeLeft = badgeCenterX - kGroupCountBadgeDiameter / 2;
        actionX = badgeLeft - 8 - actions->width();
    } else if (tree == m_fileList && item->data(0, GitItemRoles::Kind).toInt() == FileRow) {
        const auto state = static_cast<GitFileState>(item->data(0, GitItemRoles::State).toInt());
        const QString letter(GitRepository::statusLetter(state));
        const int letterWidth = tree->fontMetrics().horizontalAdvance(letter) + 8;
        // Match GitFileDelegate: item right inset, status width, then the
        // 4px breathing room before the action strip.
        actionX -= letterWidth + GitUiLayout::kFileRowTrailingInset;
    }
    actions->move(actionX, rect.top());
}

QVector<GitRowActions::Action> SourceControlPanel::hoverActionsFor(QTreeWidgetItem* item) const
{
    QVector<GitRowActions::Action> actions;
    if (!item)
        return actions;

    const int kind = item->data(0, GitItemRoles::Kind).toInt();
    if (kind == GroupRow) {
        switch (static_cast<FileGroup>(item->data(0, GitItemRoles::Group).toInt())) {
        case UnstagedGroup:
            actions << GitRowActions::Action{ ActDiscardAll, tr("Discard all changes"), kIconDiscard }
                    << GitRowActions::Action{ ActStageAll, tr("Stage all changes"), kIconStage };
            break;
        case StagedGroup:
            actions << GitRowActions::Action{ ActUnstageAll, tr("Unstage all changes"), kIconUnstage };
            break;
        case ConflictGroup:
            break;
        }
    } else if (kind == FileRow) {
        const auto state = static_cast<GitFileState>(item->data(0, GitItemRoles::State).toInt());
        if (state == GitFileState::Conflicted) {
            actions << GitRowActions::Action{ ActOpenFile, tr("Open file"), kIconOpen }
                    << GitRowActions::Action{ ActResolve, tr("Mark as resolved"), kIconResolve };
        } else if (item->data(0, GitItemRoles::Staged).toBool()) {
            actions << GitRowActions::Action{ ActOpenFile, tr("Open file"), kIconOpen }
                    << GitRowActions::Action{ ActUnstage, tr("Unstage"), kIconUnstage };
        } else {
            actions << GitRowActions::Action{ ActOpenFile, tr("Open file"), kIconOpen }
                    << GitRowActions::Action{ ActDiscard, tr("Discard changes"), kIconDiscard }
                    << GitRowActions::Action{ ActStage, tr("Stage"), kIconStage };
        }
    }
    return actions;
}

void SourceControlPanel::updateHoverActions(QTreeWidget* tree, const QPoint& viewportPos)
{
    if (!m_hoverActions)
        return;
    QTreeWidgetItem* item = tree->itemAt(viewportPos);
    if (!item) {
        hideHoverActions();
        return;
    }

    // The current file already owns the persistent strip. Do not create a
    // second overlay on the same row merely because it is hovered.
    if (tree == m_fileList && item == m_selectedItem) {
        hideHoverActions();
        return;
    }

    if (item == m_hoverItem && m_hoverTree == tree && m_hoverActions->isVisible()) {
        positionRowActions(m_hoverActions, tree, item);   // same row, only the position can have moved
        return;
    }

    const QVector<GitRowActions::Action> actions = hoverActionsFor(item);
    if (actions.isEmpty()) {
        hideHoverActions();
        return;
    }

    m_hoverTree = tree;
    m_hoverItem = item;
    m_hoverActions->setParent(tree->viewport());
    m_hoverActions->setActions(actions, m_dark);

    positionRowActions(m_hoverActions, tree, item);
    m_hoverActions->show();
    m_hoverActions->raise();
    updateInlineActionRows();
}

// Rebuilds the currently hovered row's action buttons in place (new colors
// for a just-changed selection state) without hiding the strip -- hiding it
// would tear down and recreate the QToolButtons under a stationary pointer
// for no reason. See onFileSelectionChanged().
void SourceControlPanel::refreshHoverActionsAppearance()
{
    if (!m_hoverActions || !m_hoverTree || !m_hoverItem)
        return;

    const QVector<GitRowActions::Action> actions = hoverActionsFor(m_hoverItem);
    if (actions.isEmpty()) {
        hideHoverActions();
        return;
    }

    m_hoverActions->setActions(actions, m_dark);
    positionRowActions(m_hoverActions, m_hoverTree, m_hoverItem);
    updateInlineActionRows();
}

void SourceControlPanel::hideHoverActions()
{
    if (!m_hoverActions)
        return;
    m_hoverActions->hide();
    if (m_hoverTree)
        m_hoverTree->viewport()->update();
    m_hoverItem = nullptr;
    updateInlineActionRows();
}

void SourceControlPanel::updateSelectedActions(QTreeWidgetItem* item)
{
    if (!m_selectedActions)
        return;
    const QVector<GitRowActions::Action> actions = hoverActionsFor(item);
    if (actions.isEmpty()) {
        hideSelectedActions();
        return;
    }

    m_selectedItem = item;
    m_selectedActions->setParent(m_fileList->viewport());
    m_selectedActions->setActions(actions, m_dark);
    positionRowActions(m_selectedActions, m_fileList, item);
    m_selectedActions->show();
    m_selectedActions->raise();
    updateInlineActionRows();
}

void SourceControlPanel::hideSelectedActions()
{
    if (!m_selectedActions)
        return;
    m_selectedActions->hide();
    m_selectedItem = nullptr;
    updateInlineActionRows();
}

void SourceControlPanel::updateInlineActionRows()
{
    if (!m_fileDelegate || !m_fileList)
        return;

    const QModelIndex selected = (m_selectedActions && m_selectedActions->isVisible()
                                  && m_selectedItem)
        ? m_fileList->indexFromItem(m_selectedItem) : QModelIndex();
    const QModelIndex hovered = (m_hoverActions && m_hoverActions->isVisible()
                                 && m_hoverTree == m_fileList && m_hoverItem
                                 && m_hoverItem != m_selectedItem)
        ? m_fileList->indexFromItem(m_hoverItem) : QModelIndex();
    m_fileDelegate->setInlineActionsRows(selected,
                                         selected.isValid() ? m_selectedActions->width() + 6 : 0,
                                         hovered,
                                         hovered.isValid() ? m_hoverActions->width() + 6 : 0);
    m_fileList->viewport()->update();
}

// `item` is passed in rather than read back from m_hoverItem/m_selectedItem:
// the caller holds a RefreshHold for the duration, which is what keeps the row
// alive across the confirmation box some of these actions open.
void SourceControlPanel::handleRowAction(int id, QTreeWidgetItem* item)
{
    if (!item)
        return;

    const QString path = item->data(0, GitItemRoles::Path).toString();
    const auto actionPaths = [this, item](GitFileAction action) {
        return rowActionPaths(action, item);
    };
    auto pathsInGroup = [](QTreeWidgetItem* groupItem) {
        QStringList paths;
        for (int i = 0; i < groupItem->childCount(); ++i) {
            const QString path = groupItem->child(i)->data(0, GitItemRoles::Path).toString();
            if (!path.isEmpty())
                paths << path;
        }
        return paths;
    };

    switch (id) {
    case ActOpenFile:
        emit openFileRequested(m_root + QLatin1Char('/') + path);
        break;
    case ActStage:
        stagePaths(actionPaths(GitFileAction::Stage));
        break;
    case ActUnstage:
        unstagePaths(actionPaths(GitFileAction::Unstage));
        break;
    case ActResolve:
        // Marking a conflict resolved is exactly `git add`.
        stagePaths(actionPaths(GitFileAction::Resolve));
        break;
    case ActDiscard:
        discardPaths(actionPaths(GitFileAction::Discard), true);
        break;
    case ActStageAll:
        stagePaths(pathsInGroup(item));
        break;
    case ActUnstageAll:
        unstagePaths(pathsInGroup(item));
        break;
    case ActDiscardAll:
        discardPaths(pathsInGroup(item), true);
        break;
    default:
        break;
    }
}

QVector<GitFileEntry> SourceControlPanel::selectedFileEntries() const
{
    QVector<GitFileEntry> entries;
    for (QTreeWidgetItem* item : m_fileList->selectedItems()) {
        if (item->data(0, GitItemRoles::Kind).toInt() != FileRow)
            continue;
        GitFileEntry entry;
        entry.path = item->data(0, GitItemRoles::Path).toString();
        entry.staged = item->data(0, GitItemRoles::Staged).toBool();
        entry.state = static_cast<GitFileState>(item->data(0, GitItemRoles::State).toInt());
        entries.append(entry);
    }
    return entries;
}

QStringList SourceControlPanel::rowActionPaths(GitFileAction action,
                                                QTreeWidgetItem* clicked) const
{
    GitFileEntry entry;
    entry.path = clicked->data(0, GitItemRoles::Path).toString();
    entry.staged = clicked->data(0, GitItemRoles::Staged).toBool();
    entry.state = static_cast<GitFileState>(clicked->data(0, GitItemRoles::State).toInt());
    return GitRepository::resolveFileActionPaths(action, entry, clicked->isSelected(),
                                                  selectedFileEntries());
}

// ---------------------------------------------------------------------------
// No-repository empty state

void SourceControlPanel::initializeRepository()
{
    const QString folder = m_lastCheckedFolder;
    if (folder.isEmpty())
        return;

    runLocalAt(folder, { QStringLiteral("init") }, {}, [this, folder] {
        setRepositoryFromWorkspace(folder);
    });
}

// ---------------------------------------------------------------------------
// Local write commands

void SourceControlPanel::runLocal(const QStringList& args, const QByteArray& stdinData,
                                  std::function<void()> onSuccess)
{
    if (m_root.isEmpty())
        return;
    runLocalAt(m_root, args, stdinData, std::move(onSuccess));
}

void SourceControlPanel::runLocalAt(const QString& root, const QStringList& args,
                                    const QByteArray& stdinData,
                                    std::function<void()> onSuccess)
{
    if (root.isEmpty() || m_localRunning || m_busy)
        return;
    m_localRunning = true;
    m_localRoot = root;
    m_localInitializing = m_root.isEmpty() && root == m_lastCheckedFolder;
    m_localSuccess = std::move(onSuccess);
    updateActionStates();
    m_localWatcher->setFuture(QtConcurrent::run([root, args, stdinData] {
        return GitRunner::run(root, args, stdinData);
    }));
}

void SourceControlPanel::stagePaths(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    QStringList args { QStringLiteral("add"), QStringLiteral("--") };
    args += paths;
    runLocal(args, {}, [this, count = paths.size()] {
        setStatusMessage(tr("Staged %n file(s).", nullptr, count));
        scheduleRefresh();
    });
}

void SourceControlPanel::unstagePaths(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    QStringList args { QStringLiteral("restore"), QStringLiteral("--staged"),
                       QStringLiteral("--") };
    args += paths;
    runLocal(args, {}, [this, count = paths.size()] {
        setStatusMessage(tr("Unstaged %n file(s).", nullptr, count));
        scheduleRefresh();
    });
}

void SourceControlPanel::discardPaths(const QStringList& paths, bool confirm)
{
    if (paths.isEmpty() || m_localRunning || m_busy)
        return;

    // Which paths git cannot restore is decided from the snapshot up front:
    // QFile::moveToTrash() below runs a shell operation that can pump the
    // event loop, and m_snapshot would then be replaced mid-iteration.
    QSet<QString> untrackedPaths;
    for (const GitFileEntry& entry : m_snapshot.files) {
        if (entry.state == GitFileState::Untracked)
            untrackedPaths.insert(entry.path);
    }

    // A confirmation box is a nested event loop; hold the refresh so the rows
    // and the snapshot this action came from survive it.
    RefreshHold hold(this);

    if (confirm) {
        const QString text = paths.size() == 1
            ? tr("Discard changes in \"%1\"?").arg(fileNameOf(paths.first()))
            : tr("Discard changes in %n file(s)?", nullptr, paths.size());
        if (themedWarning(this, tr("Discard Changes"), text,
                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
    }

    // Untracked files are not git's to restore. Send them to the Recycle Bin
    // so a mistaken discard is recoverable -- never `git clean`.
    QStringList tracked;
    int recycled = 0;
    for (const QString& path : paths) {
        if (untrackedPaths.contains(path)) {
            if (WorkspaceFileOps::deletePath(m_root + QLatin1Char('/') + path))
                ++recycled;
        } else {
            tracked << path;
        }
    }

    if (!tracked.isEmpty()) {
        QStringList args { QStringLiteral("restore"), QStringLiteral("--") };
        args += tracked;
        runLocal(args, {}, [this, trackedCount = tracked.size(), recycled] {
            setStatusMessage(recycled > 0
                ? tr("Discarded %1 change(s); %2 untracked file(s) moved to the Recycle Bin.")
                      .arg(trackedCount).arg(recycled)
                : tr("Discarded %n change(s).", nullptr, trackedCount));
            emit worktreeChanged();
            scheduleRefresh();
        });
        return;
    }
    setStatusMessage(recycled > 0
        ? tr("Discarded %1 change(s); %2 untracked file(s) moved to the Recycle Bin.")
              .arg(0).arg(recycled)
        : tr("Discarded %n change(s).", nullptr, 0));
    emit worktreeChanged();
    scheduleRefresh();
}

void SourceControlPanel::commitChanges(bool thenPush)
{
    if (m_root.isEmpty() || m_busy || m_localRunning)
        return;

    const QString message = m_commitMessage->toPlainText().trimmed();
    if (message.isEmpty()) {
        setStatusMessage(tr("Enter a commit message first."), true);
        m_commitMessage->setFocus();
        return;
    }

    const GitChangeState changeState = GitRepository::changeState(m_snapshot.files);
    const bool hasStaged = changeState == GitChangeState::HasStaged;
    const bool hasChanges = changeState != GitChangeState::Clean;
    if (!hasStaged && hasChanges && !m_snapshot.merging) {
        runLocal({ QStringLiteral("add"), QStringLiteral("-A") }, {},
                 [this, message, thenPush] { runCommit(message, thenPush); });
        return;
    }

    if (!hasStaged && !hasChanges && !m_snapshot.merging) {
        setStatusMessage(tr("There are no changes to commit."), true);
        return;
    }

    runCommit(message, thenPush);
}

void SourceControlPanel::runCommit(const QString& message, bool thenPush)
{
    runLocal({ QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-") },
             message.toUtf8(), [this, thenPush] {
        m_commitMessage->clear();
        setStatusMessage(tr("Committed."));
        scheduleRefresh();
        if (thenPush)
            pushToUpstream();
    });
}

// ---------------------------------------------------------------------------
// Action states, status, theme, persistence

void SourceControlPanel::updateActionStates()
{
    const bool haveRepo = !m_root.isEmpty() && m_snapshot.valid;
    const bool operationBusy = m_busy || m_localRunning;
    if (m_fileList)
        m_fileList->setEnabled(!m_localRunning);
    if (m_branchButton)
        m_branchButton->setEnabled(!operationBusy);
    if (m_btnInitRepo)
        m_btnInitRepo->setEnabled(!m_localRunning);

    // A network operation in flight owns the button outright: label and icon
    // come from setBusy()/the spin timer, not from the usual Commit/Sync/
    // Publish contextual logic below. It is deliberately left setEnabled(true)
    // -- disabling it would hand the icon to Qt's automatic disabled-icon
    // generation (QStyle::generatedIconPixmap), which washes the rotated
    // sync glyph out to a faint blur against the QSS :disabled grey. The
    // click handler ignores clicks itself while m_busy is set instead.
    // The button paints as its normal :disabled grey while busy -- via a
    // "gitBusy" QSS property, not QWidget::setEnabled(false) -- so the click
    // guard in the button's slot is what actually blocks a second click.
    // setEnabled(false) would additionally hand the icon to Qt's automatic
    // disabled-icon generation (QStyle::generatedIconPixmap), which washes a
    // supplied pixmap out to a faint blur instead of using the dimmed color
    // rotatedSyncPixmap() already paints it in.
    if (m_btnCommit->property("gitBusy").toBool() != m_busy) {
        m_btnCommit->setProperty("gitBusy", m_busy);
        m_btnCommit->style()->unpolish(m_btnCommit);
        m_btnCommit->style()->polish(m_btnCommit);
    }

    if (m_busy) {
        m_syncLabel->hide();
        m_btnCommit->setText(networkOperationLabel(m_networkOpKind));
        m_btnCommit->setToolTip(QString());
        m_btnCommit->setEnabled(true);
        m_btnCommitMenu->setVisible(false);
        m_commitMessage->setEnabled(haveRepo);
        return;
    }

    const GitChangeState changeState = GitRepository::changeState(m_snapshot.files);
    const bool hasStaged = changeState == GitChangeState::HasStaged;
    const bool hasChanges = changeState != GitChangeState::Clean;
    // Keep the primary action available whenever there is work to commit.
    // commitChanges() retains the existing click-time message validation,
    // which can focus the editor and explain what is missing.
    const bool canCommit = haveRepo && !operationBusy
                           && (hasStaged || hasChanges || m_snapshot.merging);

    const GitBranchInfo& branch = m_snapshot.branch;
    const bool network = haveRepo && !operationBusy && !m_snapshot.merging && !branch.detached;
    const bool workingTreeClean = haveRepo && !hasChanges && !m_snapshot.merging;

    // With a clean working tree the primary action becomes VS Code's Sync
    // Changes / Publish Branch instead of a no-op Commit button. An
    // up-to-date branch (ahead == behind == 0) falls through to the disabled
    // "No changes to commit" state below rather than showing "Sync Changes 0".
    const bool showSync = workingTreeClean && network && branch.hasUpstream
                          && (branch.ahead > 0 || branch.behind > 0);
    const bool showPublish = workingTreeClean && network && !branch.hasUpstream
                          && !branch.branch.isEmpty();

    if (showSync) {
        m_primaryAction = PrimaryGitAction::Sync;
        const QString label = tr("Sync Changes");
        // The sync glyph is part of the same label as the title and counters.
        // Keeping it out of QPushButton's separate icon slot prevents the two
        // independently-centred groups from overlapping.
        m_btnCommit->setIcon(QIcon());
        m_btnCommit->setText(QString());
        static_cast<SyncContentLabel*>(m_syncLabel)->setContent(label, branch.behind, branch.ahead);
        m_syncLabel->show();
        m_btnCommit->setToolTip(tr("Pull, then push, to sync with the remote"));
        m_btnCommit->setEnabled(true);
    } else if (showPublish) {
        m_primaryAction = PrimaryGitAction::Publish;
        m_syncLabel->hide();
        m_btnCommit->setIcon(IconHelper::materialIcon(QChar(kIconSync), Qt::white, kSyncIconSize));
        m_btnCommit->setText(tr("Publish Branch"));
        m_btnCommit->setToolTip(tr("Publish \"%1\" to a remote").arg(branch.branch));
        m_btnCommit->setEnabled(true);
    } else {
        m_primaryAction = PrimaryGitAction::Commit;
        m_syncLabel->hide();
        m_btnCommit->setIcon(QIcon());
        m_btnCommit->setText(hasStaged || m_snapshot.merging
            ? tr("Commit") : tr("Stage All + Commit"));
        m_btnCommit->setToolTip(hasStaged || m_snapshot.merging
            ? tr("Commit the staged changes")
            : hasChanges ? tr("Stage all changes, then commit") : tr("No changes to commit"));
        m_btnCommit->setEnabled(canCommit);
    }

    const bool commitMode = m_primaryAction == PrimaryGitAction::Commit;
    m_btnCommitMenu->setVisible(commitMode);
    m_btnCommitMenu->setEnabled(commitMode && canCommit);
    m_commitMessage->setEnabled(haveRepo && !m_localRunning);
}

void SourceControlPanel::setStatusMessage(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setProperty("gitError", isError);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    // A new message starts from the natural height again: a dragged height
    // only holds for as long as the message it was sized for.
    m_statusUserHeight = 0;
    m_statusBar->setVisible(!message.isEmpty());
    updateStatusGeometry();
}

// The label wraps, so its height is only known for a concrete width; keep the
// area exactly as tall as the message needs, capped, and let it scroll beyond
// -- unless the user dragged the top edge, which wins until the next message.
void SourceControlPanel::updateStatusGeometry()
{
    if (!m_statusScroll || !m_statusLabel)
        return;
    const int width = qMax(0, m_statusScroll->viewport()->width());
    const int height = m_statusLabel->heightForWidth(width);
    m_statusLabel->resize(width, height);
    m_statusScroll->setFixedHeight(m_statusUserHeight > 0
                                       ? m_statusUserHeight
                                       : qBound(kStatusMinHeight, height, kStatusAutoMaxHeight));
    if (m_statusClose)
        m_statusClose->move(m_statusBar->width() - kStatusCloseRightMargin - kStatusCloseSize,
                            kStatusCloseTopMargin);
}

void SourceControlPanel::updateStatusCloseIcon()
{
    if (!m_statusClose)
        return;
    m_statusClose->setIcon(IconHelper::materialIcon(QChar(kIconClose), iconColorFor(m_dark),
                                                    kStatusCloseIconSize));
}

// The branch name may take at most half the panel; past that it elides so the
// GRAPH title keeps its side of the header.
void SourceControlPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_branchButton)
        m_branchButton->setMaxTextWidth(width() / 2);
}

void SourceControlPanel::setDarkMode(bool dark)
{
    m_dark = dark;

    // Re-derives the Sync Changes / Publish Branch icon color; a no-op for
    // the plain Commit state, which has no icon.
    if (m_btnCommit)
        updateActionStates();

    if (m_fileDelegate)
        m_fileDelegate->setDarkMode(dark);
    if (m_branchButton)
        m_branchButton->setDarkMode(dark);
    if (m_selectedItem)
        updateSelectedActions(m_selectedItem);
    if (m_hoverActions && m_hoverActions->isVisible())
        refreshHoverActionsAppearance();
    applyGraphDarkMode(dark);
    for (QTreeWidget* tree : { m_fileList, m_historyList }) {
        if (tree)
            tree->viewport()->update();
    }

    if (m_sidebarSplitter)
        m_sidebarSplitter->setDarkMode(dark);

    updateStatusCloseIcon();
}

void SourceControlPanel::restoreState()
{
    QSettings settings;
    const QByteArray sidebar = settings.value(QStringLiteral("git/sidebarSplitter")).toByteArray();
    if (!sidebar.isEmpty()) {
        m_sidebarSplitter->restoreState(sidebar);
        m_sidebarSplitter->reassertHandleWidth();
    }

    const int sortMode = settings.value(QStringLiteral("git/changesSortMode"),
                                         static_cast<int>(ChangesSortMode::Path)).toInt();
    m_sortMode = GitRepository::changesSortModeFromStoredValue(sortMode);
}

void SourceControlPanel::persistState()
{
    if (!m_sidebarSplitter)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("git/sidebarSplitter"), m_sidebarSplitter->saveState());
}

void SourceControlPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    applyRepositoryState();
}
