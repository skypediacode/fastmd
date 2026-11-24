#pragma once
#include <QFont>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QVector>
#include <QPair>
#include <QTextEdit>
#include <KSyntaxHighlighting/Definition>

class LineNumberArea;
class MarkdownHighlighter;
class SourceHighlighter;
class QTimer;
class QAction;
class QContextMenuEvent;
class AlignedMenu;
class MenuCommandBar;
class QPropertyAnimation;
class QFocusEvent;
class QShowEvent;
class QMouseEvent;

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
    friend class LineNumberArea;
    Q_PROPERTY(int highlightAlpha READ highlightAlpha WRITE setHighlightAlpha)
public:
    // fontPixelSize is taken at construction so the monospace font is composed
    // and applied exactly once; setting it afterwards costs a second full
    // QTextDocument relayout.
    explicit MarkdownEditor(QWidget* parent = nullptr, int fontPixelSize = 0);

    // The editor's monospace font, composed in one place so the startup
    // warm-up in main() can realize the exact same request the first editor
    // will make. Building a font engine for a multi-family request is what
    // pulls in Qt's per-family fallback tables -- see main().
    static QFont monospaceFont(int fontPixelSize = 0);

    void setDarkMode(bool dark);
    void setMarkdownMode(bool markdown);
    // Central-controller entry point: applies an explicitly resolved
    // definition instead of re-deriving one from the document path. Reuses
    // the existing SourceHighlighter across non-Markdown language changes;
    // only swaps highlighter implementation when crossing the Markdown
    // boundary. Never touches content, cursor, selection, or scroll state.
    void setSyntaxDefinition(const KSyntaxHighlighting::Definition& definition, bool markdown);
    void setWordWrapEnabled(bool enabled);
    void setContextMenuActions(QAction* wordWrap, QAction* preview, QAction* goToPreview = nullptr);
    void setDocumentPath(const QString& path);
    const QString& documentPath() const;
    void setPlainText(const QString& text);
    bool wordWrapEnabled() const;
    bool isMarkdownMode() const { return m_markdownMode; }

    // Called by LineNumberArea's paintEvent
    void paintLineNumbers(QPaintEvent* event);
    int  lineNumberWidth() const;
    
    int highlightAlpha() const { return m_highlightAlpha; }
    void setHighlightAlpha(int alpha);

    void setCurrentLineHighlightEnabled(bool enabled);
    bool currentLineHighlightEnabled() const { return m_currentLineHighlightEnabled; }

    // Spell checking is entirely inert while this is false: no timer is
    // created, no signal is connected, and no ranges are held.
    void setSpellCheckActive(bool active);
    bool spellCheckActive() const { return m_spellCheckActive; }
    // Drops cached results and re-checks the viewport immediately. Used after
    // Fix All rewrites the document.
    void refreshSpellCheck();
    // Drops ranges immediately and schedules the normal debounced viewport
    // pass. Used for language and tab changes so those UI actions never wait
    // synchronously on the platform checker.
    void invalidateSpellCheck();

    bool commitTemporaryImages(const QString& newDocPath);
    void restoreKeyboardFocus();

    // Owned by FindController: paints the highlight-all layer under the
    // current-line highlight and jump flash in updateLineHighlight().
    void setSearchMatches(const QVector<QPair<int, int>>& ranges, int currentIndex);
    void clearSearchMatches();

public slots:
    void cut();
    void copy();
    void paste();
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);
    void fmtBold();
    void fmtItalic();
    void fmtStrike();
    void fmtHighlight();
    void fmtSubscript();
    void fmtSuperscript();
    void fmtAlignLeft();
    void fmtAlignCenter();
    void fmtAlignRight();
    void fmtAlignJustify();
    void fmtCode();
    void fmtLink();
    // Inserts a numbered in-text reference at the cursor and appends its
    // definition to the document as one undoable edit.
    void insertNumberedReferenceLink(const QString& referenceLabel, const QString& url,
                                     const QString& title = QString());
    QString nextNumericReferenceLabel() const;
    void fmtImage(const QString& docPath = QString());
    void fmtInsertImageTemplate();
    void fmtFootnote();
    void fmtNamedFootnote(const QString& name);
    void fmtHorizontalRule();
    void fmtH1();
    void fmtH2();
    void fmtH3();
    void fmtH4();
    void fmtH5();
    void fmtH6();
    void fmtQuote();
    void fmtBulletList();
    void fmtNumberList();
    void fmtChecklist();
    // Inserts a GitHub-style callout. A selected set of lines becomes the
    // callout body; without a selection, the content placeholder is selected.
    void insertCallout(const QString& type);
    void fmtTable(int rows = 3, int cols = 3);
    void fmtCodeBlock();
    // A fenced code block tagged `mermaid`, which the preview shell renders as
    // a diagram. Shares fmtCodeBlock()'s insertion, only the info string differs.
    void fmtMermaidBlock();
    void fmtMathInline();
    void fmtMathBlock();
    // Inserts a Math toolbar LaTeX template. Shares its math-context
    // detection (MathContext) and inline/display wrapping decision with
    // insertChemistrySnippet() below.
    void insertMathSnippet(const QString& templateLatex);
    // Like insertMathSnippet() (same MathContext-based wrapping), but if the
    // cursor has a selection and the template contains a "«»"-wrapped
    // placeholder, the selected text replaces that placeholder's default
    // content instead of being discarded (e.g. selecting "NaCl" and choosing
    // \ce{«H2O»} inserts \ce{NaCl}). Used by the Chemistry toolbar.
    void insertChemistrySnippet(const QString& templateLatex);
    void insertMatrix(int rows, int cols, const QString& env);
    // The exact `\begin{env}...\end{env}` LaTeX insertMatrix() inserts, cell
    // placeholders included (the "«»"-wrapped tab-stop markers consumed by
    // insertMathSnippet()). Shared with MatrixInsertDialog's live preview so
    // the two can never diverge -- the preview strips the placeholder markers
    // rather than rebuilding the body independently.
    static QString matrixLatexBody(int rows, int cols, const QString& env);
    void fmtPageBreak();
    void insertTableOfContents();
    void insertDynamicTableOfContents();
    // `column` is 1-based and clamped to the line; 0 lands at its start.
    void goToLine(int line, int column = 0);
    void goToSourceLine(int line);
    void flashLineIfVisible(int line);
    void flashCurrentLine();

    void convertCaseUpper();
    void convertCaseLower();
    void convertCaseProper();
    void convertCaseSentence();

    void removeEmptyLines();
    void trimLeadingSpaces();
    void trimTrailingSpaces();
    void trimBothSpaces();
    void joinWrappedLines();
    void sortLines();
    void reverseLines();
    void removeDuplicateLines();
    void uniqueSortLines();
    void joinLines();
    void splitLines();
    void normalizeWhitespace();
    void convertTabsToSpaces();
    void convertSpacesToTabs();
    void convertSmartQuotesToStraight();
    void removeInvisibleCharacters();
    void updateListNumbers();
    void updateSectionNumbers();
    void formatDocument();
    void formatSelection();

    void duplicateLine();
    void deleteLine();
    void moveLinesUp();
    void moveLinesDown();
    void indentSelection();
    void unindentSelection();

signals:
    void contentReady();   // emitted after 200 ms debounce
    void zoomRequested(int delta); // delta is +1 or -1
    void lineNumberWidthChanged(int width);
    // MainWindow resolves the document path to its containing folder.
    void terminalRequested(const QString& filePath);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private slots:
    void scheduleSpellCheck();
    void recheckVisibleSpelling();
    void onBlockCountChanged(int);
    void onUpdateRequest(const QRect& rect, int dy);
    void onTextChanged();

private:
    struct BlockSelectionRange {
        QTextBlock block;
        int startColumn = 0;
        int endColumn = 0;
    };

    void    repaintAfterHighlightingChange();
    void    repositionLineNumberArea();
    void    ensureMarkdownHighlighter();
    void    highlightVisibleBlocks();
    bool    handlePageBreakDeletion(QKeyEvent* event);
    bool    handleSmartList(QKeyEvent* event);
    void    splitListLineAtCursor(QTextCursor cur, const QTextBlock& block,
                                   const QString& line, int posInBlock,
                                   const QString& newMarker);
    QString markdownImagePath(const QString& imagePath) const;
    void    insertDroppedImages(const QStringList& imagePaths);
    bool    hasBlockSelection() const;
    bool    handleBlockSelectionNavigation(QKeyEvent* event);
    bool    handleBlockSelectionEdit(QKeyEvent* event);
    void    activateBlockSelection();
    void    beginBlockSelection(const QPoint& pos);
    void    updateBlockSelection(const QPoint& pos);
    void    finishBlockSelection();
    void    clearBlockSelection();
    void    syncBlockSelectionCursor();
    int     blockColumnFromX(const QPoint& pos) const;
    int     tabColumnWidth(int column) const;
    int     visualColumnForTextPosition(const QString& text, int position) const;
    int     textPositionForVisualColumn(const QString& text, int column) const;
    int     visualColumnWidth(const QString& text) const;
    int     columnAfterText(int column, const QString& text) const;
    QVector<BlockSelectionRange> blockSelectionRanges() const;
    QString blockSelectedText() const;
    void    deleteBlockSelection();
    void    deleteBlockSelectionBackward();
    void    deleteBlockSelectionForward();
    void    insertIntoBlockSelection(const QStringList& lines, bool repeatSingleLine);
    void    insertIntoBlockSelection(const QString& text);
    void    replaceBlockSelectionColumns(int startColumn, int endColumn, const QStringList& lines,
                                         bool repeatSingleLine, int caretColumn);
    QVector<QRectF> blockSelectionCaretRects() const;
    QStringList clipboardLines() const;

    // The gutter is its own interaction surface: these act on logical lines
    // through QTextCursor and never forward events into the text area.
    void    gutterMousePress(QMouseEvent* event);
    void    gutterMouseDoubleClick(QMouseEvent* event);
    void    gutterMouseMove(QMouseEvent* event);
    void    gutterMouseRelease(QMouseEvent* event);
    QTextBlock gutterBlockAt(int y) const;
    void    updateGutterDrag();

    LineNumberArea*     m_lineNumbers;
    MarkdownHighlighter* m_highlighter = nullptr;
    SourceHighlighter*  m_sourceHighlighter = nullptr;
    QTimer*             m_debounce;
    QString             m_documentPath;
    bool                m_dark = false;
    bool                m_markdownMode = true;
    
    int                 m_highlightAlpha = 0;
    int                 m_highlightLineNumber = -1;
    QPropertyAnimation* m_highlightAnim = nullptr;
    void                updateLineHighlight();
    void                flashLine(int line);

    bool                m_currentLineHighlightEnabled = true;

    // Absolute document positions of the misspellings currently painted.
    // Deliberately viewport-scoped, so its size is bounded by the window
    // height rather than by the document length.
    QVector<QPair<int, int>> m_spellRanges;
    QTimer*             m_spellDebounce = nullptr;
    bool                m_spellCheckActive = false;
    QVector<QMetaObject::Connection> m_spellConnections;
    void                appendSpellSelections(QList<QTextEdit::ExtraSelection>& selections) const;
    QPair<int, int>     spellRangeAt(int position) const;
    void                replaceRange(int start, int length, const QString& replacement);

    QVector<QPair<int, int>> m_searchMatches;
    int                       m_searchCurrentIndex = -1;

    QStringList         m_tempImages;
    bool                m_blockSelectionActive = false;
    bool                m_blockSelectionDragging = false;
    int                 m_blockAnchorBlock = 0;
    int                 m_blockCurrentBlock = 0;
    int                 m_blockAnchorColumn = 0;
    int                 m_blockCurrentColumn = 0;
    // Set while the column selection edits its own lines, so the document
    // watch below can tell those edits from undo/redo or any other writer.
    bool                m_blockSelectionEditing = false;
    int                 m_blockSelectionUndoSteps = 0;
    QMetaObject::Connection m_blockSelectionWatch;
    bool                m_gutterDragging = false;
    // False until the pointer leaves the pressed line, so a plain click (and
    // the press that precedes a double-click) never selects anything.
    bool                m_gutterSelecting = false;
    int                 m_gutterAnchorBlock = 0;
    int                 m_gutterDragY = 0;
    QTimer*             m_gutterAutoScroll = nullptr;
    QAction*            m_contextWordWrap = nullptr;
    QAction*            m_contextPreview = nullptr;
    QAction*            m_contextGoToPreview = nullptr;
    QAction*            m_contextCopyPath = nullptr;
    QAction*            m_contextReveal = nullptr;
    QAction*            m_contextTerminal = nullptr;
    QAction*            m_contextFormatDocument = nullptr;

    // Tab-navigable placeholder stops from the last inserted Math Toolbar
    // snippet (see MarkdownMathFormatting.cpp). Cleared on Escape, on click,
    // or once the last stop is passed.
    QVector<QTextCursor> m_snippetStops;
    int                   m_snippetStopIndex = -1;
    void                  clearSnippetStops();
    // Reused across invocations so its show-animation (QPropertyAnimation
    // objects, easing curves) is only constructed once per editor instead of
    // on every right-click, which previously caused a stutter each time.
    AlignedMenu*        m_contextMenu = nullptr;
    // Owned by its QWidgetAction, which is parented to this editor.
    MenuCommandBar*     m_contextCommandBar = nullptr;
};


// ---- companion widget -------------------------------------------------------
#include <QWidget>

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(MarkdownEditor* editor) : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override {
        return { m_editor->lineNumberWidth(), 0 };
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        m_editor->paintLineNumbers(event);
    }
    void mousePressEvent(QMouseEvent* event) override { m_editor->gutterMousePress(event); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { m_editor->gutterMouseDoubleClick(event); }
    void mouseMoveEvent(QMouseEvent* event) override { m_editor->gutterMouseMove(event); }
    void mouseReleaseEvent(QMouseEvent* event) override { m_editor->gutterMouseRelease(event); }

private:
    MarkdownEditor* m_editor;
};
