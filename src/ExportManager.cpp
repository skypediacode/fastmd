#include "ExportManager.h"
#include "FontPreferences.h"
#include "MathRenderer.h"

#include <QFile>
#include <QTextStream>
#include <QPageSize>
#include <QSizeF>
#include <QRegularExpression>
#include <QFileInfo>
#include <QCoreApplication>
#include <QUrl>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

// ---------------------------------------------------------------------------
QString ExportManager::katexAssetsDir()
{
    // KaTeX is bundled next to the executable (copied there at build/deploy time).
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/katex");
    if (QFileInfo::exists(dir + QStringLiteral("/katex.min.css")))
        return dir;
    return {};
}

// Build the <head> KaTeX injection that references the bundled local assets and
// triggers auto-render on load. Returns empty when assets are unavailable.
static QString buildKatexHead(bool renderOnLoad = true)
{
    const QString dir = ExportManager::katexAssetsDir();
    if (dir.isEmpty())
        return {};

    QFile cssFile(dir + QStringLiteral("/katex.min.css"));
    QFile jsFile(dir + QStringLiteral("/katex.min.js"));
    QFile mhchemFile(dir + QStringLiteral("/contrib/mhchem.min.js"));
    QFile autoFile(dir + QStringLiteral("/contrib/auto-render.min.js"));
    if (!cssFile.open(QIODevice::ReadOnly) || !jsFile.open(QIODevice::ReadOnly)
        || !mhchemFile.open(QIODevice::ReadOnly)
        || !autoFile.open(QIODevice::ReadOnly))
        return {};

    QString css = QString::fromUtf8(cssFile.readAll());
    const QRegularExpression fontUrl(QStringLiteral(R"(url\((['"]?)([^'")]+)\1\))"));
    QRegularExpressionMatchIterator it = fontUrl.globalMatch(css);
    int offset = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString relative = match.captured(2);
        if (relative.startsWith(QStringLiteral("data:")))
            continue;
        QFile font(QDir(dir).filePath(relative));
        if (!font.open(QIODevice::ReadOnly))
            continue;
        const QByteArray encoded = font.readAll().toBase64();
        const QString replacement = QStringLiteral("url(data:font/woff2;base64,%1)").arg(QString::fromLatin1(encoded));
        css.replace(match.capturedStart() + offset, match.capturedLength(), replacement);
        offset += replacement.size() - match.capturedLength();
    }

    // Inline all KaTeX assets so exported HTML is a portable single file and
    // never leaks the application's installation path into user documents.
    return QStringLiteral(
        "<style id='katex-css'>%1</style>"
        "<script data-fastmd-asset='../katex/katex.min.js'>%2</script>"
        "<script data-fastmd-asset='../katex/contrib/mhchem.min.js'>%3</script>"
        "<script data-fastmd-asset='../katex/contrib/auto-render.min.js'>%4</script>"
        "%5"
    ).arg(css, QString::fromUtf8(jsFile.readAll()), QString::fromUtf8(mhchemFile.readAll()),
          QString::fromUtf8(autoFile.readAll()), renderOnLoad ? QStringLiteral("<script>document.addEventListener('DOMContentLoaded',function(){"
        "var d=[{left:'$$',right:'$$',display:true},"
        "{left:'\\\\[',right:'\\\\]',display:true},"
        "{left:'\\\\(',right:'\\\\)',display:false}];"
        "renderMathInElement(document.body,{delimiters:d,throwOnError:false});"
        "document.querySelectorAll('blockquote').forEach(function(e){"
        "renderMathInElement(e,{delimiters:d,throwOnError:false});"
        "e.querySelectorAll('p').forEach(function(p){var v=p.textContent.trim(),o=v.indexOf('$$')===0?'$$':v.indexOf('\\\\[')===0?'\\\\[':'';"
        "var c=o==='$$'?'$$':o==='\\\\['?'\\\\]':'';"
        "if(!o||v.slice(-c.length)!==c||v.length<=o.length+c.length)return;"
        "var s=document.createElement('span');s.style.display='block';"
        "try{katex.render(v.slice(o.length,-c.length),s,{displayMode:true,throwOnError:false});p.replaceChildren(s);}catch(x){}});"
        "});"
        "});</script>") : QString());
}

bool ExportManager::hasLabelledCodeBlock(const QString& bodyHtml)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(<pre\b[^>]*>\s*<code\b[^>]*\blanguage-(?!mermaid\b))"),
        QRegularExpression::CaseInsensitiveOption);
    return bodyHtml.contains(QStringLiteral("language-"), Qt::CaseInsensitive)
        && pattern.match(bodyHtml).hasMatch();
}

// Unlabelled fences stay plain: highlight.js would auto-detect a language for
// them, so the highlighter is only embedded when a labelled block needs it.
// Mermaid fences are rendered as diagrams and never need it either.
static QString buildHighlightHead(bool dark, bool renderOnLoad = true)
{
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/preview/highlight");
    QFile cssFile(QDir(dir).filePath(dark
        ? QStringLiteral("github-dark.min.css")
        : QStringLiteral("github.min.css")));
    QFile jsFile(QDir(dir).filePath(QStringLiteral("highlight.min.js")));
    if (!cssFile.open(QIODevice::ReadOnly) || !jsFile.open(QIODevice::ReadOnly))
        return {};

    return QStringLiteral(
        "<style id='%1'>%2pre code.hljs{background:transparent;padding:0;}</style>"
        "<script data-fastmd-asset='highlight/highlight.min.js'>%3</script>%4")
        .arg(dark ? QStringLiteral("hljs-dark") : QStringLiteral("hljs-light"),
             QString::fromUtf8(cssFile.readAll()), QString::fromUtf8(jsFile.readAll()),
             renderOnLoad ? QStringLiteral("<script>document.addEventListener('DOMContentLoaded',function(){"
                 "document.querySelectorAll('pre code[class*=\"language-\"]').forEach(function(e){try{hljs.highlightElement(e);}catch(x){}});"
                 "});</script>") : QString());
}

static QString previewAsset(const QString& relativePath)
{
    QFile file(QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/preview")).filePath(relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

static QString javascriptStringLiteral(const QString& value)
{
    QJsonArray array;
    array.append(value);
    QString json = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    // A literal </script> in the Markdown would otherwise terminate the
    // bootstrap script despite being valid JSON inside it.
    json.replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
    json.replace(QLatin1Char('>'), QStringLiteral("\\u003e"));
    json.replace(QLatin1Char('&'), QStringLiteral("\\u0026"));
    return json.mid(1, json.size() - 2);
}

// ---------------------------------------------------------------------------
static QString buildCss(bool dark, int fontSize, bool isPrint)
{
    auto c = [dark](const char* ld, const char* dk) -> QLatin1StringView {
        return dark ? QLatin1StringView(dk) : QLatin1StringView(ld);
    };

    // Palette + typography deliberately mirror resources/preview/preview.css so
    // exported HTML and browser-rendered PDF match the on-screen preview. The
    // isPrint variant stays QTextDocument-safe (solid colors, no rgba/radius the
    // fallback engine can't render — it simply ignores what it doesn't grok).
    // Heading FONT SIZES are applied by buildFullHtml's inline transforms (a
    // QTextDocument shim), not here; keep the scale below in sync with the one in
    // buildFullHtml and with HEADING_SCALE in preview.js.
    QLatin1StringView bg        = isPrint ? QLatin1StringView("#ffffff") : c("#f9f9f9", "#121314");
    QLatin1StringView fg        = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#bbbebf");
    QLatin1StringView muted     = isPrint ? QLatin1StringView("#57606a") : c("#57606a", "#8c8c8c");
    QLatin1StringView codeBg    = isPrint ? QLatin1StringView("#f0f0f0") : c("#ececef", "#202122");
    QLatin1StringView codeFg    = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#c5c8ca");
    QLatin1StringView codeBdr   = isPrint ? QLatin1StringView("#e2e2e2") : c("rgba(27,31,36,0.09)", "rgba(255,255,255,0.09)");
    QLatin1StringView preBg     = isPrint ? QLatin1StringView("#f8f8f8") : c("#ffffff", "#191a1b");
    QLatin1StringView preBdr    = isPrint ? QLatin1StringView("#d8dde3") : c("#d8dde3", "#2a2b2c");
    QLatin1StringView markBg    = isPrint ? QLatin1StringView("#fff3b8") : c("#fff3b8", "#b6892b");
    QLatin1StringView markFg    = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#1b1f24");
    QLatin1StringView bqFg      = isPrint ? QLatin1StringView("#3d444d") : c("#57606a", "#9b9d9f");
    QLatin1StringView bqBdr     = c("#d0d7de", "#3a3b3c");
    QLatin1StringView bqBg      = isPrint ? QLatin1StringView("#f6f6f6") : c("rgba(27,31,36,0.025)", "rgba(255,255,255,0.03)");
    QLatin1StringView link      = c("#0969da", "#58a6ff");
    QLatin1StringView border    = c("#d0d7de", "#2a2b2c");
    QLatin1StringView borderSoft= isPrint ? QLatin1StringView("#e4e8ec") : c("#e4e8ec", "#242526");
    QLatin1StringView tableHead = isPrint ? QLatin1StringView("#f0f2f4") : c("#f0f2f4", "#191a1b");
    QLatin1StringView tableCellBdr = isPrint ? QLatin1StringView("#dee2e7") : c("#dee2e7", "#242526");
    QLatin1StringView stripe    = isPrint ? QLatin1StringView("#f6f8fa") : c("#f4f6f8", "#161718");

    QLatin1StringView marginStyle = isPrint ? QLatin1StringView("0") : QLatin1StringView("24px 30px 44px");
    QLatin1StringView bodyLh      = isPrint ? QLatin1StringView("1.6") : QLatin1StringView("1.7");
    const QString fs = QString::number(fontSize);
    const QString codeFontFamily =
        QStringLiteral("'JetBrains Mono','IBM Plex Mono','Cascadia Code','Cascadia Mono',Consolas,monospace");

    // Built with explicit concatenation (not %n placeholders) so the ~20 themed
    // values stay readable and can't drift out of numeric alignment.
    QString css;
    css += QStringLiteral("*{box-sizing:border-box;}");
    css += QStringLiteral("body{background-color:") + bg + QStringLiteral(";color:") + fg
        +  QStringLiteral(";font-family:'Segoe UI','SF Pro Text',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif;")
        +  QStringLiteral("font-size:") + fs + QStringLiteral("px;line-height:") + bodyLh
        +  QStringLiteral(";margin:") + marginStyle
        +  QStringLiteral(";overflow-wrap:break-word;word-break:break-word;}");
    // Headings — weight/spacing/color only; font size comes from buildFullHtml.
    css += QStringLiteral("h1,h2,h3,h4,h5,h6{font-weight:600;line-height:1.25;color:") + fg
        +  QStringLiteral(";}");
    css += QStringLiteral("h1{font-weight:700;}h6{font-weight:500;}");
    // Paragraphs & lists.
    css += QStringLiteral("p{margin-top:0;margin-bottom:16px;}");
    css += QStringLiteral("dl{margin-top:0;margin-bottom:16px;}dt{font-weight:600;margin:6px 0 2px;}dd{margin:0 0 7px 22px;color:") + fg
        + QStringLiteral(";}dt+dd{margin-top:0;}dd+dd{margin-top:3px;}dd>p{margin:0 0 5px;}dd>ul,dd>ol{margin-top:3px;margin-bottom:5px;}dd>:last-child{margin-bottom:0;}section.footnotes{margin-top:28px;border-top:1px solid ") + borderSoft
        + QStringLiteral(";padding-top:12px;font-size:0.92em;}section.footnotes ol{padding-left:1.55em;margin-top:0;margin-bottom:0;}section.footnotes li{margin:0 0 5px;padding-left:1px;}section.footnotes li>p{margin:0 0 4px;}section.footnotes li>p:last-of-type{margin-bottom:0;}section.footnotes li>ul,section.footnotes li>ol{margin-top:3px;margin-bottom:4px;}section.footnotes li>:last-child{margin-bottom:0;}sup[id^='fnref-']{font-size:0.78em;line-height:0;}sup[id^='fnref-'],.footnote-backlinks sup{vertical-align:super;}.footnote-backlinks{white-space:nowrap;margin-left:0.3em;}a.footnote-backref{font-size:0.85em;color:") + muted
        + QStringLiteral(";text-decoration:none;margin-left:0.16em;}a.footnote-backref:hover,a.footnote-backref:focus{color:") + link + QStringLiteral(";text-decoration:underline;}");
    css += QStringLiteral("ul,ol{margin-top:0;margin-bottom:16px;padding-left:1.6em;}");
    css += QStringLiteral("li{margin-top:0.25em;margin-bottom:0.25em;}");
    css += QStringLiteral("li > p{margin-bottom:0.5em;}");
    css += QStringLiteral("li > ul,li > ol{margin-top:0.25em;margin-bottom:0.35em;}");
    css += QStringLiteral("[data-fastmd-task-list='1']{list-style:none;padding-left:0.1em;margin-left:0;}");
    css += QStringLiteral("[data-fastmd-task-item='1']{list-style:none;margin-left:0;padding-left:0;}");
    css += QStringLiteral(".task-list-item > p:first-of-type{display:inline;}");
    css += QStringLiteral("[data-fastmd-task-list='1'][data-fastmd-loose-list='1'] > .task-list-item{margin:0 0 16px;padding:0;}");
    css += QStringLiteral("[data-fastmd-task-list='1'][data-fastmd-loose-list='1'] > .task-list-item:last-child{margin-bottom:0;}");
    css += QStringLiteral(".task-list-marker{display:inline-block;min-width:1.4em;}");
    // Inline code.
    css += QStringLiteral("code{font-family:") + codeFontFamily
        +  QStringLiteral(";font-size:0.86em;background-color:") + codeBg + QStringLiteral(";color:") + codeFg
        +  QStringLiteral(";padding:0.15em 0.4em;border-radius:5px;border:1px solid ") + codeBdr + QStringLiteral(";}");
    css += QStringLiteral(".math-inline{font-family:'Cambria Math','Times New Roman',serif;font-size:1.05em;}");
    css += QStringLiteral("mark{background-color:") + markBg + QStringLiteral(";color:") + markFg
        + QStringLiteral(";padding:0 0.2em;border-radius:3px;}sub,sup{line-height:0;}");
    // Fenced code block. The .code-block table wrapper (emitted by buildFullHtml)
    // is what QTextDocument renders; browsers honor the radius/border here too.
    css += QStringLiteral(".code-block{background-color:") + preBg
        +  QStringLiteral(";margin-top:16px;margin-bottom:16px;width:100%;border:none;border-collapse:collapse;}");
    css += QStringLiteral(".code-block td{padding:13px 16px;border:none;}");
    css += QStringLiteral("pre{margin:0;background-color:transparent;border:none;line-height:1.5;}");
    css += QStringLiteral("pre code{background-color:transparent;color:inherit;border:none;font-size:0.9em;padding:0;border-radius:0;}");
    // Blockquote — non-italic, muted, faint tint, thin accent rule.
    css += QStringLiteral("blockquote{border-left:3px solid ") + bqBdr
        +  QStringLiteral(";margin:16px 0;padding:0.5em 1em;color:") + bqFg + QStringLiteral(";background:") + bqBg
        +  QStringLiteral(";border-radius:0 6px 6px 0;}");
    css += QStringLiteral("blockquote p{margin:0 0 0.5em;}");
    css += QStringLiteral("blockquote > :last-child{margin-bottom:0;}");
    css += QStringLiteral(".fastmd-callout{border-left:3px solid #2f81f7;margin:16px 0;padding:12px 16px;border-radius:0 8px 8px 0;background:#eef6ff;}");
    css += QStringLiteral(".fastmd-callout-title{font-weight:700;margin:0 0 7px;color:#2f81f7;}.fastmd-callout-icon{margin-right:8px;}");
    css += QStringLiteral(".fastmd-callout-tip{border-left-color:#3fb950;background:#effaf0;}.fastmd-callout-tip .fastmd-callout-title{color:#238636;}");
    css += QStringLiteral(".fastmd-callout-warning{border-left-color:#d29922;background:#fff8e6;}.fastmd-callout-warning .fastmd-callout-title{color:#9a6700;}");
    css += QStringLiteral(".fastmd-callout-important{border-left-color:#a371f7;background:#f6f0ff;}.fastmd-callout-important .fastmd-callout-title{color:#8250df;}");
    css += QStringLiteral(".fastmd-callout-caution{border-left-color:#f85149;background:#fff1f0;}.fastmd-callout-caution .fastmd-callout-title{color:#cf222e;}");
    if (dark && !isPrint)
        css += QStringLiteral(".fastmd-callout-note{background:#172b45;}.fastmd-callout-tip{background:#193326;}.fastmd-callout-warning{background:#382d16;}.fastmd-callout-important{background:#2d2342;}.fastmd-callout-caution{background:#3b2022;}");
    // Links.
    css += QStringLiteral("a{color:") + link + QStringLiteral(";text-decoration:none;}");
    css += QStringLiteral("a:hover{text-decoration:underline;}");
    // Tables — rounded frame (browser), header fill, soft interior lines, stripes.
    // <table> doesn't reliably clip its own cell backgrounds/borders to a
    // border-radius in Chromium (table boxes are a special case for
    // overflow) -- the plain ".table-wrap" div wrapContentTables()
    // (ExportManagerMarkdown.cpp) adds around every content table owns the
    // rounding/clipping instead, since that's clipped reliably.
    css += QStringLiteral(".table-wrap{margin-top:16px;margin-bottom:16px;overflow:hidden;border-radius:8px;border:1px solid ") + border + QStringLiteral(";}");
    css += QStringLiteral("table{border-collapse:separate;border-spacing:0;width:100%;}");
    css += QStringLiteral("th,td{border-style:solid;border-color:") + tableCellBdr
        +  QStringLiteral(";border-width:0 1px 1px 0;padding:7px 13px;text-align:left;vertical-align:top;}");
    css += QStringLiteral("table tr>*:last-child{border-right:none;}");
    css += QStringLiteral("tbody tr:last-child>*{border-bottom:none;}");
    css += QStringLiteral("th{font-weight:700;background:") + tableHead + QStringLiteral(";}");
    css += QStringLiteral("tbody tr:nth-child(2n){background-color:") + stripe + QStringLiteral(";}");
    // Misc.
    css += QStringLiteral("img{max-width:100%;border-radius:6px;}");
    css += QStringLiteral("hr{border:none;border-bottom:1px solid ") + border
        +  QStringLiteral(";margin-top:28px;margin-bottom:28px;}");

    // Print-only refinements. These rely on real CSS support, so they only take
    // full effect on the browser PDF path (the QTextDocument fallback ignores the
    // properties it doesn't understand, which is harmless).
    if (isPrint) {
        css += QStringLiteral(
            "@media print{"
            "  body{zoom:87.5%;}"
            "}"
            "body{-webkit-print-color-adjust:exact;print-color-adjust:exact;}"
            "p,li,blockquote{orphans:3;widows:3;}"
            // keep headings attached to the content that follows them
            "h1,h2,h3,h4,h5,h6{page-break-after:avoid;break-after:avoid;}"
            // never split code blocks, blockquotes or images across pages
            "pre,blockquote,img,.code-block{page-break-inside:avoid;break-inside:avoid;}"
            // long tables may break across pages, but never mid-row, and keep the
            // header row attached to the first body row that follows it
            "table{break-inside:auto;}"
            "tr{page-break-inside:avoid;break-inside:avoid;}"
            "thead{display:table-header-group;}"
            // soft-wrap long lines so they never overflow the page width
            "pre{white-space:pre-wrap;overflow-wrap:break-word;}"
            "code{white-space:pre-wrap;overflow-wrap:anywhere;}"
            "a{overflow-wrap:anywhere;}"
            "a code{color:inherit;}"
        );
    }

    return css;
}

// ---------------------------------------------------------------------------
static QString buildMetaHeadTags(const ExportManager::DocumentMetadata& meta)
{
    QString metaTags;
    auto addTag = [&](const QString& name, const QString& value) {
        if (!value.trimmed().isEmpty()) {
            metaTags += QStringLiteral("<meta name=\"%1\" content=\"%2\">")
                            .arg(name, value.trimmed().toHtmlEscaped());
        }
    };
    addTag(QStringLiteral("author"), meta.author);
    addTag(QStringLiteral("description"), meta.description);
    addTag(QStringLiteral("keywords"), meta.keywords);
    return metaTags;
}

// ---------------------------------------------------------------------------
// A bare "---" is also a legal thematic break, so a document may open with one
// and contain more further down. Requiring the candidate block to actually look
// like YAML keeps such documents from having everything up to their second rule
// silently deleted from the preview and every export.
static bool looksLikeYamlBlock(const QString& block)
{
    static const QRegularExpression keyLine(QStringLiteral(R"(^(?:"[^"]*"|'[^']*'|[^\s:#][^:]*):(?:\s|$))"));
    bool sawKey = false;
    const QStringList lines = block.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (rawLine.startsWith(QLatin1Char(' ')) || rawLine.startsWith(QLatin1Char('\t'))
            || line.startsWith(QLatin1String("- "))) {
            continue; // nested mapping, block scalar continuation, or sequence item
        }
        if (!keyLine.match(line).hasMatch())
            return false;
        sawKey = true;
    }
    return sawKey;
}

ExportManager::FrontMatter ExportManager::findYamlFrontMatter(const QString& markdown)
{
    FrontMatter fm;
    if (!markdown.startsWith(QLatin1String("---")))
        return fm;

    const int firstLineEnd = markdown.indexOf(QLatin1Char('\n'));
    if (firstLineEnd == -1 || markdown.left(firstLineEnd).trimmed() != QLatin1String("---"))
        return fm;

    // "..." is YAML's other end-of-document marker; both must own their whole
    // line, and the match must not swallow the blank lines that follow it.
    static const QRegularExpression closingRegex(
        QStringLiteral(R"(\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$))"));

    const auto match = closingRegex.match(markdown, firstLineEnd);
    if (!match.hasMatch())
        return fm;

    const int blockStart = firstLineEnd + 1;
    const QString block = markdown.mid(blockStart, match.capturedStart() - blockStart);
    if (!looksLikeYamlBlock(block))
        return fm;

    fm.block = block;
    fm.bodyOffset = int(match.capturedStart() + match.capturedLength());
    fm.lineCount = int(markdown.left(fm.bodyOffset).count(QLatin1Char('\n')));
    return fm;
}

// ---------------------------------------------------------------------------
ExportManager::DocumentMetadata ExportManager::parseYamlFrontMatter(const QString& markdown)
{
    DocumentMetadata meta;
    const FrontMatter fm = findYamlFrontMatter(markdown);
    if (fm.lineCount == 0)
        return meta;

    auto unquote = [](QString s) {
        if (s.size() >= 2
            && ((s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
                || (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\'')))
                || (s.startsWith(QLatin1Char('[')) && s.endsWith(QLatin1Char(']'))))) {
            s = s.mid(1, s.size() - 2).trimmed();
        }
        return s;
    };

    const QStringList lines = fm.block.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int colonIdx = line.indexOf(QLatin1Char(':'));
        if (colonIdx <= 0)
            continue;

        const QString key = unquote(line.left(colonIdx).trimmed()).toLower();
        const QString value = unquote(line.mid(colonIdx + 1).trimmed());

        if (key == QLatin1String("title")) {
            meta.title = value;
        } else if (key == QLatin1String("author") || key == QLatin1String("authors")
                   || key == QLatin1String("creator") || key == QLatin1String("by")) {
            meta.author = value;
        } else if (key == QLatin1String("description") || key == QLatin1String("subject")
                   || key == QLatin1String("abstract") || key == QLatin1String("summary")) {
            meta.description = value;
        } else if (key == QLatin1String("keywords") || key == QLatin1String("keyword")
                   || key == QLatin1String("tags")) {
            meta.keywords = value;
        }
    }

    return meta;
}

// ---------------------------------------------------------------------------
QString ExportManager::stripYamlFrontMatter(const QString& markdown)
{
    const FrontMatter fm = findYamlFrontMatter(markdown);
    return fm.lineCount == 0 ? markdown : markdown.mid(fm.bodyOffset);
}

// ---------------------------------------------------------------------------
QString ExportManager::buildFullHtml(const QString& bodyHtml, bool dark, int fontSize, bool isPrint, bool injectKatex, const QString& title, bool preserveHeadingSemantics, const DocumentMetadata& meta)
{
    // Cache the CSS — it only changes when (dark, fontSize, isPrint) change.
    struct CssCache { bool dark; int fontSize; bool isPrint; QString css; };
    static CssCache s_cache{ false, -1, false, {} };
    if (s_cache.fontSize != fontSize || s_cache.dark != dark || s_cache.isPrint != isPrint) {
        s_cache = { dark, fontSize, isPrint, buildCss(dark, fontSize, isPrint) };
    }
    const QString& css = s_cache.css;

    // Derive only the values needed for body transforms (cheap — just pointer comparisons).
    auto c = [dark](const char* ld, const char* dk) -> QLatin1StringView {
        return dark ? QLatin1StringView(dk) : QLatin1StringView(ld);
    };
    // These inline transforms are the authoritative source of heading sizes and
    // code-block styling for every export engine (a QTextDocument shim that also
    // renders fine in browsers). Keep the palette + scale in lock-step with
    // buildCss above and with the heading block in resources/preview/preview.css.
    QLatin1StringView fg      = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#bbbebf");
    QLatin1StringView preBg   = isPrint ? QLatin1StringView("#f8f8f8") : c("#ffffff", "#191a1b");
    QLatin1StringView preBdr  = isPrint ? QLatin1StringView("#d8dde3") : c("#d8dde3", "#2a2b2c");

    // Same ratios as the heading block in resources/preview/preview.css
    // (11pt-body scale divided by 11), expressed in px of the body size.
    const int actualFontSize = fontSize;
    auto px = [actualFontSize](double ratio) {
        return QString::number(qRound(actualFontSize * ratio));
    };
    const QString h1Size = px(2.0),   h1Top = px(2.545), h1Bot = px(0.727);
    const QString h2Size = px(1.545), h2Top = px(2.18),  h2Bot = px(0.545);
    const QString h3Size = px(1.273), h3Top = px(1.82),  h3Bot = px(0.455);
    const QString h4Size = px(1.09),  h4Top = px(1.455), h4Bot = px(0.364);
    const QString h5Size = px(1.0),   h5Top = px(1.273), h5Bot = px(0.364);
    const QString h6Size = px(1.0),   h6Top = px(1.09),  h6Bot = px(0.364);

    // Pre-compiled heading regexes (allocated once per level).
    static const QRegularExpression rxH[7] = {
        {},  // index 0 unused
        QRegularExpression(QStringLiteral(R"(<h1([^>]*)>(.*?)</h1>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h2([^>]*)>(.*?)</h2>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h3([^>]*)>(.*?)</h3>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h4([^>]*)>(.*?)</h4>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h5([^>]*)>(.*?)</h5>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h6([^>]*)>(.*?)</h6>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
    };
    QString finalBody = bodyHtml;

    // No rules, no case change. Margins are the same ratios as preview.css;
    // QTextDocument has no :first-child or margin collapsing, so it just gets
    // the plain per-level values.
    const QString tagFor[7] = {QString(), QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
                               QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6")};
    const QString sizes[7] = {QString(), h1Size, h2Size, h3Size, h4Size, h5Size, h6Size};
    const QString tops[7] = {QString(), h1Top, h2Top, h3Top, h4Top, h5Top, h6Top};
    const QString bots[7] = {QString(), h1Bot, h2Bot, h3Bot, h4Bot, h5Bot, h6Bot};
    for (int level = 1; level <= 6; ++level) {
        const QString weight = level == 1 ? QStringLiteral("700") : (level == 6 ? QStringLiteral("500") : QStringLiteral("600"));
        const QString outTag = preserveHeadingSemantics ? tagFor[level] : QStringLiteral("div");
        finalBody.replace(rxH[level],
            QStringLiteral("<%1\\1 style=\"font-size:%2px; font-weight:%3; line-height:1.25; color:%4; margin-top:%5px; margin-bottom:%6px;\">\\2</%1>")
                .arg(outTag, sizes[level], weight, fg, tops[level], bots[level]));
    }

    const QString codeBlockMargin = QStringLiteral("margin-top:16px; margin-bottom:16px;");
    finalBody.replace(QStringLiteral("<pre><code"),
        QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" border=\"0\" style=\"%1 border-collapse:separate; border-spacing:0; page-break-inside:avoid; border:none;\"><tr><td bgcolor=\"%2\" style=\"background-color:%2; padding:13px 16px; border:1px solid %3; border-radius:8px;\"><pre style=\"margin:0; background-color:%2; border:none; white-space:pre-wrap; word-break:break-word; overflow-wrap:anywhere;\"><code")
            .arg(codeBlockMargin, preBg, preBdr));
    finalBody.replace(QStringLiteral("</code></pre>"), QStringLiteral("</code></pre></td></tr></table>"));

    // MD4C keeps the trailing newline that terminates a fenced code block.
    // In QTextBrowser that shows up as a visibly tall empty band after single-line
    // snippets, so trim just that final newline from fenced blocks.
    static const QRegularExpression rxCodeBlockTail(
        QStringLiteral(R"((<pre[^>]*><code[^>]*>)([\s\S]*?)\n</code></pre>)"),
        QRegularExpression::DotMatchesEverythingOption);
    finalBody.replace(rxCodeBlockTail, QStringLiteral("\\1\\2</code></pre>"));

    // The bare page-break div is left as-is in exports (HTML / PDF): no visible
    // marker appears; only the actual page break fires in PDF. The live preview
    // renders its own "Page Break" separator in the WebView2 shell CSS.

    // Only reference KaTeX assets when math is actually present, so the default
    // export path stays free of any KaTeX injection when there is no math.
    const QString katexHead = injectKatex ? buildKatexHead() : QString();
    const QString highlightHead = hasLabelledCodeBlock(finalBody) ? buildHighlightHead(dark) : QString();
    const QString documentTitle = !meta.title.trimmed().isEmpty()
        ? meta.title.trimmed().toHtmlEscaped()
        : (!title.trimmed().isEmpty() ? title.trimmed().toHtmlEscaped() : QStringLiteral("FastMD"));
    const QString metaHead = buildMetaHeadTags(meta);

    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>%4</title>%6"
        "<style>%1</style>"
        "%3%5"
        "</head><body>%2</body></html>"
    ).arg(css, finalBody, katexHead, documentTitle, highlightHead, metaHead);
}

// ---------------------------------------------------------------------------
QString ExportManager::buildPreviewHtml(const QString& bodyHtml, bool dark, int fontSize,
                                        bool isPrint, const QString& title, const DocumentMetadata& meta,
                                        const QString& bodyFontFamily, const DocumentSpacing::Settings& spacing)
{
    const QString css = previewAsset(QStringLiteral("preview.css"));
    const QString shell = previewAsset(QStringLiteral("preview.js"));
    if (css.isEmpty() || shell.isEmpty())
        return buildFullHtml(bodyHtml, dark, fontSize, isPrint, false, title, true, meta);

    const bool renderedDark = dark && !isPrint;
    const bool hasCode = hasLabelledCodeBlock(bodyHtml);
    const bool hasMath = QRegularExpression(
        QStringLiteral(R"rx(\$\$|\\\[|\\\()rx"))
        .match(bodyHtml).hasMatch();
    const bool hasMermaid = bodyHtml.contains(QStringLiteral("language-mermaid"), Qt::CaseInsensitive);
    const QString themeVars = QStringLiteral(":root{--fs:%1px;}").arg(fontSize);
    const QString printCss = isPrint ? QStringLiteral(
        "<style>:root{--bg:#ffffff;}@media print{html,body,#content{background:#ffffff!important;}body{padding:0;}"
        ".fastmd-page-break{height:0;margin:0;visibility:hidden;"
        "break-after:page;page-break-after:always;}"
        "p,li,blockquote{orphans:3;widows:3;}"
        // keep headings attached to the content that follows them, so one
        // doesn't get stranded alone at the bottom of a page, and a wrapped
        // heading is never split across two pages
        "h1,h2,h3,h4,h5,h6{break-after:avoid;page-break-after:avoid;"
        "break-inside:avoid;page-break-inside:avoid;orphans:2;widows:2;}"
        "pre,blockquote,img{break-inside:avoid;}"
        // long tables may split across pages, but never mid-row, and the
        // header row repeats on each page it continues onto. The rounded
        // card look (.table-wrap's border/radius/clip) is fine when the
        // table fits one page, but every way of keeping it while the wrap
        // is also allowed to fragment (border+clone, moving the border to
        // <table> cloned or not, switching to border-collapse:collapse --
        // all tried first) hit a different Chromium/WebView2 print
        // fragmentation defect: a spurious empty closing row, square
        // corners where rounded ones were expected, or a corner rendered
        // as a degenerate spike. A decoration that must survive being cut
        // at an arbitrary, content-dependent page boundary just isn't
        // reliable here, so print drops the radius/clip entirely (screen
        // Preview is untouched -- this block only exists for isPrint) and
        // every border becomes a plain per-cell line: thead's own top edge
        // (it re-displays at the top of every page, so it always needs
        // one, not only on page 1), and the true first column's left /
        // true last column's right / true last row's bottom, which the
        // interactive design otherwise leaves to the wrap's own border.
        ".table-wrap{break-inside:auto;border:none;border-radius:0;overflow:visible;}"
        "thead th{border-top:1px solid var(--border);}"
        "tr>*:first-child{border-left:1px solid var(--border);}"
        "table tr>*:last-child{border-right:1px solid var(--border);}"
        "tbody tr:last-child>*{border-bottom:1px solid var(--border);}"
        "tr{break-inside:avoid;page-break-inside:avoid;}"
        "thead{display:table-header-group;}"
        "#outline-btn{display:none!important;}}</style>") : QString();
    const QString katex = hasMath ? buildKatexHead(false) : QString();
    const QString highlight = hasCode ? buildHighlightHead(renderedDark, false) : QString();
    const QString mermaid = hasMermaid ? QStringLiteral(
        "<script data-fastmd-asset='mermaid/mermaid.min.js'>%1</script>")
        .arg(previewAsset(QStringLiteral("mermaid/mermaid.min.js"))) : QString();
    const QString documentTitle = !meta.title.trimmed().isEmpty()
        ? meta.title.trimmed().toHtmlEscaped()
        : (!title.trimmed().isEmpty() ? title.trimmed().toHtmlEscaped() : QStringLiteral("FastMD"));
    const QString metaHead = buildMetaHeadTags(meta);

    return QStringLiteral(
        "<!DOCTYPE html><html lang='en' data-theme='%1' data-fastmd-standalone='1'><head>"
        "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>%2</title>%11<style>%3%4%12</style>%5%6%7%8</head>"
        "<body><div id='content'>%9</div><script>%10</script></body></html>")
        .arg(renderedDark ? QStringLiteral("dark") : QStringLiteral("light"), documentTitle, css, themeVars,
             printCss, katex, highlight, mermaid, bodyHtml, shell, metaHead,
             FontPreferences::bodyFontCss(bodyFontFamily, FontPreferences::missingVietnameseRange(bodyFontFamily))
                 + DocumentSpacing::rootCss(spacing));
}

QString ExportManager::buildMindMapHtml(const QString& markdown, bool dark, const QString& title)
{
    const QString documentTitle = title.trimmed().isEmpty()
        ? QStringLiteral("FastMD Mind Map") : title.trimmed().toHtmlEscaped();
    const QString theme = dark ? QStringLiteral(" class=\"markmap-dark\" data-theme=\"dark\"") : QString();
    return QStringLiteral(
        "<!doctype html><html%1><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>%2</title><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;"
        "font-family:\"Segoe UI\",sans-serif;}body,#mindmap{background:#f9f9f9;color:#24292f;}"
        ".markmap-dark body,.markmap-dark #mindmap{background:#121314;color:#bbbebf;}"
        "#mindmap{display:block;width:100vw;height:100vh;--markmap-text-color:currentColor;"
        "--markmap-circle-open-bg:transparent;--markmap-code-bg:#f0f2f4;--markmap-code-color:#24292f;}"
        ".markmap-dark #mindmap{--markmap-code-bg:#191a1b;--markmap-code-color:#bbbebf;}</style></head><body>"
        "<svg id=\"mindmap\" aria-label=\"Mind map\"></svg>"
        "<script>(function(){var markdown=%3;"
        "var insideFastMd=!!(window.chrome&&window.chrome.webview);"
        "var local='https://fastmd.assets/preview/markmap/';"
        "var localKatex='https://fastmd.assets/katex/';"
        "var cdn='https://cdn.jsdelivr.net/npm/';"
        "function style(url){var l=document.createElement('link');l.rel='stylesheet';l.href=url;document.head.append(l);}"
        "function script(url){return new Promise(function(resolve,reject){var s=document.createElement('script');s.src=url;s.onload=resolve;s.onerror=reject;document.head.append(s);});}"
        // KaTeX leads the chain in both branches: markmap-lib typesets math
        // while transforming and silently leaves raw LaTeX if KaTeX is not
        // already on the page.
        "var files=insideFastMd?[localKatex+'katex.min.js',local+'d3.min.js',local+'markmap-view.min.js',local+'markmap-lib.min.js',local+'markmap-toolbar.min.js']:[cdn+'katex@0.16.11/dist/katex.min.js',cdn+'d3@7.8.5/dist/d3.min.js',cdn+'markmap-view@0.18.12/dist/browser/index.js',cdn+'markmap-lib@0.18.12/dist/browser/index.iife.js',cdn+'markmap-toolbar@0.18.12/dist/index.js'];"
        "style(insideFastMd?localKatex+'katex.min.css':cdn+'katex@0.16.11/dist/katex.min.css');"
        "style(insideFastMd?local+'markmap-toolbar.css':cdn+'markmap-toolbar@0.18.12/dist/style.css');"
        "files.reduce(function(p,url){return p.then(function(){return script(url);});},Promise.resolve()).then(function(){"
        "var transformer=new window.markmap.Transformer();var result=transformer.transform(markdown);"
        "window.__fastmdOwnWheelZoom=true;"
        // Use Markmap's own D3 wheel listener. Unlike a host-level listener,
        // it works in both WebView2 and ordinary Chromium browsers.
        //
        // pan/scrollForPan are pinned rather than left to default, matching the
        // in-app shell (resources/preview/mindmap.js): `pan` defaults to true
        // and installs a *wheel*-pan handler alongside d3.zoom's own
        // `wheel.zoom` listener -- and since it only calls preventDefault(),
        // both fire and one notch would pan and zoom at once. `scrollForPan`
        // defaults to isMacintosh, which would otherwise make an exported map
        // need Ctrl+wheel on a Mac and not on Windows.
        "var map=window.markmap.Markmap.create('svg#mindmap',{autoFit:true,duration:180,pan:false,scrollForPan:false,zoom:true},result.root);"
        "if(window.markmap.Toolbar){var toolbar=new window.markmap.Toolbar();toolbar.attach(map);var element=toolbar.render();"
        "element.style.cssText='position:absolute;bottom:20px;right:20px';document.body.append(element);}});})();</script></body></html>")
        .arg(theme, documentTitle, javascriptStringLiteral(markdown));
}

QString ExportManager::documentBaseTag(const QString& documentDir)
{
    if (documentDir.isEmpty())
        return {};
    return QStringLiteral("<base href=\"%1\">")
        .arg(QUrl::fromLocalFile(documentDir + QLatin1Char('/')).toString(QUrl::FullyEncoded).toHtmlEscaped());
}

// ---------------------------------------------------------------------------
// Standalone mind map built for rasterization rather than for a reader. Three
// differences from buildMindMapHtml, all to make a headless render
// deterministic: the runtime is referenced by absolute file:// URL (neither the
// WebView2 virtual host nor the CDN is reachable from a temp file opened
// headlessly), `duration` is 0 so no enter animation can be caught mid-flight,
// and no toolbar is attached because it would be baked into the image.
QString ExportManager::buildMindMapRenderHtml(const QString& markdown, bool dark,
                                              const QString& documentDir,
                                              bool transparentBackground)
{
    const QString assets = QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + QStringLiteral("/preview/markmap/")).toString();

    // The page lives in a temp directory, so a document-relative image would
    // otherwise resolve against that instead of the document's own folder. Only
    // relative URLs are affected; the runtime and KaTeX are absolute file:// URLs.
    const QString base = documentBaseTag(documentDir);
    // DOCX embeds a PNG over the document page, so its map canvas must not
    // carry the application's preview color with it. Keep this opt-in: PDF
    // export still needs the themed page background it prints today.
    const QString background = transparentBackground ? QStringLiteral("transparent")
        : (dark ? QStringLiteral("#121314") : QStringLiteral("#f9f9f9"));
    const QString foreground = dark ? QStringLiteral("#bbbebf") : QStringLiteral("#24292f");
    const QString codeBg = dark ? QStringLiteral("#191a1b") : QStringLiteral("#f0f2f4");

    // markmap-lib typesets `$...$` while transforming, but only if KaTeX is
    // already on the page, so it must load ahead of the runtime -- same order as
    // the in-app shell. Absent assets degrade to raw LaTeX, never a failed export.
    const QString katexDir = katexAssetsDir();
    const QString katex = katexDir.isEmpty() ? QString()
        : QStringLiteral("<link rel=\"stylesheet\" href=\"%1katex.min.css\">"
                         "<script src=\"%1katex.min.js\"></script>")
              .arg(QUrl::fromLocalFile(katexDir + QLatin1Char('/')).toString());

    return QStringLiteral(
        "<!doctype html><html><head><meta charset=\"utf-8\">%7<title>FastMD Mind Map</title>"
        // Sized in percentages, not vw/vh: under headless print-to-pdf the
        // viewport units keep resolving against the screen viewport while the
        // page box is the paper, which left the map filling only the top of the
        // sheet. Percentages resolve against the page box when printing and the
        // viewport when screenshotting, which is what both callers want.
        "<style>@page{margin:0}html,body{margin:0;width:100%;height:100%;overflow:hidden;"
        "font-family:\"Segoe UI\",sans-serif;}"
        "body,#mindmap{background:%1;color:%2;}"
        "#mindmap{display:block;width:100%;height:100%;--markmap-text-color:currentColor;"
        "--markmap-circle-open-bg:%1;--markmap-code-bg:%3;--markmap-code-color:%2;}</style>"
        "%6"
        "<script src=\"%4d3.min.js\"></script>"
        "<script src=\"%4markmap-view.min.js\"></script>"
        "<script src=\"%4markmap-lib.min.js\"></script>"
        "</head><body><svg id=\"mindmap\" aria-label=\"Mind map\"></svg>"
        "<script>(function(){var markdown=%5;"
        "var transformer=new window.markmap.Transformer();"
        "var result=transformer.transform(markdown);"
        "function collectImages(node,urls){"
        "var pattern=/<img[^>]+src=[\"']([^\"']+)[\"']/g;var match;"
        "while((match=pattern.exec(node.content||''))!==null)urls.push(match[1]);"
        "(node.children||[]).forEach(function(child){collectImages(child,urls);});"
        "return urls;}"
        // Markmap measures every node exactly once and caches the size on the
        // node itself, so an image that has not finished loading is measured at
        // zero width and its node renders empty -- and re-running setData()
        // reuses the cached size rather than re-measuring. Warm the image cache
        // before the one and only layout pass.
        "function preloadImages(){var urls=collectImages(result.root,[]);"
        "if(!urls.length)return Promise.resolve();"
        "return Promise.all(urls.map(function(url){"
        "return new Promise(function(done){var image=new Image();"
        "image.onload=done;image.onerror=done;image.src=url;});}));}"
        // Same measure-once problem, worse: KaTeX's webfonts are only fetched
        // once math is on the page, so a node measured before they arrive is
        // sized from fallback metrics and the real glyphs then overflow its
        // fixed <foreignObject>, which clips.
        "function preloadFonts(){"
        "if(!document.fonts||!document.fonts.load)return Promise.resolve();var faces=[];"
        "['Main','Math','Size1','Size2','Size3','Size4','AMS','Caligraphic','Fraktur',"
        "'SansSerif','Script','Typewriter'].forEach(function(name){"
        "['','italic ','bold '].forEach(function(style){"
        "faces.push(document.fonts.load(style+'16px \"KaTeX_'+name+'\"')"
        ".catch(function(){}));});});return Promise.all(faces);}"
        // The race caps the wait so an unreachable image or font cannot stall
        // the export; whatever has arrived by then is what gets measured.
        "Promise.race([Promise.all([preloadImages(),preloadFonts()]),"
        "new Promise(function(done){setTimeout(done,5000);})]).then(function(){"
        "var map=window.markmap.Markmap.create('svg#mindmap',"
        "{autoFit:true,duration:0,pan:false,scrollForPan:false,zoom:false},result.root);"
        // autoFit runs against the screen viewport, but a headless print lays
        // the page out at the paper box instead -- without a re-fit the map is
        // printed at a scale computed for a different size.
        "function refit(){try{map.fit();}catch(error){}}"
        "window.addEventListener('beforeprint',refit);"
        "window.addEventListener('resize',refit);"
        "});})();</script></body></html>")
        .arg(background, foreground, codeBg, assets, javascriptStringLiteral(markdown), katex, base);
}

// ---------------------------------------------------------------------------
bool ExportManager::exportHtml(const QString& markdown, const QString& filePath, bool dark,
                               const QString& bodyFontFamily, const DocumentSpacing::Settings& spacing)
{
    // Regenerate from raw markdown using the KaTeX path so exported HTML keeps
    // real LaTeX math (never the Unicode preview output).
    bool mathDetected = false;
    const DocumentMetadata meta = parseYamlFrontMatter(markdown);
    const QString body = markdownToHtml(markdown, MathRenderMode::KatexOutput, &mathDetected);
    const QString fullHtml = buildPreviewHtml(body, dark, FastMdDefaults::PreviewFontSize, false, QString(), meta,
                                              bodyFontFamily, spacing);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << fullHtml;
    return true;
}

// ---------------------------------------------------------------------------
// CSS @page rule so the headless browser honors the chosen page size & margins.
QString ExportManager::buildPageCss(const ExportManager::PdfExportOptions& options,
                                   const HeaderFooterContext& context)
{
    QSizeF sizeMm = options.pageSize.size(QPageSize::Millimeter);
    if (options.orientation == QPageLayout::Landscape && sizeMm.width() < sizeMm.height())
        sizeMm.transpose();
    const QMarginsF m = options.marginsMm;
    // Appended rather than passed through arg(): slot text is user input and
    // must never be scanned for %n placeholders.
    return QStringLiteral(
        "<style>@page{size:%1mm %2mm;margin:%3mm %4mm %5mm %6mm;")
        .arg(QString::number(sizeMm.width(), 'f', 2),
             QString::number(sizeMm.height(), 'f', 2),
             QString::number(m.top(), 'f', 2),
             QString::number(m.right(), 'f', 2),
             QString::number(m.bottom(), 'f', 2),
             QString::number(m.left(), 'f', 2))
        + HeaderFooter::marginBoxCss(options.headerFooter, context)
        + QStringLiteral("}</style>");
}

// ---------------------------------------------------------------------------
ExportManager::PdfExportOptions ExportManager::pdfExportOptionsFromSettings(const SettingsLookup& value)
{
    PdfExportOptions options;
    const auto pageSizeId = static_cast<QPageSize::PageSizeId>(
        value(QStringLiteral("pdf/pageSizeId"), static_cast<int>(QPageSize::A4)).toInt());
    if (pageSizeId == QPageSize::Custom) {
        const QSizeF pageSizeMm(
            value(QStringLiteral("pdf/customWidthMm"), 210.0).toDouble(),
            value(QStringLiteral("pdf/customHeightMm"), 297.0).toDouble());
        options.pageSize = QPageSize(
            pageSizeMm,
            QPageSize::Millimeter,
            value(QStringLiteral("pdf/customPageName"), QObject::tr("Custom")).toString());
    } else {
        options.pageSize = QPageSize(pageSizeId);
    }
    options.orientation = static_cast<QPageLayout::Orientation>(
        value(QStringLiteral("pdf/orientation"), static_cast<int>(QPageLayout::Portrait)).toInt());
    options.marginsMm = QMarginsF(
        value(QStringLiteral("pdf/marginLeftMm"), 20.0).toDouble(),
        value(QStringLiteral("pdf/marginTopMm"), 20.0).toDouble(),
        value(QStringLiteral("pdf/marginRightMm"), 20.0).toDouble(),
        value(QStringLiteral("pdf/marginBottomMm"), 20.0).toDouble());
    options.fontSize = value(QStringLiteral("pdf/fontSize"), 14).toInt();
    if (options.fontSize == 12)
        options.fontSize = 14;
    options.fontFamily = FontPreferences::normalizedFamily(
        value(FontPreferences::PrintKey, QString()).toString());
    options.spacing = DocumentSpacing::load([&value](QLatin1StringView key) {
        return value(QString(key), QVariant());
    });
    options.headerFooter = HeaderFooter::load(value);
    return options;
}
