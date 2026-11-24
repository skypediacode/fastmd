#pragma once

#include "GitRepository.h"

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// The compact `⑂ branch ▾` control on the right of the Graph section header.
// Painted rather than styled: the label must ellipsize on a long branch name
// while the icon and chevron keep their positions, which a QToolButton's own
// text layout cannot do. It has no border or background of its own; hover
// paints the same subtle rounded fill the rest of the panel's hover
// affordances use.
class GitBranchButton final : public QWidget {
    Q_OBJECT
public:
    explicit GitBranchButton(QWidget* parent = nullptr);

    void setBranchText(const QString& text);
    void setDarkMode(bool dark);
    // Ceiling for the painted branch name; longer names elide. 0 means no
    // ceiling. SourceControlPanel keeps this at half the panel width so the
    // selector can never crowd out the GRAPH title.
    void setMaxTextWidth(int width);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QFont labelFont() const;
    void rebuildGlyphs();

    // The two glyphs are theme-coloured and never change size, so they are
    // rasterized once per theme rather than on every hover repaint.
    QPixmap m_branchGlyph;
    QPixmap m_chevronGlyph;
    QString m_text;
    int     m_maxTextWidth = 0;
    bool    m_dark = false;
    bool    m_hovered = false;
    bool    m_pressed = false;
};

// The searchable LOCAL/REMOTE branch list the button opens. Deliberately a
// switcher, not branch management: it emits one request and owns no git
// state of its own (SourceControlPanel runs the checkout through the same
// runLocal()/scheduleRefresh() path every other local write uses).
class GitBranchPopup final : public QWidget {
    Q_OBJECT
public:
    explicit GitBranchPopup(QWidget* parent = nullptr);

    void setBranches(const QVector<GitBranchRef>& branches);
    // Opens under `anchor`, with its right edge aligned to `rightAlignTo`'s
    // (the panel), clamped to the anchor's screen.
    void popupUnder(QWidget* anchor, QWidget* rightAlignTo);

signals:
    // Double-click / Enter on a branch that is not the current one.
    void branchActivated(const QString& name, bool remote);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyFilter(const QString& text);
    void activateItem(QTreeWidgetItem* item);
    // Built on first use, once the tree's palette carries the QSS colour.
    QIcon checkIcon();
    QIcon blankIcon();

    QLineEdit*   m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    QIcon        m_checkIcon;
    QIcon        m_blankIcon;
};
