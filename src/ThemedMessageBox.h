#pragma once
#include "ApplicationTheme.h"
#include <QMessageBox>
#include <QWidget>
#include <QApplication>
#include <QPalette>
#include <QTimer>
#include <QShowEvent>
#include <QGridLayout>
#include <QLabel>
#include <QFont>
#include <QMargins>
#include <QAbstractButton>
#include <QProxyStyle>

// Windows hides mnemonic underlines until Alt is pressed. Dialog action keys
// work without Alt, so their underline must be visible without it too.
class AlwaysUnderlineMnemonicStyle : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == QStyle::SH_UnderlineShortcut)
            return 1;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

// Makes `key` the button's Qt mnemonic. QMessageBox::keyPressEvent() already
// clicks the button whose mnemonic matches an unmodified key press, and the
// button itself registers Alt+key, so no separate QShortcut is needed.
// Prefers an upper-case occurrence (a word initial); a translation lacking
// the letter gets the Windows " (&X)" suffix convention.
inline void bindDialogKey(QMessageBox* dialog, QAbstractButton* button, Qt::Key key)
{
    if (!dialog || !button || key < Qt::Key_A || key > Qt::Key_Z) return;
    const QString source = button->text();
    QString text;
    text.reserve(source.size() + 5);
    for (qsizetype i = 0; i < source.size(); ++i) {
        if (source.at(i) != QLatin1Char('&')) {
            text += source.at(i);
        } else if (i + 1 < source.size() && source.at(i + 1) == QLatin1Char('&')) {
            text += QStringLiteral("&&");
            ++i;
        }
    }
    const QChar letter(static_cast<char16_t>(key));
    qsizetype index = text.indexOf(letter, 0, Qt::CaseSensitive);
    if (index < 0)
        index = text.indexOf(letter, 0, Qt::CaseInsensitive);
    if (index >= 0)
        text.insert(index, QLatin1Char('&'));
    else
        text += QStringLiteral(" (&") + letter + QLatin1Char(')');
    button->setText(text);

    static QStyle* const mnemonicStyle = [] {
        auto* style = new AlwaysUnderlineMnemonicStyle;
        style->setParent(qApp);
        return style;
    }();
    button->setStyle(mnemonicStyle);
}

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

// A QMessageBox whose native title bar follows the app's current light/dark
// theme (plain QMessageBox always shows a white Windows title bar, which
// stands out sharply against a dark-themed dialog body). Use this — or the
// themedWarning/themedQuestion/themedInformation/themedCritical free
// functions below — instead of QMessageBox's own static convenience
// functions anywhere a message box is shown.
class ThemedMessageBox : public QMessageBox {
public:
    // Follows the application theme. It is not inferred from the parent's
    // palette: a parent with its own stylesheet (the Terminal panel paints its
    // own background) would hand the title bar a theme the body never gets.
    explicit ThemedMessageBox(QWidget* parent = nullptr)
        : QMessageBox(parent)
        , m_dark(ApplicationTheme::isDark())
    {
    }

    // For a dialog owned by a surface with its own theme (a detached Terminal
    // window). A theme other than the application's gets that theme's own
    // application stylesheet and palette on this dialog alone, so title bar,
    // body and controls stay consistent without touching the app theme.
    void setDarkTheme(bool dark)
    {
        m_dark = dark;
        if (dark == ApplicationTheme::isDark()) {
            setStyleSheet(QString());
            setPalette(QPalette());
        } else {
            setPalette(ApplicationTheme::palette(dark));
            setStyleSheet(ApplicationTheme::styleSheet(dark));
        }
        if (isVisible())
            applyTitleBarTheme();
    }
    bool isDarkTheme() const { return m_dark; }

    // Widens the left/right content margins and gives the primary text a
    // larger point size than the informative text (Windows Explorer's
    // rename-collision prompt layout). Call after setText()/
    // setInformativeText() so both internal labels already exist.
    void polishContentLayout()
    {
        if (auto* grid = qobject_cast<QGridLayout*>(layout())) {
            const QMargins m = grid->contentsMargins();
            grid->setContentsMargins(m.left() + 12, m.top() + 4, m.right() + 20, m.bottom() + 4);
            grid->setHorizontalSpacing(qMax(grid->horizontalSpacing(), 0));
            grid->setVerticalSpacing(qMax(grid->verticalSpacing(), 8));
        }
        if (auto* mainLabel = findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) {
            // The app-wide stylesheet's "QLabel { font-size: 13px; }" rule
            // always wins over QFont set via setFont(), so the larger size
            // must be applied as QSS too (id selector beats the type
            // selector regardless of declaration order).
            mainLabel->setStyleSheet(QStringLiteral("QLabel#qt_msgbox_label { font-size: 14px; font-weight: 500; }"));
        }
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QMessageBox::showEvent(event);
        applyTitleBarTheme();
        // DWM sometimes ignores the attribute if set before the native frame
        // is fully realized; reapplying once more after the first paint
        // makes this reliable (matches the pattern used elsewhere in the app).
        QTimer::singleShot(0, this, [this]() {
            if (isVisible()) applyTitleBarTheme();
        });
    }

private:
    bool m_dark;

    void applyTitleBarTheme()
    {
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const BOOL enabled = m_dark ? TRUE : FALSE;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))) ||
            SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)))) {
            const COLORREF captionColor = m_dark ? RGB(0x20, 0x21, 0x22) : RGB(0xf3, 0xf3, 0xf3);
            const COLORREF textColor = m_dark ? RGB(0xd4, 0xd4, 0xd4) : RGB(0x20, 0x20, 0x20);
            DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
            DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
#endif
    }
};

inline QMessageBox::StandardButton themedMessageBox(
    QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
    const QString& informativeText = QString())
{
    ThemedMessageBox box(parent);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    if (!informativeText.isEmpty())
        box.setInformativeText(informativeText);
    box.polishContentLayout();
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    // Common binary confirmations can be answered with one plain key.
    if (buttons.testFlag(QMessageBox::Yes) && buttons.testFlag(QMessageBox::No)) {
        bindDialogKey(&box, box.button(QMessageBox::Yes), Qt::Key_Y);
        bindDialogKey(&box, box.button(QMessageBox::No), Qt::Key_N);
    }
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

inline QMessageBox::StandardButton themedWarning(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return themedMessageBox(parent, QMessageBox::Warning, title, text, buttons, defaultButton);
}

inline QMessageBox::StandardButton themedQuestion(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No),
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
    const QString& informativeText = QString())
{
    return themedMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton, informativeText);
}

inline QMessageBox::StandardButton themedInformation(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return themedMessageBox(parent, QMessageBox::Information, title, text, buttons, defaultButton);
}

inline QMessageBox::StandardButton themedCritical(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return themedMessageBox(parent, QMessageBox::Critical, title, text, buttons, defaultButton);
}
