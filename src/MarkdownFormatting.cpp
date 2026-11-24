// Markdown-syntax-inserting toolbar/menu commands (bold, italic, headings,
// lists, tables, etc.) for MarkdownEditor. These are plain QTextCursor edits
// with no dependency on any other editor member state, so they're split into
// their own translation unit purely to shrink MarkdownEditor.cpp -- the
// methods are still declared in MarkdownEditor.h like any other member.
#include "MarkdownEditor.h"
#include "MarkdownInlineFormatting.h"
#include "MarkdownToc.h"

#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QRegularExpression>

#include "MarkdownAlignment.h"

namespace {

void wrapInline(QPlainTextEdit* ed, const QString& marker)
{
    QTextCursor c = ed->textCursor();
    if (c.hasSelection()) {
        QString sel = c.selectedText();
        c.insertText(marker + sel + marker);
    } else {
        int pos = c.position();
        c.insertText(marker + marker);
        c.setPosition(pos + marker.length());
        ed->setTextCursor(c);
    }
}

bool isFootnoteId(const QString& id)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9_.-]*$)"));
    return pattern.match(id).hasMatch();
}

bool hasFootnoteDefinition(const QString& text, const QString& id)
{
    const QRegularExpression pattern(
        QStringLiteral(R"(^\s{0,3}\[\^%1\]:)").arg(QRegularExpression::escape(id)),
        QRegularExpression::MultilineOption);
    return pattern.match(text).hasMatch();
}

void prependLines(QPlainTextEdit* ed, const QString& prefix)
{
    QTextCursor c = ed->textCursor();
    c.beginEditBlock();
    int start = c.selectionStart();
    int end   = c.selectionEnd();
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    while (c.position() <= end || c.block().position() <= end) {
        c.insertText(prefix);
        end += prefix.length();
        if (!c.movePosition(QTextCursor::NextBlock)) break;
    }
    c.endEditBlock();
    ed->setTextCursor(c);
}

void prependChecklistLines(MarkdownEditor* ed)
{
    QTextCursor selection = ed->textCursor();
    const int start = selection.selectionStart();
    const int end = selection.selectionEnd();
    const int currentBlock = selection.blockNumber();
    QTextBlock first = ed->document()->findBlock(start);
    QTextBlock last = ed->document()->findBlock(end);
    if (!first.isValid() || !last.isValid())
        return;

    QList<QTextBlock> blocks;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        blocks.append(block);
        if (block == last)
            break;
    }

    static const QRegularExpression task(
        QStringLiteral(R"(^([ \t]*)([-+*])[ \t]+\[[ xX]\]([ \t]*)(.*)$)"));
    static const QRegularExpression bullet(
        QStringLiteral(R"(^([ \t]*)(?:[-+*]|\d+[.)])[ \t]+(.*)$)"));

    selection.beginEditBlock();
    for (auto it = blocks.crbegin(); it != blocks.crend(); ++it) {
        const QString line = it->text();
        if (task.match(line).hasMatch())
            continue;

        QString replacement;
        const QRegularExpressionMatch bulletMatch = bullet.match(line);
        if (bulletMatch.hasMatch())
            replacement = bulletMatch.captured(1) + QStringLiteral("- [ ] ") + bulletMatch.captured(2);
        else
            replacement = QStringLiteral("- [ ] ") + line;
        if (replacement == line)
            continue;

        QTextCursor lineCursor(*it);
        lineCursor.movePosition(QTextCursor::StartOfBlock);
        lineCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        lineCursor.insertText(replacement);
    }
    selection.endEditBlock();

    if (selection.hasSelection()) {
        const QTextBlock updatedLast = ed->document()->findBlockByNumber(
            first.blockNumber() + blocks.size() - 1);
        selection.setPosition(first.position());
        selection.setPosition(updatedLast.position() + updatedLast.length(),
                              QTextCursor::KeepAnchor);
    } else {
        const QTextBlock updated = ed->document()->findBlockByNumber(currentBlock);
        selection.setPosition(updated.position() + updated.text().length());
    }
    ed->setTextCursor(selection);
}

// Set the heading level on the current line (toggles off if already at that level).
void setHeadingLevel(QPlainTextEdit* ed, int level)
{
    static QRegularExpression rxH(QStringLiteral("^(#{1,6}) "));
    QTextCursor c = ed->textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    QString line = c.block().text();
    QRegularExpressionMatch m = rxH.match(line);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    if (m.hasMatch()) {
        QString rest = line.mid(m.capturedLength());
        if (m.captured(1).length() == level)
            c.insertText(rest);                              // toggle off
        else
            c.insertText(QString(level, '#') + QStringLiteral(" ") + rest);
    } else {
        c.insertText(QString(level, '#') + QStringLiteral(" ") + line);
    }
    c.endEditBlock();
}

struct TextRange {
    int start = 0;
    int end = 0;
    bool isValid() const { return end > start; }
};

bool isBlockMarker(const QString& line)
{
    static const QRegularExpression marker(
        QStringLiteral(R"(^\s*(?:#{1,6}\s|[-+*]\s+|\d+[.)]\s+|>\s?|```|~~~|\$\$|\\\[|</?div\b))"),
        QRegularExpression::CaseInsensitiveOption);
    return marker.match(line).hasMatch() || line.trimmed().startsWith(QStringLiteral("!["));
}

// Fenced code and display math are verbatim regions whose contents must never be
// treated as Markdown blocks. Which side of a fence a line sits on can only be
// resolved by scanning from the document start: a ``` line directly above the
// cursor is just as likely to be a closing fence as an opening one.
TextRange enclosingVerbatimRegion(const QTextDocument* document, int position)
{
    QTextBlock openBlock;
    QString closer;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text().trimmed();
        if (!openBlock.isValid()) {
            if (block.position() > position)
                return {};
            if (text.startsWith(QStringLiteral("```")) || text.startsWith(QStringLiteral("~~~"))) {
                openBlock = block;
                closer = text.left(3);
            } else if (text == QStringLiteral("$$") || text == QStringLiteral("\\[")) {
                openBlock = block;
                closer = text == QStringLiteral("$$") ? QStringLiteral("$$") : QStringLiteral("\\]");
            }
            continue;
        }
        const bool fenced = closer.startsWith(QLatin1Char('`')) || closer.startsWith(QLatin1Char('~'));
        if (fenced ? !text.startsWith(closer) : text != closer)
            continue;
        const int end = block.position() + static_cast<int>(block.text().size());
        if (position >= openBlock.position() && position <= end)
            return {openBlock.position(), end};
        openBlock = QTextBlock();
    }
    // An unterminated region owns the rest of the document.
    if (openBlock.isValid() && position >= openBlock.position()) {
        const QTextBlock last = document->lastBlock();
        return {openBlock.position(), last.position() + static_cast<int>(last.text().size())};
    }
    return {};
}

// The Markdown block the cursor sits in: a verbatim region, a run of list items,
// a single already-delimited block, or the run of ordinary lines around it.
TextRange logicalBlock(const QTextDocument* document, const QTextBlock& block)
{
    if (!block.isValid())
        return {};
    if (const TextRange verbatim = enclosingVerbatimRegion(document, block.position()); verbatim.isValid())
        return verbatim;

    const QString current = block.text();
    QTextBlock first = block;
    QTextBlock last = block;

    if (isBlockMarker(current)) {
        // Headings, images and HTML lines are already single blocks; only
        // consecutive list items need to be kept together.
        static const QRegularExpression listItem(QStringLiteral(R"(^\s*(?:[-+*]|\d+[.)])\s+)"));
        if (listItem.match(current).hasMatch()) {
            while (first.previous().isValid() && listItem.match(first.previous().text()).hasMatch())
                first = first.previous();
            while (last.next().isValid() && listItem.match(last.next().text()).hasMatch())
                last = last.next();
        }
    } else {
        // A paragraph is the run of ordinary, non-blank lines around the cursor.
        // Stop at a neighbouring Markdown block even if the author omitted its
        // optional separating blank line.
        while (first.previous().isValid() && !first.previous().text().trimmed().isEmpty()
               && !isBlockMarker(first.previous().text()))
            first = first.previous();
        while (last.next().isValid() && !last.next().text().trimmed().isEmpty()
               && !isBlockMarker(last.next().text()))
            last = last.next();
    }
    return {first.position(), last.position() + static_cast<int>(last.text().size())};
}

struct AlignmentWrapper {
    int openStart = 0;
    int openEnd = 0;
    int closeStart = 0;
    int closeEnd = 0;
    QString alignment;
};

// The wrapper enclosing [start, end], or none. Nesting is tracked so a plain
// <div> inside an aligned block does not steal its closing tag.
bool enclosingAlignmentWrapper(const QTextDocument* document, const QString& source,
                               int start, int end, AlignmentWrapper* out)
{
    if (!source.contains(QLatin1String("<div")))
        return false;

    static const QRegularExpression openAlignment(MarkdownAlignment::openPattern(),
                                                  QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression openDiv(MarkdownAlignment::anyOpenPattern(),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression closeDiv(MarkdownAlignment::closePattern(),
                                             QRegularExpression::CaseInsensitiveOption);

    struct Pending { int start; int end; QString alignment; };
    QList<Pending> stack;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QString line = block.text();
        if (!line.contains(QLatin1Char('<')))
            continue;
        const int lineStart = block.position();
        const int lineEnd = lineStart + static_cast<int>(line.size());
        const QRegularExpressionMatch match = openAlignment.match(line);
        if (match.hasMatch()) {
            stack.append({lineStart, lineEnd, match.captured(1).toLower()});
        } else if (openDiv.match(line).hasMatch()) {
            stack.append({lineStart, lineEnd, QString()});
        } else if (closeDiv.match(line).hasMatch() && !stack.isEmpty()) {
            const Pending pending = stack.takeLast();
            if (!pending.alignment.isEmpty() && pending.start <= start && lineEnd >= end) {
                *out = {pending.start, pending.end, lineStart, lineEnd, pending.alignment};
                return true;
            }
        }
    }
    return false;
}

// The editor buffer is always '\n'-separated, so a document position is also a
// QString offset and a line break is always one character.
int skipLineBreakForward(const QString& text, int position)
{
    return position < text.size() && text.at(position) == QLatin1Char('\n') ? position + 1 : position;
}

int skipLineBreakBackward(const QString& text, int position)
{
    return position > 0 && text.at(position - 1) == QLatin1Char('\n') ? position - 1 : position;
}

void applyAlignment(MarkdownEditor* editor, const QString& alignment)
{
    QTextDocument* document = editor->document();
    const QString source = editor->toPlainText();
    QTextCursor selection = editor->textCursor();
    const int originalStart = selection.selectionStart();
    const int originalEnd = qMax(originalStart, selection.selectionEnd());
    QTextBlock first = document->findBlock(originalStart);
    QTextBlock last = document->findBlock(originalEnd);
    if (originalEnd > originalStart && last.isValid() && originalEnd == last.position())
        last = last.previous();
    if (!first.isValid() || !last.isValid() || (first == last && first.text().trimmed().isEmpty()))
        return;

    const bool isLeft = alignment == QStringLiteral("left");

    // Prefer an existing enclosing wrapper: that both re-aligns in place and
    // stops a second wrapper being nested around the same block.
    AlignmentWrapper wrapper;
    if (enclosingAlignmentWrapper(document, source, originalStart, originalEnd, &wrapper)) {
        QTextCursor edit(document);
        if (!isLeft) {
            if (wrapper.alignment == alignment)
                return;
            edit.setPosition(wrapper.openStart);
            edit.setPosition(wrapper.openEnd, QTextCursor::KeepAnchor);
            edit.beginEditBlock();
            edit.insertText(MarkdownAlignment::openTag(alignment));
            edit.endEditBlock();
            editor->setTextCursor(selection);
            return;
        }

        // Left is Markdown's default, so aligning left unwraps instead of
        // writing a redundant div. Drop the blank padding this command emits
        // but leave blank lines the author wrote inside the block alone.
        int bodyStart = skipLineBreakForward(source, wrapper.openEnd);
        bodyStart = skipLineBreakForward(source, bodyStart);
        int bodyEnd = skipLineBreakBackward(source, wrapper.closeStart);
        bodyEnd = skipLineBreakBackward(source, bodyEnd);
        const QString body = source.mid(bodyStart, qMax(0, bodyEnd - bodyStart));
        edit.setPosition(wrapper.openStart);
        edit.setPosition(wrapper.closeEnd, QTextCursor::KeepAnchor);
        edit.beginEditBlock();
        edit.insertText(body);
        edit.endEditBlock();
        const auto mapPosition = [&](int position) {
            return wrapper.openStart + qBound(0, position - bodyStart, static_cast<int>(body.size()));
        };
        selection.setPosition(mapPosition(originalStart));
        selection.setPosition(mapPosition(originalEnd), QTextCursor::KeepAnchor);
        editor->setTextCursor(selection);
        return;
    }

    if (isLeft)
        return;

    const TextRange target { logicalBlock(document, first).start, logicalBlock(document, last).end };
    if (!target.isValid())
        return;

    const QString body = source.mid(target.start, target.end - target.start);
    const QString prefix = MarkdownAlignment::openTag(alignment) + QStringLiteral("\n\n");
    const QString suffix = QStringLiteral("\n\n</div>");
    QTextCursor edit(document);
    edit.setPosition(target.start);
    edit.setPosition(target.end, QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.insertText(prefix + body + suffix);
    edit.endEditBlock();

    const auto mapPosition = [&](int position) {
        return target.start + static_cast<int>(prefix.size())
             + qBound(0, position - target.start, static_cast<int>(body.size()));
    };
    selection.setPosition(mapPosition(originalStart));
    selection.setPosition(mapPosition(originalEnd), QTextCursor::KeepAnchor);
    editor->setTextCursor(selection);
}

} // namespace

void MarkdownEditor::fmtBold()        { wrapInline(this, QStringLiteral("**")); }
void MarkdownEditor::fmtItalic()      { wrapInline(this, QStringLiteral("*")); }
void MarkdownEditor::fmtStrike()      { wrapInline(this, QStringLiteral("~~")); }
void MarkdownEditor::fmtHighlight()   { QTextCursor cursor = textCursor(); MarkdownInlineFormatting::toggleMark(cursor); setTextCursor(cursor); }
void MarkdownEditor::fmtSubscript()   { wrapInline(this, QStringLiteral("~")); }
void MarkdownEditor::fmtSuperscript() { wrapInline(this, QStringLiteral("^")); }
void MarkdownEditor::fmtAlignLeft()    { applyAlignment(this, QStringLiteral("left")); }
void MarkdownEditor::fmtAlignCenter()  { applyAlignment(this, QStringLiteral("center")); }
void MarkdownEditor::fmtAlignRight()   { applyAlignment(this, QStringLiteral("right")); }
void MarkdownEditor::fmtAlignJustify() { applyAlignment(this, QStringLiteral("justify")); }
void MarkdownEditor::fmtCode()        { wrapInline(this, QStringLiteral("`")); }

void MarkdownEditor::fmtFootnote()
{
    QTextCursor c = textCursor();
    const QString source = toPlainText();
    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");

    int number = 1;
    QString id;
    do {
        id = QStringLiteral("fn%1").arg(number++);
    } while (source.contains(QStringLiteral("[^") + id + QStringLiteral("]")));

    c.beginEditBlock();
    c.insertText(QStringLiteral("[^") + id + QStringLiteral("]"));
    c.movePosition(QTextCursor::End);

    const QString documentText = toPlainText();
    if (!documentText.isEmpty()) {
        if (!documentText.endsWith(QStringLiteral("\n\n")))
            c.insertText(documentText.endsWith(QLatin1Char('\n')) ? lineEnding : lineEnding + lineEnding);
    }
    c.insertText(QStringLiteral("[^") + id + QStringLiteral("]: "));
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtNamedFootnote(const QString& name)
{
    const QString id = name.trimmed();
    if (!isFootnoteId(id))
        return;

    QTextCursor c = textCursor();
    const QString source = toPlainText();
    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    c.beginEditBlock();
    c.insertText(QStringLiteral("[^") + id + QStringLiteral("]"));
    if (hasFootnoteDefinition(source, id)) {
        c.endEditBlock();
        setTextCursor(c);
        return;
    }
    c.movePosition(QTextCursor::End);
    const QString documentText = toPlainText();
    if (!documentText.isEmpty() && !documentText.endsWith(QStringLiteral("\n\n")))
        c.insertText(documentText.endsWith(QLatin1Char('\n')) ? lineEnding : lineEnding + lineEnding);
    c.insertText(QStringLiteral("[^") + id + QStringLiteral("]: "));
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtHorizontalRule()
{
    QTextCursor c = textCursor();
    const QString lineEnding = toPlainText().contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    c.beginEditBlock();
    c.movePosition(QTextCursor::EndOfBlock);
    // Keep the rule separated from surrounding paragraphs. In particular,
    // the preceding blank line prevents the dashes from becoming a Setext
    // heading underline.
    if (c.positionInBlock() > 0)
        c.insertText(lineEnding + lineEnding);
    c.insertText(QStringLiteral("---") + lineEnding + lineEnding);
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtMathInline()  { wrapInline(this, QStringLiteral("$")); }

void MarkdownEditor::fmtPageBreak()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    // Split the line at the caret so the marker sits on its own line between
    // the text before and after it, leaving the caret at the start of the rest.
    c.removeSelectedText();
    if (!c.atBlockStart())
        c.insertText(QStringLiteral("\n"));
    c.insertText(QStringLiteral("<div style=\"break-after: page;\"></div>\n"));
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::insertTableOfContents()
{
    const QString source = toPlainText();
    const MarkdownToc::Result toc = MarkdownToc::analyze(source);
    if (toc.headings.isEmpty())
        return;

    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    const QString tocText = MarkdownToc::render(toc, lineEnding);
    QTextCursor editCursor = textCursor();
    editCursor.beginEditBlock();

    if (!toc.placeholderLineNumbers.isEmpty()) {
        for (int i = toc.placeholderLineNumbers.size() - 1; i >= 0; --i) {
            const QTextBlock block = document()->findBlockByNumber(toc.placeholderLineNumbers.at(i));
            if (!block.isValid())
                continue;
            editCursor.setPosition(block.position());
            editCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            editCursor.insertText(tocText);
        }
    } else {
        const int start = editCursor.selectionStart();
        const int end = editCursor.selectionEnd();
        QString insertion = tocText;
        if (start > 0 && source.at(start - 1) != QLatin1Char('\n'))
            insertion.prepend(lineEnding + lineEnding);
        if (end < source.size() && source.at(end) != QLatin1Char('\n'))
            insertion.append(lineEnding + lineEnding);
        editCursor.insertText(insertion);
    }

    editCursor.endEditBlock();
    setTextCursor(editCursor);
}

void MarkdownEditor::insertDynamicTableOfContents()
{
    const QString source = toPlainText();
    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    QTextCursor editCursor = textCursor();
    const int start = editCursor.selectionStart();
    const int end = editCursor.selectionEnd();
    QString insertion = QStringLiteral("[TOC]");
    if (start > 0 && source.at(start - 1) != QLatin1Char('\n'))
        insertion.prepend(lineEnding + lineEnding);
    if (end < source.size() && source.at(end) != QLatin1Char('\n'))
        insertion.append(lineEnding + lineEnding);
    editCursor.insertText(insertion);
    setTextCursor(editCursor);
}

static void insertFencedBlock(MarkdownEditor* editor, const QString& info, const QString& body)
{
    QTextCursor c = editor->textCursor();
    const QString source = editor->toPlainText();
    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    // QTextCursor::selectedText() uses U+2029 for block breaks; normalize to
    // "\n" here and convert the whole body to the document's line ending once.
    const QString selected = c.hasSelection()
        ? QString(c.selectedText()).replace(QChar(0x2029), QLatin1Char('\n'))
        : QString();

    const int selectedStart = c.selectionStart();
    const bool selectionNeedsLeadingNewline = c.hasSelection()
        && selectedStart > 0 && source.at(selectedStart - 1) != QLatin1Char('\n');

    c.beginEditBlock();
    if (!c.hasSelection())
        c.movePosition(QTextCursor::EndOfBlock);
    QString text;
    if (selectionNeedsLeadingNewline || (c.positionInBlock() > 0 && !c.hasSelection()))
        text += lineEnding;
    text += QStringLiteral("```") + info + lineEnding;
    const int bodyOffset = text.size();
    QString content = selected.isEmpty() ? body : selected;
    if (lineEnding != QStringLiteral("\n"))
        content.replace(QStringLiteral("\n"), lineEnding);
    text += content + lineEnding + QStringLiteral("```") + lineEnding;

    const int start = c.selectionStart();
    c.insertText(text);
    c.endEditBlock();
    // Land the caret on the block's first content line so typing continues
    // where the user expects.
    c.setPosition(start + bodyOffset);
    if (!content.isEmpty())
        c.setPosition(start + bodyOffset + content.size(), QTextCursor::KeepAnchor);
    editor->setTextCursor(c);
}

void MarkdownEditor::fmtCodeBlock()
{
    // A plain code block starts empty; insertFencedBlock() leaves the caret on
    // its blank content line while preserving a selected range inside fences.
    insertFencedBlock(this, QString(), QString());
}

void MarkdownEditor::fmtMermaidBlock()
{
    insertFencedBlock(this, QStringLiteral("mermaid"),
                      QStringLiteral("graph TD;\n    A[Start] --> B[End];"));
}

void MarkdownEditor::fmtMathBlock()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection()) {
        const int start = c.selectionStart();
        const QString selected = document()->toPlainText().mid(start, c.selectionEnd() - start);
        c.insertText(QStringLiteral("$$\n") + selected + QStringLiteral("\n$$"));
    } else {
        const int position = c.position();
        c.insertText(QStringLiteral("$$\n\n$$"));
        c.setPosition(position + 3);
    }
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtLink()
{
    QTextCursor c = textCursor();
    QString sel = c.selectedText();
    if (sel.isEmpty()) {
        int pos = c.position();
        c.insertText(QStringLiteral("[link text](url)"));
        c.setPosition(pos + 1);
        c.setPosition(pos + 10, QTextCursor::KeepAnchor);
    } else {
        c.insertText(QStringLiteral("[") + sel + QStringLiteral("](url)"));
    }
    setTextCursor(c);
}

QString MarkdownEditor::nextNumericReferenceLabel() const
{
    qint64 highestLabel = 0;
    static const QRegularExpression definitionPattern(
        QStringLiteral(R"(^ {0,3}\[([0-9]+)\]:[ \t]*(?:<[^>\r\n]+>|\S+))"));
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QRegularExpressionMatch match = definitionPattern.match(block.text());
        bool validNumber = false;
        const qint64 numericLabel = match.captured(1).toLongLong(&validNumber);
        if (validNumber)
            highestLabel = qMax(highestLabel, numericLabel);
    }
    return QString::number(highestLabel + 1);
}

void MarkdownEditor::insertNumberedReferenceLink(const QString& referenceLabel, const QString& url,
                                                 const QString& title)
{
    QString label = referenceLabel.trimmed();
    if (label.startsWith(QLatin1Char('[')) && label.endsWith(QLatin1Char(']')))
        label = label.mid(1, label.size() - 2).trimmed();
    const QString trimmedUrl = url.trimmed();
    bool validNumber = false;
    if (trimmedUrl.isEmpty() || label.toLongLong(&validNumber) <= 0 || !validNumber)
        return;

    const QString marker = QStringLiteral("[") + label + QStringLiteral("]");
    QString definition = marker + QStringLiteral(": ") + trimmedUrl;
    if (!title.trimmed().isEmpty())
        definition += QStringLiteral(" \"") + title.trimmed().replace(QStringLiteral("\""), QStringLiteral("\\\""))
            + QStringLiteral("\"");

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.insertText(marker);
    const int markerEnd = cursor.position();
    const QString documentText = toPlainText();
    const QString lineEnding = documentText.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n") : QStringLiteral("\n");
    cursor.movePosition(QTextCursor::End);
    if (!documentText.endsWith(QStringLiteral("\n\n")))
        definition.prepend(documentText.endsWith(QLatin1Char('\n'))
            ? lineEnding : lineEnding + lineEnding);
    cursor.insertText(definition);
    cursor.endEditBlock();
    cursor.setPosition(markerEnd);
    setTextCursor(cursor);
    setFocus();
}

void MarkdownEditor::fmtH1() { setHeadingLevel(this, 1); }
void MarkdownEditor::fmtH2() { setHeadingLevel(this, 2); }
void MarkdownEditor::fmtH3() { setHeadingLevel(this, 3); }
void MarkdownEditor::fmtH4() { setHeadingLevel(this, 4); }
void MarkdownEditor::fmtH5() { setHeadingLevel(this, 5); }
void MarkdownEditor::fmtH6() { setHeadingLevel(this, 6); }

void MarkdownEditor::fmtQuote()       { prependLines(this, QStringLiteral("> ")); }
void MarkdownEditor::fmtBulletList()  { prependLines(this, QStringLiteral("- ")); }
void MarkdownEditor::fmtChecklist()   { prependChecklistLines(this); }

void MarkdownEditor::insertCallout(const QString& type)
{
    const QString calloutType = type.trimmed().toUpper();
    static const QStringList supported = { QStringLiteral("NOTE"), QStringLiteral("TIP"),
        QStringLiteral("IMPORTANT"), QStringLiteral("WARNING"), QStringLiteral("CAUTION") };
    if (!supported.contains(calloutType))
        return;

    const QString source = toPlainText();
    const QString lineEnding = source.contains(QStringLiteral("\r\n"))
        ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QTextCursor cursor = textCursor();

    if (!cursor.hasSelection()) {
        // A callout is a block construct. Finish the current line first and
        // keep it separated from the surrounding paragraph(s).
        cursor.movePosition(QTextCursor::EndOfBlock);
        const bool documentIsEmpty = source.isEmpty();
        const bool currentLineIsBlank = cursor.block().text().isEmpty();
        const QString before = documentIsEmpty ? QString()
            : currentLineIsBlank ? lineEnding : lineEnding + lineEnding;
        const QString header = QStringLiteral("> [!") + calloutType + QStringLiteral("]") + lineEnding + QStringLiteral("> ");
        const QString placeholder = QStringLiteral("Callout content");
        const int insertionStart = cursor.position();
        // The content line ends with the first newline below. At an existing
        // line boundary, the document supplies the second; at end of document
        // we add it ourselves.
        const QString after = insertionStart < source.size() ? QString() : lineEnding;

        cursor.beginEditBlock();
        cursor.insertText(before + header + placeholder + lineEnding + after);
        cursor.endEditBlock();
        const int placeholderStart = insertionStart + before.size() + header.size();
        cursor.setPosition(placeholderStart);
        cursor.setPosition(placeholderStart + placeholder.size(), QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        return;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    QTextBlock first = document()->findBlock(selectionStart);
    QTextBlock last = document()->findBlock(selectionEnd);
    // A selection ending at the start of the next line does not include that
    // line, which matches the user's visual selection.
    if (selectionEnd > selectionStart && last.isValid() && selectionEnd == last.position())
        last = last.previous();
    if (!first.isValid() || !last.isValid())
        return;

    QStringList lines;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        lines.append(block.text());
        if (block == last)
            break;
    }

    QString body;
    for (const QString& line : lines)
        body += QStringLiteral("> ") + line + lineEnding;

    const QTextBlock previous = first.previous();
    const QTextBlock next = last.next();
    const QString before = previous.isValid() && !previous.text().isEmpty() ? lineEnding : QString();
    const QString after = next.isValid() ? (next.text().isEmpty() ? QString() : lineEnding)
                                         : lineEnding;
    const QString replacement = before + QStringLiteral("> [!") + calloutType
        + QStringLiteral("]") + lineEnding + body + after;

    QTextCursor edit(document());
    edit.setPosition(first.position());
    // Replace the selected lines and their final line break. This lets the
    // replacement own its trailing spacing instead of adding a third newline
    // before the original following line.
    if (next.isValid())
        edit.setPosition(next.position(), QTextCursor::KeepAnchor);
    else
        edit.setPosition(last.position() + last.text().size(), QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.insertText(replacement);
    edit.endEditBlock();
    setTextCursor(edit);
}

void MarkdownEditor::fmtTable(int rows, int cols)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);

    QString header, separator, dataRow;
    for (int c = 0; c < cols; ++c) {
        QString colName = QStringLiteral("Col ") + QString::number(c + 1);
        header    += QStringLiteral("| ") + colName.leftJustified(9) + QStringLiteral(" ");
        separator += QStringLiteral("| --------- ");
        dataRow   += QStringLiteral("| Cell      ");
    }
    header    += QStringLiteral("|\n");
    separator += QStringLiteral("|\n");
    dataRow   += QStringLiteral("|\n");

    QString table = header + separator;
    for (int r = 0; r < rows; ++r)
        table += dataRow;

    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(table);
    c.endEditBlock();
}

void MarkdownEditor::fmtNumberList()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    int start = c.selectionStart();
    int end   = c.selectionEnd();
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    int n = 1;
    while (c.position() <= end || c.block().position() <= end) {
        QString prefix = QString::number(n++) + QStringLiteral(". ");
        c.insertText(prefix);
        end += prefix.length();
        if (!c.movePosition(QTextCursor::NextBlock)) break;
    }
    c.endEditBlock();
}
