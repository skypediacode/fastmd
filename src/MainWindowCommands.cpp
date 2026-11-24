#include "MainWindow.h"
#include "SpellChecker.h"

#include "AlignedMenu.h"
#include "CommandPaletteLauncher.h"
#include "CommandPalettePopup.h"
#include "CommandRegistry.h"
#include "DocumentModel.h"
#include "MarkdownEditor.h"
#include "PreferencesTargets.h"
#include "RecentFilesManager.h"
#include "RecentFoldersManager.h"
#include "TabWidget.h"
#include "TerminalCommands.h"
#include "TerminalController.h"
#include "TerminalPanel.h"
#include "TerminalProfiles.h"
#include "TextEncoding.h"
#include "WorkspacePanelController.h"
#include "WorkspaceFileOps.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

QString cleanMenuText(const QString& raw)
{
    QString text = raw;
    text.remove(QLatin1Char('&'));
    text = text.trimmed();
    while (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text.trimmed();
}

QString commandId(const QString& category, const QString& title)
{
    QString id = category + QLatin1Char('.') + title;
    id = id.toLower();
    id.replace(QLatin1Char(' '), QLatin1Char('-'));
    id.remove(QChar(0x25B8));
    return id;
}

// Submenus promoted to top-level palette categories. Everything else keeps its
// parent menu's name, so nested Insert groups retain useful palette categories
// while "Edit ▸ Undo" stays "Edit: Undo".
const QStringList& promotedSubmenus()
{
    static const QStringList promoted = {
        QStringLiteral("Insert"),
        QStringLiteral("Insert Callouts"),
        QStringLiteral("Insert Table of Content"),
        QStringLiteral("Export As"),
        QStringLiteral("Line"),
        QStringLiteral("Convert Case"),
        QStringLiteral("Whitespace"),
        QStringLiteral("EOL Conversion"),
        QStringLiteral("Toolbars"),
        QStringLiteral("Theme"),
    };
    return promoted;
}

// Search synonyms, keyed by the command's lower-cased title. These are what
// make "h1", "title", "pdf" or "wrap" find the right command even though none
// of those words appear in the visible label.
const QHash<QString, QStringList>& titleAliases()
{
    static const QHash<QString, QStringList> aliases = {
        { QStringLiteral("source control"),    { QStringLiteral("git"), QStringLiteral("scm"), QStringLiteral("commit"), QStringLiteral("stage"), QStringLiteral("diff"), QStringLiteral("push"), QStringLiteral("pull"), QStringLiteral("stash"), QStringLiteral("branch"), QStringLiteral("repository") } },
        { QStringLiteral("copy path"),         { QStringLiteral("path"), QStringLiteral("filename"), QStringLiteral("location"), QStringLiteral("clipboard") } },
        { QStringLiteral("reveal active file in explorer"), { QStringLiteral("explorer"), QStringLiteral("folder"), QStringLiteral("show in folder"), QStringLiteral("locate") } },
        { QStringLiteral("reveal workspace in explorer"), { QStringLiteral("explorer"), QStringLiteral("workspace"), QStringLiteral("show in folder"), QStringLiteral("locate") } },
        { QStringLiteral("open active file folder in external terminal"), { QStringLiteral("terminal"), QStringLiteral("console"), QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("powershell"), QStringLiteral("windows terminal") } },
        { QStringLiteral("open active file folder in terminal"), { QStringLiteral("terminal"), QStringLiteral("console"), QStringLiteral("shell"), QStringLiteral("internal terminal") } },
        { QStringLiteral("open workspace in terminal"), { QStringLiteral("terminal"), QStringLiteral("workspace"), QStringLiteral("shell"), QStringLiteral("internal terminal") } },
        { QStringLiteral("terminal"),          { QStringLiteral("shell"), QStringLiteral("console"), QStringLiteral("powershell"), QStringLiteral("pwsh"), QStringLiteral("cmd"), QStringLiteral("bash"), QStringLiteral("wsl"), QStringLiteral("prompt") } },
        { QStringLiteral("detach terminal"),   { QStringLiteral("terminal"), QStringLiteral("open terminal"), QStringLiteral("floating terminal"), QStringLiteral("pop out terminal"), QStringLiteral("undock terminal") } },
        { QStringLiteral("attach terminal"),   { QStringLiteral("terminal"), QStringLiteral("dock terminal"), QStringLiteral("docked terminal"), QStringLiteral("reattach terminal") } },
        { QStringLiteral("new terminal"),      { QStringLiteral("shell"), QStringLiteral("console"), QStringLiteral("create terminal"), QStringLiteral("add terminal") } },
        { QStringLiteral("new terminal with profile..."), { QStringLiteral("shell"), QStringLiteral("profile"), QStringLiteral("powershell"), QStringLiteral("pwsh"), QStringLiteral("cmd"), QStringLiteral("git bash"), QStringLiteral("wsl") } },
        { QStringLiteral("select default profile..."), { QStringLiteral("default shell"), QStringLiteral("shell"), QStringLiteral("profile"), QStringLiteral("powershell"), QStringLiteral("cmd"), QStringLiteral("wsl") } },
        { QStringLiteral("close terminal"),    { QStringLiteral("kill terminal"), QStringLiteral("exit shell") } },
        { QStringLiteral("next terminal"),     { QStringLiteral("switch terminal"), QStringLiteral("cycle terminal") } },
        { QStringLiteral("previous terminal"), { QStringLiteral("switch terminal"), QStringLiteral("cycle terminal") } },
        { QStringLiteral("focus terminal"),    { QStringLiteral("go to terminal"), QStringLiteral("shell"), QStringLiteral("console") } },
        { QStringLiteral("clear"),             { QStringLiteral("cls"), QStringLiteral("clear screen"), QStringLiteral("clear terminal"), QStringLiteral("scrollback") } },
        { QStringLiteral("increase font size"), { QStringLiteral("bigger"), QStringLiteral("larger"), QStringLiteral("zoom in") } },
        { QStringLiteral("decrease font size"), { QStringLiteral("smaller"), QStringLiteral("zoom out") } },
        { QStringLiteral("reset font size"),   { QStringLiteral("default font size"), QStringLiteral("reset zoom") } },
        { QStringLiteral("use light theme"),   { QStringLiteral("theme"), QStringLiteral("appearance"), QStringLiteral("light") } },
        { QStringLiteral("use dark theme"),    { QStringLiteral("theme"), QStringLiteral("appearance"), QStringLiteral("dark"), QStringLiteral("night") } },
        { QStringLiteral("follow app theme"),  { QStringLiteral("theme"), QStringLiteral("appearance"), QStringLiteral("system"), QStringLiteral("auto") } },
        { QStringLiteral("new"),               { QStringLiteral("new file"), QStringLiteral("create"), QStringLiteral("blank") } },
        { QStringLiteral("new window"),        { QStringLiteral("window"), QStringLiteral("instance") } },
        { QStringLiteral("open"),              { QStringLiteral("open file"), QStringLiteral("load") } },
        { QStringLiteral("import as markdown"), { QStringLiteral("import"), QStringLiteral("convert"), QStringLiteral("word"), QStringLiteral("office"), QStringLiteral("document") } },
        { QStringLiteral("open folder"),       { QStringLiteral("workspace"), QStringLiteral("directory"), QStringLiteral("project") } },
        { QStringLiteral("save"),              { QStringLiteral("write"), QStringLiteral("store") } },
        { QStringLiteral("save as"),           { QStringLiteral("save copy"), QStringLiteral("rename") } },
        { QStringLiteral("save all"),          { QStringLiteral("save everything") } },
        { QStringLiteral("close tab"),         { QStringLiteral("close document") } },
        { QStringLiteral("close all tabs"),    { QStringLiteral("close everything"), QStringLiteral("close documents") } },
        { QStringLiteral("reopen closed tab"), { QStringLiteral("restore tab"), QStringLiteral("undo close") } },
        { QStringLiteral("clear recent files"), { QStringLiteral("recent"), QStringLiteral("history"), QStringLiteral("forget files") } },
        { QStringLiteral("clear recent folders"), { QStringLiteral("recent"), QStringLiteral("history"), QStringLiteral("forget folders") } },
        { QStringLiteral("export pdf"),        { QStringLiteral("pdf"), QStringLiteral("acrobat"), QStringLiteral("export") } },
        { QStringLiteral("export docx"),       { QStringLiteral("docx"), QStringLiteral("word"), QStringLiteral("office"), QStringLiteral("export") } },
        { QStringLiteral("export html"),       { QStringLiteral("html"), QStringLiteral("web page"), QStringLiteral("export") } },
        { QStringLiteral("epub"),              { QStringLiteral("ebook"), QStringLiteral("export") } },
        { QStringLiteral("plain text"),        { QStringLiteral("txt"), QStringLiteral("export") } },
        { QStringLiteral("rich text (.rtf)"),  { QStringLiteral("rtf"), QStringLiteral("export") } },
        { QStringLiteral("opendocument (.odt)"), { QStringLiteral("odt"), QStringLiteral("libreoffice"), QStringLiteral("export") } },
        { QStringLiteral("latex (.tex)"),      { QStringLiteral("latex"), QStringLiteral("tex"), QStringLiteral("export") } },
        { QStringLiteral("print"),             { QStringLiteral("printer"), QStringLiteral("hardcopy") } },
        { QStringLiteral("page setup"),        { QStringLiteral("margins"), QStringLiteral("paper"), QStringLiteral("layout") } },
        { QStringLiteral("preferences"),       { QStringLiteral("settings"), QStringLiteral("options"), QStringLiteral("configuration") } },

        // "Settings: ..." (PreferencesTargets), which open Preferences on one control.
        { QStringLiteral("restore session on startup"), { QStringLiteral("session"), QStringLiteral("startup"), QStringLiteral("restore tabs"), QStringLiteral("reopen tabs") } },
        { QStringLiteral("close app when last tab closes"), { QStringLiteral("last tab"), QStringLiteral("close application") } },
        { QStringLiteral("confirm before closing all terminals"), { QStringLiteral("close all terminals"), QStringLiteral("confirmation") } },
        { QStringLiteral("home folder"),       { QStringLiteral("workspace root"), QStringLiteral("documents"), QStringLiteral("start folder") } },
        { QStringLiteral("default save folder"), { QStringLiteral("save location"), QStringLiteral("directory") } },
        { QStringLiteral("open pdf files externally"), { QStringLiteral("pdf viewer"), QStringLiteral("external viewer"), QStringLiteral("default app") } },
        { QStringLiteral("open html files externally"), { QStringLiteral("browser"), QStringLiteral("external viewer"), QStringLiteral("default app") } },
        { QStringLiteral("open image files externally"), { QStringLiteral("picture"), QStringLiteral("photo"), QStringLiteral("image viewer"), QStringLiteral("external viewer"), QStringLiteral("default app") } },
        { QStringLiteral("show dialog after export"), { QStringLiteral("export dialog"), QStringLiteral("open exported file") } },
        { QStringLiteral("highlight current line"), { QStringLiteral("current line"), QStringLiteral("cursor line"), QStringLiteral("line highlight") } },
        { QStringLiteral("render single line breaks"), { QStringLiteral("line break"), QStringLiteral("newline"), QStringLiteral("soft break"), QStringLiteral("commonmark") } },
        { QStringLiteral("default editor mode"), { QStringLiteral("new document"), QStringLiteral("default mode"), QStringLiteral("new file mode") } },
        { QStringLiteral("auto save"),         { QStringLiteral("autosave"), QStringLiteral("save automatically") } },
        { QStringLiteral("auto save delay"),   { QStringLiteral("autosave"), QStringLiteral("save interval") } },
        { QStringLiteral("crash recovery"),    { QStringLiteral("backup"), QStringLiteral("snapshot"), QStringLiteral("unsaved") } },
        { QStringLiteral("editor font"),       { QStringLiteral("font family"), QStringLiteral("typeface") } },
        { QStringLiteral("preview font"),      { QStringLiteral("font family"), QStringLiteral("typeface"), QStringLiteral("html font") } },
        { QStringLiteral("line spacing"),      { QStringLiteral("line height"), QStringLiteral("leading"), QStringLiteral("document spacing"), QStringLiteral("typography") } },
        { QStringLiteral("paragraph spacing"), { QStringLiteral("paragraph gap"), QStringLiteral("document spacing"), QStringLiteral("typography") } },
        { QStringLiteral("list spacing"),      { QStringLiteral("list gap"), QStringLiteral("list item spacing"), QStringLiteral("document spacing"), QStringLiteral("typography") } },
        { QStringLiteral("print / pdf font"),  { QStringLiteral("pdf font"), QStringLiteral("print font"), QStringLiteral("font family"), QStringLiteral("typeface") } },
        { QStringLiteral("file associations"), { QStringLiteral("open with"), QStringLiteral("file types"), QStringLiteral("extensions"), QStringLiteral("default app") } },
        { QStringLiteral("open settings"),     { QStringLiteral("spell"), QStringLiteral("spelling"), QStringLiteral("spell check settings") } },
        { QStringLiteral("exit"),              { QStringLiteral("quit"), QStringLiteral("close application") } },

        { QStringLiteral("undo"),              { QStringLiteral("revert") } },
        { QStringLiteral("redo"),              { QStringLiteral("repeat") } },
        { QStringLiteral("cut"),               { QStringLiteral("clipboard") } },
        { QStringLiteral("copy"),              { QStringLiteral("clipboard") } },
        { QStringLiteral("paste"),             { QStringLiteral("clipboard") } },
        { QStringLiteral("select all"),        { QStringLiteral("everything") } },
        { QStringLiteral("find"),              { QStringLiteral("search") } },
        { QStringLiteral("replace"),           { QStringLiteral("substitute"), QStringLiteral("find and replace") } },
        { QStringLiteral("find in files"),     { QStringLiteral("search in files"), QStringLiteral("search workspace"),
                                                 QStringLiteral("grep"), QStringLiteral("search folder") } },
        { QStringLiteral("replace in files"),  { QStringLiteral("replace across files"), QStringLiteral("replace in workspace"),
                                                 QStringLiteral("substitute"), QStringLiteral("find and replace in files") } },
        { QStringLiteral("go to line"),        { QStringLiteral("goto"), QStringLiteral("jump"), QStringLiteral("line number") } },
        { QStringLiteral("auto format document"), { QStringLiteral("prettify"), QStringLiteral("beautify"), QStringLiteral("tidy"), QStringLiteral("align tables") } },
        { QStringLiteral("duplicate line"),    { QStringLiteral("clone line"), QStringLiteral("copy line") } },
        { QStringLiteral("delete line"),       { QStringLiteral("remove line"), QStringLiteral("kill line") } },
        { QStringLiteral("move line up"),      { QStringLiteral("shift line up") } },
        { QStringLiteral("move line down"),    { QStringLiteral("shift line down") } },
        { QStringLiteral("convert eol to windows (crlf)"), { QStringLiteral("eol"), QStringLiteral("line ending"), QStringLiteral("crlf"), QStringLiteral("windows") } },
        { QStringLiteral("enable"), { QStringLiteral("spell"), QStringLiteral("spelling"),
                                       QStringLiteral("typo"), QStringLiteral("dictionary") } },
        { QStringLiteral("disable"), { QStringLiteral("spell"), QStringLiteral("spelling"),
                                        QStringLiteral("typo"), QStringLiteral("dictionary") } },
        { QStringLiteral("fix all misspellings"), { QStringLiteral("spell"), QStringLiteral("spelling"),
                                                     QStringLiteral("typo"), QStringLiteral("misspell"),
                                                     QStringLiteral("correct") } },
        { QStringLiteral("select language"), { QStringLiteral("spell"), QStringLiteral("spelling"),
                                                QStringLiteral("dictionary") } },
        { QStringLiteral("convert eol to unix (lf)"),      { QStringLiteral("eol"), QStringLiteral("line ending"), QStringLiteral("lf"), QStringLiteral("unix") } },
        { QStringLiteral("convert eol to mac (cr)"),       { QStringLiteral("eol"), QStringLiteral("line ending"), QStringLiteral("cr"), QStringLiteral("mac") } },
        { QStringLiteral("convert encoding to utf-8"),          { QStringLiteral("encoding"), QStringLiteral("utf8"), QStringLiteral("charset") } },
        { QStringLiteral("convert encoding to utf-8 with bom"), { QStringLiteral("encoding"), QStringLiteral("utf8"), QStringLiteral("bom"), QStringLiteral("charset") } },
        { QStringLiteral("convert encoding to utf-16 le"),      { QStringLiteral("encoding"), QStringLiteral("utf16"), QStringLiteral("little endian"), QStringLiteral("charset") } },
        { QStringLiteral("convert encoding to utf-16 be"),      { QStringLiteral("encoding"), QStringLiteral("utf16"), QStringLiteral("big endian"), QStringLiteral("charset") } },

        // All six headings answer to every heading-ish query ("h1", "title",
        // "header") so one guess surfaces the whole family; the exact level
        // still ranks first because its own title matches directly.
        { QStringLiteral("heading 1"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("heading 2"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("subtitle"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("heading 3"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("heading 4"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("heading 5"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("heading 6"),         { QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("title"), QStringLiteral("header"), QStringLiteral("heading"), QStringLiteral("section") } },
        { QStringLiteral("bold"),              { QStringLiteral("strong"), QStringLiteral("b") } },
        { QStringLiteral("italic"),            { QStringLiteral("emphasis"), QStringLiteral("em") } },
        { QStringLiteral("strikethrough"),     { QStringLiteral("strike"), QStringLiteral("crossed out") } },
        { QStringLiteral("highlight"),         { QStringLiteral("mark"), QStringLiteral("text highlight") } },
        { QStringLiteral("subscript"),         { QStringLiteral("sub") } },
        { QStringLiteral("superscript"),       { QStringLiteral("sup"), QStringLiteral("exponent") } },
        { QStringLiteral("blockquote"),        { QStringLiteral("quote"), QStringLiteral("block quote"), QStringLiteral("citation") } },
        { QStringLiteral("insert inline code"), { QStringLiteral("code"), QStringLiteral("monospace"), QStringLiteral("backtick"), QStringLiteral("inline") } },
        { QStringLiteral("insert code block"),  { QStringLiteral("fence"), QStringLiteral("fenced"), QStringLiteral("snippet"), QStringLiteral("code"), QStringLiteral("block") } },
        { QStringLiteral("mermaid diagram"),   { QStringLiteral("mermaid"), QStringLiteral("diagram"), QStringLiteral("flowchart"), QStringLiteral("graph") } },
        { QStringLiteral("link"),              { QStringLiteral("url"), QStringLiteral("hyperlink"), QStringLiteral("href") } },
        { QStringLiteral("image"),             { QStringLiteral("img"), QStringLiteral("picture"), QStringLiteral("photo"), QStringLiteral("screenshot") } },
        { QStringLiteral("table"),             { QStringLiteral("grid"), QStringLiteral("rows"), QStringLiteral("columns"), QStringLiteral("spreadsheet") } },
        { QStringLiteral("insert matrix"),     { QStringLiteral("matrix"), QStringLiteral("math"), QStringLiteral("latex"), QStringLiteral("array") } },
        { QStringLiteral("browse image"),      { QStringLiteral("image"), QStringLiteral("picture"), QStringLiteral("photo"), QStringLiteral("insert image") } },
        { QStringLiteral("indent selection"),  { QStringLiteral("indent"), QStringLiteral("tab"), QStringLiteral("shift right") } },
        { QStringLiteral("unindent selection"), { QStringLiteral("unindent"), QStringLiteral("outdent"), QStringLiteral("shift tab"), QStringLiteral("shift left") } },
        { QStringLiteral("copy file path"),    { QStringLiteral("path"), QStringLiteral("copy path"), QStringLiteral("file location") } },
        { QStringLiteral("checklist"),         { QStringLiteral("checkbox"), QStringLiteral("task list"), QStringLiteral("todo") } },
        { QStringLiteral("bullet list"),       { QStringLiteral("unordered list"), QStringLiteral("ul"), QStringLiteral("bullets") } },
        { QStringLiteral("numbered list"),     { QStringLiteral("ordered list"), QStringLiteral("ol"), QStringLiteral("numbers") } },
        { QStringLiteral("horizontal rule"),   { QStringLiteral("divider"), QStringLiteral("separator"), QStringLiteral("hr"), QStringLiteral("line") } },
        { QStringLiteral("footnote"),          { QStringLiteral("note"), QStringLiteral("reference") } },
        { QStringLiteral("page break"),        { QStringLiteral("pagebreak"), QStringLiteral("new page") } },
        { QStringLiteral("date & time (short format)"), { QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("timestamp"), QStringLiteral("now") } },
        { QStringLiteral("date & time (long format)"),  { QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("timestamp"), QStringLiteral("now") } },
        { QStringLiteral("date & time (custom)"),{ QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("timestamp") } },
        { QStringLiteral("emoji"),             { QStringLiteral("emoticon"), QStringLiteral("smiley"), QStringLiteral("symbol picker") } },
        { QStringLiteral("inline math"),       { QStringLiteral("math"), QStringLiteral("latex"), QStringLiteral("formula"), QStringLiteral("equation") } },
        { QStringLiteral("block math"),        { QStringLiteral("math"), QStringLiteral("latex"), QStringLiteral("formula"), QStringLiteral("equation"), QStringLiteral("display") } },
        { QStringLiteral("math toolbar"),      { QStringLiteral("symbols"), QStringLiteral("greek"), QStringLiteral("operators"), QStringLiteral("math") } },
        { QStringLiteral("chemistry toolbar"), { QStringLiteral("chemistry"), QStringLiteral("mhchem"), QStringLiteral("formula"), QStringLiteral("molecule") } },

        { QStringLiteral("word wrap"),         { QStringLiteral("wrap"), QStringLiteral("soft wrap"), QStringLiteral("line wrap") } },
        { QStringLiteral("preview panel"),     { QStringLiteral("preview"), QStringLiteral("render"), QStringLiteral("split view"), QStringLiteral("toggle preview") } },
        { QStringLiteral("reading mode"),      { QStringLiteral("read"), QStringLiteral("reader"), QStringLiteral("focus mode"), QStringLiteral("distraction free"), QStringLiteral("preview only"), QStringLiteral("hide editor"), QStringLiteral("zen") } },
        { QStringLiteral("view as mind map"),  { QStringLiteral("mindmap"), QStringLiteral("mind map"), QStringLiteral("map"), QStringLiteral("diagram") } },
        { QStringLiteral("go to preview"),     { QStringLiteral("preview"), QStringLiteral("jump"), QStringLiteral("focus preview") } },
        { QStringLiteral("go to source"),      { QStringLiteral("source"), QStringLiteral("editor"), QStringLiteral("focus editor") } },
        { QStringLiteral("preview in browser"),{ QStringLiteral("browser"), QStringLiteral("external preview") } },
        { QStringLiteral("workspace panel"),   { QStringLiteral("workspace"), QStringLiteral("explorer"), QStringLiteral("files"), QStringLiteral("sidebar") } },
        { QStringLiteral("status bar"),        { QStringLiteral("statusbar"), QStringLiteral("bottom bar") } },
        { QStringLiteral("outline"),           { QStringLiteral("headings"), QStringLiteral("structure"), QStringLiteral("toc"), QStringLiteral("navigate") } },
        { QStringLiteral("command palette"),   { QStringLiteral("commands"), QStringLiteral("palette") } },
        { QStringLiteral("document statistics"),  { QStringLiteral("statistics"), QStringLiteral("summary"), QStringLiteral("word count"), QStringLiteral("stats") } },
        { QStringLiteral("zoom in"),           { QStringLiteral("bigger"), QStringLiteral("larger"), QStringLiteral("font size") } },
        { QStringLiteral("zoom out"),          { QStringLiteral("smaller"), QStringLiteral("font size") } },
        { QStringLiteral("reset zoom"),        { QStringLiteral("default zoom"), QStringLiteral("font size") } },
        { QStringLiteral("light"),             { QStringLiteral("theme"), QStringLiteral("appearance") } },
        { QStringLiteral("dark"),              { QStringLiteral("theme"), QStringLiteral("appearance"), QStringLiteral("night") } },
        { QStringLiteral("system"),            { QStringLiteral("theme"), QStringLiteral("appearance"), QStringLiteral("auto") } },
        { QStringLiteral("switch language mode"), { QStringLiteral("language"), QStringLiteral("mode"), QStringLiteral("markdown"), QStringLiteral("plain text"), QStringLiteral("syntax") } },

        { QStringLiteral("check for updates"), { QStringLiteral("update"), QStringLiteral("upgrade"), QStringLiteral("version") } },
        { QStringLiteral("keyboard shortcuts"),{ QStringLiteral("keybindings"), QStringLiteral("hotkeys"), QStringLiteral("keys") } },
        { QStringLiteral("user guide"),        { QStringLiteral("help"), QStringLiteral("documentation"), QStringLiteral("manual"), QStringLiteral("tutorial"), QStringLiteral("getting started") } },
        { QStringLiteral("about fastmd"),      { QStringLiteral("about"), QStringLiteral("version"), QStringLiteral("info") } },
        { QStringLiteral("third-party licenses"), { QStringLiteral("license"), QStringLiteral("legal"), QStringLiteral("credits") } },

        { QStringLiteral("remove duplicate lines"), { QStringLiteral("dedupe"), QStringLiteral("deduplicate"), QStringLiteral("unique lines") } },
        { QStringLiteral("unique + sort"),      { QStringLiteral("dedupe"), QStringLiteral("deduplicate"), QStringLiteral("unique sort") } },
        { QStringLiteral("trim both"),          { QStringLiteral("trim lines"), QStringLiteral("trim whitespace") } },
        { QStringLiteral("convert smart quotes to straight quotes"), { QStringLiteral("curly quotes"), QStringLiteral("straighten quotes") } },
        { QStringLiteral("remove invisible characters"), { QStringLiteral("zero-width"), QStringLiteral("hidden characters"), QStringLiteral("non-printing") } },
    };
    return aliases;
}

QStringList aliasesFor(const QString& title)
{
    return titleAliases().value(title.toLower());
}

#ifdef Q_OS_WIN
// Windows has no programmatic emoji-insertion API; the shell's own emoji
// panel is opened with Win+. and inserts into whatever has keyboard focus.
void openWindowsEmojiPanel()
{
    INPUT inputs[4] = {};
    for (INPUT& input : inputs)
        input.type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].ki.wVk = VK_OEM_PERIOD;
    inputs[2].ki.wVk = VK_OEM_PERIOD;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].ki.wVk = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}
#endif

} // namespace

// ---------------------------------------------------------------------------
void MainWindow::setStatusBarVisible(bool visible)
{
    m_statusBarVisible = visible;
    if (QStatusBar* bar = statusBar())
        bar->setVisible(visible);
    if (m_actStatusBar && m_actStatusBar->isChecked() != visible)
        m_actStatusBar->setChecked(visible);
    writeSettings();
}

// ---------------------------------------------------------------------------
void MainWindow::goToSource()
{
    TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!page || !page->editor)
        return;
    leaveReadingModeForEditing();
    page->editor->setFocus(Qt::OtherFocusReason);
}

// ---------------------------------------------------------------------------
void MainWindow::ensureCommandPalette()
{
    if (m_commandPalette)
        return;

    m_commandRegistry = new CommandRegistry(this);
    registerCommands();

    m_commandPalette = new CommandPalettePopup(m_commandRegistry, this);
    m_commandPalette->setDarkMode(
        palette().color(QPalette::Window).lightness() < 128);
    connect(m_commandPalette, &CommandPalettePopup::visibilityChanged,
            this, [this](bool visible) {
        if (m_commandLauncher)
            m_commandLauncher->setActive(visible);
    });
}

void MainWindow::openCommandPalette()
{
    ensureCommandPalette();
    if (m_commandPalette->isVisible()) {
        m_commandPalette->hide();
        return;
    }
    m_commandPalette->showForAnchor(m_commandLauncher);
}

// ---------------------------------------------------------------------------
void MainWindow::openTerminalProfilePicker(bool selectDefault)
{
    ensureCommandPalette();
    const QList<TerminalProfile> profiles = TerminalProfiles::available();
    // A window whose Terminal has not been opened has no controller yet; the
    // persisted choice is what its first terminal would use.
    const QString defaultId = m_terminalPanel
        ? m_terminalPanel->defaultProfileId()
        : TerminalProfiles::resolvePreferred(
              profiles, TerminalController::persistedDefaultProfileId()).id;

    auto* choices = new CommandRegistry;
    for (const TerminalProfile& profile : profiles) {
        Command command;
        command.id = profile.id;
        command.title = profile.id == defaultId
            ? tr("%1 (Default)").arg(profile.name)
            : profile.name;
        command.keywords = { QFileInfo(profile.program).completeBaseName() };
        const QString id = profile.id;
        command.run = [this, id, selectDefault]() {
            if (selectDefault) {
                if (m_terminalPanel)
                    m_terminalPanel->setDefaultProfile(id);
                else
                    TerminalController::persistDefaultProfileId(id);
                return;
            }
            revealTerminal();
            if (m_terminalPanel)
                m_terminalPanel->openNewTerminalWithProfile(id);
        };
        choices->registerCommand(std::move(command));
    }
    m_commandPalette->showChoices(m_commandLauncher, choices,
        selectDefault ? tr("Select the default terminal profile")
                      : tr("Select a profile for the new terminal"),
        tr("No supported shell was found on this system."));
}

// ---------------------------------------------------------------------------
// Every command the palette offers is registered here. Menu and toolbar
// actions are harvested automatically, so a new menu item shows up in the
// palette without touching this file; only commands with no existing QAction
// need an explicit entry below.
void MainWindow::registerCommands()
{
    if (!m_commandRegistry)
        return;

    QSet<QString> seenTitles;

    auto add = [this, &seenTitles](const QString& category, const QString& title,
                                   QAction* action) {
        // De-duplicated by title alone, on purpose: the same command usually
        // exists as both a menu item and a toolbar button, and the palette
        // should list it once.
        const QString key = title.toLower();
        if (seenTitles.contains(key))
            return;
        seenTitles.insert(key);
        Command command;
        command.id = commandId(category, title);
        command.title = title;
        command.category = category;
        command.shortcut = action->shortcut();
        command.keywords = aliasesFor(title);
        command.run = [action]() { action->trigger(); };
        command.enabled = [action]() { return action->isEnabled(); };
        m_commandRegistry->registerCommand(std::move(command));
    };

    // ---- Menu bar (recursive) ----------------------------------------
    QMenu* recentFiles = m_recentMenu;
    QMenu* recentFolders = m_workspacePanelCtrl
        ? m_workspacePanelCtrl->recentFoldersManager()->menu() : nullptr;

    std::function<void(QMenu*, const QString&, const QStringList&)> walk =
        [&](QMenu* menu, const QString& topLevel, const QStringList& path) {
        for (QAction* action : menu->actions()) {
            if (action->isSeparator())
                continue;
            const QString text = cleanMenuText(action->text());
            if (text.isEmpty())
                continue;
            if (QMenu* sub = action->menu()) {
                // Recent Files/Folders are rebuilt from disk state; their
                // entries are documents, not commands.
                if (sub == recentFiles || sub == recentFolders)
                    continue;
                // Spell Check is registered explicitly below: a single
                // checkable QAction cannot produce the distinct "Enable" and
                // "Disable" commands the palette exposes.
                if (sub == m_spellCheckMenu)
                    continue;
                // Visible submenu labels may be shortened without changing
                // stable palette categories and command IDs.
                const QString paletteCategory = sub->property("commandPaletteCategory").toString();
                walk(sub, topLevel, path + QStringList{
                    paletteCategory.isEmpty() ? text : paletteCategory });
                continue;
            }
            QString category = topLevel;
            QStringList titleParts = path;
            if (!titleParts.isEmpty() && promotedSubmenus().contains(titleParts.first()))
                category = titleParts.takeFirst();
            const QString commandTitle = action->property("commandPaletteTitle").toString();
            titleParts.append(commandTitle.isEmpty() ? text : commandTitle);
            add(category, titleParts.join(QStringLiteral(" ")), action);
        }
    };

    for (QAction* menuAction : m_menuBar->actions()) {
        QMenu* menu = menuAction->menu();
        if (!menu)
            continue;
        walk(menu, cleanMenuText(menuAction->text()), {});
    }

    // ---- Toolbar-only actions ----------------------------------------
    static const QSet<QString> toolbarFileActions = {
        QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("save"),
        QStringLiteral("print"), QStringLiteral("export pdf"),
        QStringLiteral("export docx"), QStringLiteral("preview in browser"),
    };
    for (const TbIcon& icon : std::as_const(m_tbIcons)) {
        // The mode toggle's label follows the current document language, so it
        // would register under whatever title happened to be showing. It gets
        // an explicit, stable "Switch Language Mode" command below instead.
        if (!icon.action || icon.action == m_actModeToggle
            || icon.action->property("commandPaletteHidden").toBool())
            continue;
        const QString actionTitle = cleanMenuText(icon.action->text());
        const QString commandTitle = icon.action->property("commandPaletteTitle").toString();
        const QString title = commandTitle.isEmpty() ? actionTitle : commandTitle;
        if (title.isEmpty() || title.compare(QStringLiteral("Find and Replace"),
                                             Qt::CaseInsensitive) == 0)
            continue;
        const QString category = icon.action->property("commandPaletteCategory").toString();
        add(category.isEmpty()
                ? (toolbarFileActions.contains(title.toLower()) ? tr("File") : tr("Insert"))
                : category,
            title, icon.action);
    }

    // ---- Window-level actions outside the menu bar -------------------
    // Focus File Search is a real QAction, but it is owned by the window
    // instead of the View menu (see MainWindow::createMenus()), so the
    // recursive menu walk above cannot find it.
    if (m_actFocusFileSearch)
        add(tr("View"), cleanMenuText(m_actFocusFileSearch->text()), m_actFocusFileSearch);

    // ---- Commands with no backing QAction ----------------------------
    auto addCustom = [this](const QString& category, const QString& title,
                            std::function<void()> run,
                            std::function<bool()> enabled = nullptr,
                            const QKeySequence& shortcut = {}) {
        Command command;
        command.id = commandId(category, title);
        command.title = title;
        command.category = category;
        command.shortcut = shortcut;
        command.keywords = aliasesFor(title);
        command.run = std::move(run);
        command.enabled = std::move(enabled);
        m_commandRegistry->registerCommand(std::move(command));
    };

    auto hasEditor = [this]() { return m_activeEditor != nullptr; };
    auto isMarkdown = [this]() {
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        return m_activeEditor && model && model->editorMode() == EditorMode::Markdown;
    };

    addCustom(tr("File"), tr("Close All Tabs"), [this]() {
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            onTabCloseRequested(i);
    }, [this]() { return m_tabs && m_tabs->count() > 0; });

    // The tab context menu is built on demand, so its actions cannot be
    // harvested by the palette. These mirror them against the current tab,
    // which is the palette's only meaningful tab target. Its plain "Close"
    // is deliberately absent: File > Close Tab (Ctrl+W) already is it.
    addCustom(tr("File"), tr("Close Other Tabs"), [this]() {
        if (!m_tabs)
            return;
        TabPage* keepPage = m_tabs->currentPage();
        if (!keepPage)
            return;
        for (int i = m_tabs->count() - 1; i >= 0; --i) {
            if (m_tabs->pageAt(i) != keepPage)
                onTabCloseRequested(i);
        }
    }, [this]() { return m_tabs && m_tabs->count() > 1; });

    addCustom(tr("File"), tr("Close Tabs to the Right"), [this]() {
        if (!m_tabs)
            return;
        const int current = m_tabs->currentIndex();
        for (int i = m_tabs->count() - 1; i > current; --i)
            onTabCloseRequested(i);
    }, [this]() {
        return m_tabs && m_tabs->currentIndex() >= 0
            && m_tabs->currentIndex() < m_tabs->count() - 1;
    });

    addCustom(tr("File"), tr("Close Tabs to the Left"), [this]() {
        if (!m_tabs)
            return;
        const int current = m_tabs->currentIndex();
        for (int i = current - 1; i >= 0; --i)
            onTabCloseRequested(i);
    }, [this]() {
        return m_tabs && m_tabs->currentIndex() > 0;
    });

    addCustom(tr("File"), tr("Clear Recent Files"), [this]() {
        if (m_recentFilesMgr)
            m_recentFilesMgr->clear();
    }, [this]() {
        return m_recentFilesMgr && !m_recentFilesMgr->files().isEmpty();
    });

    addCustom(tr("File"), tr("Clear Recent Folders"), [this]() {
        if (m_workspacePanelCtrl)
            m_workspacePanelCtrl->recentFoldersManager()->clear();
    }, [this]() {
        return m_workspacePanelCtrl
            && !m_workspacePanelCtrl->recentFoldersManager()->folders().isEmpty();
    });

    addCustom(tr("Insert"), tr("Mermaid Diagram"), [this]() {
        if (m_activeEditor) m_activeEditor->fmtMermaidBlock();
    }, isMarkdown);

    addCustom(tr("Insert"), tr("Insert Matrix"), [this]() {
        openMatrixInsertDialog();
    }, isMarkdown);

    addCustom(tr("Insert"), tr("Browse Image"), [this]() {
        if (!m_activeEditor)
            return;
        TabPage* page = m_tabs ? m_tabs->currentPage() : nullptr;
        m_activeEditor->fmtImage(page ? page->model->filePath() : QString());
    }, isMarkdown);

    addCustom(tr("Edit"), tr("Indent Selection"), [this]() {
        if (m_activeEditor)
            m_activeEditor->indentSelection();
    }, hasEditor, QKeySequence(Qt::Key_Tab));

    addCustom(tr("Edit"), tr("Unindent Selection"), [this]() {
        if (m_activeEditor)
            m_activeEditor->unindentSelection();
    }, hasEditor, QKeySequence(Qt::SHIFT | Qt::Key_Tab));

    addCustom(tr("Insert"), tr("Emoji"), [this]() {
        if (!m_activeEditor)
            return;
        m_activeEditor->setFocus(Qt::OtherFocusReason);
#ifdef Q_OS_WIN
        // Give focus a turn of the event loop to settle before the shell
        // panel steals it, otherwise the emoji lands nowhere.
        QTimer::singleShot(60, this, []() { openWindowsEmojiPanel(); });
#endif
    }, hasEditor);

    addCustom(tr("Navigation"), tr("Go to Preview"), [this]() {
        goToPreview();
    }, isMarkdown, m_actGoToPreview ? m_actGoToPreview->shortcut() : QKeySequence());

    addCustom(tr("Navigation"), tr("Go to Source"), [this]() {
        goToSource();
    }, hasEditor);

    addCustom(tr("Navigation"), tr("Switch Language Mode"), [this]() {
        openLanguageSelector();
    }, [this]() {
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        return model && !model->isViewer();
    });

    // The detach button normally only exists after the Terminal panel has
    // been opened. The palette command deliberately combines those two steps,
    // so it also works when the panel has not yet been created.
    addCustom(tr("View"), tr("Detach Terminal"), [this]() {
        setTerminalVisible(true);
        detachTerminalPanel();
    });

    // Its counterpart, so the palette can express both directions of the
    // header's one reversible control. A terminal that is already docked (or
    // has not been opened yet) simply ends up open and docked.
    addCustom(tr("View"), tr("Attach Terminal"), [this]() {
        setTerminalVisible(true);
        reattachTerminalPanel();
    });

    // The Terminal's header buttons and overflow menu are built on demand
    // inside the lazily created panel, so none of them can be harvested.
    TerminalCommands::registerAll(m_commandRegistry, [this]() {
        TerminalCommands::State state;
        if (m_terminalPanel) {
            state.panelExists = true;
            state.hasActiveSession = m_terminalPanel->hasActiveSession();
            state.sessionCount = m_terminalPanel->sessionCount();
            state.fontSize = m_terminalPanel->fontSize();
            state.themeMode = m_terminalPanel->themeMode();
        }
        return state;
    }, [this](TerminalCommands::Id id) {
        using TerminalCommands::Id;
        switch (id) {
        case Id::NewTerminal:
            revealTerminal();
            if (m_terminalPanel)
                m_terminalPanel->openNewTerminalWithProfile(QString());
            return;
        case Id::NewTerminalWithProfile: openTerminalProfilePicker(false); return;
        case Id::SelectDefaultProfile:   openTerminalProfilePicker(true); return;
        case Id::FocusTerminal:          revealTerminal(); return;
        default: break;
        }
        if (!m_terminalPanel)
            return;
        using Mode = TerminalPanel::ThemeMode;
        switch (id) {
        case Id::CloseTerminal:    m_terminalPanel->closeActiveSession(); break;
        case Id::NextTerminal:     m_terminalPanel->activateAdjacentSession(1); revealTerminal(); break;
        case Id::PreviousTerminal: m_terminalPanel->activateAdjacentSession(-1); revealTerminal(); break;
        case Id::Copy:             m_terminalPanel->copySelection(); break;
        case Id::Paste:            m_terminalPanel->pasteClipboard(); revealTerminal(); break;
        case Id::SelectAll:        m_terminalPanel->selectAll(); revealTerminal(); break;
        case Id::Clear:            m_terminalPanel->clearTerminal(); revealTerminal(); break;
        case Id::IncreaseFontSize: m_terminalPanel->stepFontSize(1); break;
        case Id::DecreaseFontSize: m_terminalPanel->stepFontSize(-1); break;
        case Id::ResetFontSize:    m_terminalPanel->resetFontSize(); break;
        case Id::UseLightTheme:    m_terminalPanel->setThemeMode(Mode::Light); break;
        case Id::UseDarkTheme:     m_terminalPanel->setThemeMode(Mode::Dark); break;
        case Id::FollowAppTheme:   m_terminalPanel->setThemeMode(Mode::FollowApp); break;
        default: break;
        }
    }, [](const QString& title) { return aliasesFor(title); });

    // The Workspace header is built from buttons, not QActions, so its two
    // root-scoped commands need explicit palette entries. Resolve the root at
    // invocation time because it may change while the palette stays alive.
    auto hasWorkspace = [this]() {
        return m_workspacePanelCtrl
            && QFileInfo(m_workspacePanelCtrl->rootPath()).isDir();
    };
    addCustom(tr("Workspace"), tr("Reveal Workspace in Explorer"), [this]() {
        WorkspaceFileOps::revealWorkspaceInExplorer(m_workspacePanelCtrl->rootPath());
    }, hasWorkspace);
    addCustom(tr("Workspace"), tr("Open Workspace in Terminal"), [this]() {
        openTerminalAtWorkspace(m_workspacePanelCtrl->rootPath());
    }, hasWorkspace);

    // The only external-terminal action is active-file scoped. The remaining
    // folder-terminal actions open FastMD's Internal Terminal.
    addCustom(tr("File"), tr("Open Active File Folder in External Terminal"), [this]() {
        openCurrentFolderInExternalTerminal();
    }, [this]() {
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        return model && QFileInfo(model->filePath()).isFile();
    });

    // The encoding menu (m_encodingActions) is built lazily on first use by
    // openEncodingMenu() rather than living in the menu bar, so it is never
    // seen by the harvester above; these call the same applyDocumentEncoding()
    // mutation path directly instead of duplicating it.
    auto isEditableDoc = [this]() {
        const DocumentModel* model = m_tabs ? m_tabs->currentModel() : nullptr;
        return model && !model->isViewer() && !model->isBundled();
    };
    for (TextEncoding encoding : { TextEncoding::Utf8, TextEncoding::Utf8Bom,
                                   TextEncoding::Utf16Le, TextEncoding::Utf16Be }) {
        addCustom(tr("Encoding"), tr("Convert Encoding to %1").arg(textEncodingLabel(encoding)),
                  [this, encoding]() { applyDocumentEncoding(encoding); }, isEditableDoc);
    }

    // Spell Check. The palette renders "<category>: <title>", so these read as
    // "Spell Check: Enable" and so on. Every one drives the same shared state
    // as the Edit menu and the status-bar popup.
    addCustom(tr("Spell Check"), tr("Enable"), [this]() {
        applySpellCheckState(true, m_spellCheckLanguage);
    }, [this]() {
        return !spellCheckEffectivelyOn();
    });
    addCustom(tr("Spell Check"), tr("Disable"), [this]() {
        applySpellCheckState(false, m_spellCheckLanguage);
    }, [this]() {
        return spellCheckEffectivelyOn();
    });
    addCustom(tr("Spell Check"), tr("Select Language"), [this]() {
        openSpellCheckMenu();
    });
    addCustom(tr("Spell Check"), tr("Fix All Misspellings"), [this]() {
        fixAllMisspellings();
    }, [this]() {
        return m_actSpellFixAll && m_actSpellFixAll->isEnabled();
    });
    addCustom(tr("Spell Check"), tr("Open Settings"), [this]() {
        openEditorPreferences();
    });

    // "Settings: ..." commands only open Preferences on one control. Nothing
    // here builds the dialog; that happens when one of them runs.
    PreferencesTargets::registerAll(m_commandRegistry, [this](PreferencesTargets::Id id) {
        showPreferences(id);
    }, [](const QString& title) { return aliasesFor(title); });

    // Deliberately no per-language commands here. The registry is built once,
    // on the first palette open, which is usually before any spell-check
    // surface has enumerated the installed languages - so they would be
    // registered only for users who happened to open Preferences or the status
    // popup first, and be missing for everyone else. Enumerating eagerly to fix
    // that would put a COM call on the first F1. "Spell Check: Select Language"
    // opens the status-bar popup, which always lists every installed language.
}
