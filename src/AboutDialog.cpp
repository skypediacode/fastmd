#include "AboutDialog.h"
#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
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

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About FastMD"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(480, 300);

    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;

#ifdef Q_OS_WIN
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

    auto* dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(28, 24, 28, 20);
    dialogLayout->setSpacing(0);

    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(QIcon(QStringLiteral(":/fastmd.ico")).pixmap(72, 72));
    logoLabel->setAlignment(Qt::AlignCenter);
    dialogLayout->addWidget(logoLabel);
    dialogLayout->addSpacing(8);

    auto* titleLabel = new QLabel(this);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setText(tr("<b>FastMD</b> v%1").arg(QCoreApplication::applicationVersion()));
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    dialogLayout->addWidget(titleLabel);
    dialogLayout->addSpacing(0);

    auto* descLabel = new QLabel(tr("A lightweight Markdown & Text Editor built with C++20 and Qt 6."), this);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    QFont descFont = descLabel->font();
    descFont.setPointSize(9.5);
    descLabel->setFont(descFont);
    dialogLayout->addWidget(descLabel);
    dialogLayout->addSpacing(8);

    auto* copyrightLabel = new QLabel(tr("Copyright © 2026 Skypedia"), this);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    QFont smallFont = copyrightLabel->font();
    smallFont.setPointSize(8.5);
    copyrightLabel->setFont(smallFont);
    dialogLayout->addWidget(copyrightLabel);
    dialogLayout->addSpacing(0);

    auto* licenseLabel = new QLabel(this);
    licenseLabel->setTextFormat(Qt::RichText);
    licenseLabel->setText(tr("This software is provided \"as is\", without warranty of any kind."));
    licenseLabel->setAlignment(Qt::AlignCenter);
    licenseLabel->setWordWrap(true);
    licenseLabel->setFont(smallFont);
    dialogLayout->addWidget(licenseLabel);
    dialogLayout->addSpacing(8);

    auto* projectLabel = new QLabel(this);
    projectLabel->setTextFormat(Qt::RichText);
    if (isDarkTheme) {
        projectLabel->setText(tr("<a href=\"%1\" style=\"color: #0098ff; text-decoration: underline;\">View on GitHub</a>").arg(Urls::GitHubRepo));
    } else {
        projectLabel->setText(tr("<a href=\"%1\">View on GitHub</a>").arg(Urls::GitHubRepo));
    }
    projectLabel->setAlignment(Qt::AlignCenter);
    projectLabel->setOpenExternalLinks(false);

    connect(projectLabel, &QLabel::linkActivated, this, [](const QString& link) {
        MainWindow::openUrl(link);
    });
    dialogLayout->addWidget(projectLabel);
    dialogLayout->addSpacing(8);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setFixedWidth(90);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    dialogLayout->addLayout(btnLayout);
}
