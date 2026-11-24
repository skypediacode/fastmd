#include "TerminalPanel.h"
#include "ThemePalette.h"

#include "AlignedMenu.h"
#include "FastScrollBar.h"
#include "IconHelper.h"
#include "Stylesheet.h"
#include "TerminalCloseAllPreferences.h"
#include "TerminalController.h"
#include "TerminalLinks.h"
#include "TerminalSession.h"
#include "TerminalView.h"
#include "ThemedSplitter.h"
#include "ThemedMessageBox.h"
#include "CommandShortcutRegistry.h"
#include "WorkspaceFileOps.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCheckBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QStyle>
#include <QPushButton>
#include <QKeySequence>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace {

// Material Icons codepoints.
constexpr ushort kIconAdd      = 0xE145; // add
constexpr ushort kIconChevron  = 0xE5C5; // arrow_drop_down
constexpr ushort kIconMore     = 0xE5D3; // more_horiz
constexpr ushort kIconMinimize = 0xE15B; // remove (a single horizontal bar)
constexpr ushort kIconMaximize = 0xE835; // check_box_outline_blank (an empty square)
constexpr ushort kIconRestore  = 0xE3E0; // filter_none (two offset squares)
constexpr ushort kIconDetach   = 0xE89E; // open_in_new
constexpr ushort kIconAttach   = 0xE890; // input -- return the panel to its host window
constexpr ushort kIconHide     = 0xE4F7; // keyboard_arrow_down -- collapse panel
constexpr ushort kIconClose    = 0xE5CD; // close
constexpr ushort kIconTerminal = 0xEB8E; // terminal (>_) -- fallback for a profile with no icon

constexpr int kHeaderHeight = 36;
constexpr int kButtonSize = 28;
constexpr int kButtonIconSize = 18;
// check_box_outline_blank / filter_none fill their em box edge to edge, unlike
// the line-art glyphs beside them, so at the shared size they read as visually
// larger than the rest of the header. Draw them two pixels smaller.
constexpr int kMaximizeIconSize = kButtonIconSize - 2;
// Used only when the user has not yet resized the session list. The splitter
// remains resizable and terminal/sessionListWidth continues to take priority.
constexpr int kSessionListWidth = 240;
constexpr int kSessionListMinWidth = 96;
constexpr int kSessionRowHeight = 24;
constexpr int kSessionIconSize = 14;

constexpr auto kSessionListWidthKey = "terminal/sessionListWidth";
constexpr auto kFontSizeKey = "terminal/fontSize";
constexpr auto kThemeModeKey = "terminal/themeMode";
constexpr auto kDetachedKey = "terminal/detached";

QColor iconColorFor(bool dark)
{
    return dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
}

// One row of the session list: icon + elided title + a right-aligned close
// button. A plain QWidget (no Q_OBJECT/moc) -- click-to-select and the close
// button are wired through std::function callbacks set by the caller.
class TerminalSessionRow : public QWidget {
public:
    explicit TerminalSessionRow(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(kSessionRowHeight);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 2, 0);
        layout->setSpacing(6);

        m_icon = new QLabel(this);
        m_icon->setFixedSize(kSessionIconSize, kSessionIconSize);
        m_icon->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_icon, 0, Qt::AlignVCenter);

        m_text = new QLabel(this);
        m_text->setObjectName(QStringLiteral("terminalSessionLabel"));
        m_text->setFixedHeight(kSessionRowHeight);
        m_text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_text->installEventFilter(this);
        layout->addWidget(m_text, 1, Qt::AlignVCenter);

        m_editor = new QLineEdit(this);
        m_editor->setObjectName(QStringLiteral("terminalSessionRenameEditor"));
        m_editor->setFixedHeight(kSessionRowHeight - 2);
        m_editor->setVisible(false);
        m_editor->installEventFilter(this);
        layout->addWidget(m_editor, 1, Qt::AlignVCenter);
        connect(m_editor, &QLineEdit::editingFinished, m_editor, [this] {
            finishRename(true);
        });

        m_closeButton = new QToolButton(this);
        m_closeButton->setObjectName(QStringLiteral("terminalSessionClose"));
        m_closeButton->setAutoRaise(true);
        m_closeButton->setFixedSize(kSessionRowHeight, kSessionRowHeight);
        m_closeButton->setIconSize(QSize(kSessionIconSize, kSessionIconSize));
        m_closeButton->setCursor(Qt::ArrowCursor);
        layout->addWidget(m_closeButton, 0, Qt::AlignVCenter);
    }

    void setSessionIcon(const QIcon& icon) { m_icon->setPixmap(icon.pixmap(kSessionIconSize, kSessionIconSize)); }
    void setCloseIcon(const QIcon& icon) { m_closeButton->setIcon(icon); }
    void setCloseTooltip(const QString& tip) { m_closeButton->setToolTip(tip); }

    void setLabelText(const QString& text)
    {
        m_fullText = text;
        applyElidedText();
    }

    void setRenameText(const QString& text) { m_renameText = text; }

    void startRename()
    {
        if (m_renaming)
            return;
        m_renaming = true;
        m_originalName = m_renameText;
        m_editor->setText(m_originalName);
        m_text->setVisible(false);
        m_editor->setVisible(true);
        m_editor->setFocus(Qt::OtherFocusReason);
        m_editor->selectAll();
    }

    std::function<void()> onRowClicked;
    std::function<void()> onCloseClicked;
    std::function<void(const QString&)> onRenameCommitted;

    void wireClose()
    {
        QObject::connect(m_closeButton, &QToolButton::clicked, m_closeButton, [this] {
            if (onCloseClicked)
                onCloseClicked();
        });
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onRowClicked)
            onRowClicked();
        QWidget::mousePressEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        applyElidedText();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_text && event->type() == QEvent::MouseButtonDblClick) {
            startRename();
            return true;
        }
        if (watched == m_editor && event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishRename(false);
                keyEvent->accept();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void finishRename(bool commit)
    {
        if (!m_renaming)
            return;
        m_renaming = false;
        const QString name = m_editor->text().trimmed();
        m_editor->setVisible(false);
        m_text->setVisible(true);
        if (commit && !name.isEmpty() && name != m_originalName && onRenameCommitted)
            onRenameCommitted(name);
    }

    void applyElidedText()
    {
        const QFontMetrics fm(m_text->fontMetrics());
        m_text->setText(fm.elidedText(m_fullText, Qt::ElideRight, m_text->width()));
    }

    QLabel* m_icon = nullptr;
    QLabel* m_text = nullptr;
    QLineEdit* m_editor = nullptr;
    QToolButton* m_closeButton = nullptr;
    QString m_fullText;
    QString m_renameText;
    QString m_originalName;
    bool m_renaming = false;
};

} // namespace

TerminalPanel::TerminalPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("terminalPanel"));
    // Docked, the stylesheet paints this panel as the Terminal card.
    setAttribute(Qt::WA_StyledBackground, true);
    m_appDark = palette().color(QPalette::Window).lightness() < 128;
    const int storedMode = QSettings().value(QString::fromLatin1(kThemeModeKey),
        static_cast<int>(ThemeMode::FollowApp)).toInt();
    m_themeMode = (storedMode == static_cast<int>(ThemeMode::Light)
                   || storedMode == static_cast<int>(ThemeMode::Dark))
        ? static_cast<ThemeMode>(storedMode)
        : ThemeMode::FollowApp;
    m_dark = effectiveDark();
    m_controller = new TerminalController(this);
    buildUi();
    applyEffectiveTheme();
}

TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    // Keeps the header and the native terminal view clear of the card's
    // rounded corners; setDetached() drops it once the panel is the window.
    root->setContentsMargins(ThemeCard::Inset, ThemeCard::Inset,
                             ThemeCard::Inset, ThemeCard::Inset);
    root->setSpacing(0);

    // --- header ------------------------------------------------------------
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("terminalHeader"));
    header->setFixedHeight(kHeaderHeight);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 0, 6, 0);
    headerLayout->setSpacing(4);

    auto* title = new QLabel(header);
    title->setObjectName(QStringLiteral("terminalSectionTitle"));
    // "Terminal" reads as the section title (uppercase, matching every other
    // panel's header style -- QSS has no text-transform, so this needs rich
    // text) while "FastMD" is the product name and stays mixed-case.
    title->setText(tr("<span style=\"text-transform:uppercase\">Terminal</span> • FastMD"));
    headerLayout->addWidget(title, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_status = new QLabel(header);
    m_status->setObjectName(QStringLiteral("terminalStatusLabel"));
    m_status->setVisible(false);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(m_status, 1, Qt::AlignLeft | Qt::AlignVCenter);
    headerLayout->addStretch(1);

    const auto addButton = [&](ushort code, const QString& tip) {
        auto* button = new QToolButton(header);
        button->setObjectName(QStringLiteral("terminalHeaderButton"));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
        button->setIcon(IconHelper::materialIcon(QChar(code), iconColorFor(m_dark), kButtonIconSize));
        button->setToolTip(tip);
        headerLayout->addWidget(button, 0, Qt::AlignVCenter);
        return button;
    };

    m_newButton = addButton(kIconAdd, tr("New Terminal"));
    m_profileButton = addButton(kIconChevron, tr("Choose a shell profile"));
    // The chevron is a hair narrower so the pair reads as one split control.
    m_profileButton->setFixedSize(18, kButtonSize);
    m_overflowButton = addButton(kIconMore, tr("Terminal actions"));
    m_hideButton = addButton(kIconHide, tr("Hide Terminal Panel"));
    m_detachButton = addButton(kIconDetach, tr("Detach Terminal"));
    // Window controls, in the order every Windows title bar uses them. They
    // only mean anything while the panel *is* the detached window's caption,
    // so they stay hidden until setDetached(true) says otherwise.
    m_minimizeButton = addButton(kIconMinimize, tr("Minimize"));
    m_maximizeButton = addButton(kIconMaximize, tr("Maximize"));
    m_maximizeButton->setIconSize(QSize(kMaximizeIconSize, kMaximizeIconSize));
    m_maximizeButton->setIcon(
        IconHelper::materialIcon(QChar(kIconMaximize), iconColorFor(m_dark), kMaximizeIconSize));
    m_minimizeButton->setVisible(false);
    m_maximizeButton->setVisible(false);
    m_closeButton = addButton(kIconClose, tr("Close All Terminals"));

    root->addWidget(header);

    // --- body: terminal | session list --------------------------------------
    m_splitter = new ThemedSplitter(Qt::Horizontal, ThemedSplitter::HandleStyle::Divider, 1, this);
    m_splitter->setObjectName(QStringLiteral("terminalSplitter"));
    // The splitter handle is the intentional pane separator. The QFrame
    // around the splitter itself is not part of the terminal chrome; the
    // native WebView2 child hides it over the main pane but leaves it visible
    // around the session list as a light right/bottom frame.
    m_splitter->setFrameShape(QFrame::NoFrame);
    m_splitter->setChildrenCollapsible(false);

    m_view = new TerminalView(m_splitter);
    QSettings settings;
    const QVariant storedFontSize = settings.value(QString::fromLatin1(kFontSizeKey));
    // 13 and 14 were the application's previous defaults. Migrate those
    // implicit values so existing installations receive the new 15px default.
    const int configuredFontSize = storedFontSize.isValid()
        && (storedFontSize.toInt() == 13 || storedFontSize.toInt() == 14)
        ? TerminalView::DefaultFontSize
        : storedFontSize.toInt();
    if (storedFontSize.isValid()
        && (storedFontSize.toInt() == 13 || storedFontSize.toInt() == 14))
        settings.setValue(QString::fromLatin1(kFontSizeKey), TerminalView::DefaultFontSize);
    m_fontSize = qBound(TerminalView::MinimumFontSize,
                         storedFontSize.isValid() ? configuredFontSize
                                                  : TerminalView::DefaultFontSize,
                         TerminalView::MaximumFontSize);
    m_view->setFontSize(m_fontSize);

    m_sessionList = new QListWidget(m_splitter);
    m_sessionList->setObjectName(QStringLiteral("terminalSessionList"));
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setUniformItemSizes(true);
    m_sessionList->setMouseTracking(true);
    m_sessionList->viewport()->setMouseTracking(true);
    m_sessionList->viewport()->setAttribute(Qt::WA_Hover, true);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionList->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_sessionList));
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setMinimumWidth(kSessionListMinWidth);
    m_sessionList->setIconSize(QSize(14, 14));

    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_sessionList);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    // Only remembered here; it cannot be turned into splitter sizes until the
    // splitter has its real width -- see applySessionListWidth().
    m_sessionListWidth = QSettings()
        .value(QString::fromLatin1(kSessionListWidthKey), kSessionListWidth).toInt();
    // The collapse floor is exactly what the old restore path left behind, so
    // a stored value sitting on it cannot be told apart from that bug; it is
    // also not a width anyone would choose deliberately.
    if (m_sessionListWidth <= kSessionListMinWidth)
        m_sessionListWidth = kSessionListWidth;
    root->addWidget(m_splitter, 1);

    // --- wiring -------------------------------------------------------------
    // Persist the list width when the user actually drags it. Reading it back
    // at destruction instead would catch whatever width a *hidden* list
    // happened to hold -- the list is hidden whenever there is a single
    // session, and the splitter's layout skips hidden children -- and quietly
    // replace the user's choice with it.
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!m_sessionList || !m_sessionList->isVisible())
            return;
        m_sessionListWidth = m_splitter->sizes().value(1, m_sessionListWidth);
        QSettings().setValue(QString::fromLatin1(kSessionListWidthKey), m_sessionListWidth);
    });

    connect(m_newButton, &QToolButton::clicked, this, &TerminalPanel::newTerminal);
    connect(m_profileButton, &QToolButton::clicked, this, &TerminalPanel::openProfileMenu);
    connect(m_overflowButton, &QToolButton::clicked, this, &TerminalPanel::openOverflowMenu);
    connect(m_detachButton, &QToolButton::clicked, this, [this] {
        if (m_detached)
            emit attachRequested();
        else
            emit detachRequested();
    });
    connect(m_minimizeButton, &QToolButton::clicked, this, &TerminalPanel::minimizeRequested);
    connect(m_maximizeButton, &QToolButton::clicked, this,
            &TerminalPanel::maximizeRestoreRequested);
    connect(m_hideButton, &QToolButton::clicked, this, &TerminalPanel::hideRequested);
    connect(m_closeButton, &QToolButton::clicked, this, &TerminalPanel::closeAllTerminals);

    m_increaseFontSizeAction = new QAction(tr("Increase Font Size"), this);
    m_increaseFontSizeAction->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::TerminalIncreaseFont));
    m_increaseFontSizeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_increaseFontSizeAction, &QAction::triggered, this,
            [this] { setFontSize(m_fontSize + 1); });
    addAction(m_increaseFontSizeAction);

    m_decreaseFontSizeAction = new QAction(tr("Decrease Font Size"), this);
    m_decreaseFontSizeAction->setShortcuts(CommandShortcutRegistry::defaultShortcuts(CommandId::TerminalDecreaseFont));
    m_decreaseFontSizeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_decreaseFontSizeAction, &QAction::triggered, this,
            [this] { setFontSize(m_fontSize - 1); });
    addAction(m_decreaseFontSizeAction);

    m_resetFontSizeAction = new QAction(tr("Reset Font Size"), this);
    m_resetFontSizeAction->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::TerminalResetFont));
    m_resetFontSizeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_resetFontSizeAction, &QAction::triggered, this,
            [this] { setFontSize(TerminalView::DefaultFontSize); });
    addAction(m_resetFontSizeAction);

    auto* renameSessionAction = new QAction(tr("Rename Terminal"), this);
    renameSessionAction->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::TerminalRenameSession));
    renameSessionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(renameSessionAction, &QAction::triggered,
            this, &TerminalPanel::beginRenameActiveSession);
    addAction(renameSessionAction);

    // A detached TerminalPanel is its own top-level window, so MainWindow's
    // window shortcut cannot see a key pressed in its session list/header.
    auto* workspaceSearchAction = new QAction(tr("Focus File Search"), this);
    workspaceSearchAction->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::ViewFocusFileSearch));
    workspaceSearchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(workspaceSearchAction, &QAction::triggered,
            this, &TerminalPanel::workspaceSearchRequested);
    addAction(workspaceSearchAction);

    for (const bool replace : {false, true}) {
        auto* findInFilesAction = new QAction(replace ? tr("Replace in Files") : tr("Find in Files"), this);
        findInFilesAction->setShortcut(CommandShortcutRegistry::defaultShortcut(
            replace ? CommandId::EditReplaceInFiles : CommandId::EditFindInFiles));
        findInFilesAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(findInFilesAction, &QAction::triggered, this,
                [this, replace] { emit findInFilesRequested(replace); });
        addAction(findInFilesAction);
    }

    connect(m_sessionList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
        if (!current)
            return;
        const int id = current->data(Qt::UserRole).toInt();
        m_controller->setActiveSession(id);
    });

    connect(m_controller, &TerminalController::activeSessionChanged, this, [this](int id) {
        if (id >= 0 && m_view)
            m_view->showTerminal(id);
        refreshSessionList();
    });
    connect(m_controller, &TerminalController::sessionsChanged,
            this, &TerminalPanel::refreshSessionList);
    connect(m_controller, &TerminalController::sessionOutput, this,
            [this](int id, const QByteArray& data) {
        if (m_view) m_view->writeOutput(id, data);
    });
    connect(m_controller, &TerminalController::sessionFinished, this,
            [this](int id, int) {
        // Match VS Code: when the shell exits (for example after the user
        // types `exit`), remove that terminal tab immediately.  Keeping an
        // exited shell around makes it look reusable even though it cannot
        // accept input anymore.
        //
        // But not from *inside* this signal. It is emitted from the session's
        // process-exit notifier, and closing the session destroys that
        // notifier and the ConPtySession that is emitting -- deleting a sender
        // from within its own emission, inside an event-dispatcher callback,
        // and joining the reader/writer threads there too. Unwind first; the
        // tab is gone on the very next event-loop turn either way.
        QTimer::singleShot(0, this, [this, id] { closeSession(id); });
    });

    connect(m_view, &TerminalView::shellReady, this, [this] {
        // The queued work moves to the drain rather than running here: this
        // handler fires while the page is still settling, so start the initial
        // shell from a later event-loop turn.
        if (m_initialSessionPending || !m_pendingProfiles.isEmpty())
            scheduleSpawnDrain();
        else if (std::exchange(m_focusWhenReady, false))
            m_view->focusTerminal();
    });
    connect(m_view, &TerminalView::inputReady, this, [this](int id, const QByteArray& utf8) {
        if (TerminalSession* session = m_controller->sessionById(id))
            session->write(utf8);
    });
    connect(m_view, &TerminalView::resized, this, [this](int id, int columns, int rows) {
        if (TerminalSession* session = m_controller->sessionById(id))
            session->resize(columns, rows);
    });
    connect(m_view, &TerminalView::selectionCopyRequested, this, [](const QString& text) {
        if (!text.isEmpty())
            QApplication::clipboard()->setText(text);
    });
    connect(m_view, &TerminalView::linkActivated, this,
            [this](int id, const QString& text, const QString& reportedCwd) {
        if (const TerminalSession* session = m_controller->sessionById(id))
            followLink(text, reportedCwd, session->workingDirectory());
    });
    connect(m_view, &TerminalView::pasteRequested, this, [this](int id) {
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty() && m_view)
            m_view->pasteText(id, text);
    });
    connect(m_view, &TerminalView::commandRequested, this, [this](TerminalView::Command command) {
        switch (command) {
        case TerminalView::Command::ToggleTerminal:  emit toggleRequested(); break;
        case TerminalView::Command::CommandPalette:  emit commandPaletteRequested(); break;
        case TerminalView::Command::ReadingMode:     emit readingModeRequested(); break;
        case TerminalView::Command::NotepadMode:     emit notepadModeRequested(); break;
        case TerminalView::Command::IncreaseFontSize: setFontSize(m_fontSize + 1); break;
        case TerminalView::Command::DecreaseFontSize: setFontSize(m_fontSize - 1); break;
        case TerminalView::Command::ResetFontSize:    setFontSize(TerminalView::DefaultFontSize); break;
        case TerminalView::Command::RenameSession:     beginRenameActiveSession(); break;
        case TerminalView::Command::WorkspaceSearch:   emit workspaceSearchRequested(); break;
        case TerminalView::Command::FindInFiles:       emit findInFilesRequested(false); break;
        case TerminalView::Command::ReplaceInFiles:    emit findInFilesRequested(true); break;
        }
    });

    refreshSessionList();
}

void TerminalPanel::setWorkingDirectoryProvider(std::function<QString()> provider)
{
    m_workingDirectoryProvider = std::move(provider);
}

QString TerminalPanel::resolveWorkingDirectory(const QString& directoryOverride) const
{
    QString directory = !directoryOverride.isEmpty()
        ? directoryOverride
        : (m_workingDirectoryProvider ? m_workingDirectoryProvider() : QString());
    if (directory.isEmpty() || !QDir(directory).exists())
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (directory.isEmpty() || !QDir(directory).exists())
        directory = QDir::homePath();
    return directory;
}

bool TerminalPanel::hasSessions() const
{
    return !m_controller->sessions().isEmpty();
}

bool TerminalPanel::ensureInitialSession(bool takeFocus)
{
    // Only ever widens: a restore must not cancel a focus the user asked for
    // by opening the panel, and an explicit open still focuses even if a
    // restore got there first.
    m_focusWhenReady = m_focusWhenReady || takeFocus;
    if (hasSessions())
        return true;
    if (!m_view->isReady()) {
        m_initialSessionPending = true;
        m_view->ensureInitialized();
        return true;
    }
    const TerminalProfile profile = m_controller->defaultProfile();
    if (!profile.isValid()) {
        showStatus(tr("No supported shell was found on this system."), true);
        // The request is over; leaving the flag set would hand focus to the
        // terminal on whatever unrelated spawn happens to succeed next.
        m_focusWhenReady = false;
        return false;
    }
    m_initialSessionPending = true;
    scheduleSpawnDrain();
    return true;
}

void TerminalPanel::scheduleSpawnDrain()
{
    if (m_spawnScheduled)
        return;
    m_spawnScheduled = true;
    QTimer::singleShot(0, this, &TerminalPanel::drainSpawnQueue);
}

void TerminalPanel::drainSpawnQueue()
{
    m_spawnScheduled = false;
    if (!m_view || !m_view->isReady())
        return;

    if (m_initialSessionPending) {
        m_initialSessionPending = false;
        newTerminalWithProfile(m_controller->defaultProfile());
    } else if (!m_pendingProfiles.isEmpty()) {
        const PendingTerminal pending = m_pendingProfiles.takeFirst();
        newTerminalWithProfile(pending.profile, pending.directoryOverride);
    }

    // Focus as soon as there is a live shell to focus, not once the whole
    // restore has finished -- an explicit open must not wait on a batch.
    if (hasSessions() && std::exchange(m_focusWhenReady, false))
        m_view->focusTerminal();

    if (m_initialSessionPending || !m_pendingProfiles.isEmpty())
        scheduleSpawnDrain();
}

void TerminalPanel::newTerminal()
{
    // Explicitly asked for, so it takes focus once xterm can accept it --
    // even if the request has to wait for the page to finish loading.
    m_focusWhenReady = true;
    newTerminalWithProfile(m_controller->defaultProfile());
}

void TerminalPanel::openNewTerminalAt(const QString& directory)
{
    m_focusWhenReady = true;
    // A caller reaching here has normally just made the panel visible, and
    // that path calls ensureInitialSession() -- which, with no sessions yet,
    // queues a *default*-directory shell. This request supersedes it: both
    // would otherwise be drained and the user would get two tabs, one of them
    // in the wrong folder, on the first "Open Folder in Terminal".
    m_initialSessionPending = false;
    newTerminalWithProfile(m_controller->defaultProfile(), directory);
}

void TerminalPanel::newTerminalWithProfile(const TerminalProfile& profile,
                                            const QString& directoryOverride)
{
    if (!profile.isValid()) {
        showStatus(tr("No supported shell was found on this system."), true);
        m_focusWhenReady = false;
        return;
    }
    if (!m_view->isReady()) {
        m_pendingProfiles.append({ profile, directoryOverride });
        m_view->ensureInitialized();
        return;
    }

    // 80x24 is only a seed; the page reports its real geometry as soon as the
    // fit addon measures the viewport, and that drives ResizePseudoConsole.
    QString error;
    TerminalSession* session = m_controller->createSession(
        profile, resolveWorkingDirectory(directoryOverride), 80, 24, &error);
    if (!session) {
        showStatus(error.isEmpty() ? tr("The terminal could not be started.") : error, true);
        m_focusWhenReady = false;
        return;
    }

    showStatus(QString(), false);
    m_view->createTerminal(session->id());
    m_view->showTerminal(session->id());
    m_view->focusTerminal();
}

void TerminalPanel::closeSession(int id)
{
    if (m_view)
        m_view->destroyTerminal(id);
    m_controller->closeSession(id);
    if (!hasSessions())
        emit closeRequested();
}

void TerminalPanel::closeAllTerminals()
{
    // Take the snapshot before deciding whether this is a multi-session close
    // and reuse it below, where each close mutates the controller's list.
    const QList<TerminalSession*> sessions = m_controller->sessions();
    QSettings settings;
    if (TerminalCloseAllPreferences::shouldConfirmCloseAll(sessions.size(), settings)) {
        // Parented to the panel, so it is centred on and modal over whichever
        // window currently holds it: the main window or the detached one.
        ThemedMessageBox box(this);
        box.setDarkTheme(dialogThemeDark(m_detached, m_dark, m_appDark));
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Close All Terminals"));
        box.setText(tr("All terminal tabs will be terminated. Close them?"));
        auto* closeAll = box.addButton(tr("Close All"), QMessageBox::DestructiveRole);
        auto* cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancel);
        bindDialogKey(&box, closeAll, Qt::Key_A);
        bindDialogKey(&box, cancel, Qt::Key_C);
        auto* dontShowAgain = new QCheckBox(tr("Don't show this again"), &box);
        box.setCheckBox(dontShowAgain);
        box.polishContentLayout();
        box.exec();
        if (box.clickedButton() != closeAll)
            return;
        if (dontShowAgain->isChecked())
            TerminalCloseAllPreferences::setConfirmationEnabled(settings, false);
    }

    // Cancel deferred startup/spawn work first, so a hidden panel cannot
    // recreate a shell after its sessions were deliberately closed.
    m_initialSessionPending = false;
    m_pendingProfiles.clear();
    m_focusWhenReady = false;

    // Reuse the same controller close path as an individual session.
    for (TerminalSession* session : sessions) {
        if (m_view)
            m_view->destroyTerminal(session->id());
        m_controller->closeSession(session->id());
    }
    emit closeRequested();
}

void TerminalPanel::openProfileMenu()
{
    // Qt hides a popup before the anchor button sees the click that dismissed
    // it; without this the same click would close and immediately reopen.
    if (m_profileMenuDismissedByButton) {
        m_profileMenuDismissedByButton = false;
        return;
    }

    AlignedMenu menu(this);
    menu.setShowAnimationStyle(AlignedMenu::DropDown);
    menu.setStyleSheet(AppStyle::terminalMenu(m_dark));

    QAction* newDefault = menu.addAction(tr("New Terminal"));
    menu.addSeparator();

    const QList<TerminalProfile> profiles = m_controller->profiles();
    const QString defaultId = m_controller->defaultProfile().id;
    QHash<QAction*, QString> profileActions;
    for (const TerminalProfile& profile : profiles) {
        const QString label = profile.id == defaultId
            ? tr("%1 (Default)").arg(profile.name)
            : profile.name;
        QAction* action = menu.addAction(label);
        action->setIcon(IconHelper::materialIcon(
            QChar(profile.iconCode ? profile.iconCode : kIconTerminal),
            iconColorFor(m_dark), kButtonIconSize));
        profileActions.insert(action, profile.id);
    }

    menu.addSeparator();
    QAction* selectDefault = menu.addAction(tr("Select Default Profile..."));
    selectDefault->setEnabled(!profiles.isEmpty());

    connect(&menu, &QMenu::aboutToHide, this, [this] {
        if (m_profileButton && QApplication::mouseButtons() & Qt::LeftButton) {
            const QRect anchor(m_profileButton->mapToGlobal(QPoint(0, 0)), m_profileButton->size());
            if (anchor.contains(QCursor::pos()))
                m_profileMenuDismissedByButton = true;
        }
    });

    const QPoint origin = m_profileButton->mapToGlobal(QPoint(0, 0))
                          + QPoint(0, m_profileButton->height());
    QAction* chosen = menu.exec(origin);
    if (!chosen)
        return;

    if (chosen == newDefault) {
        newTerminal();
        return;
    }
    if (chosen == selectDefault) {
        AlignedMenu picker(this);
        picker.setStyleSheet(AppStyle::terminalMenu(m_dark));
        QHash<QAction*, QString> pickerActions;
        for (const TerminalProfile& profile : profiles) {
            QAction* action = picker.addAction(profile.name);
            action->setCheckable(true);
            action->setChecked(profile.id == defaultId);
            pickerActions.insert(action, profile.id);
        }
        if (QAction* picked = picker.exec(origin))
            setDefaultProfile(pickerActions.value(picked));
        return;
    }
    if (profileActions.contains(chosen))
        openNewTerminalWithProfile(profileActions.value(chosen));
}

void TerminalPanel::openOverflowMenu()
{
    AlignedMenu menu(this);
    menu.setShowAnimationStyle(AlignedMenu::DropDown);
    menu.setStyleSheet(AppStyle::terminalMenu(m_dark));

    const bool hasActive = hasActiveSession();

    QAction* copy = menu.addAction(tr("Copy"));
    QAction* paste = menu.addAction(tr("Paste"));
    QAction* selectAllAction = menu.addAction(tr("Select All"));
    menu.addSeparator();
    QAction* clear = menu.addAction(tr("Clear"));
    menu.addSeparator();
    QMenu* fontSizeMenu = menu.addMenu(tr("Font Size (%1 px)").arg(m_fontSize));
    fontSizeMenu->addAction(m_increaseFontSizeAction);
    fontSizeMenu->addAction(m_decreaseFontSizeAction);
    fontSizeMenu->addAction(m_resetFontSizeAction);

    QMenu* themeMenu = menu.addMenu(tr("Theme"));
    QAction* followApp = themeMenu->addAction(tr("Follow App Theme"));
    QAction* lightTheme = themeMenu->addAction(tr("Light"));
    QAction* darkTheme = themeMenu->addAction(tr("Dark"));
    for (QAction* action : { followApp, lightTheme, darkTheme })
        action->setCheckable(true);
    followApp->setChecked(m_themeMode == ThemeMode::FollowApp);
    lightTheme->setChecked(m_themeMode == ThemeMode::Light);
    darkTheme->setChecked(m_themeMode == ThemeMode::Dark);

    menu.addSeparator();
    QAction* kill = menu.addAction(tr("Close Terminal"));
    for (QAction* action : { copy, paste, selectAllAction, clear, kill })
        action->setEnabled(hasActive);

    const QPoint origin = m_overflowButton->mapToGlobal(QPoint(0, 0))
                          + QPoint(0, m_overflowButton->height());
    QAction* chosen = menu.exec(origin);
    if (!chosen)
        return;

    if (chosen == followApp)      { setThemeMode(ThemeMode::FollowApp); return; }
    if (chosen == lightTheme)     { setThemeMode(ThemeMode::Light); return; }
    if (chosen == darkTheme)      { setThemeMode(ThemeMode::Dark); return; }

    if (chosen == copy)            copySelection();
    else if (chosen == paste)      pasteClipboard();
    else if (chosen == selectAllAction) selectAll();
    else if (chosen == clear)      clearTerminal();
    else if (chosen == kill)       closeActiveSession();
}

void TerminalPanel::openNewTerminalWithProfile(const QString& profileId)
{
    m_focusWhenReady = true;
    m_initialSessionPending = false;
    newTerminalWithProfile(profileId.isEmpty()
        ? m_controller->defaultProfile()
        : TerminalProfiles::findById(m_controller->profiles(), profileId));
}

QString TerminalPanel::defaultProfileId() const
{
    return m_controller->defaultProfile().id;
}

void TerminalPanel::setDefaultProfile(const QString& profileId)
{
    m_controller->setDefaultProfileId(profileId);
    const TerminalProfile profile = m_controller->defaultProfile();
    showStatus(tr("Default profile: %1").arg(profile.name), false);
}

bool TerminalPanel::hasActiveSession() const
{
    return m_controller->activeSession() != nullptr;
}

int TerminalPanel::sessionCount() const
{
    return m_controller->sessions().size();
}

void TerminalPanel::closeActiveSession()
{
    if (hasActiveSession())
        closeSession(m_controller->activeSessionId());
}

void TerminalPanel::activateAdjacentSession(int delta)
{
    const QList<TerminalSession*>& sessions = m_controller->sessions();
    if (sessions.size() < 2)
        return;
    int index = sessions.indexOf(m_controller->activeSession());
    if (index < 0)
        index = 0;
    const int count = sessions.size();
    index = ((index + delta) % count + count) % count;
    m_controller->setActiveSession(sessions.at(index)->id());
}

void TerminalPanel::copySelection()
{
    if (m_view && hasActiveSession())
        m_view->copySelection(m_controller->activeSessionId());
}

void TerminalPanel::pasteClipboard()
{
    if (m_view && hasActiveSession())
        m_view->pasteText(m_controller->activeSessionId(), QApplication::clipboard()->text());
}

void TerminalPanel::selectAll()
{
    if (m_view && hasActiveSession())
        m_view->selectAll(m_controller->activeSessionId());
}

void TerminalPanel::clearTerminal()
{
    if (m_view && hasActiveSession())
        m_view->clearTerminal(m_controller->activeSessionId());
}

void TerminalPanel::stepFontSize(int delta)
{
    setFontSize(m_fontSize + delta);
}

void TerminalPanel::resetFontSize()
{
    setFontSize(TerminalView::DefaultFontSize);
}

void TerminalPanel::setFontSize(int pixelSize)
{
    const int bounded = qBound(TerminalView::MinimumFontSize, pixelSize,
                               TerminalView::MaximumFontSize);
    if (bounded == m_fontSize)
        return;
    m_fontSize = bounded;
    QSettings().setValue(QString::fromLatin1(kFontSizeKey), m_fontSize);
    if (m_view)
        m_view->setFontSize(m_fontSize);
}

void TerminalPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_sessionListWidthApplied)
        return;
    if (applySessionListWidth())
        return;
    // Not laid out yet (or the list is still hidden behind a single session).
    // One event-loop turn later the splitter has its real width.
    QTimer::singleShot(0, this, [this] { applySessionListWidth(); });
}

// QSplitter scales any setSizes() that does not add up to its *current* width,
// and it keeps the scaled result. Sizing the list before the splitter had been
// laid out therefore squashed it to its minimum and pinned it there, which is
// why widening the list never survived -- and why the stored width appeared to
// have no effect at all. Always size against the live width instead.
bool TerminalPanel::applySessionListWidth()
{
    if (!m_splitter || !m_sessionList || !m_sessionList->isVisible())
        return false;
    const int total = m_splitter->width();
    if (total < kSessionListMinWidth * 2)
        return false;

    const int width = qBound(kSessionListMinWidth, m_sessionListWidth,
                             qMax(kSessionListMinWidth, total / 2));
    m_splitter->setSizes({ total - m_splitter->handleWidth() - width, width });
    m_sessionListWidthApplied = true;
    return true;
}

void TerminalPanel::refreshSessionList()
{
    if (!m_sessionList)
        return;

    const QSignalBlocker blocker(m_sessionList);
    m_sessionList->clear();

    for (TerminalSession* session : m_controller->sessions()) {
        const int id = session->id();
        auto* item = new QListWidgetItem(m_sessionList);
        QString label = session->title();
        if (session->hasExited())
            label += tr(" (exited)");
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, kSessionRowHeight));
        item->setToolTip(tr("%1\nStarted in %2")
                             .arg(session->profile().name,
                                  QDir::toNativeSeparators(session->workingDirectory())));

        auto* row = new TerminalSessionRow(m_sessionList);
        row->setSessionIcon(rowIcon(session->profile().iconCode
                                        ? session->profile().iconCode
                                        : kIconTerminal));
        row->setLabelText(label);
        row->setRenameText(session->title());
        row->setCloseIcon(rowIcon(kIconClose));
        row->setCloseTooltip(tr("Close Terminal"));
        row->onRowClicked = [this, item] { m_sessionList->setCurrentItem(item); };
        row->onCloseClicked = [this, id] { closeSession(id); };
        row->onRenameCommitted = [this, id](const QString& name) {
            m_controller->renameSession(id, name);
        };
        row->wireClose();
        m_sessionList->setItemWidget(item, row);

        if (id == m_controller->activeSessionId())
            m_sessionList->setCurrentItem(item);
    }

    // With one session the list is noise; VS Code hides it too.
    const bool wasVisible = m_sessionList->isVisible();
    m_sessionList->setVisible(m_controller->sessions().size() > 1);
    // A child the splitter skipped while it was hidden comes back with
    // whatever size the surrounding layout handed it, so restate the width the
    // user chose every time the list reappears.
    if (!wasVisible && m_sessionList->isVisible())
        applySessionListWidth();
}

void TerminalPanel::beginRenameActiveSession()
{
    if (!m_sessionList || !m_controller)
        return;
    const int activeId = m_controller->activeSessionId();
    for (int row = 0; row < m_sessionList->count(); ++row) {
        QListWidgetItem* item = m_sessionList->item(row);
        if (item && item->data(Qt::UserRole).toInt() == activeId) {
            m_sessionList->setCurrentItem(item);
            if (auto* sessionRow = dynamic_cast<TerminalSessionRow*>(m_sessionList->itemWidget(item)))
                sessionRow->startRename();
            return;
        }
    }
}

QIcon TerminalPanel::rowIcon(ushort code)
{
    const auto cached = m_rowIcons.constFind(code);
    if (cached != m_rowIcons.constEnd())
        return cached.value();
    const QIcon icon =
        IconHelper::materialIcon(QChar(code), iconColorFor(m_dark), kSessionIconSize);
    m_rowIcons.insert(code, icon);
    return icon;
}

void TerminalPanel::updateIcons()
{
    const QColor color = iconColorFor(m_dark);
    const auto setIcon = [&](QToolButton* button, ushort code, int size = kButtonIconSize) {
        if (button)
            button->setIcon(IconHelper::materialIcon(QChar(code), color, size));
    };
    setIcon(m_newButton, kIconAdd);
    setIcon(m_profileButton, kIconChevron);
    setIcon(m_overflowButton, kIconMore);
    setIcon(m_detachButton, m_detached ? kIconAttach : kIconDetach);
    setIcon(m_minimizeButton, kIconMinimize);
    setIcon(m_maximizeButton, m_maximized ? kIconRestore : kIconMaximize, kMaximizeIconSize);
    setIcon(m_hideButton, kIconHide);
    setIcon(m_closeButton, kIconClose);
}

int TerminalPanel::headerHeight() const
{
    return kHeaderHeight;
}

void TerminalPanel::setDetached(bool detached)
{
    if (!m_detachButton)
        return;
    m_detached = detached;
    QSettings().setValue(QString::fromLatin1(kDetachedKey), detached);
    const int inset = detached ? 0 : ThemeCard::Inset;
    if (layout())
        layout()->setContentsMargins(inset, inset, inset, inset);
    setProperty("detached", detached);
    style()->unpolish(this);
    style()->polish(this);
    m_detachButton->setEnabled(true);
    m_detachButton->setToolTip(detached ? tr("Attach Terminal") : tr("Detach Terminal"));
    if (m_minimizeButton)
        m_minimizeButton->setVisible(detached);
    if (m_maximizeButton)
        m_maximizeButton->setVisible(detached);
    // A docked panel is never "maximized"; reattaching while the detached
    // window happened to be maximized would otherwise leave the restore glyph
    // cached for the next detach.
    if (!detached)
        m_maximized = false;
    updateIcons();
}

void TerminalPanel::setMaximized(bool maximized)
{
    if (m_maximized == maximized)
        return;
    m_maximized = maximized;
    if (m_maximizeButton)
        m_maximizeButton->setToolTip(maximized ? tr("Restore Down") : tr("Maximize"));
    updateIcons();
}

void TerminalPanel::setDarkMode(bool appDark)
{
    if (m_appDark == appDark)
        return;
    m_appDark = appDark;
    if (m_themeMode == ThemeMode::FollowApp)
        applyEffectiveTheme();
}

bool TerminalPanel::effectiveDark() const
{
    switch (m_themeMode) {
    case ThemeMode::Light: return false;
    case ThemeMode::Dark:  return true;
    case ThemeMode::FollowApp: default: return m_appDark;
    }
}

void TerminalPanel::setThemeMode(ThemeMode mode)
{
    if (m_themeMode == mode)
        return;
    m_themeMode = mode;
    QSettings().setValue(QString::fromLatin1(kThemeModeKey), static_cast<int>(mode));
    applyEffectiveTheme();
}

void TerminalPanel::applyEffectiveTheme()
{
    m_dark = effectiveDark();
    // While the terminal follows the app theme, the app-wide stylesheet
    // already paints #terminalPanel/#terminalHeader/etc. in m_dark's colors,
    // so clearing any widget-level override lets it cascade normally. Forcing
    // Light/Dark against the app's own theme needs an explicit override here
    // -- otherwise the terminal's header/session-list chrome would silently
    // follow the app theme after all, contradicting the selected mode.
    setStyleSheet(m_themeMode == ThemeMode::FollowApp
        ? QString()
        : AppStyle::terminalPanel(m_dark));
    if (m_view)
        m_view->setDarkMode(m_dark);
    if (m_splitter)
        m_splitter->setDarkMode(m_dark);
    // Cached row glyphs carry the old theme's color.
    m_rowIcons.clear();
    updateIcons();
    refreshSessionList();
}

void TerminalPanel::focusTerminal()
{
    if (m_view) {
        m_view->ensureInitialized();
        m_view->focusTerminal();
    }
}

void TerminalPanel::followLink(const QString& text, const QString& reportedCwd,
                               const QString& startDirectory)
{
    // Resolution probes the filesystem, and a path on a disconnected mapped
    // drive can stall for seconds; keep that off the UI thread. The watcher is
    // a child of the panel, so a result that lands after the panel is gone is
    // simply dropped.
    auto* watcher = new QFutureWatcher<TerminalLinks::Target>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, text] {
        const TerminalLinks::Target target = watcher->result();
        watcher->deleteLater();
        switch (target.kind) {
        case TerminalLinks::Target::Kind::Web:
            QDesktopServices::openUrl(target.url);
            break;
        case TerminalLinks::Target::Kind::Directory:
            WorkspaceFileOps::revealInExplorer(target.path, true);
            break;
        case TerminalLinks::Target::Kind::File:
            emit fileLinkActivated(target.path, target.line, target.column);
            break;
        case TerminalLinks::Target::Kind::NotFound:
        case TerminalLinks::Target::Kind::Invalid: {
            constexpr int kShownChars = 120;
            constexpr int kStatusMs = 4000;
            // A missing file names where it was looked for, which is what
            // tells a typo apart from a link resolved in the wrong directory.
            const bool missing = target.kind == TerminalLinks::Target::Kind::NotFound;
            const QString shown = missing ? QDir::toNativeSeparators(target.path) : text;
            const QString elided = shown.size() > kShownChars
                ? QChar(0x2026) + shown.right(kShownChars) : shown;
            const QString message = missing ? tr("File not found: %1").arg(elided)
                                            : tr("Cannot open link: %1").arg(elided);
            showStatus(message, false);
            QTimer::singleShot(kStatusMs, this, [this, message] {
                if (m_status && m_status->text() == message)
                    showStatus(QString(), false);
            });
            break;
        }
        }
    });
    watcher->setFuture(QtConcurrent::run([text, reportedCwd, startDirectory] {
        return TerminalLinks::resolve(text, reportedCwd, startDirectory);
    }));
}

void TerminalPanel::showStatus(const QString& message, bool isError)
{
    if (!m_status)
        return;
    m_status->setText(message);
    m_status->setVisible(!message.isEmpty());
    m_status->setProperty("terminalError", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}
