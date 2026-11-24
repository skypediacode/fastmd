#pragma once

#include <QIcon>
#include <QToolButton>

// Shared toolbar button for generated KaTeX SVGs and labeled dropdowns. It
// uses the same 48px toolbar geometry as Math while allowing notation with a
// natural wide canvas.
class ToolbarSvgButton final : public QToolButton {
    Q_OBJECT
public:
    ToolbarSvgButton(const QIcon& icon, double aspectRatio, bool hasDropdown,
                     QWidget* parent = nullptr, bool squareButton = false,
                     bool allowNaturalWidth = false);
    ToolbarSvgButton(const QString& text, bool hasDropdown, QWidget* parent = nullptr);

    void refreshIcon(const QIcon& icon);
    void setPopupOpen(bool open);
    void setIconAlignment(Qt::Alignment alignment);
    void setIconHorizontalPadding(int padding);

    static constexpr int iconHeight() { return kIconHeight; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QIcon m_icon;
    QString m_text;
    double m_aspectRatio = 1.0;
    bool m_hasDropdown = false;
    bool m_squareButton = false;
    bool m_allowNaturalWidth = false;
    Qt::Alignment m_iconAlignment = Qt::AlignCenter;
    int m_iconHorizontalPadding = kHorizontalPadding;

    static constexpr int kIconHeight = 40;
    static constexpr int kButtonHeight = 48;
    static constexpr int kHorizontalPadding = 5;
    static constexpr int kTextHorizontalPadding = 10;
    static constexpr int kDropdownWidth = 16;
    static constexpr int kMinimumWidth = 48;
    static constexpr int kMaximumWidth = 280;
};
