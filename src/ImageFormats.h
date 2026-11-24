#pragma once
#include <QSet>
#include <QString>
#include <QStringView>

namespace ImageFormats {

// Authoritative, runtime-derived set (plugin enumeration -- not free).
const QSet<QString>& supportedExtensions();

// Cheap suffix test. Rejects non-image suffixes without touching QImageReader,
// so the startup path never pays for plugin enumeration.
bool isImageExtension(QStringView suffix);

bool isImageFile(const QString& path);

// Space-separated "*.ext" glob list (sorted), the same patterns openDialogFilter()
// wraps in a translated "Images (...)" label -- exposed separately so callers
// building a combined filter (e.g. "All Supported Files") don't re-derive it.
QString openDialogPatterns();
QString openDialogFilter();

}
