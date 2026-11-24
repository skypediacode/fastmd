#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

#include "MathRenderer.h" // MathLineShift

// Renders Markdown to HTML for the live Preview only (Export/EPUB/etc. keep using
// ExportManager::markdownToHtml()'s plain md_html() path unchanged). Unlike md_html(),
// this drives md4c's raw MD_PARSER callback API directly so it can tag block-level
// opening tags with data-src-start / data-src-end (1-based line numbers into `utf8`,
// counting '\n'), used by Preview double-click source highlighting.
//
// `utf8` is the *preprocessed* buffer (FastMdMath::preprocess() output): multi-line
// math spans have already been collapsed to single-line placeholder tokens, which
// would otherwise shift every source line after them out of sync. `lineShifts` (that
// same call's output) undoes that shift when translating a position in `utf8` back to
// a line number in the original Markdown.
namespace MarkdownPreviewRenderer {
QString render(const QByteArray& utf8, unsigned parserFlags,
                const QVector<MathLineShift>& lineShifts = {},
                const QVector<int>& lineOrigins = {});
}
