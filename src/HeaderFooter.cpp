#include "HeaderFooter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>

namespace {

enum class Piece { Text, File, Title, Date, Time, Page, Pages };

struct Part {
    Piece kind;
    QString text;
};

Piece tokenKind(QStringView name)
{
    static constexpr struct { const char* name; Piece kind; } table[] = {
        { "file", Piece::File }, { "title", Piece::Title }, { "date", Piece::Date },
        { "time", Piece::Time }, { "page", Piece::Page }, { "pages", Piece::Pages },
    };
    for (const auto& entry : table) {
        if (name.compare(QLatin1StringView(entry.name), Qt::CaseInsensitive) == 0)
            return entry.kind;
    }
    return Piece::Text;
}

QList<Part> parse(const QString& text)
{
    QList<Part> parts;
    QString literal;
    const auto flush = [&] {
        if (!literal.isEmpty()) {
            parts.append({ Piece::Text, literal });
            literal.clear();
        }
    };
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n;) {
        const QChar ch = text.at(i);
        if (ch == u'{') {
            if (i + 1 < n && text.at(i + 1) == u'{') {
                literal += u'{';
                i += 2;
                continue;
            }
            const qsizetype close = text.indexOf(u'}', i + 1);
            if (close > i) {
                const Piece kind = tokenKind(QStringView(text).mid(i + 1, close - i - 1));
                if (kind != Piece::Text) {
                    flush();
                    parts.append({ kind, {} });
                    i = close + 1;
                    continue;
                }
            }
        } else if (ch == u'}' && i + 1 < n && text.at(i + 1) == u'}') {
            literal += u'}';
            i += 2;
            continue;
        }
        literal += ch;
        ++i;
    }
    flush();
    return parts;
}

QString contextText(Piece kind, const HeaderFooterContext& context)
{
    const QLocale locale = QLocale::system();
    switch (kind) {
    case Piece::File:  return context.fileName;
    case Piece::Title: return context.title;
    case Piece::Date:  return locale.toString(context.dateTime.date(), QLocale::ShortFormat);
    case Piece::Time:  return locale.toString(context.dateTime.time(), QLocale::ShortFormat);
    default:           return {};
    }
}

// A CSS string literal. Quotes and backslashes are hex-escaped, and "<" too so
// that no slot text can close the surrounding <style> element. Control and
// line-separator characters become spaces: a margin box is a single run.
QString cssString(const QString& text)
{
    QString out;
    out.reserve(text.size() + 2);
    out += u'"';
    for (const QChar ch : text) {
        const char16_t c = ch.unicode();
        if (c < 0x20 || c == 0x7F || c == 0x2028 || c == 0x2029)
            out += u' ';
        else if (c == u'\\')
            out += QLatin1StringView("\\\\");
        else if (c == u'"')
            out += QLatin1StringView("\\22 ");
        else if (c == u'<')
            out += QLatin1StringView("\\3C ");
        else
            out += ch;
    }
    out += u'"';
    return out;
}

bool hasContent(const QString& slotTemplate)
{
    return !slotTemplate.trimmed().isEmpty();
}

void appendBand(QString& css, bool enabled, const QString (&cells)[HeaderFooterOptions::SlotCount],
                bool top, const HeaderFooterContext& context)
{
    static constexpr const char* positions[] = { "left", "center", "right" };
    const QLatin1StringView edge = top ? QLatin1StringView("top") : QLatin1StringView("bottom");
    int emitted = 0;
    if (enabled) {
        for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
            if (!hasContent(cells[slot]))
                continue;
            // Equal thirds keep the centre box centred on the page; without a
            // width Chromium hands most of the band to the longest box.
            css += QLatin1StringView("@") + edge + u'-' + QLatin1StringView(positions[slot])
                 + QLatin1StringView("{content:") + HeaderFooter::cssContent(cells[slot], context)
                 + QLatin1StringView(";width:33.3%;text-align:") + QLatin1StringView(positions[slot])
                 // Centred in the margin, as word processors place them: clear
                 // of the body without crowding the paper edge.
                 + QLatin1StringView(";vertical-align:middle;font:9pt/1.25 'Segoe UI',sans-serif;color:#57606a;}");
            ++emitted;
        }
    }
    // Chromium drops its own date/title/URL header or footer for any band that
    // holds a margin box, even an empty one. Claiming the unused band keeps
    // Print from mixing FastMD's text with the browser's.
    if (emitted == 0)
        css += QLatin1StringView("@") + edge + QLatin1StringView("-center{content:\"\";}");
}

HeaderFooterOptions makeOptions(bool headerOn, std::initializer_list<const char*> header,
                                bool footerOn, std::initializer_list<const char*> footer)
{
    HeaderFooterOptions options;
    options.headerEnabled = headerOn;
    options.footerEnabled = footerOn;
    int slot = 0;
    for (const char* text : header)
        options.header[slot++] = QString::fromLatin1(text);
    slot = 0;
    for (const char* text : footer)
        options.footer[slot++] = QString::fromLatin1(text);
    return options;
}

QString slotKey(bool header, int slot)
{
    static constexpr const char* names[] = { "Left", "Center", "Right" };
    return QStringLiteral("pdf/") + (header ? QStringLiteral("header") : QStringLiteral("footer"))
         + QLatin1StringView(names[slot]);
}

} // namespace

bool HeaderFooterOptions::operator==(const HeaderFooterOptions& other) const
{
    if (headerEnabled != other.headerEnabled || footerEnabled != other.footerEnabled)
        return false;
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (header[slot] != other.header[slot] || footer[slot] != other.footer[slot])
            return false;
    }
    return true;
}

namespace HeaderFooter {

QStringList tokens()
{
    return { QStringLiteral("{file}"), QStringLiteral("{title}"), QStringLiteral("{date}"),
             QStringLiteral("{time}"), QStringLiteral("{page}"), QStringLiteral("{pages}") };
}

HeaderFooterContext makeContext(const QString& fileName, const QString& frontMatterTitle,
                                const QDateTime& dateTime)
{
    HeaderFooterContext context;
    context.fileName = fileName;
    context.title = frontMatterTitle.trimmed().isEmpty()
        ? QFileInfo(fileName).completeBaseName()
        : frontMatterTitle.trimmed();
    if (context.title.isEmpty())
        context.title = fileName;
    context.dateTime = dateTime;
    return context;
}

QString expand(const QString& slotTemplate, const HeaderFooterContext& context, int page, int pages)
{
    QString out;
    for (const Part& part : parse(slotTemplate)) {
        switch (part.kind) {
        case Piece::Text:  out += part.text; break;
        case Piece::Page:  out += QString::number(page); break;
        case Piece::Pages: out += QString::number(pages); break;
        default:           out += contextText(part.kind, context); break;
        }
    }
    return out;
}

QString cssContent(const QString& slotTemplate, const HeaderFooterContext& context)
{
    QStringList values;
    QString literal;
    const auto flush = [&] {
        if (!literal.isEmpty()) {
            values << cssString(literal);
            literal.clear();
        }
    };
    for (const Part& part : parse(slotTemplate)) {
        switch (part.kind) {
        case Piece::Text:
            literal += part.text;
            break;
        case Piece::Page:
            flush();
            values << QStringLiteral("counter(page)");
            break;
        case Piece::Pages:
            flush();
            values << QStringLiteral("counter(pages)");
            break;
        default:
            literal += contextText(part.kind, context);
            break;
        }
    }
    flush();
    return values.isEmpty() ? QStringLiteral("\"\"") : values.join(u' ');
}

QString marginBoxCss(const HeaderFooterOptions& options, const HeaderFooterContext& context)
{
    if (!options.isActive())
        return {};
    QString css;
    appendBand(css, options.headerEnabled, options.header, true, context);
    appendBand(css, options.footerEnabled, options.footer, false, context);
    return css;
}

QList<HeaderFooterPreset> presets()
{
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("HeaderFooter", text);
    };
    return {
        { tr("None"), makeOptions(false, {}, false, {}) },
        { tr("Page number"), makeOptions(false, {}, true, { "", DefaultFooterCenter, "" }) },
        { tr("Filename and page number"),
          makeOptions(true, { DefaultHeaderLeft, "", "" }, true, { "", DefaultFooterCenter, "" }) },
        { tr("Filename, date and page"),
          makeOptions(true, { DefaultHeaderLeft, "", "{date}" }, true, { "", DefaultFooterCenter, "" }) },
        { tr("Title and page"),
          makeOptions(true, { "", "{title}", "" }, true, { "", "", "{page} / {pages}" }) },
    };
}

int presetIndex(const HeaderFooterOptions& options)
{
    const QList<HeaderFooterPreset> list = presets();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).options == options)
            return i;
    }
    return -1;
}

QStringList settingsKeys()
{
    QStringList keys { QStringLiteral("pdf/headerEnabled"), QStringLiteral("pdf/footerEnabled") };
    for (const bool header : { true, false }) {
        for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot)
            keys << slotKey(header, slot);
    }
    return keys;
}

HeaderFooterOptions load(const SettingsLookup& value)
{
    HeaderFooterOptions options;
    options.headerEnabled = value(QStringLiteral("pdf/headerEnabled"), false).toBool();
    options.footerEnabled = value(QStringLiteral("pdf/footerEnabled"), false).toBool();
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        options.header[slot] = value(slotKey(true, slot), QString()).toString().left(MaxSlotLength);
        options.footer[slot] = value(slotKey(false, slot), QString()).toString().left(MaxSlotLength);
    }
    return options;
}

void save(const HeaderFooterOptions& options, const SettingsWriter& setValue)
{
    setValue(QStringLiteral("pdf/headerEnabled"), options.headerEnabled);
    setValue(QStringLiteral("pdf/footerEnabled"), options.footerEnabled);
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        setValue(slotKey(true, slot), options.header[slot]);
        setValue(slotKey(false, slot), options.footer[slot]);
    }
}

}
