#pragma once

#include <QString>
#include <QStringList>

namespace FenceLanguageAliases {

// Maps a fenced-code language tag to the file name whose KSyntaxHighlighting
// definition highlights it. Returns an empty string for tags with no
// definition. Lookup is case-insensitive.
//
// Entries are explicit on purpose: resolving every highlight.js alias through
// file extensions maps some tags to the wrong language (fs -> Forth, ps ->
// PostScript, pp -> Pascal) or to several claimants at once.
QString fileNameForTag(const QString& tag);
QStringList tags();

}
