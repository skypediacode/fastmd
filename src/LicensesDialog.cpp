#include "LicensesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QPushButton>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <qglobal.h>

LicensesDialog::LicensesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Open Source Licenses"));
    resize(680, 460);

    m_components = {
        { QStringLiteral("FastMD"), QStringLiteral("1.7.0"), QStringLiteral("MIT License"),
          QStringLiteral("https://github.com/skypediacode/fastmd"),
          QStringLiteral(":/licenses/fastmd-MIT.txt") },
        { QStringLiteral("Qt"), QString::fromLatin1(qVersion()), QStringLiteral("GNU Lesser General Public License v3.0"),
          QStringLiteral("https://www.qt.io"),
          QStringLiteral(":/licenses/qt-LGPL-3.0.txt") },
        { QStringLiteral("Pandoc"), tr("bundled, see tools/pandoc"), QStringLiteral("GNU General Public License v2.0 (or later)"),
          QStringLiteral("https://pandoc.org"),
          QStringLiteral(":/licenses/pandoc-GPL-2.0.txt") },
        { QStringLiteral("md4c"), QStringLiteral("0.5.2"), QStringLiteral("MIT License"),
          QStringLiteral("https://github.com/mity/md4c"),
          QStringLiteral(":/licenses/md4c-MIT.txt") },
        { QStringLiteral("KaTeX"), tr("bundled"), QStringLiteral("MIT License"),
          QStringLiteral("https://katex.org"),
          QStringLiteral(":/licenses/katex-MIT.txt") },
    };

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    m_list = new QListWidget(this);
    m_list->setFixedWidth(160);
    for (const Component& c : m_components)
        m_list->addItem(c.name);
    mainLayout->addWidget(m_list);

    auto* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(8);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setTextFormat(Qt::RichText);
    m_metaLabel->setOpenExternalLinks(true);
    m_metaLabel->setWordWrap(true);
    rightLayout->addWidget(m_metaLabel);

    m_licenseText = new QTextBrowser(this);
    m_licenseText->setReadOnly(true);
    m_licenseText->setLineWrapMode(QTextEdit::NoWrap);
    rightLayout->addWidget(m_licenseText, 1);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    rightLayout->addLayout(btnLayout);

    mainLayout->addLayout(rightLayout, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, &LicensesDialog::showComponent);
    m_list->setCurrentRow(0);
}

void LicensesDialog::showComponent(int index)
{
    if (index < 0 || index >= m_components.size())
        return;
    const Component& c = m_components.at(index);

    m_metaLabel->setText(
        tr("<b>%1</b> &nbsp;%2<br>"
           "License: %3<br>"
           "Homepage: <a href=\"%4\">%4</a>")
            .arg(c.name, c.version, c.license, c.homepage));

    QFile f(c.licenseResourcePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_licenseText->setPlainText(QString::fromUtf8(f.readAll()));
    } else {
        m_licenseText->setPlainText(tr("License text not available."));
    }
}
