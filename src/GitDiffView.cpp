#include "GitDiffView.h"
#include "DarkTheme.h"

#include "FastScrollBar.h"
#include "ThemedSplitter.h"

#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kGutterPadding = 10;

// Row tints, GitHub/VS Code derived. Each side is coloured from whether it
// has a line, so a replaced pair shows removed on the left and added on the
// right without needing a second enum.
QColor addedColor(bool dark)   { return dark ? QColor(70, 149, 74, 46)   : QColor(0xe6, 0xff, 0xec); }
QColor removedColor(bool dark) { return dark ? QColor(229, 83, 75, 46)   : QColor(0xff, 0xeb, 0xe9); }
QColor gapColor(bool dark)     { return dark ? QColor(255, 255, 255, 8)  : QColor(0xf6, 0xf8, 0xfa); }
QColor hunkColor(bool dark)    { return dark ? QColor(56, 139, 253, 38)  : QColor(0xdd, 0xf4, 0xff); }
QColor hunkTextColor(bool dark){ return dark ? QColor(0x8b, 0x94, 0x9e)  : QColor(0x57, 0x60, 0x6a); }

class Gutter final : public QWidget {
public:
    explicit Gutter(GitDiffPane* pane)
        : QWidget(pane)
        , m_pane(pane)
    {
    }

    QSize sizeHint() const override { return QSize(m_pane->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { m_pane->paintGutter(event); }

private:
    GitDiffPane* m_pane;
};

}

// ---------------------------------------------------------------------------
// GitDiffPane

GitDiffPane::GitDiffPane(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setVerticalScrollBar(new FastScrollBar(Qt::Vertical, this));
    setHorizontalScrollBar(new FastScrollBar(Qt::Horizontal, this));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_baseFont = font();
    m_gutter = new Gutter(this);

    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect& rect, int dy) {
                if (dy != 0)
                    m_gutter->scroll(0, dy);
                else
                    m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            });
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this](int) { updateGutterGeometry(); });

    updateGutterGeometry();
}

void GitDiffPane::setContent(const QStringList& lines, const QVector<int>& lineNumbers)
{
    m_lineNumbers = lineNumbers;
    setPlainText(lines.join(QLatin1Char('\n')));
    updateGutterGeometry();
    m_gutter->update();
}

void GitDiffPane::setDarkMode(bool dark)
{
    m_dark = dark;
    m_gutter->update();
}

void GitDiffPane::setZoomDelta(int delta)
{
    setFont(m_baseFont);
    if (delta > 0)
        zoomIn(delta);
    else if (delta < 0)
        zoomOut(-delta);
}

void GitDiffPane::setSharedGutterWidth(int width)
{
    if (m_sharedGutterWidth == width)
        return;
    m_sharedGutterWidth = width;
    updateGutterGeometry();
}

int GitDiffPane::gutterWidth() const
{
    int widest = 1;
    for (int number : m_lineNumbers)
        widest = qMax(widest, number);

    int digits = 1;
    while (widest >= 10) {
        widest /= 10;
        ++digits;
    }
    const int calculated = kGutterPadding
        + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + kGutterPadding;
    return qMax(calculated, m_sharedGutterWidth);
}

void GitDiffPane::paintGutter(QPaintEvent* event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(),
                     m_dark ? DarkTheme::color(DarkTheme::Surface) : QColor(0xf0, 0xf0, 0xf0));
    painter.setPen(m_dark ? QColor(0x85, 0x85, 0x85) : QColor(0x90, 0x90, 0x90));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();

    while (block.isValid() && top <= event->rect().bottom()) {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int number = m_lineNumbers.value(blockNumber, -1);
            if (number > 0) {
                painter.drawText(0, qRound(top), m_gutter->width() - kGutterPadding,
                                 fontMetrics().height(), Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(number));
            }
        }
        block = block.next();
        top = bottom;
        ++blockNumber;
    }
}

void GitDiffPane::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    updateGutterGeometry();
}

void GitDiffPane::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier) && event->angleDelta().y() != 0) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void GitDiffPane::updateGutterGeometry()
{
    const int width = gutterWidth();
    setViewportMargins(width, 0, 0, 0);
    const QRect area = contentsRect();
    m_gutter->setGeometry(QRect(area.left(), area.top(), width, area.height()));
}

// ---------------------------------------------------------------------------
// GitDiffView

GitDiffView::GitDiffView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    m_placeholder = new QLabel(this);
    m_placeholder->setObjectName(QStringLiteral("gitDiffPlaceholder"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);

    m_splitter = new ThemedSplitter(Qt::Horizontal, ThemedSplitter::HandleStyle::Divider, 1, this);
    m_splitter->setChildrenCollapsible(false);

    m_left = new GitDiffPane(m_splitter);
    m_left->setObjectName(QStringLiteral("gitDiffLeft"));
    m_right = new GitDiffPane(m_splitter);
    m_right->setObjectName(QStringLiteral("gitDiffRight"));
    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_right);
    m_splitter->setSizes({ 1, 1 });

    m_stack->addWidget(m_placeholder);
    m_stack->addWidget(m_splitter);
    m_stack->setCurrentWidget(m_placeholder);

    // Mirrored scrollbars in both directions. Horizontal matters as much as
    // vertical here: reading a long changed line means scrolling right, and
    // only one side moving makes the comparison useless.
    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        syncScroll(m_left->verticalScrollBar(), m_right->verticalScrollBar());
    });
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        syncScroll(m_right->verticalScrollBar(), m_left->verticalScrollBar());
    });
    connect(m_left->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        syncScroll(m_left->horizontalScrollBar(), m_right->horizontalScrollBar());
    });
    connect(m_right->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        syncScroll(m_right->horizontalScrollBar(), m_left->horizontalScrollBar());
    });
    connect(m_left, &GitDiffPane::zoomRequested, this, &GitDiffView::adjustZoom);
    connect(m_right, &GitDiffPane::zoomRequested, this, &GitDiffView::adjustZoom);

    showPlaceholder(tr("Select a file to see its changes."));
}

void GitDiffView::adjustZoom(int delta)
{
    if (delta == 0)
        return;
    setZoomDelta(m_zoomDelta + delta);
    emit zoomDeltaChanged(m_zoomDelta);
}

void GitDiffView::setZoomDelta(int delta)
{
    m_zoomDelta = delta;
    // Apply the same absolute level to both persistent panes. setRows()
    // replaces their documents but not their fonts, so this level survives
    // file selection changes for the lifetime of the tab.
    m_left->setZoomDelta(delta);
    m_right->setZoomDelta(delta);
    refreshGutterWidths();
}

void GitDiffView::refreshGutterWidths()
{
    // Zoom changes the digit metrics too. Re-establish the common gutter so
    // the two panes remain aligned as their text gets larger or smaller.
    m_left->setSharedGutterWidth(0);
    m_right->setSharedGutterWidth(0);
    const int sharedGutterWidth = qMax(m_left->gutterWidth(), m_right->gutterWidth());
    m_left->setSharedGutterWidth(sharedGutterWidth);
    m_right->setSharedGutterWidth(sharedGutterWidth);
}

void GitDiffView::syncScroll(QScrollBar* source, QScrollBar* target)
{
    if (m_syncing)
        return;
    m_syncing = true;
    if (source->orientation() == Qt::Vertical) {
        // Fraction-of-range rather than a raw value copy: with word wrap on,
        // each row can wrap to a different height in the two panes (their
        // text differs), so the panes' scroll ranges diverge and a raw copy
        // drifts. Strictly equivalent to a raw copy when wrapping is off --
        // same block count and font on both sides gives identical ranges --
        // so this is a no-op behavior change for the common case.
        const int sourceRange = source->maximum() - source->minimum();
        const int targetRange = target->maximum() - target->minimum();
        const int value = sourceRange > 0
            ? target->minimum() + qRound(double(source->value() - source->minimum()) / sourceRange * targetRange)
            : target->minimum();
        target->setValue(value);
    } else {
        target->setValue(source->value());
    }
    m_syncing = false;
}

void GitDiffView::setRows(const QVector<GitDiffRow>& rows)
{
    if (rows.isEmpty()) {
        showPlaceholder(tr("No changes to show for this file."));
        return;
    }

    m_rows = rows;
    QStringList leftLines;
    QStringList rightLines;
    QVector<int> leftNumbers;
    QVector<int> rightNumbers;
    leftLines.reserve(rows.size());
    rightLines.reserve(rows.size());
    leftNumbers.reserve(rows.size());
    rightNumbers.reserve(rows.size());

    for (const GitDiffRow& row : rows) {
        leftLines.append(row.leftText);
        rightLines.append(row.rightText);
        leftNumbers.append(row.leftNo);
        rightNumbers.append(row.rightNo);
    }

    // Recalculate for every file: a previously wide gutter must not make a
    // later short diff look off-centre.
    m_left->setSharedGutterWidth(0);
    m_right->setSharedGutterWidth(0);
    m_left->setContent(leftLines, leftNumbers);
    m_right->setContent(rightLines, rightNumbers);
    refreshGutterWidths();
    applyRowTints(rows);
    m_stack->setCurrentWidget(m_splitter);
}

void GitDiffView::applyRowTints(const QVector<GitDiffRow>& rows)
{
    QList<QTextEdit::ExtraSelection> leftSelections;
    QList<QTextEdit::ExtraSelection> rightSelections;

    QTextDocument* leftDoc = m_left->document();
    QTextDocument* rightDoc = m_right->document();

    auto tint = [](QList<QTextEdit::ExtraSelection>& into, QTextDocument* document,
                   int blockNumber, const QColor& background, const QColor& foreground) {
        const QTextBlock block = document->findBlockByNumber(blockNumber);
        if (!block.isValid())
            return;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.format.setBackground(background);
        if (foreground.isValid())
            selection.format.setForeground(foreground);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        into.append(selection);
    };

    for (int i = 0; i < rows.size(); ++i) {
        const GitDiffRow& row = rows.at(i);
        switch (row.kind) {
        case GitDiffRow::Context:
            break;
        case GitDiffRow::HunkHeader:
            tint(leftSelections, leftDoc, i, hunkColor(m_dark), hunkTextColor(m_dark));
            tint(rightSelections, rightDoc, i, hunkColor(m_dark), hunkTextColor(m_dark));
            break;
        case GitDiffRow::Notice:
            tint(leftSelections, leftDoc, i, gapColor(m_dark), hunkTextColor(m_dark));
            tint(rightSelections, rightDoc, i, gapColor(m_dark), hunkTextColor(m_dark));
            break;
        case GitDiffRow::Change:
            tint(leftSelections, leftDoc, i,
                 row.hasLeft() ? removedColor(m_dark) : gapColor(m_dark), QColor());
            tint(rightSelections, rightDoc, i,
                 row.hasRight() ? addedColor(m_dark) : gapColor(m_dark), QColor());
            break;
        }
    }

    m_left->setExtraSelections(leftSelections);
    m_right->setExtraSelections(rightSelections);
}

void GitDiffView::showPlaceholder(const QString& text)
{
    m_rows.clear();
    m_placeholder->setText(text);
    m_stack->setCurrentWidget(m_placeholder);
}

void GitDiffView::setDarkMode(bool dark)
{
    m_dark = dark;
    m_left->setDarkMode(dark);
    m_right->setDarkMode(dark);
    m_splitter->setDarkMode(dark);
    if (!m_rows.isEmpty())
        applyRowTints(m_rows);
}

void GitDiffView::setWordWrapEnabled(bool enabled)
{
    m_wordWrap = enabled;
    // Both panes always carry the same wrap state -- an unwrapped side next
    // to a wrapped one would make the row-for-row alignment meaningless.
    m_left->setWordWrapEnabled(enabled);
    m_right->setWordWrapEnabled(enabled);
}

QByteArray GitDiffView::saveSplitterState() const
{
    return m_splitter->saveState();
}

void GitDiffView::restoreSplitterState(const QByteArray& state)
{
    if (state.isEmpty())
        return;
    m_splitter->restoreState(state);
    m_splitter->reassertHandleWidth();
}
