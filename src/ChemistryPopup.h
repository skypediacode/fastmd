#pragma once
#include <QWidget>
#include <functional>
#include "ChemistrySymbols.h"

class QKeyEvent;
class QHideEvent;
class QPaintEvent;
class QShowEvent;
class QToolButton;
class QPropertyAnimation;

// Popup opened by the Math Toolbar's Chemistry button. Unlike MathSymbolPopup
// (one flat grid/list per group), this shows several titled sections (Common,
// Chemical formulas, Charges and ions, ...) in a scrollable column, mixing
// compact icon-style buttons for short formulas/arrows with full-width rows
// for longer reaction equations. Shares MathSymbolPopup's outside-click/Esc
// dismissal approach: a frameless Qt::Popup widget with an application-wide
// event filter (needed because clicks on the native WebView2 preview child
// window bypass Qt's normal popup grab).
class ChemistryPopup : public QWidget {
    Q_OBJECT
public:
    // onOutsideClick mirrors MathSymbolPopup's: returning true means the
    // caller already handled the click (e.g. switched straight to a
    // different Math Toolbar group's popup), so this popup should not also
    // treat it as a plain close.
    explicit ChemistryPopup(bool dark, std::function<void(const QString&)> callback,
                             QWidget* parent = nullptr,
                             std::function<bool(const QPoint&)> onOutsideClick = nullptr,
                             QVector<ChemistrySection> sections = {},
                             bool verticalSvgRows = false,
                             std::function<void(const QPoint&)> onPointerMove = nullptr);

    void adjustGeometryToScreen(QToolButton* button);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildContent(bool dark, std::function<void(const QString&)> callback);
    void buildSvgRows();

    std::function<bool(const QPoint&)> m_onOutsideClick;
    std::function<void(const QPoint&)> m_onPointerMove;
    bool m_filterInstalled = false;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_geomAnim = nullptr;
    int m_desiredHeight = 200;
    QVector<ChemistrySection> m_sections;
    bool m_verticalSvgRows = false;
    bool m_dark = false;
    std::function<void(const QString&)> m_callback;

    int m_popupWidth = 380;
    static constexpr int m_minHeight = 160;
    static constexpr int m_maxHeight = 520;
};
