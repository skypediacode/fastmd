// Math Toolbar snippet insertion: inserts LaTeX templates produced by
// MathSymbols.h and turns their «stop» markers into Tab-navigable
// QTextCursor selections. Kept separate from MarkdownFormatting.cpp because
// it owns the snippet-stop navigation state.
#include "MarkdownEditor.h"
#include "MathContext.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QPair>
#include <QVector>

namespace {

// Shared by insertMathSnippet() and insertChemistrySnippet(): once
// MathContext::at() has ruled out an existing $...$/$$...$$ span, decide
// whether the notation goes inline or on its own display block by looking at
// the line(s) the caret/selection touches. A selection is "empty" once its
// own text is excluded, so replacing a whole blank-looking line still counts.
enum class WrapKind { Raw, Inline, Block };

bool blockIsEmptyExcluding(const QTextBlock& block, int localStart, int localEnd)
{
    const QString text = block.text();
    return (text.left(localStart) + text.mid(localEnd)).trimmed().isEmpty();
}

WrapKind decideWrapKind(const QTextCursor& c, MathContext::Kind mathKind)
{
    if (mathKind != MathContext::Kind::None)
        return WrapKind::Raw;

    const QTextDocument* doc = c.document();
    const QTextBlock startBlock = doc->findBlock(c.selectionStart());
    const QTextBlock endBlock = doc->findBlock(c.selectionEnd());
    if (startBlock.blockNumber() != endBlock.blockNumber())
        return WrapKind::Block;

    const int blockPos = startBlock.position();
    const bool empty = blockIsEmptyExcluding(startBlock, c.selectionStart() - blockPos, c.selectionEnd() - blockPos);
    return empty ? WrapKind::Block : WrapKind::Inline;
}

struct Placement {
    QString finalText;
    int bodyOffset; // offset from the insertion point to the start of `body` within finalText
};

Placement placeNotation(const QString& body, WrapKind kind)
{
    switch (kind) {
    case WrapKind::Raw:
        return { body, 0 };
    case WrapKind::Block:
        return { QStringLiteral("$$\n%1\n$$").arg(body), 3 };
    case WrapKind::Inline:
    default:
        return { QStringLiteral("$%1$").arg(body), 1 };
    }
}

// Strips «...» stop markers from a snippet template, returning the plain
// text plus each stop's (start, end) offset within that plain text.
QString stripSnippetStops(const QString& tpl, QVector<QPair<int, int>>& stops)
{
    QString out;
    out.reserve(tpl.size());
    int i = 0;
    while (i < tpl.size()) {
        const QChar c = tpl.at(i);
        if (c == QChar(0x00AB)) { // «
            const int close = tpl.indexOf(QChar(0x00BB), i + 1); // »
            if (close < 0) {
                out += tpl.mid(i);
                break;
            }
            const int start = out.size();
            out += tpl.mid(i + 1, close - i - 1);
            stops.append(qMakePair(start, out.size()));
            i = close + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

QString applySelectionToTemplate(QString templateLatex, const QString& selection)
{
    if (selection.isEmpty())
        return templateLatex;

    // The fraction templates have two editable arguments. A selected expression
    // belongs in the numerator and the remaining argument becomes the next stop.
    if (templateLatex == QStringLiteral("\\frac{x}{y}"))
        return QStringLiteral("\\frac{%1}{\u00AB\u00BB}").arg(selection);
    if (templateLatex == QStringLiteral("\\dfrac{x}{y}"))
        return QStringLiteral("\\dfrac{%1}{\u00AB\u00BB}").arg(selection);

    // Most structural templates use x as their primary argument. Retaining the
    // template around the selection makes roots, scripts, accents, and brackets
    // behave as a true wrap operation rather than replacing the selected math.
    const int argument = templateLatex.indexOf(QLatin1Char('x'));
    if (argument >= 0)
        templateLatex.replace(argument, 1, selection);
    return templateLatex;
}

} // namespace

void MarkdownEditor::clearSnippetStops()
{
    m_snippetStops.clear();
    m_snippetStopIndex = -1;
}

void MarkdownEditor::insertMathSnippet(const QString& templateLatex)
{
    QTextCursor c = textCursor();
    QString selected = c.selectedText();
    selected.replace(QChar(0x2029), QLatin1Char('\n')); // QTextCursor::selectedText() uses U+2029 for line breaks
    const MathContext::Result context = MathContext::at(document(), c.selectionStart(), c.selectionEnd());

    QVector<QPair<int, int>> relativeStops;
    const QString body = stripSnippetStops(applySelectionToTemplate(templateLatex, selected), relativeStops);
    // insertMatrix() pre-wraps its body in "$$\n...\n$$" itself (a matrix is
    // inherently block content); treat that as already-placed raw notation
    // rather than re-running line-emptiness detection on it.
    const bool displayTemplate = body.startsWith(QStringLiteral("$$\n"))
        && body.endsWith(QStringLiteral("\n$$"));
    const Placement placement = displayTemplate ? Placement{ body, 0 }
                                                 : placeNotation(body, decideWrapKind(c, context.kind));
    const QString finalText = placement.finalText;
    const int bodyOffset = placement.bodyOffset;

    c.beginEditBlock();
    const int insertPos = c.selectionStart();
    if (c.hasSelection())
        c.removeSelectedText();
    c.insertText(finalText);
    c.endEditBlock();

    clearSnippetStops();
    for (const auto& stop : relativeStops) {
        QTextCursor stopCursor(document());
        stopCursor.setPosition(insertPos + bodyOffset + stop.first);
        stopCursor.setPosition(insertPos + bodyOffset + stop.second, QTextCursor::KeepAnchor);
        m_snippetStops.append(stopCursor);
    }

    if (!m_snippetStops.isEmpty()) {
        m_snippetStopIndex = 0;
        setTextCursor(m_snippetStops.first());
    } else {
        QTextCursor endCursor(document());
        endCursor.setPosition(insertPos + finalText.size());
        setTextCursor(endCursor);
    }
}

void MarkdownEditor::insertChemistrySnippet(const QString& templateLatex)
{
    QVector<QPair<int, int>> relativeStops;
    QString body = stripSnippetStops(templateLatex, relativeStops);

    QTextCursor c = textCursor();
    QString selectedText = c.hasSelection() ? c.selectedText() : QString();
    selectedText.replace(QChar(0x2029), QLatin1Char('\n')); // QTextCursor::selectedText() uses U+2029 for line breaks
    const MathContext::Result context = MathContext::at(document(), c.selectionStart(), c.selectionEnd());

    bool substituted = false;
    if (!selectedText.isEmpty() && !relativeStops.isEmpty()) {
        const QPair<int, int>& stop = relativeStops.first();
        body = body.left(stop.first) + selectedText + body.mid(stop.second);
        substituted = true;
    }

    const Placement placement = placeNotation(body, decideWrapKind(c, context.kind));
    const QString finalText = placement.finalText;
    const int bodyOffset = placement.bodyOffset;

    c.beginEditBlock();
    const int insertPos = c.selectionStart();
    if (c.hasSelection())
        c.removeSelectedText();
    c.insertText(finalText);
    c.endEditBlock();

    clearSnippetStops();

    if (substituted) {
        QTextCursor endCursor(document());
        endCursor.setPosition(insertPos + bodyOffset + relativeStops.first().first + selectedText.size());
        setTextCursor(endCursor);
        return;
    }

    for (const auto& stop : relativeStops) {
        QTextCursor stopCursor(document());
        stopCursor.setPosition(insertPos + bodyOffset + stop.first);
        stopCursor.setPosition(insertPos + bodyOffset + stop.second, QTextCursor::KeepAnchor);
        m_snippetStops.append(stopCursor);
    }

    if (!m_snippetStops.isEmpty()) {
        m_snippetStopIndex = 0;
        setTextCursor(m_snippetStops.first());
    } else {
        QTextCursor endCursor(document());
        endCursor.setPosition(insertPos + finalText.size());
        setTextCursor(endCursor);
    }
}

QString MarkdownEditor::matrixLatexBody(int rows, int cols, const QString& env)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);

    QString body;
    for (int r = 1; r <= rows; ++r) {
        body += QStringLiteral("    ");
        for (int c = 1; c <= cols; ++c) {
            if (c > 1)
                body += QStringLiteral(" & ");
            body += QStringLiteral("\u00ABa_{%1%2}\u00BB").arg(r).arg(c);
        }
        body += (r < rows) ? QStringLiteral(" \\\\\n") : QStringLiteral("\n");
    }

    return QStringLiteral("\\begin{%1}\n%2\\end{%1}").arg(env, body);
}

void MarkdownEditor::insertMatrix(int rows, int cols, const QString& env)
{
    const QString matrixLatex = matrixLatexBody(rows, cols, env);

    const bool insideMath = MathContext::at(textCursor()).kind != MathContext::Kind::None;
    insertMathSnippet(insideMath ? matrixLatex : QStringLiteral("$$\n%1\n$$").arg(matrixLatex));
}
