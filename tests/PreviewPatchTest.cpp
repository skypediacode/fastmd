// Changed-block preview transport: block splitting, the line-shift diff and
// the message encoding (src/PreviewPatch.*). Links only Qt6::Core.
//
// Every diff is also *applied* here -- renumbering the kept blocks the way
// preview.js does -- and must reproduce the new body block for block, so a
// patch that would leave the page different from a whole-body push fails.

#include "PreviewPatch.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define REQUIRE(cond) do { check((cond), #cond, __LINE__); if (!(cond)) return; } while (0)

QStringList blocksOf(const PreviewBlocksPtr& b)
{
    QStringList out;
    for (qsizetype i = 0; b && i < b->count(); ++i)
        out << b->block(i).toString();
    return out;
}

// Same rule as renumberSourceLines() in preview.js, on a string: every line
// attribute value inside a tag at or past `line` moves by `delta`.
QString renumber(const QString& block, int line, int delta)
{
    static const QRegularExpression tagRe(QStringLiteral("<[a-zA-Z][^<>]*>"));
    static const QRegularExpression numRe(QStringLiteral(
        "(\\sdata-(?:src-start|src-end|src-line|fastmd-task-start|fastmd-task-end)=\")(\\d+)\""));
    QString out;
    qsizetype last = 0;
    auto tags = tagRe.globalMatch(block);
    while (tags.hasNext()) {
        const auto tag = tags.next();
        out += block.mid(last, tag.capturedStart() - last);
        QString t = tag.captured();
        QString rebuilt;
        qsizetype tl = 0;
        auto nums = numRe.globalMatch(t);
        while (nums.hasNext()) {
            const auto m = nums.next();
            rebuilt += t.mid(tl, m.capturedStart() - tl) + m.captured(1);
            const int v = m.captured(2).toInt();
            rebuilt += QString::number(v >= line ? v + delta : v) + QLatin1Char('"');
            tl = m.capturedEnd();
        }
        rebuilt += t.mid(tl);
        out += rebuilt;
        last = tag.capturedEnd();
    }
    return out + block.mid(last);
}

// Applies `patch` to `before` the way the page does and returns the result.
QStringList apply(const PreviewBlocks& before, const PreviewBlocks& after,
                  const PreviewPatch::Patch& patch)
{
    QStringList blocks = blocksOf(std::make_shared<PreviewBlocks>(before));
    for (const auto& range : patch.moved)
        for (qsizetype o = range.first; o <= range.second; ++o)
            blocks[o] = renumber(blocks[o], patch.shiftLine, patch.shiftDelta);
    for (qsizetype k = patch.ops.size() - 1; k >= 0; --k) {
        const PreviewPatch::Op& op = patch.ops.at(k);
        for (qsizetype r = 0; r < op.removeCount; ++r)
            blocks.removeAt(op.oldIndex);
        for (qsizetype a = 0; a < op.insertCount; ++a)
            blocks.insert(op.oldIndex + a, after.block(op.newIndex + a).toString());
    }
    return blocks;
}

struct DiffResult {
    PreviewPatch::Patch patch;
    bool reproduces = false;
};

DiffResult diffAndApply(const QString& a, const QString& b)
{
    DiffResult r;
    const PreviewBlocksPtr before = PreviewPatch::split(a);
    const PreviewBlocksPtr after = PreviewPatch::split(b);
    if (!before || !after)
        return r;
    r.patch = PreviewPatch::diff(*before, *after);
    r.reproduces = apply(*before, *after, r.patch) == blocksOf(after);
    return r;
}

// ---------------------------------------------------------------------------

void testSplit()
{
    const QString body = QStringLiteral(
        "<h1 id=\"t\" data-src-start=\"1\" data-src-end=\"1\">Title</h1>\n"
        "<p data-src-start=\"3\">a<br data-src-line=\"3\">\nb &lt; c</p>\n"
        "<hr>\n"
        "<pre><code class=\"language-cpp\">if (a &lt; b) {}\n</code></pre>\n");
    const PreviewBlocksPtr b = PreviewPatch::split(body);
    REQUIRE(b != nullptr);
    REQUIRE(b->count() == 4);
    CHECK(b->block(0) == QStringLiteral("<h1 id=\"t\" data-src-start=\"1\" data-src-end=\"1\">Title</h1>"));
    CHECK(b->block(1).startsWith(QStringLiteral("<p ")) && b->block(1).endsWith(QStringLiteral("</p>")));
    CHECK(b->block(2) == QStringLiteral("<hr>"));
    CHECK(b->block(3).endsWith(QStringLiteral("</code></pre>")));

    // Comments are not elements; quoted '>' does not end a tag; raw text and
    // foreign self-closing tags; upper-case names.
    const PreviewBlocksPtr c = PreviewPatch::split(QStringLiteral(
        "<!-- top <div> -->\n<div title=\"a>b\" data-x='c>d'><!-- </div> --><img src=\"x>y\"></div>\n"
        "<script>if (a<b && c>d) { x = '</div>'; }</script>\n"
        "<style>p>a{color:red}</style>\n"
        "<svg viewBox=\"0 0 1 1\"><path d=\"M0 0\"/><circle r=\"1\"/></svg>\n"
        "<DIV>upper</div>\n<details><summary>s</summary><p>x</p></details>\n"
        "<table><tr><td>implicit tbody</td></tr></table>\n<!---->\n<!-->\n"));
    REQUIRE(c != nullptr);
    CHECK(c->count() == 7);
    CHECK(c->count() == 7 && c->block(1).startsWith(QStringLiteral("<script>")));

    // The recovery the browser does that stays inside one top-level element:
    // an unknown element left open ("Cell<T>"), and a stray end tag.
    const PreviewBlocksPtr recovered = PreviewPatch::split(QStringLiteral(
        "<ul><li><a href=\"#\"><span>Cell</span><T><span> and RefCell</span><T> x</a>.</li></ul>\n"
        "<p>a</p></div>\n<div><span>open</div>\n"));
    REQUIRE(recovered != nullptr);
    CHECK(recovered->count() == 3);
    CHECK(recovered->count() == 3 && recovered->block(1) == QStringLiteral("<p>a</p>"));

    // A [TOC] entry for a heading holding a footnote reference nests <a>: the
    // inner one closes the outer, inside the same list.
    const PreviewBlocksPtr toc = PreviewPatch::split(QStringLiteral(
        "<ul><li><a href=\"#h\"><span>Notes</span><sup><a href=\"#fn-1\">[1]</a></sup></a></li></ul>\n"
        "<a href=\"#x\">top<a href=\"#y\">level</a></a>\n"));
    REQUIRE(toc != nullptr);
    CHECK(toc->count() == 3);
    CHECK(!PreviewPatch::split(QStringLiteral("<p><a href=\"#\">x<b><a href=\"#\">y</a></b></a></p>")));
    CHECK(!PreviewPatch::split(QStringLiteral("<p><a href=\"#\">x<div><a href=\"#\">y</a></div></a></p>")));

    const PreviewBlocksPtr empty = PreviewPatch::split(QStringLiteral(" \n\t"));
    CHECK(empty && empty->count() == 0);

    // Everything the browser could parse into other top-level elements.
    const char* unsplittable[] = {
        "hello",                                   // top-level text
        "<p>a</p> &amp;",                         // a character reference is text
        "<p>a</p> < 3",                            // literal '<'
        "<b><i>x</b></i>",                         // misnested formatting
        "<ul><li><b>x</li></ul>",                  // <b> left open: reconstructed later
        "<span><div>x</span></div>",               // recovery stops at the <div>
        "<h1>a</h2>",                              // closes whichever heading is open
        "<ul><li>a<li>b</li></ul>",                // the second <li> closes the first
        "<td>cell</td>",                           // ignored outside a table
        "<table><tr><td>a<tr><td>b</td></tr></table>",  // <tr> closes the open cell
        "<svg><div>x</div></svg>",                 // <div> leaves the <svg>
        "<body>x</body>",
        "<div>unclosed",
        "<p>x<div>block in paragraph</div></p>",   // the browser ends the <p>
        "<table><div>fostered</div></table>",
        "<table>fostered text</table>",
        "<p>x</p></br>",
        "<div><!-- unterminated",
        "<script>never closed",
        "<plaintext>x",
        "<div title=\"unterminated>",
    };
    for (const char* s : unsplittable) {
        const bool rejected = !PreviewPatch::split(QString::fromUtf8(s));
        check(rejected, s, __LINE__);
    }
}

QString para(int line, const QString& text)
{
    return QStringLiteral("<p data-src-start=\"%1\" data-src-end=\"%1\"><span data-src-line=\"%1\">%2</span></p>")
        .arg(line).arg(text);
}

QString heading(int line, const QString& text)
{
    return QStringLiteral("<h2 data-src-start=\"%1\" data-src-end=\"%1\"><span data-src-line=\"%1\">%2</span></h2>")
        .arg(line).arg(text);
}

// Section i: heading at 4i+1, paragraph at 4i+3. `shift(i)` adds lines in
// front of section i; `edit` replaces section `at`'s paragraph text.
QString doc(int n, const std::function<int(int)>& shift = {}, int at = -1,
            const QString& text = QString())
{
    QStringList out;
    for (int i = 0; i < n; ++i) {
        const int s = 4 * i + 1 + (shift ? shift(i) : 0);
        out << heading(s, QStringLiteral("Section %1").arg(i));
        out << para(s + 2, i == at ? text : QStringLiteral("Prose %1").arg(i));
    }
    return out.join(QLatin1Char('\n'));
}

void testDiff()
{
    {   // identical
        const DiffResult r = diffAndApply(doc(20), doc(20));
        CHECK(r.reproduces);
        CHECK(r.patch.isEmpty());
    }
    {   // one block changed in place
        const DiffResult r = diffAndApply(doc(20), doc(20, {}, 7, QStringLiteral("Changed")));
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 1);
        CHECK(r.patch.ops[0].oldIndex == 15 && r.patch.ops[0].removeCount == 1
              && r.patch.ops[0].insertCount == 1);
        CHECK(r.patch.moved.isEmpty());
        CHECK(r.patch.keptBlocks() == 39);
    }
    {   // Enter inside section 7's paragraph: it gains a line, below moves by one
        const QString split = QStringLiteral("Pro<br data-src-line=\"31\">se 7");
        const DiffResult r = diffAndApply(doc(20), doc(20, [](int i) { return i > 7 ? 1 : 0; }, 7, split));
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 1);
        CHECK(r.patch.ops[0].oldIndex == 15 && r.patch.insertedBlocks() == 1);
        CHECK(r.patch.shiftDelta == 1);
        CHECK(r.patch.shiftLine == 33);              // section 8's heading
        REQUIRE(r.patch.moved.size() == 1);
        CHECK(r.patch.moved[0].first == 16 && r.patch.moved[0].second == 39);
        CHECK(r.patch.movedBlocks() == 24);
    }
    {   // a blank line only: renumber, rebuild nothing
        const DiffResult r = diffAndApply(doc(20), doc(20, [](int i) { return i >= 5 ? 1 : 0; }));
        CHECK(r.reproduces);
        CHECK(r.patch.ops.isEmpty());
        CHECK(r.patch.movedBlocks() == 30);
    }
    {   // Backspace join: delta -2
        const DiffResult r = diffAndApply(doc(20, [](int i) { return i >= 3 ? 2 : 0; }), doc(20));
        CHECK(r.reproduces);
        CHECK(r.patch.ops.isEmpty() && r.patch.shiftDelta == -2);
    }
    {   // insertion and deletion of whole blocks
        QString base = doc(10);
        QString inserted = base;
        inserted.replace(para(23, QStringLiteral("Prose 5")),
                         para(23, QStringLiteral("Prose 5")) + QLatin1Char('\n')
                         + para(24, QStringLiteral("New")));
        DiffResult r = diffAndApply(base, inserted);
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 1);
        CHECK(r.patch.ops[0].removeCount == 0 && r.patch.ops[0].insertCount == 1);
        r = diffAndApply(inserted, base);
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 1);
        CHECK(r.patch.ops[0].removeCount == 1 && r.patch.ops[0].insertCount == 0);
    }
    {   // a [TOC]-like block reflecting an edit elsewhere is swapped on its own
        const QString toc1 = QStringLiteral("<ul data-src-start=\"1\"><li>Section 9</li></ul>");
        const QString toc2 = QStringLiteral("<ul data-src-start=\"1\"><li>Renamed</li></ul>");
        QString a = toc1 + QLatin1Char('\n') + doc(20);
        QString b = toc2 + QLatin1Char('\n') + doc(20);
        b.replace(QStringLiteral(">Section 9<"), QStringLiteral(">Renamed<"));
        const DiffResult r = diffAndApply(a, b);
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 2);
        CHECK(r.patch.ops[0].oldIndex == 0 && r.patch.ops[0].insertCount == 1);
        CHECK(r.patch.insertedBlocks() == 2);
    }
    {   // numbers above the edit inside a moved block stay put
        const QString a = doc(20) + QLatin1Char('\n')
            + QStringLiteral("<p data-src-start=\"10\" data-src-end=\"90\"><span data-src-line=\"90\">tail</span></p>");
        const QString b = doc(20, [](int i) { return i >= 10 ? 1 : 0; }) + QLatin1Char('\n')
            + QStringLiteral("<p data-src-start=\"10\" data-src-end=\"91\"><span data-src-line=\"91\">tail</span></p>");
        const DiffResult r = diffAndApply(a, b);
        CHECK(r.reproduces);
        CHECK(r.patch.ops.isEmpty());
    }
    {   // a number that stayed while a smaller one moved fits no single edit
        const QString a = doc(20) + QLatin1Char('\n')
            + QStringLiteral("<p data-src-start=\"90\" data-src-end=\"95\">tail</p>");
        const QString b = doc(20, [](int i) { return i >= 10 ? 1 : 0; }) + QLatin1Char('\n')
            + QStringLiteral("<p data-src-start=\"91\" data-src-end=\"95\">tail</p>");
        const DiffResult r = diffAndApply(a, b);
        CHECK(r.reproduces);
        CHECK(r.patch.insertedBlocks() >= 1);
    }
    {   // attribute-like text is content, not a line number
        const QString a = doc(20) + QStringLiteral("\n<p data-src-start=\"90\"> data-src-line=\"90\" </p>");
        const QString b = doc(20, [](int i) { return i >= 10 ? 1 : 0; })
            + QStringLiteral("\n<p data-src-start=\"91\"> data-src-line=\"91\" </p>");
        const DiffResult r = diffAndApply(a, b);
        CHECK(r.reproduces);
        CHECK(r.patch.insertedBlocks() == 1);
    }
    {   // two different deltas: one run is rebuilt, never mis-numbered
        const DiffResult r = diffAndApply(doc(30), doc(30, [](int i) { return i >= 20 ? 2 : i >= 10 ? 1 : 0; }));
        CHECK(r.reproduces);
        CHECK(r.patch.insertedBlocks() > 0);
    }
    {   // broad change: everything replaced in one op
        QString b = doc(20);
        b.replace(QStringLiteral("Prose"), QStringLiteral("Text"));
        b.replace(QStringLiteral("Section"), QStringLiteral("Part"));
        const DiffResult r = diffAndApply(doc(20), b);
        CHECK(r.reproduces);
        REQUIRE(r.patch.ops.size() == 1);
        CHECK(r.patch.ops[0].removeCount == 40 && r.patch.ops[0].insertCount == 40);
    }
}

// Random edits of a section document: content changes, inserted/removed
// blocks, and line shifts at random points. Whatever the diff decides, the
// patch applied must reproduce the new body.
void testFuzz()
{
    QRandomGenerator rng(20261003);
    int failures = 0;
    for (int round = 0; round < 400; ++round) {
        const int n = 5 + int(rng.bounded(40));
        struct Sec { int extra; QString text; bool drop; };
        QList<Sec> secs(n, Sec{0, QString(), false});
        auto render = [&](const QList<Sec>& ss) {
            QStringList out;
            int line = 1;
            for (int i = 0; i < ss.size(); ++i) {
                line += ss[i].extra;
                if (ss[i].drop) { line += 4; continue; }
                out << heading(line, QStringLiteral("S%1").arg(i));
                out << para(line + 2, ss[i].text.isEmpty() ? QStringLiteral("P%1").arg(i) : ss[i].text);
                line += 4;
            }
            return out.join(QLatin1Char('\n'));
        };
        const QString a = render(secs);
        QList<Sec> next = secs;
        const int edits = 1 + int(rng.bounded(3));
        for (int e = 0; e < edits; ++e) {
            const int at = int(rng.bounded(n));
            switch (rng.bounded(4)) {
            case 0: next[at].text = QStringLiteral("Edit %1").arg(rng.bounded(1000)); break;
            case 1: next[at].extra += 1 + int(rng.bounded(3)); break;
            case 2: next[at].extra -= next[at].extra > 0 ? 1 : 0; break;
            default: next[at].drop = !next[at].drop; break;
            }
        }
        const DiffResult r = diffAndApply(a, render(next));
        if (!r.reproduces && ++failures <= 3)
            std::printf("FAIL fuzz round %d\n", round);
        ++g_checks;
    }
    g_failures += failures;
}

void testJson()
{
    QString out;
    PreviewPatch::appendJsonString(out, QStringView(u"a\"b\\c\nd\te\x01" u"\U0001F600" u"\xD800x"));
    CHECK(out == QStringLiteral("\"a\\\"b\\\\c\\nd\\te\\u0001\U0001F600\\ufffdx\""));
    const QJsonDocument parsed = QJsonDocument::fromJson(
        (QStringLiteral("{\"s\":") + out + QLatin1Char('}')).toUtf8());
    CHECK(parsed.object().value(QStringLiteral("s")).toString()
          == QString::fromUtf16(u"a\"b\\c\nd\te\x01" u"\U0001F600" u"�x"));

    const QString content = PreviewPatch::contentFields(QStringLiteral("<p>\"x\"</p>"), 1);
    CHECK(content == QStringLiteral("\"html\":\"<p>\\\"x\\\"</p>\",\"blocks\":1}"));
    CHECK(PreviewPatch::contentFields(QStringLiteral("x"), -1) == QStringLiteral("\"html\":\"x\"}"));

    // A patch message parses and carries exactly what the page needs.
    const QString split = QStringLiteral("Pro<br data-src-line=\"31\">se 7");
    const PreviewBlocksPtr a = PreviewPatch::split(doc(20));
    const PreviewBlocksPtr b = PreviewPatch::split(doc(20, [](int i) { return i > 7 ? 1 : 0; }, 7, split));
    REQUIRE(a && b);
    const PreviewPatch::Patch patch = PreviewPatch::diff(*a, *b);
    const QString fields = PreviewPatch::patchFields(patch, *b);
    const QJsonObject o = QJsonDocument::fromJson(
        (QStringLiteral("{\"type\":\"patch\",\"seq\":2,\"base\":1,") + fields).toUtf8()).object();
    CHECK(o.value(QStringLiteral("oldCount")).toInt() == 40);
    CHECK(o.value(QStringLiteral("blocks")).toInt() == 40);
    CHECK(o.value(QStringLiteral("shift")).toArray() == QJsonArray({33, 1}));
    QJsonArray moved;
    moved.append(QJsonArray({16, 39}));
    CHECK(o.value(QStringLiteral("moved")).toArray() == moved);
    const QJsonArray ops = o.value(QStringLiteral("ops")).toArray();
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].toArray() == QJsonArray({15, 1, 1, b->block(15).toString()}));

    // No base or an unsplittable body: the whole body, with its block count.
    const PreviewBodyUpdate full = buildPreviewBodyUpdate(doc(3), doc(3), nullptr);
    CHECK(!full.hasPatch && full.fullFields.endsWith(QStringLiteral(",\"blocks\":6}")));
    const PreviewBodyUpdate text = buildPreviewBodyUpdate(QStringLiteral("x"), QStringLiteral("x"), a);
    CHECK(!text.hasPatch && !text.blocks && text.fullFields == QStringLiteral("\"html\":\"x\"}"));
    const PreviewBodyUpdate same = buildPreviewBodyUpdate(doc(20), doc(20), a);
    CHECK(same.hasPatch && same.patchEmpty && same.patchFields.isEmpty() && same.fullFields.isEmpty());
    const PreviewBodyUpdate small = buildPreviewBodyUpdate(QString(), b->body, a);
    CHECK(small.hasPatch && !small.patchEmpty && small.patchFields == fields);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testSplit();
    testDiff();
    testFuzz();
    testJson();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
