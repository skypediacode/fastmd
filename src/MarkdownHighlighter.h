#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <vector>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QTextDocument* parent = nullptr);

    void setDarkMode(bool dark);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat    format;
        int                captureGroup = 0;
    };

    std::vector<Rule> m_rules;
    QTextCharFormat   m_codeBlockFmt;
    bool              m_dark = false;

    void buildRules();
};
