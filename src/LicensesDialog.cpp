#include "LicensesDialog.h"
#include "MainWindow.h"
#include "BundledToolVersions.h"

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
#include <QCoreApplication>

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

LicensesDialog::LicensesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Third-Party Licenses"));
    resize(680, 460);

#ifdef Q_OS_WIN
    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL enabled = isDarkTheme ? TRUE : FALSE;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))) ||
        SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)))) {
        const COLORREF captionColor = isDarkTheme ? RGB(0x20, 0x21, 0x22) : RGB(0xf3, 0xf3, 0xf3);
        const COLORREF textColor = isDarkTheme ? RGB(0xd4, 0xd4, 0xd4) : RGB(0x20, 0x20, 0x20);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif

    m_components = {
        { QStringLiteral("Qt"), QString::fromLatin1(qVersion()), QStringLiteral("GNU Lesser General Public License v3.0"),
          QStringLiteral("https://www.qt.io"),
          QStringLiteral(":/licenses/qt-LGPL-3.0.txt") },
        { QStringLiteral("Pandoc"), QStringLiteral(FASTMD_PANDOC_VERSION), QStringLiteral("GNU General Public License v2.0 (or later)"),
          QStringLiteral("https://pandoc.org"),
          QStringLiteral(":/licenses/pandoc-GPL-2.0.txt") },
        { QStringLiteral("md4c"), QStringLiteral("0.5.2"), QStringLiteral("MIT License"),
          QStringLiteral("https://github.com/mity/md4c"),
          QStringLiteral(":/licenses/md4c-MIT.txt") },
        { QStringLiteral("AnyDoc"), tr("%1 (bundled wrapper)").arg(QStringLiteral(FASTMD_ANYDOC_VERSION)), QStringLiteral("MIT License"),
          QStringLiteral("https://github.com/firecrawl/anydoc"),
          QStringLiteral(":/licenses/anydoc-MIT.txt") },
        { QStringLiteral("KaTeX"), tr("bundled"), QStringLiteral("MIT License"),
          QStringLiteral("https://katex.org"),
          QStringLiteral(":/licenses/katex-MIT.txt") },
        { QStringLiteral("highlight.js"), tr("bundled"), QStringLiteral("BSD 3-Clause License"),
          QStringLiteral("https://highlightjs.org"),
          QStringLiteral(":/licenses/highlightjs-BSD-3-Clause.txt") },
        { QStringLiteral("Mermaid"), QStringLiteral("11.16.0"), QStringLiteral("MIT License"),
          QStringLiteral("https://mermaid.js.org"),
          QStringLiteral(":/licenses/mermaid-MIT.txt") },
        { QStringLiteral("Markmap"), QStringLiteral("0.18.12"), QStringLiteral("MIT License"),
          QStringLiteral("https://markmap.js.org"),
          QStringLiteral(":/licenses/markmap-MIT.txt") },
        { QStringLiteral("Markmap Toolbar"), QStringLiteral("0.18.12"), QStringLiteral("MIT License"),
          QStringLiteral("https://markmap.js.org"),
          QStringLiteral(":/licenses/markmap-MIT.txt") },
        { QStringLiteral("D3"), QStringLiteral("7.8.5"), QStringLiteral("ISC License"),
          QStringLiteral("https://d3js.org"),
          QStringLiteral(":/licenses/d3-ISC.txt") },
        { QStringLiteral("KSyntaxHighlighting"), QStringLiteral("6.28.0"), QStringLiteral("MIT License"),
          QStringLiteral("https://invent.kde.org/frameworks/syntax-highlighting"),
          QStringLiteral(":/licenses/ksyntaxhighlighting-MIT.txt") },
        { QStringLiteral("xterm.js"), tr("bundled"), QStringLiteral("MIT License"),
          QStringLiteral("https://xtermjs.org"),
          QStringLiteral(":/licenses/xterm-MIT.txt") },
        { QStringLiteral("Material Icons"), tr("bundled"), QStringLiteral("Apache License 2.0"),
          QStringLiteral("https://fonts.google.com/icons"),
          QStringLiteral(":/licenses/materialicons-Apache-2.0.txt") },
        { QStringLiteral("Codicons"), QStringLiteral("0.0.46-24"), QStringLiteral("Creative Commons Attribution 4.0"),
          QStringLiteral("https://github.com/microsoft/vscode-codicons"),
          QStringLiteral(":/licenses/codicons-CC-BY-4.0.txt") },
        { QStringLiteral("Microsoft Edge WebView2"), tr("system runtime"), QStringLiteral("Microsoft SDK Terms"),
          QStringLiteral("https://developer.microsoft.com/microsoft-edge/webview2/"),
          QStringLiteral(":/licenses/webview2-SDK.txt") },

    };

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    m_list = new QListWidget(this);
    m_list->setFixedWidth(200);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
    if (isDarkTheme) {
        m_metaLabel->setText(
            tr("<b>%1</b> &nbsp;%2<br>"
               "License: %3<br>"
               "Homepage: <a href=\"%4\" style=\"color: #0098ff; text-decoration: underline;\">%4</a>")
                .arg(c.name, c.version, c.license, c.homepage));
    } else {
        m_metaLabel->setText(
            tr("<b>%1</b> &nbsp;%2<br>"
               "License: %3<br>"
               "Homepage: <a href=\"%4\">%4</a>")
                .arg(c.name, c.version, c.license, c.homepage));
    }

    QFile f(c.licenseResourcePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_licenseText->setPlainText(QString::fromUtf8(f.readAll()));
    } else {
        m_licenseText->setPlainText(tr("License text not available."));
    }
}
