#include "IconHelper.h"

#include <QAbstractFileIconProvider>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

namespace {

constexpr int kLogicalBox = 24;
// Ink edges are quantized to whole device pixels, so a glyph may measure up to
// about two pixels away from its true proportion at a small box.
constexpr double kMinInkRatioTolerance = 0.05;
constexpr double kInkEdgePixels = 2.0;

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

// Fraction of the physical box's height that the glyph's ink covers, or -1 when
// nothing was painted.
double inkHeightRatio(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int minY = image.height();
    int maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) <= 12)
                continue;
            minY = qMin(minY, y);
            maxY = qMax(maxY, y);
            break;
        }
    }
    if (maxY < 0)
        return -1.0;
    return static_cast<double>(maxY - minY + 1) / image.height();
}

// Paints the icon at a device pixel ratio, as a toolbar button requests it.
double inkRatioAt(const QIcon& icon, double dpr)
{
    const QPixmap pixmap = icon.pixmap(QSize(kLogicalBox, kLogicalBox), dpr);
    return inkHeightRatio(pixmap);
}

// A glyph that reaches the pixmap's border has been cut off by its box.
bool inkTouchesEdge(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    const int lastX = image.width() - 1;
    const int lastY = image.height() - 1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) <= 12)
                continue;
            if (x == 0 || y == 0 || x == lastX || y == lastY)
                return true;
        }
    }
    return false;
}

QImage painted(const QIcon& icon, double dpr)
{
    QImage image(qRound(16 * dpr), qRound(16 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    icon.paint(&painter, QRect(0, 0, 16, 16));
    return image;
}

}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // The production build registers the font from Qt resources, which this
    // test binary does not link. Register the same file from disk so the glyph
    // metrics match what the toolbar paints.
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(FASTMD_FONT_PATH));
    bool ok = require(fontId >= 0, QStringLiteral("Material Icons font failed to load"));
    ok &= require(QFontDatabase::hasFamily(IconHelper::materialFontFamily()),
                  QStringLiteral("Material Icons family is not available"));

    const QColor color(0x44, 0x44, 0x44);

    struct Case {
        QString name;
        QIcon icon;
    };
    // Toolbar overrides: the glyph is a fixed fraction of its box, so the same
    // proportion of the icon is inked at every scale factor.
    const QList<Case> cases = {
        {QStringLiteral("Copy Path toolbar glyph"),
         IconHelper::materialIconScaled(QChar(0xE14D), color, 5.0 / 6.0)},
        {QStringLiteral("Highlight toolbar glyph"),
         IconHelper::materialIconScaled(QChar(0xE22B), color, 2.0 / 3.0)},
    };

    // A 4x render gives a 96px box, so its ink proportion is measured precisely
    // and serves as the reference for every scale factor.
    const QList<double> scales = {1.0, 1.25, 1.5, 2.0};
    for (const Case& c : cases) {
        const double reference = inkRatioAt(c.icon, 4.0);
        ok &= require(reference > 0.0,
                      QStringLiteral("%1 painted no ink at 4x").arg(c.name));
        for (double dpr : scales) {
            const int box = qRound(kLogicalBox * dpr);
            const double ratio = inkRatioAt(c.icon, dpr);
            const double tolerance = qMax(kMinInkRatioTolerance, kInkEdgePixels / box);
            ok &= require(ratio > 0.0 && qAbs(ratio - reference) <= tolerance,
                          QStringLiteral("%1 fills %2 of the box at %3x but %4 at 4x")
                              .arg(c.name)
                              .arg(ratio, 0, 'f', 3)
                              .arg(dpr)
                              .arg(reference, 0, 'f', 3));
        }
    }

    // Formatting-row glyphs at their default size must stay inside the box at
    // every scale, and so must the Highlight override. Clipping at the box edge
    // is the failure mode that the proportion check above cannot see.
    struct Glyph {
        QString name;
        QIcon icon;
    };
    const QList<Glyph> formatting = {
        {QStringLiteral("Bold"), IconHelper::materialIcon(QChar(0xE238), color, kLogicalBox)},
        {QStringLiteral("Italic"), IconHelper::materialIcon(QChar(0xE23F), color, kLogicalBox)},
        {QStringLiteral("Strikethrough"), IconHelper::materialIcon(QChar(0xE257), color, kLogicalBox)},
        {QStringLiteral("Highlight"),
         IconHelper::materialIconScaled(QChar(0xE22B), color, 2.0 / 3.0)},
    };
    for (const Glyph& g : formatting) {
        for (double dpr : scales) {
            const QPixmap pixmap = g.icon.pixmap(QSize(kLogicalBox, kLogicalBox), dpr);
            ok &= require(!inkTouchesEdge(pixmap),
                          QStringLiteral("%1 is clipped by its box at %2x").arg(g.name).arg(dpr));
        }
    }

    // Menu glyphs with a bottom-heavy ink: the menu nudge must not push them into
    // the box's bottom row at 100%, where they were cut off before.
    const QList<Glyph> menu = {
        {QStringLiteral("Format Document menu"), IconHelper::materialMenuIcon(QChar(0xE89C), color, 16)},
        {QStringLiteral("Info menu"), IconHelper::materialMenuIcon(QChar(0xE88E), color, 16)},
        {QStringLiteral("Export DOCX menu"), IconHelper::materialMenuIcon(QChar(0xE873), color, 16)},
        {QStringLiteral("Export PDF menu"), IconHelper::materialMenuIcon(QChar(0xE415), color, 16)},
    };
    for (const Glyph& g : menu) {
        for (double dpr : scales) {
            const QPixmap pixmap = g.icon.pixmap(QSize(16, 16), dpr);
            ok &= require(!inkTouchesEdge(pixmap),
                          QStringLiteral("%1 is clipped by its box at %2x").arg(g.name).arg(dpr));
        }
    }

    // The Workspace ".." row draws its arrow over the folder rows' own icon, so
    // without the arrow the two are the same pixels at every scale. Only the
    // Windows platform has shell icons.
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        QAbstractFileIconProvider provider;
        provider.setOptions(QAbstractFileIconProvider::DontUseCustomDirectoryIcons);
        const QIcon folder = provider.icon(QFileInfo(QDir::tempPath()));
        for (double dpr : {1.0, 1.25, 1.5, 1.75, 2.0, 3.0}) {
            const QImage row = painted(folder, dpr);
            ok &= require(painted(IconHelper::parentFolderIcon(Qt::transparent), dpr) == row,
                          QStringLiteral("parent folder icon differs from a folder row at %1x").arg(dpr));
            ok &= require(painted(IconHelper::parentFolderIcon(), dpr) != row,
                          QStringLiteral("parent folder icon has no arrow at %1x").arg(dpr));
        }
    }

    if (!ok)
        return 1;
    qInfo() << "IconHelperDpiTest passed";
    return 0;
}
