#include "MathRenderer.h"
#include "MathDelimiters.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace FastMdMath {

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

static QString renderMathInline(const QString& expr, bool displayMode = false);

// Reads one LaTeX macro argument starting at pos: a {balanced} group, a single
// backslash command (\alpha), or a single bare character (as in \frac12,
// \sqrt3). Returns the position after the argument, or -1 if none available.
static int readMathArg(const QString& expr, int pos, QString* out)
{
    while (pos < expr.size() && expr.at(pos).isSpace())
        ++pos;
    if (pos >= expr.size())
        return -1;
    const QChar ch = expr.at(pos);
    if (ch == '{')
        return consumeBalanced(expr, pos, '{', '}', out);
    if (ch == '\\' && pos + 1 < expr.size()) {
        int p = pos + 1;
        while (p < expr.size() && expr.at(p).isLetter())
            ++p;
        *out = expr.mid(pos, p - pos);
        return p;
    }
    *out = QString(ch);
    return pos + 1;
}

static QString renderMathToken(const QString& expr, const char* tag)
{
    return QStringLiteral("<%1>%2</%1>").arg(QLatin1StringView(tag), renderMathInline(expr));
}

static QString renderMathInline(const QString& expr, bool displayMode)
{
    // QTextDocument has no TeX engine, so keep the fallback deliberately
    // small and structural: tables cover matrix-like environments while the
    // surrounding HTML remains cheap to build on every editor debounce.
    static const QRegularExpression environmentRe(
        QStringLiteral(R"(^\s*\\begin\{([A-Za-z]+)\}(?:\{[^}]*\})?([\s\S]*)\\end\{\1\}\s*$)"));
    const QRegularExpressionMatch environment = environmentRe.match(expr);
    if (environment.hasMatch()) {
        const QString name = environment.captured(1);
        const QStringList rows = environment.captured(2).split(QStringLiteral("\\\\"), Qt::SkipEmptyParts);
        if (name == QStringLiteral("cases")) {
            QString casesTable = QStringLiteral("<table style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\"><tbody>");
            for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
                if (rowIndex == 0)
                    casesTable += QStringLiteral("<tr><td rowspan=\"%1\" style=\"border:0;padding:0 7px 0 0;font-family:serif;font-size:3.2em;line-height:0.8;vertical-align:middle;\">{</td>").arg(rows.size());
                else
                    casesTable += QStringLiteral("<tr>");
                for (const QString& cell : rows.at(rowIndex).split(QChar('&')))
                    casesTable += QStringLiteral("<td style=\"border:0;padding:0 10px 0 0;vertical-align:middle;\">%1</td>").arg(renderMathInline(cell.trimmed()));
                casesTable += QStringLiteral("</tr>");
            }
            return casesTable + QStringLiteral("</tbody></table>");
        }
        QString table = QStringLiteral("<table style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;\"><tbody>");
        for (const QString& row : rows) {
            table += QStringLiteral("<tr>");
            for (const QString& cell : row.split(QChar('&')))
                table += QStringLiteral("<td style=\"border:0;padding:0 6px;\">%1</td>").arg(renderMathInline(cell.trimmed()));
            table += QStringLiteral("</tr>");
        }
        table += QStringLiteral("</tbody></table>");
        QString left;
        QString right;
        if (name == QStringLiteral("pmatrix")) {
            left = QStringLiteral("("); right = QStringLiteral(")");
        } else if (name == QStringLiteral("bmatrix")) {
            left = QStringLiteral("["); right = QStringLiteral("]");
        } else if (name == QStringLiteral("Bmatrix")) {
            left = QStringLiteral("{"); right = QStringLiteral("}");
        } else if (name == QStringLiteral("vmatrix")) {
            left = QStringLiteral("|"); right = QStringLiteral("|");
        } else if (name == QStringLiteral("Vmatrix")) {
            left = QStringLiteral("‖"); right = QStringLiteral("‖");
        }
        if (!left.isEmpty() || !right.isEmpty()) {
            return QStringLiteral("<table style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\"><tr>"
                                 "<td style=\"border:0;padding:0 3px;vertical-align:middle;font-size:1.8em;line-height:1;\">%1</td>"
                                 "<td style=\"border:0;padding:0;vertical-align:middle;\">%2</td>"
                                 "<td style=\"border:0;padding:0 3px;vertical-align:middle;font-size:1.8em;line-height:1;\">%3</td>"
                                 "</tr></table>").arg(left, table, right);
        }
        return table;
    }

    static const QRegularExpression embeddedEnvironmentStart(
        QStringLiteral(R"(\\begin\{([A-Za-z]+)\})"));
    const QRegularExpressionMatch embeddedStart = embeddedEnvironmentStart.match(expr);
    if (embeddedStart.hasMatch()) {
        const QString endToken = QStringLiteral("\\end{%1}").arg(embeddedStart.captured(1));
        const int endPos = expr.indexOf(endToken, embeddedStart.capturedEnd());
        if (endPos >= 0) {
            const int endAfter = endPos + endToken.size();
            const QString prefix = expr.left(embeddedStart.capturedStart());
            const QString environmentText = expr.mid(embeddedStart.capturedStart(), endAfter - embeddedStart.capturedStart());
            const QString suffix = expr.mid(endAfter);
            if (!prefix.trimmed().isEmpty() || !suffix.trimmed().isEmpty()) {
                QString html = QStringLiteral("<table style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\"><tr>");
                if (!prefix.isEmpty()) {
                    html += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(renderMathInline(prefix));
                }
                html += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(renderMathInline(environmentText));
                if (!suffix.isEmpty()) {
                    html += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(renderMathInline(suffix));
                }
                html += QStringLiteral("</tr></table>");
                return html;
            }
            return renderMathInline(environmentText);
        }
    }

    QString html;
    html.reserve(expr.size() * 2);

    for (int i = 0; i < expr.size();) {
        const QChar ch = expr.at(i);

        if (ch == '\\' && i + 1 < expr.size()) {
            const QChar next = expr.at(i + 1);
            if (QStringLiteral("%$&#_{}^~\\|[]()").contains(next)) {
                html += QString(next).toHtmlEscaped();
                i += 2;
                continue;
            }

            // Spacing macros (\ , \, \; \: \!) render as a plain space.
            if (QStringLiteral(" ,;:!").contains(next)) {
                html += QChar(' ');
                i += 2;
                continue;
            }

            if (next.isLetter()) {
                static const QSet<QString> knownCommands = {
                    QStringLiteral("frac"), QStringLiteral("dfrac"), QStringLiteral("tfrac"), QStringLiteral("sqrt"), QStringLiteral("quad"), QStringLiteral("qquad"),
                    QStringLiteral("text"), QStringLiteral("mathrm"), QStringLiteral("mathit"), QStringLiteral("mathbf"), QStringLiteral("boldsymbol"), QStringLiteral("mathsf"), QStringLiteral("mathtt"), QStringLiteral("mathcal"), QStringLiteral("mathbb"), QStringLiteral("mathfrak"), QStringLiteral("mathscr"),
                    QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("langle"), QStringLiteral("rangle"), QStringLiteral("lfloor"), QStringLiteral("rfloor"), QStringLiteral("lceil"), QStringLiteral("rceil"), QStringLiteral("lvert"), QStringLiteral("rvert"), QStringLiteral("lVert"), QStringLiteral("rVert"),
                    QStringLiteral("bar"), QStringLiteral("overline"), QStringLiteral("underline"), QStringLiteral("hat"), QStringLiteral("widehat"), QStringLiteral("tilde"), QStringLiteral("widetilde"), QStringLiteral("dot"), QStringLiteral("ddot"), QStringLiteral("breve"), QStringLiteral("check"), QStringLiteral("acute"), QStringLiteral("grave"), QStringLiteral("vec"), QStringLiteral("overrightarrow"), QStringLiteral("overleftarrow"),
                    QStringLiteral("binom"), QStringLiteral("bmod"), QStringLiteral("bmod"), QStringLiteral("pmod"), QStringLiteral("bmod"),
                    QStringLiteral("limits"), QStringLiteral("nolimits"), QStringLiteral("operatorname"),
                    QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma"), QStringLiteral("delta"),
                    QStringLiteral("epsilon"), QStringLiteral("zeta"), QStringLiteral("eta"), QStringLiteral("theta"),
                    QStringLiteral("iota"), QStringLiteral("kappa"), QStringLiteral("lambda"), QStringLiteral("mu"),
                    QStringLiteral("nu"), QStringLiteral("xi"), QStringLiteral("omicron"), QStringLiteral("pi"),
                    QStringLiteral("rho"), QStringLiteral("sigma"), QStringLiteral("tau"), QStringLiteral("upsilon"),
                    QStringLiteral("phi"), QStringLiteral("chi"), QStringLiteral("psi"), QStringLiteral("omega"),
                    QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma"), QStringLiteral("Delta"),
                    QStringLiteral("Epsilon"), QStringLiteral("Zeta"), QStringLiteral("Eta"), QStringLiteral("Theta"),
                    QStringLiteral("Iota"), QStringLiteral("Kappa"), QStringLiteral("Lambda"), QStringLiteral("Mu"),
                    QStringLiteral("Nu"), QStringLiteral("Xi"), QStringLiteral("Omicron"), QStringLiteral("Pi"),
                    QStringLiteral("Rho"), QStringLiteral("Sigma"), QStringLiteral("Tau"), QStringLiteral("Upsilon"),
                    QStringLiteral("Phi"), QStringLiteral("Chi"), QStringLiteral("Psi"), QStringLiteral("Omega"),
                    QStringLiteral("sum"), QStringLiteral("int"), QStringLiteral("infty"), QStringLiteral("approx"),
                    QStringLiteral("neq"), QStringLiteral("pm"), QStringLiteral("cdot"), QStringLiteral("times"),
                    QStringLiteral("div"), QStringLiteral("leq"), QStringLiteral("geq"), QStringLiteral("rightarrow"),
                    QStringLiteral("leftarrow"), QStringLiteral("Rightarrow"), QStringLiteral("Leftarrow"),
                    QStringLiteral("in"), QStringLiteral("notin"), QStringLiteral("subset"), QStringLiteral("propto"),
                    QStringLiteral("partial"), QStringLiteral("nabla"), QStringLiteral("sin"), QStringLiteral("cos"),
                    QStringLiteral("tan"), QStringLiteral("log"), QStringLiteral("ln"), QStringLiteral("lim"),
                    QStringLiteral("exp"), QStringLiteral("le"), QStringLiteral("ge"), QStringLiteral("to"),
                    QStringLiteral("gets"), QStringLiteral("ell"), QStringLiteral("forall"), QStringLiteral("exists"),
                    QStringLiteral("empty"), QStringLiteral("cup"), QStringLiteral("cap"), QStringLiteral("wedge"),
                    QStringLiteral("vee"), QStringLiteral("equiv"), QStringLiteral("cong"), QStringLiteral("sim"),
                    QStringLiteral("circ"), QStringLiteral("angle"), QStringLiteral("doteq"), QStringLiteral("ll"), QStringLiteral("gg"), QStringLiteral("infty"),
                    QStringLiteral("sum"), QStringLiteral("prod"), QStringLiteral("coprod"), QStringLiteral("iint"), QStringLiteral("iiint"), QStringLiteral("oint"), QStringLiteral("limsup"), QStringLiteral("liminf"), QStringLiteral("max"), QStringLiteral("min"), QStringLiteral("sup"), QStringLiteral("inf"), QStringLiteral("gcd"), QStringLiteral("deg"), QStringLiteral("det"), QStringLiteral("Pr"), QStringLiteral("arcsin"), QStringLiteral("arccos"), QStringLiteral("arctan"), QStringLiteral("sinh"), QStringLiteral("cosh"), QStringLiteral("tanh"), QStringLiteral("cot"), QStringLiteral("sec"), QStringLiteral("csc"),
                    QStringLiteral("therefore"), QStringLiteral("because"), QStringLiteral("nexists"), QStringLiteral("neg"), QStringLiteral("land"), QStringLiteral("lor"), QStringLiteral("implies"), QStringLiteral("iff"), QStringLiteral("ni"), QStringLiteral("subseteq"), QStringLiteral("subsetneq"), QStringLiteral("supset"), QStringLiteral("supseteq"), QStringLiteral("supsetneq"), QStringLiteral("setminus"), QStringLiteral("emptyset"), QStringLiteral("varnothing"), QStringLiteral("parallel"), QStringLiteral("nparallel"), QStringLiteral("perp"), QStringLiteral("measuredangle"), QStringLiteral("triangle"), QStringLiteral("triangleleft"), QStringLiteral("triangleright"), QStringLiteral("ldots"), QStringLiteral("cdots"), QStringLiteral("vdots"), QStringLiteral("ddots"),
                    QStringLiteral("circ"), QStringLiteral("angle"),
                };

                int maxPos = i + 1;
                while (maxPos < expr.size() && expr.at(maxPos).isLetter()) {
                    ++maxPos;
                }
                // Greedily consuming every following letter breaks commands that
                // are immediately followed by a bare-argument letter, e.g.
                // \fracmn would otherwise be read as the single unknown command
                // "fracmn". Prefer the longest known command name instead.
                int pos = i + 1;
                for (int end = maxPos; end > i + 1; --end) {
                    if (knownCommands.contains(expr.mid(i + 1, end - (i + 1)))) {
                        pos = end;
                        break;
                    }
                }
                if (pos == i + 1)
                    pos = maxPos;
                QString cmd = expr.mid(i + 1, pos - (i + 1));

                if (cmd == QStringLiteral("frac") || cmd == QStringLiteral("dfrac") || cmd == QStringLiteral("tfrac")) {
                    QString num;
                    int afterNum = readMathArg(expr, pos, &num);
                    if (afterNum > 0) {
                        QString den;
                        int afterDen = readMathArg(expr, afterNum, &den);
                        if (afterDen > 0) {
                            if (displayMode) {
                                html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\">"
                                                       "<tr><td style=\"border:0;border-bottom:1px solid currentColor;padding:0 3px;text-align:center;vertical-align:baseline;\">%1</td></tr>"
                                                       "<tr><td style=\"border:0;padding:0 3px;text-align:center;\">%2</td></tr>"
                                                       "</table>")
                                            .arg(renderMathInline(num, false), renderMathInline(den, false));
                            } else {
                                html += QStringLiteral("<span style=\"white-space:nowrap;\"><sup>%1</sup>&thinsp;/&thinsp;<sub>%2</sub></span>")
                                            .arg(renderMathInline(num, false), renderMathInline(den, false));
                            }
                            i = afterDen;
                            continue;
                        }
                    }
                } else if (cmd == QStringLiteral("sqrt")) {
                    QString root;
                    int rootEnd = pos;
                    if (rootEnd < expr.size() && expr.at(rootEnd) == '[')
                        rootEnd = consumeBalanced(expr, rootEnd, '[', ']', &root);
                    QString inner;
                    int afterInner = readMathArg(expr, rootEnd > pos ? rootEnd : pos, &inner);
                    if (afterInner > 0) {
                        const QString index = root.isEmpty() ? QString() : QStringLiteral("<sup style=\"font-size:0.7em;\">%1</sup>").arg(renderMathInline(root));
                        html += QStringLiteral("%1&radic;<span style=\"text-decoration:overline;\">%2</span>")
                                    .arg(index, renderMathInline(inner));

                        i = afterInner;
                        continue;
                    }
                } else if (cmd == QStringLiteral("text") || cmd == QStringLiteral("mathrm") || cmd == QStringLiteral("mathit") || cmd == QStringLiteral("mathbf") || cmd == QStringLiteral("boldsymbol") || cmd == QStringLiteral("mathsf") || cmd == QStringLiteral("mathtt") || cmd == QStringLiteral("mathcal") || cmd == QStringLiteral("mathbb") || cmd == QStringLiteral("mathfrak") || cmd == QStringLiteral("mathscr") || cmd == QStringLiteral("operatorname")) {
                    QString arg;
                    const int after = readMathArg(expr, pos, &arg);
                    if (after > 0) {
                        const QString style = cmd == QStringLiteral("text") ? QStringLiteral("font-style:normal;")
                            : cmd == QStringLiteral("mathbf") || cmd == QStringLiteral("boldsymbol") ? QStringLiteral("font-weight:bold;")
                            : cmd == QStringLiteral("mathit") ? QStringLiteral("font-style:italic;")
                            : cmd == QStringLiteral("mathsf") ? QStringLiteral("font-family:sans-serif;")
                            : cmd == QStringLiteral("mathtt") ? QStringLiteral("font-family:monospace;")
                            : cmd == QStringLiteral("mathcal") || cmd == QStringLiteral("mathscr") ? QStringLiteral("font-family:cursive;")
                            : QStringLiteral("font-family:serif;");
                        html += QStringLiteral("<span style=\"%1\">%2</span>").arg(style, renderMathInline(arg));
                        i = after;
                        continue;
                    }
                } else if (cmd == QStringLiteral("binom")) {
                    QString top, bottom;
                    const int afterTop = readMathArg(expr, pos, &top);
                    const int afterBottom = afterTop > 0 ? readMathArg(expr, afterTop, &bottom) : -1;
                    if (afterBottom > 0) {
                        if (displayMode) {
                            html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\">"
                                                   "<tr><td rowspan=\"2\" style=\"font-size:1.8em;vertical-align:middle;padding-right:3px;\">(</td>"
                                                   "<td style=\"border:0;padding:0;text-align:center;vertical-align:bottom;\">%1</td>"
                                                   "<td rowspan=\"2\" style=\"font-size:1.8em;vertical-align:middle;padding-left:3px;\">)</td></tr>"
                                                   "<tr><td style=\"border:0;padding:0;text-align:center;vertical-align:top;\">%2</td></tr>"
                                                   "</table>")
                                        .arg(renderMathInline(top, false), renderMathInline(bottom, false));
                        } else {
                            html += QStringLiteral("(%1) / (%2)").arg(renderMathInline(top, false), renderMathInline(bottom, false));
                        }
                        i = afterBottom;
                        continue;
                    }
                } else if (cmd == QStringLiteral("overline") || cmd == QStringLiteral("bar") || cmd == QStringLiteral("underline") || cmd == QStringLiteral("hat") || cmd == QStringLiteral("widehat") || cmd == QStringLiteral("tilde") || cmd == QStringLiteral("widetilde") || cmd == QStringLiteral("dot") || cmd == QStringLiteral("ddot") || cmd == QStringLiteral("breve") || cmd == QStringLiteral("check") || cmd == QStringLiteral("acute") || cmd == QStringLiteral("grave") || cmd == QStringLiteral("vec") || cmd == QStringLiteral("overrightarrow") || cmd == QStringLiteral("overleftarrow") || cmd == QStringLiteral("overbrace") || cmd == QStringLiteral("underbrace")) {
                    QString arg;
                    const int after = readMathArg(expr, pos, &arg);
                    if (after > 0) {
                        if (cmd == QStringLiteral("underline") || cmd == QStringLiteral("underbrace")) {
                            html += QStringLiteral("<span style=\"text-decoration:underline;\">%1</span>").arg(renderMathInline(arg, displayMode));
                        } else if (cmd == QStringLiteral("overline") || cmd == QStringLiteral("bar") || cmd == QStringLiteral("overbrace")) {
                            html += QStringLiteral("<span style=\"text-decoration:overline;\">%1</span>").arg(renderMathInline(arg, displayMode));
                        } else if (cmd == QStringLiteral("overrightarrow") || cmd == QStringLiteral("overleftarrow") || cmd == QStringLiteral("vec")) {
                            if (displayMode) {
                                QString arrow = (cmd == QStringLiteral("overleftarrow")) ? QStringLiteral("&larr;") : QStringLiteral("&rarr;");
                                html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"display:inline-table;width:auto;margin:0;border-collapse:collapse;vertical-align:middle;\">"
                                                       "<tr><td style=\"border:0;padding:0;text-align:center;font-size:0.8em;line-height:0.5;vertical-align:bottom;\">%1</td></tr>"
                                                       "<tr><td style=\"border:0;padding:0;text-align:center;line-height:0.8;vertical-align:top;\">%2</td></tr>"
                                                       "</table>").arg(arrow, renderMathInline(arg, false));
                            } else {
                                if (cmd == QStringLiteral("vec")) {
                                    html += QStringLiteral("<span style=\"white-space:nowrap;\">%1&#x20D7;</span>").arg(renderMathInline(arg, false));
                                } else {
                                    html += QStringLiteral("<span style=\"text-decoration:overline;\">%1</span>").arg(renderMathInline(arg, false));
                                }
                            }
                        } else {
                            QString combining;
                            if (cmd == QStringLiteral("hat") || cmd == QStringLiteral("widehat")) combining = QStringLiteral("&#x0302;");
                            else if (cmd == QStringLiteral("tilde") || cmd == QStringLiteral("widetilde")) combining = QStringLiteral("&#x0303;");
                            else if (cmd == QStringLiteral("dot")) combining = QStringLiteral("&#x0307;");
                            else if (cmd == QStringLiteral("ddot")) combining = QStringLiteral("&#x0308;");
                            else combining = QStringLiteral("&#x0304;"); // macron for anything else like breve, check
                            html += QStringLiteral("<span style=\"white-space:nowrap;\">%1%2</span>").arg(renderMathInline(arg, displayMode), combining);
                        }
                        i = after;
                        continue;
                    }
                } else if (cmd == QStringLiteral("left") || cmd == QStringLiteral("right") || cmd == QStringLiteral("limits") || cmd == QStringLiteral("nolimits")) {
                    i = pos;
                    continue;
                } else if (cmd == QStringLiteral("quad")) {
                    html += QStringLiteral("&emsp;");
                    i = pos;
                    continue;
                } else if (cmd == QStringLiteral("qquad")) {
                    html += QStringLiteral("&emsp;&emsp;");
                    i = pos;
                    continue;
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
                        {QStringLiteral("varepsilon"), QStringLiteral("ϵ")}, {QStringLiteral("vartheta"), QStringLiteral("ϑ")}, {QStringLiteral("varpi"), QStringLiteral("ϖ")}, {QStringLiteral("varrho"), QStringLiteral("ϱ")}, {QStringLiteral("varsigma"), QStringLiteral("ς")}, {QStringLiteral("varphi"), QStringLiteral("ϕ")},
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
                        {QStringLiteral("circ"), QStringLiteral("&ring;")}, {QStringLiteral("angle"), QStringLiteral("&ang;")},
                        {QStringLiteral("simeq"), QStringLiteral("≃")}, {QStringLiteral("propto"), QStringLiteral("&prop;")}, {QStringLiteral("doteq"), QStringLiteral("≐")}, {QStringLiteral("ll"), QStringLiteral("≪")}, {QStringLiteral("gg"), QStringLiteral("≫")},
                        {QStringLiteral("prod"), QStringLiteral("&prod;")}, {QStringLiteral("coprod"), QStringLiteral("∐")}, {QStringLiteral("iint"), QStringLiteral("∬")}, {QStringLiteral("iiint"), QStringLiteral("∭")}, {QStringLiteral("oint"), QStringLiteral("∮")},
                        {QStringLiteral("langle"), QStringLiteral("&lang;")}, {QStringLiteral("rangle"), QStringLiteral("&rang;")}, {QStringLiteral("lfloor"), QStringLiteral("&lfloor;")}, {QStringLiteral("rfloor"), QStringLiteral("&rfloor;")}, {QStringLiteral("lceil"), QStringLiteral("&lceil;")}, {QStringLiteral("rceil"), QStringLiteral("&rceil;")}, {QStringLiteral("lvert"), QStringLiteral("|")}, {QStringLiteral("rvert"), QStringLiteral("|")}, {QStringLiteral("lVert"), QStringLiteral("‖")}, {QStringLiteral("rVert"), QStringLiteral("‖")},
                        {QStringLiteral("to"), QStringLiteral("&rarr;")}, {QStringLiteral("leftrightarrow"), QStringLiteral("&harr;")}, {QStringLiteral("Leftrightarrow"), QStringLiteral("&hArr;")}, {QStringLiteral("mapsto"), QStringLiteral("↦")}, {QStringLiteral("longrightarrow"), QStringLiteral("⟶")}, {QStringLiteral("longleftarrow"), QStringLiteral("⟵")}, {QStringLiteral("Longrightarrow"), QStringLiteral("⟹")}, {QStringLiteral("Longleftrightarrow"), QStringLiteral("⟺")}, {QStringLiteral("uparrow"), QStringLiteral("↑")}, {QStringLiteral("downarrow"), QStringLiteral("↓")}, {QStringLiteral("updownarrow"), QStringLiteral("↕")},
                        {QStringLiteral("forall"), QStringLiteral("&forall;")}, {QStringLiteral("exists"), QStringLiteral("&exist;")}, {QStringLiteral("nexists"), QStringLiteral("∄")}, {QStringLiteral("neg"), QStringLiteral("&not;")}, {QStringLiteral("land"), QStringLiteral("&and;")}, {QStringLiteral("lor"), QStringLiteral("&or;")}, {QStringLiteral("implies"), QStringLiteral("⟹")}, {QStringLiteral("iff"), QStringLiteral("⟺")}, {QStringLiteral("therefore"), QStringLiteral("∴")}, {QStringLiteral("because"), QStringLiteral("∵")},
                        {QStringLiteral("ni"), QStringLiteral("&ni;")}, {QStringLiteral("subseteq"), QStringLiteral("&sube;")}, {QStringLiteral("subsetneq"), QStringLiteral("⊊")}, {QStringLiteral("supset"), QStringLiteral("&sup;")}, {QStringLiteral("supseteq"), QStringLiteral("&supe;")}, {QStringLiteral("supsetneq"), QStringLiteral("⊋")}, {QStringLiteral("cup"), QStringLiteral("&cup;")}, {QStringLiteral("cap"), QStringLiteral("&cap;")}, {QStringLiteral("setminus"), QStringLiteral("∖")}, {QStringLiteral("emptyset"), QStringLiteral("&empty;")}, {QStringLiteral("varnothing"), QStringLiteral("∅")},
                        {QStringLiteral("parallel"), QStringLiteral("∥")}, {QStringLiteral("nparallel"), QStringLiteral("∦")}, {QStringLiteral("perp"), QStringLiteral("⊥")}, {QStringLiteral("triangle"), QStringLiteral("△")}, {QStringLiteral("triangleleft"), QStringLiteral("◃")}, {QStringLiteral("triangleright"), QStringLiteral("▹")}, {QStringLiteral("measuredangle"), QStringLiteral("∡")},
                        {QStringLiteral("ldots"), QStringLiteral("…")}, {QStringLiteral("cdots"), QStringLiteral("⋯")}, {QStringLiteral("vdots"), QStringLiteral("⋮")}, {QStringLiteral("ddots"), QStringLiteral("⋱")}, {QStringLiteral("aleph"), QStringLiteral("ℵ")}, {QStringLiteral("hbar"), QStringLiteral("ℏ")}, {QStringLiteral("Re"), QStringLiteral("ℜ")}, {QStringLiteral("Im"), QStringLiteral("ℑ")}, {QStringLiteral("Box"), QStringLiteral("□")}, {QStringLiteral("blacksquare"), QStringLiteral("■")},
                        {QStringLiteral("cdot"), QStringLiteral("&thinsp;&sdot;&thinsp;")}, {QStringLiteral("times"), QStringLiteral("&thinsp;&times;&thinsp;")}, {QStringLiteral("div"), QStringLiteral("&thinsp;&divide;&thinsp;")},
                        {QStringLiteral("pm"), QStringLiteral("&thinsp;&plusmn;&thinsp;")}, {QStringLiteral("mp"), QStringLiteral("&thinsp;∓&thinsp;")}, {QStringLiteral("oplus"), QStringLiteral("&thinsp;&oplus;&thinsp;")}, {QStringLiteral("ominus"), QStringLiteral("&thinsp;⊖&thinsp;")}, {QStringLiteral("otimes"), QStringLiteral("&thinsp;&otimes;&thinsp;")}, {QStringLiteral("oslash"), QStringLiteral("&thinsp;⊘&thinsp;")}, {QStringLiteral("odot"), QStringLiteral("&thinsp;⊙&thinsp;")}, {QStringLiteral("pmod"), QStringLiteral(" mod ")}, {QStringLiteral("bmod"), QStringLiteral(" mod ")}, {QStringLiteral("max"), QStringLiteral("max")}, {QStringLiteral("min"), QStringLiteral("min")}, {QStringLiteral("sup"), QStringLiteral("sup")}, {QStringLiteral("inf"), QStringLiteral("inf")}, {QStringLiteral("det"), QStringLiteral("det")}, {QStringLiteral("Pr"), QStringLiteral("Pr")}
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
                const int start = pos;
                if (pos < expr.size() && expr.at(pos) == '\\') {
                    ++pos;
                    while (pos < expr.size() && expr.at(pos).isLetter())
                        ++pos;
                } else {
                    ++pos;
                }
                token = expr.mid(start, pos - start);
            }
            if (!token.isEmpty()) {
                html += QStringLiteral("<span style=\"font-size:1px;\">&nbsp;</span>");
                html += superscript
                    ? QStringLiteral("<sup>%1</sup>").arg(renderMathInline(token, displayMode))
                    : QStringLiteral("<sub>%1</sub>").arg(renderMathInline(token, displayMode));
                i = pos;
                continue;
            }
        }

        // Bare grouping braces (e.g. the "{,}" decimal-comma idiom) carry no
        // markup of their own — render the contents and drop the braces.
        if (ch == '{') {
            QString inner;
            int after = consumeBalanced(expr, i, '{', '}', &inner);
            if (after > 0) {
                html += renderMathInline(inner);
                i = after;
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
    QString html = renderMathInline(expr.trimmed(), true);
    
    // QTextDocument breaks lines when a table is placed next to inline text.
    // To prevent this in display math, we wrap everything in a master single-row table.
    if (html.contains(QStringLiteral("<table"))) {
        QString masterTable = QStringLiteral("<table style=\"margin:0;border-collapse:collapse;vertical-align:middle;\"><tr>");
        int pos = 0;
        while (pos < html.size()) {
            int tableStart = html.indexOf(QStringLiteral("<table"), pos);
            if (tableStart == -1) {
                QString text = html.mid(pos);
                if (!text.isEmpty())
                    masterTable += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(text);
                break;
            }
            if (tableStart > pos) {
                QString text = html.mid(pos, tableStart - pos);
                masterTable += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(text);
            }
            int tableEnd = tableStart + 6;
            int depth = 1;
            while (tableEnd < html.size()) {
                int nextStart = html.indexOf(QStringLiteral("<table"), tableEnd);
                int nextEnd = html.indexOf(QStringLiteral("</table>"), tableEnd);
                if (nextEnd == -1) {
                    tableEnd = html.size();
                    break;
                }
                if (nextStart != -1 && nextStart < nextEnd) {
                    depth++;
                    tableEnd = nextStart + 6;
                } else {
                    depth--;
                    tableEnd = nextEnd + 8;
                    if (depth == 0) break;
                }
            }
            
            QString tableHtml = html.mid(tableStart, tableEnd - tableStart);
            masterTable += QStringLiteral("<td style=\"border:0;padding:0;vertical-align:middle;\">%1</td>").arg(tableHtml);
            pos = tableEnd;
        }
        masterTable += QStringLiteral("</tr></table>");
        html = masterTable;
    }

    // Centering (CSS text-align:center, the align="center" attribute, or a
    // centered <td>) makes QTextBrowser's HTML engine render the whole block
    // blank, so the math body is left-aligned with an indent instead.
    return QStringLiteral("<div style=\"margin-top:16px;margin-bottom:16px;margin-left:24px;\">"
                           "%1</div>").arg(html);
}

// ChatGPT's Markdown copy spelling uses doubled bracket delimiters and escapes
// braced subscripts as \_{...}. Unlike Markdown-first browser renderers, we
// protect math before MD4C unescapes it. Decode only this script spelling, only
// in that delimiter dialect. TeX escapes, row separators and text arguments are
// otherwise opaque; in particular \text{file\_name} must keep its underscore.
static QString normalizeEscapedMathScripts(const QString& expr)
{
    if (!expr.contains(QStringLiteral("\\_")))
        return expr;
    QString out;
    out.reserve(expr.size());
    for (int i = 0; i < expr.size();) {
        if (expr.at(i) != QLatin1Char('\\')) {
            out += expr.at(i++);
            continue;
        }
        int end = i + 1;
        while (end < expr.size() && expr.at(end) == QLatin1Char('\\'))
            ++end;
        if (end - i != 1) {
            out += expr.mid(i, end - i);
            i = end;
            continue;
        }
        if (end < expr.size() && expr.at(end) == QLatin1Char('_')) {
            int argument = end + 1;
            while (argument < expr.size() && expr.at(argument).isSpace())
                ++argument;
            if (argument < expr.size() && expr.at(argument) == QLatin1Char('{')) {
                i = end; // Copy the script, dropping only its Markdown escape.
                continue;
            }
        }
        int commandEnd = end;
        while (commandEnd < expr.size() && expr.at(commandEnd).isLetter())
            ++commandEnd;
        const QStringView command = QStringView(expr).mid(end, commandEnd - end);
        const bool textArgument = command == u"text" || command == u"textrm"
            || command == u"textsf" || command == u"texttt" || command == u"textnormal"
            || command == u"textbf" || command == u"textit" || command == u"textmd"
            || command == u"textup" || command == u"textsl" || command == u"textsc"
            || command == u"mbox" || command == u"operatorname" || command == u"url";
        int argument = commandEnd;
        if (argument < expr.size() && expr.at(argument) == QLatin1Char('*'))
            ++argument;
        while (argument < expr.size() && expr.at(argument).isSpace())
            ++argument;
        if (textArgument && argument < expr.size() && expr.at(argument) == QLatin1Char('{')) {
            int depth = 1;
            end = argument + 1;
            while (end < expr.size() && depth > 0) {
                const QChar ch = expr.at(end++);
                if (ch == QLatin1Char('\\') && end < expr.size())
                    ++end; // Escaped braces and paired backslashes aren't groups.
                else if (ch == QLatin1Char('{'))
                    ++depth;
                else if (ch == QLatin1Char('}'))
                    --depth;
            }
        } else {
            end = qMax(end + 1, commandEnd);
            end = qMin(end, int(expr.size()));
        }
        out += expr.mid(i, end - i);
        i = end;
    }
    return out;
}

// Build the protected snippet in the requested renderer's representation,
// after decoding the copied subscript spelling when that dialect was selected.
static QString makeMathSnippet(MathRenderMode mode, bool block, const QString& expression,
                               const QString& openDelim, const QString& closeDelim,
                               bool markdownEscaped = false)
{
    const QString expr = markdownEscaped ? normalizeEscapedMathScripts(expression) : expression;
    if (mode == MathRenderMode::KatexOutput) {
        // A bare '$' never delimits math in the rendered page: this scanner has
        // already separated math from currency, and KaTeX auto-render would
        // pair the literal dollars again.
        if (openDelim == QLatin1String("$"))
            return (QLatin1String("\\(") + expr + QLatin1String("\\)")).toHtmlEscaped();
        return (openDelim + expr + closeDelim).toHtmlEscaped();
    }
    return block ? renderMathBlock(expr) : renderMathInline(expr, false);
}

static int blockquoteContentStart(const QString& text, int lineStart)
{
    int pos = lineStart;
    while (pos < text.size() && (text.at(pos) == QLatin1Char(' ') || text.at(pos) == QLatin1Char('\t')))
        ++pos;
    bool quoted = false;
    while (pos < text.size() && text.at(pos) == QLatin1Char('>')) {
        quoted = true;
        ++pos;
        while (pos < text.size() && (text.at(pos) == QLatin1Char(' ') || text.at(pos) == QLatin1Char('\t')))
            ++pos;
    }
    return quoted ? pos : -1;
}

static QStringView quotedLineContent(QStringView line, int* quoteDepth)
{
    int depth = 0;
    qsizetype pos = 0;
    while (pos < line.size() && (line.at(pos).isSpace() || line.at(pos) == QLatin1Char('>'))) {
        if (line.at(pos) == QLatin1Char('>'))
            ++depth;
        ++pos;
    }
    if (quoteDepth)
        *quoteDepth = depth;
    return line.mid(pos).trimmed();
}

static QStringView lineAt(const QString& text, qsizetype start)
{
    const qsizetype end = text.indexOf(QLatin1Char('\n'), start);
    return QStringView(text).mid(start, end < 0 ? -1 : end - start);
}

static bool isTableDelimiterRow(QStringView content)
{
    bool pipe = false;
    bool dash = false;
    for (const QChar ch : content) {
        if (ch == QLatin1Char('|'))
            pipe = true;
        else if (ch == QLatin1Char('-'))
            dash = true;
        else if (ch != QLatin1Char(':') && !ch.isSpace())
            return false;
    }
    return pipe && dash;
}

static bool isAtxHeading(QStringView content)
{
    qsizetype hashes = 0;
    while (hashes < content.size() && content.at(hashes) == QLatin1Char('#'))
        ++hashes;
    return hashes >= 1 && hashes <= 6
        && (hashes == content.size() || content.at(hashes).isSpace());
}

// Whether a line (quote markers removed) cannot continue the paragraph above.
static bool endsParagraph(QStringView content)
{
    if (content.isEmpty() || isAtxHeading(content) || isTableDelimiterRow(content))
        return true;
    const QChar first = content.at(0);
    const QChar second = content.size() > 1 ? content.at(1) : QChar(QLatin1Char(' '));
    if (first == QLatin1Char('|'))
        return true;
    if (first == QLatin1Char('<'))
        return second.isLetter() || second == QLatin1Char('/') || second == QLatin1Char('!')
            || second == QLatin1Char('?');
    if (first == QLatin1Char(':'))
        return second.isSpace();
    if (first == QLatin1Char('[') && second == QLatin1Char('^'))
        return content.contains(QLatin1String("]:"));
    if ((first == QLatin1Char('-') || first == QLatin1Char('+') || first == QLatin1Char('*'))
        && second.isSpace())
        return true;
    if (content.startsWith(QLatin1String("```")) || content.startsWith(QLatin1String("~~~")))
        return true;
    if (first == QLatin1Char('-') || first == QLatin1Char('=') || first == QLatin1Char('*')
        || first == QLatin1Char('_')) {
        // Setext underline or thematic break.
        for (const QChar ch : content) {
            if (ch != first && !ch.isSpace())
                return false;
        }
        return first == QLatin1Char('-') || first == QLatin1Char('=') || content.count(first) >= 3;
    }
    qsizetype digits = 0;
    while (digits < content.size() && digits < 10 && content.at(digits) >= QLatin1Char('0')
           && content.at(digits) <= QLatin1Char('9'))
        ++digits;
    return digits >= 1 && digits <= 9 && digits < content.size()
        && (content.at(digits) == QLatin1Char('.') || content.at(digits) == QLatin1Char(')'))
        && (digits + 1 == content.size() || content.at(digits + 1).isSpace());
}

// End of the block holding `pos`: inline math must close before it.
static qsizetype inlineBlockEnd(const QString& text, qsizetype pos, bool inTable)
{
    qsizetype lineStart = pos > 0 ? text.lastIndexOf(QLatin1Char('\n'), pos - 1) + 1 : 0;
    int depth = 0;
    const bool singleLine = isAtxHeading(quotedLineContent(lineAt(text, lineStart), &depth)) || inTable;
    for (;;) {
        const qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            return text.size();
        if (singleLine)
            return lineEnd;
        int nextDepth = 0;
        const QStringView next = quotedLineContent(lineAt(text, lineEnd + 1), &nextDepth);
        if (nextDepth > depth || endsParagraph(next))
            return lineEnd;
        depth = nextDepth;
        lineStart = lineEnd + 1;
    }
}

QString preprocess(const QString& markdown, MathRenderMode mode, QStringList* mathSnippets,
                    QVector<MathLineShift>* lineShifts, QStringList* literalSnippets)
{
    QString out;
    out.reserve(markdown.size());
    int outLine = 1;

    bool inFence = false;
    QChar fenceChar;
    int fenceLength = 0;
    int fenceQuoteDepth = 0;
    int inlineCodeTicks = 0;
    bool inTable = false;
    qsizetype blockEnd = 0;

    for (int i = 0; i < markdown.size();) {
        bool fenceLine = false;
        if (!inlineCodeTicks && (i == 0 || markdown.at(i - 1) == '\n')) {
            int lineEnd = markdown.indexOf('\n', i);
            if (lineEnd < 0) lineEnd = markdown.size();
            int j = i;
            const int quotedStart = blockquoteContentStart(markdown, i);
            const int quoteDepth = quotedStart < 0 ? 0
                : markdown.mid(i, quotedStart - i).count(QLatin1Char('>'));
            if (quotedStart >= 0)
                j = quotedStart;
            while (j < lineEnd && markdown.at(j).isSpace())
                ++j;
            int fenceCount = 0;
            if (j < lineEnd && (markdown.at(j) == '`' || markdown.at(j) == '~')) {
                while (j + fenceCount < lineEnd && markdown.at(j + fenceCount) == markdown.at(j))
                    ++fenceCount;
            }
            if (fenceCount >= 3 && (!inFence || quoteDepth == fenceQuoteDepth)) {
                if (!inFence) {
                    inFence = true;
                    fenceChar = markdown.at(j);
                    fenceLength = fenceCount;
                    fenceQuoteDepth = quoteDepth;
                    fenceLine = true;
                } else if (fenceChar == markdown.at(j) && fenceCount >= fenceLength) {
                    inFence = false;
                    fenceLength = 0;
                    fenceLine = true;
                }
            }
        }

        if (i == 0 || markdown.at(i - 1) == QLatin1Char('\n')) {
            const QStringView content = quotedLineContent(lineAt(markdown, i), nullptr);
            if (content.isEmpty() || inFence)
                inTable = false;
            else if (isTableDelimiterRow(content))
                inTable = true;
        }

        const QChar ch = markdown.at(i);
        // A closing fence transitions out of the protected region before this
        // character is copied. Keep the entire line protected too: consuming
        // the remaining backticks as inline-code delimiters strands that state
        // and suppresses all following math.
        if (inFence || fenceLine) {
            int lineEnd = markdown.indexOf(QLatin1Char('\n'), i);
            if (lineEnd < 0)
                lineEnd = markdown.size();
            else {
                ++lineEnd;
                ++outLine;
            }
            out += markdown.mid(i, lineEnd - i);
            i = lineEnd;
            continue;
        }

        if (ch == '`') {
            int run = 1;
            while (i + run < markdown.size() && markdown.at(i + run) == QLatin1Char('`'))
                ++run;
            if (!inlineCodeTicks)
                inlineCodeTicks = run;
            else if (inlineCodeTicks == run)
                inlineCodeTicks = 0;
            out += markdown.mid(i, run);
            i += run;
            continue;
        }

        // A display block inside a blockquote carries the quote marker on every
        // source line (`> $$`, `> expression`, `> $$`). Strip those markers from
        // the expression before handing it to the math renderer, while keeping
        // the first marker in the Markdown placeholder so MD4C keeps the math
        // inside the original blockquote/callout.
        if (!inlineCodeTicks && (i == 0 || markdown.at(i - 1) == QLatin1Char('\n'))) {
            const int contentStart = blockquoteContentStart(markdown, i);
            const bool dollarBlock = contentStart >= 0 && contentStart + 1 < markdown.size()
                && markdown.at(contentStart) == QLatin1Char('$')
                && markdown.at(contentStart + 1) == QLatin1Char('$');
            const bool bracketBlock = contentStart >= 0 && contentStart + 1 < markdown.size()
                && markdown.at(contentStart) == QLatin1Char('\\')
                && markdown.at(contentStart + 1) == QLatin1Char('[');
            const bool escapedBracketBlock = contentStart >= 0 && contentStart + 2 < markdown.size()
                && markdown.at(contentStart) == QLatin1Char('\\')
                && markdown.at(contentStart + 1) == QLatin1Char('\\')
                && markdown.at(contentStart + 2) == QLatin1Char('[');
            if (dollarBlock || bracketBlock || escapedBracketBlock) {
                const int openingLength = escapedBracketBlock ? 3 : 2;
                const QString closingDelimiter = dollarBlock ? QStringLiteral("$$")
                    : escapedBracketBlock ? QStringLiteral("\\\\]") : QStringLiteral("\\]");
                int openingEnd = markdown.indexOf(QLatin1Char('\n'), contentStart);
                if (openingEnd < 0) openingEnd = markdown.size();
                if (markdown.mid(contentStart + openingLength,
                                 openingEnd - contentStart - openingLength).trimmed().isEmpty()) {
                    QStringList expressionLines;
                    int nextLine = openingEnd < markdown.size() ? openingEnd + 1 : openingEnd;
                    int closeStart = -1;
                    int closeEnd = -1;
                    while (nextLine < markdown.size()) {
                        int lineEnd = markdown.indexOf(QLatin1Char('\n'), nextLine);
                        if (lineEnd < 0) lineEnd = markdown.size();
                        const int nestedContentStart = blockquoteContentStart(markdown, nextLine);
                        if (nestedContentStart < 0)
                            break;
                        const QString content = markdown.mid(nestedContentStart, lineEnd - nestedContentStart);
                        if (content.trimmed() == closingDelimiter) {
                            closeStart = nestedContentStart;
                            closeEnd = nestedContentStart + content.size();
                            break;
                        }
                        expressionLines.append(content);
                        nextLine = lineEnd < markdown.size() ? lineEnd + 1 : lineEnd;
                    }
                    if (closeStart >= 0) {
                        const QString expr = expressionLines.join(QLatin1Char('\n'));
                        const QString placeholder = QStringLiteral("@@FASTMD_MATH_%1@@").arg(mathSnippets->size());
                        mathSnippets->append(makeMathSnippet(mode, true, expr,
                                                              dollarBlock ? QStringLiteral("$$") : QStringLiteral("\\["),
                                                              dollarBlock ? QStringLiteral("$$") : QStringLiteral("\\]"),
                                                              escapedBracketBlock));
                        if (literalSnippets)
                            literalSnippets->append(markdown.mid(contentStart, closeEnd - contentStart));
                        if (lineShifts) {
                            const int removed = markdown.mid(i, closeEnd - i).count(QLatin1Char('\n'));
                            if (removed > 0)
                                lineShifts->append({ outLine, removed });
                        }
                        out += markdown.mid(i, contentStart - i) + placeholder;
                        i = closeEnd;
                        continue;
                    }
                }
            }
        }

        if (!inlineCodeTicks && ch == '$' && (i == 0 || markdown.at(i - 1) != '\\')) {
            const bool block = (i + 1 < markdown.size() && markdown.at(i + 1) == '$');
            const int start = i + (block ? 2 : 1);
            int end = -1;
            if (!block) {
                if (i >= blockEnd)
                    blockEnd = inlineBlockEnd(markdown, i, inTable);
                end = int(findInlineDollarClose(markdown, i, blockEnd));
            } else {
                bool codeSpan = false;
                for (int j = start; j < markdown.size(); ++j) {
                    if (markdown.at(j) == QLatin1Char('`')) {
                        codeSpan = !codeSpan;
                        continue;
                    }
                    if (codeSpan || markdown.at(j) != '$' || markdown.at(j - 1) == '\\')
                        continue;
                    if (j + 1 < markdown.size() && markdown.at(j + 1) == '$') {
                        end = j;
                        break;
                    }
                }
            }
            if (end > start) {
                const int matchEnd = end + (block ? 2 : 1);
                const QString expr = markdown.mid(start, end - start);
                const QString placeholder = QStringLiteral("@@FASTMD_MATH_%1@@").arg(mathSnippets->size());
                const QString delim = block ? QStringLiteral("$$") : QStringLiteral("$");
                mathSnippets->append(makeMathSnippet(mode, block, expr, delim, delim));
                if (literalSnippets)
                    literalSnippets->append(markdown.mid(i, matchEnd - i));
                if (lineShifts) {
                    const int removed = markdown.mid(i, matchEnd - i).count(QLatin1Char('\n'));
                    if (removed > 0)
                        lineShifts->append({ outLine, removed });
                }
                out += placeholder;
                i = matchEnd;
                continue;
            }
            // Neither half of an unmatched "$$" may open inline math.
            out += markdown.mid(i, start - i);
            i = start;
            continue;
        }

        // LaTeX bracket delimiters: \(...\) (inline) and \[...\] (display).
        // A backslash run is read whole: Markdown pairs backslashes into
        // escaped literals, so an odd run ends in a real \( or \[, while an
        // even run ending in \\( or \\[ is the Markdown-escaped spelling of
        // that delimiter (written so Markdown emits \[ for MathJax/KaTeX),
        // closed by \\) or \\]. Starting a match on the second backslash of a
        // pair would leave its partner in front of the placeholder, and MD4C
        // would then read "\@" as an escaped '@' and split the token apart.
        if (!inlineCodeTicks && ch == '\\') {
            int run = 1;
            while (i + run < markdown.size() && markdown.at(i + run) == QLatin1Char('\\'))
                ++run;
            const QChar next = i + run < markdown.size() ? markdown.at(i + run) : QChar();
            if (next == '(' || next == '[') {
                const bool block = (next == '[');
                const int delimSlashes = (run % 2) ? 1 : 2;
                const QString closeDelim = QString(delimSlashes, QLatin1Char('\\'))
                    + (block ? QLatin1Char(']') : QLatin1Char(')'));
                const int start = i + run + 1;
                const int end = markdown.indexOf(closeDelim, start);
                if (end >= start) {
                    const int matchEnd = end + closeDelim.size();
                    const QString expr = markdown.mid(start, end - start);
                    const QString placeholder = QStringLiteral("@@FASTMD_MATH_%1@@").arg(mathSnippets->size());
                    mathSnippets->append(makeMathSnippet(mode, block, expr,
                                                          block ? QStringLiteral("\\[") : QStringLiteral("\\("),
                                                          block ? QStringLiteral("\\]") : QStringLiteral("\\)"),
                                                          run == 2));
                    if (literalSnippets)
                        literalSnippets->append(markdown.mid(i + run - delimSlashes,
                                                            matchEnd - i - run + delimSlashes));
                    if (lineShifts) {
                        const int removed = markdown.mid(i, matchEnd - i).count(QLatin1Char('\n'));
                        if (removed > 0)
                            lineShifts->append({ outLine, removed });
                    }
                    out += QString(run - delimSlashes, QLatin1Char('\\'));
                    out += placeholder;
                    i = matchEnd;
                    continue;
                }
            }
            out += QString(run, QLatin1Char('\\'));
            i += run;
            continue;
        }

        out += ch;
        if (ch == '\n') ++outLine;
        ++i;
    }

    return out;
}


} // namespace FastMdMath
