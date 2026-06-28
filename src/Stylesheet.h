#pragma once
#include <QString>

// ---------------------------------------------------------------------------
// All QSS lives here.  applyTheme() selects the right sheet.
// ---------------------------------------------------------------------------
namespace AppStyle {

// shared: scrollbars + splitter (toolbar colour is per-theme)
inline QString common()
{
    return QStringLiteral(R"(

/* ─── Scrollbars (thin, modern) ─────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    border: none;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: rgba(130,130,130,0.38);
    border-radius: 5px;
    min-height: 28px;
    margin: 2px 2px;
}
QScrollBar::handle:vertical:hover  { background: rgba(130,130,130,0.60); }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical      { height: 0; border: none; }
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical      { background: transparent; }

QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    border: none;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: rgba(130,130,130,0.38);
    border-radius: 5px;
    min-width: 28px;
    margin: 2px 2px;
}
QScrollBar::handle:horizontal:hover { background: rgba(130,130,130,0.60); }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal     { width: 0; border: none; }
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal     { background: transparent; }

/* ─── Splitter ──────────────────────────────────────────────── */
QSplitter::handle:horizontal { width:  1px; }
QSplitter::handle:vertical   { height: 1px; }
)");
}

inline QString light()
{
    return common() + QStringLiteral(R"(
/* ─── Window / global ───────────────────────────────────────── */
QMainWindow   { background: #f0f0f0; }
QSplitter::handle { background: #d8d8d8; }

/* ─── Tooltip ───────────────────────────────────────────────── */
QToolTip {
    background: #ffffff;
    color: #222222;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 4px 6px;
    font-size: 13px;
    font-family: "Segoe UI";
}

/* ─── Toolbar (light, Notepad++ style) ──────────────────────── */
QToolBar {
    background: #f3f3f3;
    border: none;
    border-bottom: 1px solid #d4d4d4;
    spacing: 1px;
    padding: 3px 6px;
}
QToolBar::separator {
    width: 1px;
    background: #c8c8c8;
    margin: 4px 5px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    color: #333333;
}
QToolButton:hover {
    background: rgba(0,0,0,0.08);
    border-color: rgba(0,0,0,0.12);
}
QToolButton:pressed {
    background: rgba(0,0,0,0.14);
}
QToolButton:checked {
    background: rgba(0,120,212,0.13);
    border-color: rgba(0,120,212,0.30);
}
QToolButton[modeToggle="true"] {
    min-width: 150px;
    max-width: 150px;
    padding: 4px 12px;
    font-weight: 600;
    font-size: 16px;
}
QToolButton[modeToggle="true"]::menu-indicator {
    image: none;
    width: 0px;
}
QToolButton[modeToggle="true"]:hover {
    background: rgba(0,0,0,0.10);
    border-color: rgba(0,0,0,0.16);
}
QToolButton[modeToggle="true"]:checked {
    background: rgba(0,120,212,0.18);
    border-color: rgba(0,120,212,0.36);
}

/* ─── Menu bar ──────────────────────────────────────────────── */
QMenuBar {
    background: #f0f0f0;
    color: #333333;
    border-bottom: 1px solid #e0e0e0;
    padding: 2px 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: rgba(0,0,0,0.08); }

/* ─── Menus ─────────────────────────────────────────────────── */
QMenu {
    background: #ffffff;
    border: 1px solid #d4d4d4;
    border-radius: 6px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item           { padding: 6px 24px 6px 14px; border-radius: 4px; color: #222; }
QMenu::item:selected  { background: #0078d4; color: #fff; }
QMenu::separator      { height: 1px; background: #e8e8e8; margin: 4px 8px; }

/* ─── Tabs (light) ──────────────────────────────────────────── */
QTabWidget::pane { border: none; border-top: 1px solid #c4c4c4; background: #ffffff; }
QTabWidget { background: #f0f0f0; }
QTabBar           { background: #f0f0f0; }
QTabBar::tab {
    background: #e4e4e4;
    color: #555555;
    padding: 0px 20px 8px 24px;
    min-height: 28px;
    border: 1px solid transparent;
    border-right: 1px solid #c4c4c4;
    margin-top: 0px;
    font-size: 14px;
    font-family: "Segoe UI";
    min-width: 140px;
    max-height: 28px;
}
QTabBar::tab:selected {
    background: #ffffff;
    color: #111111;
    border: 1px solid #a0a0a0;
    border-top: 2px solid #f39c12;
    border-bottom: 1px solid #ffffff;
    margin-top: 0px;
    margin-bottom: -1px;
    padding-bottom: 4px;
}
QTabBar::tab:hover:!selected { background: #d4d4d4; color: #333333; }

/* ─── Status bar ────────────────────────────────────────────── */
QStatusBar {
    background: #f0f0f0;
    color: #555555;
    border-top: 1px solid #d4d4d4;
    font-size: 12px;
    font-family: "Segoe UI";
}
QStatusBar QLabel {
    color: #555555;
    padding: 2px 10px;
    border-right: 1px solid #d4d4d4;
}

/* ─── Content widgets ───────────────────────────────────────── */
QPlainTextEdit {
    border: none;
    background: #ffffff;
    color: #222222;
    selection-background-color: #add6ff;
    selection-color: #000000;
    font-family: "Consolas", "JetBrains Mono", "IBM Plex Mono", "Cascadia Code", "Cascadia Mono", "Courier New", monospace;
}
QTextBrowser  { border: none; background: #f9f9f9; }

/* ─── Dialogs & form widgets ─────────────────────────────────── */
QDialog { background: #f5f5f5; }
QLabel  { color: #333; font-size: 13px; font-family: "Segoe UI"; }

QLineEdit {
    background: #ffffff;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 5px 9px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222;
    selection-background-color: #0078d4;
}
QLineEdit:focus { border-color: #0078d4; }

QPushButton {
    background: #f0f0f0;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 4px 18px 6px 18px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222;
    min-width: 72px;
}
QPushButton:hover   { background: #e4e4e4; border-color: #aaaaaa; }
QPushButton:pressed { background: #d8d8d8; }
QPushButton:default {
    background: #0078d4;
    border-color: #0078d4;
    color: white;
}
QPushButton:default:hover { background: #106ebe; }

QCheckBox {
    font-size: 13px;
    font-family: "Segoe UI";
    spacing: 7px;
    color: #333;
}
QCheckBox::indicator {
    width: 15px; height: 15px;
    border: 1px solid #bbbbbb;
    border-radius: 3px;
    background: white;
}
QCheckBox::indicator:checked {
    background: #0078d4;
    border-color: #0078d4;
    image: url(:/icons/checkmark.svg);
}

/* ─── Workspace tree ────────────────────────────────────────── */
QTreeView {
    background: #ffffff;
    color: #222222;
    border: none;
    show-decoration-selected: 1;
}
QTreeView::item { padding: 2px 4px; }
QTreeView::item:hover { background: rgba(0,0,0,0.06); }
QTreeView::item:selected { background: #0078d4; color: #ffffff; }
QTreeView::item:selected:!active { background: #cce4f7; color: #222222; }
QTreeView::branch { background: #ffffff; }
)");
}

inline QString dark()
{
    return common() + QStringLiteral(R"(
/* ─── Window / global ───────────────────────────────────────── */
QMainWindow   { background: #1e1e1e; }
QSplitter::handle { background: #3c3c3c; }

/* ─── Tooltip ───────────────────────────────────────────────── */
QToolTip {
    background: #2d2d2d;
    color: #cccccc;
    border: 1px solid #555555;
    border-radius: 4px;
    padding: 4px 6px;
    font-size: 13px;
    font-family: "Segoe UI";
}

/* ─── Toolbar (dark theme) ───────────────────────────────────── */
QToolBar {
    background: #2d2d2d;
    border: none;
    border-bottom: 1px solid #1a1a1a;
    spacing: 1px;
    padding: 3px 6px;
}
QToolBar::separator {
    width: 1px;
    background: #484848;
    margin: 4px 5px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    color: #cccccc;
}
QToolButton:hover {
    background: rgba(255,255,255,0.10);
    border-color: rgba(255,255,255,0.12);
}
QToolButton:pressed {
    background: rgba(255,255,255,0.16);
}
QToolButton:checked {
    background: rgba(0,120,212,0.28);
    border-color: rgba(0,120,212,0.45);
}
QToolButton[modeToggle="true"] {
    min-width: 150px;
    max-width: 150px;
    padding: 4px 12px;
    font-weight: 600;
    font-size: 16px;
}
QToolButton[modeToggle="true"]::menu-indicator {
    image: none;
    width: 0px;
}
QToolButton[modeToggle="true"]:hover {
    background: rgba(255,255,255,0.14);
    border-color: rgba(255,255,255,0.18);
}
QToolButton[modeToggle="true"]:checked {
    background: rgba(0,120,212,0.34);
    border-color: rgba(0,120,212,0.52);
}

/* ─── Menu bar ──────────────────────────────────────────────── */
QMenuBar {
    background: #2d2d2d;
    color: #cccccc;
    border-bottom: 1px solid #1a1a1a;
    padding: 2px 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: rgba(255,255,255,0.09); }

/* ─── Menus ─────────────────────────────────────────────────── */
QMenu {
    background: #2d2d2d;
    border: 1px solid #3f3f3f;
    border-radius: 6px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item           { padding: 6px 24px 6px 14px; border-radius: 4px; color: #cccccc; }
QMenu::item:selected  { background: #0078d4; color: #fff; }
QMenu::separator      { height: 1px; background: #3c3c3c; margin: 4px 8px; }

/* ─── Tabs ──────────────────────────────────────────────────── */
QTabWidget { background: #252526; }
QTabWidget::pane { border: none; border-top: 1px solid #3c3c3c; background: #1e1e1e; }
QTabBar           { background: #252526; }
QTabBar::tab {
    background: #2a2a2a;
    color: #888888;
    padding: 0px 20px 8px 24px;
    min-height: 28px;
    border: 1px solid transparent;
    border-right: 1px solid #1a1a1a;
    margin-top: 0px;
    font-size: 14px;
    font-family: "Segoe UI";
    min-width: 140px;
    max-height: 24px;
}
QTabBar::tab:selected {
    background: #1e1e1e;
    color: #ffffff;
    border: 1px solid #111111;
    border-top: 2px solid #f39c12;
    border-bottom: 1px solid #1e1e1e;
    margin-top: 0px;
    margin-bottom: -1px;
    padding-bottom: 4px;
}
QTabBar::tab:hover:!selected { background: #333333; color: #aaaaaa; }

/* ─── Status bar ────────────────────────────────────────────── */
QStatusBar {
    background: #252526;
    color: #b9bec4;
    border-top: 1px solid #33373a;
    font-size: 12px;
    font-family: "Segoe UI";
}
QStatusBar QLabel {
    color: #b9bec4;
    padding: 2px 10px;
    border-right: 1px solid rgba(255,255,255,0.10);
}

/* ─── Content widgets ───────────────────────────────────────── */
QPlainTextEdit {
    border: none;
    background: #1e1e1e;
    color: #d4d4d4;
    selection-background-color: #264f78;
    selection-color: #ffffff;
    font-family: "Consolas", "JetBrains Mono", "IBM Plex Mono", "Cascadia Code", "Cascadia Mono", "Courier New", monospace;
}
QTextBrowser  { border: none; background: #252526; }

/* ─── Dialogs & form widgets ─────────────────────────────────── */
QDialog { background: #2d2d2d; }
QLabel  { color: #cccccc; font-size: 13px; font-family: "Segoe UI"; }

QLineEdit {
    background: #3c3c3c;
    border: 1px solid #555555;
    border-radius: 4px;
    padding: 5px 9px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #cccccc;
    selection-background-color: #264f78;
}
QLineEdit:focus { border-color: #0078d4; }

QPushButton {
    background: #3c3c3c;
    border: 1px solid #555555;
    border-radius: 4px;
    padding: 4px 18px 6px 18px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #cccccc;
    min-width: 72px;
}
QPushButton:hover   { background: #484848; border-color: #666666; }
QPushButton:pressed { background: #525252; }
QPushButton:default {
    background: #0078d4;
    border-color: #0078d4;
    color: white;
}
QPushButton:default:hover { background: #106ebe; }

QCheckBox {
    font-size: 13px;
    font-family: "Segoe UI";
    spacing: 7px;
    color: #cccccc;
}
QCheckBox::indicator {
    width: 15px; height: 15px;
    border: 1px solid #555555;
    border-radius: 3px;
    background: #3c3c3c;
}
QCheckBox::indicator:checked {
    background: #0078d4;
    border-color: #0078d4;
    image: url(:/icons/checkmark.svg);
}

/* ─── Workspace tree ────────────────────────────────────────── */
QTreeView {
    background: #252526;
    color: #d4d4d4;
    border: none;
    show-decoration-selected: 1;
}
QTreeView::item { padding: 2px 4px; }
QTreeView::item:hover { background: rgba(255,255,255,0.08); }
QTreeView::item:selected { background: #264f78; color: #ffffff; }
QTreeView::item:selected:!active { background: #2d4a6b; color: #cccccc; }
QTreeView::branch { background: #252526; }
)");
}

} // namespace AppStyle
