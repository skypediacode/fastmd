#include "WorkspaceSearchFiles.h"

#include "MainWindowTextIO.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>

namespace WorkspaceSearchFiles {

bool load(const QString& path, LoadedText* out, qint64 maxBytes)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > maxBytes)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    if (file.error() != QFile::NoError)
        return false;

    const TextEncoding encoding = detectEncoding(raw);
    const bool utf16 = encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be;
    if (!utf16 && raw.contains('\0'))
        return false;

    const QStringConverter::Encoding converter =
        encoding == TextEncoding::Utf16Le ? QStringConverter::Utf16LE
        : encoding == TextEncoding::Utf16Be ? QStringConverter::Utf16BE
        : QStringConverter::Utf8;
    QStringDecoder decoder(converter);
    QString text = decoder.decode(raw);
    if (decoder.hasError())
        return false;

    if (!text.isEmpty() && text.front() == QChar(0xFEFF))
        text.removeFirst();
    if (out) {
        out->encoding = encoding;
        out->lineEnding = detectLineEnding(text);
        out->text = normalizeLoadedText(std::move(text));
    }
    return true;
}

bool save(const QString& path, const LoadedText& file)
{
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = encodeWithLineEnding(file.text, file.lineEnding, file.encoding);
    if (target.write(bytes) != bytes.size()) {
        target.cancelWriting();
        return false;
    }
    return target.commit();
}

}  // namespace WorkspaceSearchFiles
