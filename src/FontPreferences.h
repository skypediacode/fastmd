#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QLatin1StringView>
#include <QMutex>
#include <QRawFont>
#include <QString>

#include <algorithm>

// User-chosen font families for the Editor, the Preview, and Print/PDF. Each
// is one family name; an empty value means "FastMD's built-in font", which is
// what every existing install has, so no setting needs migrating. A saved
// family is applied as a plain string and Qt or the browser falls back on its
// own when it is not installed. Only missingVietnameseRange() reads a font,
// and nothing calls it on the startup path.
namespace FontPreferences {

inline constexpr QLatin1StringView EditorKey{"editor/fontFamily"};
inline constexpr QLatin1StringView PreviewKey{"preview/fontFamily"};
inline constexpr QLatin1StringView PrintKey{"pdf/fontFamily"};

inline constexpr QLatin1StringView DefaultEditorFamily{"Consolas"};

// Must match the body font-family in resources/preview/preview.css (and
// ExportManager's legacy buildCss()). A chosen family is put in front of it,
// so a glyph the chosen font lacks falls back exactly as it does today, with
// the exception lacksVietnamese() covers.
inline constexpr QLatin1StringView PreviewBodyStack{
    "'Segoe UI','SF Pro Text',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif"};

// The family as it may be stored and applied, or empty (the default) when it
// is blank or could not be quoted safely. The names reach QSS, CSS inside an
// HTML <style> element, and a JSON message, so rather than escape for each of
// those, a name holding a quote, backslash, CSS/QSS punctuation, an angle
// bracket or a control character is rejected; no real font family uses them.
inline QString normalizedFamily(const QString& family)
{
    const QString trimmed = family.trimmed();
    if (trimmed.size() > 128)
        return {};
    for (const QChar ch : trimmed) {
        const char16_t c = ch.unicode();
        if (c < 0x20 || c == 0x7F || c == 0x2028 || c == 0x2029)
            return {};
        switch (c) {
        case u'"': case u'\\': case u';': case u'{': case u'}': case u'<': case u'>':
            return {};
        default:
            break;
        }
    }
    return trimmed;
}

// The family as a double-quoted CSS/QSS string, or empty for the default.
inline QString quotedFamily(const QString& family)
{
    const QString name = normalizedFamily(family);
    return name.isEmpty() ? QString() : QStringLiteral("\"%1\"").arg(name);
}

// The editor's single family; never a list (see MarkdownEditor::monospaceFont()).
inline QString editorFamilyOrDefault(const QString& family)
{
    const QString name = normalizedFamily(family);
    return name.isEmpty() ? QString(DefaultEditorFamily) : name;
}

// Appended to the application stylesheet only when the editor font is not the
// default. It targets MarkdownEditor alone, so other QPlainTextEdits (the Git
// diff panes, the commit message) keep the QPlainTextEdit rule's Consolas, and
// it comes last so it wins over that rule at equal specificity.
inline QString editorStyleRule(const QString& family)
{
    const QString quoted = quotedFamily(family);
    return quoted.isEmpty() ? QString()
        : QStringLiteral("\nMarkdownEditor { font-family: %1; }\n").arg(quoted);
}

// Chromium does not fall back for a precomposed letter the font lacks when it
// can build that letter from the font's own base letter and combining marks.
// Georgia 5.59 lacks most precomposed Vietnamese letters but has the grave,
// acute, circumflex and breve marks and no mark positioning, so "ế" is drawn
// as its "ê" with a loose acute beside it, while "ệ" (no dot below) falls back to
// Segoe UI. Word draws every letter the font lacks from another font instead
// (Cambria for Georgia). For such a family the body stack is led by this face:
// Segoe UI limited to the letters the family lacks, so each of them falls back
// as it would in Word and the letters the family has keep its own glyphs. Text
// written with combining marks (NFD) is not covered: its base letter is outside
// the range, so the cluster still goes to the chosen font.
inline constexpr QLatin1StringView VietnameseFallbackFamily{"FastMD Vietnamese"};

// The faces for a CSS unicode-range (see missingVietnameseRange()); empty
// when the range is.
inline QString vietnameseFallbackFaces(const QString& unicodeRange)
{
    if (unicodeRange.isEmpty())
        return {};
    struct Face { const char* name; int weight; const char* style; };
    static constexpr Face kFaces[] = {
        { "Segoe UI", 400, "normal" },
        { "Segoe UI Bold", 700, "normal" },
        { "Segoe UI Italic", 400, "italic" },
        { "Segoe UI Bold Italic", 700, "italic" },
    };
    QString css;
    for (const Face& face : kFaces) {
        css += QStringLiteral("@font-face{font-family:\"%1\";src:local(\"%2\");font-weight:%3;"
                              "font-style:%4;unicode-range:%5;}")
                   .arg(VietnameseFallbackFamily, QLatin1StringView(face.name))
                   .arg(face.weight)
                   .arg(QLatin1StringView(face.style), unicodeRange);
    }
    return css;
}

// The Vietnamese letters an installed family's regular face has no glyph for,
// as a CSS unicode-range ("U+1EA0-1EA3,U+1EA5"), or empty when it has them
// all or is not installed (the default stack then renders everything anyway).
// QFontDatabase::writingSystems() cannot answer this: it reports Vietnamese
// for Georgia too. Reading the cmap realizes the family's font engine once
// (about 7 ms for the first family, under 1 ms after), so the answer is
// cached; callers are the Preview's font message and exports, never startup.
inline QString missingVietnameseRange(const QString& family)
{
    const QString name = normalizedFamily(family);
    // Fonts need a QGuiApplication; a Qt Core-only caller keeps the plain stack.
    if (name.isEmpty() || !qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return {};

    static QMutex mutex;
    static QHash<QString, QString> cache;
    {
        const QMutexLocker lock(&mutex);
        const auto it = cache.constFind(name);
        if (it != cache.constEnd())
            return *it;
    }

    QString range;
    if (QFontDatabase::hasFamily(name)) {
        static constexpr char32_t kLatin[] = {
            0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C8, 0x00C9, 0x00CA, 0x00CC, 0x00CD, 0x00D2, 0x00D3,
            0x00D4, 0x00D5, 0x00D9, 0x00DA, 0x00DD, 0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E8, 0x00E9,
            0x00EA, 0x00EC, 0x00ED, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F9, 0x00FA, 0x00FD, 0x0102,
            0x0103, 0x0110, 0x0111, 0x0128, 0x0129, 0x0168, 0x0169, 0x01A0, 0x01A1, 0x01AF, 0x01B0,
        };
        QList<char32_t> letters(std::begin(kLatin), std::end(kLatin));
        for (char32_t c = 0x1EA0; c <= 0x1EF9; ++c)
            letters.append(c);

        QFont font(name);
        font.setPixelSize(16);
        const QRawFont raw = QRawFont::fromFont(font);
        // A family the engine resolved to another one says nothing about it.
        if (raw.isValid() && raw.familyName().compare(name, Qt::CaseInsensitive) == 0) {
            QList<char32_t> missing;
            for (const char32_t c : letters) {
                if (!raw.supportsCharacter(c))
                    missing.append(c);
            }
            for (qsizetype i = 0; i < missing.size();) {
                qsizetype end = i;
                while (end + 1 < missing.size() && missing[end + 1] == missing[end] + 1)
                    ++end;
                if (!range.isEmpty())
                    range += u',';
                range += QStringLiteral("U+%1").arg(uint(missing[i]), 4, 16, QLatin1Char('0')).toUpper();
                if (end > i)
                    range += QStringLiteral("-%1").arg(uint(missing[end]), 4, 16, QLatin1Char('0')).toUpper();
                i = end + 1;
            }
        }
    }

    const QMutexLocker lock(&mutex);
    cache.insert(name, range);
    return range;
}

// The value for body's font-family when a family is chosen, or empty for the
// default (the stylesheet's own stack applies untouched). With
// vietnameseFallback the stack needs vietnameseFallbackFaces() beside it.
inline QString bodyFontStack(const QString& family, bool vietnameseFallback = false)
{
    const QString quoted = quotedFamily(family);
    if (quoted.isEmpty())
        return {};
    const QString stack = QStringLiteral("%1,%2").arg(quoted, PreviewBodyStack);
    return vietnameseFallback ? QStringLiteral("\"%1\",%2").arg(VietnameseFallbackFamily, stack) : stack;
}

// A rule for a standalone page's <style>; empty for the default, so default
// export HTML stays byte-identical. A non-empty vietnameseRange adds the
// fallback faces ahead of the rule.
inline QString bodyFontCss(const QString& family, const QString& vietnameseRange = {})
{
    const bool fallback = !vietnameseRange.isEmpty();
    const QString stack = bodyFontStack(family, fallback);
    if (stack.isEmpty())
        return {};
    return vietnameseFallbackFaces(vietnameseRange) + QStringLiteral("body{font-family:%1;}").arg(stack);
}

}
