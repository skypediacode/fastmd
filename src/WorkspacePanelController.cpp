#include "WorkspacePanelController.h"
#include "ShellWarmUp.h"
#include "ThemePalette.h"
#include "IconHelper.h"
#include "NativeTreeContainer.h"
#include "WorkspaceTreeView.h"
#include "ReadOnlyItemViewCursor.h"
#include "WorkspaceTreeFilterModel.h"
#include "WorkspaceNavigationModel.h"
#include "WorkspaceRenameDelegate.h"
#include "WorkspaceFileOps.h"
#include "RecentFoldersManager.h"
#include "SourceControlPanel.h"
#include "WorkspaceSearchPanel.h"
#include "AlignedMenu.h"
#include "ThemedMessageBox.h"
#include "StartupProfiler.h"
#include "ThemedSplitter.h"
#include "OutlineModel.h"
#include "MarkdownEditor.h"
#include "FastScrollBar.h"
#include "ProportionalSplitterGeometry.h"

#include <QWidget>
#include <QInputMethodEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QHeaderView>
#include <QToolButton>
#include <QToolBar>
#include "ToolbarHoverAnimation.h"
#include <QLineEdit>
#include <QLabel>
#include <QListView>
#include <QStackedWidget>
#include <QStyle>
#include <QSplitter>
#include <QStandardItemModel>
#include <QAbstractFileIconProvider>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QItemSelectionModel>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <QSignalBlocker>
#include <QColor>
#include <QSize>
#include <QSizePolicy>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QKeyEvent>
#include <QEvent>
#include <QCursor>
#include <QMenu>
#include <QPointer>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include "CommandShortcutRegistry.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QSettings>
#include <QStandardPaths>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDataStream>
#include <algorithm>

// Paints the Workspace and Outline cards behind the panel's children. Neither
// card is a single widget: the Workspace card spans the shared header and the
// body stack, while Outline lives inside the body's splitter, so the panel
// draws both from its sections' geometry. The panel layout keeps every child
// ThemeCard::Inset px inside the card edges, and the splitter's gap handle is
// the canvas between the two cards.
class WorkspaceCardBackdrop : public QObject
{
public:
    WorkspaceCardBackdrop(QWidget *panel, std::function<bool()> darkProvider)
        : QObject(panel), m_panel(panel), m_darkProvider(std::move(darkProvider))
    {
        panel->installEventFilter(this);
    }

    void setSections(QWidget *workspaceBottom, QWidget *outline)
    {
        m_workspaceBottom = workspaceBottom;
        m_outline = outline;
        workspaceBottom->installEventFilter(this);
        outline->installEventFilter(this);
        m_panel->update();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_panel) {
            if (event->type() == QEvent::Paint)
                paint();
            return false;
        }
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            m_panel->update();
            break;
        default:
            break;
        }
        return false;
    }

private:
    void paint()
    {
        const bool dark = m_darkProvider && m_darkProvider();
        QPainter painter(m_panel);
        QRect workspace = m_panel->rect();
        if (m_workspaceBottom && m_outline && m_outline->isVisibleTo(m_panel)) {
            const QRect bottom(m_workspaceBottom->mapTo(m_panel, QPoint()), m_workspaceBottom->size());
            const QRect outline(m_outline->mapTo(m_panel, QPoint()), m_outline->size());
            workspace.setBottom(bottom.bottom() + ThemeCard::Inset);
            ThemeCard::paint(painter, outline.adjusted(-ThemeCard::Inset, -ThemeCard::Inset,
                                                       ThemeCard::Inset, ThemeCard::Inset),
                             ThemePalette::Surface, dark);
        }
        ThemeCard::paint(painter, workspace, ThemePalette::Surface, dark);
    }

    QWidget *m_panel;
    QPointer<QWidget> m_workspaceBottom;
    QPointer<QWidget> m_outline;
    std::function<bool()> m_darkProvider;
};

namespace
{
    constexpr int kDefaultNameColumnWidth = 380;

    QString searchListStyle(bool dark)
    {
        return ThemePalette::resolve(QStringLiteral(
            "QListView { background: @surface@; color: @text@; border: none; outline: none; }"
            "QListView::item:hover { background: @hover@; }"
            "QListView::item:selected { background: @selection@; color: #ffffff; }"), dark);
    }
    const QString kShowAllByRootPrefix = QStringLiteral("workspace/showAllByRoot/");

    // QSplitter's version-1 state uses the Qt_5_0 stream format (also in Qt 6).
    // restoreState() only checks the marker/version, so check the complete
    // two-pane vertical state before allowing truncated or zero-size data in.
    bool validOutlineSplitterState(const QByteArray& state)
    {
        QDataStream stream(state);
        stream.setVersion(QDataStream::Qt_5_0);
        qint32 magic = 0, version = 0, first = 0, second = 0;
        quint32 count = 0;
        stream >> magic >> version >> count;
        if (magic != 0xff || version != 1 || count != 2)
            return false;
        bool collapsible = false, opaque = false, opaqueSet = false;
        qint32 handleWidth = 0, orientation = 0;
        stream >> first >> second >> collapsible >> handleWidth >> opaque
               >> orientation >> opaqueSet;
        return stream.status() == QDataStream::Ok && stream.atEnd()
            && first > 0 && second > 0 && orientation == Qt::Vertical;
    }

    QString normalizedWorkspaceRoot(const QString &root, QHash<QString, QString>& cache)
    {
        if (const auto cached = cache.constFind(root); cached != cache.cend())
            return *cached;
        QFileInfo info(root);
        QString normalized = info.canonicalFilePath();
        if (normalized.isEmpty())
            normalized = info.absoluteFilePath();
        normalized = QDir::cleanPath(QDir::fromNativeSeparators(normalized));
        cache.insert(root, normalized);
        return normalized;
    }

    QString showAllSettingKey(const QString &normalizedRoot)
    {
        const QByteArray digest = QCryptographicHash::hash(
            normalizedRoot.toCaseFolded().toUtf8(), QCryptographicHash::Sha256);
        return kShowAllByRootPrefix + QString::fromLatin1(digest.toHex());
    }

    bool savedShowAllForRoot(const QString &root, bool fallback)
    {
        if (root.isEmpty())
            return fallback;

        QSettings settings;
        const QString key = showAllSettingKey(root);
        return settings.contains(key) ? settings.value(key).toBool() : fallback;
    }

    void saveShowAllForRoot(const QString &root, bool showAll)
    {
        QSettings settings;
        if (root.isEmpty())
            settings.setValue(QStringLiteral("workspace/showAll"), showAll);
        else
            settings.setValue(showAllSettingKey(root), showAll);
        settings.sync();
    }

    // Material Icons codepoints used by this panel (Regular font unless noted).
    // parent=arrow_upward(0xE5D8) home=0xE88A current_file_folder=my_location(0xE55C)
    // folder_open=0xE2C8 dropdown=0xE5C5
    // new_file=0xE89C new_folder=0xE2CC show_all=0xE8F4 expand_all=0xE5D7
    // collapse_all=0xE5D6 search=0xE8B6 rename=0xE8D5 delete=0xE872
    // copy=0xE14D cut=0xE14E paste=0xE14F reveal=0xE895
    constexpr ushort kIconCurrentFileFolder = 0xE55C;
    constexpr ushort kIconHomeFolder = 0xE88A;
    constexpr ushort kIconOpenFolder = 0xE2C8;
    constexpr ushort kIconDropdown = 0xE5C5;
    constexpr ushort kIconNewFile = 0xE89C;
    constexpr ushort kIconNewTextFile = 0xE873;
    constexpr ushort kIconNewFolder = 0xE2CC;
    constexpr ushort kIconFilterActive = 0xEF4F; // filter_alt (OFF, showing writing files only)
    constexpr ushort kIconFilterOff = 0xEB32;    // filter_alt_off (ON, showing all files)
    constexpr ushort kIconExpandAll = 0xE5D7;
    constexpr ushort kIconCollapseAll = 0xE5D6;
    constexpr ushort kIconSearch = 0xE8B6;
    constexpr ushort kIconClose  = 0xE5CD; // close / X
    constexpr ushort kIconReveal = 0xE895;
    constexpr ushort kIconSourceControlMode = 0xE0B6; // call_split (branch)
    constexpr ushort kIconWorkspaceMode     = 0xE2C7; // folder
    // Same glyphs WorkspaceTreeView's own branch decoration already paints
    // (chevron_right / expand_more), so the Outline section's disclosure
    // indicator reads as the same visual language as the tree above it.
    constexpr ushort kIconChevronRight = 0xE5CC;
    constexpr ushort kIconChevronDown  = 0xE5CF;
    constexpr int kOutlineHeaderHeight = 36; // matches TerminalPanel::kHeaderHeight

    constexpr int kRevealButtonInset = 8;
    constexpr int kRevealButtonReserve = 36;

    class WorkspaceSplitButton final : public QToolButton
    {
    public:
        WorkspaceSplitButton(const QIcon &icon, QMenu *menu, const QColor &color, QWidget *parent)
            : QToolButton(parent), m_menu(menu), m_color(color)
        {
            setObjectName(QStringLiteral("workspaceSplitButton"));
            setIcon(icon);
            setIconSize(QSize(18, 18));
            setFixedWidth(44);
            setFocusPolicy(Qt::NoFocus);
            if (m_menu)
            {
                connect(m_menu, &QMenu::aboutToShow, this, [this]()
                        { setDown(true); });
                connect(m_menu, &QMenu::aboutToHide, this, [this]()
                        {
                setDown(false);
                clearFocus();
                update(); });
            }
        }

        void setThemeColor(const QColor &color) { m_color = color; update(); }

    protected:
        void mouseReleaseEvent(QMouseEvent *event) override
        {
            if (event->button() == Qt::LeftButton && event->position().x() >= width() - kArrowWidth)
            {
                setDown(true);
                if (m_menu)
                    m_menu->popup(mapToGlobal(QPoint(0, height())));
                event->accept();
                return;
            }
            QToolButton::mouseReleaseEvent(event);
            setDown(false);
            clearFocus();
            update();
        }

        void leaveEvent(QEvent *event) override
        {
            if (m_menu && m_menu->isVisible())
            {
                setDown(true);
            }
            else
            {
                setDown(false);
            }
            QToolButton::leaveEvent(event);
        }

        void paintEvent(QPaintEvent *event) override
        {
            QToolButton::paintEvent(event);
            QPainter painter(this);
            painter.setPen(m_color);
            QFont font(IconHelper::materialFontFamily());
            font.setPixelSize(16);
            painter.setFont(font);
            painter.drawText(QRect(width() - kArrowWidth, 0, kArrowWidth, height()),
                             Qt::AlignCenter, QString(QChar(kIconDropdown)));
        }

    private:
        static constexpr int kArrowWidth = 17;
        QPointer<QMenu> m_menu;
        QColor m_color;
    };

    class WorkspaceMenuButton final : public QToolButton
    {
    public:
        WorkspaceMenuButton(const QIcon& icon, QMenu* menu, QWidget* parent)
            : QToolButton(parent)
        {
            setObjectName(QStringLiteral("workspaceSortButton"));
            setIcon(icon);
            setIconSize(QSize(24, 24));
            setFixedWidth(32);
            setFocusPolicy(Qt::NoFocus);
            setToolButtonStyle(Qt::ToolButtonIconOnly);
            setPopupMode(QToolButton::InstantPopup);
            setMenu(menu);
            if (menu) {
                connect(menu, &QMenu::aboutToShow, this, [this]() { setDown(true); });
                connect(menu, &QMenu::aboutToHide, this, [this]() {
                    setDown(false);
                    clearFocus();
                    update();
                });
            }
        }

    };

    // How often the startup tree build checks the shell warm-up, and how long it
    // waits at most. Past the limit it proceeds and the shell calls block exactly
    // where they always did.
    constexpr int kShellWarmUpPollMs = 5;
    constexpr int kShellWarmUpWaitMs = 200;

    // Same icons as QFileSystemModel's own provider, without its file type
    // string: building that loads Qt's MIME database on the GUI thread, and
    // WorkspaceTreeFilterModel supplies the File Type column itself. Never empty:
    // the model creates a node with an empty type and takes the gathered icon and
    // visibility only if the two differ, so an empty one leaves blank icons and
    // folders without children.
    class WorkspaceIconProvider final : public QAbstractFileIconProvider
    {
    public:
        QString type(const QFileInfo&) const override { return QStringLiteral("-"); }
    };
}

WorkspacePanelController::WorkspacePanelController(QWidget *panelParent,
                                                   std::function<QString()> currentFilePathProvider,
                                                   std::function<bool()> darkProvider,
                                                   QObject *parent)
    : QObject(parent), m_currentFilePathProvider(std::move(currentFilePathProvider)), m_darkProvider(std::move(darkProvider))
{
    m_workspacePanel = new QWidget(panelParent);
    m_cardBackdrop = new WorkspaceCardBackdrop(m_workspacePanel, m_darkProvider);
    m_panelLayout = new QVBoxLayout(m_workspacePanel);
    m_panelLayout->setContentsMargins(ThemeCard::Inset, ThemeCard::Inset,
                                      ThemeCard::Inset, ThemeCard::Inset);
    m_panelLayout->setSpacing(0);

    auto* recentMenu = new AlignedMenu(tr("Recent Folders"), m_workspacePanel);
    recentMenu->setObjectName(QStringLiteral("workspaceContextMenu"));
    m_recentFoldersMgr = new RecentFoldersManager(recentMenu, this);
    connect(m_recentFoldersMgr, &RecentFoldersManager::folderOpenRequested, this,
            [this](const QString& path) { setRoot(path); });
}

WorkspacePanelController::~WorkspacePanelController()
{
    if (m_transferCancel)
        m_transferCancel->cancel();
}

void WorkspacePanelController::ensureUi()
{
    if (m_uiBuilt)
        return;
    m_uiBuilt = true;
    auto* panelLayout = m_panelLayout;

    // ── Header: current workspace folder path ──
    m_headerBar = new QWidget(m_workspacePanel);
    m_headerBar->setObjectName(QStringLiteral("workspaceRootHeaderBar"));
    m_headerBar->setFixedHeight(34);
    auto* headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(0, 0, 2, 0);
    headerLayout->setSpacing(0);
    m_headerLabel = new QLabel(m_workspacePanel);
    m_headerLabel->setObjectName(QStringLiteral("workspaceRootHeader"));
    m_headerLabel->setStyleSheet(
        QStringLiteral("QLabel { padding: 4px 8px; font-weight: 600; font-size: 14px; }"));
    m_headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The text is re-elided whenever this label is resized.  It can therefore
    // become the complete root path at a wide splitter size, but that must not
    // feed the path's natural width back through the header layout as a panel
    // minimum width.  The stretch below remains the sole authority for the
    // space available to the label; the fixed header actions keep their room.
    m_headerLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_headerLabel->setMinimumWidth(0);
    m_headerLabel->installEventFilter(this);
    headerLayout->addWidget(m_headerLabel, 1);

    bool firstHeaderAction = true;
    auto makeHeaderButton = [this, headerLayout, &firstHeaderAction](const QString& objectName,
                                                                       const QString& tooltip) {
        auto* button = new QToolButton(m_headerBar);
        button->setObjectName(objectName);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(24, 24);
        button->setIconSize(QSize(18, 18));
        button->setToolTip(tooltip);
        headerLayout->addSpacing(4);
        headerLayout->addWidget(button, 0, Qt::AlignVCenter);
        firstHeaderAction = false;
        return button;
    };
    // Left of the three existing actions, and present in both modes: the top
    // row is deliberately identical whichever body is showing.
    m_modeToggle = makeHeaderButton(
        QStringLiteral("workspaceModeToggle"), tr("Source Control"));
    m_modeToggle->setCheckable(true);
    m_modeToggle->setCursor(Qt::PointingHandCursor);
    connect(m_modeToggle, &QToolButton::clicked, this, [this]() {
        setPanelMode(m_panelMode == PanelMode::SourceControl ? PanelMode::Workspace
                                                             : PanelMode::SourceControl);
    });
    m_searchModeButton = makeHeaderButton(
        QStringLiteral("workspaceSearchModeButton"), tr("Search in Files (Ctrl+Shift+F)"));
    m_searchModeButton->setCheckable(true);
    m_searchModeButton->setCursor(Qt::PointingHandCursor);
    connect(m_searchModeButton, &QToolButton::clicked, this, [this]() {
        if (m_panelMode == PanelMode::Search)
            setPanelMode(PanelMode::Workspace);
        else
            openSearch(false);
    });
    // The same 1x16 divider FindOverlay draws between its groups, so it takes
    // the same QSS rule in both themes.
    headerLayout->addSpacing(6);
    auto* headerSeparator = new QWidget(m_headerBar);
    headerSeparator->setObjectName(QStringLiteral("findOverlaySeparator"));
    headerSeparator->setAttribute(Qt::WA_StyledBackground, true);
    headerSeparator->setFixedSize(1, 16);
    headerLayout->addWidget(headerSeparator, 0, Qt::AlignVCenter);
    m_copyFolderPathButton = makeHeaderButton(
        QStringLiteral("workspaceCopyFolderPathButton"), tr("Copy Folder Path"));
    m_revealFolderButton = makeHeaderButton(
        QStringLiteral("workspaceRevealFolderButton"), tr("Reveal Workspace in Explorer"));
    m_terminalButton = makeHeaderButton(
        QStringLiteral("workspaceTerminalButton"), tr("Open Workspace in Terminal"));
    connect(m_copyFolderPathButton, &QToolButton::clicked, this, [this]() {
        if (!m_workspaceRootPath.isEmpty())
            QApplication::clipboard()->setText(QDir::toNativeSeparators(
                QFileInfo(m_workspaceRootPath).absoluteFilePath()));
    });
    connect(m_revealFolderButton, &QToolButton::clicked, this, [this]() {
        WorkspaceFileOps::revealWorkspaceInExplorer(m_workspaceRootPath);
    });
    connect(m_terminalButton, &QToolButton::clicked, this, [this]() {
        if (!m_workspaceRootPath.isEmpty())
            emit terminalRequested(m_workspaceRootPath);
    });
    panelLayout->addWidget(m_headerBar);

    // Everything below the shared header lives in a mode stack: index 0 is the
    // Workspace body (toolbar + search + tree/results), index 1 is added the
    // first time Source Control is selected.
    m_bodyStack = new QStackedWidget(m_workspacePanel);
    m_workspaceBody = new QWidget(m_bodyStack);
    auto* bodyLayout = new QVBoxLayout(m_workspaceBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_bodyStack->addWidget(m_workspaceBody);
    panelLayout->addWidget(m_bodyStack, 1);

    buildToolbar();
    bodyLayout->addWidget(m_toolbar);

    // The search box is inset within the Workspace card rather than spanning
    // it edge to edge, so it reads as an input on the card's surface.
    buildSearchRow();
    auto* searchRowLayout = new QHBoxLayout();
    searchRowLayout->setContentsMargins(6, 2, 6, 4);
    searchRowLayout->addWidget(m_searchRow);
    bodyLayout->addLayout(searchRowLayout);

    m_workspaceTree = new WorkspaceTreeView();
    m_workspaceTree->setDarkTheme(m_darkProvider && m_darkProvider());
    m_workspaceTree->setHeaderHidden(false); // will be hidden by container
    m_workspaceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_workspaceTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_workspaceTree->setUniformRowHeights(true);
    m_workspaceTree->setAnimated(false);
    m_workspaceTree->setTextElideMode(Qt::ElideMiddle);
    m_workspaceTree->setIndentation(18);
    m_workspaceTree->setRootIsDecorated(true);
    m_workspaceTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_workspaceTree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_workspaceTree->setMouseTracking(true);
    setReadOnlyItemViewCursor(m_workspaceTree);
    m_workspaceTree->header()->setSectionsMovable(false);
    m_workspaceTree->header()->setSectionsClickable(true);
    m_workspaceTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_workspaceTree->header()->setStretchLastSection(false);
    // Muted neutral gray (Notepad++/Explorer-style), not the darker/lighter
    // near-black/near-white used for toolbar icons, since chevrons should
    // read as a quiet structural cue rather than a prominent icon.
    m_workspaceTree->setBranchIconColorProvider([this]() -> QColor
                                                { return (m_darkProvider && m_darkProvider()) ? QColor(0x87, 0x87, 0x87) : QColor(0x9a, 0x9a, 0x9a); });

    auto *container = new NativeTreeContainer(m_workspaceTree, m_workspacePanel);
    m_treeContainer = container;

    // Only color-independent rules belong in this per-widget stylesheet: the
    // container draws no frame, and the last header section drops its right
    // divider. The header section's themed bottom border comes from the app-wide
    // QSS (Stylesheet.h), which is theme-aware because it's swapped on every
    // theme change.
    container->setStyleSheet(
        "QFrame#NativeTreeContainer { border: none; background: transparent; }"
        "QHeaderView::section:last { border-right: none; }");

    // Flat search-results overlay: shares the same container cell as the
    // tree, shown instead of it while the search box is non-empty. Its own
    // expand/collapse state (none) is deliberately independent of the
    // tree's, per spec ("expand/collapse state must not affect search").
    m_searchModel = new QStandardItemModel(this);
    m_searchListView = new QListView(m_workspacePanel);
    m_searchListView->setModel(m_searchModel);
    m_searchListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_searchListView->setUniformItemSizes(true);
    setReadOnlyItemViewCursor(m_searchListView);
    // Keep the flat results aligned with the Workspace tree.  The tree's
    // default icon size comes from the active Qt style, so this also follows
    // its DPI scaling behavior.
    m_searchListView->setIconSize(m_workspaceTree->iconSize());
    m_searchListView->setVisible(false);
    m_searchListView->setObjectName(QStringLiteral("workspaceSearchList"));
    // Stylesheet applied here and refreshed in updateToggleIcon() so the list
    // respects the active theme. QAbstractItemView always paints its own
    // QPalette::Base (white) regardless of the parent widget background.
    m_searchListView->setStyleSheet(searchListStyle(m_darkProvider && m_darkProvider()));
    connect(m_searchListView, &QListView::activated,
            this, &WorkspacePanelController::activateSearchResult);

    m_contentStack = new QStackedWidget(m_workspacePanel);
    m_contentStack->addWidget(container);
    m_contentStack->addWidget(m_searchListView);
    m_searchEmptyLabel = new QLabel(tr("No matching files"), m_workspacePanel);
    m_searchEmptyLabel->setAlignment(Qt::AlignCenter);
    m_searchEmptyLabel->setObjectName(QStringLiteral("workspaceSearchEmptyState"));
    m_searchEmptyLabel->setStyleSheet(QStringLiteral(
        "QLabel#workspaceSearchEmptyState { color: #888888; font-style: italic; }"));
    m_contentStack->addWidget(m_searchEmptyLabel);

    // The Outline section shares the body with the tree via a vertical
    // splitter rather than a fixed split: the tree is the primary area and
    // keeps the stretch, while Outline is a collapsible secondary pane that
    // only claims space once the user expands it. The two are separate cards
    // (painted by WorkspaceCardBackdrop), so the handle is a canvas gap that
    // also spans both cards' edges.
    m_workspaceSplitter = new ThemedSplitter(Qt::Vertical, ThemedSplitter::HandleStyle::Gap,
                                             ThemeCard::CardSplitterHandle, m_workspaceBody);
    m_workspaceSplitter->setObjectName(QStringLiteral("workspaceOutlineSplitter"));
    m_workspaceSplitter->setChildrenCollapsible(false);
    m_workspaceSplitter->addWidget(m_contentStack);
    m_workspaceSplitter->addWidget(buildOutlineSection());
    // Both sides must participate in vertical growth. QSplitter uses the
    // current sizes as the effective stretch weights, so this preserves the
    // ratio established by the opening layout or by a user drag instead of
    // pinning Outline to an absolute pixel height (see the matching fix for
    // the editor/terminal splitter in MainWindowTabs.cpp).
    m_workspaceSplitter->setStretchFactor(0, 1);
    m_workspaceSplitter->setStretchFactor(1, 1);
    connect(m_workspaceSplitter, &QSplitter::splitterMoved, this,
            [this](int, int) {
        if (!m_outlineRestorePending) {
            captureOutlineReferenceRatio();
            captureOutlineSplitterState();
        }
    });
    m_workspaceSplitter->installEventFilter(this);
    m_workspaceSplitter->setDarkMode(m_darkProvider && m_darkProvider());
    bodyLayout->addWidget(m_workspaceSplitter, 1);
    m_cardBackdrop->setSections(m_contentStack, m_outlineSection);

    {
        QSignalBlocker blocker(m_outlineHeaderButton);
        m_outlineHeaderButton->setChecked(m_outlineExpanded);
    }
    applyOutlineLayout();
    updateOutlineHeaderIcon();
    if (m_outlineExpanded)
        refreshOutlineFromEditor();
    scheduleOutlineReferenceLayout();

    // Key events land on the tree widget itself (not its viewport), so this
    // is a separate installEventFilter() from the hover-tracking one below.
    m_workspaceTree->installEventFilter(this);
    m_workspaceTree->viewport()->installEventFilter(this);
    m_hoverRevealButton = new QToolButton(m_workspaceTree->viewport());
    m_hoverRevealButton->setObjectName(QStringLiteral("workspaceRevealButton"));
    m_hoverRevealButton->setAutoRaise(true);
    m_hoverRevealButton->setCursor(Qt::PointingHandCursor);
    m_hoverRevealButton->setFixedSize(20, 20);
    m_hoverRevealIconColor = iconColor();
    m_hoverRevealButton->setIcon(IconHelper::materialIcon(QChar(kIconReveal), m_hoverRevealIconColor, 14));
    m_hoverRevealButton->hide();
    m_hoverRevealButton->installEventFilter(this);
    connect(m_hoverRevealButton, &QToolButton::clicked, this, [this]()
            {
        if (!m_hoverPath.isEmpty())
            openInExplorer(m_hoverPath); });

    // The QFileSystemModel is NOT created here: constructing it spins up a
    // background file-info-gatherer thread and initializes the Windows shell
    // icon provider (~100 ms), yet the workspace tree is hidden by default.
    // It is created lazily by ensureModel() the first time the tree is
    // actually shown. The tree's signal handlers below all null-guard the
    // model, so they are safe before it exists.
    connect(m_workspaceTree, &QTreeView::activated, this, [this](const QModelIndex &proxyIndex)
            {
        if (!proxyIndex.isValid() || !m_workspaceModel || !m_navigationModel)
            return;
        if (m_navigationModel->isParentEntry(proxyIndex)) {
            navigateToParent();
            return;
        }
        const QModelIndex filterIndex = m_navigationModel->sourceIndexForEntry(proxyIndex);
        const QModelIndex srcIndex = filterIndex.isValid() ? m_filterProxy->mapToSource(filterIndex) : QModelIndex();
        if (srcIndex.isValid()) {
            const QString path = m_workspaceModel->filePath(srcIndex);
            if (m_workspaceModel->isDir(srcIndex))
                return;
            emit fileActivated(path);
            return;
        } });
    connect(m_workspaceTree, &QTreeView::expanded, this, [this](const QModelIndex &proxyIndex)
            {
        if (!proxyIndex.isValid() || !m_workspaceModel || !m_navigationModel ||
            m_navigationModel->isParentEntry(proxyIndex))
            return;
        rememberExpansion(m_workspaceModel->filePath(sourceIndexForView(proxyIndex)), true); });
    connect(m_workspaceTree, &QTreeView::collapsed, this, [this](const QModelIndex &proxyIndex)
            {
        if (!proxyIndex.isValid() || !m_workspaceModel || !m_navigationModel ||
            m_navigationModel->isParentEntry(proxyIndex))
            return;
        rememberExpansion(m_workspaceModel->filePath(sourceIndexForView(proxyIndex)), false); });
    connect(m_workspaceTree, &WorkspaceTreeView::contextMenuRequested, this,
            [this](const QPoint &globalPos, const QModelIndex &proxyIndex)
            {
                showContextMenu(globalPos, proxyIndex);
            });
    connect(m_workspaceTree, &WorkspaceTreeView::externalPathsDropped, this,
            [this](const QStringList &paths, const QModelIndex &proxyIndex)
            {
                QString destDir = m_workspaceRootPath;
                if (proxyIndex.isValid() && m_workspaceModel && m_navigationModel && !m_navigationModel->isParentEntry(proxyIndex))
                {
                    const QModelIndex srcIndex = sourceIndexForView(proxyIndex);
                    const QString hitPath = m_workspaceModel->filePath(srcIndex);
                    destDir = m_workspaceModel->isDir(srcIndex) ? hitPath : QFileInfo(hitPath).absolutePath();
                }
                importExternalPaths(paths, destDir);
            });
    connect(m_workspaceTree, &WorkspaceTreeView::internalDropRequested, this,
            [this](const QModelIndex &proxyIndex)
            {
        QString destDir = m_workspaceRootPath;
        if (proxyIndex.isValid() && m_workspaceModel && m_navigationModel &&
            !m_navigationModel->isParentEntry(proxyIndex))
        {
            const QModelIndex srcIndex = sourceIndexForView(proxyIndex);
            const QString hitPath = m_workspaceModel->filePath(srcIndex);
            destDir = m_workspaceModel->isDir(srcIndex)
                ? hitPath : QFileInfo(hitPath).absolutePath();
        }
        if (destDir.isEmpty())
            return;

        QString lastMoved;
        QStringList failed;
        for (const QString &sourcePath : selectedPaths())
        {
            if (QFileInfo(sourcePath).absolutePath().compare(destDir, Qt::CaseInsensitive) == 0)
                continue;
            const QString moved = WorkspaceFileOps::moveToDirectory(sourcePath, destDir);
            if (moved.isEmpty())
                failed << QFileInfo(sourcePath).fileName();
            else
                lastMoved = moved;
        }
        if (!lastMoved.isEmpty())
        {
            ensureModel();
            updateRoot();
            revealPath(lastMoved);
        }
        reportFailedTransfers(tr("Move"), failed);
            });
    connect(m_workspaceTree, &WorkspaceTreeView::copyRequested, this,
            [this]() { copySelected(false); });
    connect(m_workspaceTree, &WorkspaceTreeView::pasteRequested, this,
            [this]() { pasteSelection(); });

    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]()
            {
        if (!m_settingSystemClipboard) {
            m_clipboardPaths.clear();
            m_clipboardIsCut = false;
        } });

    // ── Actions with shortcuts, shared by the context menu and the tree itself ──
    auto *actRename = new QAction(tr("Rename"), m_workspaceTree);
    actRename->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::WorkspaceRename));
    actRename->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actRename, &QAction::triggered, this, &WorkspacePanelController::beginRenameSelected);
    m_workspaceTree->addAction(actRename);

    auto *actDelete = new QAction(tr("Delete"), m_workspaceTree);
    actDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    actDelete->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actDelete, &QAction::triggered, this, &WorkspacePanelController::deleteSelected);
    m_workspaceTree->addAction(actDelete);

    auto *actCopy = new QAction(tr("Copy"), m_workspaceTree);
    actCopy->setShortcut(QKeySequence::Copy);
    actCopy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actCopy, &QAction::triggered, this, [this]()
            { copySelected(false); });
    m_workspaceTree->addAction(actCopy);

    auto *actCut = new QAction(tr("Cut"), m_workspaceTree);
    actCut->setShortcut(QKeySequence::Cut);
    actCut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actCut, &QAction::triggered, this, [this]()
            { copySelected(true); });
    m_workspaceTree->addAction(actCut);

    auto *actPaste = new QAction(tr("Paste"), m_workspaceTree);
    actPaste->setShortcut(QKeySequence::Paste);
    actPaste->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actPaste, &QAction::triggered, this, [this]()
            { pasteSelection(); });
    m_workspaceTree->addAction(actPaste);

    auto *actSelectAll = new QAction(tr("Select All"), m_workspaceTree);
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    actSelectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actSelectAll, &QAction::triggered, this, [this]()
            { m_workspaceTree->selectAll(); });
    m_workspaceTree->addAction(actSelectAll);

    auto *actReveal = new QAction(tr("Reveal in Explorer"), m_workspaceTree);
    actReveal->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::FileRevealInExplorer));
    actReveal->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(actReveal, &QAction::triggered, this, [this]() {
        if (!m_workspaceModel || !m_navigationModel)
            return;
        const QModelIndex index = m_workspaceTree->currentIndex();
        if (!index.isValid() || m_navigationModel->isParentEntry(index))
            return;
        const QModelIndex sourceIndex = sourceIndexForView(index);
        if (sourceIndex.isValid())
            openInExplorer(m_workspaceModel->filePath(sourceIndex));
    });
    m_workspaceTree->addAction(actReveal);

    m_columnPersistTimer = new QTimer(this);
    m_columnPersistTimer->setSingleShot(true);
    m_columnPersistTimer->setInterval(250);
    connect(m_columnPersistTimer, &QTimer::timeout, this,
            [this]() { emit columnLayoutChanged(); });

    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now)
            {
        const bool treeFocus = now && m_workspaceTree &&
                               (now == m_workspaceTree || m_workspaceTree->isAncestorOf(now));
        if (treeFocus)
            m_workspaceInteractionActive = true;
        else if (now)
            // Any other real focus target ends the interaction, including one
            // in a different top-level FastMD window (a detached tab): only
            // `now == nullptr` (the whole application lost focus, e.g.
            // Alt+Tab away) preserves the flag, so this controller's global
            // Copy/Paste/Delete interception below never latches onto a
            // background window's stale state and steals a shortcut meant for
            // whichever window is actually focused.
            m_workspaceInteractionActive = false;

        // The selection foreground follows tree focus, so a visible row action
        // icon has to be re-tinted when focus leaves for the editor. The call
        // is a no-op unless the resolved color actually changed.
        if (m_hoverRevealButton && m_hoverRevealButton->isVisible())
            updateHoverRevealIcon();
            });
}

QColor WorkspacePanelController::iconColor() const
{
    const bool dark = m_darkProvider ? m_darkProvider() : false;
    return dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
}

void WorkspacePanelController::updateHoverRevealIcon()
{
    if (!m_hoverRevealButton || !m_workspaceTree)
        return;

    // On a selected row the icon sits on the selection fill, so it has to use
    // the same foreground as the row text beside it -- which the QSS switches
    // to a dark color once the tree loses focus. That color is not in the
    // palette (HighlightedText is #ffffff in both color groups), so it comes
    // from the view, which owns the rest of the selection's appearance.
    const QColor color = m_hoverRowSelected ? m_workspaceTree->selectionForeground()
                                            : iconColor();
    if (color == m_hoverRevealIconColor)
        return;

    m_hoverRevealIconColor = color;
    m_hoverRevealButton->setIcon(IconHelper::materialIcon(QChar(kIconReveal), color, 14));
}

void WorkspacePanelController::buildToolbar()
{
    m_toolbar = new QToolBar(m_workspacePanel);
    m_toolbar->setObjectName(QStringLiteral("workspaceToolbar"));
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);

    const QColor color = iconColor();

    // Current File Folder
    m_currentFileFolderButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(
        m_toolbar->addAction(IconHelper::materialIcon(QChar(kIconCurrentFileFolder), color),
                             tr("Go to Current File Folder"), this,
                             &WorkspacePanelController::navigateToCurrentFileFolder)));

    m_homeFolderButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(
        m_toolbar->addAction(IconHelper::materialIcon(QChar(kIconHomeFolder), color),
                             tr("Go to Home Folder"), this,
                             &WorkspacePanelController::navigateToHomeFolder)));

    // New ▼
    m_newMenu = new AlignedMenu(m_workspacePanel);
    m_newMenu->setObjectName(QStringLiteral("workspaceContextMenu"));
    m_newMenu->addAction(IconHelper::materialMenuIcon(QChar(kIconNewFile), color), tr("Markdown File"),
                         this, &WorkspacePanelController::createNewFile);
    m_newMenu->addAction(IconHelper::materialMenuIcon(QChar(kIconNewTextFile), color), tr("Text File"),
                         this, &WorkspacePanelController::createNewTextFile);
    m_newMenu->addAction(IconHelper::materialMenuIcon(QChar(kIconNewFolder), color), tr("Folder"),
                         this, &WorkspacePanelController::createNewFolder);
    m_newSplitButton = new WorkspaceSplitButton(
        IconHelper::materialIcon(QChar(kIconNewFile), color, 18), m_newMenu, color, m_toolbar);
    m_newSplitButton->setToolTip(tr("New Markdown File"));
    connect(m_newSplitButton, &QToolButton::clicked, this, &WorkspacePanelController::createNewFile);
    m_toolbar->addWidget(m_newSplitButton);

    // Open Folder ▼ — the arrow exposes the recent folders directly.
    m_openFolderSplitButton = new WorkspaceSplitButton(
        IconHelper::materialIcon(QChar(kIconOpenFolder), color, 18), m_recentFoldersMgr->menu(), color, m_toolbar);
    m_openFolderSplitButton->setToolTip(tr("Open Folder"));
    connect(m_openFolderSplitButton, &QToolButton::clicked, this, [this]()
            {
        const QString dir = QFileDialog::getExistingDirectory(
            m_workspacePanel, tr("Open Folder"), m_workspaceRootPath,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty())
            setRoot(dir); });
    m_toolbar->addWidget(m_openFolderSplitButton);

    // m_toolbar->addSeparator();

    // Show All (toggle)
    const ushort filterIcon = m_showAll ? kIconFilterOff : kIconFilterActive;
    const QString filterToolTip = m_showAll ? tr("Filter to Documents, Exports & Images") : tr("Show All Files");
    auto *showAllAction = m_toolbar->addAction(
        IconHelper::materialIcon(QChar(filterIcon), color),
        filterToolTip);
    showAllAction->setCheckable(true);
    showAllAction->setChecked(m_showAll);
    showAllAction->setToolTip(filterToolTip);
    connect(showAllAction, &QAction::toggled, this, [this](bool on)
            { setShowAll(on); });
    m_showAllButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(showAllAction));

    auto *sortMenu = new AlignedMenu(m_workspacePanel);
    sortMenu->setObjectName(QStringLiteral("workspaceCheckMenu"));
    QActionGroup *sortFieldGroup = new QActionGroup(sortMenu);
    sortFieldGroup->setExclusive(true);
    for (const auto &[column, text] : std::initializer_list<std::pair<int, QString>> {
             { 0, tr("Name") }, { 2, tr("File Type") }, { 3, tr("Date Modified") }, { 1, tr("Size") } }) {
        QAction *action = sortMenu->addAction(text);
        action->setCheckable(true);
        action->setData(column);
        action->setProperty("workspaceSortField", true);
        sortFieldGroup->addAction(action);
    }
    sortMenu->addSeparator();
    QActionGroup *sortOrderGroup = new QActionGroup(sortMenu);
    sortOrderGroup->setExclusive(true);
    for (const auto &[order, text] : std::initializer_list<std::pair<Qt::SortOrder, QString>> {
             { Qt::AscendingOrder, tr("Ascending") }, { Qt::DescendingOrder, tr("Descending") } }) {
        QAction *action = sortMenu->addAction(text);
        action->setCheckable(true);
        action->setData(static_cast<int>(order));
        action->setProperty("workspaceSortField", false);
        sortOrderGroup->addAction(action);
    }
    connect(sortFieldGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        setSort(action->data().toInt(), m_sortOrder); });
    connect(sortOrderGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        setSort(m_sortColumn, static_cast<Qt::SortOrder>(action->data().toInt())); });
    m_sortButton = new WorkspaceMenuButton(
        QIcon(QStringLiteral(":/icons/%1").arg(
            (m_darkProvider && m_darkProvider()) ? QStringLiteral("workspace-sort-dark.svg")
                                                   : QStringLiteral("workspace-sort.svg"))),
        sortMenu, m_toolbar);
    m_toolbar->addWidget(m_sortButton);
    updateSortUi();

    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    m_expandButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(
        m_toolbar->addAction(IconHelper::materialIcon(QChar(kIconExpandAll), color, 18),
                             tr("Unfold All"))));
    connect(m_expandButton, &QToolButton::clicked, this, [this]()
            {
        if (m_workspaceTree)
            m_workspaceTree->expandAll(); });
    m_collapseButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(
        m_toolbar->addAction(IconHelper::materialIcon(QChar(kIconCollapseAll), color, 18),
                             tr("Fold All"))));
    connect(m_collapseButton, &QToolButton::clicked, this, [this]()
            {
        if (m_workspaceTree)
            m_workspaceTree->collapseAll(); });
    ToolbarHoverAnimation::install(m_toolbar);
}

void WorkspacePanelController::buildSearchRow()
{
    // QLineEdit::addAction()'s built-in leading-icon slot doesn't reliably
    // vertically center a custom-painted QIcon once the widget also has
    // QSS padding applied (the app-wide QLineEdit rule in Stylesheet.h) --
    // so the icon is composed manually here (a plain QLabel in a QHBoxLayout,
    // guaranteed centered) instead, with the actual bordered/background
    // "input box" look moved onto the row container and the inner QLineEdit
    // made borderless/transparent.
    m_searchRow = new QWidget(m_workspacePanel);
    auto *rowLayout = new QHBoxLayout(m_searchRow);
    rowLayout->setContentsMargins(9, 0, 9, 0);
    rowLayout->setSpacing(6);

    // Styled by the application stylesheet (#workspaceSearchRow): an input
    // fill with a thin border, accent while the box has focus.
    m_searchRow->setAttribute(Qt::WA_StyledBackground, true);
    m_searchRow->setObjectName(QStringLiteral("workspaceSearchRow"));
    m_searchRow->setFixedHeight(30);

    m_searchIconLabel = new QLabel(m_searchRow);
    m_searchIconLabel->setFixedSize(16, 16);
    m_searchIconLabel->setPixmap(IconHelper::materialIcon(QChar(kIconSearch), iconColor(), 16).pixmap(16, 16));
    rowLayout->addWidget(m_searchIconLabel, 0, Qt::AlignVCenter);

    m_searchBox = new QLineEdit(m_searchRow);
    m_searchBox->setPlaceholderText(tr("Search files... (Shift+F1)"));
    m_searchBox->setFrame(false);
    m_searchBox->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; padding: 0; }"));
    m_searchBox->installEventFilter(this);
    rowLayout->addWidget(m_searchBox, 1);

    // Manual clear button: Qt's built-in setClearButtonEnabled renders with
    // the platform style's SP_LineEditClearButton, which appears as a chevron
    // on Windows. Use an explicit Material Icons close glyph instead.
    m_searchClearButton = new QToolButton(m_searchRow);
    m_searchClearButton->setObjectName(QStringLiteral("workspaceSearchClearButton"));
    m_searchClearButton->setIcon(IconHelper::materialIcon(QChar(kIconClose), iconColor(), 12));
    m_searchClearButton->setIconSize(QSize(12, 12));
    m_searchClearButton->setFixedSize(18, 18);
    m_searchClearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_searchClearButton->setAutoRaise(true);
    m_searchClearButton->setFocusPolicy(Qt::NoFocus);
    m_searchClearButton->setVisible(false);
    m_searchClearButton->setCursor(Qt::PointingHandCursor);
    m_searchClearButton->setToolTip(tr("Clear search"));
    connect(m_searchClearButton, &QToolButton::clicked, m_searchBox, &QLineEdit::clear);
    rowLayout->addWidget(m_searchClearButton, 0, Qt::AlignVCenter);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(200);
    connect(m_searchDebounce, &QTimer::timeout, this, &WorkspacePanelController::runSearch);

    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString &text)
            {
        if (m_searchClearButton)
            m_searchClearButton->setVisible(!text.isEmpty());
        if (text.isEmpty()) {
            clearSearch();
        } else {
            setSearchModeActive(true);
            scheduleSearch();
        } });
}

QWidget *WorkspacePanelController::panelWidget() const
{
    return m_workspacePanel;
}

void WorkspacePanelController::focusSearch()
{
    if (!m_searchBox)
        return;
    m_searchBox->setFocus(Qt::ShortcutFocusReason);
    if (m_searchBox->text().isEmpty())
        m_searchBox->setCursorPosition(0);
    else
        m_searchBox->selectAll();
}

bool WorkspacePanelController::hasWorkspaceFocus() const
{
    return m_workspaceTree &&
           (m_workspaceTree->hasFocus() || m_workspaceInteractionActive);
}

QTreeView *WorkspacePanelController::treeView() const
{
    return m_workspaceTree;
}

void WorkspacePanelController::setHeaderHeight(int px)
{
    if (!m_treeContainer)
        return;
    QHeaderView *header = m_treeContainer->standaloneHeader();
    if (!header || px <= 0)
        return;

    header->setFixedHeight(px);

    // QSS-styled QHeaderView sections don't reliably honor
    // setDefaultAlignment()'s vertical component, so center the text by
    // splitting the leftover vertical space into padding instead of
    // trusting alignment to do it. header->font() reflects the app's default
    // font, NOT the "font-size: 12px" set on QHeaderView::section in
    // Stylesheet.h (QSS font properties on a subcontrol don't update
    // QWidget::font()), so build the metrics from that same literal instead.
    QFont sectionFont(QStringLiteral("Segoe UI"));
    sectionFont.setPixelSize(12);
    const QFontMetrics fm(sectionFont);
    const int vPad = std::max(0, (px - fm.height()) / 2);
    header->setStyleSheet(QString::asprintf(
        "QHeaderView::section { padding: %dpx 6px; }", vPad));
}

void WorkspacePanelController::setVisibleDeferringBuild(bool visible)
{
    if (!visible) {
        clearSearchSelection();
        captureOutlineSplitterState();
    }
    m_workspaceTreeVisible = visible;

    updateToggleIcon(m_darkProvider ? m_darkProvider() : false);

    // The empty panel still occupies its restored splitter section, so the tree
    // materializes inside a correctly sized panel and nothing around it moves.
    if (m_workspacePanel)
        m_workspacePanel->setVisible(visible);
}

void WorkspacePanelController::materialize()
{
    if (!m_workspaceTreeVisible)
        return;
    buildPanel();
    ensureModel();
    updateRoot();
    restorePanelMode();
}

void WorkspacePanelController::materializeAfterFirstPaint()
{
    if (!m_workspaceTreeVisible)
        return;
    if (m_workspaceModel || m_deferredTreeTimer || ShellWarmUp::finished()) {
        materialize();
        return;
    }

    ShellWarmUp::start(m_workspaceRootPath);
    buildPanel();
    restorePanelMode();

    // Polled instead of signalled: the warm-up thread may outlive every Qt object
    // here, so it must not hold one.
    m_deferredTreeTimer = new QTimer(this);
    m_deferredTreeTimer->setTimerType(Qt::PreciseTimer);
    QElapsedTimer waited;
    waited.start();
    connect(m_deferredTreeTimer, &QTimer::timeout, this, [this, waited]() {
        if (!ShellWarmUp::finished() && waited.elapsed() < kShellWarmUpWaitMs)
            return;
        m_deferredTreeTimer->stop();
        updateRoot();
        FASTMD_MARK("  task: workspace tree built");
    });
    m_deferredTreeTimer->start(kShellWarmUpPollMs);
}

void WorkspacePanelController::buildPanel()
{
    ensureUi();
    // setVisibleDeferringBuild() may have refreshed the theme before the
    // lazily-created header buttons existed. Refresh once after construction
    // so their icons are initialized on the first reveal as well.
    updateToggleIcon(m_darkProvider ? m_darkProvider() : false);
    if (m_workspacePanel)
        m_workspacePanel->setVisible(true);
}

void WorkspacePanelController::restorePanelMode()
{
    // The persisted mode is applied only here, after first paint: reading it
    // any earlier would put SourceControlPanel (and a git probe) on the
    // startup path. Applied once -- a later materialize() is a no-op.
    if (!m_panelModeRestored) {
        m_panelModeRestored = true;
        QSettings settings;
        if (settings.value(QStringLiteral("workspace/panelMode"), 0).toInt() == 1)
            setPanelMode(PanelMode::SourceControl, false);
        else
            applyPanelMode();
    }
}

void WorkspacePanelController::setVisible(bool visible, bool persist)
{
    setVisibleDeferringBuild(visible);

    // First reveal lazily builds the tree UI, the filesystem model, and the root.
    materialize();

    if (persist)
        emit visibilityChanged(visible);
}

void WorkspacePanelController::ensureModel()
{
    if (m_workspaceModel)
        return;
    // A pending startup build ends here, whoever gets here first: run later, it
    // would move the selection and close an inline rename.
    if (m_deferredTreeTimer)
        m_deferredTreeTimer->stop();

    m_workspaceModel = new QFileSystemModel(this);
    // Static because the model's gatherer thread uses it for as long as the model
    // lives. Set before the option, which is stored on the provider.
    static WorkspaceIconProvider iconProvider;
    m_workspaceModel->setIconProvider(&iconProvider);
    m_workspaceModel->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
    m_workspaceModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_workspaceModel->setReadOnly(true);
    connect(m_workspaceModel, &QFileSystemModel::fileRenamed, this,
            [this](const QString& directory, const QString& oldName, const QString& newName) {
        // QFileSystemModel receives Explorer renames with the old/new pair,
        // which QFileSystemWatcher does not expose. Forward it so open tabs
        // stay in sync even if the lower-level file notification is coalesced.
        emit fileRenamed(QDir(directory).filePath(oldName),
                         QDir(directory).filePath(newName));
    });

    m_filterProxy = new WorkspaceTreeFilterModel(this);
    m_filterProxy->setSourceFsModel(m_workspaceModel);
    m_filterProxy->setShowAll(m_showAll);

    m_navigationModel = new WorkspaceNavigationModel(this);
    m_navigationModel->setSourceModel(m_filterProxy);
    m_workspaceTree->setBranchVisibilityProvider([this](const QModelIndex &viewIndex)
                                                 {
        if (!m_navigationModel || !m_filterProxy ||
            m_navigationModel->isParentEntry(viewIndex))
            return false;
        return m_filterProxy->hasVisibleDescendants(
            m_navigationModel->sourceIndexForEntry(viewIndex)); });
    // The branch decorations are painted from a probe that now answers
    // provisionally on a cache miss and completes off the paint path.
    connect(m_filterProxy, &WorkspaceTreeFilterModel::branchVisibilityChanged, this, [this]() {
        if (m_workspaceTree)
            m_workspaceTree->viewport()->update();
    });
    m_navigationModel->setParentEntryIcon(
        IconHelper::parentFolderIcon());
    m_workspaceTree->setModel(m_navigationModel);
    m_workspaceTree->setSortingEnabled(true);
    m_workspaceTree->sortByColumn(m_sortColumn, m_sortOrder);

    connect(m_workspaceTree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]()
            {
        if (!m_workspaceTree)
            return;

        m_workspaceTree->viewport()->update();

        if (!m_hoverRevealButton || !m_hoverRevealButton->isVisible())
            return;

        const QModelIndex hoveredIndex = m_workspaceTree->indexAt(
            m_workspaceTree->viewport()->mapFromGlobal(QCursor::pos()));
        m_hoverRowSelected = hoveredIndex.isValid() &&
                             m_workspaceTree->selectionModel()->isSelected(hoveredIndex);
        m_hoverRevealButton->setProperty("workspaceRowSelected", m_hoverRowSelected);
        m_hoverRevealButton->style()->unpolish(m_hoverRevealButton);
        m_hoverRevealButton->style()->polish(m_hoverRevealButton);

        updateHoverRevealIcon(); });

    m_renameDelegate = new WorkspaceRenameDelegate(
        [this](const QModelIndex &proxyIndex) -> QString
        {
            if (!m_workspaceModel || !m_filterProxy || !proxyIndex.isValid())
                return QString();
            return m_workspaceModel->filePath(sourceIndexForView(proxyIndex));
        },
        this);
    m_renameDelegate->setTrailingActionWidth(kRevealButtonReserve);
    connect(m_renameDelegate, &WorkspaceRenameDelegate::renamed, this, &WorkspacePanelController::onItemRenamed);
    m_workspaceTree->setItemDelegate(m_renameDelegate);

    // QFileSystemModel columns: 0=Name, 1=Size, 2=Type, 3=Date Modified.
    // Put the columns in the user-facing Name, File Type, Date Modified, Size
    // order. Name is the only column shown by default; the metadata columns
    // are opt-in through the header context menu.
    m_workspaceTree->header()->moveSection(2, 1);
    m_workspaceTree->header()->moveSection(3, 2);
    m_workspaceTree->setColumnHidden(2, !m_fileTypeColumnVisible);
    // Initially hide metadata columns; they can be shown via the
    // context menu and their visibility is restored from workspace settings.
    m_workspaceTree->setColumnHidden(1, !m_sizeColumnVisible);
    m_workspaceTree->setColumnHidden(3, !m_dateModifiedColumnVisible);
    // All columns are Interactive so every divider is user-draggable (Explorer
    // style). Name uses a fixed default width until the user resizes it.
    m_workspaceTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_workspaceTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_workspaceTree->header()->resizeSection(3, 100);
    m_workspaceTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_workspaceTree->header()->resizeSection(2, 110);
    m_workspaceTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_workspaceTree->header()->resizeSection(1, 60);
    // NativeTreeContainer hides the tree's own QHeaderView and shows a
    // standalone replica in its place -- that standalone header is what the
    // user actually sees and right-clicks, so the context menu must be
    // installed there. The section-hidden state itself is still toggled on
    // the real (model-bound) header, since that's what governs the tree's
    // actual column layout; syncHeaderState() below propagates the change
    // back to the standalone header's display afterward.
    if (m_treeContainer && m_treeContainer->standaloneHeader())
    {
        QHeaderView *standalone = m_treeContainer->standaloneHeader();
        standalone->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(standalone, &QHeaderView::customContextMenuRequested, this, [this](const QPoint &pos)
                {
            QHeaderView* header = m_workspaceTree->header();
            QHeaderView* standaloneHeader = m_treeContainer->standaloneHeader();
            AlignedMenu menu(m_workspacePanel);
            menu.setObjectName(QStringLiteral("workspaceCheckMenu"));
            menu.setShowAnimationStyle(AlignedMenu::ContextMenu);
            menu.setShowAnimationDuration(80);
            QAction* actType = menu.addAction(tr("File Type"));
            actType->setCheckable(true);
            actType->setChecked(!header->isSectionHidden(2));
            connect(actType, &QAction::toggled, this, [this](bool on)
                    { setOptionalColumnsVisible(on, m_dateModifiedColumnVisible, m_sizeColumnVisible); });
            QAction* actDate = menu.addAction(tr("Date Modified"));
            actDate->setCheckable(true);
            actDate->setChecked(!header->isSectionHidden(3));
            connect(actDate, &QAction::toggled, this, [this](bool on)
                    {
                setOptionalColumnsVisible(m_fileTypeColumnVisible, on, m_sizeColumnVisible); });
            QAction* actSize = menu.addAction(tr("Size"));
            actSize->setCheckable(true);
            actSize->setChecked(!header->isSectionHidden(1));
            connect(actSize, &QAction::toggled, this, [this](bool on)
                    {
                setOptionalColumnsVisible(m_fileTypeColumnVisible, m_dateModifiedColumnVisible, on); });
            menu.exec(standaloneHeader->mapToGlobal(pos));
            if (m_treeContainer)
                m_treeContainer->syncHeaderState(); });

        connect(standalone, &QHeaderView::sectionHandleDoubleClicked, this,
                [this](int logicalIndex)
                {
            if (!m_workspaceTree)
                return;
            if (logicalIndex == 0)
            {
                m_nameColumnUserResized = false;
                rebalanceNameColumn();
                return;
            }

            const int defaultWidth = logicalIndex == 1 ? 60
                                   : logicalIndex == 2 ? 110
                                   : logicalIndex == 3 ? 100
                                   : 0;
            if (defaultWidth <= 0)
                return;

            m_adjustingColumns = true;
            m_workspaceTree->header()->resizeSection(logicalIndex, defaultWidth);
            m_adjustingColumns = false;
            m_columnsInitialized = true;
            if (m_treeContainer)
                m_treeContainer->syncHeaderState();
            scheduleColumnLayoutPersist();
        });
    }

    // Sync state since model and sections just changed
    if (m_treeContainer)
        m_treeContainer->syncHeaderState();

    auto applyHeaderSort = [this](int column) {
        const Qt::SortOrder order = column == m_sortColumn
            ? (m_sortOrder == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder)
            : Qt::AscendingOrder;
        setSort(column, order);
    };
    connect(m_workspaceTree->header(), &QHeaderView::sectionClicked, this, applyHeaderSort);
    if (m_treeContainer && m_treeContainer->standaloneHeader())
        connect(m_treeContainer->standaloneHeader(), &QHeaderView::sectionClicked, this, applyHeaderSort);

    if (!m_savedColumnLayoutState.isEmpty() &&
        m_workspaceTree->header()->restoreState(m_savedColumnLayoutState))
    {
        m_nameColumnUserResized = true;
        m_columnsInitialized = true;
        if (m_treeContainer)
            m_treeContainer->syncHeaderState();
    }

    // Visibility and sorting are persisted as explicit Workspace settings.
    // They take precedence over a header state saved by an older layout.
    setOptionalColumnsVisible(m_fileTypeColumnVisible, m_dateModifiedColumnVisible,
                              m_sizeColumnVisible, false);
    setSort(m_sortColumn, m_sortOrder, false);

    connect(m_workspaceTree->header(), &QHeaderView::sectionResized,
            this, [this](int logicalIndex, int /*oldSize*/, int /*newSize*/)
            {
        if (m_adjustingColumns || !m_columnsInitialized)
            return;
        if (logicalIndex == 0)
        {
            m_nameColumnUserResized = true;
        }
        else if (!m_nameColumnUserResized)
        {
            QTimer::singleShot(0, this, [this]() { rebalanceNameColumn(); });
        }
        scheduleColumnLayoutPersist();
    });

    // The standalone header is not part of the tree's scroll area. Keep its
    // visible sections aligned when the independent column widths overflow
    // the workspace panel.
    if (m_treeContainer && m_treeContainer->standaloneHeader())
    {
        QHeaderView *standalone = m_treeContainer->standaloneHeader();
        connect(m_workspaceTree->horizontalScrollBar(), &QScrollBar::valueChanged,
                standalone, [standalone](int value) { standalone->setOffset(value); });
    }

    QTimer::singleShot(0, this, [this]() { rebalanceNameColumn(); });

    // QFileSystemModel loads directories asynchronously and emits
    // directoryLoaded once per directory, so a single expand of a large tree
    // produces a burst of them. Relayouting the tree and replaying the whole
    // expanded-path list per signal is O(directories x expanded paths); the
    // timer collapses each burst into one pass.
    m_treeRefreshTimer = new QTimer(this);
    m_treeRefreshTimer->setSingleShot(true);
    m_treeRefreshTimer->setInterval(30);
    connect(m_treeRefreshTimer, &QTimer::timeout, this, [this]()
            {
        if (!m_workspaceTreeVisible)
            return;
        m_workspaceTree->doItemsLayout();
        m_workspaceTree->viewport()->update();
        restoreExpandedState();
        if (!m_pendingRevealPath.isEmpty())
            revealPath(m_pendingRevealPath, 0); });

    connect(m_workspaceModel, &QFileSystemModel::directoryLoaded, this, [this](const QString & /*path*/)
            {
        // Never restart a pending pass: a steady stream of loads must not
        // postpone the refresh indefinitely.
        if (m_workspaceTreeVisible && !m_treeRefreshTimer->isActive())
            m_treeRefreshTimer->start(); });
}

void WorkspacePanelController::setRoot(const QString &root, bool persist,
                                       std::optional<bool> showAllFallback)
{
    const QFileInfo rootInfo(root);
    if (root.isEmpty() || !rootInfo.isDir() || !rootInfo.isReadable())
        return;

    const QString cleanRoot = normalizedWorkspaceRoot(root, m_normalizedRootCache);
    if (!m_showingDriveList && cleanRoot.compare(m_workspaceRootPath, Qt::CaseInsensitive) == 0)
        return;

    if (!m_workspaceRootPath.isEmpty())
        m_workspaceExpandedPaths.clear();

    m_showingDriveList = false;
    m_workspaceRootPath = cleanRoot;
    bool fallback = false;
    if (showAllFallback) {
        fallback = *showAllFallback;
    } else {
        QSettings settings;
        fallback = settings.value(QStringLiteral("workspace/showAll"), false).toBool();
    }
    setShowAll(savedShowAllForRoot(m_workspaceRootPath, fallback), false);
    m_workspacePanel->setVisible(m_workspaceTreeVisible);
    if (m_uiBuilt) {
        refreshHeaderLabel();
        clearSearch();
    }

    if (persist)
        emit rootChanged(cleanRoot);
    updateRoot();
}

void WorkspacePanelController::navigateToParent()
{
    if (m_showingDriveList || m_workspaceRootPath.isEmpty())
        return;
    QDir dir(m_workspaceRootPath);
    if (dir.cdUp() && dir.absolutePath().compare(m_workspaceRootPath, Qt::CaseInsensitive) != 0)
    {
        setRoot(dir.absolutePath());
        return;
    }
    showDriveList();
}

void WorkspacePanelController::navigateToCurrentFileFolder()
{
    const QString filePath = m_currentFilePathProvider ? m_currentFilePathProvider() : QString();
    if (filePath.isEmpty())
        return;
    const QFileInfo info(filePath);
    if (info.exists())
        setRoot(info.absolutePath());
}

void WorkspacePanelController::navigateToHomeFolder()
{
    const QString documentsFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QSettings settings;
    const QString configuredFolder = settings.value(QStringLiteral("workspace/homeFolder"), documentsFolder)
                                        .toString().trimmed();
    const QString homeFolder = configuredFolder.isEmpty() ? documentsFolder : configuredFolder;
    if (QFileInfo(homeFolder).isDir() && QFileInfo(homeFolder).isReadable()) {
        setRoot(homeFolder);
        return;
    }

    QToolTip::showText(m_workspacePanel->mapToGlobal(m_workspacePanel->rect().center()),
                       tr("Home Folder is no longer available. Showing Documents instead."),
                       m_workspacePanel);
    setRoot(documentsFolder);
}

void WorkspacePanelController::showDriveList()
{
    if (m_showingDriveList)
        return;

    m_showingDriveList = true;
    m_workspaceRootPath.clear();
    m_workspaceExpandedPaths.clear();
    QSettings settings;
    setShowAll(settings.value(QStringLiteral("workspace/showAll"), false).toBool(), false);
    if (m_uiBuilt) {
        refreshHeaderLabel();
        clearSearch();
    }
    // Drive-list mode clears the root without emitting rootChanged(), so the
    // Source Control panel has to be told directly or it keeps showing the
    // repository of a folder that is no longer open.
    if (m_sourceControlPanel)
        m_sourceControlPanel->setRepositoryFromWorkspace(QString());
    updateRoot();
}

void WorkspacePanelController::updateRoot()
{
    if (!m_workspacePanel || !m_workspaceTree)
        return;

    if (!m_workspaceTreeVisible)
        return;

    // The pending build ends in this same call, with whatever the root is then.
    if (!m_workspaceModel && m_deferredTreeTimer && m_deferredTreeTimer->isActive())
        return;

    ensureModel();

    if ((!m_showingDriveList && m_workspaceRootPath.isEmpty()) ||
        !m_filterProxy || !m_navigationModel || !m_workspaceModel)
        return;

    if (m_showingDriveList)
    {
        m_workspaceModel->setRootPath(QString());
        m_navigationModel->setWorkspaceRootIndex({});
        m_navigationModel->setParentEntryEnabled(true);
        m_workspaceTree->setRootIndex({});
        return;
    }

    // (Re)apply the root index -- cheap/idempotent, and needed the first
    // time the model/tree becomes visible after setRoot() ran while hidden.
    const QModelIndex srcRootIndex = m_workspaceModel->setRootPath(m_workspaceRootPath);
    const QModelIndex filterRootIndex = m_filterProxy->mapFromSource(srcRootIndex);
    // QFileSystemModel populates directories asynchronously. Do not turn a
    // temporarily unmapped folder into the model-root (drive) view; the
    // directoryLoaded handler above reapplies this root once it is ready.
    if (!srcRootIndex.isValid() || !filterRootIndex.isValid())
        return;
    m_navigationModel->setWorkspaceRootIndex(filterRootIndex);
    // The navigation model owns a real, virtual ".." item. Keep it visible
    // even at a filesystem root, where activating it simply has nowhere higher
    // to navigate; this keeps the file-manager row structurally consistent.
    m_navigationModel->setParentEntryEnabled(true);
    m_workspaceTree->setRootIndex(m_navigationModel->mapFromSource(filterRootIndex));

    const QString currentPath = m_currentFilePathProvider ? m_currentFilePathProvider() : QString();
    if (!currentPath.isEmpty())
    {
        const QString rel = QDir(m_workspaceRootPath).relativeFilePath(currentPath);
        if (rel != QStringLiteral("..") && !rel.startsWith(QStringLiteral("../")))
        {
            const QModelIndex srcIndex = m_workspaceModel->index(currentPath);
            if (srcIndex.isValid())
            {
                const QModelIndex proxyIndex = viewIndexForSource(srcIndex);
                if (proxyIndex.isValid())
                {
                    for (QModelIndex ancestor = proxyIndex.parent(); ancestor.isValid(); ancestor = ancestor.parent())
                        m_workspaceTree->expand(ancestor);
                    m_workspaceTree->setCurrentIndex(proxyIndex);
                    m_workspaceTree->scrollTo(proxyIndex);
                }
            }
        }
    }

    QTimer::singleShot(0, this, [this]()
                       { restoreExpandedState(); });
}

void WorkspacePanelController::rememberExpansion(const QString &path, bool expanded)
{
    if (path.isEmpty())
        return;

    if (expanded)
    {
        if (!m_workspaceExpandedPaths.contains(path))
            m_workspaceExpandedPaths.append(path);
    }
    else
    {
        m_workspaceExpandedPaths.removeAll(path);
    }
}

void WorkspacePanelController::restoreExpandedState()
{
    if (!m_workspaceTree || !m_workspaceModel || !m_filterProxy || m_workspaceRootPath.isEmpty())
        return;

    QStringList paths = m_workspaceExpandedPaths;
    paths.removeDuplicates();
    std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b)
              {
        if (a.size() != b.size())
            return a.size() < b.size();
        return a < b; });

    const QDir rootDir(m_workspaceRootPath);
    for (const QString &path : paths)
    {
        if (path.isEmpty())
            continue;
        const QString rel = rootDir.relativeFilePath(path);
        if (rel == QStringLiteral("..") || rel.startsWith(QStringLiteral("../")))
            continue;

        const QModelIndex srcIdx = m_workspaceModel->index(path);
        if (srcIdx.isValid())
            m_workspaceTree->expand(viewIndexForSource(srcIdx));
    }
}

void WorkspacePanelController::ensureSourceControlPanel()
{
    if (m_sourceControlPanel || !m_bodyStack)
        return;

    FASTMD_MARK("source control panel construction begin");
    m_sourceControlPanel = new SourceControlPanel(m_bodyStack);
    m_sourceControlPanel->setDarkMode(m_darkProvider && m_darkProvider());
    m_bodyStack->addWidget(m_sourceControlPanel);

    connect(this, &WorkspacePanelController::rootChanged,
            m_sourceControlPanel, &SourceControlPanel::setRepositoryFromWorkspace);
    // The initial root was seeded with persist=false and therefore never
    // announced through rootChanged(); read it once here, exactly as the old
    // standalone window did at hook-up time.
    m_sourceControlPanel->setRepositoryFromWorkspace(m_workspaceRootPath);

    emit sourceControlPanelCreated(m_sourceControlPanel);
    FASTMD_MARK("source control panel constructed");
}

void WorkspacePanelController::ensureSearchPanel()
{
    if (m_searchPanel || !m_bodyStack)
        return;

    m_searchPanel = new WorkspaceSearchPanel(m_bodyStack);
    m_searchPanel->setDarkMode(m_darkProvider && m_darkProvider());
    m_bodyStack->addWidget(m_searchPanel);
    connect(this, &WorkspacePanelController::rootChanged,
            m_searchPanel, &WorkspaceSearchPanel::setRootPath);
    m_searchPanel->setRootPath(m_workspaceRootPath);
    emit searchPanelCreated(m_searchPanel);
}

void WorkspacePanelController::openSearch(bool expandReplace, bool seedFromEditor)
{
    setPanelMode(PanelMode::Search);
    if (!m_searchPanel)
        return;
    if (expandReplace)
        m_searchPanel->setReplaceExpanded(true);
    if (seedFromEditor && m_searchSeedProvider) {
        const QString seed = m_searchSeedProvider();
        if (!seed.isEmpty())
            m_searchPanel->setQueryText(seed);
    }
    m_searchPanel->focusQuery();
}

void WorkspacePanelController::setPanelMode(PanelMode mode, bool persist)
{
    if (mode != m_panelMode) {
        clearSearchSelection();
        captureOutlineSplitterState();
    }
    if (mode == PanelMode::SourceControl) {
        ensureUi();
        ensureSourceControlPanel();
    } else if (mode == PanelMode::Search) {
        ensureUi();
        ensureSearchPanel();
        if (m_searchPanel)
            m_searchPanel->setRootPath(m_workspaceRootPath);
    }
    const bool changed = mode != m_panelMode;
    m_panelMode = mode;
    applyPanelMode();

    if (persist && mode != PanelMode::Search) {
        QSettings settings;
        settings.setValue(QStringLiteral("workspace/panelMode"),
                          mode == PanelMode::SourceControl ? 1 : 0);
    }
    if (changed)
        emit panelModeChanged(mode);
}

void WorkspacePanelController::applyPanelMode()
{
    const bool sourceControl = m_panelMode == PanelMode::SourceControl;
    const bool search = m_panelMode == PanelMode::Search;
    if (m_bodyStack) {
        if (sourceControl && m_sourceControlPanel)
            m_bodyStack->setCurrentWidget(m_sourceControlPanel);
        else if (search && m_searchPanel)
            m_bodyStack->setCurrentWidget(m_searchPanel);
        else if (m_workspaceBody)
            m_bodyStack->setCurrentWidget(m_workspaceBody);
    }
    if (m_modeToggle) {
        QSignalBlocker blocker(m_modeToggle);
        m_modeToggle->setChecked(sourceControl);
        m_modeToggle->setToolTip(sourceControl ? tr("Workspace") : tr("Source Control"));
    }
    if (m_searchModeButton) {
        QSignalBlocker blocker(m_searchModeButton);
        m_searchModeButton->setChecked(search);
        m_searchModeButton->setToolTip(search ? tr("Workspace")
                                              : tr("Search in Files (Ctrl+Shift+F)"));
    }
    updateToggleIcon(m_darkProvider && m_darkProvider());
    scheduleOutlineReferenceLayout();
}

void WorkspacePanelController::updateToggleIcon(bool dark)
{
    if (m_workspaceTree)
        m_workspaceTree->setDarkTheme(dark);
    QColor color = dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
    if (m_showAllButton)
    {
        const ushort filterIcon = m_showAll ? kIconFilterOff : kIconFilterActive;
        const QString filterToolTip = m_showAll ? tr("Filter to Documents, Exports & Images") : tr("Show All Files");
        m_showAllButton->setIcon(IconHelper::materialIcon(QChar(filterIcon), color, 18));
        m_showAllButton->setToolTip(filterToolTip);
    }
    if (m_sortButton)
        m_sortButton->setIcon(QIcon(QStringLiteral(":/icons/%1").arg(
            dark ? QStringLiteral("workspace-sort-dark.svg") : QStringLiteral("workspace-sort.svg"))));
    if (m_navigationModel)
        m_navigationModel->setParentEntryIcon(IconHelper::parentFolderIcon());
    // Toolbar buttons created with the initial theme color at build time:
    if (m_currentFileFolderButton)
        m_currentFileFolderButton->setIcon(IconHelper::materialIcon(QChar(kIconCurrentFileFolder), color));
    if (m_homeFolderButton)
        m_homeFolderButton->setIcon(IconHelper::materialIcon(QChar(kIconHomeFolder), color));
    if (m_newSplitButton)
    {
        m_newSplitButton->setIcon(IconHelper::materialIcon(QChar(kIconNewFile), color, 18));
        static_cast<WorkspaceSplitButton *>(m_newSplitButton)->setThemeColor(color);
    }
    if (m_openFolderSplitButton)
    {
        m_openFolderSplitButton->setIcon(IconHelper::materialIcon(QChar(kIconOpenFolder), color, 18));
        static_cast<WorkspaceSplitButton *>(m_openFolderSplitButton)->setThemeColor(color);
    }
    if (m_expandButton)
        m_expandButton->setIcon(IconHelper::materialIcon(QChar(kIconExpandAll), color, 18));
    if (m_collapseButton)
        m_collapseButton->setIcon(IconHelper::materialIcon(QChar(kIconCollapseAll), color, 18));
    if (m_modeToggle) {
        // The icon names the mode the button switches *to*, the way the
        // Markdown/Code toggle in the status bar does.
        const bool inSourceControl = m_panelMode == PanelMode::SourceControl;
        m_modeToggle->setIcon(IconHelper::materialIcon(
            QChar(inSourceControl ? kIconWorkspaceMode : kIconSourceControlMode), color, 16));
    }
    if (m_sourceControlPanel)
        m_sourceControlPanel->setDarkMode(dark);
    if (m_searchPanel)
        m_searchPanel->setDarkMode(dark);
    if (m_searchModeButton) {
        // Show the action this button will perform, matching the Source
        // Control toggle's state-dependent icon behavior.
        const bool inSearch = m_panelMode == PanelMode::Search;
        m_searchModeButton->setIcon(IconHelper::materialIcon(
            QChar(inSearch ? kIconWorkspaceMode : kIconSearch), color, 16));
    }
    if (m_copyFolderPathButton)
        m_copyFolderPathButton->setIcon(IconHelper::materialIcon(QChar(0xE14D), color, 16));
    if (m_revealFolderButton)
        m_revealFolderButton->setIcon(IconHelper::materialIcon(QChar(0xE89E), color, 16));
    if (m_terminalButton)
        m_terminalButton->setIcon(IconHelper::materialIcon(QChar(0xEB8E), color, 16));
    if (m_workspaceSplitter)
        m_workspaceSplitter->setDarkMode(dark);
    updateOutlineHeaderIcon();
    if (m_workspacePanel)
        m_workspacePanel->update();
    if (m_searchIconLabel)
        m_searchIconLabel->setPixmap(IconHelper::materialIcon(QChar(kIconSearch), color, 16).pixmap(16, 16));
    if (m_searchClearButton)
        m_searchClearButton->setIcon(IconHelper::materialIcon(QChar(kIconClose), color, 12));
    if (m_searchListView)
        m_searchListView->setStyleSheet(searchListStyle(dark));
    if (m_searchEmptyLabel)
        m_searchEmptyLabel->setStyleSheet(QStringLiteral(
            "QLabel#workspaceSearchEmptyState { color: %1; font-style: italic; }")
            .arg(dark ? QStringLiteral("#999999") : QStringLiteral("#888888")));
}

void WorkspacePanelController::setShowAll(bool showAll, bool persist)
{
    if (m_showAll == showAll)
        return;
    m_showAll = showAll;
    updateToggleIcon(m_darkProvider ? m_darkProvider() : false);
    if (m_filterProxy)
        m_filterProxy->setShowAll(showAll);
    if (m_workspaceTree)
        m_workspaceTree->viewport()->update();
    if (m_showAllButton)
    {
        m_showAllButton->setChecked(showAll);
        const QString filterToolTip = showAll ? tr("Filter to Documents, Exports & Images") : tr("Show All Files");
        m_showAllButton->setToolTip(filterToolTip);
    }
    if (m_searchBox && !m_searchBox->text().isEmpty())
        scheduleSearch();
    if (persist)
    {
        saveShowAllForRoot(m_workspaceRootPath, showAll);
        emit showAllChanged(showAll);
    }
}

void WorkspacePanelController::setOptionalColumnsVisible(bool fileTypeVisible, bool dateModifiedVisible, bool sizeVisible, bool persist)
{
    const bool changed = m_fileTypeColumnVisible != fileTypeVisible ||
                         m_dateModifiedColumnVisible != dateModifiedVisible ||
                         m_sizeColumnVisible != sizeVisible;
    m_fileTypeColumnVisible = fileTypeVisible;
    m_dateModifiedColumnVisible = dateModifiedVisible;
    m_sizeColumnVisible = sizeVisible;

    if (!m_workspaceTree)
        return;

    m_adjustingColumns = true;
    m_workspaceTree->setColumnHidden(2, !m_fileTypeColumnVisible);
    m_workspaceTree->setColumnHidden(3, !m_dateModifiedColumnVisible);
    m_workspaceTree->setColumnHidden(1, !m_sizeColumnVisible);
    m_adjustingColumns = false;
    if (m_treeContainer)
        m_treeContainer->syncHeaderState();
    if (!m_nameColumnUserResized)
        QTimer::singleShot(0, this, [this]() { rebalanceNameColumn(); });
    if (persist && changed)
        emit optionalColumnsChanged();
}

void WorkspacePanelController::setSort(int column, Qt::SortOrder order, bool persist)
{
    if (column < 0 || column > 3 ||
        (order != Qt::AscendingOrder && order != Qt::DescendingOrder))
        return;
    const bool changed = m_sortColumn != column || m_sortOrder != order;
    m_sortColumn = column;
    m_sortOrder = order;
    if (m_workspaceTree)
        m_workspaceTree->sortByColumn(column, order);
    updateSortUi();
    if (persist && changed)
        emit sortChanged();
}

void WorkspacePanelController::updateSortUi()
{
    static const QStringList fields { tr("Name"), tr("Size"), tr("File Type"), tr("Date Modified") };
    if (m_sortButton) {
        const QString direction = m_sortOrder == Qt::AscendingOrder ? tr("Ascending") : tr("Descending");
        m_sortButton->setToolTip(tr("Sort files: %1, %2").arg(fields.value(m_sortColumn), direction));
    }
    if (!m_sortButton || !m_sortButton->menu())
        return;
    for (QAction *action : m_sortButton->menu()->actions()) {
        if (!action->isCheckable())
            continue;
        const int value = action->data().toInt();
        const bool isField = action->property("workspaceSortField").toBool();
        action->setChecked(isField ? value == m_sortColumn : value == static_cast<int>(m_sortOrder));
    }
    if (m_workspaceTree)
        m_workspaceTree->header()->setSortIndicator(m_sortColumn, m_sortOrder);
    if (m_treeContainer)
        m_treeContainer->syncHeaderState();
}

QByteArray WorkspacePanelController::columnLayoutState() const
{
    if (m_workspaceTree && m_workspaceModel)
        return m_workspaceTree->header()->saveState();
    return m_savedColumnLayoutState;
}

void WorkspacePanelController::setColumnLayoutState(const QByteArray& state)
{
    m_savedColumnLayoutState = state;
    if (!m_workspaceTree || !m_workspaceModel || state.isEmpty())
        return;

    if (m_workspaceTree->header()->restoreState(state))
    {
        m_nameColumnUserResized = true;
        m_columnsInitialized = true;
        if (m_treeContainer)
            m_treeContainer->syncHeaderState();
    }
}

void WorkspacePanelController::setExpandedPaths(const QStringList &paths)
{
    m_workspaceExpandedPaths = paths;
}

// ---------------------------------------------------------------------------
// Outline section

QWidget* WorkspacePanelController::buildOutlineSection()
{
    m_outlineSection = new QWidget(m_workspacePanel);
    auto* sectionLayout = new QVBoxLayout(m_outlineSection);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(0);

    // Header row: a plain QWidget with setFixedHeight is the height authority.
    // QToolButton's platform-style sizeHint is font-driven and shorter than
    // any setFixedHeight request, so that call has no effect on the layout.
    // A plain QWidget container — exactly the approach TerminalPanel uses for
    // its header widget — guarantees the splitter sees the correct height.
    auto* headerWidget = new QWidget(m_outlineSection);
    headerWidget->setObjectName(QStringLiteral("workspaceOutlineHeader"));
    headerWidget->setFixedHeight(kOutlineHeaderHeight);
    {
        auto* hLayout = new QHBoxLayout(headerWidget);
        hLayout->setContentsMargins(0, 0, 0, 0);
        hLayout->setSpacing(0);

        // The whole row is the disclosure control (icon + uppercase label),
        // the same "clickable header row" shape as the Source Control panel's
        // GRAPH header, so clicking anywhere on it toggles the section.
        m_outlineHeaderButton = new QToolButton(headerWidget);
        m_outlineHeaderButton->setObjectName(QStringLiteral("workspaceOutlineHeaderButton"));
        m_outlineHeaderButton->setCheckable(true);
        m_outlineHeaderButton->setAutoRaise(true);
        m_outlineHeaderButton->setCursor(Qt::PointingHandCursor);
        m_outlineHeaderButton->setFocusPolicy(Qt::NoFocus);
        // Expanding/Expanding: fills the container both axes so the icon+text
        // are vertically centered within the full kOutlineHeaderHeight px.
        m_outlineHeaderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_outlineHeaderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_outlineHeaderButton->setIconSize(QSize(14, 14));
        m_outlineHeaderButton->setText(tr("OUTLINE"));
        connect(m_outlineHeaderButton, &QToolButton::toggled, this,
                [this](bool expanded) { setOutlineExpanded(expanded); });

        hLayout->addWidget(m_outlineHeaderButton);
    }

    m_outlineTree = new QTreeWidget(m_outlineSection);
    m_outlineTree->setObjectName(QStringLiteral("workspaceOutlineTree"));
    m_outlineTree->setHeaderHidden(true);
    m_outlineTree->setIndentation(12);
    m_outlineTree->setUniformRowHeights(true);
    m_outlineTree->setTextElideMode(Qt::ElideRight);
    m_outlineTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_outlineTree->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_outlineTree));
    m_outlineTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    setReadOnlyItemViewCursor(m_outlineTree);
    connect(m_outlineTree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
        if (!item || item->flags() == Qt::NoItemFlags)
            return;
        emit outlineHeadingActivated(item->data(0, Qt::UserRole).toInt());
    });

    sectionLayout->addWidget(headerWidget);
    sectionLayout->addWidget(m_outlineTree, 1);
    return m_outlineSection;
}

void WorkspacePanelController::setOutlineExpanded(bool expanded, bool persist)
{
    if (m_outlineExpanded && !expanded)
        captureOutlineSplitterState();
    m_outlineExpanded = expanded;
    if (m_outlineHeaderButton) {
        QSignalBlocker blocker(m_outlineHeaderButton);
        m_outlineHeaderButton->setChecked(expanded);
    }
    applyOutlineLayout();
    scheduleOutlineReferenceLayout();
    updateOutlineHeaderIcon();
    if (expanded)
        refreshOutlineFromEditor();
    if (persist)
        emit outlineExpandedChanged(expanded);
}

void WorkspacePanelController::applyOutlineLayout()
{
    if (!m_outlineSection || !m_outlineTree || !m_workspaceSplitter)
        return;

    m_outlineTree->setVisible(m_outlineExpanded);
    if (m_outlineExpanded) {
        m_outlineSection->setMinimumHeight(0);
        m_outlineSection->setMaximumHeight(QWIDGETSIZE_MAX);
        // Construction/expansion can happen on a hidden stack page. Restore
        // after Show/Resize and layout, before capturing any reference ratio.
        m_outlineRestorePending = true;
        scheduleOutlineReferenceLayout();
    } else {
        // Collapsed: only the header row remains, exactly like the Source
        // Control panel's Graph header band -- no dedicated collapse affordance
        // is needed because the splitter itself never lets this shrink further.
        // setSizes() is what actually reallocates the pixels immediately (the
        // min/max clamp alone only bounds a future relayout/drag), so both are
        // needed to collapse the pane right away rather than on the next resize.
        m_outlineSection->setMinimumHeight(kOutlineHeaderHeight);
        m_outlineSection->setMaximumHeight(kOutlineHeaderHeight);
        m_workspaceSplitter->setSizes({ 1000, kOutlineHeaderHeight });
    }
}

void WorkspacePanelController::updateOutlineHeaderIcon()
{
    if (!m_outlineHeaderButton)
        return;
    m_outlineHeaderButton->setIcon(IconHelper::materialIcon(
        QChar(m_outlineExpanded ? kIconChevronDown : kIconChevronRight), iconColor(), 14));
}

void WorkspacePanelController::setOutlineEditor(MarkdownEditor* editor)
{
    if (m_outlineEditor == editor)
        return;
    if (m_outlineEditor)
        disconnect(m_outlineEditor, &QPlainTextEdit::textChanged,
                   this, &WorkspacePanelController::refreshOutlineFromEditor);
    m_outlineEditor = editor;
    if (m_outlineEditor)
        connect(m_outlineEditor, &QPlainTextEdit::textChanged,
                this, &WorkspacePanelController::refreshOutlineFromEditor);
    refreshOutlineFromEditor();
}

void WorkspacePanelController::refreshOutlineFromEditor()
{
    // Parsing headings is a cheap single-line-prefix scan (no per-line regex,
    // see Rule 13), but there is no reason to pay it on every keystroke while
    // the section is collapsed or the panel was never built.
    if (!m_uiBuilt || !m_outlineExpanded)
        return;
    OutlineModel::populate(m_outlineTree, m_outlineEditor ? m_outlineEditor->toPlainText() : QString(),
                           tr("No headings found"));
}

QByteArray WorkspacePanelController::outlineSplitterState() const
{
    return m_savedOutlineSplitterState;
}

void WorkspacePanelController::setOutlineSplitterState(const QByteArray& state)
{
    m_savedOutlineSplitterState = validOutlineSplitterState(state) ? state : QByteArray();
    m_outlineReferenceRatio.reset();
    m_outlineRestorePending = true;
    scheduleOutlineReferenceLayout();
}

bool WorkspacePanelController::hasValidOutlineGeometry() const
{
    if (!m_outlineExpanded || !m_workspaceSplitter || !m_workspaceTreeVisible
        || m_panelMode != PanelMode::Workspace || !m_workspaceSplitter->isVisible()
        || m_workspaceSplitter->window()->isMinimized()
        || m_workspaceSplitter->width() <= 0)
        return false;
    const int available = m_workspaceSplitter->height() - m_workspaceSplitter->handleWidth();
    const int minimum = m_contentStack->minimumSizeHint().height()
        + m_outlineSection->minimumSizeHint().height();
    return available > std::max(0, minimum)
        && (!m_outlineReferenceWidget || (m_outlineReferenceWidget->isVisible()
            && m_outlineReferenceWidget->height() > 0));
}

void WorkspacePanelController::captureOutlineSplitterState()
{
    if (m_outlineRestorePending || m_outlineReferenceLayoutPending || !hasValidOutlineGeometry())
        return;
    const auto sizes = m_workspaceSplitter->sizes();
    if (sizes.size() != 2 || sizes[0] <= 0 || sizes[1] <= 0
        || sizes[0] + sizes[1] != m_workspaceSplitter->height() - m_workspaceSplitter->handleWidth())
        return;
    const QByteArray state = m_workspaceSplitter->saveState();
    if (validOutlineSplitterState(state))
        m_savedOutlineSplitterState = state;
}

void WorkspacePanelController::setOutlineReferenceWidget(QWidget* reference)
{
    if (m_outlineReferenceWidget == reference)
        return;
    if (m_outlineReferenceWidget)
        m_outlineReferenceWidget->removeEventFilter(this);
    m_outlineReferenceWidget = reference;
    if (m_outlineReferenceWidget)
        m_outlineReferenceWidget->installEventFilter(this);
    scheduleOutlineReferenceLayout();
}

void WorkspacePanelController::captureOutlineReferenceRatio()
{
    if (m_outlineRestorePending || !hasValidOutlineGeometry() || !m_outlineReferenceWidget)
        return;

    const auto* referenceSplitter = qobject_cast<const QSplitter*>(m_outlineReferenceWidget.data());
    if (!referenceSplitter)
        return;

    const int available = ProportionalSplitterGeometry::availableHeight(
        referenceSplitter->height(), referenceSplitter->handleWidth());
    if (available <= 0)
        return;

    const int handleY = m_workspaceSplitter->handle(1)->mapToGlobal(QPoint()).y();
    const int referenceY = m_outlineReferenceWidget->mapToGlobal(QPoint()).y();
    m_outlineReferenceRatio = ProportionalSplitterGeometry::ratioForPosition(
        handleY - referenceY, available);
}

void WorkspacePanelController::applyOutlineReferenceRatio()
{
    if (!hasValidOutlineGeometry() || !m_outlineReferenceWidget
        || !m_outlineReferenceRatio)
        return;

    const auto* referenceSplitter = qobject_cast<const QSplitter*>(m_outlineReferenceWidget.data());
    if (!referenceSplitter)
        return;

    const int referenceAvailable = ProportionalSplitterGeometry::availableHeight(
        referenceSplitter->height(), referenceSplitter->handleWidth());
    const int referencePosition = ProportionalSplitterGeometry::positionForRatio(
        *m_outlineReferenceRatio, referenceAvailable);
    const int targetFirstPane = ProportionalSplitterGeometry::firstPaneSizeForReferencePosition(
        m_outlineReferenceWidget->mapToGlobal(QPoint()).y(), referencePosition,
        m_workspaceSplitter->mapToGlobal(QPoint()).y());
    const int localAvailable = ProportionalSplitterGeometry::availableHeight(
        m_workspaceSplitter->height(), m_workspaceSplitter->handleWidth());
    m_workspaceSplitter->setSizes({ targetFirstPane, localAvailable - targetFirstPane });
}

void WorkspacePanelController::scheduleOutlineReferenceLayout()
{
    if (m_outlineReferenceLayoutPending)
        return;
    m_outlineReferenceLayoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_outlineReferenceLayoutPending = false;
        if (!hasValidOutlineGeometry())
            return;
        if (m_outlineRestorePending) {
            const bool restored = !m_savedOutlineSplitterState.isEmpty()
                && m_workspaceSplitter->restoreState(m_savedOutlineSplitterState);
            m_workspaceSplitter->reassertHandleWidth();
            m_workspaceSplitter->setChildrenCollapsible(false);
            if (!restored) {
                const int available = m_workspaceSplitter->height() - m_workspaceSplitter->handleWidth();
                const int outline = available / 3;
                m_workspaceSplitter->setSizes({ available - outline, outline });
            }
            m_outlineRestorePending = false;
        }
        if (m_outlineReferenceRatio)
            applyOutlineReferenceRatio();
        else
            captureOutlineReferenceRatio();
        captureOutlineSplitterState();
    });
}

void WorkspacePanelController::refreshHeaderLabel()
{
    if (!m_headerLabel)
        return;
    if (m_workspaceRootPath.isEmpty())
    {
        m_headerLabel->setText(m_showingDriveList ? tr("This PC") : QString());
        m_headerLabel->setToolTip(QString());
        updateHeaderActions();
        return;
    }
    // Only the folder's own name is shown; the full path stays in the tooltip.
    // A drive root has no name of its own, so it keeps showing the drive.
    const QString fullPath = QDir::toNativeSeparators(m_workspaceRootPath);
    QString displayName = QFileInfo(m_workspaceRootPath).fileName();
    if (displayName.isEmpty())
        displayName = fullPath;
    const QFontMetrics fm(m_headerLabel->font());
    m_headerLabel->setText(fm.elidedText(displayName, Qt::ElideMiddle, m_headerLabel->width() - 16));
    m_headerLabel->setToolTip(fullPath);
    updateHeaderActions();
}

void WorkspacePanelController::updateHeaderActions()
{
    const bool hasWorkspace = !m_workspaceRootPath.isEmpty() && !m_showingDriveList;
    for (QToolButton* button : { m_copyFolderPathButton, m_revealFolderButton, m_terminalButton }) {
        if (button)
            button->setVisible(hasWorkspace);
    }
}

QModelIndex WorkspacePanelController::sourceIndexForView(const QModelIndex &viewIndex) const
{
    if (!m_navigationModel || !m_filterProxy || !viewIndex.isValid() ||
        m_navigationModel->isParentEntry(viewIndex))
    {
        return {};
    }
    return m_filterProxy->mapToSource(m_navigationModel->mapToSource(viewIndex));
}

QModelIndex WorkspacePanelController::viewIndexForSource(const QModelIndex &sourceIndex) const
{
    if (!m_navigationModel || !m_filterProxy || !sourceIndex.isValid())
        return {};
    return m_navigationModel->mapFromSource(m_filterProxy->mapFromSource(sourceIndex));
}

void WorkspacePanelController::rebalanceNameColumn()
{
    if (!m_workspaceTree || !m_workspaceModel || m_nameColumnUserResized)
        return;
    QHeaderView *h = m_workspaceTree->header();

    m_adjustingColumns = true;
    h->resizeSection(0, kDefaultNameColumnWidth);
    m_adjustingColumns = false;
    m_columnsInitialized = true;
    scheduleColumnLayoutPersist();
}

void WorkspacePanelController::scheduleColumnLayoutPersist()
{
    if (m_columnPersistTimer)
        m_columnPersistTimer->start();
}

bool WorkspacePanelController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchBox && event->type() == QEvent::InputMethod)
        m_searchInputComposing = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
    if (watched == m_searchBox && event->type() == QEvent::FocusOut)
        m_searchInputComposing = false;
    // This filter is installed on qApp (only the clipboard/Delete interception
    // genuinely needs application scope), so it sees every event delivered
    // anywhere in the application, in every window. Everything below reacts to
    // input, geometry, or focus only; reject the rest before touching a single
    // member, and never let a paint, timer, or layout event reach the
    // focusWidget()/isAncestorOf() walks.
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::Resize:
    case QEvent::Leave:
        break;
    default:
        return QObject::eventFilter(watched, event);
    }

    // Must precede the Workspace-wide Escape handler. ShortcutOverride keeps
    // window actions from claiming these input-local keys before KeyPress.
    if (watched == m_searchBox && m_searchBox && m_searchBox->hasFocus()
        && m_panelMode == PanelMode::Workspace
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        if (m_searchInputComposing)
            return QObject::eventFilter(watched, event);
        if (handleSearchKey(static_cast<QKeyEvent*>(event)))
            return true;
    }

    if (event->type() == QEvent::KeyPress && m_searchBox &&
        !m_searchBox->text().isEmpty())
    {
        const QWidget *focus = QApplication::focusWidget();
        const bool workspaceFocus = focus && m_workspacePanel &&
                                    (focus == m_workspacePanel ||
                                     m_workspacePanel->isAncestorOf(focus));
        if (workspaceFocus && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            clearSearch();
            keyEvent->accept();
            return true;
        }
    }

    if (m_workspaceInteractionActive &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const QWidget *focus = QApplication::focusWidget();
        const bool renameEditorFocus = focus && m_workspaceTree &&
                                       focus->objectName() == QStringLiteral("workspaceRenameEditor") &&
                                       m_workspaceTree->isAncestorOf(focus);
        const bool searchFocus = focus && m_searchBox &&
                                 (focus == m_searchBox || m_searchBox->isAncestorOf(focus));
        if (!renameEditorFocus && !searchFocus && keyEvent->matches(QKeySequence::Copy))
        {
            keyEvent->accept();
            if (event->type() == QEvent::KeyPress)
                copySelected(false);
            return true;
        }
        if (!renameEditorFocus && !searchFocus && keyEvent->matches(QKeySequence::Paste))
        {
            keyEvent->accept();
            if (event->type() == QEvent::KeyPress)
                pasteSelection();
            return true;
        }
        if (!renameEditorFocus && !searchFocus && keyEvent->key() == Qt::Key_Delete && keyEvent->modifiers() == Qt::NoModifier)
        {
            // Without this, MainWindow's Edit > Delete QAction (a window-scoped
            // shortcut) claims the Delete key via Qt's ShortcutOverride
            // mechanism before it ever reaches the tree's keyPressEvent.
            keyEvent->accept();
            if (event->type() == QEvent::KeyPress)
                deleteSelected();
            return true;
        }
    }

    if (watched == m_searchBox && m_searchRow
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        m_searchRow->setProperty("focused", event->type() == QEvent::FocusIn);
        m_searchRow->style()->unpolish(m_searchRow);
        m_searchRow->style()->polish(m_searchRow);
    }

    const bool treeSurface = watched == m_workspaceTree ||
                             (m_workspaceTree && watched == m_workspaceTree->viewport());
    if (treeSurface && event->type() == QEvent::MouseButtonPress) {
        m_workspaceInteractionActive = true;
    } else if (treeSurface && event->type() == QEvent::FocusIn) {
        m_workspaceInteractionActive = true;
    } else if (treeSurface && event->type() == QEvent::FocusOut) {
        m_workspaceInteractionActive = false;
    }

    if ((watched == m_workspaceTree ||
         (m_workspaceTree && watched == m_workspaceTree->viewport())) &&
        event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape && m_searchBox &&
            !m_searchBox->text().isEmpty())
        {
            clearSearch();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Copy))
        {
            copySelected(false);
            keyEvent->accept();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Paste))
        {
            pasteSelection();
            keyEvent->accept();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete)
        {
            deleteSelected();
            keyEvent->accept();
            return true;
        }
    }
    else if (watched == m_searchBox && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape)
        {
            clearSearch();
            keyEvent->accept();
            return true;
        }
    }
    else if (watched == m_headerLabel && event->type() == QEvent::Resize)
    {
        refreshHeaderLabel();
    }
    else if ((watched == m_outlineReferenceWidget || watched == m_workspaceSplitter)
             && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        // Both resize events arrive during layout; defer until their global
        // coordinates describe the completed frame.
        scheduleOutlineReferenceLayout();
    }
    else if (watched == m_workspaceTree && event->type() == QEvent::Resize)
    {
        // Fit the default Name section only after Qt has propagated the first
        // real viewport size. Saved or user-controlled layouts stay fixed.
        if (m_workspaceModel && !m_columnsInitialized && !m_nameColumnUserResized)
            QTimer::singleShot(0, this, [this]() { rebalanceNameColumn(); });
    }
    else if (m_workspaceTree && watched == m_workspaceTree->viewport() && event->type() == QEvent::MouseMove)
    {
        // Mouse-move arrives continuously; only the cheap geometry update may
        // run unconditionally. Re-polishing the button, re-rendering its icon,
        // repainting the viewport happens only when the hovered row (or its
        // selected state) actually changed.
        auto *me = static_cast<QMouseEvent *>(event);
        const QString previousHoverPath = m_hoverPath;
        const bool previousRowSelected = m_hoverRowSelected;
        const QModelIndex proxyIndex = m_workspaceTree->indexAt(me->pos());
        if (proxyIndex.isValid() && m_workspaceModel && m_navigationModel && !m_navigationModel->isParentEntry(proxyIndex))
        {
            const QModelIndex srcIndex = sourceIndexForView(proxyIndex);
            m_hoverPath = m_workspaceModel->filePath(srcIndex);
            m_hoverRowSelected = m_workspaceTree->selectionModel() &&
                                 m_workspaceTree->selectionModel()->isSelected(proxyIndex);
            const bool hoverStateChanged = m_hoverPath != previousHoverPath ||
                                           m_hoverRowSelected != previousRowSelected;

            if (hoverStateChanged)
            {
                m_hoverRevealButton->setProperty("workspaceRowHovered", true);
                m_hoverRevealButton->setProperty("workspaceRowSelected", m_hoverRowSelected);
                m_hoverRevealButton->style()->unpolish(m_hoverRevealButton);
                m_hoverRevealButton->style()->polish(m_hoverRevealButton);

                updateHoverRevealIcon();

            }

            const QRect rowRect = m_workspaceTree->visualRect(proxyIndex.siblingAtColumn(0));
            const int x = rowRect.right() - m_hoverRevealButton->width() - kRevealButtonInset;
            m_hoverRevealButton->move(x, rowRect.top() + (rowRect.height() - m_hoverRevealButton->height()) / 2);
            m_hoverRevealButton->show();
            m_hoverRevealButton->raise();

            if (hoverStateChanged)
                m_workspaceTree->viewport()->update();
        }
        else if (!previousHoverPath.isEmpty() || m_hoverRevealButton->isVisible())
        {
            m_hoverPath.clear();
            m_hoverRowSelected = false;
            m_hoverRevealButton->hide();
            m_hoverRevealButton->setProperty("workspaceRowHovered", false);
            m_hoverRevealButton->setProperty("workspaceRowSelected", false);
            m_workspaceTree->viewport()->setToolTip(QString());
            m_workspaceTree->viewport()->update();
        }
    }
    else if (m_workspaceTree && event->type() == QEvent::Leave &&
             (watched == m_workspaceTree->viewport() || watched == m_hoverRevealButton))
    {
        // Moving the mouse from the viewport onto the hover button (a child
        // widget sitting on top of it) also delivers a Leave to the
        // viewport, and vice versa when moving off the button back onto the
        // viewport -- naively hiding on either Leave makes the button
        // disappear the instant the cursor reaches it. Only actually hide
        // once the cursor has left both widgets' combined area.
        QWidget *viewport = m_workspaceTree->viewport();
        const QPoint localPos = viewport->mapFromGlobal(QCursor::pos());
        if (!viewport->rect().contains(localPos))
        {
            m_hoverRevealButton->hide();
            m_hoverRevealButton->setProperty("workspaceRowHovered", false);
            m_hoverRevealButton->setProperty("workspaceRowSelected", false);
            viewport->setToolTip(QString());
            viewport->update();
        }
    }
    return QObject::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// File operations are implemented in WorkspacePanelControllerOperations.cpp.
