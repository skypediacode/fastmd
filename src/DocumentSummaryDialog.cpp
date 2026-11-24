#include "DocumentSummaryDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

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

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

// Keeps the value column hugging the label instead of stretching to fill the
// dialog's width (QFormLayout's default AllNonFixedFieldsGrow policy).
QFormLayout* compactForm()
{
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    return form;
}

QWidget* buildGeneralSection(const DocumentSummary& summary, QWidget* parent)
{
    auto* section = new QWidget(parent);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(sectionTitle(DocumentSummaryDialog::tr("General"), section));

    auto* form = compactForm();
    form->addRow(DocumentSummaryDialog::tr("Characters:"),
                 new QLabel(QString::number(summary.characterCount), section));
    form->addRow(DocumentSummaryDialog::tr("Words:"),
                 new QLabel(QString::number(summary.wordCount), section));
    form->addRow(DocumentSummaryDialog::tr("Lines:"),
                 new QLabel(QString::number(summary.lineCount), section));
    form->addRow(DocumentSummaryDialog::tr("Selected characters:"),
                 new QLabel(QString::number(summary.selectedCharacterCount), section));
    form->addRow(DocumentSummaryDialog::tr("Selected words:"),
                 new QLabel(QString::number(summary.selectedWordCount), section));
    layout->addLayout(form);
    return section;
}

QWidget* buildMarkdownSection(const DocumentSummary& summary, QWidget* parent)
{
    auto* section = new QWidget(parent);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(sectionTitle(DocumentSummaryDialog::tr("Markdown"), section));

    auto* leftForm = compactForm();
    leftForm->addRow(DocumentSummaryDialog::tr("H1 headings:"),
                      new QLabel(QString::number(summary.h1Count), section));
    leftForm->addRow(DocumentSummaryDialog::tr("H2 headings:"),
                      new QLabel(QString::number(summary.h2Count), section));
    leftForm->addRow(DocumentSummaryDialog::tr("H3 headings:"),
                      new QLabel(QString::number(summary.h3Count), section));
    leftForm->addRow(DocumentSummaryDialog::tr("Links:"),
                      new QLabel(QString::number(summary.linkCount), section));

    auto* rightForm = compactForm();
    rightForm->addRow(DocumentSummaryDialog::tr("Images:"),
                       new QLabel(QString::number(summary.imageCount), section));
    rightForm->addRow(DocumentSummaryDialog::tr("Tables:"),
                       new QLabel(QString::number(summary.tableCount), section));
    rightForm->addRow(DocumentSummaryDialog::tr("Code blocks:"),
                       new QLabel(QString::number(summary.codeBlockCount), section));
    rightForm->addRow(DocumentSummaryDialog::tr("Equations:"),
                       new QLabel(QString::number(summary.equationCount), section));

    auto* columns = new QHBoxLayout();
    columns->setSpacing(54);
    columns->addLayout(leftForm);
    columns->addLayout(rightForm);
    columns->addStretch(1);
    layout->addLayout(columns);
    return section;
}

} // namespace

DocumentSummaryDialog::DocumentSummaryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Document Statistics"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    applyTitleBarTheme();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 16, 16, 16);
    m_layout->setSpacing(12);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* btnClose = new QPushButton(tr("Close"), this);
    btnClose->setDefault(true);
    btnLayout->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    m_layout->addLayout(btnLayout);
    setMinimumWidth(300);
}

void DocumentSummaryDialog::setSummary(const DocumentSummary& summary)
{
    m_summary = summary;
    rebuild();
}

void DocumentSummaryDialog::rebuild()
{
    if (m_generalSection) {
        m_layout->removeWidget(m_generalSection);
        m_generalSection->hide();
        m_generalSection->deleteLater();
        m_generalSection = nullptr;
    }
    if (m_markdownSection) {
        m_layout->removeWidget(m_markdownSection);
        m_markdownSection->hide();
        m_markdownSection->deleteLater();
        m_markdownSection = nullptr;
    }

    m_generalSection = buildGeneralSection(m_summary, this);
    m_layout->insertWidget(0, m_generalSection);

    if (m_summary.isMarkdown) {
        m_markdownSection = buildMarkdownSection(m_summary, this);
        m_layout->insertWidget(1, m_markdownSection);
    }
}

void DocumentSummaryDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
}

void DocumentSummaryDialog::applyTitleBarTheme()
{
#ifdef Q_OS_WIN
    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
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
