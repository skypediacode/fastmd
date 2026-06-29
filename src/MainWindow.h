#pragma once
#include "ExportManager.h"
#include "DocumentModel.h"

#include <QMainWindow>
#include <QStringList>
#include <QIcon>

struct TabPage;
class TabWidget;
class MarkdownEditor;
class PreviewWidget;
class FindReplaceDialog;
class QLabel;
class QActionGroup;
class QTimer;
class QFileSystemWatcher;
class QFileSystemModel;
class QTreeView;
class QToolBar;
class QToolButton;
class QWidget;
class QSplitter;
class QNetworkAccessManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // File
    void newFile();
    void openFile();
    void openFilePath(const QString& path);
    void saveFile();
    void saveFileAs();
    void doExportHtml();
    void doExportPdf();
    void editPdfPageSetup();
    void openPreferences();
    void doPreviewBrowser();

    // Edit
    void openFind();
    void openReplace();
    FindReplaceDialog* findDialog();  // lazily creates m_findDialog on first use

    // View
    void togglePreview();
    void setEditorMode(bool markdown);
    void setTheme(const QString& theme);
    void zoomIn();
    void zoomOut();

    // Internal
    void onEditorActivated(MarkdownEditor* editor, PreviewWidget* preview);
    void onTabCloseRequested(int index);
    void onContentReady();
    void onCursorPositionChanged();
    void onTextChanged();
    void onFileChanged(const QString& path);

    void openRecentFile();
    void clearRecentFiles();
    void checkForUpdates();

private:
    struct SessionTabState;

    void createMenus();
    void createToolbar();
    void createStatusBar();
    void createWorkspacePanel();

    void connectEditor(MarkdownEditor* editor, PreviewWidget* preview);
    void disconnectEditor();

    bool confirmClose(int tabIndex);
    bool saveDocument(int tabIndex, bool forceDialog);

    void addToRecent(const QString& path);
    void rebuildRecentMenu();
    void ensureWorkspaceModel();   // lazily creates the QFileSystemModel on first tree reveal
    void updateWorkspaceTreeRoot();
    void setWorkspaceTreeVisible(bool visible, bool persist = true);
    QString workspaceRootForTabs() const;
    void restoreWorkspaceExpandedState();
    void rememberWorkspaceExpansion(const QString& path, bool expanded);
    void updateWorkspaceToggleIcon();

    void applyTheme(const QString& theme);
    void updateWindowTitle();
    void updatePreview();
    void updateCurrentHtml();
    void updateWordCount(const QString& text = {});
    void watchFile(const QString& path);
    void updateToolbarIcons(bool dark);
    void syncCurrentTabUi();
    bool isPreviewVisible() const;
    void setPreviewVisible(bool visible, bool persist = true);

    void readSettings();
    void writeSettings();
    void restorePreviousSession();
    void restoreTabState(TabPage* page, const SessionTabState& state);
    bool loadFileText(const QString& path, QString* content, QString* lineEnding) const;
    int addTabFromFile(const QString& path, bool activate, bool addToRecent, bool showError,
                       const SessionTabState* state = nullptr);

    // ---- toolbar icon registry ----
    struct TbIcon {
        QAction* action;
        bool     isText;    // true = text label, false = Material Icons codepoint
        QChar    code;
        QString  text;
    };
    QList<TbIcon> m_tbIcons;
    QToolBar*     m_toolbar      = nullptr;
    QToolButton*  m_openButton   = nullptr;
    QToolButton*  m_recentChevron = nullptr;
    QToolButton*  m_workspaceToggle = nullptr;

    // ---- widgets ----
    TabWidget*          m_tabs;
    FindReplaceDialog*  m_findDialog = nullptr;
    QLabel*             m_lblPosition;
    QLabel*             m_lblWords;
    QLabel*             m_lblMode;
    QLabel*             m_lblEncoding;
    QSplitter*          m_mainSplitter = nullptr;
    QWidget*            m_workspacePanel = nullptr;
    QTreeView*          m_workspaceTree = nullptr;
    QFileSystemModel*    m_workspaceModel = nullptr;

    // ---- menus / actions ----
    QMenu*        m_recentMenu;
    QActionGroup* m_themeGroup;
    QAction*      m_actTogglePreview;
    QAction*      m_actToolbarPreview = nullptr;
    QAction*      m_actModeToggle = nullptr;

    // ---- state ----
    QStringList        m_recentFiles;
    QString            m_theme = QStringLiteral("system");
    MarkdownEditor*    m_activeEditor  = nullptr;
    PreviewWidget*     m_activePreview = nullptr;
    QFileSystemWatcher* m_watcher;
    QStringList         m_ignoredFileChanges;
    ExportManager::PdfExportOptions m_pdfExportOptions;
    QString             m_workspaceRootPath;
    QStringList         m_workspaceExpandedPaths;
    bool                m_workspaceTreeVisible = false;
    bool                m_restoreSessionOnStartup = false;
    QNetworkAccessManager* m_networkManager = nullptr;

    struct SessionTabState {
        QString path;
        int     cursorPos = 0;
        int     editorScroll = 0;
        int     previewScroll = 0;
        EditorMode mode = EditorMode::Markdown;
        bool    previewVisible = true;
    };

    QList<SessionTabState> m_sessionTabs;
    int                    m_sessionActiveTab = -1;

    // current HTML body (kept for export)
    QString m_currentHtml;
    bool m_htmlDirty = true;
    bool m_previewDirty = true;
    bool m_scrollSyncing = false;
    bool m_markdownPreviewVisible = true;
    QMetaObject::Connection m_lineNumberWidthConn;
};
