#pragma once
#include <QWidget>
#include <functional>

#include "MathSymbols.h"

class QKeyEvent;
class QHideEvent;
class QShowEvent;
class QToolButton;
class QPropertyAnimation;

// Native-feeling LyX-style popup for one Math Toolbar dropdown
// group (Greek, Relations, Arrows, ...). A frameless Qt::Popup widget that
// closes on outside click, the same toolbar button, a different group
// button, or Esc, and reports the chosen symbol's LaTeX template via
// callback. Styled to match the Math Toolbar's own background/border (see
// Stylesheet.h) in both light and dark themes. Installs an application-wide
// event filter so it also closes when a click lands on a native child
// window (e.g. the WebView2 preview) that Qt's normal popup grab may not see.
class MathSymbolPopup : public QWidget {
    Q_OBJECT
public:
    // onOutsideClick is invoked (with the click's global position) whenever the
    // application-wide filter sees a mouse press outside this popup. Returning
    // true means the caller already handled it (e.g. switched directly to a
    // different Math Toolbar group's popup from that same click), so this
    // popup should not also treat the click as a plain close; returning false
    // means it was an ordinary outside click and the popup just closes.
    explicit MathSymbolPopup(const MathGroup& group, bool dark,
                              std::function<void(const QString&)> callback,
                              QWidget* parent = nullptr,
                              std::function<bool(const QPoint&)> onOutsideClick = nullptr,
                              std::function<void(const QPoint&)> onToolbarHover = nullptr);

    void adjustGeometryToScreen(QToolButton* button);
    int popupWidth() const;

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool isSeparator(const MathSymbol& symbol) const;
    bool hasVariableWidthSecondaryRow() const;
    int cellWidth(const MathSymbol& symbol, bool afterFirstSeparator) const;
    QRect cellRect(int index) const;
    QRect contentRect(int index) const;
    int contentHeight() const;
    int indexAt(const QPoint& pos) const;

    MathGroup m_group;
    bool m_dark = false;
    bool m_listLayout = false;
    bool m_matrixLayout = false;
    std::function<void(const QString&)> m_callback;
    std::function<bool(const QPoint&)> m_onOutsideClick;
    std::function<void(const QPoint&)> m_onToolbarHover;
    int m_columns = 6;
    int m_hoverIndex = -1;
    bool m_filterInstalled = false;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_geomAnim = nullptr;
    int m_scrollY = 0;
    int m_contentHeight = 0;
    int m_viewHeight = 0;

    static constexpr int m_gap = 3;
    static constexpr int m_pad = 12;
    static constexpr int m_separatorHeight = 9;
    static constexpr int m_radius = 8;
};
