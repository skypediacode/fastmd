#pragma once

#include <QColor>
#include <QPixmap>
#include <QWidget>

class QFontMetrics;
class QPainter;
class QRect;

namespace CommandPalette {

inline constexpr int PreferredWidth = 320;
inline constexpr int MinimumWidth   = 150;
// The toolbar buttons' outer height, so the box lines up with them.
inline constexpr int BoxHeight      = 36;
inline constexpr int IconSize       = 18;
inline constexpr int SearchContentYOffset = 2;

// Fills `rect` with `background` inside a thin rounded border. Shared by the
// launcher and the popup's search field, which passes roundBottom = false
// because the command list continues beneath it.
void paintSearchBox(QPainter& painter, const QRect& rect, const QColor& background, bool dark,
                    bool roundBottom = true);

// The shared, theme-colored Material search glyph used by both the launcher
// and popup search row.
QPixmap searchIcon(const QColor& color, int size = IconSize);

// The text baseline for a label drawn inside a search box of height `boxHeight`
// whose top edge is `boxTop`.
//
// This is QLineEdit::paintEvent()'s own vertical centering, verbatim: it offsets
// by `(height - fm.height() + 1) / 2` -- note the round-up bias, which plain
// Qt::AlignVCenter lacks -- and then adds the ascent. The popup's search field
// is a real QLineEdit and cannot be talked out of that formula, so the launcher
// label and the painted popup placeholder both reproduce it rather than
// centering some other way. SearchContentYOffset is applied by both the
// launcher and active line edit so opening the palette does not move the text.
int textBaseline(int boxTop, int boxHeight, const QFontMetrics& fm);

// The magnifier's top edge inside a search box of height `boxHeight`, relative
// to the box's own top.
//
// Places the icon one pixel below the box centre to align it optically with the
// text cap height, then applies the shared SearchContentYOffset used by the
// launcher and active search row so their icon positions stay identical.
int iconTop(int boxHeight);

} // namespace CommandPalette

// The compact "Click to search commands (F1)" box pinned to the right edge of
// the main toolbar. It is only a launcher: clicking it (or pressing F1) opens
// CommandPalettePopup, which parks its own search field over this box.
//
// The box is painted rather than styled so the popup's search row can
// reproduce it pixel for pixel (see CommandPalette::paintSearchBox).
class CommandPaletteLauncher : public QWidget {
    Q_OBJECT
public:
    explicit CommandPaletteLauncher(QWidget* parent = nullptr);

    void setDarkMode(bool dark);
    void setActive(bool active);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refreshIcon();

    QPixmap m_searchIcon;
    QColor  m_background;
    QColor  m_textColor;
    bool    m_dark = false;
    bool    m_hovered = false;
    bool    m_active = false;
};
