#pragma once
#include <QPlainTextEdit>

class LineNumberArea;
class MarkdownHighlighter;
class QTimer;

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget* parent = nullptr);

    void setDarkMode(bool dark);
    void setMarkdownMode(bool markdown);
    void setDocumentPath(const QString& path);
    const QString& documentPath() const;

    // Called by LineNumberArea's paintEvent
    void paintLineNumbers(QPaintEvent* event);
    int  lineNumberWidth() const;

public slots:
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);
    void fmtBold();
    void fmtItalic();
    void fmtStrike();
    void fmtCode();
    void fmtLink();
    void fmtImage(const QString& docPath = QString());
    void fmtH1();
    void fmtH2();
    void fmtH3();
    void fmtQuote();
    void fmtBulletList();
    void fmtNumberList();
    void fmtChecklist();
    void fmtTable(int rows = 3, int cols = 3);
    void fmtMathInline();
    void fmtMathBlock();

signals:
    void contentReady();   // emitted after 200 ms debounce
    void zoomRequested(int delta); // delta is +1 or -1
    void lineNumberWidthChanged(int width);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private slots:
    void onBlockCountChanged(int);
    void onUpdateRequest(const QRect& rect, int dy);
    void onTextChanged();

private:
    void    repositionLineNumberArea();
    bool    handleSmartList(QKeyEvent* event);
    QString markdownImagePath(const QString& imagePath) const;
    void    insertDroppedImages(const QStringList& imagePaths);

    LineNumberArea*     m_lineNumbers;
    MarkdownHighlighter* m_highlighter;
    QTimer*             m_debounce;
    QString             m_documentPath;
    bool                m_dark = false;
    bool                m_markdownMode = true;
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

private:
    MarkdownEditor* m_editor;
};
