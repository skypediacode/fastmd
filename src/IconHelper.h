#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QString>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QApplication>
#include <QPointer>
#include <QThread>
#include <utility>

#include <QIconEngine>
#include <QRegularExpression>
#include "StartupProfiler.h"
#include "MathSymbols.h"
#include "ChemistrySymbols.h"

namespace IconHelper {

class Cache final : public QObject {
public:
    explicit Cache(QObject* parent) : QObject(parent) {}

    QHash<QString, double> aspectRatios;
    QHash<QString, QIcon> generatedIcons;
    QHash<QString, QImage> masterImages;
    QHash<QString, QPixmap> notationPixmaps;
    QIcon folderIcon;
};

inline QPointer<Cache>& cachePointer()
{
    static QPointer<Cache> instance;
    return instance;
}

inline Cache& cache()
{
    Q_ASSERT(qApp && qApp->thread() == QThread::currentThread());
    QPointer<Cache>& instance = cachePointer();
    if (!instance)
        instance = new Cache(qApp);
    return *instance;
}

inline void clearCaches()
{
    Cache* iconCache = cachePointer();
    if (!iconCache)
        return;
    iconCache->aspectRatios.clear();
    iconCache->generatedIcons.clear();
    iconCache->masterImages.clear();
    iconCache->notationPixmaps.clear();
    iconCache->folderIcon = {};
}

inline void drawCenteredText(QPainter* painter, const QRect& rect, const QFont& font,
                             const QString& text, int opticalYOffset = 0)
{
    const QFontMetrics metrics(font);
    const QRect bounds = metrics.tightBoundingRect(text);
    const int baseline = rect.center().y() - (bounds.top() + bounds.bottom()) / 2 + opticalYOffset;
    const int x = rect.center().x() - (bounds.left() + bounds.right()) / 2;
    painter->drawText(QPoint(x, baseline), text);
}

// Same idea as drawCenteredText() but without its two rounding biases, which
// both push the glyph *up* and are what makes a row of icon buttons read as
// sitting high:
//
//   * QRect::center() floors, so a 16px box reports its center at 7 when the
//     center of pixel rows 0..15 is 7.5;
//   * integer division truncates toward zero, and tightBoundingRect()'s top /
//     bottom are negative (measured from the baseline), so an odd sum loses
//     another half pixel upwards.
//
// Both centers are therefore computed as real numbers in pixel-index space and
// rounded once, at the end. Ties round down, which reads better than up.
// Callers that deliberately nudge a glyph off-center (menu icons) keep using
// drawCenteredText(), whose offsets were tuned against its arithmetic.
inline void drawInkCenteredText(QPainter* painter, const QRect& rect, const QFont& font,
                                const QString& text, int opticalYOffset = 0)
{
    const QFontMetrics metrics(font);
    const QRect ink = metrics.tightBoundingRect(text);
    const int baseline = qRound(rect.y() + (rect.height() - 1) / 2.0
                                - (ink.top() + ink.bottom()) / 2.0 + opticalYOffset);
    const int x = qRound(rect.x() + (rect.width() - 1) / 2.0
                         - (ink.left() + ink.right()) / 2.0);
    painter->drawText(QPoint(x, baseline), text);
}

inline const QString& materialFontFamily()
{
    static QString family;
    if (family.isEmpty()) {
        FASTMD_MARK("material font load begin");
        int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/MaterialIcons-Regular.ttf"));
        if (id >= 0) {
            QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) family = fams.first();
        }
        if (family.isEmpty()) family = QStringLiteral("Material Icons");
        FASTMD_MARK("material font loaded");
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
    // tightCenter centers the glyph on its own ink instead of on the font's
    // line box. Material Icons reserve descender room most glyphs never use, so
    // line-box centering leaves them sitting slightly high -- visible when they
    // have to line up with each other in a row of buttons.
    MaterialIconEngine(QChar code, QColor color, int opticalYOffset = 0,
                       QColor activeColor = QColor(), bool tightCenter = false,
                       double glyphScale = 0.0, double opticalYFraction = 0.0)
        : m_code(code), m_color(color), m_opticalYOffset(opticalYOffset),
          m_activeColor(std::move(activeColor)), m_tightCenter(tightCenter),
          m_glyphScale(glyphScale), m_opticalYFraction(opticalYFraction) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        QFont f(materialFontFamily());
        int size = qMin(rect.width(), rect.height());
        // A glyph size scales with the box it paints into. An absolute pixel
        // size does not: the box is in device pixels, so a fixed size overflows
        // it at 100% and shrinks inside it at 150%+.
        f.setPixelSize(m_glyphScale > 0.0 ? qMax(1, qRound(size * m_glyphScale))
                                          : qMax(1, size - 2));
        // Grid-fit hinting snaps each glyph's stems to the pixel grid
        // independently, so mirrored glyphs (e.g. keyboard_arrow_up vs
        // keyboard_arrow_down) can end up hinted by a different number of
        // pixels. tightBoundingRect() below measures the *unhinted* outline,
        // so drawInkCenteredText()'s center-on-ink math only matches what
        // actually gets painted when hinting is off.
        f.setHintingPreference(QFont::PreferNoHinting);
        painter->setFont(f);

        QColor drawColor = m_color;
        if (mode == QIcon::Active && m_activeColor.isValid()) {
            drawColor = m_activeColor;
        } else if (mode == QIcon::Disabled) {
            drawColor.setAlpha(128);
        }
        painter->setPen(drawColor);
        // A fractional offset tracks the box it paints into, so it is the same
        // share of the icon at every scale factor.
        const int yOffset = m_opticalYFraction != 0.0 ? qRound(size * m_opticalYFraction)
                                                      : m_opticalYOffset;
        if (m_tightCenter)
            drawInkCenteredText(painter, rect, f, QString(m_code), yOffset);
        else if (yOffset != 0)
            drawCenteredText(painter, rect, f, QString(m_code), yOffset);
        else
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
        return new MaterialIconEngine(m_code, m_color, m_opticalYOffset, m_activeColor,
                                      m_tightCenter, m_glyphScale, m_opticalYFraction);
    }

private:
    QChar m_code;
    QColor m_color;
    int m_opticalYOffset;
    QColor m_activeColor;
    bool m_tightCenter = false;
    double m_glyphScale = 0.0;
    double m_opticalYFraction = 0.0;
};

class TextIconEngine : public QIconEngine {
public:
    // tightCenter centers on the glyphs' own ink bounds instead of the font's
    // line box. Qt::AlignCenter reserves room for descenders even when the text
    // has none, so all-caps labels ("AB") visibly sit low; opt in where the
    // label has to line up with neighbouring icons.
    TextIconEngine(const QString& text, QColor color, bool tightCenter = false,
                   int opticalYOffset = 0, const QString& baselineRef = QString())
        : m_text(text), m_color(color), m_tightCenter(tightCenter),
          m_opticalYOffset(opticalYOffset), m_baselineRef(baselineRef) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        painter->save();
        painter->setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        QFont f(QStringLiteral("Segoe UI"));
        int size = qMin(rect.width(), rect.height());
        // Reduce size proportionally so text icons don't overpower Material icons
        f.setPixelSize(qMax(1, qRound(size * 0.75)));
        f.setBold(true);
        // Ink-centering puts each label's own ink midpoint on the icon's center, so
        // labels with different ink heights ("ab" ascender vs ".*" x-height) end up
        // with different bottoms. A baseline reference reuses the baseline that
        // reference text gets, so the visible glyph bottoms line up with it.
        int refBaseline = 0;
        if (m_tightCenter && !m_baselineRef.isEmpty()) {
            const QRect refInk = QFontMetrics(f).tightBoundingRect(m_baselineRef);
            refBaseline = qRound(rect.y() + (rect.height() - 1) / 2.0
                                 - (refInk.top() + refInk.bottom()) / 2.0 + m_opticalYOffset);
        }
        if (m_tightCenter) {
            // The icon rect is square but two-character labels are wider than
            // they are tall, so a size derived from the height alone clips them
            // ("AB" loses its right edge). Shrink to fit the available width.
            const int available = rect.width() - 1;
            const int inkWidth = QFontMetrics(f).tightBoundingRect(m_text).width();
            if (inkWidth > available && inkWidth > 0)
                f.setPixelSize(qMax(1, (f.pixelSize() * available) / inkWidth));
        }
        painter->setFont(f);
        
        QColor drawColor = m_color;
        if (mode == QIcon::Disabled) {
            drawColor.setAlpha(128);
        }
        painter->setPen(drawColor);
        if (m_tightCenter && !m_baselineRef.isEmpty()) {
            const QRect ink = QFontMetrics(f).tightBoundingRect(m_text);
            const int x = qRound(rect.x() + (rect.width() - 1) / 2.0
                                 - (ink.left() + ink.right()) / 2.0);
            painter->drawText(QPoint(x, refBaseline), m_text);
        } else if (m_tightCenter)
            drawInkCenteredText(painter, rect, f, m_text, m_opticalYOffset);
        else if (m_opticalYOffset != 0)
            painter->drawText(rect.translated(0, m_opticalYOffset), Qt::AlignCenter, m_text);
        else
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
        return new TextIconEngine(m_text, m_color, m_tightCenter, m_opticalYOffset, m_baselineRef);
    }

private:
    QString m_text;
    QColor m_color;
    bool m_tightCenter;
    int m_opticalYOffset = 0;
    QString m_baselineRef;
};

// Render a Material Icons glyph to a QIcon at the given pixel size.
inline QIcon materialIcon(QChar code, QColor color, int /*size*/ = 20)
{
    return QIcon(new MaterialIconEngine(code, color));
}

// glyphScale is the glyph's em size as a fraction of the icon box, so it holds
// at every device pixel ratio.
inline QIcon materialIconScaled(QChar code, QColor color, double glyphScale)
{
    return QIcon(new MaterialIconEngine(code, color, 0, QColor(), false, glyphScale));
}

// Like materialIcon(), but optically centered on the glyph's ink. Use where
// several icons sit side by side and any vertical drift reads as misalignment.
inline QIcon materialIconTight(QChar code, QColor color, int /*size*/ = 20,
                               int opticalYOffset = 0)
{
    return QIcon(new MaterialIconEngine(code, color, opticalYOffset, QColor(), true));
}

// 1/8 of the box is the 3px nudge that reads correctly at 150% (a 24px box).
inline QIcon materialMenuIcon(QChar code, QColor color, int /*size*/ = 16)
{
    return QIcon(new MaterialIconEngine(code, color, 0, QColor(Qt::white), false, 0.0, 0.125));
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
inline QIcon textIcon(const QString& text, QColor color, int /*size*/ = 20,
                      bool tightCenter = false, int opticalYOffset = 0,
                      const QString& baselineRef = QString())
{
    return QIcon(new TextIconEngine(text, color, tightCenter, opticalYOffset, baselineRef));
}

class ParentFolderIconEngine : public QIconEngine {
public:
    ParentFolderIconEngine(QColor arrowColor) : m_arrowColor(arrowColor) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        // Draw the native folder icon first (so the color is exactly the same as normal folders).
        // Resolved once: this repaints with every hover/selection change of the ".." row,
        // and QFileIconProvider::icon() is a shell lookup. Asking for a real directory,
        // with the option the Workspace model uses, gives the folder rows' own icon:
        // only the painted size is extracted, and the rows share it. icon(Folder)
        // extracts every size up to 256 px. The system directory always exists.
        Cache& iconCache = cache();
        if (iconCache.folderIcon.isNull()) {
            QFileIconProvider provider;
            provider.setOptions(QFileIconProvider::DontUseCustomDirectoryIcons);
            iconCache.folderIcon = provider.icon(
                QFileInfo(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"))));
        }
        iconCache.folderIcon.paint(painter, rect, Qt::AlignCenter, mode, state);

        painter->save();
        painter->setRenderHints(QPainter::Antialiasing);

        const double w = rect.width();
        const double h = rect.height();
        
        QPen pen(m_arrowColor);
        pen.setWidthF(qMax(1.0, w * 0.06)); 
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);

        const double cx = rect.center().x();
        const double cy = rect.center().y();
        
        // Shift arrow slightly right (closer to center) to align it beautifully with the folder face
        const double ax = cx - w * 0.01; 
        const double ay_top = cy - h * 0.06;
        const double ay_bot = cy + h * 0.18;
        
        // Vertical stem
        painter->drawLine(QPointF(ax, ay_bot), QPointF(ax, ay_top));
        
        // Arrow head wings
        const double wingW = w * 0.10;
        const double wingH = h * 0.10;
        painter->drawLine(QPointF(ax - wingW, ay_top + wingH), QPointF(ax, ay_top));
        painter->drawLine(QPointF(ax + wingW, ay_top + wingH), QPointF(ax, ay_top));
        
        // Bent tail turning right
        const double tailW = w * 0.13;
        painter->drawLine(QPointF(ax, ay_bot), QPointF(ax + tailW, ay_bot));

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
        return new ParentFolderIconEngine(m_arrowColor);
    }

private:
    QColor m_arrowColor;
};

inline QIcon parentFolderIcon(QColor arrowColor = QColor(0x3c, 0x3c, 0x3c))
{
    return QIcon(new ParentFolderIconEngine(arrowColor));
}

// ---------------------------------------------------------------------------
// Math Toolbar quick-access icons: these draw the actual resulting expression
// (numerator/denominator, super/subscript, radical, delimiters, accents)
// instead of a generic symbol, per the Math Toolbar design.


inline QString mathPngResource(const QString& id)
{
    if (id == QStringLiteral("chemistry-reaction-launcher"))
        return QStringLiteral(":/icons/math/reaction-launcher.png");
    if (id == QStringLiteral("chemistry-state-launcher"))
        return QStringLiteral(":/icons/math/state-launcher.png");
    for (const ChemistrySection& section : chemistrySections()) {
        for (const ChemistryItem& item : section.items) {
            if (item.id == id)
                return QStringLiteral(":/icons/math/chemistry/%1.png").arg(id);
        }
    }
    if (id == QStringLiteral("func")) {
        return QStringLiteral(":/icons/math/functions/func.png");
    }
    if (id == QStringLiteral("derivative")) {
        return QStringLiteral(":/icons/math/derivative/derivative-launcher.png");
    }
    if (id == QStringLiteral("integration")) {
        return QStringLiteral(":/icons/math/integration/integration-launcher.png");
    }
    if (id == QStringLiteral("matrices")) {
        return QStringLiteral(":/icons/math/matrices.png");
    }
    if (id == QStringLiteral("smallmatrix")) {
        return QStringLiteral(":/icons/math/matrices/smallmatrix.png");
    }
    // The Matrix picker and toolbar catalog use descriptive IDs, while the
    // corresponding MathSymbol entries use KaTeX environment names.
    if (id == QStringLiteral("brace-matrix")) {
        return QStringLiteral(":/icons/math/matrices/brace-matrix.png");
    }
    if (id == QStringLiteral("norm-matrix")) {
        return QStringLiteral(":/icons/math/matrices/norm-matrix.png");
    }
    if (const MathSymbol* sym = getMathSymbol(id)) {
        return sym->iconPath;
    }
    if (const MathGroup* group = getMathGroup(id)) {
        if (group->representativeIconId == QStringLiteral("func")) {
            return QStringLiteral(":/icons/math/functions/func.png");
        }
        if (const MathSymbol* repSym = getMathSymbol(group->representativeIconId)) {
            return repSym->iconPath;
        }
    }
    return QString();
}

// Chemistry icons have no
// per-group MathGroupConfig to consult -- their canvas width varies action by
// action -- so their aspect ratio must come from the asset itself rather
// than a hardcoded table, or every chemistry icon collapses to 1:1 and wide
// notation gets squashed into a square box.
inline double pngIntrinsicAspectRatio(const QString& resourcePath)
{
    Cache& iconCache = cache();
    if (const auto it = iconCache.aspectRatios.constFind(resourcePath); it != iconCache.aspectRatios.cend())
        return *it;

    auto image = iconCache.masterImages.find(resourcePath);
    if (image == iconCache.masterImages.end())
        image = iconCache.masterImages.insert(resourcePath,
            QImage(resourcePath).convertToFormat(QImage::Format_ARGB32_Premultiplied));

    double ar = 1.0;
    if (!image->isNull() && image->height() > 0) {
        ar = static_cast<double>(image->width()) / image->height();
    }
    iconCache.aspectRatios.insert(resourcePath, ar);
    return ar;
}

inline double mathSymbolAspectRatio(const QString& id)
{
    if (id == QStringLiteral("chemistry-reaction-launcher") || id == QStringLiteral("chemistry-state-launcher"))
        return pngIntrinsicAspectRatio(mathPngResource(id));
    for (const ChemistrySection& section : chemistrySections()) {
        for (const ChemistryItem& item : section.items) {
            if (item.id == id)
                return pngIntrinsicAspectRatio(mathPngResource(id));
        }
    }
    // A symbol's aspect ratio comes from whichever group's canvas config
    // generated its icon, found by containment — MathSymbol::group
    // is a display category label (e.g. "Functions") and does not match
    // MathGroup::id (e.g. "functions"), so it cannot be used to look this up.
    for (const MathGroup& grp : mathPopupGroups()) {
        for (const MathSymbol& sym : grp.symbols) {
            if (sym.id == id) {
                return pngIntrinsicAspectRatio(mathPngResource(id));
            }
        }
    }
    // Quick-access symbols and every group's own representative toolbar icon
    // are square by design.
    return 1.0;
}

inline QPixmap generatedNotationPixmap(const QString& id, const QColor& color, const QSize& requestedSize)
{
    const QString resource = mathPngResource(id);
    const int height = qMax(1, requestedSize.height());
    if (resource.isEmpty() || !QFile::exists(resource)) {
        QPixmap missing(height, height);
        missing.fill(Qt::transparent);
        QPainter painter(&missing);
        painter.setPen(QPen(Qt::red, qMax(1, height / 16)));
        painter.drawRect(1, 1, height - 2, height - 2);
        painter.drawLine(1, 1, height - 2, height - 2);
        painter.drawLine(height - 2, 1, 1, height - 2);
        return missing;
    }

    Cache& iconCache = cache();
    const QString cacheKey = id + QLatin1Char(':')
        + QString::number(static_cast<quint32>(color.rgba()), 16)
        + QLatin1Char(':') + QString::number(requestedSize.width())
        + QLatin1Char('x') + QString::number(requestedSize.height());
    if (const auto cached = iconCache.notationPixmaps.constFind(cacheKey);
        cached != iconCache.notationPixmaps.cend())
        return *cached;

    auto master = iconCache.masterImages.find(resource);
    if (master == iconCache.masterImages.end())
        master = iconCache.masterImages.insert(resource,
            QImage(resource).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    if (master->isNull())
        return generatedNotationPixmap(QString(), color, requestedSize);

    const int width = qMax(1, qRound(height * mathSymbolAspectRatio(id)));
    QImage image = master->scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    const QPixmap result = QPixmap::fromImage(image);
    iconCache.notationPixmaps.insert(cacheKey, result);
    return result;
}

class GeneratedNotationIconEngine : public QIconEngine {
public:
    GeneratedNotationIconEngine(QString id, QColor color)
        : m_id(std::move(id)), m_color(std::move(color)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override
    {
        const QPixmap source = generatedNotationPixmap(m_id, m_color, rect.size());
        const QPixmap scaled = source.scaled(rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint offset((rect.width() - scaled.width()) / 2, (rect.height() - scaled.height()) / 2);
        painter->drawPixmap(rect.topLeft() + offset, scaled);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode, QIcon::State) override
    {
        return generatedNotationPixmap(m_id, m_color, size);
    }

    QIconEngine* clone() const override
    {
        return new GeneratedNotationIconEngine(m_id, m_color);
    }

private:
    QString m_id;
    QColor m_color;
};

inline QIcon generatedMathSvgIcon(const QString& id, const QColor& color)
{
    // Math popups repaint their complete grid whenever the hovered cell changes.
    // Cache the generated color variants so hover work is limited to painting.
    Cache& iconCache = cache();
    const QString cacheKey = id + QLatin1Char(':')
        + QString::number(static_cast<quint32>(color.rgba()), 16);
    if (const auto it = iconCache.generatedIcons.constFind(cacheKey); it != iconCache.generatedIcons.cend())
        return *it;

    const QIcon result(new GeneratedNotationIconEngine(id, color));
    iconCache.generatedIcons.insert(cacheKey, result);
    return result;
}

inline QIcon generatedChemistrySvgIcon(const QString& id, const QColor& color)
{
    return generatedMathSvgIcon(id, color); // Chemistry assets share the generated math SVG tree.
}

// Toolbar/popup icon assets are drawn with breathing room baked into their
// square canvas so they sit comfortably beside other same-size buttons. Menu
// rows have no such neighbors -- crop that transparent margin so the glyph
// fills its fixed-size QSS icon slot instead of looking small inside it.
//
// The tight crop is then padded back out to a square canvas rather than
// returned at its own aspect ratio. Group representative icons vary widely
// in native shape (a square Greek letter vs. a wide "func" wordmark vs. a
// tall df/dx fraction); Qt's ::icon subcontrol scales each into the same
// square QSS box while preserving aspect ratio, so an un-squared wide/tall
// crop gets letterboxed and ends up visibly smaller than a square one. A
// square source keeps every menu icon filling its slot by the same amount.
inline QIcon trimTransparentMargins(const QIcon& source, int sampleSize = 128)
{
    const QPixmap pm = source.pixmap(sampleSize, sampleSize);
    if (pm.isNull())
        return source;

    const QImage image = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 12) {
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
                minY = qMin(minY, y);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY)
        return source;

    const QRect bbox(minX, minY, maxX - minX + 1, maxY - minY + 1);
    const int side = qMax(bbox.width(), bbox.height());
    QPixmap squared(side, side);
    squared.fill(Qt::transparent);
    QPainter p(&squared);
    p.drawPixmap((side - bbox.width()) / 2, (side - bbox.height()) / 2, pm, bbox.x(), bbox.y(),
                 bbox.width(), bbox.height());
    p.end();

    QIcon trimmed;
    trimmed.addPixmap(squared);
    return trimmed;
}

} // namespace IconHelper
