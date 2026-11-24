#pragma once
#include <QString>

#include "ThemePalette.h"

// ---------------------------------------------------------------------------
// All QSS lives here.  applyTheme() selects the right sheet.
// ---------------------------------------------------------------------------
namespace AppStyle {

// shared: scrollbars + splitter (toolbar colour is per-theme)
inline QString common()
{
    return QStringLiteral(R"(

/* ─── Status bar item frame ──────────────────────────────────── */
/* The native style (PE_FrameStatusBarItem) draws its own sunken frame
   around every widget added to QStatusBar, independent of and drawn on
   top of the themed "border-right" divider set on the QLabel/QToolButton
   segments below. That native frame ignores the app's palette in dark
   theme and renders as a bright, unthemed line. Suppressing it here is
   what lets the QSS-drawn separators be the only ones visible. */
QStatusBar::item { border: none; }

/* ─── Scrollbars (thin, modern) ─────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 12px;
    border: none;
    margin: 0;
}
QScrollBar::groove:vertical { background: transparent; border: none; margin: 0; }
QScrollBar::handle:vertical {
    background: rgba(130,130,130,0.38);
    border: none;
    border-radius: 5px;
    min-height: 28px;
    margin: 2px 2px;
}
QScrollBar::handle:vertical:hover   { background: rgba(130,130,130,0.60); }
QScrollBar::handle:vertical:pressed { background: rgba(130,130,130,0.80); }
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical      { background: transparent; border: none; height: 0px; width: 0px; image: none; }
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical { background: transparent; border: none; }

QScrollBar:horizontal {
    background: transparent;
    height: 12px;
    border: none;
    margin: 0;
}
QScrollBar::groove:horizontal { background: transparent; border: none; margin: 0; }
QScrollBar::handle:horizontal {
    background: rgba(130,130,130,0.38);
    border: none;
    border-radius: 5px;
    min-width: 28px;
    margin: 2px 2px;
}
QScrollBar::handle:horizontal:hover   { background: rgba(130,130,130,0.60); }
QScrollBar::handle:horizontal:pressed { background: rgba(130,130,130,0.80); }
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal     { background: transparent; border: none; width: 0px; height: 0px; image: none; }
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal { background: transparent; border: none; }

/* Style every arrow sub-control so Qt renders the whole scrollbar via the
   stylesheet.  If any sub-control is left unstyled, the native windowsvista
   style composites the groove and its light hatch bleeds into the sub-page. */
QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical,
QScrollBar::left-arrow:horizontal, QScrollBar::right-arrow:horizontal {
    background: transparent; width: 0; height: 0; border: none; image: none;
}
QScrollBar::corner { background: transparent; border: none; }

QSplitter::handle { image: none; border: none; }

/* ─── TabBar Scroller ───────────────────────────────────────── */
QTabBar::scroller { 
    width: 72px; 
}
QTabBar QToolButton {
    background: transparent;
    border: none;
    border-radius: 0px;
    margin: 0px;
    padding: 0px;
    min-height: 38px;
    max-height: 38px;
}
QTabBar QToolButton:hover {
    background: rgba(130, 130, 130, 0.2);
}
/* The overflow buttons are painted by TabBar (opaque canvas fill, rounded hover
   and pressed pill, centred chevron), so only their size is styled here: the
   38px toolbar minimum above would stretch them past the tab row. */
QTabBar QToolButton#ScrollLeftButton, QTabBar QToolButton#ScrollRightButton {
    min-height: 0px; max-height: 16777215px;
}
/* The close glyph's hover square is the button itself (TabBar sizes and places it
   from kCloseTabButtonSize); the 38px rule above would stretch it past the tab. */
QTabBar QToolButton#tabCloseButton { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; }
)");
}

inline QString chemistryPopupStyle(const QString& background, const QString& border,
                                   const QString& text, const QString& heading,
                                   const QString& separator, const QString& scrollHandle)
{
    return QStringLiteral(R"(
QWidget#chemistryPopup { background: transparent; border: none; }
QLabel#chemSectionHeading { color: %3; font-weight: 600; font-size: 11px; background: transparent; border: none; padding: 6px 2px 2px 2px; }
QFrame#chemSeparatorLine { background-color: %4; max-height: 1px; min-height: 1px; border: none; }
QToolButton#chemCompactBtn, QPushButton#chemFullBtn { color: %5; background: transparent; border: 1px solid transparent; border-radius: 4px; font-size: 14px; }
QToolButton#chemCompactBtn { padding: 6px 10px; }
QPushButton#chemFullBtn { padding: 7px 10px; text-align: left; }
QToolButton#chemCompactBtn:hover, QPushButton#chemFullBtn:hover { background: #0078d4; color: #ffffff; }
QToolButton#chemSvgRow { color: %5; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 8px 12px; }
QToolButton#chemSvgRow:hover { background: #0078d4; color: #ffffff; }
QScrollArea#chemScrollArea, QScrollArea#chemScrollArea > QWidget#qt_scrollarea_viewport, QWidget#chemContent { background: transparent; border: none; }
QWidget#chemistryPopup QScrollBar:vertical { width: 9px; background: transparent; margin: 0px; }
QWidget#chemistryPopup QScrollBar::handle:vertical { background: %6; border-radius: 4px; min-height: 24px; }
QWidget#chemistryPopup QScrollBar::add-line:vertical, QWidget#chemistryPopup QScrollBar::sub-line:vertical { height: 0px; }
QWidget#chemistryPopup QScrollBar::add-page:vertical, QWidget#chemistryPopup QScrollBar::sub-page:vertical { background: transparent; }
)").arg(background, border, heading, separator, text, scrollHandle);
}

// The Terminal panel's chrome (header, session list) is themed independently
// of the rest of the app: Terminal's Theme setting can force Light/Dark while
// the app keeps its own selected theme, so this is a standalone function
// (not folded into common()/light()/dark()) that TerminalPanel can also call
// directly to build a per-widget override stylesheet. light()/dark() call it
// with their own bool purely so the default "Follow App Theme" case, which
// never sets a widget-level override, still looks right out of the box.
inline QString terminalPanel(bool dark)
{
    if (dark) {
        return ThemePalette::resolve(QStringLiteral(R"(
/* ─── Terminal panel (dark) ──────────────────── */
/* Docked, the panel is the Terminal card; detached, it is the whole window. */
QWidget#terminalPanel { background: @surface@; border: 1px solid @border@; border-radius: 10px; }
QWidget#terminalPanel[detached="true"] { border: none; border-radius: 0px; }
QWidget#terminalHeader { background: transparent; border: none; }
QLabel#terminalSectionTitle { color: @text@; font-size: 12px; font-weight: 700; }
QLabel#terminalStatusLabel { color: @textSecondary@; font-size: 12px; }
QLabel#terminalStatusLabel[terminalError="true"] { color: #f1707a; }
QLabel#terminalFallbackLabel { color: @textSecondary@; font-size: 13px; }
QToolButton#terminalHeaderButton {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#terminalHeaderButton:hover { background: rgba(255,255,255,0.12); }
QToolButton#terminalHeaderButton:pressed { background: rgba(255,255,255,0.20); }
QListWidget#terminalSessionList {
    background: transparent; border: none; border-radius: 0px; outline: none;
    font-family: "Segoe UI"; font-size: 13px; color: @text@;
    padding: 4px 0px;
}
QListWidget#terminalSessionList::item { background: transparent; border: none; border-radius: 4px; padding: 0px; }
QListWidget#terminalSessionList::item:hover { background: @rowHover@; }
QListWidget#terminalSessionList::item:selected { background: @rowSelected@; color: @text@; }
QToolButton#terminalSessionClose {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#terminalSessionClose:hover { background: rgba(255,255,255,0.16); }
QToolButton#terminalSessionClose:pressed { background: rgba(255,255,255,0.26); }
QLabel#terminalSessionLabel { color: @text@; background: transparent; }
QLineEdit#terminalSessionRenameEditor {
    padding: 0px 3px; margin: 0px; border: 1px solid @accent@; border-radius: 4px;
    background: @editor@; color: @text@; selection-background-color: @selection@;
}
)"), true);
    }
    return ThemePalette::resolve(QStringLiteral(R"(
/* ─── Terminal panel (light) ─────────────────── */
QWidget#terminalPanel { background: @surface@; border: 1px solid @border@; border-radius: 10px; }
QWidget#terminalPanel[detached="true"] { border: none; border-radius: 0px; }
QWidget#terminalHeader { background: transparent; border: none; }
QLabel#terminalSectionTitle { color: @text@; font-size: 12px; font-weight: 700; }
QLabel#terminalStatusLabel { color: #555555; font-size: 12px; }
QLabel#terminalStatusLabel[terminalError="true"] { color: #d13438; }
QLabel#terminalFallbackLabel { color: #777777; font-size: 13px; }
/* min-height: 0 opts out of the global QToolButton toolbar rule, which would
   otherwise force these to 28px and push the 36px header taller. */
QToolButton#terminalHeaderButton {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#terminalHeaderButton:hover { background: rgba(0,0,0,0.10); }
QToolButton#terminalHeaderButton:pressed { background: rgba(0,0,0,0.16); }
QListWidget#terminalSessionList {
    background: transparent; border: none; border-radius: 0px; outline: none;
    font-family: "Segoe UI"; font-size: 13px; color: #222222;
    padding: 4px 0px;
}
QListWidget#terminalSessionList::item { background: transparent; border: none; border-radius: 4px; padding: 0px; }
QListWidget#terminalSessionList::item:hover { background: @rowHover@; }
QListWidget#terminalSessionList::item:selected { background: @rowSelected@; color: @text@; }
QToolButton#terminalSessionClose {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#terminalSessionClose:hover { background: rgba(0,0,0,0.14); }
QToolButton#terminalSessionClose:pressed { background: rgba(0,0,0,0.22); }
QLabel#terminalSessionLabel { color: #222222; background: transparent; }
QLineEdit#terminalSessionRenameEditor {
    padding: 0px 3px; margin: 0px; border: 1px solid #0078d4; border-radius: 4px;
    background: #ffffff; color: #222222; selection-background-color: #0078d4;
}
)"), false);
}

// Terminal menus are transient top-level popups, so the terminal panel's
// stylesheet does not paint them. Keep their palette in step with Terminal's
// independent Theme setting instead of inheriting the application's QMenu
// palette (which can be the opposite theme).
inline QString terminalMenu(bool dark)
{
    if (dark) {
        return ThemePalette::resolve(QStringLiteral(R"(
QMenu {
    background: @elevated@;
    border: 1px solid @border@;
    border-radius: 8px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 4px; color: @text@; }
QMenu::item:selected { background: @selection@; color: #ffffff; }
QMenu::item:disabled { color: @disabled@; }
QMenu::separator { height: 1px; background: @border@; margin: 4px 8px; }
)"), true);
    }
    return QStringLiteral(R"(
QMenu {
    background: #ffffff;
    border: 1px solid @border@;
    border-radius: 8px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 4px; color: #222222; }
QMenu::item:selected { background: #0078d4; color: #ffffff; }
QMenu::item:disabled { color: #999999; }
QMenu::separator { height: 1px; background: #e8e8e8; margin: 4px 8px; }
)");
}

// Composed once per process: the concatenation allocates and copies ~23 K UTF-16
// characters, and every caller wants the identical string.
inline QString light()
{
    static const QString style = ThemePalette::resolve(common() + QStringLiteral(R"(
/* ─── Window / global ───────────────────────────────────────── */
/* Level 0: the canvas every card sits on. The editor/preview splitter is
   transparent because it paints its panes' cards itself (ThemedSplitter). */
QMainWindow   { background: @canvas@; }
QSplitter#editorPreviewSplitter { background: transparent; border: none; }

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
    background: transparent;
    border: none;
    spacing: 2px;
    padding: 0px;
    max-height: 44px;
}
QToolBar#mainToolbar QToolButton { padding-top: 5px; padding-bottom: 5px; }
QToolBar#mainToolbar QToolButton#toolbarOverflowButton { padding-top: 0px; padding-bottom: 12px; }
QWidget#mathToolbar,
QWidget#chemistryToolbar {
    background: transparent;
    border: none;
}
QWidget#mathToolbar QToolButton {
    min-width: 48px;
    max-width: 48px;
    min-height: 48px;
    max-height: 48px;
    padding: 0px;
}
QWidget#chemistryToolbar QToolButton {
    min-width: 96px;
    max-width: 96px;
    min-height: 48px;
    max-height: 48px;
    padding: 0px;
}
QWidget#chemistryToolbar QToolButton[svgToolbarButton="true"] {
    padding: 0px;
}
QWidget#toolbarContainer {
    background: transparent;
    border: none;
}

QWidget#toolbarFlowSeparator {
    background: transparent;
    border: none;
    border-left: 1px solid #c8c8c8;
}
QToolBar::separator {
    width: 1px;
    background: @border@;
    margin: 10px 6px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    color: #333333;
}
QToolButton#toolbarOverflowButton { padding: 0px 0px 12px 0px; margin: 0px; }
QToolButton:hover {
    background: rgba(0,0,0,0.07);
    border-color: transparent;
}
QToolButton[toolbarHoverAnimated="true"]:hover {
    background: transparent;
    border-color: transparent;
}
QWidget#mathToolbar QToolButton[mathToolbarHover="true"] {
    background: transparent;
    border-color: transparent;
}
QToolButton:pressed {
    background: rgba(0,0,0,0.14);
}
QToolButton:checked {
    background: rgba(0,120,212,0.13);
    border-color: rgba(0,120,212,0.30);
}
QWidget#mathToolbar QToolButton[mathToolbarPopupOpen="true"] {
    background: rgba(0,120,212,0.13);
    border-color: rgba(0,120,212,0.30);
}
QWidget#chemistryToolbar QToolButton[popupOpen="true"] {
    background: rgba(0,120,212,0.13);
    border-color: rgba(0,120,212,0.30);
}
QToolButton#workspaceRevealButton,
QToolButton#workspaceRevealButton:hover,
QToolButton#workspaceRevealButton:pressed {
    background: transparent;
    border: none;
    border-radius: 0px;
    padding: 0px;
}
QToolButton#workspaceRevealButton[workspaceRowHovered="true"] {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowHovered="true"]:hover,
QToolButton#workspaceRevealButton[workspaceRowHovered="true"]:pressed {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowSelected="true"] {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowSelected="true"]:hover,
QToolButton#workspaceRevealButton[workspaceRowSelected="true"]:pressed {
    background: transparent;
    border: none;
}
QToolBar#workspaceToolbar { spacing: 0px; padding: 0px 4px; }
/* The Workspace header, toolbar and search row sit inside the Workspace card,
   which WorkspacePanelController paints behind them. */
QWidget#workspaceRootHeaderBar { background: transparent; }
QLabel#workspaceRootHeader { background: transparent; color: @text@; }
QWidget#workspaceSearchRow { background: @editor@; border: 1px solid @border@; border-radius: 4px; }
QWidget#workspaceSearchRow[focused="true"] { border-color: @accent@; }

QToolButton#workspaceOutlineHeaderButton {
    background: transparent;
    border: none;
    border-radius: 0px;
    color: @text@;
    font-size: 11px;
    font-weight: 700;
    text-align: left;
    padding: 0px 0px 0px 6px;
}
QToolButton#workspaceOutlineHeaderButton:hover { background: @hover@; }
QTreeWidget#workspaceOutlineTree {
    background: @surface@; color: @text@;
    border: none; outline: none; show-decoration-selected: 0;
    font-size: 12px; font-family: "Segoe UI";
}
QTreeWidget#workspaceOutlineTree::item { padding: 2px 6px; background: transparent; border-radius: 4px; }
QTreeWidget#workspaceOutlineTree::item:hover { background: @rowHover@; }
QTreeWidget#workspaceOutlineTree::item:selected { background: @rowSelected@; color: @text@; }
QTreeWidget#workspaceOutlineTree::item:selected:!active { background: @rowSelectedInactive@; color: @text@; }
QTreeWidget#workspaceOutlineTree::branch { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:hover { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:selected { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:has-children:!leaf { image: none; }
QToolButton#workspaceModeToggle,
QToolButton#workspaceSearchModeButton,
QToolButton#workspaceCopyFolderPathButton,
QToolButton#workspaceRevealFolderButton,
QToolButton#workspaceTerminalButton {
    padding: 0px;
    min-width: 24px; max-width: 24px;
    min-height: 24px; max-height: 24px;
}
QToolButton#workspaceModeToggle:checked,
QToolButton#workspaceSearchModeButton:checked {
    background: rgba(0,0,0,0.08);
    border-color: transparent;
    border-radius: 4px;
}
QToolButton#workspaceSearchClearButton {
    border: none;
    background: rgba(0,0,0,0.1);
    border-radius: 9px;
    padding: 0px;
    margin: 0px;
    min-width: 18px;
    max-width: 18px;
    min-height: 18px;
    max-height: 18px;
}
QToolButton#workspaceSearchClearButton:hover { background: rgba(0,0,0,0.08); }
QToolButton#workspaceSearchClearButton:pressed { background: rgba(0,0,0,0.14); }
QToolButton#workspaceSplitButton { padding-right: 17px; }
QToolButton#workspaceSortButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarOpenButton { padding-right: 22px; }
QToolButton#toolbarOpenButton::menu-button { width: 22px; }
QToolButton#toolbarImageButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarMathButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarAlignmentButton::menu-indicator { image: none; width: 0px; }
QToolButton[modeToggle="true"] {
    min-width: 108px;
    max-width: 108px;
    padding: 4px 12px;
    font-weight: 600;
    font-size: 16px;
}
QToolButton[modeToggle="true"]::menu-indicator {
    image: none;
    width: 0px;
}
/* Main toolbar buttons, the Markdown mode toggle included: ButtonRadius (8px)
   corners and fills only. Hover is ToolbarHoverAnimation's overlay, installed
   with the same radius; :checked is a soft accent tint with no outline. The
   transparent 1px border stays so no state changes a button's geometry. */
QToolBar#mainToolbar QToolButton { border-radius: 8px; }
QToolBar#mainToolbar QToolButton:pressed { background: rgba(0,0,0,0.12); border-color: transparent; }
QToolBar#mainToolbar QToolButton:checked { background: rgba(0,120,212,0.12); border-color: transparent; }
QToolBar#mainToolbar QToolButton:checked:pressed { background: rgba(0,120,212,0.20); }

/* ─── Menu bar ──────────────────────────────────────────────── */
/* The menu bar's height is pinned to an even 32px in MainWindow::createToolbar()
   -- do not try to set it from here. Neither `border-bottom` nor `padding`
   feeds QMenuBar's height (both were measured to leave it at 31 and merely eat
   into the content area), and an odd height puts every widget below it on a
   half device pixel at 150%. */
QMenuBar {
    background: @canvas@;
    color: #333333;
    border: none;
    padding: 2px 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: rgba(0,0,0,0.08); }

/* ─── Custom window title bar ───────────────────────────────── */
/* The title bar owns the row's background and its single bottom border; the
   menu bar it hosts must not paint a second one, or the border doubles up
   under the menu items only. The caption buttons are painted in C++ (see
   WindowTitleBar.cpp): their close-button accent must not follow the button
   palette, and the maximize button paints states that never arrive as Qt
   events. */
QWidget#windowTitleBar {
    background: @canvas@;
    border: none;
}
QMenuBar#titleBarMenuBar {
    background: transparent;
    border: none;
    padding: 2px 2px;
}

/* ─── Menus ─────────────────────────────────────────────────── */
QMenu {
    background: #ffffff;
    border: 1px solid @border@;
    border-radius: 8px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item           { padding: 6px 24px 6px 14px; border-radius: 4px; color: #222; }
QMenu::item:selected  { background: #0078d4; color: #fff; }
QMenu[menuHoverAnimated="true"]::item:selected { background: palette(highlight); }
QMenu::item:disabled   { color: #999; }
QMenu#tabContextMenu { min-width: 220px; }
QMenu#previewContextMenu { min-width: 160px; }
QMenu#editorContextMenu::item { padding-left: 22px; }
QMenu#editorContextMenu::icon { left: 12px; width: 24px; height: 24px; }
QMenu#editorContextMenu::indicator { left: 14px; }
QMenu#workspaceContextMenu::item { padding-left: 12px; }
QMenu#workspaceContextMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#workspaceCheckMenu::item { padding-left: 12px; }
QMenu#topMenuBarMenu::item { padding-left: 12px; }
QMenu#topMenuBarMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#topMenuBarMenu::indicator, QMenu#workspaceCheckMenu::indicator { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#toolbarOverflowMenu::item { padding-left: 16px; }
QMenu#toolbarOverflowMenu::icon { left: 12px; top: 1px; subcontrol-origin: padding; subcontrol-position: left center; width: 22px; height: 22px; }
QMenu#toolbarDropdownMenu::item { padding: 6px 24px 6px 12px; }
QMenu#toolbarDropdownMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu::separator      { height: 1px; background: #e8e8e8; margin: 4px 8px; }

/* ─── Context-menu command bar (Notepad-style top strip) ────── */
QWidget#menuCommandBar { background: transparent; }
QToolButton#menuCommandButton {
    border: none;
    background: transparent;
    border-radius: 4px;
    padding: 5px 2px;
    color: #222;
    font-size: 12px;
    font-family: "Segoe UI";
}
QToolButton#menuCommandButton:hover:enabled  { background: rgba(0,0,0,0.06); }
QToolButton#menuCommandButton:pressed:enabled { background: rgba(0,0,0,0.12); }
QToolButton#menuCommandButton:disabled { color: #9a9a9a; }

/* ─── Tabs (light) ──────────────────────────────────────────── */
QTabWidget::pane { border: none; background: #ffffff; }
QTabWidget { background: #f0f0f0; }
QTabBar           { background: #f0f0f0; }
QTabBar::tab {
    background: #e4e4e4;
    color: #555555;
    padding: 0px 20px 0px 24px;
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
QTabWidget#preferencesTabs QTabBar::tab { padding: 0px 24px; min-width: 0px; min-height: 32px; max-height: 32px; }
QTabWidget#preferencesTabs QTabBar::tab:selected { padding: 0px 24px; min-width: 0px; }

)") + QStringLiteral(R"(
/* ─── Document tabs ─────────────────────────────────────────── */
/* No strip fill: where there is no tab the row is the application canvas.
   The row spans both the Editor and the Preview card, so no tab merges into
   either: every tab is a self-contained 8px (ThemeCard::TabRadius) pill. Inactive tabs are quiet
   labels with a translucent hover pill; the current tab is painted by
   TabBar::paintCurrentTab (fill, border, soft shadow), so here it only
   colors its label. Every tab has the same box; margin-right is kTabSpacing,
   margin-top ThemeCard::TabTopMargin and margin-bottom ThemeCard::TabGap (the
   canvas between the tabs and the cards), all in TabWidget.cpp. */
QTabWidget#documentTabs { background: transparent; }
QTabWidget#documentTabs::pane { border: none; background: transparent; }
QTabBar#documentTabBar { background: transparent; }
QTabBar#documentTabBar::tab {
    background: transparent;
    color: @textSecondary@;
    padding: 0px 20px 0px 24px;
    min-height: 34px;
    border: 1px solid transparent;
    border-radius: 8px;
    margin: 0px 2px 4px 0px;
    font-size: 14px;
    font-family: "Segoe UI";
    min-width: 140px;
}
QTabBar#documentTabBar::tab:selected {
    background: transparent;
    color: #111111;
}
QTabBar#documentTabBar::tab:hover:!selected {
    background: rgba(0,0,0,0.045);
    color: @text@;
}

/* ─── Git window (light) ──────────────────────────── */
QWidget#sourceControlPanel { background: transparent; }

QLabel#gitMergeBanner {
    background: #fff4ce; color: #6a4b00; border-bottom: 1px solid #e8d9a0;
    padding: 5px 10px; font-size: 12px;
}

QWidget#gitSourceContent { background: transparent; }
QWidget#gitGraphHeader { background: transparent; border: none; }
QLabel#gitSectionTitle { color: #444444; font-size: 11px; font-weight: 700; }
)") + QStringLiteral(R"(
/* The branch selector paints its own label, icon, chevron and hover fill
   (GitBranchButton), so only the popup it opens is styled here. */
QWidget#gitBranchPopup {
    background: #ffffff;
    border: 1px solid #b8b8b8;
}
QWidget#gitBranchPopup QLineEdit#gitBranchPopupSearch {
    border: 1px solid #c6c6c6;
    border-radius: 4px;
    padding: 4px 6px;
    background: #ffffff;
    color: #202020;
}
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree {
    border: none;
    background: #ffffff;
    color: #202020;
}
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item { border: none; outline: none; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item:hover { background: @rowHover@; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item:selected { background: @rowSelected@; color: @text@; border: none; outline: none; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch,
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch:hover,
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch:selected {
    background: #ffffff;
    border: none;
}
QLabel#gitSectionCount { color: #777777; font-size: 11px; font-weight: 600; }

QPlainTextEdit#gitCommitMessage {
    background: #ffffff; color: #222222; border: 1px solid #c4c4c4;
    border-radius: 4px; padding: 4px 6px;
    font-family: "Segoe UI"; font-size: 13px;
}
QPlainTextEdit#gitCommitMessage:focus { border-color: #0078d4; }
QPushButton#gitCommitButton {
    background: #0078d4; color: #ffffff; border: none;
    border-top-left-radius: 4px; border-bottom-left-radius: 4px;
    border-top-right-radius: 0px; border-bottom-right-radius: 0px;
    padding: 5px 10px; font-size: 14px;
}
QPushButton#gitCommitButton:hover { background: #106ebe; }
QPushButton#gitCommitButton:disabled { background: #cccccc; color: #888888; }
/* A network op in flight (Pull/Push/Sync) -- kept setEnabled(true) so the
   spinning icon isn't run through Qt's disabled-icon generation, so the grey
   "disabled" look is reproduced here instead; see updateActionStates(). */
QPushButton#gitCommitButton[gitBusy="true"],
QPushButton#gitCommitButton[gitBusy="true"]:hover,
QPushButton#gitCommitButton[gitBusy="true"]:pressed {
    background: #cccccc; color: #888888;
}
QToolButton#gitCommitMenuButton {
    background: #0078d4; color: #ffffff; border: none;
    border-left: 1px solid #106ebe;
    border-top-right-radius: 4px; border-bottom-right-radius: 4px;
    border-top-left-radius: 0px; border-bottom-left-radius: 0px;
    width: 32px; padding: 0px;
}
QToolButton#gitCommitMenuButton:hover, QToolButton#gitCommitMenuButton:pressed { background: #106ebe; }
QToolButton#gitCommitMenuButton:disabled {
    background: #cccccc; color: #888888; border-left-color: #aaaaaa;
}

QTreeWidget#gitFileList, QTreeWidget#gitHistoryList {
    background: @surface@; border: none; outline: none;
    font-family: "Segoe UI"; font-size: 13px; color: #222222;
}
QTreeWidget#gitFileList::item, QTreeWidget#gitHistoryList::item { background: transparent; }
QToolButton#gitRowAction {
    background: transparent; border: none; border-radius: 4px; padding: 0px;
}
QToolButton#gitRowAction:hover { background: rgba(0,0,0,0.10); }
QToolButton#gitRowAction:pressed { background: rgba(0,0,0,0.16); }

QWidget#gitDiffPane { background: #ffffff; }
QLabel#gitDiffTitle {
    background: #f3f3f3; color: #444444; border-bottom: 1px solid #d4d4d4;
    padding: 5px 10px; font-size: 12px;
}
QPlainTextEdit#gitDiffLeft, QPlainTextEdit#gitDiffRight { background: #ffffff; }
QLabel#gitDiffPlaceholder, QLabel#gitEmptyLabel { color: #777777; font-size: 13px; }

QLabel#gitNoRepoTitle { color: #333333; font-size: 15px; font-weight: 600; }
QLabel#gitNoRepoText { color: #666666; font-size: 14px; }
QPushButton#gitInitRepoButton {
    background: #0078d4; color: #ffffff; border: none; border-radius: 4px;
    padding: 9px 18px; font-size: 14px;
}
QPushButton#gitInitRepoButton:hover { background: #106ebe; }
QPushButton#gitInitRepoButton:pressed { background: #005a9e; }
QPushButton#gitInitRepoButton:disabled { background: #cccccc; color: #888888; }

QWidget#gitStatusBar { background: transparent; border-top: 1px solid @border@; }
QScrollArea#gitStatusScroll,
QScrollArea#gitStatusScroll > QWidget#qt_scrollarea_viewport { background: transparent; border: none; }
QLabel#gitStatusLabel { background: transparent; color: #555555; font-size: 12px; }
QLabel#gitStatusLabel[gitError="true"] { color: #d13438; }
QToolButton#gitStatusClose {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#gitStatusClose:hover { background: #e4e4e4; }
QToolButton#gitStatusClose:pressed { background: #d6d6d6; }

/* ─── Go To dialog (light) ──────────────────────────────────── */
QTabWidget#goToTabs { background: #f5f5f5; }
QTabWidget#goToTabs::pane { border: none; border-top: 1px solid #e0e0e0; background: #f5f5f5; }
QTabWidget#goToTabs QTabBar { background: #f5f5f5; }
QTabWidget#goToTabs QTabBar::tab {
    background: transparent;
    color: #555555;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 0px 18px;
    margin: 0px;
    min-width: 0px;
    min-height: 34px;
    max-height: 34px;
}
QTabWidget#goToTabs QTabBar::tab:selected {
    background: transparent;
    color: #0078d4;
    border: none;
    border-bottom: 2px solid #0078d4;
    margin: 0px;
    padding: 0px 18px;
}
QTabWidget#goToTabs QTabBar::tab:hover:!selected { background: rgba(0,0,0,0.05); color: #222222; }
QLineEdit#goToOutlineSearch {
    background: #ffffff; color: #222222;
    border: 1px solid #c4c4c4; border-radius: 4px;
    padding: 5px 6px;
}
QLineEdit#goToOutlineSearch:focus { border-color: #0078d4; }
QTreeWidget#goToOutlineTree {
    background: #ffffff; color: #222222;
    border: 1px solid #d8d8d8; border-radius: 4px;
    outline: none; show-decoration-selected: 0;
    font-size: 13px; font-family: "Segoe UI";
}
QTreeWidget#goToOutlineTree QScrollBar:vertical,
QTreeWidget#goToOutlineTree QScrollBar:horizontal { background: #ffffff; }
QTreeWidget#goToOutlineTree::item { padding: 4px 6px; background: transparent; border-radius: 4px; }
QTreeWidget#goToOutlineTree::item:hover { background: @rowHover@; }
QTreeWidget#goToOutlineTree::item:selected { background: @rowSelected@; color: @text@; }
QTreeWidget#goToOutlineTree::item:selected:!active { background: @rowSelectedInactive@; color: @text@; }
QTreeWidget#goToOutlineTree::branch { background: transparent; }
QTreeWidget#goToOutlineTree::branch:hover { background: transparent; }
QTreeWidget#goToOutlineTree::branch:selected { background: transparent; }
QTreeWidget#goToOutlineTree::branch:has-children:!leaf { image: none; }

/* ─── Fix All Misspellings dialog (light) ───────────────────── */
QLabel#fixAllIntro { color: #555555; font-size: 13px; font-family: "Segoe UI"; }
QLabel#spellUnavailableLabel { color: #8a6d3b; }
QTreeWidget#fixAllTree {
    background: #ffffff; color: #222222;
    border: 1px solid #d8d8d8; border-radius: 4px;
    outline: none; show-decoration-selected: 1;
    font-size: 14px; font-family: "Segoe UI";
}
QTreeWidget#fixAllTree QHeaderView::section { font-size: 13px; }
/* The Fix All rows are tuned entirely through this rule, because QTreeView
   derives a row's height from the tallest thing in it - which here is this
   combo, so an ::item padding or a delegate sizeHint smaller than it changes
   nothing. min-height sets the box (22 + 4px padding top/bottom + 1px border =
   32px) and margin-bottom sets the gap to the next row's box, for a 40px row
   pitch. The margin sits entirely below the box rather than being split above
   and below: the row's text is laid out from the top, so a symmetric margin
   would push the dropdown a few pixels lower than the Word/Occurrences text
   beside it. */
QComboBox#compactCombo[fixAllRow="true"] { min-height: 22px; margin: 0px 0px 8px 0px; }
QTreeWidget#fixAllTree QScrollBar:vertical,
QTreeWidget#fixAllTree QScrollBar:horizontal { background: #ffffff; }
QTreeWidget#fixAllTree::item { padding: 4px 6px; background: transparent; }
QTreeWidget#fixAllTree::item:hover { background: @rowHover@; }
QTreeWidget#fixAllTree::item:selected,
QTreeWidget#fixAllTree::item:selected:!active { background: @rowSelected@; color: @text@; }
QPushButton#fixAllApply, QPushButton#fixAllCancel, QPushButton#spellManageButton {
    min-width: 96px; padding: 5px 14px;
    border: 1px solid #c4c4c4; border-radius: 4px;
    background: #fdfdfd; color: #222222;
}
QPushButton#fixAllApply { border-color: #0078d4; background: #0078d4; color: #ffffff; }
QPushButton#fixAllApply:disabled { border-color: #c4c4c4; background: #eaeaea; color: #9a9a9a; }
QPushButton#fixAllApply:hover { background: #1a86d9; }
QPushButton#fixAllCancel:hover, QPushButton#spellManageButton:hover { background: #f0f0f0; }


)") + QStringLiteral(R"(
/* ─── Status bar ────────────────────────────────────────────── */
/* The status line is text placed directly on the canvas: no slab, no rule
   above it and no dividers between its segments. */
QStatusBar {
    background: transparent;
    color: @textSecondary@;
    border: none;
    font-size: 12px;
    font-family: "Segoe UI";
}
QStatusBar QLabel {
    color: @textSecondary@;
    padding: 2px 10px;
    border: none;
}
QStatusBar QLabel#linkTargetStatus { padding-left: 4px; }
QStatusBar QToolButton#updateStatusButton[ready="true"] { color: @accent@; }
QStatusBar QToolButton#languageStatusButton,
QStatusBar QToolButton#lineEndingStatusButton,
QStatusBar QToolButton#spellStatusButton,
QStatusBar QToolButton#updateStatusButton,
QStatusBar QToolButton#encodingStatusButton {
    color: @textSecondary@;
    padding: 2px 10px;
    border: none;
    border-radius: 4px;
    background: transparent;
    /* The generic QToolButton rule above is written for the toolbar and carries
       min-height: 28px. QStyleSheetStyle turns that into a hard widget minimum
       (28 + this box's padding), and QStatusBar sizes itself from its tallest
       item -- which is what pushed the whole bar from 28px to 40px and left
       these segments taller than the QLabel ones beside them. Opting out here
       lets them fall back to their own text height, matching the labels. */
    min-height: 0px;
}
QStatusBar QToolButton#languageStatusButton:hover,
QStatusBar QToolButton#lineEndingStatusButton:hover,
QStatusBar QToolButton#spellStatusButton:hover,
QStatusBar QToolButton#updateStatusButton:hover,
QStatusBar QToolButton#encodingStatusButton:hover { background: rgba(0,0,0,0.06); }
QStatusBar QToolButton#languageStatusButton:pressed,
QStatusBar QToolButton#lineEndingStatusButton:pressed,
QStatusBar QToolButton#spellStatusButton:pressed,
QStatusBar QToolButton#updateStatusButton:pressed,
QStatusBar QToolButton#encodingStatusButton:pressed { background: rgba(0,0,0,0.10); }
QWidget#languageSelectorPopup {
    background: #ffffff;
    border: 1px solid #b8b8b8;
}
QWidget#languageSelectorPopup QLineEdit#languageSelectorSearch {
    border: 1px solid #c6c6c6;
    border-radius: 4px;
    padding: 4px 6px;
    background: #ffffff;
    color: #202020;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree {
    border: none;
    background: #ffffff;
    color: #202020;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item {
    border: none;
    outline: none;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item:hover { background: @rowHover@; }
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item:selected { background: @rowSelected@; color: @text@; border: none; outline: none; }
/* Without an explicit rule the base style paints its own row highlight in the
   indentation column, which reads as a second, paler strip left of the item. */
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch,
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch:hover,
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch:selected {
    background: #ffffff;
    border: none;
}

)") + QStringLiteral(R"(
/* ─── Content widgets ───────────────────────────────────────── */
QPlainTextEdit {
    border: none;
    background: #ffffff;
    color: #222222;
    selection-background-color: #add6ff;
    selection-color: #000000;
    /* One family only. QStyleSheetStyle turns this declaration into the QFont
       every QPlainTextEdit actually uses -- it overrides
       MarkdownEditor::monospaceFont() -- and a multi-family QFont makes Qt open
       every installed font to build its own fallback list, on the GUI thread,
       at the editor's first block layout. Missing glyphs still fall back
       lazily. Keep this in step with MarkdownEditor::monospaceFont(). */
    font-family: "Consolas";
}
QPlainTextEdit QScrollBar:vertical, QPlainTextEdit QScrollBar:horizontal { background: #ffffff; }
QTextBrowser  { border: none; background: #f9f9f9; }
QTextBrowser QScrollBar:vertical, QTextBrowser QScrollBar:horizontal { background: #f9f9f9; }
QGraphicsView#imageViewer { border: none; background: #f9f9f9; }
QGraphicsView#imageViewer QScrollBar:vertical, QGraphicsView#imageViewer QScrollBar:horizontal { background: #f9f9f9; }

/* Floating image-viewer control bar. Opaque on purpose: it is a child of the
   view frame, not of the scrolling viewport (which would drag it during a
   pan), so a translucent fill would composite against the frame background
   rather than the image and read as a muddy panel. */
QWidget#imageViewerToolbar {
    background: #f7f7f7;
    border: 1px solid #c4c4c4;
    border-radius: 12px;
}
QWidget#imageViewerToolbar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
}
QWidget#imageViewerToolbar QToolButton:hover   { background: rgba(0,0,0,0.07); border-color: rgba(0,0,0,0.12); }
QWidget#imageViewerToolbar QToolButton:pressed { background: rgba(0,0,0,0.13); }
QWidget#imageViewerToolbar QToolButton:checked { background: rgba(0,120,212,0.16); border-color: rgba(0,120,212,0.40); }
QWidget#imageViewerToolbar QToolButton:checked:hover { background: rgba(0,120,212,0.24); }
/* padding-bottom: QLabel centres on the font's line box, which reserves
   descender room "78%" never uses, so the text reads low next to the icons.
   Shrinking the text rect from the bottom lifts it onto their optical centre. */
QLabel#imageViewerZoomLabel {
    background: transparent;
    border: none;
    color: #444444;
    font-size: 16px;
    font-family: "Segoe UI";
    padding-bottom: 4px;
}
QWidget#imageViewerToolbarSeparator { background: #dcdcdc; border: none; }

/* Opaque scrollbar troughs: an explicit colour is required — transparent/none
   lets the native Windows groove bleed through. */
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: #f5f5f5; }
QPlainTextEdit QScrollBar::add-page:vertical, QPlainTextEdit QScrollBar::sub-page:vertical,
QPlainTextEdit QScrollBar::add-page:horizontal, QPlainTextEdit QScrollBar::sub-page:horizontal { background: #ffffff; }
QTextBrowser QScrollBar::add-page:vertical, QTextBrowser QScrollBar::sub-page:vertical,
QTextBrowser QScrollBar::add-page:horizontal, QTextBrowser QScrollBar::sub-page:horizontal { background: #f9f9f9; }
QGraphicsView#imageViewer QScrollBar::add-page:vertical, QGraphicsView#imageViewer QScrollBar::sub-page:vertical,
QGraphicsView#imageViewer QScrollBar::add-page:horizontal, QGraphicsView#imageViewer QScrollBar::sub-page:horizontal { background: #f9f9f9; }
QTreeView QScrollBar::add-page:vertical, QTreeView QScrollBar::sub-page:vertical,
QTreeView QScrollBar::add-page:horizontal, QTreeView QScrollBar::sub-page:horizontal { background: @surface@; }
QListWidget QScrollBar::add-page:vertical, QListWidget QScrollBar::sub-page:vertical,
QListWidget QScrollBar::add-page:horizontal, QListWidget QScrollBar::sub-page:horizontal { background: #ffffff; }


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
QLineEdit#referenceLinkInput { padding: 5px 4px; }

/* The widget owns its fixed, even height; this only sets its compact inset. */
QLineEdit#goToLineInput { padding: 0 3px 2px; }

/* Header & Footer dialog: muted captions, compact token chips, and a visible
   disabled state for a switched-off band's fields (QLineEdit has none). */
QDialog#headerFooterDialog QLabel#headerFooterCaption { color: #686868; font-size: 12px; }
QDialog#headerFooterDialog QLineEdit:disabled { color: #a0a0a0; background: #f3f3f3; border-color: #e2e2e2; }
QDialog#headerFooterDialog QCheckBox:disabled { color: #a0a0a0; }
QPushButton#headerFooterToken {
    min-width: 0px;
    padding: 2px 9px 3px 9px;
    font-size: 12px;
    background: #ffffff;
    border: 1px solid #d0d0d0;
}
QPushButton#headerFooterToken:hover { background: #eef5fc; border-color: #0078d4; }
QPushButton#headerFooterToken:pressed { background: #dcebf8; }
QPushButton#headerFooterToken:disabled { color: #a0a0a0; background: #f5f5f5; border-color: #e2e2e2; }

QSpinBox {
    background: #ffffff;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 4px 6px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222;
    selection-background-color: #0078d4;
}
QSpinBox:focus { border-color: #0078d4; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 10px; height: 10px; }

QToolButton#matrixTypeButton {
    border: 1px solid #d4d4d4;
    border-radius: 4px;
    background: #ffffff;
    padding: 4px;
}
QToolButton#matrixTypeButton:hover { background: #eef2f7; border-color: #aaaaaa; }
QToolButton#matrixTypeButton:checked { background: rgba(0,120,212,0.14); border-color: #0078d4; }

QComboBox#compactCombo {
    background: #ffffff;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 4px 34px 4px 12px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222;
    min-height: 28px;
}
QComboBox#compactCombo[preferencesCombo="true"] { min-height: 22px; }
QComboBox#compactCombo:hover { border-color: #aaaaaa; }
QComboBox#compactCombo:focus { border-color: #0078d4; }
QComboBox#compactCombo:disabled { background: #eeeeee; color: #999999; }
QComboBox#compactCombo::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 32px;
    border: none;
    background: transparent;
}
QComboBox#compactCombo::down-arrow { image: none; width: 12px; height: 12px; }
QComboBox#compactCombo QAbstractItemView {
    background: #ffffff;
    border: 1px solid #c4c4c4;
    border-radius: 5px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222222;
    padding: 0px;
    outline: none;
}
QComboBox#compactCombo QAbstractItemView::item {
    padding: 0px 12px;
    min-height: 0px;
}
QComboBox#compactCombo QAbstractItemView::item:hover { background: #e8e8e8; }
QComboBox#compactCombo QAbstractItemView::item:selected { background: #0078d4; color: #ffffff; }
/* SearchableComboBox: a search field over the same list. The popup is
   translucent and paints this rounded box itself (PE_Widget). */
QComboBox#compactCombo QWidget#searchableComboPopup {
    background: #ffffff;
    border: 1px solid #c4c4c4;
    border-radius: 5px;
}
QComboBox#compactCombo QWidget#searchableComboPopup QAbstractItemView {
    border: none;
    background: transparent;
}

QPushButton {
    background: #f0f0f0;
    border: 1px solid #c4c4c4;
    border-radius: 4px;
    padding: 4px 18px 6px 18px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: #222;
    min-width: 72px;
    outline: none;
}
QPushButton:hover   { background: #e4e4e4; border-color: #aaaaaa; }
QPushButton:pressed { background: #d8d8d8; }
QPushButton:default {
    background: #0078d4;
    border-color: #0078d4;
    color: white;
}
QPushButton:default:hover { background: #106ebe; }

/* ─── Find/Replace overlay (light) ──────────────────────────── */
QWidget#findOverlay { background: #ffffff; border: 1px solid #d0d0d0; border-radius: 8px; }
QWidget#findOverlayInputRow { background: #ffffff; border: 1px solid #cccccc; border-radius: 4px; }
QWidget#findOverlayInputRow[focused="true"] { border: 1px solid #0078d4; }
QWidget#findOverlayInputRow[invalid="true"] { border: 1px solid #d13438; }
QWidget#findOverlaySeparator { background: #dcdcdc; border: none; }
QLineEdit#findOverlayEdit {
    background: transparent;
    border: none;
    padding: 0px;
    font-family: "Segoe UI";
    font-size: 13px;
    color: #1f1f1f;
    selection-background-color: #cce4f7;
    selection-color: #1f1f1f;
}

/* ─── Command Palette search field (light) ──────────────────── */
QLineEdit#commandPaletteSearchEdit, QLineEdit#commandPaletteSearchEdit:focus {
    background: transparent;
    border: none;
    padding: 0px;
    font-family: "Segoe UI";
    color: #1f1f1f;
    selection-background-color: #cce4f7;
    selection-color: #1f1f1f;
}
QLabel#findOverlayCount { background: transparent; color: #6e6e6e; font-family: "Segoe UI"; font-size: 11px; padding-right: 2px; }
QToolButton#findOverlayOption, QToolButton#findOverlayAction, QToolButton#findOverlayChevron {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 0px;
}
QToolButton#findOverlayOption:hover, QToolButton#findOverlayAction:hover, QToolButton#findOverlayChevron:hover { background: rgba(0,0,0,0.07); }
QToolButton#findOverlayOption:pressed, QToolButton#findOverlayAction:pressed, QToolButton#findOverlayChevron:pressed { background: rgba(0,0,0,0.13); }
QToolButton#findOverlayOption:checked, QToolButton#findOverlayAction:checked { background: rgba(0,120,212,0.16); border-color: rgba(0,120,212,0.40); }
QToolButton#findOverlayOption:checked:hover, QToolButton#findOverlayAction:checked:hover { background: rgba(0,120,212,0.24); }
/* The chevron is a disclosure toggle, not an option, so being checked must not
   tint it -- but the flat :checked rule outranks the earlier :hover rule at
   equal specificity, which silently costs it hover/pressed feedback once
   Replace is expanded. These restate both for the checked state. */
QToolButton#findOverlayChevron:checked { background: transparent; border-color: transparent; }
QToolButton#findOverlayChevron:checked:hover { background: rgba(0,0,0,0.07); }
QToolButton#findOverlayChevron:checked:pressed { background: rgba(0,0,0,0.13); }
QToolButton#findOverlayOption:disabled, QToolButton#findOverlayAction:disabled { background: transparent; border-color: transparent; }

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

QRadioButton {
    font-size: 13px;
    font-family: "Segoe UI";
    spacing: 7px;
    color: #333;
}
QRadioButton::indicator {
    width: 18px;
    height: 18px;
}
QRadioButton::indicator:unchecked {
    image: url(:/icons/radio-unchecked-light.svg);
}
QRadioButton::indicator:unchecked:hover {
    image: url(:/icons/radio-unchecked-light-hover.svg);
}
QRadioButton::indicator:checked {
    image: url(:/icons/radio-checked.svg);
}
QRadioButton::indicator:checked:hover {
    image: url(:/icons/radio-checked-hover.svg);
}

/* ─── Workspace tree ────────────────────────────────────────── */
QTreeView {
    background: @surface@;
    color: @text@;
    border: none;
    show-decoration-selected: 0;
    outline: none;
}
QTreeView QScrollBar:vertical, QTreeView QScrollBar:horizontal { background: @surface@; }
QTreeView::item, QListView#workspaceSearchList::item { background: transparent; padding: 2px 4px; }
QTreeView::item:hover { background: transparent; }
QTreeView::item:selected { background: transparent; color: @text@; }
QTreeView::item:selected:!active { background: transparent; color: #222222; }
QHeaderView::section {
    background: @surface@;
    color: @textSecondary@;
    border: none;
    border-right: 1px solid @border@;
    border-bottom: 1px solid @surface@;
    padding: 3px 6px;
    font-size: 12px;
    font-family: "Segoe UI";
}
QHeaderView::section:hover { background: @hover@; }
QHeaderView::section:pressed { background: #d8d8d8; }
QHeaderView { background: @surface@; border: none; border-bottom: 1px solid @surface@; }

/* ─── Dialog List and Browser styling ─────────────────────── */
QListWidget {
    background: #ffffff;
    color: #222222;
    border: 1px solid #bbbbbb;
    border-radius: 4px;
    outline: none;
}
QListWidget QScrollBar:vertical, QListWidget QScrollBar:horizontal { background: #ffffff; }
QListWidget::item { padding: 4px 6px; border-radius: 4px; }
QListWidget::item:hover { background: @rowHover@; }
QListWidget::item:selected { background: @rowSelected@; color: @text@; }
QListWidget::item:focus { outline: none; border: none; }
QListWidget::item:selected:focus { outline: none; border: none; }
QListWidget#commandPaletteList {
    background: #ffffff;
    border: 1px solid #d4d4d4;
    border-top: none;
    border-top-left-radius: 0px;
    border-top-right-radius: 0px;
    border-bottom-left-radius: 8px;
    border-bottom-right-radius: 8px;
}
QListWidget#commandPaletteList QScrollBar:vertical,
QListWidget#commandPaletteList QScrollBar::groove:vertical,
QListWidget#commandPaletteList QScrollBar::add-page:vertical,
QListWidget#commandPaletteList QScrollBar::sub-page:vertical {
    background: #ffffff;
    border: none;
    border-bottom-right-radius: 8px;
}

QTextBrowser {
    background: #ffffff;
    color: #222222;
    border: 1px solid #bbbbbb;
    border-radius: 4px;
}

)") + chemistryPopupStyle(QStringLiteral("#ffffff"), QStringLiteral("#d4d4d4"),
                            QStringLiteral("#222222"), QStringLiteral("#6e6e6e"),
                            QStringLiteral("#e8e8e8"), QStringLiteral("rgba(0,0,0,60)"))
        , false) + terminalPanel(false);
    return style;
}

inline QString dark()
{
    static const QString style = ThemePalette::resolve(common() + QStringLiteral(R"(
/* ─── Window / global ───────────────────────────────────────── */
/* Level 0: the canvas every card sits on -- see the light theme. */
QMainWindow   { background: @canvas@; }
QSplitter#editorPreviewSplitter { background: transparent; border: none; }

/* ─── Tooltip ───────────────────────────────────────────────── */
QToolTip {
    background: @elevated@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 6px;
    font-size: 13px;
    font-family: "Segoe UI";
}

/* ─── Toolbar (dark theme) ───────────────────────────────────── */
QToolBar {
    background: transparent;
    border: none;
    spacing: 2px;
    padding: 0px;
    max-height: 44px;
}
QToolBar#mainToolbar QToolButton { padding-top: 5px; padding-bottom: 5px; }
QToolBar#mainToolbar QToolButton#toolbarOverflowButton { padding-top: 0px; padding-bottom: 12px; }
QWidget#mathToolbar,
QWidget#chemistryToolbar {
    background: transparent;
    border: none;
}
QWidget#mathToolbar QToolButton {
    min-width: 48px;
    max-width: 48px;
    min-height: 48px;
    max-height: 48px;
    padding: 0px;
}
QWidget#chemistryToolbar QToolButton {
    min-width: 96px;
    max-width: 96px;
    min-height: 48px;
    max-height: 48px;
    padding: 0px;
}
QWidget#chemistryToolbar QToolButton[svgToolbarButton="true"] {
    padding: 0px;
}
QWidget#toolbarContainer {
    background: transparent;
    border: none;
}

QWidget#toolbarFlowSeparator {
    background: transparent;
    border: none;
    border-left: 1px solid @border@;
}
QToolBar::separator {
    width: 1px;
    background: @activeHover@;
    margin: 10px 6px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    color: @text@;
}
QToolButton#toolbarOverflowButton { padding: 0px 0px 12px 0px; margin: 0px; }
QToolButton:hover {
    background: rgba(255,255,255,0.08);
    border-color: transparent;
}
QToolButton[toolbarHoverAnimated="true"]:hover {
    background: transparent;
    border-color: transparent;
}
QWidget#mathToolbar QToolButton[mathToolbarHover="true"] {
    background: transparent;
    border-color: transparent;
}
QToolButton:pressed {
    background: rgba(255,255,255,0.16);
}
QToolButton:checked {
    background: rgba(57,148,188,0.28);
    border-color: rgba(57,148,188,0.45);
}
QWidget#mathToolbar QToolButton[mathToolbarPopupOpen="true"] {
    background: rgba(57,148,188,0.28);
    border-color: rgba(57,148,188,0.45);
}
QWidget#chemistryToolbar QToolButton[popupOpen="true"] {
    background: rgba(57,148,188,0.28);
    border-color: rgba(57,148,188,0.45);
}
QToolButton#workspaceRevealButton,
QToolButton#workspaceRevealButton:hover,
QToolButton#workspaceRevealButton:pressed {
    background: transparent;
    border: none;
    border-radius: 0px;
    padding: 0px;
}
QToolButton#workspaceRevealButton[workspaceRowHovered="true"] {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowHovered="true"]:hover,
QToolButton#workspaceRevealButton[workspaceRowHovered="true"]:pressed {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowSelected="true"] {
    background: transparent;
    border: none;
}
QToolButton#workspaceRevealButton[workspaceRowSelected="true"]:hover,
QToolButton#workspaceRevealButton[workspaceRowSelected="true"]:pressed {
    background: transparent;
    border: none;
}
QToolBar#workspaceToolbar { spacing: 0px; padding: 0px 4px; }
QWidget#workspaceRootHeaderBar { background: transparent; }
QLabel#workspaceRootHeader { background: transparent; color: @text@; }
QWidget#workspaceSearchRow { background: @surface@; border: 1px solid @border@; border-radius: 4px; }
QWidget#workspaceSearchRow[focused="true"] { border-color: @accent@; }

QToolButton#workspaceOutlineHeaderButton {
    background: transparent;
    border: none;
    border-radius: 0px;
    color: @text@;
    font-size: 11px;
    font-weight: 700;
    text-align: left;
    padding: 0px 0px 0px 6px;
}
QToolButton#workspaceOutlineHeaderButton:hover { background: @hover@; }
QTreeWidget#workspaceOutlineTree {
    background: @surface@; color: @text@;
    border: none; outline: none; show-decoration-selected: 0;
    font-size: 12px; font-family: "Segoe UI";
}
QTreeWidget#workspaceOutlineTree::item { padding: 2px 6px; background: transparent; border-radius: 4px; }
QTreeWidget#workspaceOutlineTree::item:hover { background: @rowHover@; }
QTreeWidget#workspaceOutlineTree::item:selected { background: @rowSelected@; color: @text@; }
QTreeWidget#workspaceOutlineTree::item:selected:!active { background: @rowSelectedInactive@; color: @text@; }
QTreeWidget#workspaceOutlineTree::branch { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:hover { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:selected { background: transparent; }
QTreeWidget#workspaceOutlineTree::branch:has-children:!leaf { image: none; }
QToolButton#workspaceModeToggle,
QToolButton#workspaceSearchModeButton,
QToolButton#workspaceCopyFolderPathButton,
QToolButton#workspaceRevealFolderButton,
QToolButton#workspaceTerminalButton {
    padding: 0px;
    min-width: 24px; max-width: 24px;
    min-height: 24px; max-height: 24px;
}
QToolButton#workspaceModeToggle:checked,
QToolButton#workspaceSearchModeButton:checked {
    background: rgba(255,255,255,0.10);
    border-color: transparent;
    border-radius: 4px;
}
QToolButton#workspaceSearchClearButton {
    border: none;
    background: rgba(255,255,255,0.08);
    border-radius: 9px;
    padding: 0px;
    margin: 0px;
    min-width: 18px;
    max-width: 18px;
    min-height: 18px;
    max-height: 18px;
}
QToolButton#workspaceSearchClearButton:hover { background: rgba(255,255,255,0.10); }
QToolButton#workspaceSearchClearButton:pressed { background: rgba(255,255,255,0.16); }
QToolButton#workspaceSplitButton { padding-right: 17px; }
QToolButton#workspaceSortButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarOpenButton { padding-right: 22px; }
QToolButton#toolbarOpenButton::menu-button { width: 22px; }
QToolButton#toolbarImageButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarMathButton::menu-indicator { image: none; width: 0px; }
QToolButton#toolbarAlignmentButton::menu-indicator { image: none; width: 0px; }
QToolButton[modeToggle="true"] {
    min-width: 108px;
    max-width: 108px;
    padding: 4px 12px;
    font-weight: 600;
    font-size: 16px;
}
QToolButton[modeToggle="true"]::menu-indicator {
    image: none;
    width: 0px;
}
/* Main toolbar buttons: see the light theme. */
QToolBar#mainToolbar QToolButton { border-radius: 8px; }
QToolBar#mainToolbar QToolButton:pressed { background: rgba(255,255,255,0.14); border-color: transparent; }
QToolBar#mainToolbar QToolButton:checked { background: rgba(57,148,188,0.26); border-color: transparent; }
QToolBar#mainToolbar QToolButton:checked:pressed { background: rgba(57,148,188,0.36); }

/* ─── Menu bar ──────────────────────────────────────────────── */
/* Height is pinned in createToolbar(), not here -- see the light theme. */
QMenuBar {
    background: @canvas@;
    color: @text@;
    border: none;
    padding: 2px 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: rgba(255,255,255,0.09); }

/* ─── Custom window title bar ───────────────────────────────── */
/* See the light theme for why the menu bar inside it is transparent and
   border-less. */
QWidget#windowTitleBar {
    background: @canvas@;
    border: none;
}
QMenuBar#titleBarMenuBar {
    background: transparent;
    border: none;
    padding: 2px 2px;
}

/* ─── Menus ─────────────────────────────────────────────────── */
QMenu {
    background: @elevated@;
    border: 1px solid @border@;
    border-radius: 8px;
    padding: 4px;
    font-size: 13px;
    font-family: "Segoe UI";
}
QMenu::item           { padding: 6px 24px 6px 14px; border-radius: 4px; color: @text@; }
QMenu::item:selected  { background: @selection@; color: #fff; }
QMenu[menuHoverAnimated="true"]::item:selected { background: palette(highlight); }
QMenu::item:disabled   { color: @disabled@; }
QMenu#tabContextMenu { min-width: 220px; }
QMenu#previewContextMenu { min-width: 160px; }
QMenu#editorContextMenu::item { padding-left: 22px; }
QMenu#editorContextMenu::icon { left: 8px; width: 24px; height: 24px; }
QMenu#editorContextMenu::indicator { left: 14px; }
QMenu#workspaceContextMenu::item { padding-left: 12px; }
QMenu#workspaceContextMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#workspaceCheckMenu::item { padding-left: 12px; }
QMenu#topMenuBarMenu::item { padding-left: 12px; }
QMenu#topMenuBarMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#topMenuBarMenu::indicator, QMenu#workspaceCheckMenu::indicator { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu#toolbarOverflowMenu::item { padding-left: 16px; }
QMenu#toolbarOverflowMenu::icon { left: 12px; top: 1px; subcontrol-origin: padding; subcontrol-position: left center; width: 22px; height: 22px; }
QMenu#toolbarDropdownMenu::item { padding: 6px 24px 6px 12px; }
QMenu#toolbarDropdownMenu::icon { left: 8px; subcontrol-origin: padding; subcontrol-position: left center; }
QMenu::separator      { height: 1px; background: @border@; margin: 4px 8px; }

/* ─── Context-menu command bar (Notepad-style top strip) ────── */
QWidget#menuCommandBar { background: transparent; }
QToolButton#menuCommandButton {
    border: none;
    background: transparent;
    border-radius: 4px;
    padding: 5px 2px;
    color: @text@;
    font-size: 12px;
    font-family: "Segoe UI";
}
QToolButton#menuCommandButton:hover:enabled  { background: rgba(255,255,255,0.10); }
QToolButton#menuCommandButton:pressed:enabled { background: rgba(255,255,255,0.16); }
QToolButton#menuCommandButton:disabled { color: @disabled@; }

/* ─── Tabs ──────────────────────────────────────────────────── */
QTabWidget { background: @surface@; }
QTabWidget::pane { border: none; background: @editor@; }
QTabBar           { background: @surface@; }
QTabBar::tab {
    background: @surface@;
    color: @textSecondary@;
    padding: 0px 20px 0px 24px;
    min-height: 28px;
    border: 1px solid transparent;
    border-right: 1px solid @border@;
    margin-top: 0px;
    font-size: 14px;
    font-family: "Segoe UI";
    min-width: 140px;
    max-height: 28px;
}
QTabBar::tab:selected {
    background: @editor@;
    color: #ffffff;
    border: 1px solid @border@;
    border-top: 2px solid @accent@;
    border-bottom: 1px solid @editor@;
    margin-top: 0px;
    margin-bottom: -1px;
    padding-bottom: 4px;
}
QTabBar::tab:hover:!selected { background: @hover@; color: @text@; }
QTabWidget#preferencesTabs QTabBar::tab { padding: 0px 12px; min-width: 0px; min-height: 32px; max-height: 32px; }
QTabWidget#preferencesTabs QTabBar::tab:selected { padding: 0px 12px; min-width: 0px; }

)") + QStringLiteral(R"(
/* ─── Document tabs ─────────────────────────────────────────── */
/* Document tabs: see the light theme. */
QTabWidget#documentTabs { background: transparent; }
QTabWidget#documentTabs::pane { border: none; background: transparent; }
QTabBar#documentTabBar { background: transparent; }
QTabBar#documentTabBar::tab {
    background: transparent;
    color: @textSecondary@;
    padding: 0px 20px 0px 24px;
    min-height: 34px;
    border: 1px solid transparent;
    border-radius: 8px;
    margin: 0px 2px 4px 0px;
    font-size: 14px;
    font-family: "Segoe UI";
    min-width: 140px;
}
QTabBar#documentTabBar::tab:selected {
    background: transparent;
    color: #ffffff;
}
QTabBar#documentTabBar::tab:hover:!selected {
    background: rgba(255,255,255,0.035);
    color: @text@;
}

/* ─── Git window (dark) ───────────────────────────── */
QWidget#sourceControlPanel { background: transparent; }

QLabel#gitMergeBanner {
    background: #4a3a10; color: #e2c08d; border-bottom: 1px solid #5f4a15;
    padding: 5px 10px; font-size: 12px;
}

QWidget#gitSourceContent { background: transparent; }
QWidget#gitGraphHeader { background: transparent; border: none; }
QLabel#gitSectionTitle { color: @text@; font-size: 11px; font-weight: 700; }
)") + QStringLiteral(R"(
QWidget#gitBranchPopup {
    background: @elevated@;
    border: 1px solid @border@;
}
QWidget#gitBranchPopup QLineEdit#gitBranchPopupSearch {
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 6px;
    background: @editor@;
    color: @text@;
}
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree {
    border: none;
    background: @elevated@;
    color: @text@;
}
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item { border: none; outline: none; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item:hover { background: @rowHover@; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::item:selected { background: @rowSelected@; color: @text@; border: none; outline: none; }
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch,
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch:hover,
QWidget#gitBranchPopup QTreeWidget#gitBranchPopupTree::branch:selected {
    background: @elevated@;
    border: none;
}
QLabel#gitSectionCount { color: @textSecondary@; font-size: 11px; font-weight: 600; }

QPlainTextEdit#gitCommitMessage {
    background: @surface@; color: @text@; border: 1px solid @border@;
    border-radius: 4px; padding: 4px 6px;
    font-family: "Segoe UI"; font-size: 13px;
}
QPlainTextEdit#gitCommitMessage:focus { border-color: @accent@; }
QPushButton#gitCommitButton {
    background: @accent@; color: #ffffff; border: none;
    border-top-left-radius: 4px; border-bottom-left-radius: 4px;
    border-top-right-radius: 0px; border-bottom-right-radius: 0px;
    padding: 5px 10px; font-size: 14px;
}
QPushButton#gitCommitButton:hover { background: @accentHover@; }
QPushButton#gitCommitButton:disabled { background: @hover@; color: @disabled@; }
QPushButton#gitCommitButton[gitBusy="true"],
QPushButton#gitCommitButton[gitBusy="true"]:hover,
QPushButton#gitCommitButton[gitBusy="true"]:pressed {
    background: @hover@; color: @disabled@;
}
QToolButton#gitCommitMenuButton {
    background: @accent@; color: #ffffff; border: none;
    border-left: 1px solid @accentHover@;
    border-top-right-radius: 4px; border-bottom-right-radius: 4px;
    border-top-left-radius: 0px; border-bottom-left-radius: 0px;
    width: 30px; padding: 0px;
}
QToolButton#gitCommitMenuButton:hover, QToolButton#gitCommitMenuButton:pressed { background: @accentHover@; }
QToolButton#gitCommitMenuButton:disabled {
    background: @hover@; color: @disabled@; border-left-color: @border@;
}

QTreeWidget#gitFileList, QTreeWidget#gitHistoryList {
    background: @surface@; border: none; outline: none;
    font-family: "Segoe UI"; font-size: 13px; color: @text@;
}
QTreeWidget#gitFileList::item, QTreeWidget#gitHistoryList::item { background: transparent; }
QToolButton#gitRowAction {
    background: transparent; border: none; border-radius: 4px; padding: 0px;
}
QToolButton#gitRowAction:hover { background: rgba(255,255,255,0.12); }
QToolButton#gitRowAction:pressed { background: rgba(255,255,255,0.20); }

QWidget#gitDiffPane { background: @editor@; }
QLabel#gitDiffTitle {
    background: @surface@; color: @text@; border-bottom: 1px solid @border@;
    padding: 5px 10px; font-size: 12px;
}
QPlainTextEdit#gitDiffLeft, QPlainTextEdit#gitDiffRight { background: @editor@; }
QLabel#gitDiffPlaceholder, QLabel#gitEmptyLabel { color: @textSecondary@; font-size: 13px; }

QLabel#gitNoRepoTitle { color: @text@; font-size: 15px; font-weight: 600; }
QLabel#gitNoRepoText { color: @textSecondary@; font-size: 14px; }
QPushButton#gitInitRepoButton {
    background: @accent@; color: #ffffff; border: none; border-radius: 4px;
    padding: 9px 18px; font-size: 14px;
}
QPushButton#gitInitRepoButton:hover { background: @accentHover@; }
QPushButton#gitInitRepoButton:pressed { background: @accentPressed@; }
QPushButton#gitInitRepoButton:disabled { background: @hover@; color: @disabled@; }

QWidget#gitStatusBar { background: transparent; border-top: 1px solid @border@; }
QScrollArea#gitStatusScroll,
QScrollArea#gitStatusScroll > QWidget#qt_scrollarea_viewport { background: transparent; border: none; }
QLabel#gitStatusLabel { background: transparent; color: @textSecondary@; font-size: 12px; }
QLabel#gitStatusLabel[gitError="true"] { color: #f1707a; }
QToolButton#gitStatusClose {
    background: transparent; border: none; border-radius: 4px;
    padding: 0px; margin: 0px; min-width: 0px; min-height: 0px;
}
QToolButton#gitStatusClose:hover { background: @hover@; }
QToolButton#gitStatusClose:pressed { background: @pressed@; }

/* ─── Go To dialog (dark) ───────────────────────────────────── */
QTabWidget#goToTabs { background: @elevated@; }
QTabWidget#goToTabs::pane { border: none; border-top: 1px solid @border@; background: @elevated@; }
QTabWidget#goToTabs QTabBar { background: @elevated@; }
QTabWidget#goToTabs QTabBar::tab {
    background: transparent;
    color: @textSecondary@;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 0px 18px;
    margin: 0px;
    min-width: 0px;
    min-height: 34px;
    max-height: 34px;
}
QTabWidget#goToTabs QTabBar::tab:selected {
    background: transparent;
    color: @accent@;
    border: none;
    border-bottom: 2px solid @accent@;
    margin: 0px;
    padding: 0px 18px;
}
QTabWidget#goToTabs QTabBar::tab:hover:!selected { background: rgba(255,255,255,0.07); color: @text@; }
QLineEdit#goToOutlineSearch {
    background: @editor@; color: @text@;
    border: 1px solid @border@; border-radius: 4px;
    padding: 5px 6px;
}
QLineEdit#goToOutlineSearch:focus { border-color: @accent@; }
QTreeWidget#goToOutlineTree {
    background: @surface@; color: @text@;
    border: 1px solid @border@; border-radius: 4px;
    outline: none; show-decoration-selected: 0;
    font-size: 13px; font-family: "Segoe UI";
}
QTreeWidget#goToOutlineTree QScrollBar:vertical,
QTreeWidget#goToOutlineTree QScrollBar:horizontal { background: @surface@; margin-top: 0px; }
QTreeWidget#goToOutlineTree::item { padding: 4px 6px; background: transparent; border-radius: 4px; }
QTreeWidget#goToOutlineTree::item:hover { background: @rowHover@; }
QTreeWidget#goToOutlineTree::item:selected { background: @rowSelected@; color: @text@; }
QTreeWidget#goToOutlineTree::item:selected:!active { background: @rowSelectedInactive@; color: @text@; }
QTreeWidget#goToOutlineTree::branch { background: transparent; }
QTreeWidget#goToOutlineTree::branch:hover { background: transparent; }
QTreeWidget#goToOutlineTree::branch:selected { background: transparent; }
QTreeWidget#goToOutlineTree::branch:has-children:!leaf { image: none; }

/* ─── Fix All Misspellings dialog (dark) ────────────────────── */
QLabel#fixAllIntro { color: @textSecondary@; font-size: 13px; font-family: "Segoe UI"; }
QLabel#spellUnavailableLabel { color: #d7ba7d; }
QTreeWidget#fixAllTree {
    background: @editor@; color: @text@;
    border: 1px solid @border@; border-radius: 4px;
    outline: none; show-decoration-selected: 1;
    font-size: 14px; font-family: "Segoe UI";
}
QTreeWidget#fixAllTree QHeaderView::section { font-size: 13px; }
/* The Fix All rows are tuned entirely through this rule, because QTreeView
   derives a row's height from the tallest thing in it - which here is this
   combo, so an ::item padding or a delegate sizeHint smaller than it changes
   nothing. min-height sets the box (22 + 4px padding top/bottom + 1px border =
   32px) and margin-bottom sets the gap to the next row's box, for a 40px row
   pitch. The margin sits entirely below the box rather than being split above
   and below: the row's text is laid out from the top, so a symmetric margin
   would push the dropdown a few pixels lower than the Word/Occurrences text
   beside it. */
QComboBox#compactCombo[fixAllRow="true"] { min-height: 22px; margin: 0px 0px 8px 0px; }
QTreeWidget#fixAllTree QScrollBar:vertical,
QTreeWidget#fixAllTree QScrollBar:horizontal { background: @editor@; }
QTreeWidget#fixAllTree::item { padding: 4px 6px; background: transparent; }
QTreeWidget#fixAllTree::item:hover { background: @rowHover@; }
QTreeWidget#fixAllTree::item:selected,
QTreeWidget#fixAllTree::item:selected:!active { background: @rowSelected@; color: @text@; }
QPushButton#fixAllApply, QPushButton#fixAllCancel, QPushButton#spellManageButton {
    min-width: 96px; padding: 5px 14px;
    border: 1px solid @border@; border-radius: 4px;
    background: @active@; color: @text@;
}
QPushButton#fixAllApply { border-color: @accent@; background: @accent@; color: #ffffff; }
QPushButton#fixAllApply:disabled { border-color: @border@; background: @surface@; color: @disabled@; }
QPushButton#fixAllApply:hover { background: @accentHover@; }
QPushButton#fixAllCancel:hover, QPushButton#spellManageButton:hover { background: @activeHover@; }


)") + QStringLiteral(R"(
/* ─── Status bar ────────────────────────────────────────────── */
/* Text on the canvas -- see the light theme. */
QStatusBar {
    background: transparent;
    color: @textSecondary@;
    border: none;
    font-size: 12px;
    font-family: "Segoe UI";
}
QStatusBar QLabel {
    color: @textSecondary@;
    padding: 2px 10px;
    border: none;
}
QStatusBar QLabel#linkTargetStatus { padding-left: 4px; }
QStatusBar QToolButton#updateStatusButton[ready="true"] { color: @accent@; }
QStatusBar QToolButton#languageStatusButton,
QStatusBar QToolButton#lineEndingStatusButton,
QStatusBar QToolButton#spellStatusButton,
QStatusBar QToolButton#updateStatusButton,
QStatusBar QToolButton#encodingStatusButton {
    color: @textSecondary@;
    padding: 2px 10px;
    border: none;
    border-radius: 4px;
    background: transparent;
    /* The generic QToolButton rule above is written for the toolbar and carries
       min-height: 28px. QStyleSheetStyle turns that into a hard widget minimum
       (28 + this box's padding), and QStatusBar sizes itself from its tallest
       item -- which is what pushed the whole bar from 28px to 40px and left
       these segments taller than the QLabel ones beside them. Opting out here
       lets them fall back to their own text height, matching the labels. */
    min-height: 0px;
}
QStatusBar QToolButton#languageStatusButton:hover,
QStatusBar QToolButton#lineEndingStatusButton:hover,
QStatusBar QToolButton#spellStatusButton:hover,
QStatusBar QToolButton#updateStatusButton:hover,
QStatusBar QToolButton#encodingStatusButton:hover { background: rgba(255,255,255,0.08); }
QStatusBar QToolButton#languageStatusButton:pressed,
QStatusBar QToolButton#lineEndingStatusButton:pressed,
QStatusBar QToolButton#spellStatusButton:pressed,
QStatusBar QToolButton#updateStatusButton:pressed,
QStatusBar QToolButton#encodingStatusButton:pressed { background: rgba(255,255,255,0.14); }
QWidget#languageSelectorPopup {
    background: @elevated@;
    border: 1px solid @border@;
}
QWidget#languageSelectorPopup QLineEdit#languageSelectorSearch {
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 6px;
    background: @editor@;
    color: @text@;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree {
    border: none;
    background: @elevated@;
    color: @text@;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item {
    border: none;
    outline: none;
}
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item:hover { background: @rowHover@; }
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::item:selected { background: @rowSelected@; color: @text@; border: none; outline: none; }
/* Without an explicit rule the base style paints its own row highlight in the
   indentation column, which reads as a second, paler strip left of the item. */
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch,
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch:hover,
QWidget#languageSelectorPopup QTreeWidget#languageSelectorTree::branch:selected {
    background: @elevated@;
    border: none;
}

)") + QStringLiteral(R"(
/* ─── Content widgets ───────────────────────────────────────── */
QPlainTextEdit {
    border: none;
    background: @editor@;
    color: @text@;
    selection-background-color: @selection@;
    selection-color: #ffffff;
    /* One family only. QStyleSheetStyle turns this declaration into the QFont
       every QPlainTextEdit actually uses -- it overrides
       MarkdownEditor::monospaceFont() -- and a multi-family QFont makes Qt open
       every installed font to build its own fallback list, on the GUI thread,
       at the editor's first block layout. Missing glyphs still fall back
       lazily. Keep this in step with MarkdownEditor::monospaceFont(). */
    font-family: "Consolas";
}
QPlainTextEdit QScrollBar:vertical, QPlainTextEdit QScrollBar:horizontal { background: @editor@; }
QTextBrowser  { border: none; background: @surface@; }
QTextBrowser QScrollBar:vertical, QTextBrowser QScrollBar:horizontal { background: @surface@; }
QGraphicsView#imageViewer { border: none; background: @editor@; }
QGraphicsView#imageViewer QScrollBar:vertical, QGraphicsView#imageViewer QScrollBar:horizontal { background: @editor@; }

/* Floating image-viewer control bar -- see the light theme for why it is opaque. */
QWidget#imageViewerToolbar {
    background: @elevated@;
    border: 1px solid @border@;
    border-radius: 12px;
}
QWidget#imageViewerToolbar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
}
QWidget#imageViewerToolbar QToolButton:hover   { background: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.12); }
QWidget#imageViewerToolbar QToolButton:pressed { background: rgba(255,255,255,0.16); }
QWidget#imageViewerToolbar QToolButton:checked { background: rgba(57,148,188,0.32); border-color: rgba(57,148,188,0.55); }
QWidget#imageViewerToolbar QToolButton:checked:hover { background: rgba(57,148,188,0.42); }
QLabel#imageViewerZoomLabel {
    background: transparent;
    border: none;
    color: @text@;
    font-size: 16px;
    font-family: "Segoe UI";
    padding-bottom: 4px;
}
QWidget#imageViewerToolbarSeparator { background: @activeHover@; border: none; }

/* Opaque scrollbar troughs: an explicit colour is required — transparent/none
   lets the native Windows groove (a light strip) bleed through. */
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: @surface@; }
QPlainTextEdit QScrollBar::add-page:vertical, QPlainTextEdit QScrollBar::sub-page:vertical,
QPlainTextEdit QScrollBar::add-page:horizontal, QPlainTextEdit QScrollBar::sub-page:horizontal { background: @editor@; }
QTextBrowser QScrollBar::add-page:vertical, QTextBrowser QScrollBar::sub-page:vertical,
QTextBrowser QScrollBar::add-page:horizontal, QTextBrowser QScrollBar::sub-page:horizontal { background: @surface@; }
QGraphicsView#imageViewer QScrollBar::add-page:vertical, QGraphicsView#imageViewer QScrollBar::sub-page:vertical,
QGraphicsView#imageViewer QScrollBar::add-page:horizontal, QGraphicsView#imageViewer QScrollBar::sub-page:horizontal { background: @editor@; }
QTreeView QScrollBar::add-page:vertical, QTreeView QScrollBar::sub-page:vertical,
QTreeView QScrollBar::add-page:horizontal, QTreeView QScrollBar::sub-page:horizontal { background: @surface@; }
QListWidget QScrollBar::add-page:vertical, QListWidget QScrollBar::sub-page:vertical,
QListWidget QScrollBar::add-page:horizontal, QListWidget QScrollBar::sub-page:horizontal { background: @surface@; }


/* ─── Dialogs & form widgets ─────────────────────────────────── */
QDialog { background: @elevated@; }
QLabel  { color: @text@; font-size: 13px; font-family: "Segoe UI"; }

QLineEdit {
    background: @editor@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 5px 9px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: @text@;
    selection-background-color: @selection@;
}
QLineEdit:focus { border-color: @accent@; }
QLineEdit#referenceLinkInput { padding: 5px 4px; }

/* The widget owns its fixed, even height; this only sets its compact inset. */
QLineEdit#goToLineInput { padding: 0 3px 2px; }

/* Header & Footer dialog: muted captions, compact token chips, and a visible
   disabled state for a switched-off band's fields (QLineEdit has none). */
QDialog#headerFooterDialog QLabel#headerFooterCaption { color: @textSecondary@; font-size: 12px; }
QDialog#headerFooterDialog QLineEdit:disabled { color: @disabled@; background: @surface@; border-color: @border@; }
QDialog#headerFooterDialog QCheckBox:disabled { color: @disabled@; }
QPushButton#headerFooterToken {
    min-width: 0px;
    padding: 2px 9px 3px 9px;
    font-size: 12px;
    background: @editor@;
    border: 1px solid @border@;
}
QPushButton#headerFooterToken:hover { background: @activeHover@; border-color: @accent@; }
QPushButton#headerFooterToken:pressed { background: @pressed@; }
QPushButton#headerFooterToken:disabled { color: @disabled@; background: @surface@; border-color: @border@; }

QSpinBox {
    background: @editor@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 6px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: @text@;
    selection-background-color: @selection@;
}
QSpinBox:focus { border-color: @accent@; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 10px; height: 10px; }

QToolButton#matrixTypeButton {
    border: 1px solid @border@;
    border-radius: 4px;
    background: @editor@;
    padding: 4px;
}
QToolButton#matrixTypeButton:hover { background: @activeHover@; border-color: @activeHover@; }
QToolButton#matrixTypeButton:checked { background: rgba(57,148,188,0.22); border-color: @accent@; }

QComboBox#compactCombo {
    background: @editor@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 34px 4px 12px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: @text@;
    min-height: 28px;
}
QComboBox#compactCombo[preferencesCombo="true"] { min-height: 22px; }
QComboBox#compactCombo:hover { border-color: @activeHover@; }
QComboBox#compactCombo:focus { border-color: @accent@; }
QComboBox#compactCombo:disabled { background: @active@; color: @disabled@; }
QComboBox#compactCombo::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 32px;
    border: none;
    background: transparent;
}
QComboBox#compactCombo::down-arrow { image: none; width: 12px; height: 12px; }
QComboBox#compactCombo QAbstractItemView {
    background: @elevated@;
    border: 1px solid @border@;
    border-radius: 5px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: @text@;
    padding: 0px;
    outline: none;
}
QComboBox#compactCombo QAbstractItemView::item {
    padding: 0px 12px;
    min-height: 0px;
}
QComboBox#compactCombo QAbstractItemView::item:hover { background: @activeHover@; }
QComboBox#compactCombo QAbstractItemView::item:selected { background: @selection@; color: #ffffff; }
/* SearchableComboBox: a search field over the same list. The popup is
   translucent and paints this rounded box itself (PE_Widget). */
QComboBox#compactCombo QWidget#searchableComboPopup {
    background: @elevated@;
    border: 1px solid @border@;
    border-radius: 5px;
}
QComboBox#compactCombo QWidget#searchableComboPopup QAbstractItemView {
    border: none;
    background: transparent;
}

QPushButton {
    background: @active@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 4px 18px 6px 18px;
    font-size: 13px;
    font-family: "Segoe UI";
    color: @text@;
    min-width: 72px;
    outline: none;
}
QPushButton:hover   { background: @activeHover@; border-color: @activeHover@; }
QPushButton:pressed { background: @pressed@; }
QPushButton:default {
    background: @accent@;
    border-color: @accent@;
    color: white;
}
QPushButton:default:hover { background: @accentHover@; }

/* ─── Find/Replace overlay (dark) ───────────────────────────── */
QWidget#findOverlay { background: @elevated@; border: 1px solid @activeHover@; border-radius: 8px; }
QWidget#findOverlayInputRow { background: @editor@; border: 1px solid @border@; border-radius: 4px; }
QWidget#findOverlayInputRow[focused="true"] { border: 1px solid @accent@; }
QWidget#findOverlayInputRow[invalid="true"] { border: 1px solid #f1707a; }
/* In the Workspace card the fields share the panel surface; the border defines them. */
QWidget#workspaceSearchPanel QWidget#findOverlayInputRow { background: @surface@; }
QWidget#findOverlaySeparator { background: @activeHover@; border: none; }
QLineEdit#findOverlayEdit {
    background: transparent;
    border: none;
    padding: 0px;
    font-family: "Segoe UI";
    font-size: 13px;
    color: @text@;
    selection-background-color: @selection@;
    selection-color: #ffffff;
}

/* ─── Command Palette search field (dark) ───────────────────── */
QLineEdit#commandPaletteSearchEdit, QLineEdit#commandPaletteSearchEdit:focus {
    background: transparent;
    border: none;
    padding: 0px;
    font-family: "Segoe UI";
    color: @text@;
    selection-background-color: @selection@;
    selection-color: #ffffff;
}
QLabel#findOverlayCount { background: transparent; color: @textSecondary@; font-family: "Segoe UI"; font-size: 11px; padding-right: 2px; }
QToolButton#findOverlayOption, QToolButton#findOverlayAction, QToolButton#findOverlayChevron {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 0px;
}
QToolButton#findOverlayOption:hover, QToolButton#findOverlayAction:hover, QToolButton#findOverlayChevron:hover { background: rgba(255,255,255,0.10); }
QToolButton#findOverlayOption:pressed, QToolButton#findOverlayAction:pressed, QToolButton#findOverlayChevron:pressed { background: rgba(255,255,255,0.16); }
QToolButton#findOverlayOption:checked, QToolButton#findOverlayAction:checked { background: rgba(57,148,188,0.32); border-color: rgba(57,148,188,0.55); }
QToolButton#findOverlayOption:checked:hover, QToolButton#findOverlayAction:checked:hover { background: rgba(57,148,188,0.42); }
/* See the light theme: :checked would otherwise swallow the chevron's hover. */
QToolButton#findOverlayChevron:checked { background: transparent; border-color: transparent; }
QToolButton#findOverlayChevron:checked:hover { background: rgba(255,255,255,0.10); }
QToolButton#findOverlayChevron:checked:pressed { background: rgba(255,255,255,0.16); }
QToolButton#findOverlayOption:disabled, QToolButton#findOverlayAction:disabled { background: transparent; border-color: transparent; }

QCheckBox {
    font-size: 13px;
    font-family: "Segoe UI";
    spacing: 7px;
    color: @text@;
}
QCheckBox::indicator {
    width: 15px; height: 15px;
    border: 1px solid @border@;
    border-radius: 3px;
    background: @editor@;
}
QCheckBox::indicator:checked {
    background: @accent@;
    border-color: @accent@;
    image: url(:/icons/checkmark.svg);
}

QRadioButton {
    font-size: 13px;
    font-family: "Segoe UI";
    spacing: 7px;
    color: @text@;
}
QRadioButton::indicator {
    width: 18px;
    height: 18px;
}
QRadioButton::indicator:unchecked {
    image: url(:/icons/radio-unchecked-dark.svg);
}
QRadioButton::indicator:unchecked:hover {
    image: url(:/icons/radio-unchecked-dark-hover.svg);
}
QRadioButton::indicator:checked {
    image: url(:/icons/radio-checked.svg);
}
QRadioButton::indicator:checked:hover {
    image: url(:/icons/radio-checked-hover.svg);
}

/* ─── Workspace tree ────────────────────────────────────────── */
QTreeView {
    background: @surface@;
    color: @text@;
    border: none;
    show-decoration-selected: 0;
    outline: none;
}
QTreeView QScrollBar:vertical, QTreeView QScrollBar:horizontal { background: @surface@; }
QTreeView QScrollBar:vertical { margin-top: 34px; }
QTreeView::item, QListView#workspaceSearchList::item { background: transparent; padding: 2px 4px; }
QTreeView::item:hover { background: transparent; }
QTreeView::item:selected { background: transparent; color: @text@; }
QTreeView::item:selected:!active { background: transparent; color: @text@; }
QHeaderView::section {
    background: @surface@;
    color: @textSecondary@;
    border: none;
    border-right: 1px solid @border@;
    border-bottom: 1px solid @surface@;
    padding: 3px 6px;
    font-size: 12px;
    font-family: "Segoe UI";
}
QHeaderView::section:hover { background: @hover@; }
QHeaderView::section:pressed { background: @pressed@; }
/* Match the header background exactly (not just leave it unset): the empty
   trailing area past the last section (stretchLastSection is off) otherwise
   falls back to the native style's light separator, showing as a white
   underline. Painting a same-color bottom border covers that line without
   drawing a visible separator. */
QHeaderView { background: @surface@; border: none; border-bottom: 1px solid @surface@; }

/* ─── Dialog List and Browser styling ─────────────────────── */
QListWidget {
    background: @surface@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 4px;
    outline: none;
}
QListWidget QScrollBar:vertical, QListWidget QScrollBar:horizontal { background: @surface@; }
QListWidget::item { padding: 4px 6px; border-radius: 4px; }
QListWidget::item:hover { background: @rowHover@; }
QListWidget::item:selected { background: @rowSelected@; color: @text@; }
QListWidget::item:focus { outline: none; border: none; }
QListWidget::item:selected:focus { outline: none; border: none; }
QListWidget#commandPaletteList {
    background: @elevated@;
    border: 1px solid @border@;
    border-top: none;
    border-top-left-radius: 0px;
    border-top-right-radius: 0px;
    border-bottom-left-radius: 8px;
    border-bottom-right-radius: 8px;
}
QListWidget#commandPaletteList QScrollBar:vertical,
QListWidget#commandPaletteList QScrollBar::groove:vertical,
QListWidget#commandPaletteList QScrollBar::add-page:vertical,
QListWidget#commandPaletteList QScrollBar::sub-page:vertical {
    background: @elevated@;
    border: none;
    border-bottom-right-radius: 8px;
}

QTextBrowser {
    background: @surface@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 4px;
}

)") + chemistryPopupStyle(QStringLiteral("@elevated@"), QStringLiteral("@border@"),
                            QStringLiteral("@text@"), QStringLiteral("@textSecondary@"),
                            QStringLiteral("@border@"), QStringLiteral("rgba(255,255,255,60)"))
        + terminalPanel(true), true);
    return style;
}

} // namespace AppStyle
