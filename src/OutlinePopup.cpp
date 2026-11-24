#include "OutlinePopup.h"
#include "IconHelper.h"
#include "MarkdownEditor.h"
#include "DocumentModel.h"
#include "OutlineModel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QAbstractButton>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPalette>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QHeaderView>
#include <QEvent>
#include <QDateTime>
#include <QTimer>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

OutlinePopup::OutlinePopup(QWidget* parent) : QWidget(parent) {
    // Deliberately Qt::Tool in *both* pinned and unpinned modes. Qt::Popup would
    // give free click-outside dismissal, but switching between the two window
    // types on a visible widget forces Qt to destroy and recreate the native
    // window, which unavoidably flickers. Unpinned dismissal is done in event()
    // instead, so the window type never changes and the window is never
    // recreated.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    // An opaque window would show its square corners outside the QSS radius.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(300, 400);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    updateStylesheet(dark);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search headings..."));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &OutlinePopup::filterTree);
    QTimer::singleShot(0, this, &OutlinePopup::positionSearchClearButton);

    m_pinButton = new QToolButton(this);
    m_pinButton->setObjectName(QStringLiteral("outlinePinButton"));
    m_pinButton->setCheckable(true);
    m_pinButton->setFixedSize(26, 26);
    m_pinButton->setIconSize(QSize(18, 18));
    m_pinButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_pinButton->setAutoRaise(true);
    m_pinButton->setToolTip(tr("Keep outline open"));
    connect(m_pinButton, &QToolButton::toggled, this, [this](bool pinned) {
        // Refresh the icon/tooltip *before* the window-mode switch: setPinned()
        // snapshots the popup to mask the native window recreation, so a stale
        // pin icon would be baked into that snapshot and then visibly jump.
        updatePinButton();
        setPinned(pinned);
    });

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePopup::onItemClicked);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    headerLayout->addWidget(m_search, 1);
    headerLayout->addWidget(m_pinButton);

    layout->addLayout(headerLayout);
    layout->addWidget(m_tree);
    updatePinButton();
}

void OutlinePopup::showAt(const QPoint& targetPos) {
    const QRect targetGeometry(targetPos, size());
    const int flyOffset = 24;
    const QRect startGeometry = targetGeometry.translated(0, flyOffset);

    setWindowOpacity(0.0);
    setGeometry(startGeometry);
    show();
    raise();

    auto* opacityAnim = new QPropertyAnimation(this, "windowOpacity", this);
    opacityAnim->setDuration(160);
    opacityAnim->setStartValue(0.0);
    opacityAnim->setEndValue(1.0);

    auto* geometryAnim = new QPropertyAnimation(this, "geometry", this);
    geometryAnim->setDuration(180);
    geometryAnim->setStartValue(startGeometry);
    geometryAnim->setEndValue(targetGeometry);
    geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto* group = new QParallelAnimationGroup(this);
    group->addAnimation(opacityAnim);
    group->addAnimation(geometryAnim);
    group->start(QAbstractAnimation::DeleteWhenStopped);

    // A Qt::Tool window has to actually be active for the unpinned
    // click-outside dismissal in event() to get its WindowDeactivate, and for
    // the search field to receive typing the way the old Qt::Popup's keyboard
    // grab did.
    activateWindow();
    m_search->setFocus(Qt::PopupFocusReason);
}

void OutlinePopup::resetPinned() {
    if (m_pinButton->isChecked()) {
        m_pinButton->setChecked(false);
    } else {
        setPinned(false);
    }
    updatePinButton();
}

void OutlinePopup::setDarkMode(bool dark) {
    updateStylesheet(dark);
}

void OutlinePopup::updateStylesheet(bool dark) {
    if (dark) {
        setStyleSheet(
            "OutlinePopup { background: #202122; border: 1px solid #2a2b2c; border-radius: 8px; }"
            "QTreeWidget { background: #202122; border: none; outline: none; font-family: 'Segoe UI'; font-size: 13px; color: #bfbfbf; show-decoration-selected: 1; }"
            "QTreeWidget QScrollBar:vertical, QTreeWidget QScrollBar:horizontal { background: #202122; }"
            "QTreeWidget::item { padding: 4px 6px; border-right: 4px solid transparent; }"
            "QTreeWidget::item:hover { background: rgba(255,255,255,0.08); }"
            "QTreeWidget::item:selected { background: #276782; color: #ffffff; }"
            "QTreeWidget::branch { background: transparent; }"
            "QTreeWidget::branch:hover { background: transparent; }"
            "QTreeWidget::branch:selected { background: #276782; }"
            "QTreeWidget::branch:has-children:!leaf { image: none; }"
            "QLineEdit { background: #121314; color: #bfbfbf; border: 1px solid #2a2b2c; border-radius: 4px; padding: 4px 6px; margin: 4px; }"
            "QLineEdit:focus { border-color: #3994bc; }"
            "QLineEdit::clear-button { subcontrol-origin: padding; subcontrol-position: right center; width: 24px; height: 24px; padding: 0px; margin: 0px 1px 0px 0px; }"
            "QToolButton#outlinePinButton { background: transparent; border: 1px solid transparent; border-radius: 4px; min-width: 27px; max-width: 27px; min-height: 27px; max-height: 27px; padding: 0px; margin: 4px 4px 4px 0px; }"
            "QToolButton#outlinePinButton:hover { background: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.12); }"
            "QToolButton#outlinePinButton:pressed { background: rgba(255,255,255,0.16); }"
            "QToolButton#outlinePinButton:checked { background: rgba(0,120,212,0.28); border-color: rgba(0,120,212,0.45); }");
    } else {
        setStyleSheet(
            "OutlinePopup { background: #ffffff; border: 1px solid #c4c4c4; border-radius: 8px; }"
            "QTreeWidget { background: #ffffff; border: none; outline: none; font-family: 'Segoe UI'; font-size: 13px; color: #222222; show-decoration-selected: 1; }"
            "QTreeWidget QScrollBar:vertical, QTreeWidget QScrollBar:horizontal { background: #ffffff; }"
            "QTreeWidget::item { padding: 4px 6px; border-right: 4px solid transparent; }"
            "QTreeWidget::item:hover { background: rgba(0,0,0,0.06); }"
            "QTreeWidget::item:selected { background: #0078d4; color: #ffffff; }"
            "QTreeWidget::branch { background: transparent; }"
            "QTreeWidget::branch:hover { background: transparent; }"
            "QTreeWidget::branch:selected { background: #0078d4; }"
            "QTreeWidget::branch:has-children:!leaf { image: none; }"
            "QLineEdit { background: #ffffff; color: #222222; border: 1px solid #c4c4c4; border-radius: 4px; padding: 4px 6px; margin: 4px; }"
            "QLineEdit:focus { border-color: #0078d4; }"
            "QLineEdit::clear-button { subcontrol-origin: padding; subcontrol-position: right center; width: 24px; height: 24px; padding: 0px; margin: 0px 1px 0px 0px; }"
            "QToolButton#outlinePinButton { background: transparent; border: 1px solid transparent; border-radius: 4px; min-width: 27px; max-width: 27px; min-height: 27px; max-height: 27px; padding: 0px; margin: 4px 4px 4px 0px; }"
            "QToolButton#outlinePinButton:hover { background: rgba(0,0,0,0.08); border-color: rgba(0,0,0,0.12); }"
            "QToolButton#outlinePinButton:pressed { background: rgba(0,0,0,0.14); }"
            "QToolButton#outlinePinButton:checked { background: rgba(0,120,212,0.13); border-color: rgba(0,120,212,0.30); }");
    }
}

void OutlinePopup::setPinned(bool pinned) {
    // Pinning is pure state: it only decides whether event() dismisses the
    // popup when it loses activation. No window flags, no reparenting, no
    // native window recreation -- and therefore nothing to flicker.
    m_pinned = pinned;
}

void OutlinePopup::updatePinButton() {
    m_pinned = m_pinButton->isChecked();
    const QColor iconColor = palette().color(QPalette::ButtonText);
    m_pinButton->setIcon(m_pinned
        ? IconHelper::materialIcon(QChar(0xF10D), iconColor, 18)
        : IconHelper::materialIconOutlined(QChar(0xF10D), iconColor, 18));
    m_pinButton->setToolTip(m_pinned ? tr("Unpin outline") : tr("Keep outline open"));
}

void OutlinePopup::setEditor(MarkdownEditor* editor, DocumentModel* model) {
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    m_model = model;
    
    if (m_editor) {
        connect(m_editor, &MarkdownEditor::textChanged, this, &OutlinePopup::refresh);
        connect(m_editor, &MarkdownEditor::cursorPositionChanged, this, &OutlinePopup::updateHighlight);
    }
    refresh();
}

void OutlinePopup::refresh() {
    if (!m_editor) {
        m_tree->clear();
        return;
    }

    OutlineModel::populate(m_tree, m_editor->toPlainText(), tr("No headings found"));
    OutlineModel::filter(m_tree, m_search->text());
    updateHighlight();
}

void OutlinePopup::filterTree(const QString& text) {
    OutlineModel::filter(m_tree, text);
}

void OutlinePopup::onItemClicked(QTreeWidgetItem* item) {
    if (!m_editor || (item->flags() & Qt::NoItemFlags)) return;
    
    int line = item->data(0, Qt::UserRole).toInt();
    emit navigationRequested();
    QTextCursor cursor(m_editor->document()->findBlockByNumber(line));
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    
    if (!m_pinned) hide();
}

void OutlinePopup::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    setProperty("lastHideTime", QDateTime::currentMSecsSinceEpoch());
    m_search->clear();
    emit popupHidden();
}

// A translucent top-level gets no automatic background, so the QSS rounded
// fill and border are painted here.
void OutlinePopup::paintEvent(QPaintEvent*) {
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

bool OutlinePopup::event(QEvent* event) {
    // Stands in for the click-outside dismissal Qt::Popup used to provide.
    // Anything that takes activation away -- the editor, the preview's own
    // Outline button (a real child HWND owned by WebView2, which Qt's event
    // system never sees), another window, another application -- deactivates
    // this tool window and closes it while unpinned.
    if (event->type() == QEvent::WindowDeactivate && !m_pinned) {
        // Deferred so the popup is not torn down inside its own deactivation
        // handling; re-checked because a click on the pin can arrive first.
        QTimer::singleShot(0, this, [this]() {
            if (!m_pinned && isVisible() && !isActiveWindow())
                hide();
        });
    }
    return QWidget::event(event);
}

void OutlinePopup::keyPressEvent(QKeyEvent* event) {
    // Qt::Popup closed on Escape for free; Qt::Tool does not.
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool OutlinePopup::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_search && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        QTimer::singleShot(0, this, &OutlinePopup::positionSearchClearButton);
    }
    return QWidget::eventFilter(obj, event);
}

void OutlinePopup::positionSearchClearButton() {
    if (!m_search)
        return;

    if (auto* clearButton = m_search->findChild<QToolButton*>()) {
        const int size = 24;
        const int x = m_search->width() - size - 4;
        const int y = (m_search->height() - size) / 2;
        clearButton->setFixedSize(size, size);
        clearButton->setAutoRaise(true);
        clearButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        clearButton->setIconSize(QSize(20, 20));
        clearButton->setIcon(IconHelper::materialIconOutlined(
            QChar(0xE5CD), palette().color(QPalette::Text), 20));
        clearButton->move(x, y);
    }
}

void OutlinePopup::updateHighlight() {
    if (!m_editor) return;
    int currentLine = m_editor->textCursor().blockNumber();
    
    QTreeWidgetItem* bestMatch = nullptr;
    int minDistance = 999999;

    std::function<void(QTreeWidgetItem*)> search = [&](QTreeWidgetItem* p) {
        if (!p || (p->flags() & Qt::NoItemFlags)) return;
        int line = p->data(0, Qt::UserRole).toInt();
        if (line <= currentLine && (currentLine - line) < minDistance) {
            minDistance = currentLine - line;
            bestMatch = p;
        }
        for (int i = 0; i < p->childCount(); ++i) {
            search(p->child(i));
        }
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        search(m_tree->topLevelItem(i));
    }

    m_ignoreSelectionChange = true;
    m_tree->clearSelection();
    if (bestMatch) {
        bestMatch->setSelected(true);
        m_tree->scrollToItem(bestMatch);
    }
    m_ignoreSelectionChange = false;
}
