#include "DocumentModel.h"
#include "ImageFormats.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QSet<QString>& extensions = ImageFormats::supportedExtensions();

    bool ok = true;

    // Built into Qt6Gui or provided by the imageformats plugins Qt installs by
    // default -- their absence is a real regression.
    static const QStringList required = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("ico"),
        QStringLiteral("ppm"), QStringLiteral("pgm"), QStringLiteral("pbm"),
    };
    for (const QString& ext : required) {
        ok &= require(extensions.contains(ext),
                      QStringLiteral("supported image extensions missing '%1'").arg(ext));
    }

    // Only present with the optional Qt Image Formats add-on (qtimageformats,
    // installed via the Qt Maintenance Tool). Warn rather than fail: a dev
    // machine without it is a supported configuration -- those formats simply
    // fall through to existing handling. scripts/build-msix.ps1 is the hard
    // gate that stops a *packaged* build from shipping without them.
    static const QStringList addOn = {
        QStringLiteral("webp"), QStringLiteral("tif"), QStringLiteral("tiff"),
    };
    for (const QString& ext : addOn) {
        if (!extensions.contains(ext)) {
            qWarning().noquote() << QStringLiteral(
                "warning: '%1' unsupported -- install the Qt Image Formats add-on "
                "(required for packaging)").arg(ext);
        }
    }

    ok &= require(!extensions.contains(QStringLiteral("pdf")),
                  QStringLiteral("image extensions must not claim 'pdf' (qpdf.dll hijack regression)"));
    ok &= require(!extensions.contains(QStringLiteral("xbm")),
                  QStringLiteral("image extensions must not claim 'xbm' (treated as text)"));
    ok &= require(!extensions.contains(QStringLiteral("xpm")),
                  QStringLiteral("image extensions must not claim 'xpm' (treated as text)"));
    ok &= require(!extensions.contains(QStringLiteral("svg")),
                  QStringLiteral("image extensions must not claim 'svg' (routed to the WebView2 SVG viewer)"));

    ok &= require(DocumentModel::modeForPath(QStringLiteral("x.pdf")) == EditorMode::Pdf,
                  QStringLiteral("x.pdf must resolve to EditorMode::Pdf, not Image"));
    ok &= require(DocumentModel::modeForPath(QStringLiteral("x.png")) == EditorMode::Image,
                  QStringLiteral("x.png must resolve to EditorMode::Image"));
    ok &= require(DocumentModel::modeForPath(QStringLiteral("x.xpm")) != EditorMode::Image,
                  QStringLiteral("x.xpm must not resolve to EditorMode::Image"));
    ok &= require(DocumentModel::modeForPath(QStringLiteral("x.svg")) == EditorMode::Svg,
                  QStringLiteral("x.svg must resolve to EditorMode::Svg, not Image"));

    // The cheap suffix gate must agree with the authoritative runtime set:
    // a candidate suffix missing from the gate would silently disable a format.
    for (const QString& ext : extensions) {
        ok &= require(ImageFormats::isImageExtension(ext),
                      QStringLiteral("isImageExtension() rejects supported format '%1' "
                                     "-- missing from the candidate-suffix gate").arg(ext));
    }

    return ok ? 0 : 1;
}
