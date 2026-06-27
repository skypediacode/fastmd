#include "MarkdownHighlighter.h"

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    buildRules();
}

void MarkdownHighlighter::setDarkMode(bool dark)
{
    m_dark = dark;
    buildRules();
    rehighlight();
}

void MarkdownHighlighter::buildRules()
{
    m_rules.clear();

    // colours that adapt to theme
    QColor headCol   = m_dark ? QColor(0x56, 0x9c, 0xd6) : QColor(0x00, 0x5c, 0xc5);
    QColor boldCol   = m_dark ? QColor(0xd4, 0xd4, 0xd4) : QColor(0x1a, 0x1a, 0x1a);
    QColor italicCol = m_dark ? QColor(0xd4, 0xd4, 0xd4) : QColor(0x1a, 0x1a, 0x1a);
    QColor codeCol   = m_dark ? QColor(0xce, 0x91, 0x78) : QColor(0xd7, 0x3a, 0x49);
    QColor codeBg    = m_dark ? QColor(0x2d, 0x2d, 0x2d) : QColor(0xf5, 0xf5, 0xf5);
    QColor linkCol   = m_dark ? QColor(0x4e, 0xc9, 0xb0) : QColor(0x03, 0x66, 0xd6);
    QColor bqCol     = m_dark ? QColor(0x88, 0x88, 0x88) : QColor(0x6a, 0x73, 0x7d);
    QColor listCol   = m_dark ? QColor(0xc5, 0x86, 0xc0) : QColor(0x6f, 0x42, 0xc1);
    QColor hrCol     = m_dark ? QColor(0x55, 0x55, 0x55) : QColor(0xcc, 0xcc, 0xcc);

    auto addRule = [&](const QString& pat, QTextCharFormat fmt, int cap = 0) {
        m_rules.push_back({ QRegularExpression(pat), fmt, cap });
    };

    // heading
    {
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        f.setForeground(headCol);
        addRule(QStringLiteral("^#{1,6}\\s+.*"), f);
    }

    // bold + italic  *** ... ***
    {
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        f.setFontItalic(true);
        f.setForeground(boldCol);
        addRule(QStringLiteral("\\*{3}[^*]+\\*{3}"), f);
        addRule(QStringLiteral("_{3}[^_]+_{3}"), f);
    }

    // bold  ** ... ** or __ ... __
    {
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        f.setForeground(boldCol);
        addRule(QStringLiteral("\\*{2}[^*]+\\*{2}"), f);
        addRule(QStringLiteral("_{2}[^_]+_{2}"), f);
    }

    // italic  * ... * or _ ... _
    {
        QTextCharFormat f;
        f.setFontItalic(true);
        f.setForeground(italicCol);
        addRule(QStringLiteral("(?<![*_])\\*(?![*\\s])[^*]+(?<!\\s|\\*)\\*(?![*])"), f);
        addRule(QStringLiteral("(?<![*_])_(?![_\\s])[^_]+(?<!\\s|_)_(?![_])"), f);
    }

    // inline code
    {
        QTextCharFormat f;
        f.setFontFamilies({QStringLiteral("JetBrains Mono"),
                           QStringLiteral("IBM Plex Mono"),
                           QStringLiteral("Cascadia Code"),
                           QStringLiteral("Consolas"),
                           QStringLiteral("monospace")});
        f.setForeground(codeCol);
        f.setBackground(codeBg);
        addRule(QStringLiteral("`[^`]+`"), f);
    }

    // link  [text](url)
    {
        QTextCharFormat f;
        f.setForeground(linkCol);
        f.setFontUnderline(true);
        addRule(QStringLiteral("\\[([^\\]]+)\\]\\([^)]+\\)"), f);
    }

    // image  ![alt](url)
    {
        QTextCharFormat f;
        f.setForeground(linkCol);
        addRule(QStringLiteral("!\\[[^\\]]*\\]\\([^)]+\\)"), f);
    }

    // blockquote
    {
        QTextCharFormat f;
        f.setForeground(bqCol);
        f.setFontItalic(true);
        addRule(QStringLiteral("^>.*"), f);
    }

    // unordered list marker
    {
        QTextCharFormat f;
        f.setForeground(listCol);
        f.setFontWeight(QFont::Bold);
        addRule(QStringLiteral("^(\\s*[-*+])\\s"), f);
    }

    // ordered list marker
    {
        QTextCharFormat f;
        f.setForeground(listCol);
        f.setFontWeight(QFont::Bold);
        addRule(QStringLiteral("^(\\s*\\d+\\.)\\s"), f);
    }

    // horizontal rule
    {
        QTextCharFormat f;
        f.setForeground(hrCol);
        addRule(QStringLiteral("^([-*_]\\s?){3,}$"), f);
    }

    // strikethrough  ~~text~~
    {
        QTextCharFormat f;
        f.setFontStrikeOut(true);
        f.setForeground(bqCol);
        addRule(QStringLiteral("~~[^~]+~~"), f);
    }

    // code block format (used in highlightBlock for fenced blocks)
    m_codeBlockFmt = QTextCharFormat{};
    m_codeBlockFmt.setFontFamilies({QStringLiteral("JetBrains Mono"),
                                    QStringLiteral("IBM Plex Mono"),
                                    QStringLiteral("Cascadia Code"),
                                    QStringLiteral("Consolas"),
                                    QStringLiteral("monospace")});
    m_codeBlockFmt.setBackground(codeBg);
    m_codeBlockFmt.setForeground(m_dark ? QColor(0xd4, 0xd4, 0xd4) : QColor(0x24, 0x29, 0x2e));
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    // --- fenced code block (multi-line state) ---
    // state 0 = normal, state 1 = inside ``` block
    bool wasInBlock = (previousBlockState() == 1);

    if (text.startsWith(QStringLiteral("```"))) {
        if (wasInBlock) {
            setCurrentBlockState(0);
        } else {
            setCurrentBlockState(1);
        }
        setFormat(0, text.length(), m_codeBlockFmt);
        return;
    }

    if (wasInBlock) {
        setCurrentBlockState(1);
        setFormat(0, text.length(), m_codeBlockFmt);
        return;
    }

    setCurrentBlockState(0);

    // --- apply inline rules ---
    for (const auto& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
}
