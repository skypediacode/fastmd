#pragma once

#include <QSplitter>

// A QSplitter whose handle appearance is owned by C++.
//
// Gap handles separate two cards: they paint nothing but a low-contrast
// three-dot grip, so the application canvas behind them reads as the space
// between the cards. Divider handles split the inside of a single card and
// paint a 1px rule.
//
// With setPaintsCards(true) the splitter also draws the card around every
// pane tagged by ThemeCard::setRole(), keeping each pane ThemeCard::Inset px
// inside its card edge.
//
// setHandleHitSlop() widens a handle's drag target beyond its visible width,
// over the neighbouring panes' edges (the handle is transparent there).
class ThemedSplitter : public QSplitter {
public:
    enum class HandleStyle { Gap, Divider };

    ThemedSplitter(Qt::Orientation orientation, HandleStyle style, int handleWidthPx,
                   QWidget* parent = nullptr);

    void setDarkMode(bool dark);
    bool isDarkMode() const { return m_dark; }

    void setPaintsCards(bool paints);
    void setHandleHitSlop(int px);

    // Reapply the configured width after restoreState().
    void reassertHandleWidth();

protected:
    QSplitterHandle* createHandle() override;
    void childEvent(QChildEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    HandleStyle m_style;
    int m_handleWidthPx;
    bool m_dark = false;
    bool m_paintsCards = false;
    int m_hitSlop = 0;
};
