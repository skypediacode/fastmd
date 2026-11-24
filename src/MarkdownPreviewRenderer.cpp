#include "MarkdownPreviewRenderer.h"

#include <QVector>

#include "md4c.h"

// entity.h has no extern "C" guard of its own (md4c-html.c, which declares
// entity_lookup, is compiled as C, giving it an unmangled symbol) -- add one
// here so this C++ translation unit links against that same symbol.
extern "C" {
#include "entity.h"
}

// Adapted from md4c's own md4c-html.c (MIT-licensed, vendored via CMake FetchContent --
// see CMakeLists.txt), with two changes: output is buffered per source-mapped block
// (instead of streamed flat) so each block's opening tag can be annotated with
// data-src-start/data-src-end once its span is known, and MD_TEXT/MD_ATTRIBUTE source
// pointers are used to track which source line is currently being rendered.

namespace {

// -- byte-oriented HTML/URL escaping, ported verbatim from md4c-html.c ------------

bool needHtmlEsc(char ch)
{
    return ch == '"' || ch == '&' || ch == '<' || ch == '>';
}

bool isAlnum(unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool needUrlEsc(unsigned char ch)
{
    static const char* allowedPunct = "~-_.+!*(),%#@?=;:/,+$";
    if (isAlnum(ch))
        return false;
    for (const char* p = allowedPunct; *p; ++p) {
        if (static_cast<unsigned char>(*p) == ch)
            return false;
    }
    return true;
}

void appendUtf8Codepoint(QByteArray& out, unsigned codepoint)
{
    if (codepoint == 0 || codepoint > 0x10ffff) {
        out.append("\xef\xbf\xbd", 3); // U+FFFD
        return;
    }
    if (codepoint <= 0x7f) {
        out.append(char(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.append(char(0xc0 | ((codepoint >> 6) & 0x1f)));
        out.append(char(0x80 | ((codepoint >> 0) & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.append(char(0xe0 | ((codepoint >> 12) & 0xf)));
        out.append(char(0x80 | ((codepoint >> 6) & 0x3f)));
        out.append(char(0x80 | ((codepoint >> 0) & 0x3f)));
    } else {
        out.append(char(0xf0 | ((codepoint >> 18) & 0x7)));
        out.append(char(0x80 | ((codepoint >> 12) & 0x3f)));
        out.append(char(0x80 | ((codepoint >> 6) & 0x3f)));
        out.append(char(0x80 | ((codepoint >> 0) & 0x3f)));
    }
}

void appendHtmlEscaped(QByteArray& out, const char* data, MD_SIZE size)
{
    for (MD_SIZE i = 0; i < size; ++i) {
        switch (data[i]) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:  out += data[i]; break;
        }
    }
}

void appendUrlEscaped(QByteArray& out, const char* data, MD_SIZE size)
{
    static const char hex[] = "0123456789ABCDEF";
    for (MD_SIZE i = 0; i < size; ++i) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        if (!needUrlEsc(ch)) {
            out += char(ch);
        } else if (ch == '&') {
            out += "&amp;";
        } else {
            out += '%';
            out += hex[(ch >> 4) & 0xf];
            out += hex[ch & 0xf];
        }
    }
}

unsigned hexVal(char ch)
{
    if (ch >= '0' && ch <= '9') return unsigned(ch - '0');
    if (ch >= 'A' && ch <= 'Z') return unsigned(ch - 'A' + 10);
    return unsigned(ch - 'a' + 10);
}

using AppendFn = void (*)(QByteArray&, const char*, MD_SIZE);

void appendEntity(QByteArray& out, const char* text, MD_SIZE size, AppendFn fn)
{
    if (size > 3 && text[1] == '#') {
        unsigned codepoint = 0;
        if (text[2] == 'x' || text[2] == 'X') {
            for (MD_SIZE i = 3; i < size - 1; ++i)
                codepoint = 16 * codepoint + hexVal(text[i]);
        } else {
            for (MD_SIZE i = 2; i < size - 1; ++i)
                codepoint = 10 * codepoint + unsigned(text[i] - '0');
        }
        QByteArray utf8;
        appendUtf8Codepoint(utf8, codepoint);
        fn(out, utf8.constData(), MD_SIZE(utf8.size()));
        return;
    }
    const ENTITY* ent = entity_lookup(text, size);
    if (ent) {
        QByteArray utf8;
        appendUtf8Codepoint(utf8, ent->codepoints[0]);
        if (ent->codepoints[1])
            appendUtf8Codepoint(utf8, ent->codepoints[1]);
        fn(out, utf8.constData(), MD_SIZE(utf8.size()));
        return;
    }
    fn(out, text, size);
}

void appendAttribute(QByteArray& out, const MD_ATTRIBUTE* attr, AppendFn fn)
{
    for (int i = 0; attr->substr_offsets[i] < attr->size; ++i) {
        MD_TEXTTYPE type = attr->substr_types[i];
        MD_OFFSET off = attr->substr_offsets[i];
        MD_SIZE size = attr->substr_offsets[i + 1] - off;
        const char* text = attr->text + off;
        switch (type) {
        case MD_TEXT_NULLCHAR: appendUtf8Codepoint(out, 0); break;
        case MD_TEXT_ENTITY:   appendEntity(out, text, size, fn); break;
        default:                fn(out, text, size); break;
        }
    }
}

// -- source-line tracking ----------------------------------------------------------

struct BlockEntry {
    QByteArray openTag;  // e.g. "<p>" or "<li class=\"task-list-item\">" -- attrs get
                         // inserted before its first '>'.
    QByteArray closeTag; // e.g. "</p>\n"
    int startLine = -1;
    bool taskItem = false;
    bool taskList = false;
    bool taskTightParagraphUsed = false;
    bool listContainer = false;
    bool looseListBySpacing = false;
    bool singleLineFallback = false;
};

struct Ctx {
    const char* base = nullptr;
    int length = 0;          // byte length of the buffer `base` points into
    QVector<int> lineStarts; // byte offset of the start of each 1-based line
    int currentLine = 0;     // 0 == "nothing touched yet" (lines are 1-based)
    int currentRawLine = 0;  // currentLine's preprocessed-text counterpart (pre-shift)
    int imageNestingLevel = 0;
    QVector<QByteArray> bufStack;
    QVector<BlockEntry> blockStack;
    QVector<bool> listTightStack;

    // Undoes the line-collapsing caused by FastMdMath::preprocess() replacing
    // multi-line math spans with single-line placeholders -- see MathLineShift.
    // Parallel, ascending-by-atLine arrays; shiftCumulative[k] is the total delta
    // from all shifts up to and including shiftAtLine[k].
    QVector<int> shiftAtLine;
    QVector<int> shiftCumulative;
    // Final-preprocessing line -> original editor line. This takes precedence
    // over legacy math shifts when transformations insert/remove whole lines.
    QVector<int> lineOrigins;

    QByteArray& top() { return bufStack.last(); }

    int applyLineShift(int preprocessedLine) const
    {
        if (shiftAtLine.isEmpty()) return preprocessedLine;
        // upper bound: first index with shiftAtLine[idx] >= preprocessedLine, so the
        // shift's own placeholder line maps to itself (the collapsed span's first
        // original line), not to its last original line.
        int lo = 0, hi = shiftAtLine.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (shiftAtLine[mid] < preprocessedLine) lo = mid + 1; else hi = mid;
        }
        return lo == 0 ? preprocessedLine : preprocessedLine + shiftCumulative[lo - 1];
    }

    int rawLineForOffset(qsizetype off) const
    {
        // binary search: greatest i with lineStarts[i] <= off
        int lo = 0, hi = lineStarts.size() - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (lineStarts[mid] <= off) lo = mid; else hi = mid - 1;
        }
        return lo + 1; // 1-based, preprocessed-text line (shift not yet applied)
    }

    int sourceLineForRawLine(int rawLine) const
    {
        if (rawLine > 0 && rawLine <= lineOrigins.size() && lineOrigins.at(rawLine - 1) > 0)
            return lineOrigins.at(rawLine - 1);
        return applyLineShift(rawLine);
    }

    int sourceLineFor(const char* ptr) const
    {
        if (!ptr || !base) return currentLine;
        const qsizetype off = ptr - base;
        if (off < 0 || off >= length) return currentLine;
        return sourceLineForRawLine(rawLineForOffset(off));
    }

    bool hasBlankLineBefore(int line) const
    {
        if (line <= 1 || line - 1 >= lineStarts.size())
            return false;
        const int begin = lineStarts.at(line - 2);
        const int end = lineStarts.at(line - 1);
        for (int i = begin; i < end; ++i) {
            const char ch = base[i];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                return false;
        }
        return true;
    }

    // Advances currentLine/currentRawLine and any still-unset ancestor block
    // startLine to `rawLine` (a preprocessed-text line number). Used both when
    // real text/attribute source positions are seen, and to locate leaf blocks
    // (e.g. <hr>) that never do.
    void touchRawLine(int rawLine)
    {
        if (rawLine > currentRawLine) currentRawLine = rawLine;
        int line = sourceLineForRawLine(rawLine);
        if (line > currentLine) currentLine = line;
        for (int i = blockStack.size() - 1; i >= 0; --i) {
            if (blockStack[i].startLine != -1) break;
            blockStack[i].startLine = line;
        }
    }

    void touchLine(const char* ptr)
    {
        if (!ptr || !base) return;
        // MD4C passes some text (a code block's line breaks) from its own static
        // strings rather than the source buffer; those carry no position.
        qsizetype off = ptr - base;
        if (off < 0 || off >= length) return;
        touchRawLine(rawLineForOffset(off));
    }

    void touchAttr(const MD_ATTRIBUTE* attr)
    {
        if (attr && attr->text) touchLine(attr->text);
    }

    // Leaf/empty blocks (e.g. <hr>, an empty fence) never touch any text or
    // attribute, so currentLine is never refreshed to their own source line by
    // touchLine(). Blank lines only ever separate blocks -- they never start
    // one -- so the next non-blank preprocessed line strictly after the last
    // touched line (currentRawLine) must be this block's own line. Touches it
    // (advancing currentLine/currentRawLine) and returns the resulting
    // (shift-corrected) currentLine.
    int touchNextContentLine()
    {
        if (!base) return currentLine;
        int rawLine = currentRawLine + 1;
        for (int idx = qMax(0, currentRawLine); idx < lineStarts.size(); ++idx) {
            const int off = lineStarts[idx];
            const int end = (idx + 1 < lineStarts.size()) ? lineStarts[idx + 1] : length;
            bool blank = true;
            for (int p = off; p < end; ++p) {
                const char c = base[p];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { blank = false; break; }
            }
            if (!blank) { rawLine = idx + 1; break; }
        }
        touchRawLine(rawLine);
        return currentLine;
    }
};

void writeAttrsIntoOpenTag(QByteArray& tag, int startLine, int endLine)
{
    int gt = tag.indexOf('>');
    if (gt < 0) return;
    QByteArray attrs = QByteArray(" data-src-start=\"") + QByteArray::number(startLine)
                      + "\" data-src-end=\"" + QByteArray::number(endLine) + "\"";
    tag.insert(gt, attrs);
}

void appendEntityEscaped(QByteArray& out, const char* text, MD_SIZE size)
{
    appendEntity(out, text, size, appendHtmlEscaped);
}

// Text inside one rendered block can originate on several Markdown lines.  The
// block attributes remain useful for block-level navigation, but wrapping each
// rendered text run gives the preview click handler an exact source line when
// the user clicks prose within a multi-line paragraph, list item, or heading.
// These spans are intentionally inline so they do not affect the one-top-level-
// block invariant used by the incremental preview renderer.
void appendLineMappedText(Ctx& ctx, const char* text, MD_SIZE size, AppendFn fn)
{
    if (!text || size == 0) return;

    MD_SIZE begin = 0;
    while (begin < size) {
        MD_SIZE end = begin;
        while (end < size && text[end] != '\n') ++end;
        if (end < size) ++end; // Keep the source newline with its preceding line.

        ctx.touchLine(text + begin);
        ctx.top() += QByteArray("<span data-src-line=\"")
                   + QByteArray::number(ctx.sourceLineFor(text + begin)) + "\">";
        fn(ctx.top(), text + begin, end - begin);
        ctx.top() += "</span>";
        begin = end;
    }
}

void pushMappedBlock(Ctx& ctx, const QByteArray& openTag, const QByteArray& closeTag,
                      const QByteArray& leadingInner = QByteArray())
{
    BlockEntry entry;
    entry.openTag = openTag;
    entry.closeTag = closeTag;
    ctx.blockStack.append(entry);
    ctx.bufStack.append(QByteArray());
    if (!leadingInner.isEmpty())
        ctx.top() += leadingInner;
}

void popMappedBlock(Ctx& ctx)
{
    QByteArray inner = ctx.bufStack.takeLast();
    BlockEntry entry = ctx.blockStack.takeLast();
    // A block with zero text/attribute children (e.g. an empty fence or heading)
    // never had its startLine set by a descendant touch -- resolve it now, the
    // same way <hr> does.
    int startLine = entry.startLine != -1 ? entry.startLine : ctx.touchNextContentLine();
    int endLine = entry.singleLineFallback ? startLine : ctx.currentLine;
    // A task checkbox identifies the marker on the list item's first source
    // line.  Do not expose the enclosing list's accumulated end line: after a
    // preview refresh that broad range can make a toggle target a different
    // task with the same label.
    if (entry.taskItem)
        endLine = startLine;
    QByteArray openTag = entry.openTag;
    writeAttrsIntoOpenTag(openTag, startLine, endLine);
    if (entry.taskItem) {
        const int gt = openTag.indexOf('>');
        if (gt >= 0) {
            const QByteArray attrs = QByteArray(" data-fastmd-task-start=\"")
                + QByteArray::number(startLine) + "\" data-fastmd-task-end=\""
                + QByteArray::number(endLine) + "\"";
            openTag.insert(gt, attrs);
        }
    }
    if (entry.listContainer && entry.looseListBySpacing) {
        const int gt = openTag.indexOf('>');
        if (gt >= 0 && !openTag.contains("data-fastmd-loose-list"))
            openTag.insert(gt, " data-fastmd-loose-list=\"1\"");
    }
    if (entry.listContainer && entry.taskList) {
        const int gt = openTag.indexOf('>');
        if (gt >= 0)
            openTag.insert(gt, " data-fastmd-task-list=\"1\"");
    }
    ctx.top() += openTag;
    ctx.top() += inner;
    ctx.top() += entry.closeTag;
}

// -- block/span open-tag builders, ported from md4c-html.c -------------------------

QByteArray openUlTag(const MD_BLOCK_UL_DETAIL* det)
{
    return det->is_tight ? QByteArrayLiteral("<ul>\n")
                         : QByteArrayLiteral("<ul data-fastmd-loose-list=\"1\">\n");
}

QByteArray openOlTag(const MD_BLOCK_OL_DETAIL* det)
{
    if (det->start == 1)
        return det->is_tight ? QByteArrayLiteral("<ol>\n")
                             : QByteArrayLiteral("<ol data-fastmd-loose-list=\"1\">\n");
    QByteArray tag = QByteArray("<ol start=\"") + QByteArray::number(det->start) + "\"";
    if (!det->is_tight)
        tag += " data-fastmd-loose-list=\"1\"";
    tag += ">\n";
    return tag;
}

QByteArray openLiLeading(const MD_BLOCK_LI_DETAIL* det)
{
    if (!det->is_task)
        return QByteArray();
    const bool checked = det->task_mark == 'x' || det->task_mark == 'X';
    QByteArray s = "<input type=\"checkbox\" class=\"task-list-item-checkbox\" aria-label=\"Toggle task\" data-fastmd-checked=\"";
    s += checked ? "1\"" : "0\"";
    if (checked)
        s += " checked";
    s += ">";
    return s;
}

QByteArray openLiTag(const MD_BLOCK_LI_DETAIL* det)
{
    return det->is_task ? QByteArrayLiteral("<li class=\"task-list-item\" data-fastmd-task-item=\"1\">")
                         : QByteArrayLiteral("<li>");
}

QByteArray openCodeTag(const MD_BLOCK_CODE_DETAIL* det)
{
    QByteArray s = "<pre><code";
    // MD4C provides a non-null, zero-length attribute for an unlabelled
    // fenced block.  Emitting "language-" for it makes highlight.js treat the
    // block as unlabelled and auto-detect prose as an arbitrary language.
    if (det->lang.text && det->lang.size > 0) {
        s += " class=\"language-";
        appendAttribute(s, &det->lang, appendHtmlEscaped);
        s += "\"";
    }
    s += ">";
    return s;
}

QByteArray openTdTag(const char* cellType, const MD_BLOCK_TD_DETAIL* det)
{
    QByteArray s = QByteArray("<") + cellType;
    switch (det->align) {
    case MD_ALIGN_LEFT:   s += " align=\"left\">"; break;
    case MD_ALIGN_CENTER: s += " align=\"center\">"; break;
    case MD_ALIGN_RIGHT:  s += " align=\"right\">"; break;
    default:              s += ">"; break;
    }
    return s;
}

// -- MD_PARSER callbacks -------------------------------------------------------------

int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    static const char* headOpen[6] = { "<h1>", "<h2>", "<h3>", "<h4>", "<h5>", "<h6>" };
    static const char* headClose[6] = { "</h1>\n", "</h2>\n", "</h3>\n", "</h4>\n", "</h5>\n", "</h6>\n" };
    Ctx& ctx = *static_cast<Ctx*>(userdata);

    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        pushMappedBlock(ctx, "<blockquote>\n", "</blockquote>\n");
        break;
    case MD_BLOCK_UL:
        ctx.listTightStack.append(static_cast<const MD_BLOCK_UL_DETAIL*>(detail)->is_tight != 0);
        pushMappedBlock(ctx, openUlTag(static_cast<const MD_BLOCK_UL_DETAIL*>(detail)), "</ul>\n");
        ctx.blockStack.last().listContainer = true;
        break;
    case MD_BLOCK_OL: {
        auto* det = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
        ctx.listTightStack.append(det->is_tight != 0);
        pushMappedBlock(ctx, openOlTag(det), "</ol>\n");
        ctx.blockStack.last().listContainer = true;
        break;
    }
    case MD_BLOCK_LI: {
        auto* det = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        if (det->is_task) {
            const int taskLine = ctx.rawLineForOffset(det->task_mark_offset);
            for (int i = ctx.blockStack.size() - 1; i >= 0; --i) {
                if (ctx.blockStack[i].listContainer) {
                    ctx.blockStack[i].taskList = true;
                    if (ctx.hasBlankLineBefore(taskLine))
                        ctx.blockStack[i].looseListBySpacing = true;
                    break;
                }
            }
        }
        pushMappedBlock(ctx, openLiTag(det), "</li>\n", openLiLeading(det));
        if (det->is_task)
            ctx.blockStack.last().taskItem = true;
        break;
    }
    case MD_BLOCK_HR: {
        int line = ctx.touchNextContentLine();
        QByteArray tag = "<hr>\n";
        writeAttrsIntoOpenTag(tag, line, line);
        ctx.top() += tag;
        break;
    }
    case MD_BLOCK_H: {
        unsigned level = static_cast<const MD_BLOCK_H_DETAIL*>(detail)->level;
        pushMappedBlock(ctx, headOpen[level - 1], headClose[level - 1]);
        break;
    }
    case MD_BLOCK_CODE: {
        auto* det = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
        ctx.touchAttr(&det->lang);
        pushMappedBlock(ctx, openCodeTag(det), "</code></pre>\n");
        break;
    }
    case MD_BLOCK_HTML:
        // Raw HTML has no ordinary MD_TEXT children to annotate. Keep it in a
        // transparent wrapper so the source resolver still has an ancestor.
        pushMappedBlock(ctx, "<div class=\"fastmd-raw-html\">", "</div>\n");
        ctx.blockStack.last().singleLineFallback = true;
        break;
    case MD_BLOCK_P:
    {
        bool tightTaskParagraph = false;
        for (int i = ctx.blockStack.size() - 1; i >= 0; --i) {
            if (ctx.blockStack[i].taskItem) {
                const bool tightList = !ctx.listTightStack.isEmpty() && ctx.listTightStack.last();
                tightTaskParagraph = tightList && !ctx.blockStack[i].taskTightParagraphUsed;
                if (tightTaskParagraph)
                    ctx.blockStack[i].taskTightParagraphUsed = true;
                break;
            }
        }
        pushMappedBlock(ctx, tightTaskParagraph ? "<p class=\"task-list-tight-paragraph\">" : "<p>", "</p>\n");
        break;
    }
    case MD_BLOCK_TABLE:
        pushMappedBlock(ctx, "<table>\n", "</table>\n");
        break;
    case MD_BLOCK_THEAD: ctx.top() += "<thead>\n"; break;
    case MD_BLOCK_TBODY: ctx.top() += "<tbody>\n"; break;
    case MD_BLOCK_TR:    ctx.top() += "<tr>\n"; break;
    case MD_BLOCK_TH:    ctx.top() += openTdTag("th", static_cast<const MD_BLOCK_TD_DETAIL*>(detail)); break;
    case MD_BLOCK_TD:    ctx.top() += openTdTag("td", static_cast<const MD_BLOCK_TD_DETAIL*>(detail)); break;
    }
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    Ctx& ctx = *static_cast<Ctx*>(userdata);
    switch (type) {
    case MD_BLOCK_DOC:
    case MD_BLOCK_HR:
        break;
    case MD_BLOCK_HTML:
        popMappedBlock(ctx);
        break;
    case MD_BLOCK_QUOTE:
        popMappedBlock(ctx);
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!ctx.listTightStack.isEmpty())
            ctx.listTightStack.removeLast();
        popMappedBlock(ctx);
        break;
    case MD_BLOCK_LI:
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_P:
    case MD_BLOCK_TABLE:
        popMappedBlock(ctx);
        break;
    case MD_BLOCK_THEAD: ctx.top() += "</thead>\n"; break;
    case MD_BLOCK_TBODY: ctx.top() += "</tbody>\n"; break;
    case MD_BLOCK_TR:    ctx.top() += "</tr>\n"; break;
    case MD_BLOCK_TH:    ctx.top() += "</th>\n"; break;
    case MD_BLOCK_TD:    ctx.top() += "</td>\n"; break;
    }
    (void)detail;
    return 0;
}

int enterSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    Ctx& ctx = *static_cast<Ctx*>(userdata);
    bool insideImg = ctx.imageNestingLevel > 0;

    if (type == MD_SPAN_IMG)
        ctx.imageNestingLevel++;
    if (insideImg)
        return 0;

    switch (type) {
    case MD_SPAN_EM:     ctx.top() += "<em>"; break;
    case MD_SPAN_STRONG: ctx.top() += "<strong>"; break;
    case MD_SPAN_U:      ctx.top() += "<u>"; break;
    case MD_SPAN_A: {
        auto* det = static_cast<const MD_SPAN_A_DETAIL*>(detail);
        ctx.touchAttr(&det->href);
        QByteArray& out = ctx.top();
        out += "<a href=\"";
        appendAttribute(out, &det->href, appendUrlEscaped);
        if (det->title.text) {
            out += "\" title=\"";
            appendAttribute(out, &det->title, appendHtmlEscaped);
        }
        out += "\">";
        break;
    }
    case MD_SPAN_IMG: {
        auto* det = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
        ctx.touchAttr(&det->src);
        QByteArray& out = ctx.top();
        out += "<img src=\"";
        appendAttribute(out, &det->src, appendUrlEscaped);
        out += "\" alt=\"";
        break;
    }
    case MD_SPAN_CODE: ctx.top() += "<code>"; break;
    case MD_SPAN_DEL:  ctx.top() += "<del>"; break;
    case MD_SPAN_LATEXMATH:         ctx.top() += "<x-equation>"; break;
    case MD_SPAN_LATEXMATH_DISPLAY: ctx.top() += "<x-equation type=\"display\">"; break;
    case MD_SPAN_WIKILINK: {
        auto* det = static_cast<const MD_SPAN_WIKILINK_DETAIL*>(detail);
        QByteArray& out = ctx.top();
        out += "<x-wikilink data-target=\"";
        appendAttribute(out, &det->target, appendHtmlEscaped);
        out += "\">";
        break;
    }
    }
    return 0;
}

int leaveSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    Ctx& ctx = *static_cast<Ctx*>(userdata);

    if (type == MD_SPAN_IMG)
        ctx.imageNestingLevel--;
    if (ctx.imageNestingLevel > 0)
        return 0;

    switch (type) {
    case MD_SPAN_EM:     ctx.top() += "</em>"; break;
    case MD_SPAN_STRONG:  ctx.top() += "</strong>"; break;
    case MD_SPAN_U:       ctx.top() += "</u>"; break;
    case MD_SPAN_A:       ctx.top() += "</a>"; break;
    case MD_SPAN_IMG: {
        auto* det = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
        QByteArray& out = ctx.top();
        if (det->title.text) {
            out += "\" title=\"";
            appendAttribute(out, &det->title, appendHtmlEscaped);
        }
        out += "\">";
        break;
    }
    case MD_SPAN_CODE: ctx.top() += "</code>"; break;
    case MD_SPAN_DEL:  ctx.top() += "</del>"; break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY: ctx.top() += "</x-equation>"; break;
    case MD_SPAN_WIKILINK: ctx.top() += "</x-wikilink>"; break;
    }
    return 0;
}

int textCb(MD_TEXTTYPE type, const char* text, MD_SIZE size, void* userdata)
{
    Ctx& ctx = *static_cast<Ctx*>(userdata);
    ctx.touchLine(text);
    QByteArray& out = ctx.top();

    // Image alt text is accumulated between <img ... alt=\" and its closing
    // quote, where markup would be invalid. It is not visible preview content
    // in any case, so only update the enclosing block mapping.
    if (ctx.imageNestingLevel > 0) {
        switch (type) {
        case MD_TEXT_NULLCHAR: appendUtf8Codepoint(out, 0); break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:   out += " "; break;
        case MD_TEXT_HTML:     out.append(text, size); break;
        case MD_TEXT_ENTITY:   appendEntity(out, text, size, appendHtmlEscaped); break;
        default:                appendHtmlEscaped(out, text, size); break;
        }
        return 0;
    }

    switch (type) {
    case MD_TEXT_NULLCHAR: appendUtf8Codepoint(out, 0); break;
    case MD_TEXT_BR:
        out += QByteArray("<br data-src-line=\"") + QByteArray::number(ctx.sourceLineFor(text)) + "\">\n";
        break;
    case MD_TEXT_SOFTBR:
        out += QByteArray("<span data-src-line=\"") + QByteArray::number(ctx.sourceLineFor(text)) + "\">\n</span>";
        break;
    case MD_TEXT_HTML:     out.append(text, size); break;
    case MD_TEXT_ENTITY:   appendLineMappedText(ctx, text, size, appendEntityEscaped); break;
    default:                appendLineMappedText(ctx, text, size, appendHtmlEscaped); break;
    }
    return 0;
}

} // namespace

QString MarkdownPreviewRenderer::render(const QByteArray& utf8, unsigned parserFlags,
                                         const QVector<MathLineShift>& lineShifts,
                                         const QVector<int>& lineOrigins)
{
    Ctx ctx;
    ctx.base = utf8.constData();
    ctx.length = int(utf8.size());
    ctx.lineStarts.append(0);
    for (qsizetype i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n')
            ctx.lineStarts.append(int(i + 1));
    }
    ctx.bufStack.append(QByteArray());
    ctx.lineOrigins = lineOrigins;

    int cumulative = 0;
    for (const MathLineShift& shift : lineShifts) {
        cumulative += shift.deltaLines;
        ctx.shiftAtLine.append(shift.atLine);
        ctx.shiftCumulative.append(cumulative);
    }

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = parserFlags;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = textCb;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    md_parse(utf8.constData(), MD_SIZE(utf8.size()), &parser, &ctx);

    return QString::fromUtf8(ctx.bufStack.isEmpty() ? QByteArray() : ctx.bufStack.first());
}
