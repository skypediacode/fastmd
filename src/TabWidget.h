#pragma once
#include <QTabWidget>
#include <QTabBar>
#include <QList>
#include <QMouseEvent>

class DocumentModel;
class MarkdownEditor;
class PreviewWidget;
class QSplitter;
class QToolButton;
class QPaintEvent;
class QShowEvent;

struct TabPage {
    DocumentModel* model   = nullptr;
    MarkdownEditor* editor  = nullptr;
    PreviewWidget*  preview = nullptr;
    QSplitter*      splitter= nullptr;
};

// QTabBar subclass that emits newTabRequested when the user double-clicks
// on the vacant area to the right of all existing tabs.

class TabBar : public QTabBar {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr) : QTabBar(parent) {}
signals:
    void newTabRequested();
    void middleClickCloseRequested(int index);
protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && tabAt(event->pos()) == -1)
            emit newTabRequested();
        else
            QTabBar::mouseDoubleClickEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            event->accept();
            return;
        }
        QTabBar::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            int index = tabAt(event->pos());
            if (index != -1) {
                emit middleClickCloseRequested(index);
                return;
            }
        }
        QTabBar::mouseReleaseEvent(event);
    }
};

class TabWidget : public QTabWidget {
    Q_OBJECT
public:
    using QTabWidget::tabBar;

    explicit TabWidget(QWidget* parent = nullptr);

    void updateTabBarLayout();

    // Returns the new tab index
    int  addNewTab(const QString& filePath = {}, bool activate = true);
    void closeTabAt(int index);

    TabPage*      currentPage()  const;
    TabPage*      pageAt(int i)  const;
    MarkdownEditor* currentEditor()  const;
    PreviewWidget*  currentPreview() const;
    DocumentModel*  currentModel()   const;

    void markDirty(int index, bool dirty);
    void refreshTitle(int index);
    void setDarkMode(bool dark);

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

private slots:
    void onCurrentChanged(int index);
    void onCloseRequested(int index);

private:
    class CloseTabButton;
    QWidget* makeCloseButton(TabPage* page);
    void scheduleTabBarLayoutUpdate();
    QList<TabPage*> m_pages;
    bool            m_darkMode    = false;
    bool            m_layoutGuard = false;
    bool            m_layoutPending = false;
    static int      s_untitled;
};
