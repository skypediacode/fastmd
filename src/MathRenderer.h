#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

enum class MathRenderMode {
    UnicodePreview,
    KatexOutput
};

// Multi-line math spans ($$...$$, \[...\]) collapse to a single-line placeholder
// during preprocess(), which shifts every source line after them. Records, for
// each such collapse, the placeholder's line in the *preprocessed* text
// (1-based) and how many original lines were removed there -- used to map
// preprocessed-text line numbers back to original source lines (see
// MarkdownPreviewRenderer).
struct MathLineShift {
    int atLine;
    int deltaLines;
};

namespace FastMdMath {
// Optional literalSnippets keeps source spelling for placeholders that MD4C
// subsequently classifies as code (e.g. indented code in a nested list).
QString preprocess(const QString& markdown, MathRenderMode mode, QStringList* mathSnippets,
                    QVector<MathLineShift>* lineShifts = nullptr,
                    QStringList* literalSnippets = nullptr);
}

