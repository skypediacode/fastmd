#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QString>

#include <QIconEngine>

namespace IconHelper {

inline const QString& materialFontFamily()
{
    static QString family;
    if (family.isEmpty()) {
        int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/MaterialIcons-Regular.ttf"));
        if (id >= 0) {
            QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) family = fams.first();
        }
        if (family.isEmpty()) family = QStringLiteral("Material Icons");
    }
    return family;
}

inline const QString& materialFontFamilyOutlined()
{
    static QString family;
    if (family.isEmpty()) {
        int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/MaterialIconsOutlined-Regular.otf"));
        if (id >= 0) {
            QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) family = fams.first();
        }
        if (family.isEmpty()) family = QStringLiteral("Material Icons Outlined");
    }
    return family;
}

class MaterialIconEngine : public QIconEngine {
public:
    MaterialIconEngine(QChar code, QColor color) : m_code(code), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        QFont f(materialFontFamily());
        int size = qMin(rect.width(), rect.height());
        f.setPixelSize(qMax(1, size - 2));
        painter->setFont(f);
        
        QColor drawColor = m_color;
        if (mode == QIcon::Disabled) {
            drawColor.setAlpha(128);
        }
        painter->setPen(drawColor);
        painter->drawText(rect, Qt::AlignCenter, QString(m_code));
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override {
        return new MaterialIconEngine(m_code, m_color);
    }

private:
    QChar m_code;
    QColor m_color;
};

class TextIconEngine : public QIconEngine {
public:
    TextIconEngine(const QString& text, QColor color) : m_text(text), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        QFont f(QStringLiteral("Segoe UI"));
        int size = qMin(rect.width(), rect.height());
        f.setPixelSize(qMax(1, size - 4));
        f.setBold(true);
        painter->setFont(f);
        
        QColor drawColor = m_color;
        if (mode == QIcon::Disabled) {
            drawColor.setAlpha(128);
        }
        painter->setPen(drawColor);
        painter->drawText(rect, Qt::AlignCenter, m_text);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override {
        return new TextIconEngine(m_text, m_color);
    }

private:
    QString m_text;
    QColor m_color;
};

// Render a Material Icons glyph to a QIcon at the given pixel size.
inline QIcon materialIcon(QChar code, QColor color, int /*size*/ = 20)
{
    return QIcon(new MaterialIconEngine(code, color));
}

class MaterialIconOutlinedEngine : public QIconEngine {
public:
    MaterialIconOutlinedEngine(QChar code, QColor color) : m_code(code), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        QFont f(materialFontFamilyOutlined());
        int size = qMin(rect.width(), rect.height());
        f.setPixelSize(qMax(1, size - 2));
        painter->setFont(f);
        QColor drawColor = m_color;
        if (mode == QIcon::Disabled) drawColor.setAlpha(128);
        painter->setPen(drawColor);
        painter->drawText(rect, Qt::AlignCenter, QString(m_code));
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override {
        return new MaterialIconOutlinedEngine(m_code, m_color);
    }

private:
    QChar m_code;
    QColor m_color;
};

// Render a Material Icons Outlined glyph to a QIcon.
inline QIcon materialIconOutlined(QChar code, QColor color, int /*size*/ = 20)
{
    return QIcon(new MaterialIconOutlinedEngine(code, color));
}

// Render a short text label ("H1", "H2" ...) to a QIcon.
inline QIcon textIcon(const QString& text, QColor color, int /*size*/ = 20)
{
    return QIcon(new TextIconEngine(text, color));
}

} // namespace IconHelper
