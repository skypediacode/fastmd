#pragma once

#include <QString>

// Byte encoding a document was loaded from and will be written back as.
// Editor buffers are always plain QString with '\n' line breaks; the encoding
// and the line ending only decide how bytes are decoded on load and produced
// on save, so neither can make a buffer dirty by itself.
enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
};

inline QString textEncodingLabel(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8Bom: return QStringLiteral("UTF-8 with BOM");
    case TextEncoding::Utf16Le: return QStringLiteral("UTF-16 LE");
    case TextEncoding::Utf16Be: return QStringLiteral("UTF-16 BE");
    case TextEncoding::Utf8:    break;
    }
    return QStringLiteral("UTF-8");
}

inline QString windowsLineEnding() { return QStringLiteral("\r\n"); }
inline QString unixLineEnding()    { return QStringLiteral("\n"); }
inline QString classicMacLineEnding() { return QStringLiteral("\r"); }

// CRLF / LF / CR, as shown in the status bar.
inline QString lineEndingLabel(const QString& lineEnding)
{
    if (lineEnding == classicMacLineEnding())
        return QStringLiteral("CR");
    if (lineEnding == unixLineEnding())
        return QStringLiteral("LF");
    return QStringLiteral("CRLF");
}
