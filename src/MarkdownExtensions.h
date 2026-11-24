#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace MarkdownExtensions {

QString preprocess(const QString& markdown, QStringList* htmlSnippets,
                   const QVector<int>* inputLineOrigins = nullptr,
                   QVector<int>* outputLineOrigins = nullptr,
                   bool sourceMapping = false);
QString restore(QString html, const QStringList& htmlSnippets);
bool isPageBreakLine(const QString& line);

} // namespace MarkdownExtensions
