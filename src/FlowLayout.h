#pragma once
#include <QLayout>
#include <QList>
#include <QStyle>

// Arranges child widgets left-to-right, wrapping to a new row when the
// current row would exceed the available width, and reports
// heightForWidth() so a container using this layout grows vertically to
// show every child across as many rows as needed instead of clipping,
// scrolling, or hiding items behind an overflow menu.
class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(QWidget* parent, int margin = 0, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = 0, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_itemList;
    int m_hSpace;
    int m_vSpace;
};
