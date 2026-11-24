#include "ShortcutsDialog.h"
#include "MainWindow.h"
#include "FastScrollBar.h"
#include "CommandShortcutRegistry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>
#include <QAction>
#include <QMenuBar>
#include "NativeTreeContainer.h"

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

ShortcutsDialog::ShortcutsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(520, 480);

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

    auto* dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(16, 16, 16, 16);
    dialogLayout->setSpacing(12);

    m_tree = new QTreeWidget();
    m_tree->setVerticalScrollBar(new FastScrollBar(Qt::Vertical, m_tree));
    m_tree->setHorizontalScrollBar(new FastScrollBar(Qt::Horizontal, m_tree));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Action"), tr("Shortcut")});
    m_tree->setIndentation(12);
    m_tree->setAnimated(false);
    m_tree->setFocusPolicy(Qt::NoFocus);
    // Categories use a bold font while shortcut rows use the regular font.
    // Without uniform heights, QTreeWidget asks the delegate for a separate
    // size per item, producing visibly uneven vertical rhythm in this flat
    // always-expanded list.
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(true);
    m_tree->setItemsExpandable(false); // keep categories always expanded

    auto* container = new NativeTreeContainer(m_tree, this);
    container->syncHeaderState();

    dialogLayout->addWidget(container, 1);

    // Set custom styling for QTreeWidget inside the shortcuts dialog to ensure headers match inactive tab colors (#e4e4e4 light / #2a2a2a dark)
    const bool isDark = palette().color(QPalette::Window).lightness() < 128;
    const QString headerBg = isDark ? QStringLiteral("#191a1b") : QStringLiteral("#e4e4e4");
    const QString headerColor = isDark ? QStringLiteral("#8c8c8c") : QStringLiteral("#555555");
    const QString gridColor = isDark ? QStringLiteral("#2a2b2c") : QStringLiteral("#d4d4d4");
    const QString borderColor = isDark ? QStringLiteral("#2a2b2c") : QStringLiteral("#bbbbbb");

    container->setStyleSheet(QString::asprintf(
        "QFrame#NativeTreeContainer { background-color: palette(base); border: 1px solid %s; border-radius: 4px; }"
        "QTreeWidget { border: none; border-bottom-left-radius: 4px; border-bottom-right-radius: 4px; background-color: transparent; alternate-background-color: palette(alternate-base); color: palette(text); }"
        "QHeaderView::section { background-color: %s; color: %s; border: none; border-bottom: 1px solid %s; border-right: 1px solid %s; padding: 5px 8px; font-size: 11.5px; font-weight: bold; }"
        "QHeaderView::section:last { border-right: none; }"
        "QHeaderView { background-color: %s; border-top-left-radius: 4px; border-top-right-radius: 4px; border-bottom: 1px solid %s; }"
        "QTreeWidget::item { padding: 4px 6px; }",
        qPrintable(borderColor), qPrintable(headerBg), qPrintable(headerColor), qPrintable(gridColor), qPrintable(gridColor), qPrintable(headerBg), qPrintable(gridColor)
    ));

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    dialogLayout->addLayout(btnLayout);

    populateShortcuts();
    m_tree->expandAll();
}

void ShortcutsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Restore geometry
    QSettings s;
    QByteArray geo = s.value(QStringLiteral("shortcutsDialog/geometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

void ShortcutsDialog::closeEvent(QCloseEvent* event)
{
    // Save geometry
    QSettings s;
    s.setValue(QStringLiteral("shortcutsDialog/geometry"), saveGeometry());
    QDialog::closeEvent(event);
}

void ShortcutsDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        accept();
    } else {
        QDialog::keyPressEvent(event);
    }
}

void ShortcutsDialog::addShortcutRow(QTreeWidget* tree, QTreeWidgetItem* categoryItem, const QString& actionName, const QKeySequence& shortcut)
{
    Q_UNUSED(tree);
    if (shortcut.isEmpty()) {
        return;
    }
    auto* item = new QTreeWidgetItem(categoryItem);
    item->setText(0, actionName);
    item->setText(1, shortcut.toString(QKeySequence::NativeText));
}

void ShortcutsDialog::populateShortcuts()
{
    MainWindow* mainWin = qobject_cast<MainWindow*>(parentWidget());
    if (!mainWin) {
        return;
    }

    // Helper to extract clean text from actions/menus
    auto cleanText = [](QString text) {
        text.remove(QChar('&'));
        text.remove(QStringLiteral("..."));
        text.remove(QStringLiteral("…"));
        return text.trimmed();
    };

    // Find actions by name/text
    auto findAction = [mainWin](const QString& text) -> QAction* {
        // Look in menuBar actions
        QList<QAction*> actions = mainWin->findChildren<QAction*>();
        for (QAction* act : actions) {
            if (act->text().contains(text, Qt::CaseInsensitive)) {
                return act;
            }
        }
        return nullptr;
    };

    // Category: File
    auto* fileCat = new QTreeWidgetItem(m_tree);
    fileCat->setText(0, tr("File"));
    QFont boldFont = fileCat->font(0);
    boldFont.setBold(true);
    fileCat->setFont(0, boldFont);

    if (auto* act = findAction(QStringLiteral("&New"))) {
        // New carries an alternate Ctrl+T; list every sequence it accepts.
        for (const QKeySequence& sc : act->shortcuts())
            addShortcutRow(m_tree, fileCat, cleanText(act->text()), sc);
    }
    if (auto* act = findAction(QStringLiteral("New &Window"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Open..."))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Open &Folder"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Save"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Save &As"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Save A&ll"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Close &Tab"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Reopen &Closed Tab"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Export &HTML"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Export &DOCX"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Export &PDF"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Print"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Preferences..."))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("E&xit"))) {
        addShortcutRow(m_tree, fileCat, cleanText(act->text()), act->shortcut());
    }

    // Category: Edit
    auto* editCat = new QTreeWidgetItem(m_tree);
    editCat->setText(0, tr("Edit"));
    editCat->setFont(0, boldFont);
    if (auto* act = findAction(QStringLiteral("&Undo"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Redo"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Cu&t"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Copy"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Paste"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Delete"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Select &All"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Auto Format Document"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    // Smart URL paste is internal to Paste action or has separate shortcut in editor
    if (auto* act = findAction(QStringLiteral("&Duplicate Line"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Delete L&ine"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Move Line Up"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Move Line Down"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Find…"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Replace…"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Find &Next"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Find Previous"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Go To Line"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Back"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Forward"))) {
        addShortcutRow(m_tree, editCat, cleanText(act->text()), act->shortcut());
    }

    // Category: View
    auto* viewCat = new QTreeWidgetItem(m_tree);
    viewCat->setText(0, tr("View"));
    viewCat->setFont(0, boldFont);
    if (auto* act = findAction(QStringLiteral("&Word Wrap"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Refresh"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Terminal"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Source Control"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Workspace Panel"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Focus File Search"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Preview Panel"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Reading Mode"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("&Notepad Mode"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("View as Mind Map"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Zoom In"))) {
        for (const QKeySequence& shortcut : act->shortcuts())
            addShortcutRow(m_tree, viewCat, cleanText(act->text()), shortcut);
    }
    if (auto* act = findAction(QStringLiteral("Zoom Out"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Reset Zoom"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Preview in Browser"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }
    if (auto* act = findAction(QStringLiteral("Command Palette"))) {
        addShortcutRow(m_tree, viewCat, cleanText(act->text()), act->shortcut());
    }

    // Terminal-local shortcuts are created lazily with TerminalPanel, so they
    // cannot be harvested from MainWindow's action tree while this dialog is
    // open. They deliberately share keys with the active-view zoom actions;
    // the terminal's WidgetWithChildrenShortcut context wins while it has focus.
    auto* terminalCat = new QTreeWidgetItem(m_tree);
    terminalCat->setText(0, tr("Terminal"));
    terminalCat->setFont(0, boldFont);
    for (const QKeySequence& sc : CommandShortcutRegistry::defaultShortcuts(CommandId::TerminalIncreaseFont))
        addShortcutRow(m_tree, terminalCat, tr("Increase Font Size"), sc);
    for (const QKeySequence& sc : CommandShortcutRegistry::defaultShortcuts(CommandId::TerminalDecreaseFont))
        addShortcutRow(m_tree, terminalCat, tr("Decrease Font Size"), sc);
    addShortcutRow(m_tree, terminalCat, tr("Reset Font Size"),
                   CommandShortcutRegistry::defaultShortcut(CommandId::TerminalResetFont));

    // Category: Editor
    auto* editorCat = new QTreeWidgetItem(m_tree);
    editorCat->setText(0, tr("Editor"));
    editorCat->setFont(0, boldFont);

    // Helper to query from formatActions or toolbar actions
    QList<QAction*> allActions = mainWin->findChildren<QAction*>();
    for (QAction* act : allActions) {
        QString text = cleanText(act->text());
        if (text == tr("Bold")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Italic")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Strikethrough")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Highlight")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Subscript")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Superscript")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 1")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 2")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 3")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 4")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 5")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Heading 6")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Bullet list")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Numbered list")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Checklist")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Blockquote")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Code Block")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Inline math")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Block math")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Table")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Insert Inline Link")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Insert Reference Link")) addShortcutRow(
            m_tree, editorCat, tr("Insert Numbered Reference Link"), act->shortcut());
        else if (text == tr("Link")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Image")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        // cleanText() above removes Qt mnemonic markers, so compare against the
        // full visible Date & Time submenu labels. These no longer carry a
        // shortcut -- F5/Shift+F5 became the canonical Refresh binding
        // (View > Refresh).
        else if (text == tr("Short Format")) addShortcutRow(
            m_tree, editorCat, tr("Date & Time (Short Format)"), act->shortcut());
        else if (text == tr("Long Format")) addShortcutRow(
            m_tree, editorCat, tr("Date & Time (Long Format)"), act->shortcut());
        else if (text == tr("Footnote")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Named Footnote...")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Horizontal Rule")) addShortcutRow(m_tree, editorCat, text, act->shortcut());
        else if (text == tr("Page Break")) {
            for (const QKeySequence& shortcut : act->shortcuts())
                addShortcutRow(m_tree, editorCat, text, shortcut);
        }
        else if (text == tr("Markdown") && act->isCheckable()) {
            addShortcutRow(m_tree, editorCat, tr("Switch Editor Mode"), act->shortcut());
        }
    }
}
