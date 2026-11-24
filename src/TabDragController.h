#pragma once

#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QElapsedTimer>

class MainWindow;
class TabWidget;
struct TabPage;
class QWidget;

class TabDragController final : public QObject {
    Q_OBJECT
public:
    static TabDragController* instance();
    void beginDrag(MainWindow* source, TabWidget* tabs, TabPage* page,
                   const QPoint& globalPress, const QPoint& globalPosition);
    void cancelForWindow(MainWindow* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class State { Idle, ReorderingInSource, HoveringDestination, Detaching };

    explicit TabDragController(QObject* parent = nullptr);
    void updateDrag(const QPoint& globalPosition);
    void finishDrag(const QPoint& globalPosition);
    void cancelDrag();
    void clearVisuals();
    TabWidget* tabWidgetAt(const QPoint& globalPosition, MainWindow** window = nullptr) const;
    int insertionIndex(TabWidget* tabs, const QPoint& globalPosition) const;

    State m_state = State::Idle;
    QPointer<MainWindow> m_sourceWindow;
    QPointer<TabWidget> m_sourceTabs;
    QPointer<TabWidget> m_destinationTabs;
    TabPage* m_page = nullptr;
    int m_originalIndex = -1;
    int m_destinationIndex = -1;
    QPoint m_dragOffset;
    QWidget* m_preview = nullptr;
    QPointer<MainWindow> m_hoverWindow;
    QPointer<MainWindow> m_raisedWindow;
    QElapsedTimer m_hoverTimer;
    bool m_sourceDetached = false;
};
