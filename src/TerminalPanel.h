#pragma once

#include "TerminalProfiles.h"

// The bottom Terminal panel: a header strip, the xterm.js view, and the
// right-side vertical session list.
//
// Owns no shell logic of its own -- TerminalController owns the sessions and
// TerminalView owns the page. The panel is the wiring plus the chrome, the same
// division SourceControlPanel keeps.

#include <QHash>
#include <QIcon>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class QAction;
class QToolButton;
class TerminalController;
class TerminalView;
class ThemedSplitter;

class TerminalPanel : public QWidget {
    Q_OBJECT

public:
    // Terminal's own appearance setting, independent of the app theme. Values
    // are persisted as-is, so do not renumber existing entries.
    enum class ThemeMode {
        FollowApp = 0,
        Light = 1,
        Dark = 2,
    };

    explicit TerminalPanel(QWidget* parent = nullptr);
    ~TerminalPanel() override;

    // The application theme changed; only actually affects the terminal's
    // appearance while ThemeMode::FollowApp is selected.
    void setDarkMode(bool appDark);

    // Theme for dialogs the Terminal opens. Docked, the panel is part of the
    // main window, so its dialogs follow the app theme even when the terminal
    // itself is pinned Light/Dark; detached, they belong to the Terminal
    // window and follow its effective theme.
    static bool dialogThemeDark(bool detached, bool terminalDark, bool appDark)
    {
        return detached ? terminalDark : appDark;
    }

    // Supplies the directory new terminals start in. Called on every open, so a
    // workspace change is picked up by the *next* terminal without touching any
    // session that is already running.
    void setWorkingDirectoryProvider(std::function<QString()> provider);

    // Starts one fresh terminal tab if none exists yet. Returns false when no
    // shell could be started, in which case the panel shows why. The first
    // shell is deferred until xterm is ready, so a true return only means the
    // attempt started.
    //
    // `takeFocus` must be false when the panel is being restored at startup
    // rather than opened by the user: focusing moves *native* focus to the
    // WebView2 child, and the terminal page finishes loading well after the
    // main window is interactive -- so a restore that focuses steals
    // keystrokes the user has already started typing into the editor.
    bool ensureInitialSession(bool takeFocus = true);

    // Creates a new terminal tab whose working directory is exactly
    // `directory`, bypassing the working-directory provider for this one
    // tab. Used by "Open Folder in Terminal" actions, which must land in the
    // folder the action was invoked on rather than the current workspace.
    void openNewTerminalAt(const QString& directory);

    bool hasSessions() const;
    void focusTerminal();

    // Entry points for the Command Palette. Each is the handler behind the
    // matching header or overflow-menu control; the session-scoped ones do
    // nothing while there is no active session.
    // Opens one new tab with `profileId` (empty means the default profile),
    // superseding a pending initial shell the way openNewTerminalAt() does.
    void openNewTerminalWithProfile(const QString& profileId);
    QString defaultProfileId() const;
    void setDefaultProfile(const QString& profileId);
    bool hasActiveSession() const;
    int sessionCount() const;
    void closeActiveSession();
    // Steps through the session list in order, wrapping at either end.
    void activateAdjacentSession(int delta);
    void copySelection();
    void pasteClipboard();
    void selectAll();
    void clearTerminal();
    int fontSize() const { return m_fontSize; }
    void stepFontSize(int delta);
    void resetFontSize();
    ThemeMode themeMode() const { return m_themeMode; }
    void setThemeMode(ThemeMode mode);

    // Height, in logical pixels, of the header strip. DetachedTerminalWindow
    // uses this to know which band of its own client area should behave like
    // a native caption (draggable, double-click to maximize) once its real
    // title bar is removed -- the header becomes the detached window's only
    // visible "title bar", so its bounds double as the hit-test region.
    int headerHeight() const;

    // Reflects whether the panel currently lives in the detached window, so
    // the one stable header control can offer the inverse layout action rather
    // than duplicating that state in MainWindow.
    void setDetached(bool detached);

    // Keeps the header's maximize/restore control in sync with the detached
    // window's actual state, which can change without the button (Aero Snap,
    // a double-click on the header, the system menu, Win+Up).
    void setMaximized(bool maximized);

protected:
    void showEvent(QShowEvent* event) override;

signals:
    // The user asked to hide the panel without changing its sessions.
    void hideRequested();
    // The last session was closed, or the user confirmed closing all sessions;
    // the window hides the panel.
    void closeRequested();
    // The user clicked the header's layout control. MainWindow owns moving the
    // panel into and out of its own window.
    void detachRequested();
    void attachRequested();
    // Emitted by the header's window controls, which are only visible while
    // the panel is the detached window's caption. The panel deliberately does
    // not touch window() itself: docked, that would minimize MainWindow.
    void minimizeRequested();
    void maximizeRestoreRequested();
    // Forwarded from the WebView2 host, which owns the keyboard while focused.
    void toggleRequested();
    void commandPaletteRequested();
    void readingModeRequested();
    void notepadModeRequested();
    void workspaceSearchRequested();
    // Ctrl+Shift+F (false) / Ctrl+Shift+H (true) with the Terminal focused.
    void findInFilesRequested(bool expandReplace);
    // A Ctrl+Clicked file link, already resolved to an existing file. MainWindow
    // decides whether FastMD opens it or Explorer reveals it. `line` and
    // `column` are 1-based, 0 when the link named no position.
    void fileLinkActivated(const QString& path, int line, int column);

private:
    void buildUi();
    void newTerminal();
    void newTerminalWithProfile(const TerminalProfile& profile,
                                 const QString& directoryOverride = QString());
    void openProfileMenu();
    void openOverflowMenu();
    void closeSession(int id);
    void closeAllTerminals();
    void beginRenameActiveSession();
    void refreshSessionList();
    // Applies the remembered session-list width to the splitter. Fails (and
    // must be retried) while the splitter has no real width yet or the list is
    // hidden, because QSplitter would scale the request down and keep it.
    bool applySessionListWidth();
    // Session rows are rebuilt on every sessionsChanged, and every row asks
    // for the same close glyph, so rasterizing per row costs O(rows^2) glyph
    // renders across a restore. Keyed by codepoint; the cache is dropped
    // whenever the effective theme changes the icon color.
    QIcon rowIcon(ushort code);
    void updateIcons();
    void setFontSize(int pixelSize);
    void showStatus(const QString& message, bool isError);
    // Web links and folders are handled here; files go out through
    // fileLinkActivated().
    void followLink(const QString& text, const QString& reportedCwd,
                    const QString& startDirectory);
    QString resolveWorkingDirectory(const QString& directoryOverride) const;
    // Starting a shell is CreatePseudoConsole + CreateProcess + two reader/
    // writer threads. Requests wait until xterm is ready, then drain through
    // the event loop so WebView2 can finish settling first.
    void scheduleSpawnDrain();
    void drainSpawnQueue();
    void applyEffectiveTheme();
    bool effectiveDark() const;

    TerminalController* m_controller = nullptr;
    TerminalView* m_view = nullptr;
    QListWidget* m_sessionList = nullptr;
    ThemedSplitter* m_splitter = nullptr;
    QLabel* m_status = nullptr;
    QToolButton* m_newButton = nullptr;
    QToolButton* m_profileButton = nullptr;
    QToolButton* m_overflowButton = nullptr;
    QToolButton* m_detachButton = nullptr;
    QToolButton* m_minimizeButton = nullptr;
    QToolButton* m_maximizeButton = nullptr;
    QToolButton* m_hideButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QAction* m_increaseFontSizeAction = nullptr;
    QAction* m_decreaseFontSizeAction = nullptr;
    QAction* m_resetFontSizeAction = nullptr;
    std::function<QString()> m_workingDirectoryProvider;

    // Never launch a shell until xterm is ready: otherwise its initial prompt
    // and startup output can be lost while WebView2 is loading.
    struct PendingTerminal {
        TerminalProfile profile;
        QString directoryOverride;
    };
    QList<PendingTerminal> m_pendingProfiles;
    bool m_initialSessionPending = false;
    bool m_spawnScheduled = false;
    // Whether the terminal should take focus once it has a live shell. Set by
    // an explicit user request only -- a startup restore must leave it false;
    // see ensureInitialSession().
    bool m_focusWhenReady = false;
    QHash<ushort, QIcon> m_rowIcons;
    // The session list width the user last dragged to, in logical pixels.
    int m_sessionListWidth = 240;
    bool m_sessionListWidthApplied = false;
    int m_fontSize = 15;
    // The effective dark/light state actually applied to the terminal's own
    // chrome and the xterm.js page -- derived from m_appDark and m_themeMode,
    // recomputed by applyEffectiveTheme().
    bool m_dark = false;
    // Whether the application theme (not the terminal's) is currently dark.
    bool m_appDark = false;
    bool m_detached = false;
    bool m_maximized = false;
    ThemeMode m_themeMode = ThemeMode::FollowApp;
    // Qt hides a Qt::Popup before the button that opened it sees the click, so
    // without this a click on the profile button would close and immediately
    // reopen the menu.
    bool m_profileMenuDismissedByButton = false;
};
