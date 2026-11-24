#include "UpdateProgressDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QCloseEvent>
#include <QtMath>

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

UpdateProgressDialog::UpdateProgressDialog(QWidget* parent, const QString& title,
                                            const QString& line1, const QString& line2,
                                            const QString& line3, bool determinate)
    : QDialog(parent)
{
    setWindowTitle(title);
    setWindowFlags((windowFlags() & ~Qt::WindowContextHelpButtonHint) & ~Qt::WindowCloseButtonHint);
    setWindowModality(Qt::ApplicationModal);

    applyTitleBarTheme();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(8);
    // Size the dialog to exactly fit its content (native Win11 dialogs are
    // fixed-size and content-driven, not stretched with leftover space) and
    // lock it non-resizable at that size.
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, determinate ? 100 : 0);
    m_progressBar->setTextVisible(determinate);
    // A determinate progress bar also paints its percentage text. A 4 px
    // bar leaves no vertical room for the text and clips the glyphs.
    m_progressBar->setFixedHeight(20);
    mainLayout->addWidget(m_progressBar);

    auto* label1 = new QLabel(line1, this);
    QFont boldFont = label1->font();
    boldFont.setBold(true);
    label1->setFont(boldFont);
    mainLayout->addWidget(label1);

    if (!line2.isEmpty()) {
        auto* label2 = new QLabel(line2, this);
        label2->setWordWrap(true);
        label2->setFixedWidth(300);
        // QLabel's wrapped height is not always propagated through a fixed
        // width item while the dialog is being constrained to its content.
        // Reserve the full wrapped height so line3 cannot overlap the second
        // line of the installation guidance.
        label2->setFixedHeight(label2->heightForWidth(label2->width()));
        label2->setStyleSheet(QStringLiteral("color: palette(mid);"));
        mainLayout->addWidget(label2);
    }

    if (!line3.isEmpty()) {
        auto* label3 = new QLabel(line3, this);
        label3->setStyleSheet(QStringLiteral("color: palette(mid);"));
        mainLayout->addWidget(label3);
    }
}

void UpdateProgressDialog::setProgress(double progress)
{
    if (!m_progressBar || m_progressBar->maximum() == 0)
        return;

    m_progressBar->setValue(qBound(0, qRound(progress * 100.0), 100));
}

void UpdateProgressDialog::dismiss()
{
    if (!isVisible() && m_dismissRequested)
        return;

    m_dismissRequested = true;
    close();
}

void UpdateProgressDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
}

void UpdateProgressDialog::closeEvent(QCloseEvent* event)
{
    // Only dismiss() (which sets the flag first) may actually close this
    // dialog -- the user has no close box, but Alt+F4/system-menu close
    // still deliver a QCloseEvent, which must be blocked.
    if (!m_dismissRequested) {
        event->ignore();
        return;
    }
    event->accept();
}

void UpdateProgressDialog::reject()
{
    // Block Escape-driven dismissal. QDialog::reject() hides the dialog
    // directly (it doesn't go through closeEvent), so it needs its own guard.
    if (m_dismissRequested)
        QDialog::reject();
}

void UpdateProgressDialog::applyTitleBarTheme()
{
#ifdef Q_OS_WIN
    const bool isDarkTheme = (parentWidget() ? parentWidget()->palette() : palette())
                                 .color(QPalette::Window).lightness() < 128;
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
