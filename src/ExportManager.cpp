#include "ExportManager.h"

#include <QFile>
#include <QTextStream>
#include <QPrinter>
#include <QTextDocument>
#include <QPageSize>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QSettings>
#include <QMessageBox>

#include "md4c-html.h"

namespace {

static int consumeBalanced(const QString& text, int openPos, QChar openCh, QChar closeCh, QString* out)
{
    int depth = 0;
    for (int i = openPos; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == openCh) {
            ++depth;
        } else if (ch == closeCh) {
            --depth;
            if (depth == 0) {
                if (out)
                    *out = text.mid(openPos + 1, i - openPos - 1);
                return i + 1;
            }
        }
    }
    return -1;
}

static QString renderMathInline(const QString& expr);

static QString renderMathToken(const QString& expr, const char* tag)
{
    return QStringLiteral("<%1>%2</%1>").arg(QLatin1StringView(tag), renderMathInline(expr));
}

static QString renderMathInline(const QString& expr)
{
    QString html;
    html.reserve(expr.size() * 2);

    for (int i = 0; i < expr.size();) {
        const QChar ch = expr.at(i);

        if (ch == '\\' && i + 1 < expr.size()) {
            const QChar next = expr.at(i + 1);
            if (QStringLiteral("%$&#_{}^~\\").contains(next)) {
                html += QString(next).toHtmlEscaped();
                i += 2;
                continue;
            }

            if (next.isLetter()) {
                int pos = i + 1;
                while (pos < expr.size() && expr.at(pos).isLetter()) {
                    ++pos;
                }
                QString cmd = expr.mid(i + 1, pos - (i + 1));

                if (cmd == QStringLiteral("frac")) {
                    if (pos < expr.size() && expr.at(pos) == '{') {
                        QString num;
                        int afterNum = consumeBalanced(expr, pos, '{', '}', &num);
                        if (afterNum > 0 && afterNum < expr.size() && expr.at(afterNum) == '{') {
                            QString den;
                            int afterDen = consumeBalanced(expr, afterNum, '{', '}', &den);
                            if (afterDen > 0) {
                                html += QStringLiteral("<sup>%1</sup>/<sub>%2</sub>")
                                            .arg(renderMathInline(num), renderMathInline(den));
                                i = afterDen;
                                continue;
                            }
                        }
                    }
                } else if (cmd == QStringLiteral("sqrt")) {
                    if (pos < expr.size() && expr.at(pos) == '{') {
                        QString inner;
                        int afterInner = consumeBalanced(expr, pos, '{', '}', &inner);
                        if (afterInner > 0) {
                            html += QStringLiteral("&radic;<span style=\"text-decoration:overline;\">%1</span>")
                                        .arg(renderMathInline(inner));
                            i = afterInner;
                            continue;
                        }
                    }
                } else {
                    static const QHash<QString, QString> syms = {
                        {QStringLiteral("alpha"), QStringLiteral("&alpha;")}, {QStringLiteral("beta"), QStringLiteral("&beta;")}, {QStringLiteral("gamma"), QStringLiteral("&gamma;")},
                        {QStringLiteral("delta"), QStringLiteral("&delta;")}, {QStringLiteral("epsilon"), QStringLiteral("&epsilon;")}, {QStringLiteral("zeta"), QStringLiteral("&zeta;")},
                        {QStringLiteral("eta"), QStringLiteral("&eta;")}, {QStringLiteral("theta"), QStringLiteral("&theta;")}, {QStringLiteral("iota"), QStringLiteral("&iota;")},
                        {QStringLiteral("kappa"), QStringLiteral("&kappa;")}, {QStringLiteral("lambda"), QStringLiteral("&lambda;")}, {QStringLiteral("mu"), QStringLiteral("&mu;")},
                        {QStringLiteral("nu"), QStringLiteral("&nu;")}, {QStringLiteral("xi"), QStringLiteral("&xi;")}, {QStringLiteral("omicron"), QStringLiteral("&omicron;")},
                        {QStringLiteral("pi"), QStringLiteral("&pi;")}, {QStringLiteral("rho"), QStringLiteral("&rho;")}, {QStringLiteral("sigma"), QStringLiteral("&sigma;")},
                        {QStringLiteral("tau"), QStringLiteral("&tau;")}, {QStringLiteral("upsilon"), QStringLiteral("&upsilon;")}, {QStringLiteral("phi"), QStringLiteral("&phi;")},
                        {QStringLiteral("chi"), QStringLiteral("&chi;")}, {QStringLiteral("psi"), QStringLiteral("&psi;")}, {QStringLiteral("omega"), QStringLiteral("&omega;")},
                        {QStringLiteral("Alpha"), QStringLiteral("&Alpha;")}, {QStringLiteral("Beta"), QStringLiteral("&Beta;")}, {QStringLiteral("Gamma"), QStringLiteral("&Gamma;")},
                        {QStringLiteral("Delta"), QStringLiteral("&Delta;")}, {QStringLiteral("Epsilon"), QStringLiteral("&Epsilon;")}, {QStringLiteral("Zeta"), QStringLiteral("&Zeta;")},
                        {QStringLiteral("Eta"), QStringLiteral("&Eta;")}, {QStringLiteral("Theta"), QStringLiteral("&Theta;")}, {QStringLiteral("Iota"), QStringLiteral("&Iota;")},
                        {QStringLiteral("Kappa"), QStringLiteral("&Kappa;")}, {QStringLiteral("Lambda"), QStringLiteral("&Lambda;")}, {QStringLiteral("Mu"), QStringLiteral("&Mu;")},
                        {QStringLiteral("Nu"), QStringLiteral("&Nu;")}, {QStringLiteral("Xi"), QStringLiteral("&Xi;")}, {QStringLiteral("Omicron"), QStringLiteral("&Omicron;")},
                        {QStringLiteral("Pi"), QStringLiteral("&Pi;")}, {QStringLiteral("Rho"), QStringLiteral("&Rho;")}, {QStringLiteral("Sigma"), QStringLiteral("&Sigma;")},
                        {QStringLiteral("Tau"), QStringLiteral("&Tau;")}, {QStringLiteral("Upsilon"), QStringLiteral("&Upsilon;")}, {QStringLiteral("Phi"), QStringLiteral("&Phi;")},
                        {QStringLiteral("Chi"), QStringLiteral("&Chi;")}, {QStringLiteral("Psi"), QStringLiteral("&Psi;")}, {QStringLiteral("Omega"), QStringLiteral("&Omega;")},
                        {QStringLiteral("sum"), QStringLiteral("&sum;")}, {QStringLiteral("int"), QStringLiteral("&int;")}, {QStringLiteral("infty"), QStringLiteral("&infin;")},
                        {QStringLiteral("approx"), QStringLiteral("&asymp;")}, {QStringLiteral("neq"), QStringLiteral("&ne;")}, {QStringLiteral("pm"), QStringLiteral("&plusmn;")},
                        {QStringLiteral("cdot"), QStringLiteral("&sdot;")}, {QStringLiteral("times"), QStringLiteral("&times;")}, {QStringLiteral("div"), QStringLiteral("&divide;")},
                        {QStringLiteral("leq"), QStringLiteral("&le;")}, {QStringLiteral("geq"), QStringLiteral("&ge;")}, {QStringLiteral("rightarrow"), QStringLiteral("&rarr;")},
                        {QStringLiteral("leftarrow"), QStringLiteral("&larr;")}, {QStringLiteral("Rightarrow"), QStringLiteral("&rArr;")}, {QStringLiteral("Leftarrow"), QStringLiteral("&lArr;")},
                        {QStringLiteral("in"), QStringLiteral("&isin;")}, {QStringLiteral("notin"), QStringLiteral("&notin;")}, {QStringLiteral("subset"), QStringLiteral("&sub;")},
                        {QStringLiteral("propto"), QStringLiteral("&prop;")}, {QStringLiteral("partial"), QStringLiteral("&part;")}, {QStringLiteral("nabla"), QStringLiteral("&nabla;")},
                        {QStringLiteral("sin"), QStringLiteral("sin")}, {QStringLiteral("cos"), QStringLiteral("cos")}, {QStringLiteral("tan"), QStringLiteral("tan")},
                        {QStringLiteral("log"), QStringLiteral("log")}, {QStringLiteral("ln"), QStringLiteral("ln")}, {QStringLiteral("lim"), QStringLiteral("lim")}, {QStringLiteral("exp"), QStringLiteral("exp")},
                        {QStringLiteral("le"), QStringLiteral("&le;")}, {QStringLiteral("ge"), QStringLiteral("&ge;")}, {QStringLiteral("to"), QStringLiteral("&rarr;")},
                        {QStringLiteral("gets"), QStringLiteral("&larr;")}, {QStringLiteral("ell"), QStringLiteral("&#8467;")}, {QStringLiteral("forall"), QStringLiteral("&forall;")},
                        {QStringLiteral("exists"), QStringLiteral("&exist;")}, {QStringLiteral("empty"), QStringLiteral("&empty;")}, {QStringLiteral("cup"), QStringLiteral("&cup;")},
                        {QStringLiteral("cap"), QStringLiteral("&cap;")}, {QStringLiteral("wedge"), QStringLiteral("&and;")}, {QStringLiteral("vee"), QStringLiteral("&or;")},
                        {QStringLiteral("equiv"), QStringLiteral("&equiv;")}, {QStringLiteral("cong"), QStringLiteral("&cong;")}, {QStringLiteral("sim"), QStringLiteral("&sim;")},
                        {QStringLiteral("circ"), QStringLiteral("&ring;")}, {QStringLiteral("angle"), QStringLiteral("&ang;")}
                    };
                    if (syms.contains(cmd)) {
                        html += syms.value(cmd);
                        i = pos;
                        continue;
                    }
                }
            }
        }

        if (ch == '^' || ch == '_') {
            const bool superscript = (ch == '^');
            int pos = i + 1;
            QString token;
            if (pos < expr.size() && expr.at(pos) == '{') {
                pos = consumeBalanced(expr, pos, '{', '}', &token);
            } else {
                int start = pos;
                while (pos < expr.size()) {
                    const QChar t = expr.at(pos);
                    if (!(t.isLetterOrNumber() || t == '.' || t == '+' || t == '-' || t == ',')) break;
                    ++pos;
                }
                token = expr.mid(start, pos - start);
            }
            if (!token.isEmpty()) {
                html += superscript
                    ? QStringLiteral("<sup>%1</sup>").arg(renderMathInline(token))
                    : QStringLiteral("<sub>%1</sub>").arg(renderMathInline(token));
                i = pos;
                continue;
            }
        }

        html += QString(ch).toHtmlEscaped();
        ++i;
    }

    return html;
}

static QString renderMathBlock(const QString& expr)
{
    return QStringLiteral("<div class=\"math-block\">%1</div>").arg(renderMathInline(expr));
}

// Build the snippet that replaces a detected math span.
//  - UnicodePreview: convert to the Unicode/HTML fallback.
//  - KatexOutput: keep the raw LaTeX wrapped in its original delimiters (HTML
//    escaped) so KaTeX auto-render can find and typeset it in the browser.
static QString makeMathSnippet(MathRenderMode mode, bool block, const QString& expr,
                               const QString& openDelim, const QString& closeDelim)
{
    if (mode == MathRenderMode::KatexOutput)
        return (openDelim + expr + closeDelim).toHtmlEscaped();
    return block ? renderMathBlock(expr) : renderMathInline(expr);
}

static QString preprocessMath(const QString& markdown, MathRenderMode mode, QStringList* mathSnippets)
{
    QString out;
    out.reserve(markdown.size());

    bool inFence = false;
    QChar fenceChar;
    bool inInlineCode = false;

    for (int i = 0; i < markdown.size();) {
        if (!inInlineCode && (i == 0 || markdown.at(i - 1) == '\n')) {
            int lineEnd = markdown.indexOf('\n', i);
            if (lineEnd < 0) lineEnd = markdown.size();
            int j = i;
            while (j < lineEnd && markdown.at(j).isSpace())
                ++j;
            int fenceCount = 0;
            while (j + fenceCount < lineEnd && markdown.at(j + fenceCount) == markdown.at(j))
                ++fenceCount;
            if (fenceCount >= 3 && (markdown.at(j) == '`' || markdown.at(j) == '~')) {
                if (!inFence) {
                    inFence = true;
                    fenceChar = markdown.at(j);
                } else if (fenceChar == markdown.at(j)) {
                    inFence = false;
                }
            }
        }

        const QChar ch = markdown.at(i);
        if (inFence) {
            out += ch;
            ++i;
            continue;
        }

        if (ch == '`') {
            inInlineCode = !inInlineCode;
            out += ch;
            ++i;
            continue;
        }

        if (!inInlineCode && ch == '$' && (i == 0 || markdown.at(i - 1) != '\\')) {
            const bool block = (i + 1 < markdown.size() && markdown.at(i + 1) == '$');
            const int start = i + (block ? 2 : 1);
            int end = -1;
            for (int j = start; j < markdown.size(); ++j) {
                if (markdown.at(j) != '$' || (j > 0 && markdown.at(j - 1) == '\\'))
                    continue;
                if (block) {
                    if (j + 1 < markdown.size() && markdown.at(j + 1) == '$') {
                        end = j;
                        break;
                    }
                } else {
                    end = j;
                    break;
                }
            }
            if (end > start) {
                const QString expr = markdown.mid(start, end - start);
                const QString placeholder = QStringLiteral("@@FASTMD_MATH_%1@@").arg(mathSnippets->size());
                const QString delim = block ? QStringLiteral("$$") : QStringLiteral("$");
                mathSnippets->append(makeMathSnippet(mode, block, expr, delim, delim));
                out += placeholder;
                i = end + (block ? 2 : 1);
                continue;
            }
        }

        // LaTeX bracket delimiters: \(...\) (inline) and \[...\] (display).
        if (!inInlineCode && ch == '\\' && i + 1 < markdown.size()) {
            const QChar next = markdown.at(i + 1);
            if (next == '(' || next == '[') {
                const bool block = (next == '[');
                const QString closeDelim = block ? QStringLiteral("\\]") : QStringLiteral("\\)");
                const int start = i + 2;
                const int end = markdown.indexOf(closeDelim, start);
                if (end >= start) {
                    const QString expr = markdown.mid(start, end - start);
                    const QString placeholder = QStringLiteral("@@FASTMD_MATH_%1@@").arg(mathSnippets->size());
                    const QString openDelim = block ? QStringLiteral("\\[") : QStringLiteral("\\(");
                    mathSnippets->append(makeMathSnippet(mode, block, expr, openDelim, closeDelim));
                    out += placeholder;
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

} // namespace

// ---------------------------------------------------------------------------
QString ExportManager::markdownToHtml(const QString& markdown, MathRenderMode mode, bool* mathDetected)
{
    QString md = markdown;

    // Auto-fix links and images that have spaces or brackets in their unquoted paths
    // e.g., ![alt](C:/path with space[1].png) -> ![alt](<C:/path with space[1].png>)
    QRegularExpression re(QStringLiteral(R"((!?\[[^\]\n]*\])\(([^)<>\"\n]*(?:\s+|\[|\])[^)<>\"\n]*)\))"));
    md.replace(re, QStringLiteral("\\1(<\\2>)"));

    QStringList mathSnippets;
    const QString preprocessed = preprocessMath(md, mode, &mathSnippets);
    if (mathDetected)
        *mathDetected = !mathSnippets.isEmpty();
    QByteArray utf8 = preprocessed.toUtf8();
    QString result;

    auto cb = [](const MD_CHAR* out, MD_SIZE sz, void* ud) {
        static_cast<QString*>(ud)->append(QString::fromUtf8(out, static_cast<int>(sz)));
    };

    unsigned flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), cb, &result, flags, 0);
    for (int i = 0; i < mathSnippets.size(); ++i)
        result.replace(QStringLiteral("@@FASTMD_MATH_%1@@").arg(i), mathSnippets.at(i));

    // Decode URL-encoded paths in src="..." so QTextDocument/QTextBrowser can find local files with spaces
    QRegularExpression srcRe(QStringLiteral("src=\"([^\"]+)\""));
    QRegularExpressionMatchIterator it = srcRe.globalMatch(result);
    int offset = 0;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString srcUrl = match.captured(1);
        if (srcUrl.contains('%')) {
            QString decoded = QUrl::fromPercentEncoding(srcUrl.toUtf8());
            if (decoded != srcUrl) {
                result.replace(match.capturedStart(1) + offset, srcUrl.length(), decoded);
                offset += decoded.length() - srcUrl.length();
            }
        }
    }

    return result;
}

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
static QString buildKatexHead()
{
    const QString dir = ExportManager::katexAssetsDir();
    if (dir.isEmpty())
        return {};

    const QString cssUrl  = QUrl::fromLocalFile(dir + QStringLiteral("/katex.min.css")).toString();
    const QString jsUrl   = QUrl::fromLocalFile(dir + QStringLiteral("/katex.min.js")).toString();
    const QString autoUrl = QUrl::fromLocalFile(dir + QStringLiteral("/contrib/auto-render.min.js")).toString();

    // Both scripts are deferred so they execute in order: katex defines the API,
    // auto-render's onload then typesets all four delimiter styles.
    return QStringLiteral(
        "<link rel=\"stylesheet\" href=\"%1\">"
        "<script defer src=\"%2\"></script>"
        "<script defer src=\"%3\" onload=\""
        "renderMathInElement(document.body,{delimiters:["
        "{left:'$$',right:'$$',display:true},"
        "{left:'\\\\[',right:'\\\\]',display:true},"
        "{left:'\\\\(',right:'\\\\)',display:false},"
        "{left:'$',right:'$',display:false}"
        "],throwOnError:false});\"></script>"
    ).arg(cssUrl, jsUrl, autoUrl);
}

// ---------------------------------------------------------------------------
static QString buildCss(bool dark, int fontSize, bool isPrint)
{
    auto c = [dark](const char* ld, const char* dk) -> QLatin1StringView {
        return dark ? QLatin1StringView(dk) : QLatin1StringView(ld);
    };

    QLatin1StringView bg      = isPrint ? QLatin1StringView("#ffffff") : c("#f9f9f9", "#252526");
    QLatin1StringView fg      = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#c9d1d9");
    QLatin1StringView codeBg  = isPrint ? QLatin1StringView("#f0f0f0") : c("#eaebed", "#383e44");
    QLatin1StringView codeFg  = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#c9d1d9");
    QLatin1StringView preBg   = isPrint ? QLatin1StringView("#f0f0f0") : c("#d8dce3", "#141414");
    QLatin1StringView bqBdr   = c("#d0d7de", "#30363d");
    QLatin1StringView bqFg    = c("#57606a", "#8b949e");
    QLatin1StringView link    = c("#0969da", "#58a6ff");
    QLatin1StringView border  = c("#d0d7de", "#30363d");
    QLatin1StringView h1Bdr   = c("#d0d7de", "#21262d");

    QString fontSizeUnit = QStringLiteral("px");
    QString marginStyle  = isPrint ? QStringLiteral("0") : QStringLiteral("32px 36px 24px");
    int actualFontSize   = fontSize;
    auto headingSize = [actualFontSize](double scale, int minSize) {
        return QString::number(qMax(minSize, qRound(actualFontSize * scale)));
    };

    const QString h1Size = headingSize(2.2, 26);
    const QString h2Size = headingSize(2.0, 22);
    const QString h3Size = headingSize(1.8, 19);
    const QString h4Size = headingSize(1.6, 16);
    const QString h5Size = headingSize(1.4, 14);
    const QString h6Size = headingSize(1.2, 12);

    QString css = QStringLiteral(
        // reset & base
        "*{box-sizing:border-box;}"
        "body{"
        "  background-color:%1;color:%2;"
        "  font-family:'Segoe UI','SF Pro Text',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif;"
        "  font-size:%11%12;"
        "  line-height:1.2;"
        "  margin: %13;"
        "}"
        // headings
        "h1,h2,h3,h4,h5,h6{margin-top:28px;font-weight:700;line-height:1.2;color:%2;}"
        "h1{margin-bottom:24px;border-bottom:1px solid %10;padding-bottom:0.25em;}"
        "h2{margin-bottom:20px;border-bottom:1px solid %10;padding-bottom:0.2em;}"
        "h3{margin-bottom:16px;}"
        "h4{margin-bottom:13px;}"
        "h5{margin-bottom:10px;}"
        "h6{margin-bottom:8px;}"
        // paragraph & lists
        "p{margin-top:0;margin-bottom:0.5em;}"
        "ul,ol{margin-top:0;margin-bottom:0.5em;padding-left:1.5em;}"
        "li{margin-top:0.2em;margin-bottom:0.2em;}"
        "li > p{margin-bottom:0.5em;}"
        // inline code
        "code{"
        "  font-family:'JetBrains Mono','IBM Plex Mono','Cascadia Code','Cascadia Mono',Consolas,monospace;"
        "  font-size:0.92em;"
        "  background-color:%3;color:%4;"
        "  padding:0.3em 0.3em;border-radius:4px;"
        "}"
        ".math-inline,.math-block{font-family:'Cambria Math','Times New Roman',serif;}"
        ".math-inline{font-size:1.05em;}"
        ".math-block{margin-top:16px;margin-bottom:16px;text-align:center;font-size:1.1em;}"
        // fenced code block
        ".code-block{"
        "  background-color:%5;"
        "  margin-top:16px;margin-bottom:16px;"
        "  width:100%; border:none; border-collapse:collapse;"
        "}"
        ".code-block td{padding:12px; border:none;}"
        "pre{margin:0; background-color:transparent; border:none; line-height:1.2;}"
        "pre code{background-color:transparent;color:inherit;font-size:0.9em;padding:0;border-radius:0;}"
        // blockquote
        "blockquote{"
        "  border-left:4px solid %6;margin-top:16px;margin-bottom:16px;"
        "  padding:4px 12px;color:%7;font-style:italic;"
        "}"
        "blockquote p{margin:0;}"
        // links
        "a{color:%8;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        // tables
        "table{border-collapse:collapse;margin-top:16px;margin-bottom:16px;width:100%;}"
        "th,td{border:1px solid %9;padding:6px 12px;text-align:left;}"
        "th{font-weight:700;}"
        "tr:nth-child(2n){background-color:%5;}"
        // misc
        "img{max-width:100%;}"
        "hr{border:none;border-bottom:2px solid %9;margin-top:24px;margin-bottom:24px;}"
    ).arg(bg, fg, codeBg, codeFg, preBg, bqBdr, bqFg, link, border, h1Bdr).arg(actualFontSize).arg(fontSizeUnit).arg(marginStyle);

    // Print-only refinements. These rely on real CSS support, so they only take
    // effect on the browser PDF path (the QTextDocument fallback ignores the
    // properties it doesn't understand, which is harmless).
    if (isPrint) {
        css += QStringLiteral(
            "body{line-height:1.5;-webkit-print-color-adjust:exact;print-color-adjust:exact;}"
            "p,li,blockquote{orphans:3;widows:3;}"
            // keep headings attached to the content that follows them
            "h1,h2,h3,h4,h5,h6{page-break-after:avoid;break-after:avoid;}"
            // never split code blocks, tables, blockquotes or images across pages
            "pre,blockquote,table,img{page-break-inside:avoid;break-inside:avoid;}"
            // wrap long inline code / links instead of overflowing the page
            "code{white-space:pre-wrap;overflow-wrap:anywhere;}"
            "a{overflow-wrap:anywhere;}"
        );
    }

    return css;
}

// ---------------------------------------------------------------------------
QString ExportManager::buildFullHtml(const QString& bodyHtml, bool dark, int fontSize, bool isPrint, bool injectKatex)
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
    QLatin1StringView fg    = isPrint ? QLatin1StringView("#000000") : c("#24292f", "#c9d1d9");
    QLatin1StringView preBg = isPrint ? QLatin1StringView("#f0f0f0") : c("#d8dce3", "#141414");
    QLatin1StringView h1Bdr = c("#d0d7de", "#21262d");

    int actualFontSize = fontSize;
    auto headingSize = [actualFontSize](double scale, int minSize) {
        return QString::number(qMax(minSize, qRound(actualFontSize * scale)));
    };
    const QString h1Size = headingSize(2.2, 26);
    const QString h2Size = headingSize(2.0, 22);
    const QString h3Size = headingSize(1.8, 19);
    const QString h4Size = headingSize(1.6, 16);
    const QString h5Size = headingSize(1.4, 14);
    const QString h6Size = headingSize(1.2, 12);

    // Pre-compiled heading regexes (allocated once per level).
    static const QRegularExpression rxH[7] = {
        {},  // index 0 unused
        QRegularExpression(QStringLiteral(R"(<h1[^>]*>(.*?)</h1>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h2[^>]*>(.*?)</h2>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h3[^>]*>(.*?)</h3>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h4[^>]*>(.*?)</h4>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h5[^>]*>(.*?)</h5>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression(QStringLiteral(R"(<h6[^>]*>(.*?)</h6>)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
    };

    QString finalBody = bodyHtml;

    finalBody.replace(rxH[1],
        QStringLiteral("<table width=\"100%\" style=\"border-collapse:collapse; width:100%; margin-top:26px; margin-bottom:24px;\"><tr><td style=\"font-size:%1px; font-weight:700; line-height:1.2; color:%2; border:none; border-bottom:1px solid %3; padding:0 0 0px 0;\">\\1</td></tr></table>").arg(h1Size, fg, h1Bdr));
    finalBody.replace(rxH[2],
        QStringLiteral("<table width=\"100%\" style=\"border-collapse:collapse; width:100%; margin-top:22px; margin-bottom:20px;\"><tr><td style=\"font-size:%1px; font-weight:700; line-height:1.2; color:%2; border:none; border-bottom:1px solid %3; padding:0 0 0px 0;\">\\1</td></tr></table>").arg(h2Size, fg, h1Bdr));
    finalBody.replace(rxH[3], QStringLiteral("<div style=\"font-size:%1px; margin-top:19px; margin-bottom:16px; font-weight:700; line-height:1.2; color:%2;\">\\1</div>").arg(h3Size, fg));
    finalBody.replace(rxH[4], QStringLiteral("<div style=\"font-size:%1px; margin-top:16px; margin-bottom:13px; font-weight:700; line-height:1.2; color:%2;\">\\1</div>").arg(h4Size, fg));
    finalBody.replace(rxH[5], QStringLiteral("<div style=\"font-size:%1px; margin-top:14px; margin-bottom:10px; font-weight:700; line-height:1.2; color:%2;\">\\1</div>").arg(h5Size, fg));
    finalBody.replace(rxH[6], QStringLiteral("<div style=\"font-size:%1px; margin-top:12px; margin-bottom:8px; font-weight:700; line-height:1.2; color:%2;\">\\1</div>").arg(h6Size, fg));

    // Replace <pre><code with a table to guarantee background color and padding in QTextBrowser.
    finalBody.replace(QStringLiteral("<pre><code"),
        QStringLiteral("<table width=\"100%\" style=\"margin-top:16px; margin-bottom:16px; border-collapse:separate; page-break-inside:avoid;\"><tr><td bgcolor=\"%1\" style=\"background-color:%1; padding:12px 14px; border:1px solid %2; border-radius:6px;\"><pre style=\"margin:0; background-color:%1; border:none; white-space:pre-wrap; word-break:break-word; overflow-wrap:anywhere;\"><code").arg(preBg, h1Bdr));
    finalBody.replace(QStringLiteral("</code></pre>"), QStringLiteral("</code></pre></td></tr></table>"));

    // Only reference KaTeX assets when math is actually present, so the default
    // in-app preview path stays free of any KaTeX injection.
    const QString katexHead = injectKatex ? buildKatexHead() : QString();

    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>%1</style>"
        "%3"
        "</head><body>%2</body></html>"
    ).arg(css, finalBody, katexHead);
}

// ---------------------------------------------------------------------------
bool ExportManager::exportHtml(const QString& markdown, const QString& filePath, bool dark)
{
    // Regenerate from raw markdown using the KaTeX path so exported HTML keeps
    // real LaTeX math (never the Unicode preview output).
    bool mathDetected = false;
    const QString body = markdownToHtml(markdown, MathRenderMode::KatexOutput, &mathDetected);
    const QString fullHtml = buildFullHtml(body, dark, FastMdDefaults::PreviewFontSize, false, mathDetected);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << fullHtml;
    return true;
}

// ---------------------------------------------------------------------------
// Locate an installed Chrome or Edge on Windows for headless PDF printing.
static QString findBrowserExecutable()
{
    QStringList candidates;
    auto addEnv = [&](const char* var, const char* rel) {
        const QString base = qEnvironmentVariable(var);
        if (!base.isEmpty())
            candidates << base + QLatin1StringView(rel);
    };
    addEnv("ProgramFiles",      "/Google/Chrome/Application/chrome.exe");
    addEnv("ProgramFiles(x86)", "/Google/Chrome/Application/chrome.exe");
    addEnv("LocalAppData",      "/Google/Chrome/Application/chrome.exe");
    addEnv("ProgramFiles",      "/Microsoft/Edge/Application/msedge.exe");
    addEnv("ProgramFiles(x86)", "/Microsoft/Edge/Application/msedge.exe");
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }

    // Best-effort registry "App Paths" lookup.
    const char* keys[] = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
    };
    for (const char* k : keys) {
        QSettings reg(QString::fromLatin1(k), QSettings::NativeFormat);
        const QString path = reg.value(QStringLiteral(".")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            return path;
    }
    return {};
}

// CSS @page rule so the headless browser honors the chosen page size & margins.
static QString buildPageCss(const ExportManager::PdfExportOptions& options)
{
    QSizeF sizeMm = options.pageSize.size(QPageSize::Millimeter);
    if (options.orientation == QPageLayout::Landscape && sizeMm.width() < sizeMm.height())
        sizeMm.transpose();
    const QMarginsF m = options.marginsMm;
    return QStringLiteral(
        "<style>@page{size:%1mm %2mm;margin:%3mm %4mm %5mm %6mm;}</style>")
        .arg(QString::number(sizeMm.width(), 'f', 2),
             QString::number(sizeMm.height(), 'f', 2),
             QString::number(m.top(), 'f', 2),
             QString::number(m.right(), 'f', 2),
             QString::number(m.bottom(), 'f', 2),
             QString::number(m.left(), 'f', 2));
}

// Fast, offline QTextDocument/QPrinter path used when the document has no math.
static bool exportPdfViaQTextDocument(const QString& bodyHtml, const QString& filePath,
                                      const QString& docPath, const ExportManager::PdfExportOptions& options)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(options.pageSize);
    printer.setPageOrientation(options.orientation);
    printer.setPageMargins(options.marginsMm, QPageLayout::Millimeter);

    // Scale from points (72 DPI) to logical pixels (96 DPI) so QTextDocument renders 1:1 without blowing up fonts.
    const QSizeF pageSize = printer.pageRect(QPrinter::Point).size() * (96.0 / 72.0);
    const int pageWidthPx = qRound(pageSize.width());

    QString fullHtml = ExportManager::buildFullHtml(bodyHtml, false, options.fontSize, true);

    // QTextDocument ignores CSS max-width percentages, so stamp an explicit pixel max-width on every <img>.
    static const QRegularExpression imgTagRe(
        QStringLiteral("<img((?:\\s+[^>]*)?)(/?)>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
    );
    static const QRegularExpression styleAttrRe(
        QStringLiteral("\\bstyle\\s*=\\s*\"([^\"]*)\""),
        QRegularExpression::CaseInsensitiveOption
    );
    const QString maxWidthDecl = QStringLiteral("max-width:%1px;").arg(pageWidthPx);

    QRegularExpressionMatchIterator imgIt = imgTagRe.globalMatch(fullHtml);
    int offset = 0;
    while (imgIt.hasNext()) {
        const QRegularExpressionMatch m = imgIt.next();
        QString attrs   = m.captured(1);
        const QString slash = m.captured(2);

        QRegularExpressionMatch styleMatch = styleAttrRe.match(attrs);
        if (styleMatch.hasMatch()) {
            QString existingStyle = styleMatch.captured(1);
            if (!existingStyle.endsWith(';')) existingStyle += ';';
            existingStyle += maxWidthDecl;
            attrs.replace(styleMatch.capturedStart(1), styleMatch.capturedLength(1), existingStyle);
        } else {
            attrs += QStringLiteral(" style=\"%1\"").arg(maxWidthDecl);
        }

        const QString newTag = QStringLiteral("<img%1%2>").arg(attrs, slash);
        fullHtml.replace(m.capturedStart() + offset, m.capturedLength(), newTag);
        offset += newTag.length() - m.capturedLength();
    }

    QTextDocument doc;
    if (!docPath.isEmpty()) {
        doc.setBaseUrl(QUrl::fromLocalFile(QFileInfo(docPath).absolutePath() + "/"));
    }
    doc.setHtml(fullHtml);
    doc.setPageSize(pageSize);
    doc.print(&printer);
    return true;
}

// Render a PDF by driving an installed Chrome/Edge in headless mode. This
// produces a properly authored PDF that renders identically across all viewers
// (Edge, Chrome, Adobe, Foxit) and supports the full CSS stylesheet, unlike the
// limited QTextDocument path. Used whenever a browser is available.
static bool exportPdfViaBrowser(const QString& bodyHtml, const QString& filePath,
                                const QString& docPath, const ExportManager::PdfExportOptions& options,
                                const QString& browser, bool injectKatex, QWidget* parent)
{
    // Build a self-referencing temporary HTML file.
    QString fullHtml = ExportManager::buildFullHtml(bodyHtml, false, options.fontSize, true, injectKatex);
    fullHtml.insert(fullHtml.indexOf(QStringLiteral("</head>")), buildPageCss(options));

    // Place the temp HTML next to the source document (if any) so relative image
    // paths resolve; otherwise fall back to the system temp directory.
    const QString tempDir = docPath.isEmpty()
        ? QDir::tempPath()
        : QFileInfo(docPath).absolutePath();
    const QString tempHtml = QDir(tempDir).filePath(
        QStringLiteral(".fastmd_pdf_%1.html").arg(QFileInfo(filePath).completeBaseName()));

    {
        QFile f(tempHtml);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (parent)
                QMessageBox::warning(parent, QObject::tr("PDF Export Failed"),
                                     QObject::tr("Could not create a temporary HTML file for PDF export."));
            return false;
        }
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << fullHtml;
    }

    // Isolated profile dir so we never collide with a browser the user already
    // has open (a locked default profile can make headless printing hang/fail).
    const QString profileDir = QDir(QDir::tempPath()).filePath(
        QStringLiteral(".fastmd_chrome_%1").arg(QFileInfo(filePath).completeBaseName()));

    QProcess proc;
    QStringList args;
    args << QStringLiteral("--headless")
         << QStringLiteral("--disable-gpu")
         << QStringLiteral("--no-pdf-header-footer")
         << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(profileDir)
         << QStringLiteral("--print-to-pdf=") + QDir::toNativeSeparators(filePath)
         << QStringLiteral("--virtual-time-budget=10000")
         << QUrl::fromLocalFile(tempHtml).toString();

    // Remove any stale output so we can reliably detect success.
    QFile::remove(filePath);

    proc.start(browser, args);
    bool ok = proc.waitForStarted(10000);
    if (ok)
        ok = proc.waitForFinished(60000);

    // The browser may keep the file briefly; confirm a non-empty PDF was produced.
    const bool produced = QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0;

    QFile::remove(tempHtml);
    QDir(profileDir).removeRecursively();

    if (!ok || !produced) {
        if (parent)
            QMessageBox::warning(parent, QObject::tr("PDF Export Failed"),
                                 QObject::tr("The browser failed to generate the PDF.\n\n%1")
                                     .arg(QString::fromLocal8Bit(proc.readAllStandardError()).trimmed()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool ExportManager::exportPdf(const QString& markdown, const QString& filePath, const QString& docPath, const PdfExportOptions& options, QWidget* parent)
{
    bool mathDetected = false;
    const QString katexBody = markdownToHtml(markdown, MathRenderMode::KatexOutput, &mathDetected);

    const QString browser = findBrowserExecutable();

    // Prefer the browser whenever one is available: it yields a properly authored
    // PDF that renders consistently in every viewer (Edge's PDF engine, in
    // particular, hides QTextDocument table-cell text behind its background fill)
    // and honors the full CSS stylesheet. Math additionally *requires* a browser
    // so KaTeX can typeset it.
    if (!browser.isEmpty() && (!mathDetected || !katexAssetsDir().isEmpty())) {
        return exportPdfViaBrowser(katexBody, filePath, docPath, options, browser,
                                   /*injectKatex=*/mathDetected, parent);
    }

    // From here, no usable browser path. Math can't be rendered without one.
    if (mathDetected) {
        if (parent) {
            const QString reason = katexAssetsDir().isEmpty()
                ? QObject::tr("Bundled KaTeX math assets were not found next to the application.")
                : QObject::tr("This document contains math, which requires Google Chrome or "
                              "Microsoft Edge to be installed. Neither browser was found.");
            QMessageBox::warning(parent, QObject::tr("PDF Export Failed"),
                                 QObject::tr("Cannot render math for PDF export.\n\n%1").arg(reason));
        }
        return false;
    }

    // No math, no browser: fall back to the offline QTextDocument/QPrinter path.
    const QString body = markdownToHtml(markdown, MathRenderMode::UnicodePreview);
    if (!exportPdfViaQTextDocument(body, filePath, docPath, options)) {
        if (parent)
            QMessageBox::warning(parent, QObject::tr("Export Failed"),
                                 QObject::tr("Could not write %1").arg(filePath));
        return false;
    }
    return true;
}
