#include "PreviewPatch.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool isHtmlSpace(char16_t c)
{
    return c == u' ' || c == u'\n' || c == u'\t' || c == u'\r' || c == u'\f';
}

bool isAsciiAlpha(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// What the HTML parser does with a tag, as far as top-level element
// boundaries are concerned. Names follow the WHATWG "in body" rules.
enum TagFlag : quint32 {
    Void           = 1u << 0,   // never has an end tag
    RawText        = 1u << 1,   // content is text up to its own end tag
    ClosesP        = 1u << 2,   // its start tag ends an open <p>
    Special        = 1u << 3,   // "special" category: stops end-tag recovery
    Formatting     = 1u << 4,   // reconstructed by the parser when left open
    ScopeBoundary  = 1u << 5,   // end tags do not see past it
    TableContext   = 1u << 6,   // other content is fostered out of it
    TablePart      = 1u << 7,   // allowed directly inside TableContext
    BlockEnd       = 1u << 8,   // end tag pops through anything in scope
    Heading        = 1u << 9,
    Breakout       = 1u << 10,  // leaves <svg>/<math> content
    Unsupported    = 1u << 11,  // parsed in ways not modelled here: give up
    ListItem       = 1u << 12,  // li, dd, dt: start tag can close an open one
};

const std::unordered_map<std::string, quint32>& tagFlags()
{
    static const std::unordered_map<std::string, quint32> table = [] {
        std::unordered_map<std::string, quint32> t;
        auto add = [&](std::initializer_list<const char*> names, quint32 flags) {
            for (const char* n : names)
                t[n] |= flags;
        };
        add({"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
             "source", "track", "wbr", "param", "keygen", "frame", "basefont", "bgsound"}, Void);
        add({"script", "style", "textarea", "title", "xmp", "iframe", "noembed", "noframes"},
            RawText);
        add({"address", "article", "aside", "blockquote", "center", "details", "dialog", "dir",
             "div", "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
             "h4", "h5", "h6", "header", "hgroup", "hr", "listing", "main", "menu", "nav", "ol",
             "p", "pre", "search", "section", "summary", "table", "ul", "li", "dd", "dt", "xmp"},
            ClosesP);
        add({"address", "applet", "area", "article", "aside", "base", "basefont", "bgsound",
             "blockquote", "body", "br", "button", "caption", "center", "col", "colgroup", "dd",
             "details", "dir", "div", "dl", "dt", "embed", "fieldset", "figcaption", "figure",
             "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
             "header", "hgroup", "hr", "html", "iframe", "img", "input", "keygen", "li", "link",
             "listing", "main", "marquee", "menu", "meta", "nav", "noembed", "noframes",
             "noscript", "object", "ol", "p", "param", "plaintext", "pre", "script", "search",
             "section", "select", "source", "style", "summary", "table", "tbody", "td",
             "template", "textarea", "tfoot", "th", "thead", "title", "tr", "track", "ul", "wbr",
             "xmp"}, Special);
        add({"a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small", "strike",
             "strong", "tt", "u"}, Formatting);
        add({"applet", "caption", "html", "table", "td", "th", "marquee", "object", "template",
             "foreignobject", "desc"}, ScopeBoundary);
        add({"table", "thead", "tbody", "tfoot", "tr", "colgroup"}, TableContext);
        add({"caption", "colgroup", "col", "thead", "tbody", "tfoot", "tr", "td", "th",
             "template", "script", "style"}, TablePart);
        add({"address", "article", "aside", "blockquote", "button", "center", "details",
             "dialog", "dir", "div", "dl", "fieldset", "figcaption", "figure", "footer",
             "header", "hgroup", "listing", "main", "menu", "nav", "ol", "pre", "search",
             "section", "summary", "ul", "li", "dd", "dt"}, BlockEnd);
        add({"h1", "h2", "h3", "h4", "h5", "h6"}, Heading);
        add({"b", "big", "blockquote", "body", "br", "center", "code", "dd", "div", "dl", "dt",
             "em", "embed", "h1", "h2", "h3", "h4", "h5", "h6", "head", "hr", "i", "img", "li",
             "listing", "menu", "meta", "nobr", "ol", "p", "pre", "ruby", "s", "small", "span",
             "strong", "strike", "sub", "sup", "table", "tt", "u", "ul", "var", "font"},
            Breakout);
        add({"html", "head", "body", "frameset", "noscript", "select", "option", "optgroup",
             "plaintext", "form", "image", "isindex", "math", "template", "frame",
             "sarcasm"}, Unsupported);
        add({"li", "dd", "dt"}, ListItem);
        return t;
    }();
    return table;
}

quint32 flagsOf(const std::string& name)
{
    const auto& t = tagFlags();
    const auto it = t.find(name);
    return it == t.end() ? 0u : it->second;
}

// Lower-cased tag name at `i`; `i` is left on the first character after it.
std::string readTagName(const char16_t* p, qsizetype n, qsizetype& i)
{
    std::string name;
    while (i < n) {
        const char16_t c = p[i];
        if (isHtmlSpace(c) || c == u'/' || c == u'>')
            break;
        name.push_back(c < 0x80 ? char(c >= u'A' && c <= u'Z' ? c + 32 : c) : '?');
        ++i;
    }
    return name;
}

// Skips a tag's attributes up to and past its '>'. Returns false at the end of
// input; `selfClosing` reports a "/>" ending.
bool skipAttributes(const char16_t* p, qsizetype n, qsizetype& i, bool& selfClosing)
{
    selfClosing = false;
    while (i < n) {
        const char16_t c = p[i];
        if (c == u'>') {
            selfClosing = i > 0 && p[i - 1] == u'/';
            ++i;
            return true;
        }
        if (c == u'"' || c == u'\'') {
            // A quote only opens a value right after '='.
            qsizetype k = i - 1;
            while (k >= 0 && isHtmlSpace(p[k]))
                --k;
            if (k >= 0 && p[k] == u'=') {
                const qsizetype close = QStringView(p + i + 1, n - i - 1).indexOf(QChar(c));
                if (close < 0)
                    return false;
                i += close + 2;
                continue;
            }
        }
        ++i;
    }
    return false;
}

qsizetype indexOfCaseless(const char16_t* p, qsizetype n, qsizetype from, QStringView needle)
{
    return QStringView(p, n).indexOf(needle, from, Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
// The line-shift model, ported from preview.js (matchBlock and its helpers).

constexpr QStringView kLineAttrPrefixes[] = {
    u" data-src-start=\"", u" data-src-end=\"", u" data-src-line=\"",
    u" data-fastmd-task-start=\"", u" data-fastmd-task-end=\"",
};

// Index of the value of the next line attribute at or after `from`, or -1.
// Only an occurrence inside a tag counts; elsewhere it is content.
qsizetype nextLineValue(QStringView s, qsizetype from)
{
    for (qsizetype k = s.indexOf(u" data-", from); k >= 0; k = s.indexOf(u" data-", k + 6)) {
        const QStringView rest = s.sliced(k);
        for (QStringView prefix : kLineAttrPrefixes) {
            if (rest.startsWith(prefix)) {
                if (s.lastIndexOf(u'<', k) > s.lastIndexOf(u'>', k))
                    return k + prefix.size();
                break;
            }
        }
    }
    return -1;
}

// The digits at `at` and the index past the closing quote, or false when the
// value is not a plain number.
bool readLineValue(QStringView s, qsizetype at, qint64& value, qsizetype& end)
{
    qsizetype q = at;
    qint64 v = 0;
    while (q < s.size() && q - at < 9 && s.at(q) >= u'0' && s.at(q) <= u'9') {
        v = v * 10 + (s.at(q).unicode() - u'0');
        ++q;
    }
    if (q == at || q >= s.size() || s.at(q) != u'"')
        return false;
    value = v;
    end = q + 1;
    return true;
}

struct ShiftModel {
    bool hasDelta = false;
    qint64 delta = 0;
    qint64 maxStayed = -1;
    qint64 minMoved = std::numeric_limits<qint64>::max();
};

enum class Match { Same, Moved, Different };

Match matchBlock(QStringView a, QStringView b, ShiftModel& model, bool commit)
{
    if (a == b)
        return Match::Same;
    qsizetype i = 0, j = 0;
    ShiftModel m = model;
    for (;;) {
        const qsizetype ia = nextLineValue(a, i);
        const qsizetype ib = nextLineValue(b, j);
        if (ia < 0 || ib < 0) {
            if (ia != ib || a.sliced(i) != b.sliced(j))
                return Match::Different;
            break;
        }
        if (ia - i != ib - j || a.sliced(i, ia - i) != b.sliced(j, ib - j))
            return Match::Different;
        qint64 va = 0, vb = 0;
        qsizetype ea = 0, eb = 0;
        if (!readLineValue(a, ia, va, ea) || !readLineValue(b, ib, vb, eb))
            return Match::Different;
        if (va == vb) {
            if (va >= m.minMoved)
                return Match::Different;
            m.maxStayed = std::max(m.maxStayed, va);
        } else {
            if (!m.hasDelta) {
                m.hasDelta = true;
                m.delta = vb - va;
            } else if (vb - va != m.delta) {
                return Match::Different;
            }
            if (va <= m.maxStayed)
                return Match::Different;
            m.minMoved = std::min(m.minMoved, va);
        }
        i = ea;
        j = eb;
    }
    if (commit)
        model = m;
    return Match::Moved;
}

} // namespace

namespace PreviewPatch {

PreviewBlocksPtr split(const QString& body, qsizetype* failedAt)
{
    auto out = std::make_shared<PreviewBlocks>();
    out->body = body;
    const char16_t* p = reinterpret_cast<const char16_t*>(body.constData());
    const qsizetype n = body.size();

    // The open elements, as the browser's parser would hold them. Only enough
    // of its error recovery is modelled to follow ordinary Markdown output and
    // the common slips in raw HTML (an unclosed <T> in "Cell<T>"); anything
    // that could move content across a top-level boundary gives up instead.
    struct Open {
        std::string name;
        quint32 flags;
        bool foreign;
    };
    std::vector<Open> stack;
    int paragraphs = 0;
    qsizetype blockStart = -1;
    qsizetype i = 0;
    auto fail = [&]() -> PreviewBlocksPtr {
        if (failedAt)
            *failedAt = i;
        return {};
    };
    auto pop = [&]() {
        if (stack.back().name == "p" && !stack.back().foreign)
            --paragraphs;
        stack.pop_back();
    };
    auto closeBlock = [&](qsizetype at) {
        if (stack.empty()) {
            out->begin.append(blockStart);
            out->end.append(at);
        }
    };
    auto findOpen = [&](const std::string& name) -> qsizetype {
        for (qsizetype j = qsizetype(stack.size()) - 1; j >= 0; --j)
            if (stack[size_t(j)].name == name)
                return j;
        return -1;
    };

    while (i < n) {
        const char16_t c = p[i];
        if (c != u'<') {
            const bool strict = stack.empty() || (stack.back().flags & TableContext);
            if (strict && !isHtmlSpace(c))
                return fail();          // top-level text, or text a table fosters out
            if (strict) {
                ++i;
            } else {
                const qsizetype next = QStringView(p + i, n - i).indexOf(u'<');
                i = next < 0 ? n : i + next;
            }
            continue;
        }
        if (i + 1 >= n)
            return fail();
        const char16_t c1 = p[i + 1];
        const bool inForeign = !stack.empty() && stack.back().foreign;

        if (c1 == u'!' || c1 == u'?') {
            qsizetype end;
            if (QStringView(p + i, n - i).startsWith(u"<!--")) {
                if (i + 4 < n && p[i + 4] == u'>')
                    end = i + 5;
                else if (i + 5 < n && p[i + 4] == u'-' && p[i + 5] == u'>')
                    end = i + 6;
                else {
                    const qsizetype close = QStringView(p, n).indexOf(u"-->", i + 4);
                    if (close < 0)
                        return fail();
                    end = close + 3;
                }
            } else if (inForeign && QStringView(p + i, n - i).startsWith(u"<![CDATA[")) {
                const qsizetype close = QStringView(p, n).indexOf(u"]]>", i + 9);
                if (close < 0)
                    return fail();
                end = close + 3;
            } else {
                const qsizetype close = QStringView(p, n).indexOf(u'>', i + 2);
                if (close < 0)
                    return fail();
                end = close + 1;
            }
            i = end;                    // a comment is no element: not a block
            continue;
        }

        if (c1 == u'/') {
            qsizetype k = i + 2;
            if (k >= n || !isAsciiAlpha(p[k]))
                return fail();
            const std::string name = readTagName(p, n, k);
            const quint32 flags = flagsOf(name);
            bool selfClosing = false;
            if (!skipAttributes(p, n, k, selfClosing))
                return fail();
            const bool wasOpen = !stack.empty();
            if (!stack.empty() && stack.back().name == name) {
                pop();
            } else if (inForeign) {
                // Foreign content pops to the matching element, through other
                // foreign elements only.
                const qsizetype j = findOpen(name);
                if (j < 0)
                    return fail();
                for (size_t m = size_t(j) + 1; m < stack.size(); ++m)
                    if (!stack[m].foreign)
                        return fail();
                while (qsizetype(stack.size()) > j)
                    pop();
            } else {
                // </p> and </br> without their element create one; a heading
                // end tag closes whichever heading is open.
                if ((flags & (Unsupported | Heading)) || name == "p" || name == "br")
                    return fail();
                const qsizetype j = findOpen(name);
                if (j >= 0) {
                    // Popping through a formatting element leaves it to be
                    // reconstructed further on -- possibly in the next block.
                    // A scope boundary hides the element from the end tag. A
                    // special element stops recovery for all but block end
                    // tags (and starts the adoption agency for formatting ones).
                    for (size_t m = size_t(j) + 1; m < stack.size(); ++m) {
                        const quint32 f = stack[m].flags;
                        if ((f & (Formatting | ScopeBoundary)) || stack[m].foreign
                            || (!(flags & BlockEnd) && (f & Special)))
                            return fail();
                    }
                    while (qsizetype(stack.size()) > j)
                        pop();
                }
                // Not open at all: the browser ignores the stray end tag too.
            }
            i = k;
            if (wasOpen)
                closeBlock(i);
            continue;
        }

        if (!isAsciiAlpha(c1)) {
            if (stack.empty())
                return fail();          // a literal '<' as top-level text
            ++i;
            continue;
        }

        const qsizetype tagStart = i;
        qsizetype k = i + 1;
        const std::string name = readTagName(p, n, k);
        const quint32 flags = flagsOf(name);
        bool selfClosing = false;
        if (!skipAttributes(p, n, k, selfClosing))
            return fail();

        const bool parentForeign = inForeign
            && stack.back().name != "foreignobject" && stack.back().name != "desc"
            && stack.back().name != "title";
        const bool foreign = parentForeign || name == "svg";
        if (parentForeign && (flags & Breakout))
            return fail();              // the parser leaves the <svg> for it
        if (!foreign) {
            if (flags & Unsupported)
                return fail();
            if (paragraphs > 0 && (flags & ClosesP))
                return fail();
            const quint32 parent = stack.empty() ? 0u : stack.back().flags;
            if ((parent & TableContext) && !(flags & TablePart))
                return fail();          // fostered out in front of the table
            if ((flags & TablePart) && !(parent & TableContext)
                && name != "script" && name != "style" && name != "template")
                return fail();          // ignored, or closes the open cell
            if ((flags & Heading) && (parent & Heading))
                return fail();
            if (name == "a" || name == "nobr") {
                // A new <a> closes the open one (a [TOC] link holding a
                // footnote link). With nothing special or formatting above it
                // the adoption agency just pops to it; anything else moves
                // nodes around.
                if (const qsizetype j = findOpen(name); j >= 0) {
                    for (size_t m = size_t(j) + 1; m < stack.size(); ++m)
                        if ((stack[m].flags & (Special | Formatting)) || stack[m].foreign)
                            return fail();
                    while (qsizetype(stack.size()) > j)
                        pop();
                    closeBlock(tagStart);
                }
            }
            if (name == "button" && findOpen(name) >= 0)
                return fail();
            if (flags & ListItem) {
                for (qsizetype j = qsizetype(stack.size()) - 1; j >= 0; --j) {
                    const Open& o = stack[size_t(j)];
                    const bool sameKind = name == "li" ? o.name == "li"
                                                       : (o.name == "dd" || o.name == "dt");
                    if (sameKind)
                        return fail();  // the new item closes the open one
                    if ((o.flags & Special) && o.name != "address" && o.name != "div"
                        && o.name != "p")
                        break;
                }
            }
        }
        i = k;
        if (stack.empty())
            blockStart = tagStart;

        if (foreign ? selfClosing : bool(flags & Void)) {
            closeBlock(i);
            continue;
        }
        stack.push_back(Open{name, flags, foreign});
        if (name == "p" && !foreign)
            ++paragraphs;
        if (!foreign && (flags & RawText)) {
            const std::u16string closeTag = u"</" + std::u16string(name.begin(), name.end());
            const qsizetype close = indexOfCaseless(p, n, i,
                QStringView(closeTag.data(), qsizetype(closeTag.size())));
            if (close < 0)
                return fail();
            i = close;                  // the end tag is consumed above
        }
    }
    if (!stack.empty())
        return fail();
    return out;
}

qsizetype Patch::insertedBlocks() const
{
    qsizetype total = 0;
    for (const Op& op : ops)
        total += op.insertCount;
    return total;
}

qsizetype Patch::keptBlocks() const
{
    return newCount - insertedBlocks();
}

qsizetype Patch::movedBlocks() const
{
    qsizetype total = 0;
    for (const auto& range : moved)
        total += range.second - range.first + 1;
    return total;
}

Patch diff(const PreviewBlocks& before, const PreviewBlocks& after)
{
    Patch patch;
    const qsizetype oldCount = before.count();
    const qsizetype newCount = after.count();
    patch.oldCount = oldCount;
    patch.newCount = newCount;

    // Same walks as applyBlocks() in preview.js, so the page ends exactly where
    // a whole-body push would have left it.
    ShiftModel model;
    QList<qsizetype> moved;
    auto keep = [&](qsizetype o, qsizetype nw) {
        const Match r = matchBlock(before.block(o), after.block(nw), model, true);
        if (r == Match::Moved)
            moved.append(o);
        return r != Match::Different;
    };
    auto fits = [&](qsizetype o, qsizetype nw) {
        return matchBlock(before.block(o), after.block(nw), model, false) != Match::Different;
    };

    QList<Op> swaps;
    qsizetype head = 0;
    const qsizetype limit = std::min(oldCount, newCount);
    while (head < limit) {
        if (!keep(head, head)) {
            if (head + 1 >= limit || !fits(head + 1, head + 1))
                break;
            swaps.append(Op{head, 1, head, 1});
        }
        ++head;
    }

    qsizetype tail = 0;
    const qsizetype tailLimit = limit - head;
    while (tail < tailLimit) {
        if (!keep(oldCount - 1 - tail, newCount - 1 - tail)) {
            if (tail + 1 >= tailLimit || !fits(oldCount - 2 - tail, newCount - 2 - tail))
                break;
            swaps.append(Op{oldCount - 1 - tail, 1, newCount - 1 - tail, 1});
        }
        ++tail;
    }

    patch.ops = swaps;
    const qsizetype removeCount = oldCount - head - tail;
    const qsizetype addCount = newCount - head - tail;
    if (removeCount > 0 || addCount > 0)
        patch.ops.append(Op{head, removeCount, head, addCount});
    std::sort(patch.ops.begin(), patch.ops.end(),
              [](const Op& a, const Op& b) { return a.oldIndex < b.oldIndex; });

    if (!moved.isEmpty()) {
        patch.shiftLine = int(model.minMoved);
        patch.shiftDelta = int(model.delta);
        std::sort(moved.begin(), moved.end());
        for (qsizetype o : moved) {
            if (!patch.moved.isEmpty() && patch.moved.last().second + 1 == o)
                patch.moved.last().second = o;
            else
                patch.moved.append({o, o});
        }
    }
    return patch;
}

void appendJsonString(QString& out, QStringView s)
{
    static const char16_t hex[] = u"0123456789abcdef";
    out.reserve(out.size() + s.size() + s.size() / 16 + 2);
    out.append(u'"');
    const char16_t* p = reinterpret_cast<const char16_t*>(s.data());
    const qsizetype n = s.size();
    qsizetype run = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t c = p[i];
        const bool high = c >= 0xD800 && c <= 0xDBFF;
        const bool low = c >= 0xDC00 && c <= 0xDFFF;
        if (c >= 0x20 && c != u'"' && c != u'\\' && !high && !low)
            continue;
        if (high && i + 1 < n && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDFFF) {
            ++i;                         // a valid pair stays as it is
            continue;
        }
        out.append(QStringView(p + run, i - run));
        run = i + 1;
        switch (c) {
        case u'"':  out.append(u"\\\""); break;
        case u'\\': out.append(u"\\\\"); break;
        case u'\n': out.append(u"\\n"); break;
        case u'\r': out.append(u"\\r"); break;
        case u'\t': out.append(u"\\t"); break;
        default:
            if (high || low) {
                out.append(u"\\ufffd");
            } else {
                const char16_t esc[] = {u'\\', u'u', u'0', u'0', hex[(c >> 4) & 0xF], hex[c & 0xF]};
                out.append(QStringView(esc, 6));
            }
        }
    }
    out.append(QStringView(p + run, n - run));
    out.append(u'"');
}

QString contentFields(const QString& preparedBody, qsizetype blockCount)
{
    QString out;
    out.reserve(preparedBody.size() + preparedBody.size() / 16 + 64);
    out.append(u"\"html\":");
    appendJsonString(out, preparedBody);
    if (blockCount >= 0)
        out.append(u",\"blocks\":").append(QString::number(blockCount));
    out.append(u'}');
    return out;
}

QString patchFields(const Patch& patch, const PreviewBlocks& after)
{
    QString out;
    out.append(u"\"oldCount\":").append(QString::number(patch.oldCount));
    out.append(u",\"blocks\":").append(QString::number(patch.newCount));
    if (!patch.moved.isEmpty()) {
        out.append(u",\"shift\":[").append(QString::number(patch.shiftLine)).append(u',')
           .append(QString::number(patch.shiftDelta)).append(u"],\"moved\":[");
        for (qsizetype r = 0; r < patch.moved.size(); ++r) {
            if (r)
                out.append(u',');
            out.append(u'[').append(QString::number(patch.moved.at(r).first)).append(u',')
               .append(QString::number(patch.moved.at(r).second)).append(u']');
        }
        out.append(u']');
    }
    out.append(u",\"ops\":[");
    for (qsizetype k = 0; k < patch.ops.size(); ++k) {
        const Op& op = patch.ops.at(k);
        if (k)
            out.append(u',');
        out.append(u'[').append(QString::number(op.oldIndex)).append(u',')
           .append(QString::number(op.removeCount)).append(u',')
           .append(QString::number(op.insertCount)).append(u',');
        QStringView html;
        if (op.insertCount > 0) {
            const qsizetype from = after.begin.at(op.newIndex);
            const qsizetype to = after.end.at(op.newIndex + op.insertCount - 1);
            html = QStringView(after.body).sliced(from, to - from);
        }
        appendJsonString(out, html);
        out.append(u']');
    }
    out.append(u"]}");
    return out;
}

} // namespace PreviewPatch

PreviewBodyUpdate buildPreviewBodyUpdate(QString html, QString prepared, PreviewBlocksPtr base)
{
    PreviewBodyUpdate u;
    u.html = std::move(html);
    u.prepared = std::move(prepared);
    u.blocks = PreviewPatch::split(u.prepared);
    if (u.blocks && base) {
        u.base = std::move(base);
        u.patch = PreviewPatch::diff(*u.base, *u.blocks);
        u.hasPatch = true;
        u.patchEmpty = u.patch.isEmpty();
        if (!u.patchEmpty)
            u.patchFields = PreviewPatch::patchFields(u.patch, *u.blocks);
    } else {
        u.fullFields = PreviewPatch::contentFields(u.prepared, u.blocks ? u.blocks->count() : -1);
    }
    return u;
}
