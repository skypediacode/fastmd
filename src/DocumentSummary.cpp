#include "DocumentSummary.h"
#include "MathRenderer.h"

#include "md4c.h"

namespace DocumentSummaryAnalyzer {

int countWords(QStringView text)
{
    int words = 0;
    bool inWord = false;
    for (QChar ch : text) {
        if (ch.isSpace()) {
            inWord = false;
            continue;
        }
        if (!inWord) {
            ++words;
            inWord = true;
        }
    }
    return words;
}

namespace {

struct MarkdownCounts {
    int h1Count = 0;
    int h2Count = 0;
    int h3Count = 0;
    int linkCount = 0;
    int imageCount = 0;
    int tableCount = 0;
    int codeBlockCount = 0;
};

int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* counts = static_cast<MarkdownCounts*>(userdata);
    switch (type) {
    case MD_BLOCK_H: {
        const auto* h = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
        if (h->level == 1)
            ++counts->h1Count;
        else if (h->level == 2)
            ++counts->h2Count;
        else if (h->level == 3)
            ++counts->h3Count;
        break;
    }
    case MD_BLOCK_TABLE:
        ++counts->tableCount;
        break;
    case MD_BLOCK_CODE:
        ++counts->codeBlockCount;
        break;
    default:
        break;
    }
    return 0;
}

int leaveBlock(MD_BLOCKTYPE, void*, void*) { return 0; }

int enterSpan(MD_SPANTYPE type, void*, void* userdata)
{
    auto* counts = static_cast<MarkdownCounts*>(userdata);
    if (type == MD_SPAN_A)
        ++counts->linkCount;
    else if (type == MD_SPAN_IMG)
        ++counts->imageCount;
    return 0;
}

int leaveSpan(MD_SPANTYPE, void*, void*) { return 0; }

int onText(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*) { return 0; }

MarkdownCounts analyzeMarkdownStructure(const QString& markdown)
{
    MarkdownCounts counts;
    const QByteArray utf8 = markdown.toUtf8();

    MD_PARSER parser {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = onText;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), &parser, &counts);
    return counts;
}

} // namespace

DocumentSummary analyze(const QString& text, const QString& selectedText, EditorMode mode)
{
    DocumentSummary summary;
    summary.characterCount = text.size();
    summary.wordCount = countWords(QStringView(text));
    summary.lineCount = text.count(QLatin1Char('\n')) + 1;
    summary.selectedCharacterCount = selectedText.size();
    summary.selectedWordCount = countWords(QStringView(selectedText));

    summary.isMarkdown = (mode == EditorMode::Markdown);
    if (!summary.isMarkdown)
        return summary;

    QStringList mathSnippets;
    const QString mathPreprocessed =
        FastMdMath::preprocess(text, MathRenderMode::KatexOutput, &mathSnippets);
    summary.equationCount = mathSnippets.size();

    const MarkdownCounts structure = analyzeMarkdownStructure(mathPreprocessed);
    summary.h1Count = structure.h1Count;
    summary.h2Count = structure.h2Count;
    summary.h3Count = structure.h3Count;
    summary.linkCount = structure.linkCount;
    summary.imageCount = structure.imageCount;
    summary.tableCount = structure.tableCount;
    summary.codeBlockCount = structure.codeBlockCount;

    return summary;
}

} // namespace DocumentSummaryAnalyzer
