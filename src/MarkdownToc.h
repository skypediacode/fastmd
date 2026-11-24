#pragma once

#include <QVector>
#include <QString>
#include <QStringList>

namespace MarkdownToc {

struct Heading {
    int level = 0;
    QString title;
    QString slug;
};

struct Result {
    QString expandedMarkdown;
    // For every line in expandedMarkdown, the 1-based line it originated from
    // in the Markdown passed to analyze(). Generated TOC entries all point at
    // their single [TOC] placeholder.
    QVector<int> expandedLineOrigins;
    QStringList slugs;
    QString tocMarkdown;
    QVector<Heading> headings;
    QVector<int> placeholderLineNumbers;
};

Result analyze(const QString& markdown);
QString render(const Result& result, const QString& lineEnding = QStringLiteral("\n"));
bool hasHeadings(const QString& markdown);

} // namespace MarkdownToc
