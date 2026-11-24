#pragma once

#include <QScrollBar>
#include <QPainter>
#include <QStyleOptionSlider>

class FastScrollBar : public QScrollBar {
public:
    explicit FastScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QScrollBar(orientation, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionSlider option;
        initStyleOption(&option);

        QPainter painter(this);
        const QColor track = palette().color(QPalette::Base);
        painter.fillRect(rect(), track);

        QRect handle = style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                QStyle::SC_ScrollBarSlider, this);
        if (handle.isEmpty())
            return;

        handle.adjust(2, 2, -2, -2);

        const int alpha = option.state & QStyle::State_Sunken ? 204
            : option.state & QStyle::State_MouseOver ? 153 : 97;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(130, 130, 130, alpha));
        painter.drawRoundedRect(handle, 5, 5);
    }
};
