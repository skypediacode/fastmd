#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QVariant>

#include <array>

// User-chosen Document Spacing for the Preview, Browser Preview, HTML export,
// Markdown Print and PDF (GUI and command line). Three independent presets,
// each stored as an integer so nothing is parsed from a float:
//   line      hundredths of a line-height multiplier   (150 -> 1.5)
//   paragraph points                                   (12 -> 12pt, = 16px)
//   list      hundredths of an em                      (25 -> 0.25em)
// A stored value that is not one of the presets (a hand-edited .ini) is the
// default, and a default is never emitted, so an untouched install produces the
// same CSS as before. Values reach CSS only through the preset tables below.
namespace DocumentSpacing {

inline constexpr QLatin1StringView LineKey{"document/lineSpacing"};
inline constexpr QLatin1StringView ParagraphKey{"document/paragraphSpacing"};
inline constexpr QLatin1StringView ListKey{"document/listSpacing"};

inline constexpr int DefaultLine = 150;
inline constexpr int DefaultParagraph = 12;
inline constexpr int DefaultList = 25;

inline constexpr std::array<int, 7> LinePresets{100, 115, 125, 140, 150, 175, 200};
inline constexpr std::array<int, 7> ParagraphPresets{0, 3, 6, 9, 12, 15, 18};
inline constexpr std::array<int, 5> ListPresets{0, 10, 25, 40, 60};

template <size_t N>
constexpr bool isPreset(const std::array<int, N>& presets, int value)
{
    for (const int preset : presets) {
        if (preset == value)
            return true;
    }
    return false;
}

struct Settings {
    int line = DefaultLine;
    int paragraph = DefaultParagraph;
    int list = DefaultList;

    bool operator==(const Settings&) const = default;
    bool isDefault() const { return *this == Settings(); }
};

// A stored (or null/invalid) value as a usable one: itself when it is a preset,
// otherwise the default.
inline int normalizedLine(int value) { return isPreset(LinePresets, value) ? value : DefaultLine; }
inline int normalizedParagraph(int value) { return isPreset(ParagraphPresets, value) ? value : DefaultParagraph; }
inline int normalizedList(int value) { return isPreset(ListPresets, value) ? value : DefaultList; }

inline Settings normalized(const Settings& settings)
{
    return { normalizedLine(settings.line), normalizedParagraph(settings.paragraph),
             normalizedList(settings.list) };
}

// A settings value as read from QSettings or a lookup: absent or non-numeric
// is the default.
inline int storedInt(const QVariant& value, int fallback)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    return ok ? number : fallback;
}

// Reads the three keys through `value(key)`, which returns an invalid QVariant
// for a missing key.
template <typename Lookup>
Settings load(const Lookup& value)
{
    return { normalizedLine(storedInt(value(LineKey), DefaultLine)),
             normalizedParagraph(storedInt(value(ParagraphKey), DefaultParagraph)),
             normalizedList(storedInt(value(ListKey), DefaultList)) };
}

// Each CSS custom property value, or empty for the default so the stylesheet's
// own fallback applies. Written out as exact decimals (1.15, 0.25em), never
// through floating point.
inline QString hundredthsText(int hundredths)
{
    QString text = QString::number(hundredths / 100) + u'.' + QString::number(hundredths % 100).rightJustified(2, u'0');
    while (text.endsWith(u'0') && !text.endsWith(QLatin1StringView(".0")))
        text.chop(1);
    return text;
}

inline QString lineValue(const Settings& settings)
{
    const int line = normalizedLine(settings.line);
    return line == DefaultLine ? QString() : hundredthsText(line);
}

inline QString paragraphValue(const Settings& settings)
{
    const int paragraph = normalizedParagraph(settings.paragraph);
    return paragraph == DefaultParagraph ? QString() : QString::number(paragraph) + QStringLiteral("pt");
}

inline QString listValue(const Settings& settings)
{
    const int list = normalizedList(settings.list);
    return list == DefaultList ? QString() : hundredthsText(list) + QStringLiteral("em");
}

// A standalone page's rule, or empty when every value is the default, which
// keeps default export HTML unchanged. It sets --lh, --pspace and --li-gap
// only; --space and every heading rule are not touched.
inline QString rootCss(const Settings& settings)
{
    QString declarations;
    const QString line = lineValue(settings);
    const QString paragraph = paragraphValue(settings);
    const QString list = listValue(settings);
    if (!line.isEmpty())
        declarations += QStringLiteral("--lh:%1;").arg(line);
    if (!paragraph.isEmpty())
        declarations += QStringLiteral("--pspace:%1;").arg(paragraph);
    if (!list.isEmpty())
        declarations += QStringLiteral("--li-gap:%1;").arg(list);
    return declarations.isEmpty() ? QString() : QStringLiteral(":root{%1}").arg(declarations);
}

}
