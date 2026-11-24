#pragma once
#include <QString>
#include "DocumentModel.h"

// Immutable snapshot of document statistics, computed once when the
// View -> Document Statistics dialog opens. Never wired to editor edit signals.
struct DocumentSummary {
    // General (all modes)
    qsizetype characterCount = 0;
    int wordCount = 0;
    int lineCount = 0;
    int selectedCharacterCount = 0;
    int selectedWordCount = 0;

    // Markdown-only (isMarkdown == true); zero otherwise.
    bool isMarkdown = false;
    int h1Count = 0;        // MD_BLOCK_H, level 1
    int h2Count = 0;        // MD_BLOCK_H, level 2
    int h3Count = 0;        // MD_BLOCK_H, level 3
    int linkCount = 0;      // MD_SPAN_A (includes autolinks and reference links)
    int imageCount = 0;     // MD_SPAN_IMG
    int tableCount = 0;     // MD_BLOCK_TABLE
    int codeBlockCount = 0; // MD_BLOCK_CODE (fenced and indented)
    int equationCount = 0;  // FastMdMath-recognized inline/display math spans
};

namespace DocumentSummaryAnalyzer {
// Shared by the status bar and the Document Statistics dialog: contiguous runs
// of non-space characters, split on QChar::isSpace().
int countWords(QStringView text);

// `text` and `selectedText` are snapshots (e.g. editor->toPlainText() and
// textCursor().selectedText()); `mode` selects whether the Markdown section
// is populated. Pass EditorMode::Markdown or EditorMode::PlainText only --
// Pdf/Html tabs never reach this (the View action is disabled for them).
DocumentSummary analyze(const QString& text, const QString& selectedText, EditorMode mode);
}
