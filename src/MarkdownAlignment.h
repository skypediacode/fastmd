#pragma once

#include <QString>

// One definition of FastMD's text-alignment wrapper syntax, shared by the editor
// command that writes it (MarkdownFormatting.cpp) and the preview pipeline that
// has to recognize it (ExportManagerMarkdown.cpp). The patterns follow
// CommonMark's HTML-block rule: at most three spaces of indent, tag alone on its
// line. Keep both sides on these constants -- a divergence here silently makes an
// alignment the editor wrote unrecognizable to the preview.
namespace MarkdownAlignment {

inline QString openPattern()
{
    return QStringLiteral(
        R"(^ {0,3}<div\s+style\s*=\s*["']\s*text-align\s*:\s*(left|center|right|justify)\s*;?\s*["']\s*>[ \t]*$)");
}

inline QString anyOpenPattern()
{
    return QStringLiteral(R"(^ {0,3}<div\b[^>]*>[ \t]*$)");
}

inline QString closePattern()
{
    return QStringLiteral(R"(^ {0,3}</div\s*>[ \t]*$)");
}

inline QString openTag(const QString& alignment)
{
    return QStringLiteral("<div style=\"text-align: %1;\">").arg(alignment);
}

} // namespace MarkdownAlignment
