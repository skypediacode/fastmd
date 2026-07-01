#include "MarkdownEditor.h"
#include "MarkdownHighlighter.h"
#include "ExportManager.h"

#include <QPainter>
#include <QTextBlock>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QTextLayout>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QMimeData>
#include <QDateTime>
#include <QUuid>

namespace {
constexpr int kEditorTopPadding = 4;

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
}

// ---------------------------------------------------------------------------
MarkdownEditor::MarkdownEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    // monospace font — prefer modern coding fonts, fall back gracefully
    QFont font;
    font.setFamilies({ QStringLiteral("Consolas"),
                       QStringLiteral("JetBrains Mono"),
                       QStringLiteral("IBM Plex Mono"),
                       QStringLiteral("Cascadia Code"),
                       QStringLiteral("Cascadia Mono"),
                       QStringLiteral("Courier New"),
                       QStringLiteral("monospace") });
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(FastMdDefaults::EditorFontSize);
    font.setFixedPitch(true);
    setFont(font);

    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);

    m_lineNumbers = new LineNumberArea(this);
    m_highlighter = new MarkdownHighlighter(document());

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &MarkdownEditor::onBlockCountChanged);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &MarkdownEditor::onUpdateRequest);
    connect(document(), &QTextDocument::contentsChanged,
            this, &MarkdownEditor::onTextChanged);
    connect(m_debounce, &QTimer::timeout,
            this,       &MarkdownEditor::contentReady);

    onBlockCountChanged(0);
}

// ---------------------------------------------------------------------------
void MarkdownEditor::setDarkMode(bool dark)
{
    m_dark = dark;
    m_highlighter->setDarkMode(dark);

    QPalette p = palette();
    if (dark) {
        p.setColor(QPalette::Base,  QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::Text,  QColor(0xd4, 0xd4, 0xd4));
    } else {
        p.setColor(QPalette::Base,  Qt::white);
        p.setColor(QPalette::Text,  QColor(0x22, 0x22, 0x22));
    }
    setPalette(p);
    m_lineNumbers->update();
}

void MarkdownEditor::setMarkdownMode(bool markdown)
{
    if (m_markdownMode == markdown)
        return;

    m_markdownMode = markdown;
    m_highlighter->setEnabled(markdown);
}

void MarkdownEditor::setDocumentPath(const QString& path)
{
    m_documentPath = path;
}

const QString& MarkdownEditor::documentPath() const
{
    return m_documentPath;
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
    setViewportMargins(w, kEditorTopPadding, 0, 0);
    QRect cr = contentsRect();
    m_lineNumbers->setGeometry(QRect(cr.left(), cr.top(), w, cr.height()));
    emit lineNumberWidthChanged(w);
}

void MarkdownEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    repositionLineNumberArea();
}

void MarkdownEditor::paintEvent(QPaintEvent* e)
{
    QPlainTextEdit::paintEvent(e);

    QPainter painter(viewport());
    painter.setClipRect(e->rect());

    const QColor lineCol  = m_dark ? QColor(0x55, 0x88, 0xaa) : QColor(0x88, 0xa8, 0xc0);
    const QColor bgCol    = m_dark ? QColor(0x1e, 0x1e, 0x1e) : Qt::white;
    const QColor textCol  = m_dark ? QColor(0x66, 0x99, 0xbb) : QColor(0x77, 0x99, 0xaa);

    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();

    while (block.isValid()) {
        const QRectF geom = blockBoundingGeometry(block).translated(offset);
        if (geom.top() > e->rect().bottom()) break;

        if (block.text().contains(QLatin1String("break-after: page"))) {
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
            destDir = QFileInfo(m_documentPath).absolutePath() + QStringLiteral("/images");
        }

        QDir dir(destDir);
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }

        QTextCursor c = textCursor();
        c.beginEditBlock();

        if (!imagePaths.isEmpty()) {
            for (int i = 0; i < imagePaths.size(); ++i) {
                QString srcPath = imagePaths.at(i);
                QFileInfo fi(srcPath);
                QString baseName = fi.completeBaseName();
                QString suffix = fi.suffix();
                if (suffix.isEmpty()) suffix = QStringLiteral("png");
                QString uniqueName = baseName + QStringLiteral("_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QStringLiteral(".") + suffix;
                QString destPath = destDir + QStringLiteral("/") + uniqueName;
                
                QFile::copy(srcPath, destPath);
                
                if (isTemp) m_tempImages.append(destPath);

                QString outPath = isTemp ? destPath : QStringLiteral("images/") + uniqueName;
                if (outPath.contains(' ') || outPath.contains('[') || outPath.contains(']'))
                    outPath = QStringLiteral("<") + outPath + QStringLiteral(">");

                c.insertText(QStringLiteral("![](") + outPath + QStringLiteral(")"));
                if (i + 1 < imagePaths.size())
                    c.insertText(QStringLiteral("\n"));
            }
        } else if (isImage) {
            QImage image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull()) {
                QString uniqueName = QStringLiteral("image_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QStringLiteral(".png");
                QString destPath = destDir + QStringLiteral("/") + uniqueName;
                image.save(destPath, "PNG");
                
                if (isTemp) m_tempImages.append(destPath);

                QString outPath = isTemp ? destPath : QStringLiteral("images/") + uniqueName;
                if (outPath.contains(' ') || outPath.contains('[') || outPath.contains(']'))
                    outPath = QStringLiteral("<") + outPath + QStringLiteral(">");

                c.insertText(QStringLiteral("![](") + outPath + QStringLiteral(")"));
            }
        }

        c.endEditBlock();
        setTextCursor(c);
        return;
    }

    QPlainTextEdit::insertFromMimeData(source);
}

void MarkdownEditor::commitTemporaryImages(const QString& newDocPath)
{
    if (m_tempImages.isEmpty() || newDocPath.isEmpty())
        return;

    QString destDir = QFileInfo(newDocPath).absolutePath() + QStringLiteral("/images");
    QDir dir(destDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QTextCursor c = textCursor();
    c.beginEditBlock();

    for (const QString& tempPath : m_tempImages) {
        if (!QFile::exists(tempPath)) continue;

        QFileInfo fi(tempPath);
        QString uniqueName = fi.fileName();
        QString destPath = destDir + QStringLiteral("/") + uniqueName;

        QFile::copy(tempPath, destPath);
        
        QString oldLink = tempPath;
        if (oldLink.contains(' ') || oldLink.contains('[') || oldLink.contains(']'))
            oldLink = QStringLiteral("<") + oldLink + QStringLiteral(">");
            
        QString newLink = QStringLiteral("images/") + uniqueName;
        if (newLink.contains(' ') || newLink.contains('[') || newLink.contains(']'))
            newLink = QStringLiteral("<") + newLink + QStringLiteral(">");

        QTextCursor searchCursor(document());
        while (!searchCursor.isNull() && !searchCursor.atEnd()) {
            searchCursor = document()->find(oldLink, searchCursor);
            if (!searchCursor.isNull()) {
                searchCursor.insertText(newLink);
            }
        }
    }
    
    c.endEditBlock();
    m_tempImages.clear();
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

    if (rect.contains(viewport()->rect()))
        onBlockCountChanged(0);
}

void MarkdownEditor::onTextChanged()
{
    m_debounce->start();
}

// ---------------------------------------------------------------------------
void MarkdownEditor::paintLineNumbers(QPaintEvent* event)
{
    QPainter painter(m_lineNumbers);

    QColor bg = m_dark ? QColor(0x25, 0x25, 0x25) : QColor(0xf0, 0xf0, 0xf0);
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

    // Draw Notepad++ style orange separator line on the right edge
    QPen orangePen(QColor(0xff, 0x8c, 0x00));
    orangePen.setWidth(2);
    painter.setPen(orangePen);
    // painter.drawLine(m_lineNumbers->width() - 1, event->rect().top(),
    //                  m_lineNumbers->width() - 1, event->rect().bottom());
}

// ---------------------------------------------------------------------------
void MarkdownEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (handleSmartList(event))
            return;
    }

    // auto-indent on Enter
    if (event->key() == Qt::Key_Return && !(event->modifiers() & Qt::ShiftModifier)) {
        QTextCursor cur = textCursor();
        QString line = cur.block().text();
        QString indent;
        for (QChar c : line) {
            if (c == ' ' || c == '\t') indent += c;
            else break;
        }
        QPlainTextEdit::keyPressEvent(event);
        if (!indent.isEmpty())
            insertPlainText(indent);
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

bool MarkdownEditor::handleSmartList(QKeyEvent* event)
{
    QTextCursor cur  = textCursor();
    QString     line = cur.block().text();

    // match unordered list marker:  optional spaces, then - * or +, then space
    static QRegularExpression rxUl(QStringLiteral("^(\\s*)([-*+])( +)(.*)$"));
    // match ordered list marker:   optional spaces, digits, period, space
    static QRegularExpression rxOl(QStringLiteral("^(\\s*)(\\d+)(\\.)( +)(.*)$"));

    QRegularExpressionMatch mu = rxUl.match(line);
    QRegularExpressionMatch mo = rxOl.match(line);

    if (mu.hasMatch()) {
        QString indent  = mu.captured(1);
        QString marker  = mu.captured(2);
        QString space   = mu.captured(3);
        QString content = mu.captured(4);

        if (content.trimmed().isEmpty()) {
            // empty item → exit list
            cur.select(QTextCursor::BlockUnderCursor);
            cur.removeSelectedText();
            cur.insertText(QStringLiteral("\n"));
        } else {
            cur.movePosition(QTextCursor::EndOfBlock);
            cur.insertText(QStringLiteral("\n") + indent + marker + space);
        }
        setTextCursor(cur);
        return true;
    }

    if (mo.hasMatch()) {
        QString indent  = mo.captured(1);
        int     num     = mo.captured(2).toInt();
        QString dot     = mo.captured(3);
        QString space   = mo.captured(4);
        QString content = mo.captured(5);

        if (content.trimmed().isEmpty()) {
            cur.select(QTextCursor::BlockUnderCursor);
            cur.removeSelectedText();
            cur.insertText(QStringLiteral("\n"));
        } else {
            cur.movePosition(QTextCursor::EndOfBlock);
            cur.insertText(QStringLiteral("\n") + indent +
                           QString::number(num + 1) + dot + space);
        }
        setTextCursor(cur);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
static void wrapInline(QPlainTextEdit* ed, const QString& marker)
{
    QTextCursor c = ed->textCursor();
    if (c.hasSelection()) {
        QString sel = c.selectedText();
        c.insertText(marker + sel + marker);
    } else {
        int pos = c.position();
        c.insertText(marker + marker);
        c.setPosition(pos + marker.length());
        ed->setTextCursor(c);
    }
}

static void prependLines(QPlainTextEdit* ed, const QString& prefix)
{
    QTextCursor c = ed->textCursor();
    c.beginEditBlock();
    int start = c.selectionStart();
    int end   = c.selectionEnd();
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    while (c.position() <= end || c.block().position() <= end) {
        c.insertText(prefix);
        end += prefix.length();
        if (!c.movePosition(QTextCursor::NextBlock)) break;
    }
    c.endEditBlock();
}

void MarkdownEditor::fmtBold()        { wrapInline(this, QStringLiteral("**")); }
void MarkdownEditor::fmtItalic()      { wrapInline(this, QStringLiteral("*")); }
void MarkdownEditor::fmtStrike()      { wrapInline(this, QStringLiteral("~~")); }
void MarkdownEditor::fmtCode()        { wrapInline(this, QStringLiteral("`")); }

void MarkdownEditor::fmtMathInline()  { wrapInline(this, QStringLiteral("$")); }

void MarkdownEditor::fmtPageBreak()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    // Move to end of current line, then insert the page-break div on its own line.
    c.movePosition(QTextCursor::EndOfBlock);
    const bool atLineStart = c.positionInBlock() == 0;
    if (!atLineStart)
        c.insertText(QStringLiteral("\n"));
    c.insertText(QStringLiteral("<div style=\"break-after: page;\"></div>\n"));
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtMathBlock()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(QStringLiteral("$$\n\n$$\n"));
    // Place cursor between the $$ delimiters
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::PreviousBlock);
    c.movePosition(QTextCursor::StartOfBlock);
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEditor::fmtLink()
{
    QTextCursor c = textCursor();
    QString sel = c.selectedText();
    if (sel.isEmpty()) {
        int pos = c.position();
        c.insertText(QStringLiteral("[link text](url)"));
        c.setPosition(pos + 1);
        c.setPosition(pos + 10, QTextCursor::KeepAnchor);
    } else {
        c.insertText(QStringLiteral("[") + sel + QStringLiteral("](url)"));
    }
    setTextCursor(c);
}

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

QString MarkdownEditor::markdownImagePath(const QString& imagePath) const
{
    QString finalPath = imagePath;
    if (!m_documentPath.isEmpty()) {
        QDir docDir(QFileInfo(m_documentPath).absolutePath());
        finalPath = docDir.relativeFilePath(imagePath);
    }

    if (finalPath.contains(' ') || finalPath.contains('[') || finalPath.contains(']'))
        finalPath = QStringLiteral("<") + finalPath + QStringLiteral(">");

    return finalPath;
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

// Set the heading level on the current line (toggles off if already at that level).
static void setHeadingLevel(QPlainTextEdit* ed, int level)
{
    static QRegularExpression rxH(QStringLiteral("^(#{1,6}) "));
    QTextCursor c = ed->textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    QString line = c.block().text();
    QRegularExpressionMatch m = rxH.match(line);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    if (m.hasMatch()) {
        QString rest = line.mid(m.capturedLength());
        if (m.captured(1).length() == level)
            c.insertText(rest);                              // toggle off
        else
            c.insertText(QString(level, '#') + QStringLiteral(" ") + rest);
    } else {
        c.insertText(QString(level, '#') + QStringLiteral(" ") + line);
    }
    c.endEditBlock();
}

void MarkdownEditor::fmtH1() { setHeadingLevel(this, 1); }
void MarkdownEditor::fmtH2() { setHeadingLevel(this, 2); }
void MarkdownEditor::fmtH3() { setHeadingLevel(this, 3); }

void MarkdownEditor::fmtQuote()       { prependLines(this, QStringLiteral("> ")); }
void MarkdownEditor::fmtBulletList()  { prependLines(this, QStringLiteral("- ")); }
void MarkdownEditor::fmtChecklist()   { prependLines(this, QStringLiteral("- [ ] ")); }

void MarkdownEditor::fmtTable(int rows, int cols)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);

    QString header, separator, dataRow;
    for (int c = 0; c < cols; ++c) {
        QString colName = QStringLiteral("Col ") + QString::number(c + 1);
        header    += QStringLiteral("| ") + colName.leftJustified(9) + QStringLiteral(" ");
        separator += QStringLiteral("| --------- ");
        dataRow   += QStringLiteral("| Cell      ");
    }
    header    += QStringLiteral("|\n");
    separator += QStringLiteral("|\n");
    dataRow   += QStringLiteral("|\n");

    QString table = header + separator;
    for (int r = 0; r < rows; ++r)
        table += dataRow;

    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(table);
    c.endEditBlock();
}

void MarkdownEditor::fmtNumberList()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    int start = c.selectionStart();
    int end   = c.selectionEnd();
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    int n = 1;
    while (c.position() <= end || c.block().position() <= end) {
        QString prefix = QString::number(n++) + QStringLiteral(". ");
        c.insertText(prefix);
        end += prefix.length();
        if (!c.movePosition(QTextCursor::NextBlock)) break;
    }
    c.endEditBlock();
}
