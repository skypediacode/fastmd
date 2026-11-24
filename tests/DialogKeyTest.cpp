// Guards the dialog action-key contract from src/ThemedMessageBox.h:
// bindDialogKey() must express each plain-letter shortcut as the button's own
// Qt mnemonic (so the letter is underlined and Alt+letter works too), and
// QMessageBox's own mnemonic handling must still click the button for an
// unmodified key press without any separate QShortcut.

#include "ThemedMessageBox.h"

#include <QApplication>
#include <QPushButton>
#include <QStyle>
#include <QTest>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

QString mnemonicFor(const QString& text, Qt::Key key)
{
    ThemedMessageBox box;
    QPushButton* button = box.addButton(text, QMessageBox::AcceptRole);
    bindDialogKey(&box, button, key);
    return button->text();
}

void testMnemonicPlacement()
{
    CHECK(mnemonicFor(QStringLiteral("Close All"), Qt::Key_A) == QStringLiteral("Close &All"));
    CHECK(mnemonicFor(QStringLiteral("Keep Existing"), Qt::Key_E) == QStringLiteral("Keep &Existing"));
    CHECK(mnemonicFor(QStringLiteral("Keep Dragged"), Qt::Key_D) == QStringLiteral("Keep &Dragged"));
    CHECK(mnemonicFor(QStringLiteral("Decide Later"), Qt::Key_L) == QStringLiteral("Decide &Later"));
    CHECK(mnemonicFor(QStringLiteral("Reload from Disk"), Qt::Key_R) == QStringLiteral("&Reload from Disk"));
    CHECK(mnemonicFor(QStringLiteral("Cancel"), Qt::Key_C) == QStringLiteral("&Cancel"));
    // An existing mnemonic is moved, never doubled; a literal "&&" survives.
    CHECK(mnemonicFor(QStringLiteral("&Yes"), Qt::Key_Y) == QStringLiteral("&Yes"));
    CHECK(mnemonicFor(QStringLiteral("Yes to &All"), Qt::Key_Y) == QStringLiteral("&Yes to All"));
    CHECK(mnemonicFor(QStringLiteral("Save && Close"), Qt::Key_C) == QStringLiteral("Save && &Close"));
    // Falls back to a lower-case occurrence, then to the Windows suffix form.
    CHECK(mnemonicFor(QStringLiteral("Reset"), Qt::Key_S) == QStringLiteral("Re&set"));
    CHECK(mnemonicFor(QStringLiteral("Fermer"), Qt::Key_Q) == QStringLiteral("Fermer (&Q)"));
}

void testUnderlineIsAlwaysVisible()
{
    // The app always runs under a stylesheet, which wraps the button style.
    qApp->setStyleSheet(QStringLiteral("QPushButton { padding: 4px 12px; }"));
    ThemedMessageBox box;
    QPushButton* button = box.addButton(QStringLiteral("Close All"), QMessageBox::AcceptRole);
    bindDialogKey(&box, button, Qt::Key_A);
    CHECK(button->shortcut() == QKeySequence(Qt::ALT | Qt::Key_A));
    CHECK(button->style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, button) == 1);
}

QAbstractButton* clickedAfterKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    static ThemedMessageBox* box = nullptr;
    delete box;
    box = new ThemedMessageBox;
    auto* closeAll = box->addButton(QStringLiteral("Close All"), QMessageBox::DestructiveRole);
    auto* cancel = box->addButton(QMessageBox::Cancel);
    box->setDefaultButton(cancel);
    bindDialogKey(box, closeAll, Qt::Key_A);
    bindDialogKey(box, cancel, Qt::Key_C);
    box->show();
    QTest::keyClick(box->defaultButton(), key, modifiers);
    QTest::qWait(250);
    return box->clickedButton();
}

void testKeysReachTheRightButton()
{
    QAbstractButton* clicked = clickedAfterKey(Qt::Key_A);
    CHECK(clicked && clicked->text() == QStringLiteral("Close &All"));
    clicked = clickedAfterKey(Qt::Key_C);
    CHECK(clicked && clicked->text() == QStringLiteral("&Cancel"));
    // Escape and Enter keep QMessageBox's own escape/default button behavior.
    clicked = clickedAfterKey(Qt::Key_Escape);
    CHECK(clicked && clicked->text() == QStringLiteral("&Cancel"));
    clicked = clickedAfterKey(Qt::Key_Return);
    CHECK(clicked && clicked->text() == QStringLiteral("&Cancel"));
    // Ctrl+A is not a mnemonic and must not close the terminals.
    clicked = clickedAfterKey(Qt::Key_A, Qt::ControlModifier);
    CHECK(!clicked);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testMnemonicPlacement();
    testUnderlineIsAlwaysVisible();
    testKeysReachTheRightButton();

    if (g_failures == 0)
        std::printf("DialogKeyTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
