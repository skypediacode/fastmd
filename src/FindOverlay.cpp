#include "FindOverlay.h"
#include "FindController.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "IconHelper.h"

#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QShortcut>
#include <QKeyEvent>
#include <QHideEvent>
#include <QPainter>
#include <QStyleOption>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QStyle>
#include <QTextCursor>
#include <QSignalBlocker>

#include <initializer_list>

namespace {

constexpr int kOverlayWidth   = 420;
constexpr int kRowHeight      = 28;
constexpr int kButtonSize     = 24;
constexpr int kIconPx         = 16;
constexpr int kCountWidth     = 62;
constexpr int kChevronWidth   = 18;
constexpr int kChevronIconPx  = 16;
constexpr int kRightInset     = 16;
constexpr int kTopInset       = 10;
// The toolbar and scrollbar are drawn by the runtime's PDF viewer in a frame no
// script can measure. Both are CSS px, which map 1:1 to Qt's logical px; the
// track measures 15.3 at 150%, so it is rounded up to a whole logical px.
constexpr int kPdfToolbarHeight   = 40;
constexpr int kPdfScrollbarWidth  = 16;
constexpr int kPdfOverlayWidth    = 320;
// The gap the Markdown overlay keeps from the editor's scrollbar.
constexpr int kPdfScrollbarGap    = 6;
constexpr int kPdfRightInset      = kPdfScrollbarWidth + kPdfScrollbarGap;
constexpr int kTightGap       = 2;   // between adjacent action buttons
constexpr int kGroupGap       = 4;   // between button groups
// The overlay's 1px QSS border is painted inside the widget rect and a plain
// QWidget's layout does not reserve room for it, so every margin has to carry
// it explicitly -- otherwise the visible padding is one pixel short on the
// sides that have a border and the box reads as off-center.
constexpr int kBorder         = 1;
constexpr int kInnerPad       = 6;   // visible border -> first/last control
constexpr int kOuterMargin    = kInnerPad + kBorder;
constexpr int kRowGap         = 6;   // find row -> replace row

// Verified codepoints (see IconHelper usages elsewhere in the repo).
constexpr ushort kIconChevronRight = 0xE5CC; // chevron_right
constexpr ushort kIconExpandMore   = 0xE5CF; // expand_more
constexpr ushort kIconExpandLess   = 0xE5CE; // expand_less
constexpr ushort kIconSelectAll    = 0xE162; // select_all
constexpr ushort kIconClose        = 0xE5CD; // close
constexpr ushort kIconFindReplace  = 0xE881; // find_replace
constexpr ushort kIconDoneAll      = 0xE877; // done_all

void repolish(QWidget* widget)
{
    if (!widget || !widget->style())
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

FindOverlay::FindOverlay(FindController* controller, QWidget* parent)
    : QWidget(parent), m_controller(controller)
{
    setObjectName(QStringLiteral("findOverlay"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    // An opaque window would show its square corners outside the QSS radius.
    setAttribute(Qt::WA_TranslucentBackground, true);

    buildUi();
    applyFixedMetrics();
    updateIcons();

    {
        const FindController::Options opts = m_controller->options();
        const QSignalBlocker b1(m_matchCaseButton);
        const QSignalBlocker b2(m_wholeWordButton);
        const QSignalBlocker b3(m_regexButton);
        const QSignalBlocker b4(m_inSelectionButton);
        const QSignalBlocker b5(m_preserveCaseButton);
        m_matchCaseButton->setChecked(opts.matchCase);
        m_wholeWordButton->setChecked(opts.wholeWord);
        m_regexButton->setChecked(opts.regex);
        m_inSelectionButton->setChecked(opts.inSelection);
        m_preserveCaseButton->setChecked(opts.preserveCase);
    }

    connect(m_controller, &FindController::matchesChanged, this, [this]() {
        updateCountLabel();
        updateInvalidState();
        updateEnabledStates();
    });

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(150);
    connect(m_refreshDebounce, &QTimer::timeout, this, &FindOverlay::flushPendingTerm);

    updateCountLabel();
    updateEnabledStates();

    if (parent)
        parent->installEventFilter(this);

    const auto addShortcut = [this](QKeySequence seq, auto handler) {
        auto* shortcut = new QShortcut(seq, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    addShortcut(QKeySequence(Qt::Key_Escape), [this] { closeOverlay(); });
    addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return), [this] { doReplaceAll(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_C), [this] { if (!m_pdfMode) m_matchCaseButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_W), [this] { if (!m_pdfMode) m_wholeWordButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_R), [this] { if (!m_pdfMode) m_regexButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_L), [this] { if (!m_pdfMode) m_inSelectionButton->toggle(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_P), [this] { if (!m_pdfMode) m_preserveCaseButton->toggle(); });
}

QToolButton* FindOverlay::makeButton(QWidget* parent, const QString& objectName, bool checkable)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setIconSize(QSize(kIconPx, kIconPx));
    return button;
}

QWidget* FindOverlay::makeSeparator(QWidget* parent)
{
    auto* line = new QWidget(parent);
    line->setObjectName(QStringLiteral("findOverlaySeparator"));
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setFixedWidth(1);
    line->setFixedHeight(16);
    return line;
}

void FindOverlay::buildUi()
{
    // One grid holds everything, and the navigation buttons share their grid
    // columns with the replace buttons: that is what guarantees Replace sits
    // under the up chevron and Replace All under the down chevron. Group gaps
    // come from empty spacer columns rather than from per-cluster layouts, so
    // both rows keep one horizontal rhythm.
    //
    //   col   0        1    2       3    4     5     6      7    8    9
    //         chevron  gap  inputs  gap  prev  next  inSel  gap  sep  close
    //
    // Vertically the same idea: vertical *spacing* is zero and the gap between
    // the two input rows is an explicit spacer row whose height is toggled with
    // the replace state. A hidden row plus layout spacing would leave the
    // collapsed overlay's height depending on how the grid treats an empty row,
    // which is exactly what makes the padding above and below the Find field
    // come out unequal.
    //
    //   row 0  find row / prev / next / in-selection / close
    //   row 1  gap (kRowGap when expanded, 0 when collapsed)
    //   row 2  replace row / replace / replace all
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(kOuterMargin, kOuterMargin, kOuterMargin, kOuterMargin);
    m_grid->setHorizontalSpacing(kTightGap);
    m_grid->setVerticalSpacing(0);

    m_chevronButton = makeButton(this, QStringLiteral("findOverlayChevron"), true);
    // One adaptive control, VS Code style: the toggle spans every row that is
    // showing -- the find row alone when collapsed, both rows and the gap
    // between them when expanded -- so its hover/pressed background covers the
    // whole left column and its glyph centres on the button as a whole. The
    // height is driven from updateFixedHeight() rather than from a size policy,
    // so it is exactly the span it occupies and never negotiates with the grid.
    m_chevronButton->setFixedWidth(kChevronWidth);
    m_chevronButton->setIconSize(QSize(kChevronIconPx, kChevronIconPx));
    m_chevronButton->setToolTip(tr("Toggle Replace (Ctrl+H)"));
    connect(m_chevronButton, &QToolButton::toggled, this, &FindOverlay::setReplaceExpanded);
    m_grid->addWidget(m_chevronButton, 0, 0, 3, 1);

    // ---- find row ---------------------------------------------------------
    m_findRow = new QWidget(this);
    m_findRow->setObjectName(QStringLiteral("findOverlayInputRow"));
    m_findRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* findRowLayout = new QHBoxLayout(m_findRow);
    // Same border-aware reasoning as the overlay's own margins: the row paints a
    // 1px border the layout does not know about, so the text and the option
    // buttons need it back on every side to sit centred inside the field.
    findRowLayout->setContentsMargins(kBorder + 6, kBorder, kBorder + 3, kBorder);
    findRowLayout->setSpacing(kTightGap);

    m_findEdit = new QLineEdit(m_findRow);
    m_findEdit->setObjectName(QStringLiteral("findOverlayEdit"));
    m_findEdit->setFrame(false);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_findEdit->installEventFilter(this);
    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        m_refreshDebounce->start();
    });
    findRowLayout->addWidget(m_findEdit, 1);

    // Count sits inside the field (VS Code does the same) at a fixed width, so
    // "No results" / "12 of 340" / "" never shift the option buttons around.
    m_countLabel = new QLabel(m_findRow);
    m_countLabel->setObjectName(QStringLiteral("findOverlayCount"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_countLabel->setFixedWidth(kCountWidth);
    findRowLayout->addWidget(m_countLabel);

    findRowLayout->addSpacing(kGroupGap - kTightGap);
    m_optionsSeparator = makeSeparator(m_findRow);
    findRowLayout->addWidget(m_optionsSeparator, 0, Qt::AlignVCenter);
    findRowLayout->addSpacing(kGroupGap - kTightGap);

    m_matchCaseButton = makeButton(m_findRow, QStringLiteral("findOverlayOption"), true);
    m_wholeWordButton = makeButton(m_findRow, QStringLiteral("findOverlayOption"), true);
    m_regexButton     = makeButton(m_findRow, QStringLiteral("findOverlayOption"), true);
    m_matchCaseButton->setToolTip(tr("Match Case (Alt+C)"));
    m_wholeWordButton->setToolTip(tr("Match Whole Word (Alt+W)"));
    m_regexButton->setToolTip(tr("Use Regular Expression (Alt+R)"));
    for (QToolButton* b : { m_matchCaseButton, m_wholeWordButton, m_regexButton }) {
        findRowLayout->addWidget(b, 0, Qt::AlignVCenter);
        connect(b, &QToolButton::toggled, this, &FindOverlay::pushOptions);
    }
    m_grid->addWidget(m_findRow, 0, 2);

    // ---- replace row ------------------------------------------------------
    m_replaceRow = new QWidget(this);
    m_replaceRow->setObjectName(QStringLiteral("findOverlayInputRow"));
    m_replaceRow->setAttribute(Qt::WA_StyledBackground, true);
    auto* replaceRowLayout = new QHBoxLayout(m_replaceRow);
    replaceRowLayout->setContentsMargins(kBorder + 6, kBorder, kBorder + 3, kBorder);
    replaceRowLayout->setSpacing(kTightGap);

    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setObjectName(QStringLiteral("findOverlayEdit"));
    m_replaceEdit->setFrame(false);
    m_replaceEdit->setPlaceholderText(tr("Replace"));
    m_replaceEdit->installEventFilter(this);
    replaceRowLayout->addWidget(m_replaceEdit, 1);

    replaceRowLayout->addSpacing(kGroupGap - kTightGap);
    replaceRowLayout->addWidget(makeSeparator(m_replaceRow), 0, Qt::AlignVCenter);
    replaceRowLayout->addSpacing(kGroupGap - kTightGap);

    m_preserveCaseButton = makeButton(m_replaceRow, QStringLiteral("findOverlayOption"), true);
    m_preserveCaseButton->setToolTip(tr("Preserve Case (Alt+P)"));
    replaceRowLayout->addWidget(m_preserveCaseButton, 0, Qt::AlignVCenter);
    connect(m_preserveCaseButton, &QToolButton::toggled, this, &FindOverlay::pushOptions);
    m_grid->addWidget(m_replaceRow, 2, 2);

    // ---- action buttons ---------------------------------------------------
    m_prevButton        = makeButton(this, QStringLiteral("findOverlayAction"), false);
    m_nextButton        = makeButton(this, QStringLiteral("findOverlayAction"), false);
    m_inSelectionButton = makeButton(this, QStringLiteral("findOverlayAction"), true);
    m_closeButton       = makeButton(this, QStringLiteral("findOverlayAction"), false);
    m_replaceOneButton  = makeButton(this, QStringLiteral("findOverlayAction"), false);
    m_replaceAllButton  = makeButton(this, QStringLiteral("findOverlayAction"), false);
    m_prevButton->setToolTip(tr("Previous Match (Shift+Enter)"));
    m_nextButton->setToolTip(tr("Next Match (Enter)"));
    m_inSelectionButton->setToolTip(tr("Find in Selection (Alt+L)"));
    m_closeButton->setToolTip(tr("Close (Esc)"));
    m_replaceOneButton->setToolTip(tr("Replace (Enter)"));
    m_replaceAllButton->setToolTip(tr("Replace All (Ctrl+Alt+Enter)"));

    m_grid->addWidget(m_prevButton,        0, 4, Qt::AlignVCenter);
    m_grid->addWidget(m_nextButton,        0, 5, Qt::AlignVCenter);
    m_grid->addWidget(m_inSelectionButton, 0, 6, Qt::AlignVCenter);
    m_grid->addWidget(m_replaceOneButton,  2, 4, Qt::AlignVCenter);
    m_grid->addWidget(m_replaceAllButton,  2, 5, Qt::AlignVCenter);

    m_navSeparator = makeSeparator(this);
    m_grid->addWidget(m_navSeparator, 0, 8, Qt::AlignVCenter);
    m_grid->addWidget(m_closeButton,  0, 9, Qt::AlignVCenter);

    // Column 1 is an empty spacer that makes the chevron's gap to the input row
    // equal to its gap to the overlay's visible border, so the hover rect sits
    // evenly between the two. The grid's own spacing contributes on both sides.
    m_grid->setColumnMinimumWidth(1, kInnerPad - 2 * kTightGap);
    m_grid->setColumnMinimumWidth(3, kGroupGap - kTightGap);
    m_grid->setColumnMinimumWidth(7, kGroupGap - kTightGap);
    m_grid->setColumnStretch(2, 1);
    m_grid->setRowMinimumHeight(0, kRowHeight);

    connect(m_prevButton, &QToolButton::clicked, this, &FindOverlay::doFindPrevious);
    connect(m_nextButton, &QToolButton::clicked, this, &FindOverlay::doFindNext);
    connect(m_closeButton, &QToolButton::clicked, this, &FindOverlay::closeOverlay);
    connect(m_replaceOneButton, &QToolButton::clicked, this, &FindOverlay::doReplaceOne);
    connect(m_replaceAllButton, &QToolButton::clicked, this, &FindOverlay::doReplaceAll);
    connect(m_inSelectionButton, &QToolButton::toggled, this, [this](bool checked) {
        // The scope was captured when the overlay opened; only fall back to the
        // live cursor when there is nothing remembered, since after a find the
        // editor selection *is* the current match.
        if (checked && !m_controller->hasSelectionScope())
            m_controller->captureSelectionScope();
        pushOptions();
    });

    m_replaceRow->setVisible(false);
    m_replaceOneButton->setVisible(false);
    m_replaceAllButton->setVisible(false);
}

void FindOverlay::applyFixedMetrics()
{
    // qApp->setStyleSheet() in applyTheme() repolishes every widget and drops
    // programmatically set fixed sizes, so these are re-asserted on each theme
    // change (see MainWindow::applyTheme's repolish caveat).
    setFixedWidth(m_pdfMode ? kPdfOverlayWidth : kOverlayWidth);
    m_findRow->setFixedHeight(kRowHeight);
    m_replaceRow->setFixedHeight(kRowHeight);
    m_chevronButton->setFixedWidth(kChevronWidth);
    m_chevronButton->setIconSize(QSize(kChevronIconPx, kChevronIconPx));
    m_countLabel->setFixedWidth(kCountWidth);
    for (QToolButton* b : { m_matchCaseButton, m_wholeWordButton, m_regexButton,
                            m_preserveCaseButton, m_inSelectionButton, m_prevButton,
                            m_nextButton, m_closeButton, m_replaceOneButton, m_replaceAllButton }) {
        b->setFixedSize(kButtonSize, kButtonSize);
        b->setIconSize(QSize(kIconPx, kIconPx));
    }
    updateFixedHeight();
}

void FindOverlay::updateFixedHeight()
{
    // Stated outright rather than taken from the grid's size hint: the hint
    // depends on how the grid accounts for the hidden replace row, and any
    // mismatch there lands entirely below the Find field.
    const int contentHeight = m_replaceExpanded ? kRowHeight + kRowGap + kRowHeight : kRowHeight;
    m_grid->setRowMinimumHeight(1, m_replaceExpanded ? kRowGap : 0);
    // The toggle spans all three rows, so giving it exactly the content height
    // makes it fill that span precisely: the grid has no slack to distribute and
    // no reason to grow a row to fit it, in either state.
    m_chevronButton->setFixedHeight(contentHeight);
    m_grid->invalidate();
    setFixedHeight(2 * kOuterMargin + contentHeight);
}

void FindOverlay::updateIcons()
{
    const QColor color = m_dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x42, 0x42, 0x42);

    m_chevronButton->setIcon(IconHelper::materialIconTight(
        QChar(m_replaceExpanded ? kIconExpandMore : kIconChevronRight), color, kChevronIconPx, 1));
    // tightCenter: Qt::AlignCenter reserves descender room these labels do not
    // use, which leaves them sitting visibly low next to the Material glyphs.
    m_matchCaseButton->setIcon(IconHelper::textIcon(QStringLiteral("Aa"), color, kIconPx, true, 0, QStringLiteral("ab")));
    m_wholeWordButton->setIcon(IconHelper::textIcon(QStringLiteral("ab"), color, kIconPx, true));
    m_regexButton->setIcon(IconHelper::textIcon(QStringLiteral(".*"), color, kIconPx, true, 0, QStringLiteral("ab")));
    m_preserveCaseButton->setIcon(IconHelper::textIcon(QStringLiteral("AB"), color, kIconPx, true));
    // materialIconTight: centered on each glyph's ink, so the chevrons, the
    // replace glyphs and the close cross share one centerline and one vertical
    // axis per column instead of drifting with each glyph's font metrics.
    m_prevButton->setIcon(IconHelper::materialIconTight(QChar(kIconExpandLess), color, kIconPx, 1));
    m_nextButton->setIcon(IconHelper::materialIconTight(QChar(kIconExpandMore), color, kIconPx, 1));
    m_inSelectionButton->setIcon(IconHelper::materialIconTight(QChar(kIconSelectAll), color, kIconPx));
    m_closeButton->setIcon(IconHelper::materialIconTight(QChar(kIconClose), color, kIconPx));
    m_replaceOneButton->setIcon(IconHelper::materialIconTight(QChar(kIconFindReplace), color, kIconPx));
    m_replaceAllButton->setIcon(IconHelper::materialIconTight(QChar(kIconDoneAll), color, kIconPx));
}

void FindOverlay::setDarkMode(bool dark)
{
    m_dark = dark;
    updateIcons();
    applyFixedMetrics();
    if (isVisible())
        anchorToParent();
}

void FindOverlay::setAnchorWidget(QWidget* widget)
{
    if (m_anchorWidget == widget)
        return;
    if (m_anchorWidget)
        m_anchorWidget->removeEventFilter(this);
    m_anchorWidget = widget;
    if (m_anchorWidget)
        m_anchorWidget->installEventFilter(this);
    if (isVisible())
        anchorToParent();
}

void FindOverlay::setPdfMode(bool pdfMode)
{
    if (m_pdfMode == pdfMode)
        return;
    m_pdfMode = pdfMode;
    if (pdfMode)
        setReplaceExpanded(false);
    for (QWidget* control : std::initializer_list<QWidget*>{
             m_chevronButton, m_optionsSeparator, m_matchCaseButton, m_wholeWordButton,
             m_regexButton, m_inSelectionButton })
        control->setVisible(!pdfMode);
    m_grid->setColumnMinimumWidth(1, pdfMode ? 0 : kInnerPad - 2 * kTightGap);
    applyFixedMetrics();
    if (isVisible())
        anchorToParent();
}

void FindOverlay::setReplaceExpanded(bool expanded)
{
    if (m_replaceExpanded == expanded && m_chevronButton->isChecked() == expanded)
        return;
    m_replaceExpanded = expanded;
    m_replaceRow->setVisible(expanded);
    m_replaceOneButton->setVisible(expanded);
    m_replaceAllButton->setVisible(expanded);
    if (m_chevronButton->isChecked() != expanded) {
        const QSignalBlocker blocker(m_chevronButton);
        m_chevronButton->setChecked(expanded);
    }
    updateIcons();
    updateFixedHeight();
    if (isVisible())
        anchorToParent();
}

void FindOverlay::pushOptions()
{
    FindController::Options opts;
    opts.matchCase    = m_matchCaseButton->isChecked();
    opts.wholeWord    = m_wholeWordButton->isChecked();
    opts.regex        = m_regexButton->isChecked();
    opts.inSelection  = m_inSelectionButton->isChecked();
    opts.preserveCase = m_preserveCaseButton->isChecked();
    m_controller->setOptions(opts);
}

void FindOverlay::focusFindField()
{
    // Focus set immediately after show() is lost to the show event, so it is
    // deferred one event-loop turn (same technique the old dialog used).
    QTimer::singleShot(0, this, [this]() {
        m_findEdit->setFocus(Qt::ShortcutFocusReason);
        m_findEdit->selectAll();
    });
}

void FindOverlay::seedFromSelection()
{
    MarkdownEditor* editor = m_controller->editor();
    if (!editor)
        return;

    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        m_controller->clearSelectionScope();
        if (m_inSelectionButton->isChecked()) {
            const QSignalBlocker blocker(m_inSelectionButton);
            m_inSelectionButton->setChecked(false);
            pushOptions();
        }
        return;
    }

    QString selectedText = cursor.selectedText();
    selectedText.replace(QChar(0x2029), QLatin1Char('\n'));

    // Multi-line selection means "search inside this", not "search for this".
    if (selectedText.contains(QLatin1Char('\n'))) {
        m_controller->captureSelectionScope();
        const QSignalBlocker blocker(m_inSelectionButton);
        m_inSelectionButton->setChecked(true);
        pushOptions();
        return;
    }

    m_controller->captureSelectionScope();

    // Skip re-setting identical text: right after a successful find the editor
    // selection *is* the match just found, so re-seeding would reset the
    // search on every reopen and never advance past the first hit.
    if (selectedText == m_findEdit->text())
        return;
    const QSignalBlocker blocker(m_findEdit);
    m_findEdit->setText(selectedText);
    m_refreshDebounce->stop();
    m_controller->setTerm(selectedText);
}

void FindOverlay::openInternal(bool expandReplace)
{
    const bool wasVisible = isVisible();

    // A closed overlay has no mode to preserve: Ctrl+F must reopen as the
    // single-row Find panel even if the last session was Ctrl+H's Replace view.
    // While it is already open, the chevron state is the user's own choice and
    // Ctrl+F only refocuses (VS Code behaves the same way).
    if (expandReplace && !m_pdfMode)
        setReplaceExpanded(true);
    else if (!wasVisible)
        setReplaceExpanded(false);
    seedFromSelection();
    m_controller->setActive(true);

    if (wasVisible) {
        anchorToParent();
        raise();
        activateWindow();
        focusFindField();
        return;
    }

    updateFixedHeight();
    anchorToParent();
    show();
    raise();
    activateWindow();
    animateShow();
    focusFindField();
}

void FindOverlay::openFind()
{
    openInternal(false);
}

void FindOverlay::openReplace()
{
    openInternal(true);
}

void FindOverlay::closeOverlay()
{
    if (!isVisible())
        return;
    m_refreshDebounce->stop();
    m_controller->setActive(false);
    animateHide();
}

void FindOverlay::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // Deliberately does not deactivate the controller: a Qt::Tool window is
    // also hidden when its parent minimizes, and the search must survive that
    // and come back with its highlights. closeOverlay() owns deactivation.
    // Only pull focus back when FastMD itself is still the active application;
    // a hide caused by the parent minimizing must not steal focus from
    // whatever the user switched to.
    if (!QApplication::activeWindow())
        return;
    if (MarkdownEditor* editor = m_controller->editor())
        editor->restoreKeyboardFocus();
    else if (PreviewWidget* preview = m_controller->pdfPreview())
        preview->focusContent();
}

// A translucent top-level gets no automatic background, so the QSS rounded fill
// and border are painted here.
void FindOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void FindOverlay::anchorToParent()
{
    QWidget* anchor = m_anchorWidget ? m_anchorWidget.data() : parentWidget();
    if (!anchor || !anchor->isVisible())
        anchor = parentWidget();
    if (!anchor)
        return;

    QScreen* screen = anchor->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect screenRect = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // Only the anchor's own top-left may be mapped to global coordinates: with
    // per-screen scale factors, mapToGlobal() resolves the scale/origin from the
    // screen the *mapped point* lands on, so mapping a point outside the widget
    // (e.g. its top-right corner at local x == width()) can be attributed to the
    // neighbouring screen and come back with that screen's origin applied. On a
    // secondary monitor whose logical origin differs from the primary's, that
    // produced a corner tens of pixels above the editor -- over the toolbar.
    // Derive the top-right arithmetically instead; it cannot drift.
    const QPoint anchorTopLeft = anchor->mapToGlobal(QPoint(0, 0));
    const QPoint anchorTopRight = anchorTopLeft + QPoint(anchor->width(), 0);

    int targetX = anchorTopRight.x() - width() - (m_pdfMode ? kPdfRightInset : kRightInset);
    int targetY = anchorTopRight.y() + kTopInset + (m_pdfMode ? kPdfToolbarHeight : 0);

    // Never slide off the left edge of the pane being searched.
    targetX = qMax(targetX, anchorTopLeft.x());

    targetX = qBound(screenRect.left(), targetX, qMax(screenRect.left(), screenRect.right() - width()));
    targetY = qBound(screenRect.top(), targetY, qMax(screenRect.top(), screenRect.bottom() - height()));

    move(targetX, targetY);
}

bool FindOverlay::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == parentWidget()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
        case QEvent::Show:
            if (isVisible())
                anchorToParent();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(obj, event);
    }

    if (obj == m_anchorWidget.data()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
            if (isVisible())
                anchorToParent();
            break;
        default:
            break;
        }
        return QWidget::eventFilter(obj, event);
    }

    if (obj == m_findEdit || obj == m_replaceEdit) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            QTimer::singleShot(0, this, &FindOverlay::updateFocusState);
        } else if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                if (obj == m_findEdit) {
                    if (!m_pdfMode && m_controller->options().regex && (ke->modifiers() & Qt::ControlModifier)) {
                        m_findEdit->insert(QStringLiteral("\n"));
                        return true;
                    }
                    if (ke->modifiers() & Qt::ShiftModifier)
                        doFindPrevious();
                    else
                        doFindNext();
                } else {
                    doReplaceOne();
                }
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void FindOverlay::flushPendingTerm()
{
    m_refreshDebounce->stop();
    m_controller->setTerm(m_findEdit->text());
}

void FindOverlay::updateCountLabel()
{
    const QString term = m_controller->term();
    if (term.isEmpty()) {
        m_countLabel->clear();
        return;
    }
    if (m_controller->options().regex && !m_controller->regexValid()) {
        m_countLabel->setText(tr("Bad pattern"));
        return;
    }
    const int count = m_controller->matchCount();
    if (count == 0) {
        m_countLabel->setText(tr("No results"));
        return;
    }
    if (m_controller->capped()) {
        m_countLabel->setText(tr("%1+").arg(FindController::kMaxMatches));
        return;
    }
    const int idx = m_controller->currentIndex();
    m_countLabel->setText(tr("%1 of %2").arg(idx >= 0 ? idx + 1 : 0).arg(count));
}

void FindOverlay::updateInvalidState()
{
    const bool invalid = m_controller->options().regex && !m_controller->term().isEmpty()
        && !m_controller->regexValid();
    if (m_findRow->property("invalid").toBool() != invalid) {
        m_findRow->setProperty("invalid", invalid);
        repolish(m_findRow);
    }
    m_findEdit->setToolTip(invalid ? m_controller->regexError() : QString());
}

void FindOverlay::updateFocusState()
{
    const auto sync = [](QWidget* row, QLineEdit* edit) {
        const bool focused = edit->hasFocus();
        if (row->property("focused").toBool() == focused)
            return;
        row->setProperty("focused", focused);
        repolish(row);
    };
    sync(m_findRow, m_findEdit);
    sync(m_replaceRow, m_replaceEdit);
}

void FindOverlay::updateEnabledStates()
{
    const bool hasMatches = m_controller->matchCount() > 0;
    m_prevButton->setEnabled(hasMatches);
    m_nextButton->setEnabled(hasMatches);
    const bool canReplace = hasMatches && m_controller->editor() != nullptr;
    m_replaceOneButton->setEnabled(canReplace);
    m_replaceAllButton->setEnabled(canReplace);
}

void FindOverlay::doFindNext()
{
    flushPendingTerm();
    m_controller->findNext();
}

void FindOverlay::doFindPrevious()
{
    flushPendingTerm();
    m_controller->findPrevious();
}

void FindOverlay::doReplaceOne()
{
    flushPendingTerm();
    m_controller->replaceCurrent(m_replaceEdit->text());
}

void FindOverlay::doReplaceAll()
{
    flushPendingTerm();
    m_controller->replaceAll(m_replaceEdit->text());
}

void FindOverlay::animateShow()
{
    if (!m_showAnimGroup) {
        // Cached rather than rebuilt per open: per-invocation construction of
        // these is a documented source of visible stutter in this codebase.
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity", this);
        m_geometryAnim = new QPropertyAnimation(this, "geometry", this);
        m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_showAnimGroup = new QParallelAnimationGroup(this);
        m_showAnimGroup->addAnimation(m_opacityAnim);
        m_showAnimGroup->addAnimation(m_geometryAnim);
        connect(m_showAnimGroup, &QParallelAnimationGroup::finished, this, [this]() {
            if (!m_closing)
                return;
            m_closing = false;
            hide();
            setWindowOpacity(1.0);
        });
    }
    m_showAnimGroup->stop();
    m_closing = false;

    const QRect target = geometry();
    const QRect start = target.translated(0, -10);
    setWindowOpacity(0.0);
    setGeometry(start);

    m_opacityAnim->setDuration(120);
    m_opacityAnim->setStartValue(0.0);
    m_opacityAnim->setEndValue(1.0);

    m_geometryAnim->setDuration(140);
    m_geometryAnim->setStartValue(start);
    m_geometryAnim->setEndValue(target);

    m_showAnimGroup->start();
}

void FindOverlay::animateHide()
{
    if (!m_showAnimGroup) {
        hide();
        return;
    }
    m_showAnimGroup->stop();
    m_closing = true;

    m_opacityAnim->setDuration(90);
    m_opacityAnim->setStartValue(windowOpacity());
    m_opacityAnim->setEndValue(0.0);

    m_geometryAnim->setDuration(90);
    m_geometryAnim->setStartValue(geometry());
    m_geometryAnim->setEndValue(geometry());

    m_showAnimGroup->start();
}
