#include "SourceHighlighter.h"
#include "SyntaxHighlightingRepository.h"

SourceHighlighter::SourceHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    applyTheme();
}

void SourceHighlighter::applyTheme()
{
    auto& repo = syntaxRepository();
    setTheme(repo.defaultTheme(m_dark ? KSyntaxHighlighting::Repository::DarkTheme
                                       : KSyntaxHighlighting::Repository::LightTheme));
}

void SourceHighlighter::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    const bool wasModified = document()->isModified();
    m_dark = dark;
    applyTheme();
    rehighlight();
    document()->setModified(wasModified);
}

void SourceHighlighter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    const bool wasModified = document()->isModified();
    m_enabled = enabled;
    rehighlight();
    document()->setModified(wasModified);
}

void SourceHighlighter::setDefinition(const KSyntaxHighlighting::Definition& newDefinition)
{
    if (definition().name() == newDefinition.name())
        return;
    const bool wasModified = document()->isModified();
    KSyntaxHighlighting::AbstractHighlighter::setDefinition(newDefinition);
    if (m_enabled)
        rehighlight();
    document()->setModified(wasModified);
}

void SourceHighlighter::applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format)
{
    QTextCharFormat cf;
    if (format.hasTextColor(theme()))
        cf.setForeground(format.textColor(theme()));
    if (format.hasBackgroundColor(theme()))
        cf.setBackground(format.backgroundColor(theme()));
    if (format.isBold(theme()))
        cf.setFontWeight(QFont::Bold);
    if (format.isItalic(theme()))
        cf.setFontItalic(true);
    if (format.isUnderline(theme()))
        cf.setFontUnderline(true);
    if (format.isStrikeThrough(theme()))
        cf.setFontStrikeOut(true);
    setFormat(offset, length, cf);
}

void SourceHighlighter::applyFolding(int, int, KSyntaxHighlighting::FoldingRegion)
{
}

void SourceHighlighter::highlightBlock(const QString& text)
{
    if (!m_enabled || !definition().isValid()) {
        setCurrentBlockState(0);
        setCurrentBlockUserData(nullptr);
        return;
    }

    const auto* previous = currentBlock().previous().isValid()
        ? dynamic_cast<const BlockState*>(currentBlock().previous().userData())
        : nullptr;
    const KSyntaxHighlighting::State previousState = previous ? previous->state : KSyntaxHighlighting::State();

    auto* state = new BlockState;
    state->state = highlightLine(text, previousState);
    setCurrentBlockUserData(state);
    setCurrentBlockState(0);
}
