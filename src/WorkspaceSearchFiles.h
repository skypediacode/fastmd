#pragma once

#include "TextEncoding.h"

#include <QString>

// Disk access for "Search in Files". Both directions go through the same
// encoding helpers Open and Save use (MainWindowTextIO), so a file replaced
// without ever being opened keeps its BOM, its UTF-16 byte order and its line
// ending. Every function here is safe to call from a worker thread.
namespace WorkspaceSearchFiles {

struct LoadedText {
    QString text;         // '\n' separated, BOM stripped
    QString lineEnding;
    TextEncoding encoding = TextEncoding::Utf8;
};

// False for anything that is not readable, decodable text: a binary file (NUL
// bytes in an 8-bit encoding), an invalid byte sequence, or a file larger than
// `maxBytes`.
bool load(const QString& path, LoadedText* out, qint64 maxBytes);

// Atomic (QSaveFile) write in the file's own encoding and line ending.
bool save(const QString& path, const LoadedText& file);

}  // namespace WorkspaceSearchFiles
