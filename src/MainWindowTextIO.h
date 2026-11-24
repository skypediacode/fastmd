#pragma once

#include "TextEncoding.h"

#include <QString>
#include <QByteArray>

// Small text-encoding/line-ending helpers shared by MainWindowFile.cpp
// (loading/saving documents) and MainWindowSession.cpp / MainWindowAutoSave.cpp
// (session restore, autosave), so they need external linkage rather than
// staying file-local statics.
//
// detectLineEnding() takes *decoded* text on purpose: a UTF-16 document's
// bytes contain a stray 0x0D for every CRLF, so probing the raw bytes would
// report Classic Mac for an ordinary Windows file.
QString detectLineEnding(const QString& text);
TextEncoding detectEncoding(const QByteArray& data);
QString normalizeLoadedText(QString text);
QString decodeText(const QByteArray& raw, TextEncoding encoding);
QString decodeAutoText(const QByteArray& raw, TextEncoding* detected = nullptr);
QByteArray encodeWithLineEnding(QString text, const QString& lineEnding,
                                TextEncoding encoding = TextEncoding::Utf8);
QString normalizeMarkdownSpacing(const QString& text);
bool readTextFile(const QString& path, QString* content, QString* lineEnding,
                  TextEncoding* encoding = nullptr);

enum class EditableTextFileLoadResult {
    Success,
    NotEditable,
    ReadError,
};

EditableTextFileLoadResult loadEditableTextFile(const QString& path, QString* content,
                                                QString* lineEnding,
                                                TextEncoding* encoding = nullptr);
