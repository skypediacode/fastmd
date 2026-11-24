#include "ExportManager.h"
#include "ThemedMessageBox.h"
#include "MarkdownToc.h"
#include "AppShutdown.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QMessageBox>
#include <QHash>

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

namespace {
// waitForFinished() timing out leaves the child still running, and ~QProcess
// then kills it and waits with *no* timeout -- so a wedged headless browser or
// Pandoc turns an already-bad bounded stall into an unbounded one, on the UI
// thread, with the window not pumping messages. Always leave the scope with the
// child actually reaped.
bool waitForProcessBounded(QProcess& process, int startMs, int finishMs)
{
    if (!process.waitForStarted(startMs)) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    constexpr int pollMs = 200;
    int remaining = finishMs;
    while (remaining > 0) {
        if (process.waitForFinished(qMin(pollMs, remaining)))
            return true;
        if (AppShutdown::started())
            break;
        remaining -= pollMs;
    }
    process.kill();
    process.waitForFinished(2000);
    return false;
}
} // namespace

namespace {

// Wraps standalone $$...$$ / \[...\] display-math blocks in a Pandoc fenced
// div with a "FastMDCenteredMath" custom-style attribute. Pandoc's ODT writer
// otherwise centers display math purely via frame positioning
// (style:horizontal-pos="center"); LibreOffice honors that, but Word's ODT
// import flattens the frame and left-aligns the resulting paragraph. The
// custom-style forces an explicit fo:text-align="center" on the paragraph
// itself (defined in the bundled reference-fastmd.odt), which survives that
// flattening. Only block-form math -- delimiter alone at the start of a line
// -- is wrapped, so inline math running alongside text is left untouched.
static QString wrapOdtDisplayMath(const QString& markdown)
{
    QString out;
    out.reserve(markdown.size());

    bool inFence = false;
    QChar fenceChar;
    bool inInlineCode = false;
    const int n = markdown.size();

    int i = 0;
    while (i < n) {
        if (!inInlineCode && (i == 0 || markdown.at(i - 1) == QLatin1Char('\n'))) {
            int lineEnd = markdown.indexOf(QLatin1Char('\n'), i);
            if (lineEnd < 0) lineEnd = n;
            int j = i;
            while (j < lineEnd && markdown.at(j).isSpace())
                ++j;
            int fenceCount = 0;
            while (j + fenceCount < lineEnd && markdown.at(j + fenceCount) == markdown.at(j))
                ++fenceCount;
            if (fenceCount >= 3 && j < lineEnd
                && (markdown.at(j) == QLatin1Char('`') || markdown.at(j) == QLatin1Char('~'))) {
                if (!inFence) { inFence = true; fenceChar = markdown.at(j); }
                else if (fenceChar == markdown.at(j)) inFence = false;
                // Consume the leading whitespace + fence run as one step so the
                // fence's own backtick/tilde characters never reach the inline-
                // code toggle below -- otherwise the closing fence (inFence
                // just turned false here, in the same iteration) falls through
                // to that toggle and its 3 backticks flip inInlineCode on.
                out += markdown.mid(i, j + fenceCount - i);
                i = j + fenceCount;
                continue;
            }
        }

        const bool atLineStart = (i == 0 || markdown.at(i - 1) == QLatin1Char('\n'));
        const QChar ch = markdown.at(i);

        if (!inFence && ch == QLatin1Char('`')) {
            inInlineCode = !inInlineCode;
            out += ch;
            ++i;
            continue;
        }

        if (!inFence && !inInlineCode && atLineStart) {
            const bool dollarBlock = ch == QLatin1Char('$') && i + 1 < n && markdown.at(i + 1) == QLatin1Char('$');
            const bool bracketBlock = ch == QLatin1Char('\\') && i + 1 < n && markdown.at(i + 1) == QLatin1Char('[');
            if (dollarBlock || bracketBlock) {
                const QString closeDelim = dollarBlock ? QStringLiteral("$$") : QStringLiteral("\\]");
                const int end = markdown.indexOf(closeDelim, i + 2);
                // Only wrap when the block is "clean": the closer is followed
                // by end-of-string or a newline, matching the conventional
                // display-math-as-its-own-paragraph form.
                if (end > i && (end + 2 == n || markdown.at(end + 2) == QLatin1Char('\n'))) {
                    const QString block = markdown.mid(i, end + 2 - i);
                    // Fenced divs are a block-level construct and (unlike bare
                    // $$ math, which Pandoc special-cases) are only recognized
                    // when blank-line-separated from surrounding text -- without
                    // it Pandoc leaves the ":::" markers as literal paragraph
                    // text instead of parsing a Div.
                    out += QStringLiteral("\n\n::: {custom-style=\"FastMDCenteredMath\"}\n")
                         + block + QStringLiteral("\n:::\n\n");
                    i = end + 2;
                    continue;
                }
            }
        }

        out += ch;
        ++i;
    }
    return out;
}

// Resolve a FastMD export-styling asset bundled next to the executable (copied
// there at build/deploy time by CMake). Returns empty if it isn't present, so
// every call site degrades to Pandoc's own defaults instead of failing.
static QString bundledExportAsset(const QString& relPath)
{
    const QString path = QCoreApplication::applicationDirPath()
        + QLatin1Char('/') + relPath;
    return QFileInfo::exists(path) ? path : QString();
}

// Pandoc's RTF writer hardcodes table appearance directly into the generated
// row/cell control words (no template lever exists, unlike DOCX/ODT), emitting
// only a plain black rule under the header row and no grid at all. This patches
// the already-written .rtf text to add a uniform light-gray grid on every cell,
// header shading + bold, matching the app's HTML/PDF table look as closely as
// RTF's per-cell (not per-table) border model allows.
static QString patchRtfTableStyling(const QString& rtf)
{
    static const QString colorTblOld = QStringLiteral("{\\colortbl;\\red255\\green0\\blue0;\\red9\\green105\\blue218;}");
    static const QString colorTblNew = QStringLiteral("{\\colortbl;\\red255\\green0\\blue0;\\red9\\green105\\blue218;\\red208\\green215\\blue222;\\red240\\green242\\blue244;}");
    if (!rtf.contains(colorTblOld))
        return rtf; // Pandoc/template output changed shape -- leave untouched.

    // Pandoc emits CRLF line endings; normalize to bare \n for matching below
    // and restore CRLF on the way out.
    QString patched = rtf;
    patched.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    patched.replace(colorTblOld, colorTblNew);
    // Color index 3 = grid border (#D0D7DE), index 4 = header shading (#F0F2F4).

    static const QString rowPrefix = QStringLiteral("\\trowd \\trgaph120\n");
    static const QString rowMiddleEnd = QStringLiteral("\n\\trkeep\\intbl");
    static const QString headerMarker = QStringLiteral("\\clbrdrb\\brdrs");
    static const QString cellxTok = QStringLiteral("\\cellx");
    static const QString borderCodes = QStringLiteral(
        "\\clbrdrt\\brdrs\\brdrw10\\brdrcf3"
        "\\clbrdrl\\brdrs\\brdrw10\\brdrcf3"
        "\\clbrdrb\\brdrs\\brdrw10\\brdrcf3"
        "\\clbrdrr\\brdrs\\brdrw10\\brdrcf3");
    static const QString headerShade = QStringLiteral("\\clshdng10000\\clcbpat4");
    static const QString rowTerminator = QStringLiteral("\\intbl\\row}");
    static const QString cellParaPrefix = QStringLiteral("\\pard\\intbl \\ql \\f0 \\sa0 \\li0 \\fi0 ");
    static const QString parEnd = QStringLiteral("\\par");

    QString out;
    out.reserve(patched.size() + 4096);
    int pos = 0;
    while (true) {
        const int start = patched.indexOf(rowPrefix, pos);
        if (start < 0) { out += patched.mid(pos); break; }
        const int midStart = start + rowPrefix.size();
        const int midEnd = patched.indexOf(rowMiddleEnd, midStart);
        if (midEnd < 0) { out += patched.mid(pos); break; }
        const int termPos = patched.indexOf(rowTerminator, midEnd);
        if (termPos < 0) { out += patched.mid(pos); break; }

        out += patched.mid(pos, start - pos);

        const QString middle = patched.mid(midStart, midEnd - midStart);
        const bool isHeader = middle.contains(headerMarker);

        QString newMiddle;
        int p = 0;
        while (true) {
            const int c = middle.indexOf(cellxTok, p);
            if (c < 0) break;
            int numEnd = c + cellxTok.size();
            while (numEnd < middle.size() && middle.at(numEnd).isDigit()) ++numEnd;
            if (isHeader) newMiddle += headerShade;
            newMiddle += borderCodes;
            newMiddle += middle.mid(c, numEnd - c);
            p = numEnd;
        }

        QString cellContent = patched.mid(midEnd + rowMiddleEnd.size(), termPos - (midEnd + rowMiddleEnd.size()));
        if (isHeader) {
            QString bolded;
            int cp = 0;
            while (true) {
                const int pfx = cellContent.indexOf(cellParaPrefix, cp);
                if (pfx < 0) { bolded += cellContent.mid(cp); break; }
                const int textStart = pfx + cellParaPrefix.size();
                const int parPos = cellContent.indexOf(parEnd, textStart);
                if (parPos < 0) { bolded += cellContent.mid(cp); break; }
                bolded += cellContent.mid(cp, pfx - cp);
                bolded += cellParaPrefix;
                bolded += QStringLiteral("\\b ");
                bolded += cellContent.mid(textStart, parPos - textStart);
                bolded += QStringLiteral("\\b0");
                bolded += parEnd;
                cp = parPos + parEnd.size();
            }
            cellContent = bolded;
        }

        out += rowPrefix + newMiddle + rowMiddleEnd + cellContent + rowTerminator;
        pos = termPos + rowTerminator.size();
    }
    out.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    return out;
}

// Pandoc's ODT writer hardcodes each table's per-cell border styles directly
// into content.xml (fo:border="none"), ignoring --reference-doc entirely for
// this -- unlike named paragraph/character styles (e.g. header bold comes from
// the reference doc's "Table Heading" style and needs no patching). This
// rewrites the just-written .odt in place to give cells a uniform light-gray
// border and the header row a background fill, matching the HTML/PDF look.
static void patchOdtTableBorders(const QString& odtPath)
{
    QZipReader reader(odtPath);
    if (!reader.isReadable())
        return;

    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    QHash<QString, QByteArray> data;
    for (const auto& info : entries) {
        if (info.isFile)
            data.insert(info.filePath, reader.fileData(info.filePath));
    }
    reader.close();

    QString content = QString::fromUtf8(data.value(QStringLiteral("content.xml")));
    if (content.isEmpty())
        return;

    static const QRegularExpression headerCellStyleRe(
        QStringLiteral(R"(<style:style style:name="TableHeaderRowCell" style:family="table-cell">\s*<style:table-cell-properties fo:border="none"\s*/>\s*</style:style>)"));
    static const QRegularExpression rowCellStyleRe(
        QStringLiteral(R"(<style:style style:name="TableRowCell" style:family="table-cell">\s*<style:table-cell-properties fo:border="none"\s*/>\s*</style:style>)"));

    if (!content.contains(headerCellStyleRe) || !content.contains(rowCellStyleRe))
        return; // Pandoc's output shape changed -- leave the file untouched.

    content.replace(headerCellStyleRe, QStringLiteral(
        "<style:style style:name=\"TableHeaderRowCell\" style:family=\"table-cell\">"
        "<style:table-cell-properties fo:border=\"0.5pt solid #D0D7DE\" fo:background-color=\"#F0F2F4\" />"
        "</style:style>"));
    content.replace(rowCellStyleRe, QStringLiteral(
        "<style:style style:name=\"TableRowCell\" style:family=\"table-cell\">"
        "<style:table-cell-properties fo:border=\"0.5pt solid #D0D7DE\" />"
        "</style:style>"));
    data[QStringLiteral("content.xml")] = content.toUtf8();

    const QString tempPath = odtPath + QStringLiteral(".tmp");
    QFile::remove(tempPath);
    {
        QZipWriter writer(tempPath);
        for (const auto& info : entries) {
            if (info.isDir) {
                writer.addDirectory(info.filePath);
            } else if (info.isFile) {
                writer.setCompressionPolicy(info.filePath == QStringLiteral("mimetype")
                    ? QZipWriter::NeverCompress : QZipWriter::AutoCompress);
                writer.addFile(info.filePath, data.value(info.filePath));
            }
        }
        writer.close();
        if (writer.status() != QZipWriter::NoError) {
            QFile::remove(tempPath);
            return;
        }
    }

    QFile::remove(odtPath);
    QFile::rename(tempPath, odtPath);
}

// The editor command deliberately writes literal HTML because that is the
// portable source representation used by the live renderer. Pandoc recognizes
// ==mark== as a native Span, however, whereas raw HTML is discarded by several
// non-HTML writers. Convert only exact, balanced <mark> tags outside code.
// Attribute-bearing HTML is left alone: it was authored markup, not the
// command's wire format, and rewriting it could lose semantics.
static QString normalizeMarksForPandoc(const QString& markdown, bool rtf)
{
    const QString open = QStringLiteral("<mark>");
    const QString close = QStringLiteral("</mark>");
    QString out;
    out.reserve(markdown.size());
    bool inFence = false;
    QChar fence;

    for (int i = 0; i < markdown.size();) {
        const bool lineStart = i == 0 || markdown.at(i - 1) == QLatin1Char('\n');
        if (lineStart) {
            int first = i;
            while (first < markdown.size() && (markdown.at(first) == QLatin1Char(' ') || markdown.at(first) == QLatin1Char('\t')))
                ++first;
            if (first + 2 < markdown.size()
                && (markdown.mid(first, 3) == QStringLiteral("```") || markdown.mid(first, 3) == QStringLiteral("~~~"))) {
                const QChar currentFence = markdown.at(first);
                if (!inFence) { inFence = true; fence = currentFence; }
                else if (fence == currentFence) inFence = false;
            }
        }

        if (inFence) {
            out += markdown.at(i++);
            continue;
        }

        // Copy complete inline-code spans unchanged. A run length is used so
        // code containing a literal single backtick remains protected too.
        if (markdown.at(i) == QLatin1Char('`')) {
            int run = 1;
            while (i + run < markdown.size() && markdown.at(i + run) == QLatin1Char('`'))
                ++run;
            const QString delimiter(run, QLatin1Char('`'));
            const int end = markdown.indexOf(delimiter, i + run);
            if (end >= 0) {
                out += markdown.mid(i, end + run - i);
                i = end + run;
                continue;
            }
        }

        if ((i == 0 || markdown.at(i - 1) != QLatin1Char('\\'))
            && markdown.mid(i, open.size()).compare(open, Qt::CaseInsensitive) == 0) {
            const int closeAt = markdown.indexOf(close, i + open.size(), Qt::CaseInsensitive);
            if (closeAt >= 0) {
                const QString content = markdown.mid(i + open.size(), closeAt - i - open.size());
                // Pandoc's == delimiter is unambiguous only when it is not
                // already part of the selected source. Keep exotic/manual HTML
                // intact instead of risking a different parse.
                if (!content.contains(QStringLiteral("=="))) {
                    if (rtf) {
                        // Pandoc's RTF writer drops native mark Spans. Raw RTF
                        // controls survive; patchRtfMarkHighlight() resolves
                        // this sentinel to a real color-table index afterward.
                        out += QStringLiteral("`\\highlight9999 `{=rtf}") + content
                            + QStringLiteral("`\\highlight0`{=rtf}");
                    } else {
                        out += QStringLiteral("==") + content + QStringLiteral("==");
                    }
                    i = closeAt + close.size();
                    continue;
                }
            }
        }
        out += markdown.at(i++);
    }
    return out;
}

// Replace the RTF sentinel emitted above with a real highlight color. Appending
// to an existing table avoids perturbing Pandoc's own text/link color indices;
// documents with no table receive the smallest valid one.
static QString patchRtfMarkHighlight(QString rtf)
{
    static const QString sentinel = QStringLiteral("\\highlight9999");
    if (!rtf.contains(sentinel))
        return rtf;

    const QString color = QStringLiteral("\\red255\\green243\\blue184;");
    const int colorStart = rtf.indexOf(QStringLiteral("{\\colortbl"));
    int index = 1;
    if (colorStart >= 0) {
        const int colorEnd = rtf.indexOf(QLatin1Char('}'), colorStart);
        if (colorEnd >= 0) {
            const QString table = rtf.mid(colorStart, colorEnd - colorStart);
            index = table.count(QLatin1Char(';'));
            rtf.insert(colorEnd, color);
        }
    } else {
        const int headerEnd = rtf.indexOf(QLatin1Char(' '));
        const int insertAt = headerEnd >= 0 ? headerEnd : 0;
        rtf.insert(insertAt, QStringLiteral("{\\colortbl;") + color + QStringLiteral("}"));
    }
    rtf.replace(sentinel, QStringLiteral("\\highlight%1").arg(index));
    return rtf;
}

static QString normalizeCalloutsForPandoc(const QString& markdown)
{
    QStringList lines = markdown.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    static const QRegularExpression marker(
        QStringLiteral(R"(^([ \t]*>[ \t]*)\\?\[!([A-Za-z]+)\\?\][ \t]*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QStringList supported = { QStringLiteral("NOTE"), QStringLiteral("TIP"),
        QStringLiteral("WARNING"), QStringLiteral("IMPORTANT"), QStringLiteral("CAUTION") };
    bool inFence = false;
    QChar fence;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
            const QChar current = trimmed.at(0);
            if (!inFence) { inFence = true; fence = current; }
            else if (fence == current) inFence = false;
            continue;
        }
        if (inFence) continue;
        const QRegularExpressionMatch match = marker.match(lines.at(i));
        if (!match.hasMatch() || !supported.contains(match.captured(2).toUpper())) continue;
        const QString previous = i > 0 ? lines.at(i - 1).trimmed() : QString();
        if (!previous.isEmpty() && previous.startsWith(QLatin1Char('>'))) continue;
        lines[i] = match.captured(1) + QStringLiteral("**")
            + match.captured(2).toUpper() + QStringLiteral("**");
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

// ---------------------------------------------------------------------------
QString ExportManager::odtReferenceDocPath()
{
    // Bundled next to the executable (copied there at build/deploy time). This
    // is Pandoc's own default reference.odt plus one extra paragraph style,
    // "FastMDCenteredMath" -- see resources/odt/reference-fastmd.odt.
    const QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/odt/reference-fastmd.odt");
    return QFileInfo::exists(path) ? path : QString();
}

// ---------------------------------------------------------------------------
QString ExportManager::docxReferenceDocPath()
{
    // Bundled next to the executable (copied there at build/deploy time). This
    // is Pandoc's own default reference.docx with the "Table" style given a
    // bordered grid + header shading matching the app's HTML/PDF table look --
    // see resources/docx/reference-fastmd.docx.
    const QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/docx/reference-fastmd.docx");
    return QFileInfo::exists(path) ? path : QString();
}

// ---------------------------------------------------------------------------
QString ExportManager::pandocExecutablePath()
{
    // 1. Bundled copy next to the executable (shipped by the installer/portable build).
    const QString bundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/tools/pandoc/pandoc.exe");
    if (QFileInfo::exists(bundled))
        return bundled;

    // 2. Fall back to a system-installed "pandoc" on PATH.
    return QStandardPaths::findExecutable(QStringLiteral("pandoc"));
}

// ---------------------------------------------------------------------------
bool ExportManager::exportDocx(const QString& markdown, const QString& filePath, const QString& docPath, QWidget* parent)
{
    return exportViaPandoc(markdown, filePath, docPath, QString(), QStringLiteral("DOCX"), {}, parent);
}

// ---------------------------------------------------------------------------
bool ExportManager::exportViaPandoc(const QString& markdown, const QString& filePath, const QString& docPath,
                                    const QString& pandocFormat, const QString& formatLabel,
                                    const QStringList& extraArgs, QWidget* parent)
{
    const QString pandoc = pandocExecutablePath();
    if (pandoc.isEmpty()) {
        if (parent) {
            themedWarning(parent, QObject::tr("Export Failed"),
                QObject::tr("Pandoc could not be found.\n\n"
                            "FastMD looks for a bundled copy at tools/pandoc/pandoc.exe "
                            "next to the application, or a system installation of pandoc "
                            "on PATH."));
        }
        return false;
    }

    // Write the raw Markdown to a temp .md file next to the source document (if
    // any) so relative image paths resolve, then let Pandoc convert it.
    const QString tempDir = docPath.isEmpty()
        ? QDir::tempPath()
        : QFileInfo(docPath).absolutePath();
    QTemporaryFile tempMd(QDir(tempDir).filePath(QStringLiteral(".fastmd_export_XXXXXX.md")));
    tempMd.setAutoRemove(true);

    // FastMD's page-break command inserts a raw-HTML div that only the WebView2
    // preview/HTML/PDF paths understand. Pandoc's LaTeX and RTF writers drop
    // unrecognized raw HTML silently, so translate it into a writer-specific
    // raw block (consumed via the raw_attribute reader extension below) that
    // actually forces a page break in the generated document.
    // Keep the inline [TOC] placeholder non-destructive in the editor while
    // giving Pandoc the same generated list used by the MD4C HTML paths.
    QString effectiveMarkdown = MarkdownToc::analyze(markdown).expandedMarkdown;
    effectiveMarkdown = normalizeCalloutsForPandoc(effectiveMarkdown);
    effectiveMarkdown = normalizeMarksForPandoc(effectiveMarkdown,
                                                 formatLabel == QStringLiteral("RTF"));
    static const QRegularExpression pageBreakDivRe(
        QStringLiteral(R"(<div\s+style\s*=\s*"break-after:\s*page;?"\s*>\s*</div>)"),
        QRegularExpression::CaseInsensitiveOption);
    if (formatLabel == QStringLiteral("LaTeX"))
        effectiveMarkdown.replace(pageBreakDivRe, QStringLiteral("\n\n```{=latex}\n\\newpage\n```\n\n"));
    else if (formatLabel == QStringLiteral("RTF"))
        effectiveMarkdown.replace(pageBreakDivRe, QStringLiteral("\n\n```{=rtf}\n\\page\n```\n\n"));

    // Inject the bundled FastMD styling asset for each format so exported
    // documents share the app's visual identity (sans typography, blue links,
    // light code blocks, clean tables) instead of Pandoc's generic defaults.
    // Each is optional: if the asset is missing we fall back to Pandoc defaults.
    QStringList effectiveExtraArgs = extraArgs;
    if (formatLabel == QStringLiteral("DOCX")) {
        const QString refDoc = docxReferenceDocPath();
        if (!refDoc.isEmpty())
            effectiveExtraArgs << QStringLiteral("--reference-doc=") + QDir::toNativeSeparators(refDoc);
    } else if (formatLabel == QStringLiteral("ODT")) {
        // Force display math to stay centered even in viewers (Word) that flatten
        // Pandoc's centered math frame back to a plain paragraph -- see
        // wrapOdtDisplayMath() above. The reference doc also carries FastMD styles.
        effectiveMarkdown = wrapOdtDisplayMath(effectiveMarkdown);
        const QString refDoc = odtReferenceDocPath();
        if (!refDoc.isEmpty())
            effectiveExtraArgs << QStringLiteral("--reference-doc=") + QDir::toNativeSeparators(refDoc);
    } else if (formatLabel == QStringLiteral("EPUB")) {
        const QString css = bundledExportAsset(QStringLiteral("epub/fastmd-epub.css"));
        if (!css.isEmpty())
            effectiveExtraArgs << QStringLiteral("--css=") + QDir::toNativeSeparators(css);
    } else if (formatLabel == QStringLiteral("LaTeX")) {
        const QString header = bundledExportAsset(QStringLiteral("latex/fastmd-header.tex"));
        if (!header.isEmpty())
            effectiveExtraArgs << QStringLiteral("--include-in-header=") + QDir::toNativeSeparators(header);
    } else if (formatLabel == QStringLiteral("RTF")) {
        // Pandoc's RTF writer hardcodes heading sizes/weights, so the only real
        // lever is the standalone template's font table + default size — swap
        // Helvetica/Courier for the app's Segoe UI / Consolas at 11pt.
        const QString tmpl = bundledExportAsset(QStringLiteral("rtf/fastmd.rtf"));
        if (!tmpl.isEmpty())
            effectiveExtraArgs << QStringLiteral("--template=") + QDir::toNativeSeparators(tmpl);
    }

    if (!tempMd.open()) {
            if (parent)
                themedWarning(parent, QObject::tr("Export Failed"),
                                     QObject::tr("Could not create a temporary file for %1 export.").arg(formatLabel));
            return false;
    }
    QTextStream out(&tempMd);
    out.setEncoding(QStringConverter::Utf8);
    // Pandoc must receive the editor's original Markdown so it can parse
    // TeX math and emit native equations in formats that support them.
    out << effectiveMarkdown;
    tempMd.close();

    QFile::remove(filePath);

    QProcess proc;
    QStringList args;
    args << QDir::toNativeSeparators(tempMd.fileName())
         << QStringLiteral("-f")
         << QStringLiteral("markdown+autolink_bare_uris+definition_lists+emoji+fenced_divs+footnotes+gfm_auto_identifiers+mark+pipe_tables+raw_attribute+raw_html+strikeout+subscript+superscript+task_lists+tex_math_dollars+tex_math_single_backslash")
                + (renderSingleLineBreaksEnabled() ? QStringLiteral("+hard_line_breaks") : QString());
    if (!pandocFormat.isEmpty())
        args << QStringLiteral("-t") << pandocFormat;
    args << effectiveExtraArgs;
    args << QStringLiteral("-o") << QDir::toNativeSeparators(filePath);

    // Pandoc resolves relative resource paths (images, includes) against its
    // *working directory*, not the input file, so writing tempMd beside the
    // source document only pays off if the process runs from there too. Without
    // this every relative image is dropped with "Could not fetch resource" --
    // which Pandoc reports as a warning while still exiting 0.
    proc.setWorkingDirectory(tempDir);
    proc.start(pandoc, args);
    bool ok = waitForProcessBounded(proc, 10000, 60000);
    // waitForFinished() reports only that the process ended. A Pandoc that exits
    // non-zero after writing a partial file would otherwise pass as a success.
    if (ok)
        ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;

    const QString stderrText = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    const bool produced = QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0;

    if (!ok || !produced) {
        if (parent)
            themedWarning(parent, QObject::tr("Export Failed"),
                                 QObject::tr("Pandoc failed to generate the %1 file.\n\n%2")
                                     .arg(formatLabel, stderrText));
        return false;
    }

    // Pandoc warns rather than fails when an image does not make it in -- the
    // file could not be resolved, or it could not be rasterized (what an SVG
    // hits without rsvg-convert). Both leave a document that is structurally
    // fine and visually empty, so say so instead of reporting a plain success.
    if (parent && (stderrText.contains(QStringLiteral("Could not convert image"))
                   || stderrText.contains(QStringLiteral("Could not fetch resource")))) {
        themedWarning(parent, QObject::tr("Image Not Embedded"),
                             QObject::tr("The %1 file was created, but one or more images could not be "
                                         "embedded and will appear blank.\n\n%2")
                                 .arg(formatLabel, stderrText));
    }

    // Pandoc bakes borderless table styling directly into RTF/ODT output
    // regardless of --reference-doc/--template, so patch the written file
    // afterward to add FastMD's table grid. Best-effort: leaves the file
    // untouched if Pandoc's output shape doesn't match what we expect.
    if (formatLabel == QStringLiteral("RTF")) {
        QFile rtfFile(filePath);
        if (rtfFile.open(QIODevice::ReadOnly)) {
            const QString original = QString::fromLatin1(rtfFile.readAll());
            rtfFile.close();
            const QString patched = patchRtfMarkHighlight(patchRtfTableStyling(original));
            QFile out(filePath);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                out.write(patched.toLatin1());
        }
    } else if (formatLabel == QStringLiteral("ODT")) {
        patchOdtTableBorders(filePath);
    }

    return true;
}
