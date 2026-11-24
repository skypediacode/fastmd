#include "MatrixInsertDialog.h"
#include "DarkTheme.h"
#include "MarkdownEditor.h"
#include "IconHelper.h"
#include "PreviewWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QGridLayout>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <functional>

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

namespace {

struct MatrixTypeInfo {
    QString id;      // preview delimiter id / distinguishing key
    QString env;     // LaTeX environment name to wrap the matrix body in
    QString label;   // tooltip
    QString iconId;  // id IconHelper::generatedMathSvgIcon looks up
};

const QVector<MatrixTypeInfo>& matrixTypes()
{
    static const QVector<MatrixTypeInfo> types = {
        { QStringLiteral("pmatrix"),     QStringLiteral("pmatrix"),     QObject::tr("(x)  Parenthesized matrix"),   QStringLiteral("pmatrix") },
        { QStringLiteral("bmatrix"),     QStringLiteral("bmatrix"),     QObject::tr("[x]  Bracket matrix"),         QStringLiteral("bmatrix") },
        { QStringLiteral("Bmatrix"),     QStringLiteral("Bmatrix"),     QObject::tr("{x}  Braced matrix"),          QStringLiteral("brace-matrix") },
        { QStringLiteral("vmatrix"),     QStringLiteral("vmatrix"),     QObject::tr("|x|  Determinant matrix"),     QStringLiteral("vmatrix") },
        { QStringLiteral("Vmatrix"),     QStringLiteral("Vmatrix"),     QObject::tr("\xE2\x80\x96x\xE2\x80\x96  Norm matrix"), QStringLiteral("norm-matrix") },
        { QStringLiteral("cases"),       QStringLiteral("cases"),       QObject::tr("Cases (piecewise)"),           QStringLiteral("cases") },
        { QStringLiteral("rcases"),      QStringLiteral("rcases"),      QObject::tr("Right-braced cases"),          QStringLiteral("rcases") },
        { QStringLiteral("smallmatrix"), QStringLiteral("smallmatrix"), QObject::tr("Small matrix (inline)"),       QStringLiteral("smallmatrix") },
    };
    return types;
}

// Matrix type button metrics -- shared between buildTypeButtons() and the
// dialog width calculation, which sizes the dialog to the type-button row.
constexpr int kTypeButtonW  = 90;
constexpr int kTypeButtonH  = 84;
constexpr int kTypeIconSize = 60;
constexpr int kTypeRowSpacing = 8;
constexpr int kDialogSideMargin = 24;
constexpr int kTypeGridCols = 4;
constexpr int kTypeGridRows = 2;

// The shared content width used by the matrix-size selector, the 4x2 Matrix
// Type grid, and the Preview panel -- the Matrix Type grid (this dialog's
// widest fixed-size content) defines it.
constexpr int kContentWidth = kTypeGridCols * kTypeButtonW + (kTypeGridCols - 1) * kTypeRowSpacing;

// RoundedScrollArea's viewport inset (see setViewportMargins() there) --
// mirrored here since QAbstractScrollArea::viewportMargins() is protected
// and MatrixGridSelector only holds a plain QScrollArea*.
constexpr int kGridPanelBorderWidth = 1;

int defaultTypeIndex()
{
    const auto& types = matrixTypes();
    for (int i = 0; i < types.size(); ++i) {
        if (types.at(i).id == QStringLiteral("pmatrix"))
            return i;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// RoundedScrollArea -- a QScrollArea whose frame is a rounded rectangle
// instead of Qt's default square-cornered QSS frame. The background and
// border are painted here from the exact same QPainterPath/radius, and the
// viewport is masked to a matching (border-inset) rounded rect so child
// content -- the MatrixGridSelector's grid squares -- never paints past the
// rounded corners no matter how the child is scrolled.
// ---------------------------------------------------------------------------
class RoundedScrollArea : public QScrollArea {
public:
    explicit RoundedScrollArea(QWidget* parent = nullptr)
        : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setAttribute(Qt::WA_StyledBackground, false);
        // The viewport otherwise auto-fills with its own default palette
        // background (visibly grey against the explicit white/dark panel
        // colors painted below) in any sliver MatrixGridSelector doesn't
        // yet cover.
        if (viewport())
            viewport()->setAutoFillBackground(false);
        // Inset the viewport by the border stroke width so the viewport
        // child widget -- which otherwise would paint on top of and hide
        // this widget's own painted border -- never overlaps those pixels.
        // The border always stays visible, on top, unobscured by content.
        setViewportMargins(kGridPanelBorderWidth, kGridPanelBorderWidth, kGridPanelBorderWidth, kGridPanelBorderWidth);
    }

    void setPanelColors(const QColor& background, const QColor& border)
    {
        m_bg = background;
        m_border = border;
        update();
    }

    // Re-applies the viewport mask against the current (now-authoritative,
    // post-show) geometry. Cheap and idempotent -- safe to call from
    // MatrixInsertDialog::showEvent() every time the dialog becomes visible.
    void resyncViewportMask() { applyViewportMask(); }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QScrollArea::resizeEvent(event);
        applyViewportMask();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Corners outside the rounded shape show the surrounding dialog
        // background so the rounding reads correctly instead of leaving a
        // square backing-store fill behind the rounded panel.
        p.fillRect(rect(), palette().color(QPalette::Window));

        const QRectF r = QRectF(rect()).adjusted(kBorderWidth / 2.0, kBorderWidth / 2.0,
                                                   -kBorderWidth / 2.0, -kBorderWidth / 2.0);
        QPainterPath path;
        path.addRoundedRect(r, kRadius, kRadius);
        p.fillPath(path, m_bg);
        p.setPen(QPen(m_border, kBorderWidth));
        p.drawPath(path);
    }

private:
    void applyViewportMask()
    {
        if (!viewport())
            return;
        QPainterPath path;
        path.addRoundedRect(QRectF(viewport()->rect()), qMax(0.0, kRadius - kBorderWidth),
                             qMax(0.0, kRadius - kBorderWidth));
        viewport()->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    static constexpr qreal kRadius = 4.0;
    static constexpr qreal kBorderWidth = 1.0;
    QColor m_bg = Qt::white;
    QColor m_border = Qt::gray;
};

// ---------------------------------------------------------------------------
// MatrixGridSelector -- LyX-style expandable size selector living inside a
// fixed-viewport QScrollArea.
//
// Three separate size states are tracked:
//   - committed rows/cols: the last size the user actually chose (mirrors the
//     Rows/Columns spin boxes and the Preview). Persists across drags.
//   - drag rows/cols: only meaningful while m_dragging is true -- the
//     in-progress selection, tracked live from the pointer position.
//   - render rows/cols: the grid actually painted/laid out right now. While
//     idle this equals the committed size (rendered with a neutral fill, no
//     blue); while dragging it equals the drag size exactly, so a 2x2 drag
//     paints exactly a 2x2 grid, never an extra guide row/column.
//
// Press-and-drag starts an active selection: the rectangle from the
// top-left cell to the pointer is highlighted, the drag can extend past the
// currently laid-out grid (which grows to follow the pointer exactly, up to
// m_maxDim, with the surrounding QScrollArea gaining scrollbars once it
// outgrows the viewport), and the pointer is grabbed so the drag keeps
// tracking even once it leaves this widget's own bounds. Nearing a viewport
// edge while dragging auto-scrolls. Releasing commits the drag size as the
// new committed size (reported via onChange) and the grid keeps rendering
// that committed size -- it never snaps back to a hard-coded idle size.
// ---------------------------------------------------------------------------
class MatrixGridSelector : public QWidget {
public:
    explicit MatrixGridSelector(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        resize(cellsWidth(m_renderCols), cellsHeight(m_renderRows));
    }

    void setScrollArea(QScrollArea* area) { m_scrollArea = area; }
    void setOnChange(std::function<void(int, int)> cb) { m_onChange = std::move(cb); }
    void setDarkMode(bool dark)
    {
        m_darkMode = dark;
        update();
    }

    // Called when the Rows/Columns spin boxes are edited directly, so the
    // rendered grid always matches what Rows/Columns/Preview show.
    void setCommittedSize(int rows, int cols)
    {
        rows = qBound(1, rows, m_maxDim);
        cols = qBound(1, cols, m_maxDim);
        m_committedRows = rows;
        m_committedCols = cols;
        if (!m_dragging) {
            m_renderRows = rows;
            m_renderCols = cols;
            updateGeometryForRender();
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const bool isDarkTheme = m_darkMode;

        // Explicit, theme-aware colors rather than the ambiguous raw
        // QPalette::Base (which can read as an off-white/beige "wash" on
        // some Windows styles). The viewport background is filled uniformly
        // (see updateGeometryForRender(), which keeps this widget at least
        // as large as the QScrollArea viewport) so no grey QScrollArea
        // backdrop ever shows around a small grid.
        const QColor viewportBg = isDarkTheme ? DarkTheme::color(DarkTheme::Editor) : QColor(0xff, 0xff, 0xff);
        // m_renderRows/m_renderCols always exactly match the current
        // selection -- the committed size while idle, the in-progress drag
        // size while dragging -- so every rendered cell represents a
        // selected cell in both states. One neutral grey (deliberately not
        // the application accent blue) fill/border pair is used for it
        // regardless of m_dragging, so committing a drag never changes how
        // the grid looks.
        const QColor selectedColor  = isDarkTheme ? QColor(0x50, 0x50, 0x50) : QColor(0xd6, 0xd6, 0xd6);
        const QColor selectedBorder = isDarkTheme ? QColor(0xa0, 0xa0, 0xa0) : QColor(0x707070);

        p.fillRect(rect(), viewportBg);

        // A pen stroked exactly on a cell's integer-pixel boundary straddles
        // two rows/columns of pixels; without antialiasing, the raster
        // engine has to round that coverage per edge, and the four edges of
        // the same rect can round differently -- exactly the "some sides
        // look thicker/darker" artifact. Insetting the stroked rect by half
        // a pixel puts the (non-antialiased) line squarely on one row/column
        // instead. A cosmetic pen (width 0) keeps that line exactly one
        // device pixel wide regardless of display scaling (100/125/150%).
        QPen borderPen(selectedBorder);
        borderPen.setWidth(0);
        borderPen.setCosmetic(true);
        borderPen.setJoinStyle(Qt::MiterJoin);
        p.setPen(borderPen);

        for (int r = 0; r < m_renderRows; ++r) {
            for (int c = 0; c < m_renderCols; ++c) {
                const QRect cr = cellRect(r, c);
                p.fillRect(cr, selectedColor);
                const QRectF borderRect(cr.x() + 0.5, cr.y() + 0.5, cr.width() - 1, cr.height() - 1);
                p.drawRect(borderRect);
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        // A press alone must not commit a new size -- only grab the mouse
        // and remember where the press started. Actual dragging (and thus
        // any visible selection change) begins only once the pointer moves
        // past a small threshold, in mouseMoveEvent() below.
        m_pressed = true;
        m_dragging = false;
        m_pressGlobalPos = e->globalPosition().toPoint();
        grabMouse();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_pressed)
            return;
        const QPoint globalPos = e->globalPosition().toPoint();
        if (!m_dragging) {
            if ((globalPos - m_pressGlobalPos).manhattanLength() < QApplication::startDragDistance())
                return;
            m_dragging = true;
            if (!m_scrollTimer) {
                m_scrollTimer = new QTimer(this);
                connect(m_scrollTimer, &QTimer::timeout, this, &MatrixGridSelector::autoScrollTick);
            }
            m_scrollTimer->start(40);
        }
        updateFromGlobalPos(globalPos);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !m_pressed)
            return;
        m_pressed = false;
        releaseMouse();
        if (m_scrollTimer)
            m_scrollTimer->stop();
        if (!m_dragging)
            return; // click without a meaningful drag: keep the previous committed size
        m_dragging = false;
        m_committedRows = m_dragRows;
        m_committedCols = m_dragCols;
        m_renderRows = m_committedRows;
        m_renderCols = m_committedCols;
        updateGeometryForRender();
        update();
        if (m_onChange)
            m_onChange(m_committedRows, m_committedCols);
    }

private:
    QRect cellRect(int row, int col) const
    {
        return QRect(m_pad + col * (m_cell + m_gap), m_pad + row * (m_cell + m_gap), m_cell, m_cell);
    }

    static int cellsWidth(int cols)  { return 2 * m_pad + cols * (m_cell + m_gap) - m_gap; }
    static int cellsHeight(int rows) { return 2 * m_pad + rows * (m_cell + m_gap) - m_gap; }

    // The widget is kept at least as large as the QScrollArea's viewport
    // (so paintEvent's uniform background fill covers the whole visible
    // area, never leaving a grey QScrollArea backdrop around a small grid),
    // and grows beyond that only when the rendered grid itself needs more
    // room -- at which point the QScrollArea grows scrollbars.
    void updateGeometryForRender()
    {
        const int contentW = cellsWidth(m_renderCols);
        const int contentH = cellsHeight(m_renderRows);
        // Derive the target viewport size from the QScrollArea's own frame
        // size and viewport margins -- both set synchronously by
        // setFixedSize()/setViewportMargins() at construction -- rather than
        // viewport()->size(). QAbstractScrollArea only computes the
        // viewport's actual geometry from a resizeEvent-driven internal
        // layout pass; querying it here (e.g. from setCommittedSize() while
        // the dialog is still hidden and never received that pass) could
        // read a stale/default size, undersizing the grid until some later
        // event happens to re-trigger this same computation.
        QSize vp(360, 240);
        if (m_scrollArea) {
            const QSize frame = m_scrollArea->size();
            const int inset = 2 * kGridPanelBorderWidth;
            vp = QSize(qMax(0, frame.width() - inset), qMax(0, frame.height() - inset));
        }
        resize(qMax(contentW, vp.width()), qMax(contentH, vp.height()));
    }

    // Grows the drag (and rendered) grid to follow the pointer exactly --
    // never a row/column further than the pointer has actually reached, so
    // e.g. a 2x2 drag selection never paints a guide 3rd row/column.
    void growIfNeeded(int hoverRow, int hoverCol)
    {
        const int rows = qMin(m_maxDim, hoverRow);
        const int cols = qMin(m_maxDim, hoverCol);
        if (rows == m_renderRows && cols == m_renderCols)
            return;
        m_renderRows = rows;
        m_renderCols = cols;
        updateGeometryForRender();
    }

    void updateFromGlobalPos(const QPoint& globalPos)
    {
        m_lastGlobalPos = globalPos;
        const QPoint local = mapFromGlobal(globalPos);
        const int row = qBound(1, (local.y() - m_pad) / (m_cell + m_gap) + 1, m_maxDim);
        const int col = qBound(1, (local.x() - m_pad) / (m_cell + m_gap) + 1, m_maxDim);
        growIfNeeded(row, col);
        m_dragRows = row;
        m_dragCols = col;
        update();
    }

    void autoScrollTick()
    {
        if (!m_dragging || !m_scrollArea)
            return;
        QWidget* viewport = m_scrollArea->viewport();
        const QPoint vpPos = viewport->mapFromGlobal(m_lastGlobalPos);
        const QRect vp = viewport->rect();
        constexpr int margin = 22;
        constexpr int step = 16;
        bool scrolled = false;
        QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
        QScrollBar* vbar = m_scrollArea->verticalScrollBar();
        if (vpPos.x() > vp.right() - margin) { hbar->setValue(hbar->value() + step); scrolled = true; }
        else if (vpPos.x() < vp.left() + margin) { hbar->setValue(hbar->value() - step); scrolled = true; }
        if (vpPos.y() > vp.bottom() - margin) { vbar->setValue(vbar->value() + step); scrolled = true; }
        else if (vpPos.y() < vp.top() + margin) { vbar->setValue(vbar->value() - step); scrolled = true; }
        if (scrolled)
            updateFromGlobalPos(m_lastGlobalPos);
    }

    static constexpr int m_cell        = 28;
    static constexpr int m_gap         = 3;
    static constexpr int m_pad         = 28;  // generous margin -- comfortable interactive
                                               // space around the grid
    static constexpr int m_baseDim     = 2;   // default committed grid
    static constexpr int m_maxDim      = 30;  // practical upper bound on draggable size

    int m_committedRows = m_baseDim;
    int m_committedCols = m_baseDim;
    int m_dragRows = m_baseDim;
    int m_dragCols = m_baseDim;
    int m_renderRows = m_baseDim;
    int m_renderCols = m_baseDim;
    bool m_dragging = false;
    bool m_pressed = false;
    bool m_darkMode = false;
    QPoint m_pressGlobalPos;
    QPoint m_lastGlobalPos;
    QTimer* m_scrollTimer = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    std::function<void(int, int)> m_onChange;
};

// ---------------------------------------------------------------------------
// MatrixInsertDialog
// ---------------------------------------------------------------------------
MatrixInsertDialog::MatrixInsertDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Insert Matrix"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setSizeGripEnabled(false);

    m_darkMode = palette().color(QPalette::Window).lightness() < 128;
    applyTitleBarTheme();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kDialogSideMargin, 20, kDialogSideMargin, 20);
    root->setSpacing(14);

    // Rows/Columns: a balanced two-column layout (each side gets equal
    // stretch) rather than left-packed controls, spanning the dialog's
    // shared content width.
    auto* dimsRow = new QHBoxLayout();
    dimsRow->setSpacing(20);
    auto* rowsLabel = new QLabel(tr("Rows:"), this);
    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setRange(1, 30);
    m_rowsSpin->setValue(2);
    auto* colsLabel = new QLabel(tr("Columns:"), this);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 30);
    m_colsSpin->setValue(2);
    auto* rowsGroup = new QHBoxLayout();
    rowsGroup->setSpacing(10);
    rowsGroup->addWidget(rowsLabel);
    rowsGroup->addWidget(m_rowsSpin, 1);
    auto* colsGroup = new QHBoxLayout();
    colsGroup->setSpacing(10);
    colsGroup->addWidget(colsLabel);
    colsGroup->addWidget(m_colsSpin, 1);
    dimsRow->addLayout(rowsGroup, 1);
    dimsRow->addLayout(colsGroup, 1);
    root->addLayout(dimsRow);

    m_gridScrollArea = new RoundedScrollArea(this);
    m_gridSelector = new MatrixGridSelector();
    m_gridScrollArea->setWidget(m_gridSelector);
    m_gridScrollArea->setWidgetResizable(false);
    // Match the Matrix Type grid's width so the size selector, the type
    // grid, and the Preview panel all share one content width.
    m_gridScrollArea->setFixedSize(kContentWidth, 240);
    m_gridScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_gridScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_gridSelector->setScrollArea(m_gridScrollArea);
    // MatrixGridSelector's constructor sizes itself before it has a
    // QScrollArea to measure, so it starts far smaller than the viewport;
    // nothing else forces a resize until the user drags (setCommittedSize()
    // to the already-current 2x2 default wouldn't fire a spin box signal).
    // Without this, the viewport's own default background shows through
    // around the small grid until the first drag interaction resizes it.
    m_gridSelector->setCommittedSize(m_rowsSpin->value(), m_colsSpin->value());
    m_gridSelector->setOnChange([this](int rows, int cols) {
        m_rowsSpin->blockSignals(true);
        m_colsSpin->blockSignals(true);
        m_rowsSpin->setValue(rows);
        m_colsSpin->setValue(cols);
        m_rowsSpin->blockSignals(false);
        m_colsSpin->blockSignals(false);
        updatePreview();
    });
    auto* gridRow = new QHBoxLayout();
    gridRow->addStretch();
    gridRow->addWidget(m_gridScrollArea);
    gridRow->addStretch();
    root->addLayout(gridRow);

    auto* typeLabel = new QLabel(tr("Matrix type"), this);
    typeLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    root->addWidget(typeLabel);
    auto* typeGrid = new QGridLayout();
    typeGrid->setContentsMargins(0, 0, 0, 0);
    typeGrid->setHorizontalSpacing(kTypeRowSpacing);
    typeGrid->setVerticalSpacing(kTypeRowSpacing);
    buildTypeButtons(typeGrid);
    root->addLayout(typeGrid);

    auto* previewLabel = new QLabel(tr("Preview"), this);
    previewLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    root->addWidget(previewLabel);
    m_previewFrame = new QFrame(this);
    m_previewFrame->setObjectName(QStringLiteral("matrixPreviewFrame"));
    auto* previewFrameLayout = new QVBoxLayout(m_previewFrame);
    previewFrameLayout->setContentsMargins(1, 1, 1, 1);
    m_preview = new PreviewWidget(m_previewFrame);
    m_preview->setMinimumHeight(144);
    m_preview->setDarkMode(palette().color(QPalette::Window).lightness() < 128);
    previewFrameLayout->addWidget(m_preview);
    root->addWidget(m_previewFrame);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* btnCancel = new QPushButton(tr("Cancel"), this);
    auto* btnInsert = new QPushButton(tr("Insert"), this);
    btnInsert->setDefault(true);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnInsert);
    root->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnInsert, &QPushButton::clicked, this, &MatrixInsertDialog::insertAndClose);
    // QAbstractSpinBox::lineEdit() is protected; its internal QLineEdit
    // (the actual focus proxy that consumes Return) is reachable via
    // findChild() instead.
    if (auto* le = m_rowsSpin->findChild<QLineEdit*>())
        connect(le, &QLineEdit::returnPressed, this, &MatrixInsertDialog::insertAndClose);
    if (auto* le = m_colsSpin->findChild<QLineEdit*>())
        connect(le, &QLineEdit::returnPressed, this, &MatrixInsertDialog::insertAndClose);
    connect(m_rowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int rows) {
        m_gridSelector->setCommittedSize(rows, m_colsSpin->value());
        updatePreview();
    });
    connect(m_colsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int cols) {
        m_gridSelector->setCommittedSize(m_rowsSpin->value(), cols);
        updatePreview();
    });

    // Size the dialog to the 4x2 Matrix Type grid (this dialog's widest
    // fixed-size content), plus normal side padding -- rather than an
    // arbitrary fixed width.
    setFixedWidth(kContentWidth + 2 * kDialogSideMargin);
    refreshPanelTheme();
    selectType(defaultTypeIndex());
    updatePreview();
}

void MatrixInsertDialog::setEditor(MarkdownEditor* editor)
{
    m_editor = editor;
}

void MatrixInsertDialog::setDarkMode(bool dark)
{
    m_darkMode = dark;
    applyTitleBarTheme();
    refreshTypeIcons();
    refreshPanelTheme();
    if (m_gridSelector)
        m_gridSelector->setDarkMode(dark);
    if (m_preview)
        m_preview->setDarkMode(dark);
}

void MatrixInsertDialog::openDialog()
{
    applyTitleBarTheme();
    refreshTypeIcons();
    show();
    raise();
    activateWindow();
}

void MatrixInsertDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
    refreshTypeIcons();
    refreshPanelTheme();
    // The size selector's geometry/mask were computed at construction time,
    // while the dialog (and this whole widget tree) was still hidden.
    // Re-derive them now, against the dialog's real on-screen geometry,
    // using the exact same paths a completed drag commits through -- so the
    // picker never depends on a mouse interaction to reach its correct
    // state.
    if (m_gridScrollArea)
        m_gridScrollArea->resyncViewportMask();
    if (m_gridSelector)
        m_gridSelector->setCommittedSize(m_rowsSpin->value(), m_colsSpin->value());
    setDarkMode(palette().color(QPalette::Window).lightness() < 128);
    m_focusBeforeOpen = QApplication::focusWidget();
    m_rowsSpin->setFocus(Qt::ShortcutFocusReason);
    m_rowsSpin->selectAll();
}

void MatrixInsertDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    if (m_editor)
        m_editor->restoreKeyboardFocus();
    else if (m_focusBeforeOpen)
        m_focusBeforeOpen->setFocus(Qt::PopupFocusReason);
}

void MatrixInsertDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);

    if (event->type() != QEvent::PaletteChange &&
        event->type() != QEvent::StyleChange)
        return;

    setDarkMode(palette().color(QPalette::Window).lightness() < 128);
}

void MatrixInsertDialog::applyTitleBarTheme()
{
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
}

void MatrixInsertDialog::buildTypeButtons(QGridLayout* grid)
{
    const QColor iconColor = palette().color(QPalette::ButtonText);
    const auto& types = matrixTypes();
    for (int i = 0; i < types.size(); ++i) {
        const MatrixTypeInfo& info = types.at(i);
        auto* btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("matrixTypeButton"));
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setToolTip(info.label);
        btn->setFixedSize(kTypeButtonW, kTypeButtonH);
        btn->setIconSize(QSize(kTypeIconSize, kTypeIconSize));
        btn->setIcon(IconHelper::generatedMathSvgIcon(info.iconId, iconColor));
        connect(btn, &QToolButton::toggled, this, [this, i](bool checked) {
            if (checked)
                selectType(i);
        });
        // Fixed 4 columns x 2 rows, left-to-right then top-to-bottom -- tab
        // order still follows creation order above, matching that same
        // reading order regardless of grid placement.
        grid->addWidget(btn, i / kTypeGridCols, i % kTypeGridCols);
        m_typeButtons.append(btn);
    }
}

void MatrixInsertDialog::refreshTypeIcons()
{
    const QColor iconColor = palette().color(QPalette::ButtonText);
    const auto& types = matrixTypes();
    for (int i = 0; i < m_typeButtons.size() && i < types.size(); ++i)
        m_typeButtons.at(i)->setIcon(IconHelper::generatedMathSvgIcon(types.at(i).iconId, iconColor));
}

void MatrixInsertDialog::refreshPanelTheme()
{
    const QColor bg = m_darkMode ? DarkTheme::color(DarkTheme::Editor) : QColor(0xff, 0xff, 0xff);
    const QColor border = m_darkMode ? DarkTheme::color(DarkTheme::Border) : QColor(0xd4, 0xd4, 0xd4);
    if (m_gridScrollArea)
        m_gridScrollArea->setPanelColors(bg, border);
    if (m_previewFrame) {
        m_previewFrame->setStyleSheet(QStringLiteral(
            "QFrame#matrixPreviewFrame { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(bg.name(), border.name()));
    }
}

void MatrixInsertDialog::selectType(int index)
{
    if (index < 0 || index >= m_typeButtons.size())
        return;
    m_typeIndex = index;
    if (!m_typeButtons.at(index)->isChecked())
        m_typeButtons.at(index)->setChecked(true);
    updatePreview();
}

void MatrixInsertDialog::updatePreview()
{
    if (!m_preview)
        return;

    const int rows = qMax(1, m_rowsSpin->value());
    const int cols = qMax(1, m_colsSpin->value());
    const QString env = matrixTypes().at(m_typeIndex).env;

    // The exact same LaTeX MarkdownEditor::insertMatrix() will insert, so the
    // Preview can never diverge from what actually ends up in the document --
    // the only difference is stripping the "«»" tab-stop placeholder markers,
    // which insertMathSnippet() itself consumes rather than inserting.
    QString matrixLatex = MarkdownEditor::matrixLatexBody(rows, cols, env);
    matrixLatex.remove(QChar(0x00AB));
    matrixLatex.remove(QChar(0x00BB));

    // KaTeX itself generates and scales the delimiters from the matrix
    // height -- no manually drawn braces/brackets and no container scaling.
    m_preview->setBodyHtml(QStringLiteral("<div class=\"matrix-insert-preview\">$$%1$$</div>").arg(matrixLatex));
    m_preview->ensureInitialized();
}

void MatrixInsertDialog::insertAndClose()
{
    if (!m_editor) {
        reject();
        return;
    }
    const int rows = m_rowsSpin->value();
    const int cols = m_colsSpin->value();
    const QString env = matrixTypes().at(m_typeIndex).env;
    m_editor->insertMatrix(rows, cols, env);
    accept();
}
