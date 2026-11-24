#include "ImageFormats.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

namespace ImageFormats {

namespace {

// Superset gate for isImageExtension(). QImageReader::supportedImageFormats()
// enumerates the imageformats plugin directory, which must never run on the
// startup path (session restore resolves an EditorMode for every restored tab,
// and the command line / shell extension resolve one per launch argument).
// Every suffix any Qt image plugin can plausibly claim is listed here; anything
// not in it is rejected without touching QImageReader, so restoring .md/.txt/
// source tabs costs nothing. Deliberately excludes pdf/xbm/xpm/svg -- see the
// denylist below -- and it only gates: supportedExtensions() stays
// authoritative for what is actually installed. A denylisted suffix must be
// omitted here too, or it pays the full plugin enumeration to be told "no".
// (svgz stays: it is still handled by Qt's raster SVG path, since WebView2
// cannot decode a gzipped SVG from a file:// URL.)
bool isImageCandidateSuffix(QStringView suffix)
{
    static const QSet<QString> kCandidates = {
        QStringLiteral("avif"), QStringLiteral("bmp"),  QStringLiteral("cur"),
        QStringLiteral("dds"),  QStringLiteral("exr"),  QStringLiteral("gif"),
        QStringLiteral("hdr"),  QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("icns"), QStringLiteral("ico"),  QStringLiteral("jfif"),
        QStringLiteral("jp2"),  QStringLiteral("jpe"),  QStringLiteral("jpeg"),
        QStringLiteral("jpg"),  QStringLiteral("jxl"),  QStringLiteral("mng"),
        QStringLiteral("pbm"),
        QStringLiteral("pcx"),  QStringLiteral("pgm"),  QStringLiteral("pic"),
        QStringLiteral("png"),  QStringLiteral("ppm"),  QStringLiteral("psd"),
        QStringLiteral("qoi"),  QStringLiteral("ras"),  QStringLiteral("rgb"),
        QStringLiteral("svgz"), QStringLiteral("tga"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("wbmp"),
        QStringLiteral("webp"),
    };
    return kCandidates.contains(suffix.toString().toLower());
}

}

const QSet<QString>& supportedExtensions()
{
    static const QSet<QString> extensions = [] {
        // Denylist rationale: the optional Qt Image Formats add-on bundles
        // qpdf.dll, which would otherwise make QImageReader claim "pdf" and
        // hijack .pdf away from the WebView2 PDF viewer. xbm/xpm are plain-
        // text C source and are deliberately routed to the text editor. svg is
        // routed to its own WebView2-backed viewer mode (EditorMode::Svg) --
        // Qt's SVG plugin has no foreignObject/script support, so it can never
        // match what a real browser renders.
        static const QSet<QString> denylist = { QStringLiteral("pdf"), QStringLiteral("xbm"), QStringLiteral("xpm"), QStringLiteral("svg") };
        QSet<QString> result;
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for (const QByteArray& format : formats) {
            const QString ext = QString::fromLatin1(format).toLower();
            if (!denylist.contains(ext))
                result.insert(ext);
        }
        return result;
    }();
    return extensions;
}

bool isImageExtension(QStringView suffix)
{
    if (!isImageCandidateSuffix(suffix))
        return false;
    return supportedExtensions().contains(suffix.toString().toLower());
}

bool isImageFile(const QString& path)
{
    if (!isImageExtension(QFileInfo(path).suffix()))
        return false;
    QImageReader reader(path);
    return reader.canRead();
}

QString openDialogPatterns()
{
    QStringList filters;
    const QSet<QString>& extensions = supportedExtensions();
    filters.reserve(extensions.size());
    for (const QString& ext : extensions)
        filters << QStringLiteral("*.%1").arg(ext);
    filters.sort();
    return filters.join(QLatin1Char(' '));
}

QString openDialogFilter()
{
    return QCoreApplication::translate("ImageFormats", "Images (%1)").arg(openDialogPatterns());
}

}
