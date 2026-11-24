#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextDocument>
#include <QDebug>
#include <functional>
#include <cstdio>
#include <QFile>
#include <QDir>
#include <QFileInfo>

#include "ExportManager.h"
#include "DocumentSummary.h"
#include "MarkdownToc.h"
#include "MarkdownExtensions.h"
#include "MarkdownPreviewRenderer.h"
#include "MathRenderer.h"
#include "PreviewPatch.h"
#include "md4c.h"

struct DocOpts { bool code = true; bool math = true; bool tables = true; bool tasks = false; };

static QString buildDocOpts(int lines, DocOpts o)
{
    QStringList out;
    int i = 0;
    while (out.size() < lines) {
        out << QStringLiteral("## Section %1").arg(i);
        out << QString();
        out << QStringLiteral("Some **bold** and *italic* text with `code` and a [link](https://x.y) number %1.").arg(i);
        out << QString();
        if (o.code && i % 5 == 0) {
            out << QStringLiteral("```cpp");
            out << QStringLiteral("int f%1(int x) { return x * %1; }").arg(i);
            out << QStringLiteral("```");
            out << QString();
        }
        if (o.math && i % 11 == 0) {
            out << QStringLiteral("Inline math $E_%1 = mc^2$ and display:").arg(i);
            out << QString();
            out << QStringLiteral("$$\\sum_{k=0}^{%1} k^2$$").arg(i);
            out << QString();
        }
        if (o.tables && i % 17 == 0) {
            out << QStringLiteral("| a | b | c |");
            out << QStringLiteral("|---|---|---|");
            out << QStringLiteral("| %1 | y | z |").arg(i);
            out << QString();
        }
        if (o.tasks && i % 7 == 0) {
            out << QStringLiteral("- [ ] task %1").arg(i);
            out << QStringLiteral("- [x] done %1").arg(i);
            out << QString();
        }
        ++i;
    }
    return out.join(QLatin1Char('\n'));
}

static QString buildDoc(int lines)
{
    QStringList out;
    int i = 0;
    while (out.size() < lines) {
        out << QStringLiteral("## Section %1").arg(i);
        out << QString();
        out << QStringLiteral("Some **bold** and *italic* text with `code` and a [link](https://x.y) number %1.").arg(i);
        out << QStringLiteral("More prose to make the paragraph realistic and reasonably long for wrapping.");
        out << QString();
        if (i % 5 == 0) {
            out << QStringLiteral("```cpp");
            out << QStringLiteral("int f%1(int x) { return x * %1; }").arg(i);
            out << QStringLiteral("std::vector<int> v; v.push_back(%1);").arg(i);
            out << QStringLiteral("```");
            out << QString();
        }
        if (i % 11 == 0) {
            out << QStringLiteral("Inline math $E_%1 = mc^2$ and display:").arg(i);
            out << QString();
            out << QStringLiteral("$$\\sum_{k=0}^{%1} k^2$$").arg(i);
            out << QString();
        }
        if (i % 17 == 0) {
            out << QStringLiteral("| a | b | c |");
            out << QStringLiteral("|---|---|---|");
            out << QStringLiteral("| %1 | y | z |").arg(i);
            out << QString();
        }
        ++i;
    }
    return out.join(QLatin1Char('\n'));
}

static void bench(const char* label, int reps, const std::function<void()>& fn)
{
    fn(); // warm
    QElapsedTimer t; t.start();
    for (int i = 0; i < reps; ++i) fn();
    const double ms = double(t.nsecsElapsed()) / 1e6 / reps;
    std::printf("%-42s %8.2f ms\n", label, ms);
    std::fflush(stdout);
}

// --emit <outDir> <file...>: write preview/export HTML for each input so two
// builds can be diffed for output equivalence.
static int emitMode(int argc, char** argv)
{
    const bool includeLayout = QString::fromLocal8Bit(argv[1]) == QLatin1String("--emit-layout");
    const QString outDir = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(outDir);
    for (int i = 3; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            continue;
        const QString md = QString::fromUtf8(in.readAll());
        const QString base = QFileInfo(path).fileName();
        for (int variant = 0; variant < (includeLayout ? 5 : 3); ++variant) {
            QString html;
            const char* suffix = "";
            if (variant == 0) {
                html = ExportManager::markdownToPreviewHtml(md, nullptr, true);
                suffix = ".preview.html";
            } else if (variant == 1) {
                html = ExportManager::markdownToHtml(md, MathRenderMode::KatexOutput, nullptr, true);
                suffix = ".katex.html";
            } else if (variant == 2) {
                html = ExportManager::markdownToHtml(md, MathRenderMode::UnicodePreview, nullptr, false);
                suffix = ".unicode.html";
            } else {
                const QString body = ExportManager::markdownToHtml(md, MathRenderMode::KatexOutput, nullptr, true);
                html = ExportManager::buildPreviewHtml(body, false, 17, variant == 4);
                suffix = variant == 4 ? ".print.html" : ".standalone.html";
            }
            QFile out(outDir + QLatin1Char('/') + base + QString::fromLatin1(suffix));
            if (out.open(QIODevice::WriteOnly))
                out.write(html.toUtf8());
        }
        std::printf("emitted %s\n", qPrintable(base));
    }
    return 0;
}

// --patch <base.html> <next.html> <out.json>: the changed-block patch the
// preview would send for that edit (tests/preview/patchcases.py feeds it to
// patchtest). out.json holds {"blocks": [old, new] or null, "patch": fields}.
static int patchMode(char** argv)
{
    auto read = [](const char* path) {
        QFile f(QString::fromLocal8Bit(path));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    auto splitReporting = [&](const char* path) {
        const QString body = read(path);
        qsizetype failedAt = -1;
        PreviewBlocksPtr blocks = PreviewPatch::split(body, &failedAt);
        if (!blocks)
            std::printf("%s: unsplittable at %lld: %s\n", path, qint64(failedAt),
                        qPrintable(body.mid(std::max<qsizetype>(0, failedAt - 200), 260)));
        return blocks;
    };
    const PreviewBlocksPtr base = splitReporting(argv[2]);
    const PreviewBlocksPtr next = splitReporting(argv[3]);
    QString json = QStringLiteral("{\"blocks\":");
    if (base && next) {
        const PreviewPatch::Patch patch = PreviewPatch::diff(*base, *next);
        json += QStringLiteral("[%1,%2],\"patch\":{").arg(base->count()).arg(next->count())
              + PreviewPatch::patchFields(patch, *next) + QLatin1Char('}');
    } else {
        json += QStringLiteral("null,\"patch\":null}");
    }
    QFile out(QString::fromLocal8Bit(argv[4]));
    if (!out.open(QIODevice::WriteOnly))
        return 1;
    out.write(json.toUtf8());
    return 0;
}

// --split <body.html> <out.json>: the [begin, end) UTF-16 span of every block
// the C++ split finds, or null, to compare against the browser's own parse.
static int splitMode(char** argv)
{
    QFile in(QString::fromLocal8Bit(argv[2]));
    if (!in.open(QIODevice::ReadOnly))
        return 1;
    const PreviewBlocksPtr blocks = PreviewPatch::split(QString::fromUtf8(in.readAll()));
    QString json;
    if (!blocks) {
        json = QStringLiteral("null");
    } else {
        QStringList spans;
        for (qsizetype i = 0; i < blocks->count(); ++i)
            spans << QStringLiteral("[%1,%2]").arg(blocks->begin.at(i)).arg(blocks->end.at(i));
        json = QLatin1Char('[') + spans.join(QLatin1Char(',')) + QLatin1Char(']');
    }
    QFile out(QString::fromLocal8Bit(argv[3]));
    if (!out.open(QIODevice::WriteOnly))
        return 1;
    out.write(json.toUtf8());
    return 0;
}

// --patch-cost <file.md> [line]: times the worker-side transport work for a
// one-character edit and for Enter at `line` (default: the middle).
static int patchCostMode(int argc, char** argv)
{
    QFile f(QString::fromLocal8Bit(argv[2]));
    if (!f.open(QIODevice::ReadOnly))
        return 1;
    const QString md = QString::fromUtf8(f.readAll());
    QStringList lines = md.split(QLatin1Char('\n'));
    const int at = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() - 1 : int(lines.size() / 2);
    QStringList charEdit = lines;
    charEdit[at] += QStringLiteral(" X");
    QStringList enterEdit = lines;
    enterEdit.insert(at + 1, QString());
    QElapsedTimer t;
    t.start();
    const QString baseHtml = ExportManager::markdownToPreviewHtml(md, nullptr, true);
    std::printf("convert                 %8.1f ms  (%lld chars)\n", t.nsecsElapsed() / 1e6,
                qint64(baseHtml.size()));
    t.restart();
    qsizetype failedAt = -1;
    const PreviewBlocksPtr base = PreviewPatch::split(baseHtml, &failedAt);
    std::printf("split                   %8.1f ms  (%lld blocks)\n", t.nsecsElapsed() / 1e6,
                base ? qint64(base->count()) : -1LL);
    if (!base)
        std::printf("unsplittable at %lld: %s\n", qint64(failedAt),
                    qPrintable(baseHtml.mid(std::max<qsizetype>(0, failedAt - 300), 400)));
    t.restart();
    const QString full = PreviewPatch::contentFields(baseHtml, base ? base->count() : -1);
    std::printf("full message JSON       %8.1f ms  (%lld chars)\n", t.nsecsElapsed() / 1e6,
                qint64(full.size()));
    for (const auto& [label, edited] : {std::pair{"char edit", charEdit}, std::pair{"enter", enterEdit}}) {
        const QString html = ExportManager::markdownToPreviewHtml(edited.join(QLatin1Char('\n')), nullptr, true);
        t.restart();
        const PreviewBodyUpdate u = buildPreviewBodyUpdate(html, html, base);
        std::printf("%-9s split+diff+JSON %8.1f ms  payload %lld chars, inserted %lld, kept %lld, "
                    "renumbered %lld\n", label, t.nsecsElapsed() / 1e6,
                    qint64(u.patchFields.size()), qint64(u.patch.insertedBlocks()),
                    qint64(u.patch.keptBlocks()), qint64(u.patch.movedBlocks()));
    }
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc > 3 && (QString::fromLocal8Bit(argv[1]) == QLatin1String("--emit")
                    || QString::fromLocal8Bit(argv[1]) == QLatin1String("--emit-layout")))
        return emitMode(argc, argv);
    if (argc > 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--patch"))
        return patchMode(argv);
    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--split"))
        return splitMode(argv);
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--patch-cost"))
        return patchCostMode(argc, argv);
    const QString arg = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QString fileDoc;
    if (!arg.isEmpty() && !arg.at(0).isDigit()) {
        QFile f(arg);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::fprintf(stderr, "cannot open %s\n", qPrintable(arg));
            return 1;
        }
        fileDoc = QString::fromUtf8(f.readAll());
        std::printf("source: %s\n", qPrintable(arg));
    }
    const int lines = arg.isEmpty() || !arg.at(0).isDigit() ? 10000 : arg.toInt();
    const QString md = fileDoc.isEmpty() ? buildDoc(lines) : fileDoc;
    std::printf("document: %d lines, %lld chars\n",
                int(md.count(QLatin1Char('\n'))) + 1, qint64(md.size()));

    QTextDocument doc;
    doc.setPlainText(md);
    std::printf("blocks: %d\n", doc.blockCount());
    std::fflush(stdout);

    bench("QTextDocument::toPlainText()", 20, [&] {
        volatile int n = doc.toPlainText().size(); (void)n;
    });

    QString html;
    bench("markdownToPreviewHtml()", 10, [&] {
        html = ExportManager::markdownToPreviewHtml(md, nullptr, false);
    });
    std::printf("html size: %lld chars\n", qint64(html.size()));
    std::fflush(stdout);

    bench("markdownToHtml(KatexOutput)", 10, [&] {
        volatile int n = ExportManager::markdownToHtml(
            md, MathRenderMode::KatexOutput, nullptr).size();
        (void)n;
    });

    bench("QJsonDocument encode of body", 20, [&] {
        QJsonObject o;
        o["type"] = "content";
        o["html"] = html;
        o["seq"] = 1.0;
        volatile int n = QJsonDocument(o).toJson(QJsonDocument::Compact).size();
        (void)n;
    });

    bench("JSON + QString->wstring (postJson)", 20, [&] {
        QJsonObject o; o["html"] = html;
        const QByteArray j = QJsonDocument(o).toJson(QJsonDocument::Compact);
        volatile size_t n = QString::fromUtf8(j).toStdWString().size();
        (void)n;
    });

    bench("word count (toPlainText + countWords)", 20, [&] {
        const QString t = doc.toPlainText();
        volatile int n = DocumentSummaryAnalyzer::countWords(QStringView(t));
        (void)n;
    });

    std::printf("\n--- composition breakdown (markdownToPreviewHtml, %d lines) ---\n", lines);
    struct { const char* name; DocOpts o; } variants[] = {
        { "prose only",           DocOpts{ false, false, false, false } },
        { "prose + code",         DocOpts{ true,  false, false, false } },
        { "prose + math",         DocOpts{ false, true,  false, false } },
        { "prose + tables",       DocOpts{ false, false, true,  false } },
        { "prose + tasks",        DocOpts{ false, false, false, true  } },
        { "everything",           DocOpts{ true,  true,  true,  true  } },
    };
    for (const auto& v : variants) {
        const QString d = buildDocOpts(lines, v.o);
        bench(v.name, 5, [&] {
            volatile int n = ExportManager::markdownToPreviewHtml(d, nullptr, false).size();
            (void)n;
        });
    }

    std::printf("\n--- stage breakdown (everything, %d lines) ---\n", lines);
    {
        const QString src = md;
        bench("stage: MarkdownToc::analyze", 5, [&] {
            volatile int n = MarkdownToc::analyze(src).expandedMarkdown.size(); (void)n;
        });
        const MarkdownToc::Result toc = MarkdownToc::analyze(src);
        std::printf("   (slugs: %lld)\n", qint64(toc.slugs.size()));

        QStringList mathSnips; QVector<MathLineShift> shifts;
        bench("stage: FastMdMath::preprocess", 5, [&] {
            QStringList s; QVector<MathLineShift> sh;
            volatile int n = FastMdMath::preprocess(toc.expandedMarkdown,
                MathRenderMode::KatexOutput, &s, &sh).size(); (void)n;
        });
        const QString mathPre = FastMdMath::preprocess(toc.expandedMarkdown,
            MathRenderMode::KatexOutput, &mathSnips, &shifts);
        std::printf("   (math snippets: %lld)\n", qint64(mathSnips.size()));

        QStringList extSnips;
        bench("stage: MarkdownExtensions::preprocess", 5, [&] {
            QStringList s;
            volatile int n = MarkdownExtensions::preprocess(mathPre, &s).size(); (void)n;
        });
        const QString extPre = MarkdownExtensions::preprocess(mathPre, &extSnips);
        std::printf("   (ext snippets: %lld)\n", qint64(extSnips.size()));

        const QByteArray utf8 = extPre.toUtf8();
        bench("stage: MarkdownPreviewRenderer::render", 5, [&] {
            volatile int n = MarkdownPreviewRenderer::render(utf8,
                MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS, shifts).size();
            (void)n;
        });
        const QString rawHtml = MarkdownPreviewRenderer::render(utf8,
            MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS, shifts);
        std::printf("   (raw html: %lld chars)\n", qint64(rawHtml.size()));

        bench("stage: MarkdownExtensions::restore", 5, [&] {
            volatile int n = MarkdownExtensions::restore(rawHtml, extSnips).size(); (void)n;
        });
    }

    std::printf("\n--- scaling (everything) ---\n");
    for (int n : { 1000, 2000, 4000, 8000, 16000 }) {
        const QString d = buildDocOpts(n, DocOpts{});
        char label[64];
        std::snprintf(label, sizeof(label), "%d lines", n);
        bench(label, 3, [&] {
            volatile int r = ExportManager::markdownToPreviewHtml(d, nullptr, false).size();
            (void)r;
        });
    }

    return 0;
}
