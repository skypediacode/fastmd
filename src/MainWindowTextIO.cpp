#include "MainWindowTextIO.h"
#include "SyntaxDefinitionResolver.h"
#include "DocumentModel.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QStringList>
#include <utility>

QString detectLineEnding(const QString& text)
{
    if (text.contains(QStringLiteral("\r\n")))
        return windowsLineEnding();
    if (text.contains(QLatin1Char('\r')))
        return classicMacLineEnding();
    if (text.contains(QLatin1Char('\n')))
        return unixLineEnding();
    // A single-line file states no preference; keep the platform default.
    return windowsLineEnding();
}

// ---------------------------------------------------------------------------
TextEncoding detectEncoding(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return TextEncoding::Utf8Bom;
    if (data.size() >= 2) {
        const auto first = static_cast<unsigned char>(data.at(0));
        const auto second = static_cast<unsigned char>(data.at(1));
        // FF FE 00 00 is UTF-32LE, not a UTF-16LE document with two NULs.
        // FastMD does not read UTF-32; leaving it as UTF-8 lets the binary
        // guard in loadEditableTextFile() reject it.
        const bool utf32Le = data.size() >= 4 && data.at(2) == '\0' && data.at(3) == '\0';
        if (first == 0xFF && second == 0xFE && !utf32Le)
            return TextEncoding::Utf16Le;
        if (first == 0xFE && second == 0xFF)
            return TextEncoding::Utf16Be;
    }
    return TextEncoding::Utf8;
}

// ---------------------------------------------------------------------------
QString normalizeLoadedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

// ---------------------------------------------------------------------------
QString decodeText(const QByteArray& raw, TextEncoding encoding)
{
    QString text;
    switch (encoding) {
    case TextEncoding::Utf16Le:
        text = QStringDecoder(QStringDecoder::Utf16LE).decode(raw);
        break;
    case TextEncoding::Utf16Be:
        text = QStringDecoder(QStringDecoder::Utf16BE).decode(raw);
        break;
    case TextEncoding::Utf8:
    case TextEncoding::Utf8Bom:
        text = QString::fromUtf8(raw);
        break;
    }
    if (!text.isEmpty() && text.front() == QChar(0xFEFF))
        text.removeFirst();
    return text;
}

// ---------------------------------------------------------------------------
QString decodeAutoText(const QByteArray& raw, TextEncoding* detected)
{
    const TextEncoding encoding = detectEncoding(raw);
    if (detected)
        *detected = encoding;
    return decodeText(raw, encoding);
}

// ---------------------------------------------------------------------------
QByteArray encodeWithLineEnding(QString text, const QString& lineEnding, TextEncoding encoding)
{
    if (!lineEnding.isEmpty() && lineEnding != unixLineEnding()) {
        text.replace(QStringLiteral("\n"), lineEnding);
    }

    switch (encoding) {
    case TextEncoding::Utf8Bom:
        return QByteArray("\xEF\xBB\xBF", 3) + text.toUtf8();
    case TextEncoding::Utf16Le: {
        const QByteArray bytes = QStringEncoder(QStringEncoder::Utf16LE).encode(text);
        return QByteArray("\xFF\xFE", 2) + bytes;
    }
    case TextEncoding::Utf16Be: {
        const QByteArray bytes = QStringEncoder(QStringEncoder::Utf16BE).encode(text);
        return QByteArray("\xFE\xFF", 2) + bytes;
    }
    case TextEncoding::Utf8:
        break;
    }
    return text.toUtf8();
}

// ---------------------------------------------------------------------------
QString normalizeMarkdownSpacing(const QString& text)
{
    QStringList lines = text.split('\n');
    QStringList out;
    out.reserve(lines.size());

    bool inFence = false;
    QChar fenceChar;
    int blankRun = 0;

    for (const QString& line : lines) {
        int pos = 0;
        while (pos < line.size() && (line.at(pos) == ' ' || line.at(pos) == '\t'))
            ++pos;

        int fenceCount = 0;
        if (pos < line.size() && (line.at(pos) == '`' || line.at(pos) == '~')) {
            fenceChar = line.at(pos);
            while (pos + fenceCount < line.size() && line.at(pos + fenceCount) == fenceChar)
                ++fenceCount;
        }

        if (fenceCount >= 3) {
            if (!inFence) {
                inFence = true;
            } else if (line.at(pos) == fenceChar) {
                inFence = false;
            }
            blankRun = 0;
            out.append(line);
            continue;
        }

        if (inFence) {
            out.append(line);
            continue;
        }

        if (line.trimmed().isEmpty()) {
            ++blankRun;
            if (blankRun == 1)
                out.append(QString());
            continue;
        }

        blankRun = 0;
        out.append(line);
    }

    return out.join('\n');
}

// ---------------------------------------------------------------------------
bool readTextFile(const QString& path, QString* content, QString* lineEnding,
                  TextEncoding* encoding)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = f.readAll();
    TextEncoding detected = TextEncoding::Utf8;
    const QString text = decodeAutoText(raw, &detected);
    if (encoding)
        *encoding = detected;
    if (lineEnding)
        *lineEnding = detectLineEnding(text);
    if (content)
        *content = normalizeLoadedText(text);
    return true;
}

// ---------------------------------------------------------------------------
EditableTextFileLoadResult loadEditableTextFile(const QString& path, QString* content,
                                                QString* lineEnding, TextEncoding* encoding)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return EditableTextFileLoadResult::ReadError;

    const auto definition = SyntaxDefinitionResolver::resolve(path, EditorMode::PlainText);
    static QMimeDatabase mimeDatabase;
    const auto mimeType = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    const bool isTextMimeType = mimeType.name().startsWith(QStringLiteral("text/"))
        || mimeType.inherits(QStringLiteral("text/plain"));
    if (!definition.isValid() && !isTextMimeType)
        return EditableTextFileLoadResult::NotEditable;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return EditableTextFileLoadResult::ReadError;

    const QByteArray raw = f.readAll();
    if (f.error() != QFile::NoError)
        return EditableTextFileLoadResult::ReadError;

    const TextEncoding detected = detectEncoding(raw);
    // Every second byte of ASCII-range UTF-16 is a NUL, so the binary guard
    // only applies to the 8-bit encodings.
    const bool utf16 = detected == TextEncoding::Utf16Le || detected == TextEncoding::Utf16Be;
    if (!utf16 && raw.contains('\0'))
        return EditableTextFileLoadResult::NotEditable;

    const QStringConverter::Encoding converterEncoding =
        detected == TextEncoding::Utf16Le ? QStringConverter::Utf16LE
        : detected == TextEncoding::Utf16Be ? QStringConverter::Utf16BE
        : QStringConverter::Utf8;
    QStringDecoder decoder(converterEncoding);
    QString text = decoder.decode(raw);
    if (decoder.hasError())
        return EditableTextFileLoadResult::NotEditable;
    const auto finalizeResult = decoder.finalize();
    if (finalizeResult.error != QStringConverter::FinalizeResultError::NoError || decoder.hasError())
        return EditableTextFileLoadResult::NotEditable;

    if (!text.isEmpty() && text.front() == QChar(0xFEFF))
        text.removeFirst();
    if (encoding)
        *encoding = detected;
    if (lineEnding)
        *lineEnding = detectLineEnding(text);
    if (content)
        *content = normalizeLoadedText(std::move(text));
    return EditableTextFileLoadResult::Success;
}
