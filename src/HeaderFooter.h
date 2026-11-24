#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

// Page headers and footers for Print and PDF export. Rendered as CSS @page
// margin boxes, so the browser paginates and numbers the pages itself; this
// module only turns the user's slot templates into CSS. Qt Core only, so the
// export pipeline, the dialog preview and the tests share one implementation.
struct HeaderFooterOptions {
    enum Slot { Left, Center, Right, SlotCount };

    bool headerEnabled = false;
    bool footerEnabled = false;
    QString header[SlotCount];
    QString footer[SlotCount];

    bool isActive() const { return headerEnabled || footerEnabled; }
    bool operator==(const HeaderFooterOptions& other) const;
    bool operator!=(const HeaderFooterOptions& other) const { return !(*this == other); }
};

// Values substituted for the non-counter tokens. Resolved once per print or
// export job so every page shows the same date and time.
struct HeaderFooterContext {
    QString fileName;
    QString title;
    QDateTime dateTime;
};

struct HeaderFooterPreset {
    QString name;
    HeaderFooterOptions options;
};

namespace HeaderFooter {

inline constexpr int MaxSlotLength = 120;

// Defaults a band receives when it is switched on with every slot empty.
inline constexpr const char* DefaultHeaderLeft = "{file}";
inline constexpr const char* DefaultFooterCenter = "Page {page} of {pages}";

// {file} {title} {date} {time} {page} {pages}; "{{" and "}}" are literal braces.
QStringList tokens();

HeaderFooterContext makeContext(const QString& fileName, const QString& frontMatterTitle,
                                const QDateTime& dateTime = QDateTime::currentDateTime());

// Template text with every token replaced, as one page would show it.
QString expand(const QString& slotTemplate, const HeaderFooterContext& context, int page, int pages);

// The value of a margin box's `content` property, e.g. "Page " counter(page).
QString cssContent(const QString& slotTemplate, const HeaderFooterContext& context);

// Margin-box rules to place inside @page{...}. Empty when both bands are off,
// which keeps the page CSS byte-identical to the output without this feature.
QString marginBoxCss(const HeaderFooterOptions& options, const HeaderFooterContext& context);

QList<HeaderFooterPreset> presets();
// Index into presets(), or -1 when the options match none of them (Custom).
int presetIndex(const HeaderFooterOptions& options);

QStringList settingsKeys();
using SettingsLookup = std::function<QVariant(const QString& key, const QVariant& defaultValue)>;
using SettingsWriter = std::function<void(const QString& key, const QVariant& value)>;
HeaderFooterOptions load(const SettingsLookup& value);
void save(const HeaderFooterOptions& options, const SettingsWriter& setValue);

}
