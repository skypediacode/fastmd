#pragma once
#include "DocumentModel.h"
#include "DocumentSpacing.h"

#include <QTabWidget>
#include <QTabBar>
#include <QByteArray>
#include <QList>
#include <QMouseEvent>

class DocumentModel;
class MarkdownEditor;
class PreviewWidget;
class ImageViewer;
class GitDiffView;
class QSplitter;
class QToolButton;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QTimer;

struct TabPage {
    DocumentModel* model       = nullptr;
    MarkdownEditor* editor      = nullptr;
    PreviewWidget*  preview     = nullptr;
    ImageViewer*    imageViewer = nullptr;  // lazily created, Image mode only
    GitDiffView*    diffView    = nullptr;  // Diff mode only
    QSplitter*      splitter    = nullptr;
};

struct RestoredTabSettings {
    QByteArray splitterState;
    bool markdownPreviewVisible = true;
    bool wordWrapEnabled = true;
    int editorFontSize = 0;
};

// QTabBar subclass that emits newTabRequested when the user double-clicks
// on the vacant area to the right of all existing tabs, and tabRenameRequested
// when double-clicking an existing tab. Right-click emits tabContextMenuRequested
// over a tab and emptyAreaContextMenuRequested anywhere else. It also owns the
// trailing "+" button, which is kept just after the last tab.

class TabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr);
    void setDragVisual(qulonglong pageId, int insertIndex, bool destination);
    void clearDragVisual();
    void setDarkMode(bool dark);
    // The visible box of tab `index`: tabRect() without the QSS margins.
    QRect tabBox(int index) const;
    // The part of the bar tabs are drawn in: between the scroll buttons when
    // the tabs overflow.
    QRect tabArea() const;
signals:
    void newTabRequested();
    void middleClickCloseRequested(int index);
    void tabRenameRequested(int index);
    void tabContextMenuRequested(int index, QPoint globalPos);
    void emptyAreaContextMenuRequested(QPoint globalPos);
    void tabDragStarted(qulonglong pageId, QPoint globalPress, QPoint globalPosition);
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const int idx = tabAt(event->pos());
            if (idx == -1)
                emit newTabRequested();
            else
                emit tabRenameRequested(idx);
        } else {
            QTabBar::mouseDoubleClickEvent(event);
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            // Capture the tab under the cursor at press time so the context
            // menu operates on the correct tab even if the cursor drifts
            // slightly before the button is released.
            m_rightClickedTab = tabAt(event->pos());
        }
        if (event->button() == Qt::LeftButton) {
            m_dragPageId = 0;
            const int index = tabAt(event->pos());
            if (index >= 0)
                m_dragPageId = tabData(index).toULongLong();
            m_dragStart = event->globalPosition().toPoint();
            m_dragStarted = false;
        }
        QTabBar::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            int index = tabAt(event->pos());
            if (index != -1) {
                emit middleClickCloseRequested(index);
                return;
            }
        }
        if (event->button() == Qt::RightButton) {
            const int idx = m_rightClickedTab;
            m_rightClickedTab = -1;
            if (idx != -1)
                emit tabContextMenuRequested(idx, event->globalPosition().toPoint());
            else
                emit emptyAreaContextMenuRequested(event->globalPosition().toPoint());
            return;
        }
        if (event->button() == Qt::LeftButton) {
            m_dragPageId = 0;
            m_dragStarted = false;
        }
        QTabBar::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void tabLayoutChange() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    class NewTabButton;
    void paintScrollButton(QToolButton* button) const;
    // The "+" follows the last tab, so its position changes with every tab
    // insertion, removal, move and horizontal scroll. QTabBar signals none of
    // those, but it repaints for all of them.
    void updateNewTabButtonGeometry();
    void updateCloseButtonGeometry();
    int  trailingLimit() const;
    void paintCurrentTab(QPainter& painter, int dragged) const;
    void paintDraggedTab(QPainter& painter, int dragged) const;
    int  draggedIndex() const;

    NewTabButton* m_newTabButton = nullptr;
    bool m_dark = false;
    int m_rightClickedTab = -1;
    qulonglong m_dragPageId = 0;
    QPoint m_dragStart;
    bool m_dragStarted = false;
    qulonglong m_visualPageId = 0;
    int m_visualInsertIndex = -1;
    bool m_visualDestination = false;
};


class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    using QTabWidget::tabBar;

    explicit TabWidget(QWidget* parent = nullptr);
    ~TabWidget() override;

    void updateTabBarLayout();

    // Returns the new tab index
    // `diffSpec`, when non-null, builds a read-only Git diff document instead of
    // an editable tab: EditorMode::Diff, no file path, a GitDiffView in place of
    // the Editor/Preview pair. See MainWindow::openGitDiff().
    int  addNewTab(const QString& filePath = {}, bool activate = true,
                   const RestoredTabSettings* restoredSettings = nullptr,
                   const GitDiffSpec* diffSpec = nullptr);
    void closeTabAt(int index);
    TabPage* takeTabPage(int index);
    int insertTabPage(TabPage* page, int index, bool activate = true);
    int indexOf(TabPage* page) const;
    void moveTabPage(int from, int to);
    void setDragVisual(TabPage* page, int insertIndex, bool destination);
    void clearDragVisual();

    TabPage*      currentPage()  const;
    TabPage*      pageAt(int i)  const;
    MarkdownEditor* currentEditor()  const;
    PreviewWidget*  currentPreview() const;
    DocumentModel*  currentModel()   const;

    void markDirty(int index, bool dirty);
    void refreshTitle(int index);
    void setDarkMode(bool dark);
    void setCurrentLineHighlightEnabled(bool enabled);
    // The Preview font family for every tab's preview, present and future.
    void setPreviewFontFamily(const QString& family);
    void setDocumentSpacing(const DocumentSpacing::Settings& spacing);
    // Fans the window-level spell-check flag out to every tab, applying
    // per-tab eligibility (prose modes only). Safe to call repeatedly - it is
    // how a language/mode change re-evaluates which tabs are checked.
    void setSpellCheckActive(bool active);
    void setDefaultTabSettings(const RestoredTabSettings& settings, EditorMode mode);
    // The shared Git diff zoom level, common to every diff tab in every
    // window; applied to each new diff tab as it is built.
    void setDiffZoomDelta(int delta) { m_cachedDiffZoomDelta = delta; }
    // Mirrors MainWindow's window-level Reading Mode flag so a tab created while
    // it is on is laid out read-only from the start, instead of being built as a
    // split Editor|Preview and corrected afterwards (which flashes the handle).
    void setReadingMode(bool enabled) { m_readingMode = enabled; }
    // Same idea for Notepad Mode: a tab created while it is on is built with the
    // Preview already hidden.
    void setNotepadMode(bool enabled) { m_notepadMode = enabled; }

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

signals:
    void editorActivated(MarkdownEditor* editor, PreviewWidget* preview);
    void tabClosed(int index);
    void outlineRequested();
    void tabRenameRequested(int index);
    void tabContextMenuRequested(int index, QPoint globalPos);
    void tabBarContextMenuRequested(QPoint globalPos);
    void newTabRequested();
    void tabDragStarted(TabPage* page, QPoint globalPress, QPoint globalPosition);
    // A user changed a diff tab's zoom (Ctrl+wheel); MainWindow broadcasts the
    // new level to every other open diff tab, in this window and any other,
    // and persists it.
    void diffZoomChanged(int delta);

private slots:
    void onCurrentChanged(int index);
    void onCloseRequested(int index);

private:
    class CloseTabButton;
    QWidget* makeCloseButton(TabPage* page);
    QTimer*  ensureSplitterPersistTimer();
    void     persistCachedSplitterState();

    QTimer*         m_splitterPersistTimer = nullptr;

    QList<TabPage*> m_pages;
    bool            m_darkMode    = false;
    bool            m_layoutGuard = false;
    bool            m_currentLineHighlightEnabled = true;
    QString         m_previewFontFamily;
    DocumentSpacing::Settings m_documentSpacing;
    bool            m_spellCheckActive = false;

    QByteArray      m_cachedSplitterState;
    int             m_cachedDefaultMode = 0;
    bool            m_cachedPreviewVisible = true;
    bool            m_cachedWordWrapEnabled = true;
    bool            m_readingMode = false;
    bool            m_notepadMode = false;
    int             m_cachedFontSize = 0;
    int             m_cachedDiffZoomDelta = 0;

    static int      s_untitled;
};
