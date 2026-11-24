#include "MarkdownEditor.h"
#include "ApplicationTheme.h"
#include "DarkTheme.h"
#include "FontPreferences.h"
#include "StartupProfiler.h"

#include <QSignalBlocker>
#include "MarkdownHighlighter.h"
#include "SourceHighlighter.h"
#include "SyntaxDefinitionResolver.h"
#include "ExportManager.h"
#include "FastScrollBar.h"
#include "TextTransforms.h"
#include "IconHelper.h"
#include "CompactComboBox.h"
#include "CommandShortcutRegistry.h"
#include "GutterSelection.h"

#include <QPainter>
#include <QRegion>
#include <QTextBlock>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QTextLayout>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QMimeData>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QMessageBox>
#include "AlignedMenu.h"
#include "MenuCommandBar.h"
#include "WorkspaceFileOps.h"
#include "SpellChecker.h"
#include "SpellCheckExclusions.h"
#include "MarkdownExtensions.h"

#include <windows.h>
#include <utility>

#ifdef Q_OS_WIN
// Defined in MainWindow.cpp; sets the native title bar to dark/light mode.
bool setWindowsTitleBarDarkMode(QWidget* window, bool dark, bool includeCaptionColor = true);
#endif

namespace {
constexpr int kEditorTopPadding = 0;
constexpr int kLineNumberGutterGap = 0;

// Alpha (out of 255) blended over the editor background for the current-line
// highlight. Focused uses the full, still-subtle strength; unfocused fades it
// toward invisible rather than removing it outright, so the caret's line
// doesn't visually "jump" when focus briefly leaves (e.g. dialogs, other panes).
//
// Tuned per theme, not as a shared delta: the old alpha=14 blended to ~#f1f1f1
// (light) / ~#2a2a2a (dark), both of which land almost exactly on the tab
// bar/chrome background (#f0f0f0 light, #2d2d2d dark in Stylesheet.h), making
// the active line read as an extension of the chrome rather than a subtle
// in-editor highlight. These values instead target roughly 45% of the way
// from the chrome color toward the editor background (#ffffff/#1e1e1e), which
// happens to need close but not identical alpha in each theme.
constexpr int kCurrentLineAlphaFocusedLight = 9;
constexpr int kCurrentLineAlphaUnfocusedLight = 3;
constexpr int kCurrentLineAlphaFocusedDark = 10;
constexpr int kCurrentLineAlphaUnfocusedDark = 3;

bool isPageBreakBlock(const QTextBlock& block)
{
    return block.isValid() && MarkdownExtensions::isPageBreakLine(block.text());
}

int blockTextEnd(const QTextBlock& block)
{
    return block.position() + block.text().length();
}

bool isImageFilePath(const QString& path)
{
    static const QStringList exts = {
        QStringLiteral("png"),
        QStringLiteral("xpm"),
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("gif"),
        QStringLiteral("svg"),
        QStringLiteral("bmp"),
        QStringLiteral("webp")
    };

    const QString suffix = QFileInfo(path).suffix().toLower();
    return exts.contains(suffix);
}

QString imageAssetFileName(const QDir& assetDir, QString baseName, QString suffix)
{
    baseName.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    baseName = baseName.trimmed();
    if (baseName.isEmpty())
        baseName = QStringLiteral("image");
    suffix = suffix.trimmed();
    if (suffix.isEmpty())
        suffix = QStringLiteral("png");

    const QString extension = QStringLiteral(".") + suffix;
    QString candidate = baseName + extension;
    int number = 2;
    while (assetDir.exists(candidate))
        candidate = baseName + QStringLiteral(" (") + QString::number(number++) + QStringLiteral(")") + extension;
    return candidate;
}

QString timestampImageBaseName()
{
    return QStringLiteral("image-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hHmmss"));
}

QString escapedMarkdownPath(QString path)
{
    if (path.contains(' ') || path.contains('[') || path.contains(']'))
        path = QStringLiteral("<") + path + QStringLiteral(">");
    return path;
}

bool moveImageFile(const QString& source, const QString& destination)
{
    if (QFile::rename(source, destination))
        return true;
    if (!QFile::copy(source, destination))
        return false;
    if (QFile::remove(source))
        return true;
    QFile::remove(destination);
    return false;
}

int clampedBlockNumber(const QTextDocument* document, int blockNumber)
{
    return qBound(0, blockNumber, qMax(0, document->blockCount() - 1));
}

bool isValidHttpOrHttpsUrl(const QString& text)
{
    if (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')))
        return false;

    if (text.contains(QLatin1Char(' ')) || text.contains(QLatin1Char('\t')))
        return false;

    QUrl url(text);
    if (!url.isValid())
        return false;

    QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return false;

    if (url.host().isEmpty())
        return false;

    return true;
}

bool isSelectionOrCursorInsideLink(const QTextCursor& c)
{
    auto checkLine = [](const QString& lineText, int start, int end) -> bool {
        int idx = 0;
        while ((idx = lineText.indexOf(QLatin1Char('['), idx)) != -1) {
            int closeBracket = lineText.indexOf(QLatin1Char(']'), idx + 1);
            if (closeBracket == -1) {
                idx++;
                continue;
            }
            
            int linkEnd = -1;
            if (closeBracket + 1 < lineText.length() && lineText.at(closeBracket + 1) == QLatin1Char('(')) {
                int closeParen = lineText.indexOf(QLatin1Char(')'), closeBracket + 2);
                if (closeParen != -1) {
                    linkEnd = closeParen;
                }
            } else if (closeBracket + 1 < lineText.length() && lineText.at(closeBracket + 1) == QLatin1Char('[')) {
                int closeSecondBracket = lineText.indexOf(QLatin1Char(']'), closeBracket + 2);
                if (closeSecondBracket != -1) {
                    linkEnd = closeSecondBracket;
                }
            }
            
            if (linkEnd != -1) {
                int startPos = idx;
                if (idx > 0 && lineText.at(idx - 1) == QLatin1Char('!')) {
                    startPos = idx - 1;
                }
                
                int linkStart = startPos + 1;
                if (qMax(start, linkStart) <= qMin(end, linkEnd)) {
                    return true;
                }
            }
            idx = closeBracket + 1;
        }
        return false;
    };

    QTextBlock startBlock = c.document()->findBlock(c.selectionStart());
    QTextBlock endBlock = c.document()->findBlock(c.selectionEnd());

    if (startBlock == endBlock) {
        int startInLine = c.selectionStart() - startBlock.position();
        int endInLine = c.selectionEnd() - startBlock.position();
        return checkLine(startBlock.text(), startInLine, endInLine);
    } else {
        int startInStartBlock = c.selectionStart() - startBlock.position();
        int endInStartBlock = startBlock.text().length();
        if (checkLine(startBlock.text(), startInStartBlock, endInStartBlock))
            return true;
            
        int startInEndBlock = 0;
        int endInEndBlock = c.selectionEnd() - endBlock.position();
        if (checkLine(endBlock.text(), startInEndBlock, endInEndBlock))
            return true;
    }
    
    return false;
}
}

// ---------------------------------------------------------------------------
QFont MarkdownEditor::monospaceFont(int fontPixelSize)
{
    // Exactly one family, on purpose. Qt on Windows treats a multi-family
    // QFont as a request to build its own Unicode fallback list, which it does
    // by opening *every* installed family (qfontdatabase.cpp:814 ->
    // QPlatformFontDatabase::fallbacksForFamily). That cost is linear in the
    // installed font count and lands on whichever thread first realizes the
    // engine -- during startup, the GUI thread, via the editor's first block
    // layout. One family closes that gate; missing glyphs still resolve
    // through Qt's ordinary lazy per-glyph fallback.
    //
    // The same list must not reappear in QSS: QStyleSheetStyle's QPlainTextEdit
    // font wins over this setFont(), so src/Stylesheet.h names one family too.
    // Registering a QFont::insertSubstitution() for Consolas would also reopen
    // the gate -- familyList() folds substitutes into fallBackFamilies.
    // A user-chosen Editor font replaces Consolas here and in the QSS
    // (FontPreferences::editorStyleRule()); it is never added in front of it.
    QFont font;
    font.setFamilies({ FontPreferences::editorFamilyOrDefault(ApplicationTheme::editorFontFamily()) });
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(fontPixelSize > 0 ? fontPixelSize : FastMdDefaults::EditorFontSize);
    font.setFixedPitch(true);
    return font;
}

// ---------------------------------------------------------------------------
MarkdownEditor::MarkdownEditor(QWidget* parent, int fontPixelSize)
    : QPlainTextEdit(parent)
{
    FASTMD_MARK("      editor: QPlainTextEdit constructed");
    setVerticalScrollBar(new FastScrollBar(Qt::Vertical, this));
    setHorizontalScrollBar(new FastScrollBar(Qt::Horizontal, this));
    FASTMD_MARK("      editor: scrollbars installed");

    setFont(monospaceFont(fontPixelSize));
    FASTMD_MARK("      editor: font configured");

    // QPlainTextEdit leaves a 4-DIP document margin by default.  The editor
    // sits directly below the tab strip, so that inset reads as an unintended
    // empty band before the first line.
    document()->setDocumentMargin(0);

    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * 4);
    FASTMD_MARK("      editor: layout configured");

    m_lineNumbers = new LineNumberArea(this);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &MarkdownEditor::onBlockCountChanged);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &MarkdownEditor::onUpdateRequest);
    connect(m_debounce, &QTimer::timeout,
            this,       &MarkdownEditor::contentReady);
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &MarkdownEditor::updateLineHighlight);

    // An empty editor has no syntax to highlight. KSyntaxHighlighting's
    // definition repository is created lazily by ensureMarkdownHighlighter(),
    // called from setPlainText()/setMarkdownMode()/onTextChanged() once real
    // content exists - a blank untitled document never pays that cost.
    onBlockCountChanged(0);

    QTextCursor cursor(document());
    QTextBlockFormat format = cursor.blockFormat();
    format.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
    cursor.setBlockFormat(format);

    // Constructor-only block formatting emits contentsChanged(). Connecting
    // after it prevents an empty tab from treating that internal setup as the
    // user's first edit and eagerly constructing the syntax repository.
    connect(document(), &QTextDocument::contentsChanged,
            this, &MarkdownEditor::onTextChanged);
    FASTMD_MARK("      editor: document configured");

    // Registered eagerly (not lazily with the other context-menu-only actions)
    // so the shortcut is live even before the context menu has ever been opened.
    m_contextReveal = new QAction(tr("Reveal in Explorer"), this);
    m_contextReveal->setIcon(IconHelper::materialMenuIcon(QChar(0xE89E), palette().color(QPalette::Text)));
    m_contextReveal->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::FileRevealInExplorer));
    m_contextReveal->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_contextReveal->setEnabled(!m_documentPath.isEmpty());
    connect(m_contextReveal, &QAction::triggered, this, [this]() {
        WorkspaceFileOps::revealFileInExplorer(m_documentPath);
    });
    addAction(m_contextReveal);
    FASTMD_MARK("      editor: constructed");
}

// ---------------------------------------------------------------------------
void MarkdownEditor::setDarkMode(bool dark)
{
    QSignalBlocker signalBlocker(this);
    m_dark = dark;
    if (m_highlighter)
        m_highlighter->setDarkMode(dark);
    if (m_sourceHighlighter)
        m_sourceHighlighter->setDarkMode(dark);

    if (m_contextFormatDocument) {
        const QColor iconColor = dark ? DarkTheme::color(DarkTheme::EditorText) : QColor(0x22, 0x22, 0x22);
        m_contextFormatDocument->setIcon(IconHelper::materialMenuIcon(QChar(0xE23C), iconColor));
        if (m_contextCopyPath)
            m_contextCopyPath->setIcon(IconHelper::materialMenuIcon(QChar(0xE14D), iconColor));
        if (m_contextReveal)
            m_contextReveal->setIcon(IconHelper::materialMenuIcon(QChar(0xE89E), iconColor));
        if (m_contextTerminal)
            m_contextTerminal->setIcon(IconHelper::materialMenuIcon(QChar(0xEB8E), iconColor));
    }

    QPalette p = palette();
    if (dark) {
        p.setColor(QPalette::Base,  DarkTheme::color(DarkTheme::Editor));
        p.setColor(QPalette::Text,  DarkTheme::color(DarkTheme::EditorText));
    } else {
        p.setColor(QPalette::Base,  Qt::white);
        p.setColor(QPalette::Text,  QColor(0x22, 0x22, 0x22));
    }
    setPalette(p);
    m_lineNumbers->update();
    updateLineHighlight();
}

void MarkdownEditor::setCurrentLineHighlightEnabled(bool enabled)
{
    if (m_currentLineHighlightEnabled == enabled)
        return;
    m_currentLineHighlightEnabled = enabled;
    updateLineHighlight();
}

void MarkdownEditor::focusInEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusInEvent(event);
    updateLineHighlight();
    if (hasBlockSelection())
        viewport()->update();
}

void MarkdownEditor::focusOutEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusOutEvent(event);
    updateLineHighlight();
    if (hasBlockSelection())
        viewport()->update();
}

void MarkdownEditor::setPlainText(const QString& text)
{
    if (m_markdownMode)
        ensureMarkdownHighlighter();
    if (m_highlighter)
        m_highlighter->beginBulkLoad();
    // One edit block around the text replacement and the line-height merge,
    // the way QWidgetTextControl pairs setPlainText() with its char format:
    // the document then reports a single contentsChange() for both, so the
    // layout makes one whole-document pass, not two. The highlighter sits that
    // change out and highlights progressively from endBulkLoad().
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    QPlainTextEdit::setPlainText(text);
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
    if (m_highlighter)
        m_highlighter->endBulkLoad();
}

void MarkdownEditor::setMarkdownMode(bool markdown)
{
    const KSyntaxHighlighting::Definition definition = markdown
        ? KSyntaxHighlighting::Definition()
        : SyntaxDefinitionResolver::resolve(m_documentPath, EditorMode::PlainText);
    setSyntaxDefinition(definition, markdown);
}

void MarkdownEditor::setSyntaxDefinition(const KSyntaxHighlighting::Definition& definition, bool markdown)
{
    // A highlighting pass only writes character formats, but every one of them
    // still ends an edit block, so the document emits contentsChanged() and
    // QPlainTextEdit re-emits it as textChanged() -- which MainWindow reads as
    // a real edit and would use to dirty the tab and schedule an auto-save.
    // Block this widget's own signals rather than the document's: the document
    // notifications are what drive the viewport repaint and this editor's
    // preview debounce, so blocking those instead would leave the new
    // highlighting unpainted. (Same technique as setDarkMode() above.)
    QSignalBlocker editorBlocker(this);

    if (m_markdownMode == markdown) {
        if (!markdown && m_sourceHighlighter)
            m_sourceHighlighter->setDefinition(definition);
        repaintAfterHighlightingChange();
        return;
    }

    const bool wasModified = document()->isModified();
    m_markdownMode = markdown;
    if (markdown) {
        delete m_sourceHighlighter;
        m_sourceHighlighter = nullptr;
        ensureMarkdownHighlighter();
    } else {
        delete m_highlighter;
        m_highlighter = nullptr;
        m_sourceHighlighter = new SourceHighlighter(document());
        m_sourceHighlighter->setDarkMode(m_dark);
        m_sourceHighlighter->setDefinition(definition);
        m_sourceHighlighter->rehighlight();
    }
    document()->setModified(wasModified);
    repaintAfterHighlightingChange();
}

// Blocking this widget's signals above also suppresses updateRequest(), which
// normally drives the line-number area's repaint, so ask for it explicitly --
// same reason setDarkMode() ends with m_lineNumbers->update().
void MarkdownEditor::repaintAfterHighlightingChange()
{
    viewport()->update();
    m_lineNumbers->update();
}

void MarkdownEditor::ensureMarkdownHighlighter()
{
    if (m_highlighter)
        return;

    // The highlighting pass runs only once the member is assigned: it emits
    // contentsChanged, which comes back through onTextChanged() into this
    // function, and only a set m_highlighter stops that from recursing.
    m_highlighter = new MarkdownHighlighter(document(), m_dark);
    m_highlighter->rehighlight();
    FASTMD_MARK("first highlight complete");
}

void MarkdownEditor::setWordWrapEnabled(bool enabled)
{
    setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    setWordWrapMode(enabled ? QTextOption::WrapAtWordBoundaryOrAnywhere
                            : QTextOption::NoWrap);
}

void MarkdownEditor::setContextMenuActions(QAction* wordWrap, QAction* preview, QAction* goToPreview)
{
    m_contextWordWrap = wordWrap;
    m_contextPreview = preview;
    m_contextGoToPreview = goToPreview;
}

void MarkdownEditor::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* standardMenu = createStandardContextMenu();

    // Reuse one AlignedMenu across every right-click. Constructing a fresh one
    // per invocation would also re-create its QPropertyAnimation objects right
    // before exec(), causing a stutter on the very frame the menu appears.
    if (!m_contextMenu) {
        m_contextMenu = new AlignedMenu(this);
        m_contextMenu->setObjectName(QStringLiteral("editorContextMenu"));
        m_contextMenu->setCompactShortcutLayout(true);
        m_contextMenu->setShowAnimationStyle(AlignedMenu::ContextMenu);
        m_contextMenu->setShowAnimationDuration(80);
    }

    // Qt names the standard text-control actions ("edit-cut", "select-all", ...);
    // the five clipboard/selection commands are lifted out into the top command
    // bar and anything else (Undo, Redo, plus whatever a future Qt adds) stays
    // in the vertical list in its original order.
    QAction* stdCut = nullptr;
    QAction* stdCopy = nullptr;
    QAction* stdPaste = nullptr;
    QAction* stdDelete = nullptr;
    QAction* stdSelectAll = nullptr;
    QList<QAction*> verticalActions;
    for (QAction* action : standardMenu->actions()) {
        if (!action || action->isSeparator())
            continue;
        const QString name = action->objectName();
        if (name == QLatin1String("edit-cut"))              stdCut = action;
        else if (name == QLatin1String("edit-copy"))        stdCopy = action;
        else if (name == QLatin1String("edit-paste"))       stdPaste = action;
        else if (name == QLatin1String("edit-delete"))      stdDelete = action;
        else if (name == QLatin1String("select-all"))       stdSelectAll = action;
        else                                                 verticalActions.append(action);
    }

    if (!m_contextCommandBar)
        m_contextCommandBar = MenuCommandBar::create(this);
    m_contextCommandBar->setIconColor(palette().color(QPalette::Text));
    m_contextCommandBar->setCommands(stdCut, stdCopy, stdPaste, stdSelectAll, stdDelete);
    m_contextCommandBar->attachTo(m_contextMenu);

    // Spelling actions are added only when the click landed on a word this
    // editor has already flagged; everything below is untouched otherwise.
    if (m_spellCheckActive && !m_spellRanges.isEmpty()) {
        const int clicked = cursorForPosition(event->pos()).position();
        const QPair<int, int> range = spellRangeAt(clicked);
        if (range.first >= 0) {
            QTextCursor wordCursor(document());
            wordCursor.setPosition(range.first);
            wordCursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
            const QString word = wordCursor.selectedText();

            SpellChecker& checker = SpellChecker::instance();
            const QStringList suggestions = checker.suggestions(word);
            const int start = range.first;
            const int length = range.second;

            if (suggestions.isEmpty()) {
                QAction* none = m_contextMenu->addAction(tr("No spelling suggestions"));
                none->setEnabled(false);
            } else {
                for (int i = 0; i < suggestions.size() && i < 5; ++i) {
                    const QString suggestion = suggestions.at(i);
                    QAction* action = m_contextMenu->addAction(suggestion);
                    if (i == 0) {
                        // The checker returns its best candidate first; bolding
                        // it makes the likely pick readable at a glance.
                        QFont font = m_contextMenu->font();
                        font.setBold(true);
                        action->setFont(font);
                    }
                    connect(action, &QAction::triggered, this, [this, start, length, suggestion]() {
                        replaceRange(start, length, suggestion);
                    });
                }
            }
            m_contextMenu->addSeparator();
            QAction* ignore = m_contextMenu->addAction(tr("Ignore Word"));
            connect(ignore, &QAction::triggered, this, [word]() {
                SpellChecker::instance().ignoreWord(word);
            });
            QAction* addWord = m_contextMenu->addAction(tr("Add to Dictionary"));
            connect(addWord, &QAction::triggered, this, [word]() {
                SpellChecker::instance().addToDictionary(word);
            });
            m_contextMenu->addSeparator();
        }
    }

    for (QAction* action : std::as_const(verticalActions)) {
        action->setProperty("alignedMenuShowShortcut", !action->shortcut().isEmpty());
        m_contextMenu->addAction(action);
    }
    m_contextMenu->addSeparator();

    if (!m_contextFormatDocument) {
        m_contextFormatDocument = new QAction(tr("Auto Format Document"), this);
        m_contextFormatDocument->setShortcut(CommandShortcutRegistry::defaultShortcut(CommandId::FormatAutoFormatDocument));
        m_contextFormatDocument->setIcon(
            IconHelper::materialMenuIcon(QChar(0xE23C), palette().color(QPalette::Text)));
        m_contextFormatDocument->setProperty("alignedMenuShowShortcut", true);
        connect(m_contextFormatDocument, &QAction::triggered, this, [this]() {
            if (textCursor().hasSelection())
                formatSelection();
            else
                formatDocument();
        });
    }
    const bool hasSelection = textCursor().hasSelection();
    m_contextFormatDocument->setText(hasSelection ? tr("Auto Format Selection") : tr("Auto Format Document"));
    m_contextFormatDocument->setEnabled(m_markdownMode);
    // m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_contextFormatDocument);

    if (m_contextGoToPreview) {
        m_contextGoToPreview->setEnabled(m_markdownMode);
        m_contextMenu->addAction(m_contextGoToPreview);
    }

    if (!m_contextCopyPath) {
        m_contextCopyPath = new QAction(tr("Copy File Path"), this);
        m_contextTerminal = new QAction(tr("Open Folder in Terminal"), this);
        const QColor iconColor = palette().color(QPalette::Text);
        m_contextCopyPath->setIcon(IconHelper::materialMenuIcon(QChar(0xE14D), iconColor));
        m_contextTerminal->setIcon(IconHelper::materialMenuIcon(QChar(0xEB8E), iconColor));
        connect(m_contextCopyPath, &QAction::triggered, this, [this]() {
            QApplication::clipboard()->setText(
                QDir::toNativeSeparators(QFileInfo(m_documentPath).absoluteFilePath()));
        });
        connect(m_contextTerminal, &QAction::triggered, this, [this]() {
            emit terminalRequested(m_documentPath);
        });
    }
    const bool hasSavedPath = !m_documentPath.isEmpty();
    m_contextCopyPath->setEnabled(hasSavedPath);
    m_contextReveal->setEnabled(hasSavedPath);
    m_contextTerminal->setEnabled(hasSavedPath);

    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_contextCopyPath);
    m_contextMenu->addAction(m_contextReveal);
    m_contextMenu->addAction(m_contextTerminal);

    if (m_contextWordWrap && m_contextPreview) {
        m_contextMenu->addSeparator();
        m_contextMenu->addAction(m_contextWordWrap);
        m_contextMenu->addAction(m_contextPreview);
    }


    m_contextMenu->exec(event->globalPos());

    // The actions just added are owned by standardMenu; drop our references
    // before it's destroyed so none linger as dangling pointers.
    m_contextCommandBar->detach();
    m_contextMenu->clear();
    delete standardMenu;
}

void MarkdownEditor::setDocumentPath(const QString& path)
{
    m_documentPath = path;
    const bool hasSavedPath = !m_documentPath.isEmpty();
    if (m_contextCopyPath) m_contextCopyPath->setEnabled(hasSavedPath);
    if (m_contextReveal) m_contextReveal->setEnabled(hasSavedPath);
    if (m_contextTerminal) m_contextTerminal->setEnabled(hasSavedPath);
}

const QString& MarkdownEditor::documentPath() const
{
    return m_documentPath;
}

bool MarkdownEditor::wordWrapEnabled() const
{
    return lineWrapMode() != QPlainTextEdit::NoWrap;
}

// ---------------------------------------------------------------------------
int MarkdownEditor::lineNumberWidth() const
{
    int digits = qMax(4, QString::number(qMax(1, blockCount())).length());
    return 24 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void MarkdownEditor::repositionLineNumberArea()
{
    int w = lineNumberWidth();
    setViewportMargins(w + kLineNumberGutterGap, kEditorTopPadding, 0, 0);
    QRect cr = contentsRect();
    m_lineNumbers->setGeometry(QRect(cr.left(), cr.top(), w, cr.height()));
    emit lineNumberWidthChanged(w);
}

void MarkdownEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    repositionLineNumberArea();
    scheduleSpellCheck();
}

// The tab stop is a pixel distance, so it must be re-derived whenever the font
// changes (zoom, or a new Editor font arriving through the stylesheet);
// otherwise a tab stops being four columns wide in an editor that already exists.
void MarkdownEditor::changeEvent(QEvent* e)
{
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::FontChange)
        setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * 4);
}

// A large document is still being highlighted in time slices; whatever is about
// to be painted is highlighted first, so a region the pass has not reached yet
// never shows as plain text.
void MarkdownEditor::highlightVisibleBlocks()
{
    const QTextBlock first = firstVisibleBlock();
    const QPointF offset = contentOffset();
    const qreal bottom = viewport()->rect().bottom();
    QTextBlock last = first;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (blockBoundingGeometry(block).translated(offset).top() > bottom)
            break;
        last = block;
    }
    m_highlighter->ensureHighlighted(first, last);
}

void MarkdownEditor::paintEvent(QPaintEvent* e)
{
    static bool firstEditorPaint = false;
    if (!firstEditorPaint) {
        firstEditorPaint = true;
        FASTMD_MARK("editor first paint");
    }
    if (m_highlighter && !m_highlighter->isHighlightingComplete())
        highlightVisibleBlocks();
    if (m_markdownMode) {
        QPainter codePainter(viewport());
        codePainter.setClipRect(e->rect());
        const QColor codeBg = m_dark ? QColor(0x22, 0x29, 0x33) : QColor(0xe8, 0xed, 0xf2);
        const QColor codeBorder = m_dark ? QColor(0x30, 0x36, 0x3d) : QColor(0xd0, 0xd7, 0xde);
        QTextBlock codeBlock = firstVisibleBlock();
        const qreal codeViewportTop = e->rect().top();
        const qreal codeViewportBottom = e->rect().bottom();
        while (codeBlock.isValid()) {
            // Without this the loop ran from the first visible block to the end
            // of the document on every repaint, constructing a QString per
            // block. The backward walk below is bounded by the fence's own
            // length, so a fence starting above the viewport still paints.
            if (blockBoundingGeometry(codeBlock).translated(contentOffset()).top() > codeViewportBottom)
                break;

            const QString line = codeBlock.text();
            if (!line.startsWith(QStringLiteral("```")) && codeBlock.userState() != 1) {
                codeBlock = codeBlock.next();
                continue;
            }

            // Paint only the visible run. Walking back to the opening fence and
            // forward to the closing fence makes one very long fenced block as
            // expensive as the whole document even when only a screenful is
            // exposed.
            const QTextBlock start = codeBlock;
            QTextBlock end = start;
            QTextBlock lastPainted;
            bool continuesBelowViewport = false;
            while (end.isValid()) {
                const QRectF endGeometry = blockBoundingGeometry(end).translated(contentOffset());
                if (endGeometry.top() > codeViewportBottom) {
                    continuesBelowViewport = true;
                    break;
                }
                const QString endLine = end.text();
                if (end != start && !endLine.startsWith(QStringLiteral("```")) && end.userState() != 1)
                    break;
                lastPainted = end;
                if (endLine.startsWith(QStringLiteral("```")) && end != start) {
                    end = end.next();
                    break;
                }
                end = end.next();
            }

            const QPointF offset = contentOffset();
            const QRectF startGeom = blockBoundingGeometry(start).translated(offset);
            if (!lastPainted.isValid())
                break;
            const QRectF endGeom = blockBoundingGeometry(lastPainted).translated(offset);
            qreal paintTop = startGeom.top() - 2.0;
            const QTextBlock previous = start.previous();
            if (start.userState() == 1 && previous.isValid() && previous.userState() == 1)
                paintTop = qMin(paintTop, codeViewportTop - 4.0);
            qreal paintBottom = endGeom.bottom() + 2.0;
            if (continuesBelowViewport)
                paintBottom = qMax(paintBottom, codeViewportBottom + 4.0);
            const QRectF blockRect(0.0, paintTop,
                                   static_cast<qreal>(qMax(1, viewport()->width() - 4)),
                                   paintBottom - paintTop);
            codePainter.setBrush(codeBg);
            codePainter.setPen(QPen(codeBorder, 1.0));
            codePainter.drawRoundedRect(blockRect, 4.0, 4.0);
            codeBlock = end.isValid() ? end : QTextBlock();
        }
    }

    QPlainTextEdit::paintEvent(e);

    QPainter painter(viewport());
    painter.setClipRect(e->rect());

    const QColor lineCol  = m_dark ? QColor(0x55, 0x88, 0xaa) : QColor(0x88, 0xa8, 0xc0);
    const QColor bgCol    = m_dark ? DarkTheme::color(DarkTheme::Editor) : Qt::white;
    const QColor textCol  = m_dark ? QColor(0x66, 0x99, 0xbb) : QColor(0x77, 0x99, 0xaa);

    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();

    while (block.isValid()) {
        const QRectF geom = blockBoundingGeometry(block).translated(offset);
        if (geom.top() > e->rect().bottom()) break;

        if (isPageBreakBlock(block)) {
            const QRect r(0, (int)geom.top(), viewport()->width(), (int)geom.height());
            painter.fillRect(r, bgCol);

            const int y = r.center().y();
            painter.setPen(QPen(lineCol, 1, Qt::DashLine));
            painter.drawLine(r.left(), y, r.right(), y);

            const QString label = QStringLiteral("Page Break");
            QFont f = font();
            f.setItalic(true);
            painter.setFont(f);
            const QFontMetrics fm(f);
            const int lw = fm.horizontalAdvance(label) + 16;
            const int lx = (r.width() - lw) / 2;
            painter.fillRect(lx, r.top(), lw, r.height(), bgCol);
            painter.setPen(textCol);
            painter.drawText(QRect(lx, r.top(), lw, r.height()), Qt::AlignCenter, label);
        }
        block = block.next();
    }

    if (hasBlockSelection()) {
        const QColor selectionColor = m_dark
            ? QColor(0x4f, 0x8c, 0xc9, 120)
            : QColor(0x8d, 0xbf, 0xf0, 140);

        painter.setPen(Qt::NoPen);
        painter.setBrush(selectionColor);

        const QVector<BlockSelectionRange> ranges = blockSelectionRanges();
        for (const BlockSelectionRange& range : ranges) {
            const QRectF geom = blockBoundingGeometry(range.block).translated(offset);
            if (!geom.intersects(e->rect()))
                continue;

            QTextLayout* layout = range.block.layout();
            if (!layout)
                continue;

            const QString text = range.block.text();
            const int startPos = textPositionForVisualColumn(text, qMin(range.startColumn, range.endColumn));
            const int endPos = textPositionForVisualColumn(text, qMax(range.startColumn, range.endColumn));
            if (startPos >= endPos)
                continue;

            for (int lineIndex = 0; lineIndex < layout->lineCount(); ++lineIndex) {
                const QTextLine line = layout->lineAt(lineIndex);
                if (!line.isValid())
                    continue;

                const int lineStart = line.textStart();
                const int lineEnd = lineStart + line.textLength();
                const int segmentStart = qMax(startPos, lineStart);
                const int segmentEnd = qMin(endPos, lineEnd);
                if (segmentStart >= segmentEnd)
                    continue;

                const qreal startX = line.cursorToX(segmentStart);
                const qreal endX = line.cursorToX(segmentEnd);
                if (endX <= startX)
                    continue;

                const QRectF selectionRect(
                    geom.left() + startX,
                    geom.top() + line.y(),
                    endX - startX,
                    line.height());
                if (selectionRect.intersects(e->rect()))
                    painter.fillRect(selectionRect, selectionColor);
            }
        }

        // Qt's caret blink lives in its private text control; the paint
        // context is the public view of it -- no cursor position means the
        // primary caret is in its off phase or the editor is unfocused. The
        // column's carets follow that same state, so they blink with the
        // primary one instead of on a timer of their own.
        if (getPaintContext().cursorPosition != -1) {
            const QColor caretColor = palette().color(QPalette::Text);
            for (const QRectF& caret : blockSelectionCaretRects()) {
                if (caret.intersects(e->rect()))
                    painter.fillRect(caret, caretColor);
            }
        }
    }
}

// Qt paints only the real cursor, so every other visible line of the column
// gets a caret here. Past a line's end the caret sits in virtual space, one
// space width per column, which is where typing will pad the line out to.
// Bounded by the viewport, not by the number of lines in the column, since
// this runs on every caret blink.
QVector<QRectF> MarkdownEditor::blockSelectionCaretRects() const
{
    QVector<QRectF> carets;
    if (!m_blockSelectionActive)
        return carets;

    const int firstRow = qMin(m_blockAnchorBlock, m_blockCurrentBlock);
    const int lastRow = qMax(m_blockAnchorBlock, m_blockCurrentBlock);
    const QPointF offset = contentOffset();
    const qreal viewportBottom = viewport()->height();
    const qreal spaceWidth = fontMetrics().horizontalAdvance(QLatin1Char(' '));
    const qreal caretWidth = qMax(1, cursorWidth());
    const int column = m_blockCurrentColumn;

    QTextBlock block = firstVisibleBlock();
    for (int number = block.blockNumber(); block.isValid() && number <= lastRow;
         block = block.next(), ++number) {
        const QRectF geom = blockBoundingGeometry(block).translated(offset);
        if (geom.top() > viewportBottom)
            break;
        if (number < firstRow || !block.isVisible())
            continue;

        QTextLayout* layout = block.layout();
        if (!layout || layout->lineCount() <= 0)
            continue;

        const QString text = block.text();
        const int width = visualColumnWidth(text);
        if (number == m_blockCurrentBlock && column <= width)
            continue;

        const int position = textPositionForVisualColumn(text, column);
        QTextLine line = layout->lineForTextPosition(position);
        if (!line.isValid())
            line = layout->lineAt(layout->lineCount() - 1);
        qreal x = line.cursorToX(position);
        if (column > width)
            x += (column - width) * spaceWidth;

        carets.append(QRectF(geom.left() + x, geom.top() + line.y(), caretWidth, line.height()));
    }
    return carets;
}

void MarkdownEditor::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        int delta = e->angleDelta().y();
        if (delta > 0) {
            emit zoomRequested(1);
        } else if (delta < 0) {
            emit zoomRequested(-1);
        }
        e->accept();
        return;
    }

    if ((e->modifiers() & Qt::ShiftModifier) && !(e->modifiers() & Qt::AltModifier)) {
        QScrollBar* hbar = horizontalScrollBar();
        if (hbar && hbar->isVisible() && hbar->maximum() > hbar->minimum()) {
            const QPoint angleDelta = e->angleDelta();
            int delta = angleDelta.y();
            if (delta == 0)
                delta = angleDelta.x();

            if (delta != 0) {
                const int steps = delta / 120;
                const int singleStep = qMax(1, hbar->singleStep());
                const int pageStep = qMax(singleStep, hbar->pageStep() / 8);
                const int scrollAmount = (steps != 0 ? steps * pageStep
                                                     : (delta > 0 ? singleStep : -singleStep));
                hbar->setValue(hbar->value() - scrollAmount);
                e->accept();
                return;
            }
        }
    }

    QPlainTextEdit::wheelEvent(e);
}

bool MarkdownEditor::canInsertFromMimeData(const QMimeData* source) const
{
    if (source && source->hasImage())
        return true;

    if (source && source->hasText())
        return true;

    if (source && source->hasUrls()) {
        const QList<QUrl> urls = source->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile() && isImageFilePath(url.toLocalFile()))
                return true;
        }
    }

    return QPlainTextEdit::canInsertFromMimeData(source);
}

void MarkdownEditor::insertFromMimeData(const QMimeData* source)
{
    if (!source) return;

    bool isImage = source->hasImage();
    QStringList imagePaths;
    
    if (source->hasUrls()) {
        const QList<QUrl> urls = source->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile() && isImageFilePath(url.toLocalFile())) {
                imagePaths.append(url.toLocalFile());
            }
        }
    }

    if (isImage || (!imagePaths.isEmpty() && (!source->hasText() || source->text().trimmed().isEmpty() || source->text().startsWith("file://")))) {
        QString destDir;
        bool isTemp = m_documentPath.isEmpty();
        if (isTemp) {
            destDir = QDir::tempPath() + QStringLiteral("/FastMD_Images");
        } else {
            destDir = QFileInfo(m_documentPath).absolutePath() + QStringLiteral("/assets");
        }

        QDir dir(destDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            QMessageBox::warning(this, tr("Image Paste Failed"),
                tr("Cannot create the image assets folder:\n%1").arg(destDir));
            return;
        }

        QTextCursor c = textCursor();
        c.beginEditBlock();
        bool insertedImage = false;

        if (!imagePaths.isEmpty()) {
            for (int i = 0; i < imagePaths.size(); ++i) {
                QString srcPath = imagePaths.at(i);
                QFileInfo fi(srcPath);
                QString baseName = fi.completeBaseName();
                QString suffix = fi.suffix();
                QString uniqueName = imageAssetFileName(dir, baseName, suffix);
                QString destPath = dir.filePath(uniqueName);

                if (!QFile::copy(srcPath, destPath)) {
                    QMessageBox::warning(this, tr("Image Paste Failed"),
                        tr("Cannot copy the image to:\n%1").arg(destPath));
                    c.endEditBlock();
                    return;
                }
                
                if (isTemp) m_tempImages.append(destPath);

                QString outPath = isTemp ? destPath : QStringLiteral("assets/") + uniqueName;
                outPath = escapedMarkdownPath(outPath);

                c.insertText(QStringLiteral("![](") + outPath + QStringLiteral(")"));
                insertedImage = true;
                if (i + 1 < imagePaths.size())
                    c.insertText(QStringLiteral("\n"));
            }
        } else if (isImage) {
            QImage image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull()) {
                QString uniqueName = imageAssetFileName(dir, timestampImageBaseName(), QStringLiteral("png"));
                QString destPath = dir.filePath(uniqueName);
                if (!image.save(destPath, "PNG")) {
                    QMessageBox::warning(this, tr("Image Paste Failed"),
                        tr("Cannot save the clipboard image to:\n%1").arg(destPath));
                    c.endEditBlock();
                    return;
                }
                
                if (isTemp) m_tempImages.append(destPath);

                QString outPath = isTemp ? destPath : QStringLiteral("assets/") + uniqueName;
                outPath = escapedMarkdownPath(outPath);

                c.insertText(QStringLiteral("![](") + outPath + QStringLiteral(")"));
                insertedImage = true;
            }
        }

        c.endEditBlock();
        setTextCursor(c);
        if (insertedImage) {
            m_debounce->stop();
            emit contentReady();
        }
        return;
    }

    if (m_markdownMode && source->hasText()) {
        QString clipText = source->text().trimmed();
        QTextCursor c = textCursor();
        if (isValidHttpOrHttpsUrl(clipText) && !isSelectionOrCursorInsideLink(c)) {
            c.beginEditBlock();
            if (c.hasSelection()) {
                QString sel = c.selectedText();
                c.insertText(QStringLiteral("[") + sel + QStringLiteral("](") + clipText + QStringLiteral(")"));
            } else {
                int pos = c.position();
                c.insertText(QStringLiteral("[link text](") + clipText + QStringLiteral(")"));
                c.setPosition(pos + 1);
                c.setPosition(pos + 10, QTextCursor::KeepAnchor);
            }
            c.endEditBlock();
            setTextCursor(c);
            return;
        }
    }

    QPlainTextEdit::insertFromMimeData(source);
}

bool MarkdownEditor::commitTemporaryImages(const QString& newDocPath)
{
    if (m_tempImages.isEmpty() || newDocPath.isEmpty())
        return true;

    QString destDir = QFileInfo(newDocPath).absolutePath() + QStringLiteral("/assets");
    QDir dir(destDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        QMessageBox::warning(this, tr("Image Migration Failed"),
            tr("Cannot create the image assets folder:\n%1").arg(destDir));
        return false;
    }

    struct Move { QString source; QString destination; };
    QVector<Move> moved;

    for (const QString& tempPath : m_tempImages) {
        if (!QFile::exists(tempPath)) {
            for (const Move& prior : moved)
                moveImageFile(prior.destination, prior.source);
            QMessageBox::warning(this, tr("Image Migration Failed"),
                tr("The temporary image no longer exists:\n%1").arg(tempPath));
            return false;
        }

        QFileInfo fi(tempPath);
        const QString uniqueName = imageAssetFileName(dir, fi.completeBaseName(), fi.suffix());
        const QString destPath = dir.filePath(uniqueName);

        if (!moveImageFile(tempPath, destPath)) {
            for (const Move& prior : moved) {
                moveImageFile(prior.destination, prior.source);
            }
            QMessageBox::warning(this, tr("Image Migration Failed"),
                tr("Cannot move the temporary image to:\n%1").arg(destPath));
            return false;
        }
        moved.append({tempPath, destPath});
    }

    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (const Move& move : moved) {
        const QString oldLink = markdownImagePath(move.source);
        const QString newLink = escapedMarkdownPath(
            QDir(QFileInfo(newDocPath).absolutePath()).relativeFilePath(move.destination));
        QTextCursor searchCursor(document());
        while (!searchCursor.isNull() && !searchCursor.atEnd()) {
            searchCursor = document()->find(oldLink, searchCursor);
            if (!searchCursor.isNull())
                searchCursor.insertText(newLink);
        }
    }
    c.endEditBlock();
    m_tempImages.clear();
    return true;
}

void MarkdownEditor::zoomIn(int range)
{
    QFont f = font();
    int ps = f.pixelSize();
    if (ps > 0) {
        f.setPixelSize(ps + range);
        setFont(f);
    } else {
        QPlainTextEdit::zoomIn(range);
    }
    repositionLineNumberArea();
}

void MarkdownEditor::zoomOut(int range)
{
    QFont f = font();
    int ps = f.pixelSize();
    if (ps > 0) {
        int newSize = ps - range;
        if (newSize < FastMdDefaults::MinimumPreviewFontSize)
            newSize = FastMdDefaults::MinimumPreviewFontSize;
        f.setPixelSize(newSize);
        setFont(f);
    } else {
        QPlainTextEdit::zoomOut(range);
    }
    repositionLineNumberArea();
}

void MarkdownEditor::onBlockCountChanged(int)
{
    repositionLineNumberArea();
}

void MarkdownEditor::onUpdateRequest(const QRect& rect, int dy)
{
    if (dy)
        m_lineNumbers->scroll(0, dy);
    else
        m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());

    // Each blink of the primary caret repaints only that caret's rectangle;
    // widen it to the column's other carets so they toggle in the same paint.
    if (m_blockSelectionActive && !dy && rect.intersects(cursorRect())) {
        QRegion carets;
        for (const QRectF& caret : blockSelectionCaretRects())
            carets += caret.toAlignedRect();
        if (!carets.isEmpty())
            viewport()->update(carets);
    }

    if (rect.contains(viewport()->rect()))
        onBlockCountChanged(0);
}

void MarkdownEditor::onTextChanged()
{
    static bool firstInputHandled = false;
    if (!firstInputHandled) {
        firstInputHandled = true;
        FASTMD_MARK("first editor input handled");
    }
    if (m_markdownMode)
        ensureMarkdownHighlighter();
    scheduleSpellCheck();
    m_debounce->start();
}

// ---------------------------------------------------------------------------
void MarkdownEditor::paintLineNumbers(QPaintEvent* event)
{
    QPainter painter(m_lineNumbers);

    // The gutter is part of the editor surface, not a strip of its own: same
    // background, no rule, only the subdued numbers set it apart.
    QColor bg = palette().color(QPalette::Base);
    QColor fg = m_dark ? QColor(0x85, 0x85, 0x85) : QColor(0x90, 0x90, 0x90);
    QColor hl = m_dark ? QColor(0xb0, 0xb0, 0xb0) : QColor(0x44, 0x44, 0x44);

    painter.fillRect(event->rect(), bg);

    int curLine = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int blockNum     = block.blockNumber();
    int top    = qRound(blockBoundingGeometry(block).translated(contentOffset()).top()) + kEditorTopPadding;
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setFont(document()->defaultFont());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            bool current = (blockNum == curLine);
            painter.setPen(current ? hl : fg);
            QTextLine firstLine = block.layout()->lineAt(0);
            int baseline = top + qRound(firstLine.y() + firstLine.ascent());
            QString numStr = QString::number(blockNum + 1);
            int tw = painter.fontMetrics().horizontalAdvance(numStr);
            // Right-aligned but with 18px padding from the right edge
            painter.drawText(m_lineNumbers->width() - 24 - tw, baseline, numStr);
        }
        block = block.next();
        top   = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNum;
    }
}

// ---------------------------------------------------------------------------
void MarkdownEditor::copy()
{
    QString text;
    if (hasBlockSelection()) {
        text = blockSelectedText();
    } else {
        QTextCursor cur = textCursor();
        if (!cur.hasSelection())
            return;
        text = cur.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
    }

    QApplication::clipboard()->setText(text);
}

void MarkdownEditor::cut()
{
    if (!hasBlockSelection()) {
        QPlainTextEdit::cut();
        return;
    }

    const QVector<BlockSelectionRange> ranges = blockSelectionRanges();
    if (ranges.isEmpty())
        return;

    copy();
    if (qMin(m_blockAnchorColumn, m_blockCurrentColumn) == qMax(m_blockAnchorColumn, m_blockCurrentColumn))
        return;

    deleteBlockSelection();
}

void MarkdownEditor::paste()
{
    if (!hasBlockSelection()) {
        QPlainTextEdit::paste();
        return;
    }

    const QStringList lines = clipboardLines();
    insertIntoBlockSelection(lines, lines.size() <= 1);
}

void MarkdownEditor::keyPressEvent(QKeyEvent* event)
{
    static bool firstKeyEventHandled = false;
    if (!firstKeyEventHandled) {
        firstKeyEventHandled = true;
        FASTMD_MARK("first editor key event handled");
    }
    if (!m_snippetStops.isEmpty() &&
        (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
        !(event->modifiers() & ~Qt::ShiftModifier)) {
        const bool backward = event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
        if (backward && m_snippetStopIndex > 0) {
            setTextCursor(m_snippetStops[--m_snippetStopIndex]);
        } else if (!backward && m_snippetStopIndex + 1 < m_snippetStops.size()) {
            setTextCursor(m_snippetStops[++m_snippetStopIndex]);
        } else {
            QTextCursor c = textCursor();
            c.clearSelection();
            setTextCursor(c);
            clearSnippetStops();
        }
        event->accept();
        return;
    }

    if (!m_snippetStops.isEmpty() && event->key() == Qt::Key_Escape) {
        QTextCursor c = textCursor();
        c.clearSelection();
        setTextCursor(c);
        clearSnippetStops();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }

    if (event->matches(QKeySequence::Cut)) {
        cut();
        return;
    }

    if (event->matches(QKeySequence::Paste)) {
        paste();
        return;
    }

    if (handleBlockSelectionNavigation(event))
        return;

    if (handleBlockSelectionEdit(event))
        return;

    if (handlePageBreakDeletion(event))
        return;

    const bool plainEnter = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & Qt::ShiftModifier);
    if (plainEnter) {
        if (handleSmartList(event))
            return;

        QTextCursor cur = textCursor();
        QString line = cur.block().text();
        QString indent;
        for (QChar c : line) {
            if (c == ' ' || c == '\t') indent += c;
            else break;
        }
        cur.beginEditBlock();
        cur.insertBlock();
        if (!indent.isEmpty())
            cur.insertText(indent);
        cur.endEditBlock();
        setTextCursor(cur);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::ShiftModifier))) {
        unindentSelection();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Tab) {
        if (textCursor().hasSelection()) {
            indentSelection();
            event->accept();
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);
}

void MarkdownEditor::mousePressEvent(QMouseEvent* event)
{
    // WebView2 owns a native child HWND. Qt may still consider this editor
    // focused after the preview took native focus, so set native focus first;
    // otherwise QWidget::setFocus() can be a no-op.
    restoreKeyboardFocus();
    clearSnippetStops();

    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier)) {
        beginBlockSelection(event->position().toPoint());
        event->accept();
        return;
    }

    if (hasBlockSelection())
        clearBlockSelection();

    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::restoreKeyboardFocus()
{
    if (QWidget* host = window())
        ::SetFocus(reinterpret_cast<HWND>(host->winId()));
    setFocus(Qt::MouseFocusReason);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (m_blockSelectionDragging) {
        updateBlockSelection(event->position().toPoint());
        event->accept();
        return;
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void MarkdownEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_blockSelectionDragging && event->button() == Qt::LeftButton) {
        updateBlockSelection(event->position().toPoint());
        finishBlockSelection();
        event->accept();
        return;
    }

    QPlainTextEdit::mouseReleaseEvent(event);
}

// Pointers above or below the gutter resolve one line past the visible edge,
// so the autoscroll timer extends a drag by exactly one line per tick.
QTextBlock MarkdownEditor::gutterBlockAt(int y) const
{
    const int height = m_lineNumbers->height();
    const int probe = qBound(0, y, qMax(0, height - 1));

    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top() + kEditorTopPadding;
    QTextBlock hit = block;
    while (block.isValid()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible())
            hit = block;
        if (probe < bottom)
            break;
        block = block.next();
        top = bottom;
    }

    if (y < 0 && hit.previous().isValid())
        return hit.previous();
    if (y >= height && hit.next().isValid())
        return hit.next();
    return hit;
}

void MarkdownEditor::gutterMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    restoreKeyboardFocus();
    clearSnippetStops();
    if (hasBlockSelection())
        clearBlockSelection();

    const QTextBlock block = gutterBlockAt(event->position().toPoint().y());
    if (!block.isValid())
        return;

    setTextCursor(GutterSelection::clickCursor(textCursor(), block.blockNumber()));
    m_gutterDragging = true;
    m_gutterSelecting = false;
    m_gutterAnchorBlock = block.blockNumber();
    m_gutterDragY = event->position().toPoint().y();
    event->accept();
}

void MarkdownEditor::gutterMouseDoubleClick(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    const QTextBlock block = gutterBlockAt(event->position().toPoint().y());
    if (!block.isValid())
        return;

    m_gutterDragging = true;
    m_gutterSelecting = true;
    m_gutterAnchorBlock = block.blockNumber();
    m_gutterDragY = event->position().toPoint().y();
    setTextCursor(GutterSelection::lineRangeCursor(document(), m_gutterAnchorBlock, m_gutterAnchorBlock));
    event->accept();
}

void MarkdownEditor::gutterMouseMove(QMouseEvent* event)
{
    if (!m_gutterDragging || !(event->buttons() & Qt::LeftButton)) {
        event->ignore();
        return;
    }

    m_gutterDragY = event->position().toPoint().y();
    updateGutterDrag();

    const bool outside = m_gutterDragY < 0 || m_gutterDragY >= m_lineNumbers->height();
    if (outside) {
        if (!m_gutterAutoScroll) {
            m_gutterAutoScroll = new QTimer(this);
            m_gutterAutoScroll->setInterval(50);
            connect(m_gutterAutoScroll, &QTimer::timeout, this, [this] {
                if (!m_gutterDragging || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
                    m_gutterDragging = false;
                    m_gutterAutoScroll->stop();
                    return;
                }
                updateGutterDrag();
            });
        }
        if (!m_gutterAutoScroll->isActive())
            m_gutterAutoScroll->start();
    } else if (m_gutterAutoScroll) {
        m_gutterAutoScroll->stop();
    }
    event->accept();
}

void MarkdownEditor::gutterMouseRelease(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_gutterDragging) {
        event->ignore();
        return;
    }

    m_gutterDragging = false;
    if (m_gutterAutoScroll)
        m_gutterAutoScroll->stop();
    event->accept();
}

void MarkdownEditor::updateGutterDrag()
{
    const QTextBlock block = gutterBlockAt(m_gutterDragY);
    if (!block.isValid())
        return;
    if (!m_gutterSelecting && block.blockNumber() == m_gutterAnchorBlock)
        return;
    m_gutterSelecting = true;

    const QTextCursor range = GutterSelection::lineRangeCursor(document(), m_gutterAnchorBlock,
                                                               block.blockNumber());
    const QTextCursor current = textCursor();
    if (range.anchor() != current.anchor() || range.position() != current.position())
        setTextCursor(range);
}

// A Page Break is a raw HTML line that paintEvent() hides under its label.
// Character-wise Backspace/Delete beside it would only remove its line break,
// pulling the neighbouring text into the hidden marker line, or chip the
// marker out of sight. Remove the whole line as one undoable edit instead.
bool MarkdownEditor::handlePageBreakDeletion(QKeyEvent* event)
{
    const bool backward = event->key() == Qt::Key_Backspace;
    const bool forward = event->key() == Qt::Key_Delete;
    if ((!backward && !forward) || isReadOnly()
        || (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)))
        return false;

    QTextCursor cursor = textCursor();
    int start = 0;
    int end = 0;
    int caret = 0;
    if (cursor.hasSelection()) {
        start = cursor.selectionStart();
        end = cursor.selectionEnd();
        const QTextBlock first = document()->findBlock(start);
        const QTextBlock last = document()->findBlock(end);
        if (isPageBreakBlock(first) && start > first.position() && start < blockTextEnd(first))
            start = first.position();
        if (isPageBreakBlock(last) && end > last.position() && end < blockTextEnd(last))
            end = blockTextEnd(last);
        if (start == cursor.selectionStart() && end == cursor.selectionEnd())
            return false;
        caret = start;
    } else {
        const QTextBlock block = cursor.block();
        QTextBlock pageBreak;
        if (isPageBreakBlock(block))
            pageBreak = block;
        else if (backward && cursor.atBlockStart() && isPageBreakBlock(block.previous()))
            pageBreak = block.previous();
        else if (forward && cursor.atBlockEnd() && isPageBreakBlock(block.next()))
            pageBreak = block.next();
        if (!pageBreak.isValid())
            return false;

        start = pageBreak.position();
        end = blockTextEnd(pageBreak);
        if (pageBreak.next().isValid())
            ++end;
        else if (pageBreak.previous().isValid())
            --start;
        caret = qMin(cursor.position(), start);
    }

    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setPosition(caret);
    setTextCursor(cursor);
    event->accept();
    return true;
}

bool MarkdownEditor::handleSmartList(QKeyEvent* event)
{
    Q_UNUSED(event);

    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        cur.removeSelectedText();

    QTextBlock  block      = cur.block();
    QString     line       = block.text();
    const int   posInBlock = cur.positionInBlock();

    // optional leading blockquote markers (e.g. "> " or "> > "), then the list marker.
    // unordered: optional spaces, then - * or +, then space, then optional task checkbox
    static QRegularExpression rxUl(
        QStringLiteral("^((?:>\\s*)*)(\\s*)([-*+])( +)(\\[[ xX]\\]\\s+)?(.*)$"));
    // ordered: optional spaces, digits, '.' or ')', then space
    static QRegularExpression rxOl(
        QStringLiteral("^((?:>\\s*)*)(\\s*)(\\d+)([.)])( +)(.*)$"));

    QRegularExpressionMatch mu = rxUl.match(line);
    QRegularExpressionMatch mo = rxOl.match(line);

    if (mu.hasMatch()) {
        QString quote    = mu.captured(1);
        QString indent   = mu.captured(2);
        QString marker   = mu.captured(3);
        QString space    = mu.captured(4);
        QString checkbox = mu.captured(5);
        QString content  = mu.captured(6);

        if (content.trimmed().isEmpty()) {
            // empty item → exit list
            cur.select(QTextCursor::BlockUnderCursor);
            cur.removeSelectedText();
            cur.insertText(QStringLiteral("\n") + quote);
            setTextCursor(cur);
            return true;
        }

        QString newMarker = quote + indent + marker + space;
        if (!checkbox.isEmpty())
            newMarker += QStringLiteral("[ ] ");
        splitListLineAtCursor(cur, block, line, posInBlock, newMarker);
        return true;
    }

    if (mo.hasMatch()) {
        QString quote   = mo.captured(1);
        QString indent  = mo.captured(2);
        int     num     = mo.captured(3).toInt();
        QString dot     = mo.captured(4);
        QString space   = mo.captured(5);
        QString content = mo.captured(6);

        if (content.trimmed().isEmpty()) {
            cur.select(QTextCursor::BlockUnderCursor);
            cur.removeSelectedText();
            cur.insertText(QStringLiteral("\n") + quote);
            setTextCursor(cur);
            return true;
        }

        QString newMarker = quote + indent + QString::number(num + 1) + dot + space;
        splitListLineAtCursor(cur, block, line, posInBlock, newMarker);
        return true;
    }

    return false;
}

void MarkdownEditor::splitListLineAtCursor(QTextCursor cur, const QTextBlock& block,
                                            const QString& line, int posInBlock,
                                            const QString& newMarker)
{
    const QString afterText = line.mid(posInBlock);
    const int     splitPos  = block.position() + posInBlock;

    cur.setPosition(splitPos);
    cur.setPosition(block.position() + line.length(), QTextCursor::KeepAnchor);
    cur.removeSelectedText();

    cur.insertText(QStringLiteral("\n") + newMarker + afterText);
    cur.setPosition(splitPos + 1 + newMarker.length());
    setTextCursor(cur);
}

bool MarkdownEditor::hasBlockSelection() const
{
    return m_blockSelectionActive;
}

bool MarkdownEditor::handleBlockSelectionNavigation(QKeyEvent* event)
{
    if (!(event->modifiers().testFlag(Qt::AltModifier) && event->modifiers().testFlag(Qt::ShiftModifier)))
        return false;

    int blockDelta = 0;
    int columnDelta = 0;

    switch (event->key()) {
    case Qt::Key_Up:
        blockDelta = -1;
        break;
    case Qt::Key_Down:
        blockDelta = 1;
        break;
    case Qt::Key_Left:
        columnDelta = -1;
        break;
    case Qt::Key_Right:
        columnDelta = 1;
        break;
    default:
        return false;
    }

    if (!m_blockSelectionActive) {
        QTextCursor cursor = textCursor();
        m_blockAnchorBlock = cursor.blockNumber();
        m_blockCurrentBlock = m_blockAnchorBlock;
        m_blockAnchorColumn = visualColumnForTextPosition(cursor.block().text(), cursor.positionInBlock());
        m_blockCurrentColumn = m_blockAnchorColumn;
        activateBlockSelection();
    }

    m_blockCurrentBlock = clampedBlockNumber(document(), m_blockCurrentBlock + blockDelta);
    m_blockCurrentColumn = qMax(0, m_blockCurrentColumn + columnDelta);
    syncBlockSelectionCursor();
    viewport()->update();
    event->accept();
    return true;
}

bool MarkdownEditor::handleBlockSelectionEdit(QKeyEvent* event)
{
    if (!m_blockSelectionActive)
        return false;

    switch (event->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
        event->accept();
        return true;
    default:
        break;
    }

    if (event->key() == Qt::Key_Escape) {
        clearBlockSelection();
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Backspace) {
        if (qMin(m_blockAnchorColumn, m_blockCurrentColumn) == qMax(m_blockAnchorColumn, m_blockCurrentColumn))
            deleteBlockSelectionBackward();
        else
            deleteBlockSelection();
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Delete) {
        if (qMin(m_blockAnchorColumn, m_blockCurrentColumn) == qMax(m_blockAnchorColumn, m_blockCurrentColumn))
            deleteBlockSelectionForward();
        else
            deleteBlockSelection();
        event->accept();
        return true;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool plainTextInput = !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (plainTextInput) {
        if (event->key() == Qt::Key_Tab) {
            insertIntoBlockSelection(QStringLiteral("\t"));
            event->accept();
            return true;
        }

        const QString text = event->text();
        if (!text.isEmpty() && !text.at(0).isNull() && text.at(0).isPrint()) {
            insertIntoBlockSelection(text);
            event->accept();
            return true;
        }
    }

    clearBlockSelection();
    return false;
}

void MarkdownEditor::beginBlockSelection(const QPoint& pos)
{
    QTextCursor cursor = cursorForPosition(pos);
    m_blockAnchorBlock = cursor.blockNumber();
    m_blockCurrentBlock = m_blockAnchorBlock;
    m_blockAnchorColumn = blockColumnFromX(pos);
    m_blockCurrentColumn = m_blockAnchorColumn;
    activateBlockSelection();
    m_blockSelectionDragging = true;
    syncBlockSelectionCursor();
    viewport()->update();
}

// Undo, redo, find/replace or a reload can rewrite the lines under the column
// without going through it; the column then refers to text that is no longer
// there, so leave column mode. Formatting-only changes from the highlighter
// keep the length and the undo stack, and are ignored. Connected only while
// the column is active, so ordinary typing pays nothing for it.
void MarkdownEditor::activateBlockSelection()
{
    m_blockSelectionActive = true;
    m_blockSelectionUndoSteps = document()->availableUndoSteps();
    if (m_blockSelectionWatch)
        return;

    m_blockSelectionWatch = connect(document(), &QTextDocument::contentsChange, this,
        [this](int, int charsRemoved, int charsAdded) {
            if (m_blockSelectionEditing)
                return;
            if (charsRemoved != charsAdded
                || document()->availableUndoSteps() != m_blockSelectionUndoSteps)
                clearBlockSelection();
        });
}

void MarkdownEditor::updateBlockSelection(const QPoint& pos)
{
    QTextCursor cursor = cursorForPosition(pos);
    m_blockCurrentBlock = cursor.blockNumber();
    m_blockCurrentColumn = blockColumnFromX(pos);
    syncBlockSelectionCursor();
    viewport()->update();
}

void MarkdownEditor::finishBlockSelection()
{
    m_blockSelectionDragging = false;
    syncBlockSelectionCursor();
    viewport()->update();
}

void MarkdownEditor::clearBlockSelection()
{
    m_blockSelectionActive = false;
    m_blockSelectionDragging = false;
    disconnect(m_blockSelectionWatch);
    m_blockSelectionWatch = {};
    viewport()->update();
}

void MarkdownEditor::syncBlockSelectionCursor()
{
    QTextBlock block = document()->findBlockByNumber(clampedBlockNumber(document(), m_blockCurrentBlock));
    if (!block.isValid())
        return;

    QTextCursor cursor(document());
    cursor.setPosition(block.position() + textPositionForVisualColumn(block.text(), m_blockCurrentColumn));
    setTextCursor(cursor);
    ensureCursorVisible();
}

int MarkdownEditor::blockColumnFromX(const QPoint& pos) const
{
    QTextCursor cursor = cursorForPosition(pos);
    QTextBlock block = cursor.block();
    if (!block.isValid())
        return 0;

    int column = visualColumnForTextPosition(block.text(), cursor.positionInBlock());
    QTextLayout* layout = block.layout();
    if (!layout || layout->lineCount() <= 0)
        return column;

    QTextLine line = layout->lineForTextPosition(cursor.positionInBlock());
    if (!line.isValid())
        line = layout->lineAt(layout->lineCount() - 1);
    if (!line.isValid())
        return column;

    const QRectF blockGeom = blockBoundingGeometry(block).translated(contentOffset());
    const qreal relativeX = pos.x() - blockGeom.left();
    const int lineEndPos = line.textStart() + line.textLength();
    const qreal lineEndX = line.cursorToX(line.textLength());
    if (relativeX <= lineEndX)
        return column;

    const qreal spaceWidth = fontMetrics().horizontalAdvance(QLatin1Char(' '));
    if (spaceWidth <= 0.0)
        return column;

    const int extraColumns = qMax(0, qRound((relativeX - lineEndX) / spaceWidth));
    return visualColumnForTextPosition(block.text(), lineEndPos) + extraColumns;
}

int MarkdownEditor::tabColumnWidth(int column) const
{
    const qreal charWidth = fontMetrics().horizontalAdvance(QLatin1Char(' '));
    const int tabColumns = qMax(1, qRound(tabStopDistance() / qMax<qreal>(1.0, charWidth)));
    return tabColumns - (column % tabColumns);
}

int MarkdownEditor::visualColumnForTextPosition(const QString& text, int position) const
{
    int column = 0;
    const int limit = qBound(0, position, text.size());
    for (int i = 0; i < limit; ++i) {
        column += (text.at(i) == QLatin1Char('\t')) ? tabColumnWidth(column) : 1;
    }
    return column;
}

int MarkdownEditor::textPositionForVisualColumn(const QString& text, int column) const
{
    if (column <= 0)
        return 0;

    int currentColumn = 0;
    for (int i = 0; i < text.size(); ++i) {
        const int advance = (text.at(i) == QLatin1Char('\t')) ? tabColumnWidth(currentColumn) : 1;
        if (currentColumn + advance > column)
            return i + 1;
        currentColumn += advance;
        if (currentColumn >= column)
            return i + 1;
    }
    return text.size();
}

int MarkdownEditor::visualColumnWidth(const QString& text) const
{
    return visualColumnForTextPosition(text, text.size());
}

int MarkdownEditor::columnAfterText(int column, const QString& text) const
{
    for (const QChar ch : text)
        column += (ch == QLatin1Char('\t')) ? tabColumnWidth(column) : 1;
    return column;
}

QVector<MarkdownEditor::BlockSelectionRange> MarkdownEditor::blockSelectionRanges() const
{
    QVector<BlockSelectionRange> ranges;
    if (!m_blockSelectionActive)
        return ranges;

    const int startBlock = qMin(m_blockAnchorBlock, m_blockCurrentBlock);
    const int endBlock = qMax(m_blockAnchorBlock, m_blockCurrentBlock);
    const int startColumn = qMin(m_blockAnchorColumn, m_blockCurrentColumn);
    const int endColumn = qMax(m_blockAnchorColumn, m_blockCurrentColumn);

    ranges.reserve(endBlock - startBlock + 1);
    for (int blockNumber = startBlock; blockNumber <= endBlock; ++blockNumber) {
        QTextBlock block = document()->findBlockByNumber(blockNumber);
        if (!block.isValid())
            continue;

        BlockSelectionRange range;
        range.block = block;
        range.startColumn = startColumn;
        range.endColumn = endColumn;
        ranges.append(range);
    }

    return ranges;
}

QString MarkdownEditor::blockSelectedText() const
{
    QStringList lines;
    const QVector<BlockSelectionRange> ranges = blockSelectionRanges();
    lines.reserve(ranges.size());

    for (const BlockSelectionRange& range : ranges) {
        const QString text = range.block.text();
        if (visualColumnWidth(text) <= range.startColumn) {
            lines.append(QString());
            continue;
        }

        const int startPos = textPositionForVisualColumn(text, range.startColumn);
        const int endPos = textPositionForVisualColumn(text, range.endColumn);
        lines.append(text.mid(startPos, qMax(0, endPos - startPos)));
    }

    return lines.join(QLatin1Char('\n'));
}

void MarkdownEditor::deleteBlockSelection()
{
    insertIntoBlockSelection(QStringList{ QString() }, true);
}

// The column has zero width here. Each line that reaches the caret column
// reports where the character covering the neighbouring column starts (or
// ends): one column for ordinary text, a whole tab for a tab. The widest such
// span is removed from every line, so the column stays on real character
// boundaries and the text beside it stays aligned. A line that ends before
// the span -- the caret is in its virtual space -- loses nothing.
void MarkdownEditor::deleteBlockSelectionBackward()
{
    const int column = m_blockCurrentColumn;
    if (column <= 0)
        return;

    int target = column - 1;
    for (const BlockSelectionRange& range : blockSelectionRanges()) {
        const QString text = range.block.text();
        if (visualColumnWidth(text) < column)
            continue;
        const int position = textPositionForVisualColumn(text, column);
        if (position > 0)
            target = qMin(target, visualColumnForTextPosition(text, position - 1));
    }

    replaceBlockSelectionColumns(target, column, QStringList{ QString() }, true, target);
}

void MarkdownEditor::deleteBlockSelectionForward()
{
    const int column = m_blockCurrentColumn;
    int next = column + 1;
    for (const BlockSelectionRange& range : blockSelectionRanges()) {
        const QString text = range.block.text();
        if (visualColumnWidth(text) <= column)
            continue;
        const int position = textPositionForVisualColumn(text, column);
        if (position < text.size())
            next = qMax(next, visualColumnForTextPosition(text, position + 1));
    }

    replaceBlockSelectionColumns(column, next, QStringList{ QString() }, true, column);
}

void MarkdownEditor::insertIntoBlockSelection(const QStringList& lines, bool repeatSingleLine)
{
    const int startColumn = qMin(m_blockAnchorColumn, m_blockCurrentColumn);
    const int endColumn = qMax(m_blockAnchorColumn, m_blockCurrentColumn);
    const int rows = qAbs(m_blockCurrentBlock - m_blockAnchorBlock) + 1;
    const int currentRow = m_blockCurrentBlock - qMin(m_blockAnchorBlock, m_blockCurrentBlock);

    // Text whose lines differ in width leaves no single column every caret
    // could share, so the caret stays only on the active line.
    const int caretColumn = columnAfterText(startColumn, lines.value(repeatSingleLine ? 0 : currentRow));
    bool ragged = false;
    for (int i = 0; !repeatSingleLine && i < rows; ++i)
        ragged = ragged || columnAfterText(startColumn, lines.value(i)) != caretColumn;

    replaceBlockSelectionColumns(startColumn, endColumn, lines, repeatSingleLine, caretColumn);
    if (ragged)
        clearBlockSelection();
}

// Replaces visual columns [startColumn, endColumn) on every line of the column
// as one undo step, then keeps the column active as a zero-width caret at
// caretColumn. A short line is padded out to startColumn only when text goes
// into it, so a deletion never leaves trailing spaces behind.
void MarkdownEditor::replaceBlockSelectionColumns(int startColumn, int endColumn, const QStringList& lines,
                                                  bool repeatSingleLine, int caretColumn)
{
    const QVector<BlockSelectionRange> ranges = blockSelectionRanges();
    if (ranges.isEmpty())
        return;

    m_blockSelectionEditing = true;
    QTextCursor editCursor(document());
    editCursor.beginEditBlock();

    for (int i = ranges.size() - 1; i >= 0; --i) {
        const QTextBlock block = ranges.at(i).block;
        const QString text = block.text();
        const QString replacement = repeatSingleLine ? lines.value(0) : lines.value(i);
        const int width = visualColumnWidth(text);

        QTextCursor lineCursor(document());
        if (width < startColumn) {
            if (replacement.isEmpty())
                continue;
            lineCursor.setPosition(block.position() + text.size());
            lineCursor.insertText(QString(startColumn - width, QLatin1Char(' ')) + replacement);
            continue;
        }

        const int startPos = textPositionForVisualColumn(text, startColumn);
        const int endPos = qMax(startPos, textPositionForVisualColumn(text, endColumn));
        lineCursor.setPosition(block.position() + startPos);
        lineCursor.setPosition(block.position() + endPos, QTextCursor::KeepAnchor);
        if (lineCursor.hasSelection())
            lineCursor.removeSelectedText();
        if (!replacement.isEmpty())
            lineCursor.insertText(replacement);
    }

    editCursor.endEditBlock();
    m_blockSelectionEditing = false;

    m_blockAnchorColumn = caretColumn;
    m_blockCurrentColumn = caretColumn;
    m_blockSelectionUndoSteps = document()->availableUndoSteps();
    syncBlockSelectionCursor();
    viewport()->update();
}

void MarkdownEditor::insertIntoBlockSelection(const QString& text)
{
    insertIntoBlockSelection(QStringList{ text }, true);
}

QStringList MarkdownEditor::clipboardLines() const
{
    QString text = QApplication::clipboard()->text();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
void MarkdownEditor::fmtImage(const QString& docPath)
{
    QString initialDir = docPath.isEmpty() ? QString() : QFileInfo(docPath).absolutePath();
    QString imgPath = QFileDialog::getOpenFileName(
        this, tr("Select Image"), initialDir,
        tr("Images (*.png *.xpm *.jpg *.jpeg *.gif *.svg *.bmp *.webp)")
    );

    if (imgPath.isEmpty()) return;

    const QString outPath = markdownImagePath(imgPath);

    QTextCursor c = textCursor();
    QString sel = c.selectedText();
    if (sel.isEmpty()) {
        c.insertText(QStringLiteral("![alt text](") + outPath + QStringLiteral(")"));
    } else {
        c.insertText(QStringLiteral("![") + sel + QStringLiteral("](") + outPath + QStringLiteral(")"));
    }
    setTextCursor(c);
}

void MarkdownEditor::fmtInsertImageTemplate()
{
    QTextCursor c = textCursor();
    QString sel = c.selectedText();
    const QString placeholder = QStringLiteral("path/to/image.png");
    if (sel.isEmpty()) {
        int pos = c.position();
        c.insertText(QStringLiteral("![alt text](") + placeholder + QStringLiteral(")"));
        c.setPosition(pos + 12);
        c.setPosition(pos + 12 + placeholder.size(), QTextCursor::KeepAnchor);
    } else {
        int pos = c.selectionEnd();
        c.insertText(QStringLiteral("![") + sel + QStringLiteral("](") + placeholder + QStringLiteral(")"));
        const int pathStart = pos + 4;
        c.setPosition(pathStart);
        c.setPosition(pathStart + placeholder.size(), QTextCursor::KeepAnchor);
    }
    setTextCursor(c);
}

QString MarkdownEditor::markdownImagePath(const QString& imagePath) const
{
    QString finalPath = imagePath;
    if (!m_documentPath.isEmpty()) {
        QDir docDir(QFileInfo(m_documentPath).absolutePath());
        finalPath = docDir.relativeFilePath(imagePath);
    }

    return escapedMarkdownPath(finalPath);
}

void MarkdownEditor::insertDroppedImages(const QStringList& imagePaths)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (int i = 0; i < imagePaths.size(); ++i) {
        const QString outPath = markdownImagePath(imagePaths.at(i));
        c.insertText(QStringLiteral("![](") + outPath + QStringLiteral(")"));
        if (i + 1 < imagePaths.size())
            c.insertText(QStringLiteral("\n"));
    }
    c.endEditBlock();
    setTextCursor(c);
}

// ---------------------------------------------------------------------------
// Convert Case / Whitespace utilities
// ---------------------------------------------------------------------------
namespace {

// Applies `transform` to the current selection, or to the whole current line
// when nothing is selected. Used for Convert Case, which operates on exactly
// what the user selected rather than snapping to line boundaries.
template<typename Transform>
void applySelectionOrLineTransform(QPlainTextEdit* ed, Transform transform)
{
    QTextCursor c = ed->textCursor();
    if (!c.hasSelection()) {
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    const QString original = TextTransforms::blockSeparatorsToNewline(c.selectedText());
    const QString transformed = transform(original);
    if (transformed == original)
        return;
    c.beginEditBlock();
    c.insertText(transformed);
    c.endEditBlock();
    ed->setTextCursor(c);
}

// Applies `transform` to the whole lines touched by the current selection, or
// to the entire document when nothing is selected. Used for the line-oriented
// Whitespace commands.
template<typename Transform>
void applySelectedLinesOrDocumentTransform(QPlainTextEdit* ed, Transform transform)
{
    QTextCursor sel = ed->textCursor();
    QTextDocument* doc = ed->document();
    QTextCursor range(doc);

    if (sel.hasSelection()) {
        QTextBlock firstBlock = doc->findBlock(sel.selectionStart());
        QTextBlock lastBlock  = doc->findBlock(sel.selectionEnd());
        // A selection that ends exactly at the start of a block (e.g. a
        // whole-line selection made by dragging to the start of the next
        // line) shouldn't pull that next line into the operation.
        if (lastBlock != firstBlock && lastBlock.position() == sel.selectionEnd())
            lastBlock = lastBlock.previous();

        range.setPosition(firstBlock.position());
        QTextCursor endCursor(lastBlock);
        endCursor.movePosition(QTextCursor::EndOfBlock);
        range.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
    } else {
        range.movePosition(QTextCursor::Start);
        range.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    }

    const QString original = TextTransforms::blockSeparatorsToNewline(range.selectedText());
    const QString transformed = transform(original);
    if (transformed == original)
        return;
    range.beginEditBlock();
    range.insertText(transformed);
    range.endEditBlock();
    ed->setTextCursor(range);
}

} // namespace

void MarkdownEditor::convertCaseUpper()
{
    applySelectionOrLineTransform(this, [](const QString& s) { return s.toUpper(); });
}

void MarkdownEditor::convertCaseLower()
{
    applySelectionOrLineTransform(this, [](const QString& s) { return s.toLower(); });
}

void MarkdownEditor::convertCaseProper()
{
    applySelectionOrLineTransform(this, TextTransforms::transformProperCase);
}

void MarkdownEditor::convertCaseSentence()
{
    applySelectionOrLineTransform(this, TextTransforms::transformSentenceCase);
}

void MarkdownEditor::removeEmptyLines()
{
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        const QStringList lines = s.split(QLatin1Char('\n'));
        QStringList kept;
        kept.reserve(lines.size());
        for (const QString& line : lines) {
            if (!line.trimmed().isEmpty())
                kept.append(line);
        }
        return kept.join(QLatin1Char('\n'));
    });
}

void MarkdownEditor::trimLeadingSpaces()
{
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        QStringList lines = s.split(QLatin1Char('\n'));
        for (QString& line : lines)
            line = TextTransforms::stripLeadingSpacesTabs(line);
        return lines.join(QLatin1Char('\n'));
    });
}

void MarkdownEditor::trimTrailingSpaces()
{
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        QStringList lines = s.split(QLatin1Char('\n'));
        for (QString& line : lines)
            line = TextTransforms::stripTrailingSpacesTabs(line);
        return lines.join(QLatin1Char('\n'));
    });
}

void MarkdownEditor::trimBothSpaces()
{
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        QStringList lines = s.split(QLatin1Char('\n'));
        for (QString& line : lines)
            line = TextTransforms::stripTrailingSpacesTabs(TextTransforms::stripLeadingSpacesTabs(line));
        return lines.join(QLatin1Char('\n'));
    });
}

void MarkdownEditor::sortLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::sortLines);
}

void MarkdownEditor::reverseLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::reverseLines);
}

void MarkdownEditor::removeDuplicateLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::removeDuplicateLines);
}

void MarkdownEditor::uniqueSortLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::uniqueSortLines);
}

void MarkdownEditor::joinLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::joinLines);
}

void MarkdownEditor::splitLines()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Split Lines"));
    dialog.setMinimumWidth(320);
#ifdef Q_OS_WIN
    setWindowsTitleBarDarkMode(&dialog, dialog.palette().color(QPalette::Window).lightness() < 128);
#endif

    auto* form = new QFormLayout(&dialog);
    form->setContentsMargins(18, 16, 18, 14);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    auto* delimiterCombo = new CompactComboBox(&dialog);
    delimiterCombo->setFixedHeight(34);
    delimiterCombo->addItem(tr("Comma"), QStringLiteral(","));
    delimiterCombo->addItem(tr("Semicolon"), QStringLiteral(";"));
    delimiterCombo->addItem(tr("Tab"), QStringLiteral("\t"));
    delimiterCombo->addItem(tr("Custom…"));
    form->addRow(tr("Split on:"), delimiterCombo);

    auto* customDelimiter = new QLineEdit(&dialog);
    customDelimiter->setPlaceholderText(tr("Enter a delimiter"));
    customDelimiter->setClearButtonEnabled(true);
    customDelimiter->setVisible(false);
    auto* customLabel = new QLabel(tr("Custom:"), &dialog);
    customLabel->setVisible(false);
    form->addRow(customLabel, customDelimiter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Split"));
    form->addRow(buttons);

    const auto updateCustomField = [&]() {
        const bool custom = delimiterCombo->currentIndex() == delimiterCombo->count() - 1;
        customLabel->setVisible(custom);
        customDelimiter->setVisible(custom);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!custom || !customDelimiter->text().isEmpty());
        if (custom)
            customDelimiter->setFocus();
    };
    connect(delimiterCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&updateCustomField](int) { updateCustomField(); });
    connect(customDelimiter, &QLineEdit::textChanged, &dialog, [&updateCustomField](const QString&) {
        updateCustomField();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool custom = delimiterCombo->currentIndex() == delimiterCombo->count() - 1;
    const QString delimiter = custom ? customDelimiter->text() : delimiterCombo->currentData().toString();
    applySelectedLinesOrDocumentTransform(this, [delimiter](const QString& s) {
        return TextTransforms::splitLines(s, delimiter);
    });
}

void MarkdownEditor::normalizeWhitespace()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::normalizeWhitespace);
}

void MarkdownEditor::convertTabsToSpaces()
{
    // Matches the fixed tab width the editor itself displays with
    // (setTabStopDistance(... * 4) in the constructor below).
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        return TextTransforms::tabsToSpaces(s, 4);
    });
}

void MarkdownEditor::convertSpacesToTabs()
{
    applySelectedLinesOrDocumentTransform(this, [](const QString& s) {
        return TextTransforms::spacesToTabs(s, 4);
    });
}

void MarkdownEditor::convertSmartQuotesToStraight()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::smartQuotesToStraight);
}

void MarkdownEditor::removeInvisibleCharacters()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::removeInvisibleCharacters);
}

void MarkdownEditor::joinWrappedLines()
{
    applySelectedLinesOrDocumentTransform(this, TextTransforms::joinWrappedLinesText);
}

namespace {
template<typename Transform>
void applyMarkdownNumberTransform(MarkdownEditor* editor, Transform transform)
{
    if (!editor || !editor->isMarkdownMode())
        return;
    const QString original = editor->toPlainText();
    QTextCursor selected = editor->textCursor();
    QTextDocument* document = editor->document();
    int firstLine = -1;
    int lastLine = -1;
    QTextCursor range(document);
    if (selected.hasSelection()) {
        QTextBlock first = document->findBlock(selected.selectionStart());
        QTextBlock last = document->findBlock(selected.selectionEnd());
        if (last != first && last.position() == selected.selectionEnd())
            last = last.previous();
        firstLine = first.blockNumber();
        lastLine = last.blockNumber();
        range.setPosition(first.position());
        QTextCursor end(last);
        end.movePosition(QTextCursor::EndOfBlock);
        range.setPosition(end.position(), QTextCursor::KeepAnchor);
    } else {
        range.movePosition(QTextCursor::Start);
        range.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    }

    const QString transformed = transform(original, firstLine, lastLine);
    if (transformed == original)
        return;
    QString replacement = transformed;
    if (firstLine >= 0) {
        const QStringList changedLines = transformed.split(QLatin1Char('\n'));
        replacement = changedLines.mid(firstLine, lastLine - firstLine + 1)
            .join(QLatin1Char('\n'));
    }
    range.beginEditBlock();
    range.insertText(replacement);
    range.endEditBlock();
    editor->setTextCursor(range);
}
} // namespace

void MarkdownEditor::updateListNumbers()
{
    applyMarkdownNumberTransform(this, TextTransforms::updateListNumbers);
}

void MarkdownEditor::updateSectionNumbers()
{
    applyMarkdownNumberTransform(this, TextTransforms::updateSectionNumbers);
}

namespace {
void applyMarkdownFormat(MarkdownEditor* editor, bool selectionOnly)
{
    if (!editor || !editor->isMarkdownMode())
        return;
    const QString original = editor->toPlainText();
    QTextCursor old = editor->textCursor();
    int firstLine = -1, lastLine = -1;
    if (selectionOnly) {
        if (!old.hasSelection()) return;
        QTextDocument* doc = editor->document();
        QTextBlock first = doc->findBlock(old.selectionStart());
        QTextBlock last = doc->findBlock(old.selectionEnd());
        if (last != first && last.position() == old.selectionEnd()) last = last.previous();
        firstLine = first.blockNumber();
        lastLine = last.blockNumber();
    }
    const QString formatted = TextTransforms::formatMarkdown(original, firstLine, lastLine);
    if (formatted == original) return;

    const int oldAnchor = old.anchor();
    const int oldPosition = old.position();
    const int scroll = editor->verticalScrollBar()->value();
    QTextCursor edit(editor->document());
    edit.beginEditBlock();
    edit.movePosition(QTextCursor::Start);
    edit.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    edit.insertText(formatted);
    edit.endEditBlock();

    const auto mapPosition = [&](int position) {
        const QStringList before = original.split(QLatin1Char('\n'));
        const QStringList after = formatted.split(QLatin1Char('\n'));
        int line = original.left(position).count(QLatin1Char('\n'));
        const int beforeStart = original.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
        const int column = position - beforeStart;
        if (line >= after.size()) return formatted.size();
        const QString& a = before.at(qMin(line, before.size() - 1));
        const QString& b = after.at(line);
        int common = 0;
        while (common < column && common < a.size() && common < b.size() && a.at(common) == b.at(common)) ++common;
        int mappedColumn = column;
        if (column > common) {
            int suffix = 0;
            while (suffix < a.size() - common && suffix < b.size() - common
                   && a.at(a.size() - 1 - suffix) == b.at(b.size() - 1 - suffix)) ++suffix;
            if (column >= a.size() - suffix)
                mappedColumn = b.size() - (a.size() - column);
            else
                mappedColumn = common + (column - common) + (b.size() - a.size());
        }
        int start = 0;
        for (int i = 0; i < line; ++i) start += after.at(i).size() + 1;
        return qBound(start, start + mappedColumn, start + b.size());
    };
    QTextCursor restored(editor->document());
    restored.setPosition(mapPosition(oldAnchor));
    restored.setPosition(mapPosition(oldPosition), QTextCursor::KeepAnchor);
    editor->setTextCursor(restored);
    editor->verticalScrollBar()->setValue(scroll);
}
}

void MarkdownEditor::formatDocument() { applyMarkdownFormat(this, false); }
void MarkdownEditor::formatSelection() { applyMarkdownFormat(this, true); }

// Duplicates the current line (no selection) or every line touched by the
// selection (a block, inserted immediately below the original). Block
// numbers -- not character offsets -- locate the duplicate afterward, since
// the operation always inserts exactly as many whole lines as it read, so the
// duplicate is guaranteed to start right after the original block.
void MarkdownEditor::duplicateLine()
{
    QTextCursor sel = textCursor();
    QTextDocument* doc = document();
    const bool hadSelection = sel.hasSelection();

    QTextBlock firstBlock, lastBlock;
    int selStartOffset = 0, selEndOffset = 0, caretOffset = 0;

    if (hadSelection) {
        firstBlock = doc->findBlock(sel.selectionStart());
        lastBlock  = doc->findBlock(sel.selectionEnd());
        // A whole-line selection made by dragging to the start of the next
        // line shouldn't pull that next line into the duplicated block.
        bool rolledBack = false;
        if (lastBlock != firstBlock && lastBlock.position() == sel.selectionEnd()) {
            lastBlock = lastBlock.previous();
            rolledBack = true;
        }
        selStartOffset = sel.selectionStart() - firstBlock.position();
        selEndOffset = rolledBack ? qMax(0, lastBlock.length() - 1)
                                   : (sel.selectionEnd() - lastBlock.position());
    } else {
        firstBlock = lastBlock = sel.block();
        caretOffset = sel.positionInBlock();
    }

    const int lastBlockNumber = lastBlock.blockNumber();
    const int lineCount = lastBlockNumber - firstBlock.blockNumber() + 1;

    QTextCursor endOfLast(lastBlock);
    endOfLast.movePosition(QTextCursor::EndOfBlock);

    QTextCursor range(doc);
    range.setPosition(firstBlock.position());
    range.setPosition(endOfLast.position(), QTextCursor::KeepAnchor);
    const QString blockText = TextTransforms::blockSeparatorsToNewline(range.selectedText());

    QTextCursor editCursor(doc);
    editCursor.setPosition(endOfLast.position());
    editCursor.beginEditBlock();
    editCursor.insertBlock();
    editCursor.insertText(blockText);
    editCursor.endEditBlock();

    const QTextBlock dupFirst = doc->findBlockByNumber(lastBlockNumber + 1);
    const QTextBlock dupLast  = doc->findBlockByNumber(lastBlockNumber + lineCount);

    QTextCursor result(doc);
    if (hadSelection) {
        result.setPosition(dupFirst.position() + qMin(selStartOffset, qMax(0, dupFirst.length() - 1)));
        result.setPosition(dupLast.position() + qMin(selEndOffset, qMax(0, dupLast.length() - 1)),
                            QTextCursor::KeepAnchor);
    } else {
        result.setPosition(dupFirst.position() + qMin(caretOffset, qMax(0, dupFirst.length() - 1)));
    }
    setTextCursor(result);
}

// Deletes the current line (no selection) or every line touched by the
// selection. The caret lands on whatever now occupies the deleted block's
// position (the "next remaining line"), or on the previous line when the
// deleted block was the last one in the document.
void MarkdownEditor::deleteLine()
{
    QTextCursor sel = textCursor();
    QTextDocument* doc = document();
    const bool hadSelection = sel.hasSelection();

    QTextBlock firstBlock, lastBlock;
    if (hadSelection) {
        firstBlock = doc->findBlock(sel.selectionStart());
        lastBlock  = doc->findBlock(sel.selectionEnd());
        if (lastBlock != firstBlock && lastBlock.position() == sel.selectionEnd())
            lastBlock = lastBlock.previous();
    } else {
        firstBlock = lastBlock = sel.block();
    }

    // Preserve the caret's horizontal column so the landing line feels natural.
    const int column = hadSelection ? (sel.selectionStart() - firstBlock.position())
                                     : sel.positionInBlock();

    const QTextBlock nextBlock = lastBlock.next();
    QTextCursor range(doc);
    int caretTargetBlockNumber;

    if (nextBlock.isValid()) {
        // Remove the lines together with the newline that follows them, so
        // whatever came after slides up into the deleted block's position.
        range.setPosition(firstBlock.position());
        range.setPosition(nextBlock.position(), QTextCursor::KeepAnchor);
        caretTargetBlockNumber = firstBlock.blockNumber();
    } else if (firstBlock.previous().isValid()) {
        // The deleted block sits at the end of the document: remove the
        // newline that precedes it instead, so the previous line becomes the
        // new end of the document.
        const QTextBlock prevBlock = firstBlock.previous();
        QTextCursor endOfDoc(doc);
        endOfDoc.movePosition(QTextCursor::End);
        range.setPosition(prevBlock.position() + prevBlock.length() - 1);
        range.setPosition(endOfDoc.position(), QTextCursor::KeepAnchor);
        caretTargetBlockNumber = prevBlock.blockNumber();
    } else {
        // The whole document is exactly the deleted block(s): clear
        // everything, leaving the single empty paragraph Qt always keeps.
        QTextCursor endOfDoc(doc);
        endOfDoc.movePosition(QTextCursor::End);
        range.setPosition(0);
        range.setPosition(endOfDoc.position(), QTextCursor::KeepAnchor);
        caretTargetBlockNumber = 0;
    }

    range.beginEditBlock();
    range.removeSelectedText();
    range.endEditBlock();

    const QTextBlock target =
        doc->findBlockByNumber(qBound(0, caretTargetBlockNumber, doc->blockCount() - 1));
    QTextCursor result(target);
    result.setPosition(target.position() + qMin(column, qMax(0, target.length() - 1)));
    setTextCursor(result);
}

void MarkdownEditor::moveLinesUp()
{
    QTextCursor selection = textCursor();
    QTextDocument* doc = document();
    QTextBlock firstBlock = doc->findBlock(selection.selectionStart());
    QTextBlock lastBlock = doc->findBlock(selection.selectionEnd());
    if (selection.hasSelection() && lastBlock != firstBlock
        && lastBlock.position() == selection.selectionEnd()) {
        lastBlock = lastBlock.previous();
    }

    const QTextBlock previousBlock = firstBlock.previous();
    if (!previousBlock.isValid())
        return;

    const int firstBlockNumber = firstBlock.blockNumber();
    const int lineCount = lastBlock.blockNumber() - firstBlockNumber + 1;
    const int anchor = selection.anchor();
    const int position = selection.position();

    struct Endpoint {
        int relativeBlock;
        int column;
    };
    auto captureEndpoint = [&](int oldPosition) {
        const QTextBlock oldBlock = doc->findBlock(oldPosition);
        if (oldPosition == lastBlock.position() + lastBlock.text().size()) {
            return Endpoint {lineCount, 0};
        }
        return Endpoint {oldBlock.blockNumber() - firstBlockNumber,
                         oldPosition - oldBlock.position()};
    };

    const Endpoint anchorEndpoint = captureEndpoint(anchor);
    const Endpoint positionEndpoint = captureEndpoint(position);

    QTextCursor range(doc);
    range.setPosition(previousBlock.position());
    range.setPosition(lastBlock.position() + lastBlock.text().size(), QTextCursor::KeepAnchor);
    const QString affectedText = TextTransforms::blockSeparatorsToNewline(range.selectedText());
    const int previousLength = previousBlock.text().size();
    const QString reordered = affectedText.mid(previousLength + 1)
        + QLatin1Char('\n') + affectedText.left(previousLength);

    range.beginEditBlock();
    range.insertText(reordered);
    range.endEditBlock();

    auto mapEndpoint = [&](const Endpoint& endpoint) {
        const QTextBlock movedBlock = doc->findBlockByNumber(
            firstBlockNumber - 1 + qMin(endpoint.relativeBlock, lineCount - 1));
        return movedBlock.position() + (endpoint.relativeBlock == lineCount
            ? movedBlock.text().size() : endpoint.column);
    };
    QTextCursor result(doc);
    result.setPosition(mapEndpoint(anchorEndpoint));
    result.setPosition(mapEndpoint(positionEndpoint), QTextCursor::KeepAnchor);
    setTextCursor(result);
}

void MarkdownEditor::moveLinesDown()
{
    QTextCursor selection = textCursor();
    QTextDocument* doc = document();
    QTextBlock firstBlock = doc->findBlock(selection.selectionStart());
    QTextBlock lastBlock = doc->findBlock(selection.selectionEnd());
    if (selection.hasSelection() && lastBlock != firstBlock
        && lastBlock.position() == selection.selectionEnd()) {
        lastBlock = lastBlock.previous();
    }

    const QTextBlock nextBlock = lastBlock.next();
    if (!nextBlock.isValid())
        return;

    const int firstBlockNumber = firstBlock.blockNumber();
    const int lineCount = lastBlock.blockNumber() - firstBlockNumber + 1;
    const int anchor = selection.anchor();
    const int position = selection.position();

    struct Endpoint {
        int relativeBlock;
        int column;
    };
    auto captureEndpoint = [&](int oldPosition) {
        const QTextBlock oldBlock = doc->findBlock(oldPosition);
        if (oldPosition == lastBlock.position() + lastBlock.text().size()) {
            return Endpoint {lineCount, 0};
        }
        return Endpoint {oldBlock.blockNumber() - firstBlockNumber,
                         oldPosition - oldBlock.position()};
    };

    const Endpoint anchorEndpoint = captureEndpoint(anchor);
    const Endpoint positionEndpoint = captureEndpoint(position);

    QTextCursor range(doc);
    range.setPosition(firstBlock.position());
    range.setPosition(nextBlock.position() + nextBlock.text().size(), QTextCursor::KeepAnchor);
    const QString affectedText = TextTransforms::blockSeparatorsToNewline(range.selectedText());
    const int blockLength = nextBlock.position() - firstBlock.position() - 1;
    const QString reordered = affectedText.mid(blockLength + 1)
        + QLatin1Char('\n') + affectedText.left(blockLength);

    range.beginEditBlock();
    range.insertText(reordered);
    range.endEditBlock();

    auto mapEndpoint = [&](const Endpoint& endpoint) {
        const QTextBlock movedBlock = doc->findBlockByNumber(
            firstBlockNumber + 1 + qMin(endpoint.relativeBlock, lineCount - 1));
        return movedBlock.position() + (endpoint.relativeBlock == lineCount
            ? movedBlock.text().size() : endpoint.column);
    };
    QTextCursor result(doc);
    result.setPosition(mapEndpoint(anchorEndpoint));
    result.setPosition(mapEndpoint(positionEndpoint), QTextCursor::KeepAnchor);
    setTextCursor(result);
}

void MarkdownEditor::goToLine(int line, int column)
{
    int blockCount = document()->blockCount();
    if (line > blockCount) {
        line = blockCount;
    } else if (line < 1) {
        line = 1;
    }

    QTextBlock block = document()->findBlockByNumber(line - 1);
    QTextCursor cursor(block);
    if (column > 1)
        cursor.setPosition(block.position() + qMin(column - 1, block.length() - 1));
    setTextCursor(cursor);
    centerCursor();
    setFocus();

    flashLine(line);
}

void MarkdownEditor::goToSourceLine(int line)
{
    const int blockCount = document()->blockCount();
    line = qBound(1, line, blockCount);

    const QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;

    setTextCursor(QTextCursor(block));
    centerCursor();
    restoreKeyboardFocus();
}

// Preview double-click source highlighting: flashes the mapped line without
// touching caret, selection, or scroll position -- and only if it's already
// visible, per the "do not scroll or shift the Editor viewport" rule. Does
// take keyboard focus (unlike the caret/scroll state): double-clicking the
// Preview leaves real Windows keyboard focus on WebView2's native child HWND,
// so a plain Qt setFocus() only updates Qt's own bookkeeping -- Qt never
// learns the native focus moved, and keystrokes keep going to the browser
// instead of the Editor (see MainWindow::eventFilter's MouseButtonPress
// handler, which reclaims native focus the same way for an ordinary editor
// click; that path never fires here since there's no such click).
void MarkdownEditor::flashLineIfVisible(int line)
{
    // Reading Mode hides the whole Editor: there is nothing to flash, and
    // taking native focus back would strand it on an invisible window.
    if (!isVisible())
        return;

    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;

    const QRectF geom = blockBoundingGeometry(block).translated(contentOffset());
    if (!viewport()->rect().intersects(geom.toRect()))
        return;

    restoreKeyboardFocus();
    flashLine(line);
}

void MarkdownEditor::flashCurrentLine()
{
    const QTextBlock block = textCursor().block();
    if (!block.isValid())
        return;

    flashLine(block.blockNumber() + 1);
}

void MarkdownEditor::flashLine(int line)
{
    m_highlightLineNumber = line - 1;

    if (!m_highlightAnim) {
        m_highlightAnim = new QPropertyAnimation(this, "highlightAlpha", this);
        m_highlightAnim->setDuration(2000);
        m_highlightAnim->setStartValue(80);
        m_highlightAnim->setEndValue(0);
        m_highlightAnim->setEasingCurve(QEasingCurve::OutQuad);
    }
    m_highlightAnim->stop();
    m_highlightAnim->start();
}

void MarkdownEditor::setHighlightAlpha(int alpha)
{
    m_highlightAlpha = alpha;
    updateLineHighlight();
}

void MarkdownEditor::setSearchMatches(const QVector<QPair<int, int>>& ranges, int currentIndex)
{
    m_searchMatches = ranges;
    m_searchCurrentIndex = currentIndex;
    updateLineHighlight();
}

void MarkdownEditor::clearSearchMatches()
{
    if (m_searchMatches.isEmpty() && m_searchCurrentIndex < 0)
        return;
    m_searchMatches.clear();
    m_searchCurrentIndex = -1;
    updateLineHighlight();
}

// ---------------------------------------------------------------------------
// Spell checking.
//
// Live checking is deliberately viewport-scoped: its cost is bounded by the
// window height, not by the document length, which is what keeps a 28k-line
// file responsive. Results are painted as extra selections rather than
// character formats, so nothing here touches the document, the undo stack, or
// MarkdownHighlighter's own formatting.

void MarkdownEditor::showEvent(QShowEvent* event)
{
    QPlainTextEdit::showEvent(event);
    // Hidden tabs deliberately do no spell work. Visibility is the common
    // boundary for tab activation and leaving Reading Mode, so refresh here
    // rather than teaching every UI path that can reveal an editor about the
    // deferred scan.
    if (m_spellCheckActive)
        scheduleSpellCheck();
}

void MarkdownEditor::setSpellCheckActive(bool active)
{
    if (m_spellCheckActive == active)
        return;
    m_spellCheckActive = active;

    if (active) {
        if (!m_spellDebounce) {
            m_spellDebounce = new QTimer(this);
            m_spellDebounce->setSingleShot(true);
            m_spellDebounce->setInterval(150);
            connect(m_spellDebounce, &QTimer::timeout,
                    this, &MarkdownEditor::recheckVisibleSpelling);
        }
        m_spellConnections.append(connect(verticalScrollBar(), &QScrollBar::valueChanged,
                                          this, &MarkdownEditor::scheduleSpellCheck));
        m_spellConnections.append(connect(&SpellChecker::instance(), &SpellChecker::dictionaryChanged,
                                          this, &MarkdownEditor::invalidateSpellCheck));
        if (isVisible())
            scheduleSpellCheck();
        return;
    }

    for (const QMetaObject::Connection& connection : std::as_const(m_spellConnections))
        disconnect(connection);
    m_spellConnections.clear();
    if (m_spellDebounce)
        m_spellDebounce->stop();
    if (!m_spellRanges.isEmpty()) {
        m_spellRanges.clear();
        updateLineHighlight();
    }
}

void MarkdownEditor::scheduleSpellCheck()
{
    if (!m_spellCheckActive || !m_spellDebounce)
        return;
    m_spellDebounce->start();
}

void MarkdownEditor::refreshSpellCheck()
{
    if (!m_spellCheckActive)
        return;
    recheckVisibleSpelling();
}

void MarkdownEditor::invalidateSpellCheck()
{
    if (!m_spellCheckActive)
        return;
    if (!m_spellRanges.isEmpty()) {
        m_spellRanges.clear();
        updateLineHighlight();
    }
    if (isVisible())
        scheduleSpellCheck();
}

void MarkdownEditor::recheckVisibleSpelling()
{
    if (!m_spellCheckActive)
        return;

    // Blocks checked beyond the viewport so a short scroll or a one-line edit
    // does not expose an unchecked strip before the debounce fires again.
    constexpr int kViewportMargin = 20;
    // Enough to recover front matter and $$ display-math state, which
    // MarkdownHighlighter does not publish per block, without ever scanning the
    // whole document.
    constexpr int kContextScan = 200;

    QVector<QPair<int, int>> ranges;
    SpellChecker& checker = SpellChecker::instance();

    const QTextBlock firstVisible = firstVisibleBlock();
    if (checker.isReady() && firstVisible.isValid()) {
        const int firstNumber = qMax(0, firstVisible.blockNumber() - kViewportMargin);
        const int lastNumber =
            qMin(document()->blockCount() - 1,
                 cursorForPosition(QPoint(0, viewport()->height())).blockNumber() + kViewportMargin);
        const int contextNumber = qMax(0, firstNumber - kContextScan);

        SpellCheckExclusions::LineState state;
        QTextBlock block = document()->findBlockByNumber(contextNumber);
        for (int number = contextNumber; block.isValid() && number <= lastNumber;
             block = block.next(), ++number) {
            const QString text = block.text();

            if (m_markdownMode) {
                // MarkdownHighlighter mirrors "this block is inside a fenced
                // code block" into the block's user state, so the multi-line
                // part of the exclusion mask reuses the parse the editor
                // already pays for. -1 means it has not run on this block yet.
                const int userState = block.userState();
                if (userState >= 0)
                    state.inFence = (userState == 1);
            }

            if (number >= firstNumber) {
                const QVector<SpellCheckExclusions::Span> prose =
                    m_markdownMode ? SpellCheckExclusions::proseSpans(text, state)
                                   : SpellCheckExclusions::plainTextSpans(text);
                const int blockPosition = block.position();
                for (const SpellCheckExclusions::Span& span : prose) {
                    const QString fragment = text.mid(span.start, span.length);
                    for (const SpellCheckExclusions::Span& error : checker.check(fragment))
                        ranges.append({ blockPosition + span.start + error.start, error.length });
                }
            }

            state = SpellCheckExclusions::advance(text, state, number);
        }
    }

    if (ranges == m_spellRanges)
        return;
    m_spellRanges = ranges;
    updateLineHighlight();
}

void MarkdownEditor::appendSpellSelections(QList<QTextEdit::ExtraSelection>& selections) const
{
    if (m_spellRanges.isEmpty())
        return;
    const QColor color = m_dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xe8, 0x11, 0x23);
    // The cached ranges are up to one debounce interval old, so an edit that
    // shortened the document can leave them pointing past its end until the
    // next recheck. Skip those rather than letting QTextCursor warn per repaint.
    const int end = document()->characterCount() - 1;
    for (const QPair<int, int>& range : m_spellRanges) {
        if (range.first < 0 || range.first + range.second > end)
            continue;
        QTextCursor cursor(document());
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        selection.format.setUnderlineColor(color);
        selections.append(selection);
    }
}

QPair<int, int> MarkdownEditor::spellRangeAt(int position) const
{
    for (const QPair<int, int>& range : m_spellRanges) {
        if (position >= range.first && position < range.first + range.second)
            return range;
    }
    return { -1, 0 };
}

void MarkdownEditor::replaceRange(int start, int length, const QString& replacement)
{
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
}

void MarkdownEditor::updateLineHighlight()
{
    QList<QTextEdit::ExtraSelection> selections;

    appendSpellSelections(selections);

    // Highlight-all layer, painted first so the current-line highlight and
    // the jump flash below still read on top when they coincide with a match.
    if (!m_searchMatches.isEmpty()) {
        const QColor otherColor = m_dark ? QColor(0x62, 0x33, 0x15, 180) : QColor(0xff, 0xe9, 0xa8, 220);
        const QColor currentColor = m_dark ? QColor(0x8a, 0x4a, 0x1e, 220) : QColor(0xff, 0xcc, 0x4d, 230);
        // Painting is capped to bound the cost on pathological documents;
        // navigation and the match counter stay exact beyond it, and the
        // current match is always painted even when it falls past the cap.
        const int cap = qMin(m_searchMatches.size(), 2000);
        const auto paintMatch = [&](int i) {
            const auto& range = m_searchMatches[i];
            QTextCursor cursor(document());
            cursor.setPosition(range.first);
            cursor.setPosition(range.second, QTextCursor::KeepAnchor);
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format.setBackground(i == m_searchCurrentIndex ? currentColor : otherColor);
            selections.append(sel);
        };
        for (int i = 0; i < cap; ++i)
            paintMatch(i);
        if (m_searchCurrentIndex >= cap && m_searchCurrentIndex < m_searchMatches.size())
            paintMatch(m_searchCurrentIndex);
    }

    if (m_currentLineHighlightEnabled) {
        const int alpha = m_dark
            ? (hasFocus() ? kCurrentLineAlphaFocusedDark : kCurrentLineAlphaUnfocusedDark)
            : (hasFocus() ? kCurrentLineAlphaFocusedLight : kCurrentLineAlphaUnfocusedLight);
        QTextEdit::ExtraSelection sel;
        QColor color = m_dark ? QColor(255, 255, 255, alpha)
                              : QColor(0, 0, 0, alpha);
        sel.format.setBackground(color);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        selections.append(sel);
    }

    // Transient "jump to line" flash, drawn on top of the persistent
    // current-line highlight so it remains visible when they coincide.
    if (m_highlightAlpha > 0 && m_highlightLineNumber >= 0) {
        QTextEdit::ExtraSelection sel;
        QColor color = m_dark ? QColor(255, 255, 255, m_highlightAlpha)
                              : QColor(0, 0, 0, m_highlightAlpha);
        sel.format.setBackground(color);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);

        QTextBlock block = document()->findBlockByNumber(m_highlightLineNumber);
        if (block.isValid()) {
            sel.cursor = QTextCursor(block);
            sel.cursor.clearSelection();
            selections.append(sel);
        }
    }

    setExtraSelections(selections);
}

void MarkdownEditor::indentSelection()
{
    QTextCursor cur = textCursor();
    cur.beginEditBlock();

    const bool hasSel = cur.hasSelection();
    const int startPos = cur.selectionStart();
    const int endPos = cur.selectionEnd();

    QTextBlock startBlock = document()->findBlock(startPos);
    QTextBlock endBlock = document()->findBlock(hasSel ? qMax(startPos, endPos - 1) : startPos);

    int blockCount = 0;
    int firstBlockAdded = 0;

    QTextBlock block = startBlock;
    while (block.isValid()) {
        QTextCursor blockCur(block);
        blockCur.movePosition(QTextCursor::StartOfBlock);
        blockCur.insertText(QStringLiteral("    "));

        if (blockCount == 0)
            firstBlockAdded = 4;

        blockCount++;
        if (block == endBlock)
            break;
        block = block.next();
    }

    cur.endEditBlock();

    if (hasSel) {
        QTextCursor newCur(document());
        newCur.setPosition(startPos + firstBlockAdded);
        newCur.setPosition(endPos + (blockCount * 4), QTextCursor::KeepAnchor);
        setTextCursor(newCur);
    }
}

void MarkdownEditor::unindentSelection()
{
    QTextCursor cur = textCursor();
    cur.beginEditBlock();

    const bool hasSel = cur.hasSelection();
    const int startPos = cur.selectionStart();
    const int endPos = cur.selectionEnd();

    QTextBlock startBlock = document()->findBlock(startPos);
    QTextBlock endBlock = document()->findBlock(hasSel ? qMax(startPos, endPos - 1) : startPos);

    int firstBlockRemoved = 0;
    int totalRemoved = 0;
    int blockCount = 0;

    QTextBlock block = startBlock;
    while (block.isValid()) {
        const QString text = block.text();
        int toRemove = 0;
        if (text.startsWith(QLatin1Char('\t'))) {
            toRemove = 1;
        } else {
            while (toRemove < 4 && toRemove < text.length() && text.at(toRemove) == QLatin1Char(' ')) {
                toRemove++;
            }
        }

        if (toRemove > 0) {
            QTextCursor blockCur(block);
            blockCur.movePosition(QTextCursor::StartOfBlock);
            blockCur.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, toRemove);
            blockCur.removeSelectedText();

            if (blockCount == 0)
                firstBlockRemoved = toRemove;
            totalRemoved += toRemove;
        }

        blockCount++;
        if (block == endBlock)
            break;
        block = block.next();
    }

    cur.endEditBlock();

    if (hasSel) {
        QTextCursor newCur(document());
        int newStart = qMax(startBlock.position(), startPos - firstBlockRemoved);
        int newEnd = qMax(startBlock.position(), endPos - totalRemoved);
        newCur.setPosition(newStart);
        newCur.setPosition(newEnd, QTextCursor::KeepAnchor);
        setTextCursor(newCur);
    }
}
