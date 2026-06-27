#pragma once
#include "ExportManager.h"

#include <QMainWindow>
#include <QStringList>
#include <QIcon>

class TabWidget;
class MarkdownEditor;
class PreviewWidget;
class FindReplaceDialog;
class QLabel;
class QActionGroup;
class QTimer;
class QFileSystemWatcher;
class QToolBar;
class QToolButton;

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
    void doPreviewBrowser();

    // Edit
    void openFind();
    void openReplace();

    // View
    void togglePreview();
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

private:
    void createMenus();
    void createToolbar();
    void createStatusBar();

    void connectEditor(MarkdownEditor* editor, PreviewWidget* preview);
    void disconnectEditor();

    bool confirmClose(int tabIndex);
    bool saveDocument(int tabIndex, bool forceDialog);

    void addToRecent(const QString& path);
    void rebuildRecentMenu();

    void applyTheme(const QString& theme);
    void updateWindowTitle();
    void updatePreview();
    void updateCurrentHtml();
    void updateWordCount(const QString& text = {});
    void watchFile(const QString& path);
    void updateToolbarIcons(bool dark);
    bool isPreviewVisible() const;

    void readSettings();
    void writeSettings();

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

    // ---- widgets ----
    TabWidget*          m_tabs;
    FindReplaceDialog*  m_findDialog;
    QLabel*             m_lblPosition;
    QLabel*             m_lblWords;
    QLabel*             m_lblEncoding;

    // ---- menus / actions ----
    QMenu*        m_recentMenu;
    QActionGroup* m_themeGroup;
    QAction*      m_actTogglePreview;

    // ---- state ----
    QStringList        m_recentFiles;
    QString            m_theme = QStringLiteral("system");
    MarkdownEditor*    m_activeEditor  = nullptr;
    PreviewWidget*     m_activePreview = nullptr;
    QFileSystemWatcher* m_watcher;
    QStringList         m_ignoredFileChanges;
    ExportManager::PdfExportOptions m_pdfExportOptions;

    // current HTML body (kept for export)
    QString m_currentHtml;
    bool m_htmlDirty = true;
    bool m_previewDirty = true;
};
