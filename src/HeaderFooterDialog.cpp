#include "HeaderFooterDialog.h"

#include "CompactComboBox.h"
#include "ThemePalette.h"

#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTextLayout>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

constexpr double kCrowdedMarginMm = 10.0;
constexpr int kSamplePage = 1;
// The margin boxes' font: 9pt with a 1.25 line height (see HeaderFooter.cpp).
constexpr double kFontSizeMm = 9.0 * 25.4 / 72.0;
constexpr double kLineHeightMm = kFontSizeMm * 1.25;
constexpr int kSamplePages = 3;

bool isDarkPalette(const QWidget* widget)
{
    return widget->palette().color(QPalette::Window).lightness() < 128;
}

} // namespace

// The configured page in miniature: real Page Setup proportions and margins,
// placeholder body lines, and the header/footer slots laid out in the same
// equal thirds the CSS margin boxes use. Text is drawn larger than its true
// 9 pt scale, which would be a couple of pixels high here, so it stays legible.
class HeaderFooterPreview final : public QWidget
{
public:
    HeaderFooterPreview(const ExportManager::PdfExportOptions& page, const HeaderFooterContext& sample,
                        QWidget* parent)
        : QWidget(parent), m_sample(sample), m_marginsMm(page.marginsMm)
    {
        m_pageMm = page.pageSize.size(QPageSize::Millimeter);
        if (page.orientation == QPageLayout::Landscape && m_pageMm.width() < m_pageMm.height())
            m_pageMm.transpose();
        if (m_pageMm.isEmpty())
            m_pageMm = QSizeF(210.0, 297.0);
        setFixedSize(196, 264);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

    void setOptions(const HeaderFooterOptions& options)
    {
        m_options = options;
        update();
    }

    // band: 0 header, 1 footer, -1 none.
    void setHighlight(int band, int slot)
    {
        m_highlightBand = band;
        m_highlightSlot = slot;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(this);

        const QRectF avail = QRectF(rect()).adjusted(6, 4, -6, -8);
        const double scale = std::min(avail.width() / m_pageMm.width(), avail.height() / m_pageMm.height());
        const QSizeF pagePx(m_pageMm.width() * scale, m_pageMm.height() * scale);
        const QRectF page(QPointF(std::round(avail.center().x() - pagePx.width() / 2.0) + 0.5,
                                  std::round(avail.top()) + 0.5),
                          QSizeF(std::round(pagePx.width()), std::round(pagePx.height())));

        for (int i = 3; i >= 1; --i) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, dark ? 34 : 14));
            p.drawRoundedRect(page.translated(0, i), 2, 2);
        }
        p.setBrush(Qt::white);
        p.setPen(QPen(ThemePalette::color(ThemePalette::Border, dark), 1));
        p.drawRoundedRect(page, 2, 2);

        const QRectF body = page.adjusted(m_marginsMm.left() * scale, m_marginsMm.top() * scale,
                                          -m_marginsMm.right() * scale, -m_marginsMm.bottom() * scale);
        paintBody(p, body);

        p.save();
        p.setClipRect(page.adjusted(1, 1, -1, -1));
        paintBand(p, page, body, true);
        paintBand(p, page, body, false);
        p.restore();
    }

private:
    void paintBody(QPainter& p, const QRectF& body) const
    {
        if (body.width() < 4 || body.height() < 4)
            return;
        p.save();
        p.setClipRect(body);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xE3, 0xE6, 0xEA));
        static constexpr double widths[] = { 1.0, 0.94, 1.0, 0.86, 1.0, 0.62 };
        double y = body.top() + 3;
        p.drawRoundedRect(QRectF(body.left(), y, body.width() * 0.55, 5), 1.5, 1.5);
        y += 11;
        int line = 0;
        while (y + 3 <= body.bottom()) {
            const double w = widths[line % 6];
            p.drawRoundedRect(QRectF(body.left(), y, body.width() * w, 3), 1, 1);
            y += 6;
            if (++line % 6 == 0)
                y += 5;
        }
        p.restore();
    }

    void paintBand(QPainter& p, const QRectF& page, const QRectF& body, bool header) const
    {
        const bool enabled = header ? m_options.headerEnabled : m_options.footerEnabled;
        const QString* cells = header ? m_options.header : m_options.footer;
        const double third = body.width() / 3.0;
        const QRectF band = header ? QRectF(body.left(), page.top(), body.width(), body.top() - page.top())
                                   : QRectF(body.left(), body.bottom(), body.width(), page.bottom() - body.bottom());

        QFont font = this->font();
        font.setPixelSize(7);
        p.setFont(font);
        const QFontMetricsF fm(font);
        const bool dark = isDarkPalette(this);
        const double thirdMm = (m_pageMm.width() - m_marginsMm.left() - m_marginsMm.right()) / 3.0;
        const double bandMm = header ? m_marginsMm.top() : m_marginsMm.bottom();
        // Lines the real 9pt/1.25 margin box can show before Chromium clips it.
        const int paperLines = std::max(1, int(bandMm / kLineHeightMm));
        const int displayLines = std::max(1, int((band.height() - 2) / fm.height()));

        for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
            const QRectF cell(band.left() + slot * third, band.top(), third, band.height());
            if ((header ? 0 : 1) == m_highlightBand && slot == m_highlightSlot) {
                p.setPen(QPen(ThemePalette::color(ThemePalette::Accent, dark), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 2, 2);
            }
            if (!enabled)
                continue;
            const QString text = HeaderFooter::expand(cells[slot], m_sample, kSamplePage, kSamplePages)
                                     .simplified();
            if (text.isEmpty())
                continue;
            const Qt::Alignment h = slot == HeaderFooterOptions::Left ? Qt::AlignLeft
                                  : slot == HeaderFooterOptions::Center ? Qt::AlignHCenter
                                                                        : Qt::AlignRight;
            // Line breaks come from the paper, not the thumbnail: the legible
            // preview font is several times the true scale and would wrap text
            // that fits on one printed line. What does not fit is elided.
            const QStringList lines = paperLineBreaks(text, thirdMm);
            const int shown = std::min({ int(lines.size()), paperLines, displayLines });
            const bool truncated = lines.size() > shown;
            const double blockHeight = shown * fm.height();
            // Centred in the margin like the CSS boxes; text taller than its
            // box starts at the box top and is clipped at the box edge.
            double y = truncated || blockHeight > cell.height() ? cell.top() + 1
                                                                : cell.center().y() - blockHeight / 2.0;
            p.save();
            p.setClipRect(cell, Qt::IntersectClip);
            p.setPen(QColor(0x57, 0x60, 0x6A));
            for (int i = 0; i < shown; ++i) {
                QString line = lines.at(i);
                if (truncated && i == shown - 1)
                    line += QChar(0x2026);
                line = fm.elidedText(line, Qt::ElideRight, cell.width() - 2);
                p.drawText(QRectF(cell.left() + 1, y, cell.width() - 2, fm.height()), int(h) | Qt::AlignTop, line);
                y += fm.height();
            }
            p.restore();
        }
    }

    // Where Chromium breaks a slot's text inside a margin box `widthMm` wide,
    // laid out in Segoe UI at its real 9pt size (to scale, in millimetres).
    static QStringList paperLineBreaks(const QString& text, double widthMm)
    {
        constexpr double kPxPerMm = 8.0;
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(qRound(kFontSizeMm * kPxPerMm));
        QTextLayout layout(text, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WordWrap);
        layout.setTextOption(option);
        QStringList lines;
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(widthMm * kPxPerMm);
            lines << text.mid(line.textStart(), line.textLength()).trimmed();
        }
        layout.endLayout();
        return lines;
    }

    HeaderFooterOptions m_options;
    HeaderFooterContext m_sample;
    QSizeF m_pageMm;
    QMarginsF m_marginsMm;
    int m_highlightBand = -1;
    int m_highlightSlot = -1;
};

HeaderFooterDialog::HeaderFooterDialog(const ExportManager::PdfExportOptions& page,
                                       const HeaderFooterContext& sample, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
    , m_marginsMm(page.marginsMm)
{
    setObjectName(QStringLiteral("headerFooterDialog"));
    setWindowTitle(tr("Header & Footer"));
    setSizeGripEnabled(false);
    applyTitleBarTheme();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* top = new QHBoxLayout();
    top->setSpacing(18);
    root->addLayout(top);

    m_preview = new HeaderFooterPreview(page, sample, this);
    top->addWidget(m_preview, 0, Qt::AlignTop);

    auto* form = new QVBoxLayout();
    form->setSpacing(8);
    top->addLayout(form, 1);

    auto* presetRow = new QHBoxLayout();
    presetRow->setSpacing(8);
    auto* presetLabel = new QLabel(tr("Preset"), this);
    m_preset = new CompactComboBox(this);
    for (const HeaderFooterPreset& preset : HeaderFooter::presets())
        m_preset->addItem(preset.name);
    m_preset->addItem(tr("Custom"));
    presetLabel->setBuddy(m_preset);
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(m_preset, 1);
    form->addLayout(presetRow);
    form->addSpacing(4);

    const QString tokenTip = tr("Text and tokens: {file}  {title}  {date}  {time}  {page}  {pages}\n"
                                "Type {{ or }} for a literal brace.");
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    m_headerCheck = new QCheckBox(tr("Header"), this);
    m_footerCheck = new QCheckBox(tr("Footer"), this);
    grid->addWidget(m_headerCheck, 0, 0, 1, 3);
    const QString captions[] = { tr("Left"), tr("Center"), tr("Right") };
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        auto* caption = new QLabel(captions[slot], this);
        caption->setObjectName(QStringLiteral("headerFooterCaption"));
        caption->setAlignment(slot == HeaderFooterOptions::Left ? Qt::AlignLeft
                              : slot == HeaderFooterOptions::Center ? Qt::AlignHCenter
                                                                    : Qt::AlignRight);
        grid->addWidget(caption, 1, slot);
        for (const bool header : { true, false }) {
            auto* edit = new QLineEdit(this);
            edit->setMaxLength(HeaderFooter::MaxSlotLength);
            edit->setToolTip(tokenTip);
            edit->setAccessibleName((header ? tr("Header %1") : tr("Footer %1")).arg(captions[slot]));
            edit->setMinimumWidth(124);
            edit->installEventFilter(this);
            (header ? m_header : m_footer)[slot] = edit;
            grid->addWidget(edit, header ? 2 : 4, slot);
            // textChanged, not textEdited: values set through accessibility
            // (UI Automation) must refresh the preview as typing does.
            connect(edit, &QLineEdit::textChanged, this, [this] {
                if (!m_updating)
                    refresh();
            });
        }
    }
    grid->addWidget(m_footerCheck, 3, 0, 1, 3);
    grid->setRowMinimumHeight(3, 30);
    grid->setAlignment(m_footerCheck, Qt::AlignBottom);
    form->addLayout(grid);
    form->addSpacing(4);

    auto* tokenRow = new QHBoxLayout();
    tokenRow->setSpacing(5);
    auto* insertLabel = new QLabel(tr("Insert"), this);
    tokenRow->addWidget(insertLabel);
    tokenRow->addSpacing(3);
    const QString tokenNames[] = { tr("File"), tr("Title"), tr("Date"), tr("Time"), tr("Page"), tr("Pages") };
    const QStringList tokens = HeaderFooter::tokens();
    for (int i = 0; i < tokens.size(); ++i) {
        auto* button = new QPushButton(tokenNames[i], this);
        button->setObjectName(QStringLiteral("headerFooterToken"));
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoDefault(false);
        button->setToolTip(tr("Insert %1").arg(tokens.at(i)));
        const QString token = tokens.at(i);
        connect(button, &QPushButton::clicked, this, [this, token] { insertToken(token); });
        tokenRow->addWidget(button);
        m_tokenButtons.append(button);
    }
    tokenRow->addStretch(1);
    form->addLayout(tokenRow);

    auto* scope = new QLabel(tr("Used by Print and PDF export."), this);
    scope->setObjectName(QStringLiteral("headerFooterCaption"));
    form->addWidget(scope);
    form->addStretch(1);

    m_marginHint = new QLabel(this);
    m_marginHint->setObjectName(QStringLiteral("headerFooterCaption"));
    m_marginHint->setWordWrap(true);
    m_marginHint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Two lines are reserved so the hint appearing never resizes the dialog.
    m_marginHint->setFixedHeight(2 * m_marginHint->fontMetrics().lineSpacing() + 2);
    root->addWidget(m_marginHint);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* ok = new QPushButton(tr("OK"), this);
    ok->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    root->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_preset, &QComboBox::activated, this, &HeaderFooterDialog::applyPreset);
    connect(m_headerCheck, &QCheckBox::toggled, this, [this](bool on) { bandToggled(true, on); });
    connect(m_footerCheck, &QCheckBox::toggled, this, [this](bool on) { bandToggled(false, on); });

    m_lastEdit = m_header[HeaderFooterOptions::Left];
    setOptions(page.headerFooter);
    if (!page.headerFooter.headerEnabled && page.headerFooter.footerEnabled)
        m_lastEdit = m_footer[HeaderFooterOptions::Center];
    refresh();
}

HeaderFooterOptions HeaderFooterDialog::options() const
{
    HeaderFooterOptions options;
    options.headerEnabled = m_headerCheck->isChecked();
    options.footerEnabled = m_footerCheck->isChecked();
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        options.header[slot] = m_header[slot]->text();
        options.footer[slot] = m_footer[slot]->text();
    }
    return options;
}

void HeaderFooterDialog::setOptions(const HeaderFooterOptions& options)
{
    m_updating = true;
    m_headerCheck->setChecked(options.headerEnabled);
    m_footerCheck->setChecked(options.footerEnabled);
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        m_header[slot]->setText(options.header[slot]);
        m_header[slot]->setCursorPosition(0);
        m_footer[slot]->setText(options.footer[slot]);
        m_footer[slot]->setCursorPosition(0);
    }
    m_updating = false;
}

void HeaderFooterDialog::applyPreset(int index)
{
    const QList<HeaderFooterPreset> presets = HeaderFooter::presets();
    if (index < 0 || index >= presets.size())
        return;
    setOptions(presets.at(index).options);
    refresh();
}

void HeaderFooterDialog::bandToggled(bool header, bool on)
{
    if (m_updating)
        return;
    // Switching on an empty band fills a sensible default, so ticking the box
    // always produces something visible on the page.
    QLineEdit* const* cells = header ? m_header : m_footer;
    if (on && cells[0]->text().isEmpty() && cells[1]->text().isEmpty() && cells[2]->text().isEmpty()) {
        QLineEdit* target = header ? cells[HeaderFooterOptions::Left] : cells[HeaderFooterOptions::Center];
        target->setText(QString::fromLatin1(header ? HeaderFooter::DefaultHeaderLeft
                                                   : HeaderFooter::DefaultFooterCenter));
        target->setCursorPosition(0);
        m_lastEdit = target;
    } else if (on) {
        m_lastEdit = cells[HeaderFooterOptions::Left];
        for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
            if (!cells[slot]->text().isEmpty()) {
                m_lastEdit = cells[slot];
                break;
            }
        }
    }
    refresh();
}

void HeaderFooterDialog::insertToken(const QString& token)
{
    if (!m_lastEdit || !m_lastEdit->isEnabled())
        return;
    m_lastEdit->setFocus(Qt::OtherFocusReason);
    m_lastEdit->insert(token);
    refresh();
}

void HeaderFooterDialog::refresh()
{
    const HeaderFooterOptions current = options();
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        m_header[slot]->setEnabled(current.headerEnabled);
        m_footer[slot]->setEnabled(current.footerEnabled);
    }

    const int preset = HeaderFooter::presetIndex(current);
    m_preset->setCurrentIndex(preset < 0 ? m_preset->count() - 1 : preset);

    int band = -1;
    int cell = -1;
    for (int slot = 0; slot < HeaderFooterOptions::SlotCount; ++slot) {
        if (m_lastEdit == m_header[slot]) { band = 0; cell = slot; }
        if (m_lastEdit == m_footer[slot]) { band = 1; cell = slot; }
    }
    const bool targetEnabled = m_lastEdit && m_lastEdit->isEnabled();
    for (QPushButton* button : std::as_const(m_tokenButtons))
        button->setEnabled(targetEnabled);
    m_preview->setOptions(current);
    m_preview->setHighlight(targetEnabled ? band : -1, cell);

    QStringList crowded;
    if (current.headerEnabled && m_marginsMm.top() < kCrowdedMarginMm)
        crowded << tr("The top margin is %1 mm, so the header may crowd the text.")
                       .arg(QString::number(m_marginsMm.top(), 'f', 0));
    if (current.footerEnabled && m_marginsMm.bottom() < kCrowdedMarginMm)
        crowded << tr("The bottom margin is %1 mm, so the footer may crowd the text.")
                       .arg(QString::number(m_marginsMm.bottom(), 'f', 0));
    if (!crowded.isEmpty())
        crowded << tr("Adjust it in Page Setup.");
    m_marginHint->setText(crowded.join(u' '));
}

bool HeaderFooterDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) {
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            m_lastEdit = edit;
            refresh();
        }
    }
    return QDialog::eventFilter(watched, event);
}

void HeaderFooterDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
    // Fixed only once the application stylesheet has polished every child;
    // fixing it in the constructor would freeze pre-polish metrics.
    adjustSize();
    setFixedSize(size());
}

void HeaderFooterDialog::applyTitleBarTheme()
{
#ifdef Q_OS_WIN
    const bool isDarkTheme = isDarkPalette(this);
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL enabled = isDarkTheme ? TRUE : FALSE;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))) ||
        SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)))) {
        const COLORREF captionColor = isDarkTheme ? RGB(0x20, 0x21, 0x22) : RGB(0xf3, 0xf3, 0xf3);
        const COLORREF textColor = isDarkTheme ? RGB(0xbf, 0xbf, 0xbf) : RGB(0x20, 0x20, 0x20);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}
