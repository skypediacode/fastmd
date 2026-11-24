#include "GoToLineDialog.h"
#include "MarkdownEditor.h"
#include "OutlineModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QIntValidator>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QKeyEvent>
#include <limits>

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
constexpr int kDialogWidth = 360;
constexpr QSize kLineTabSize(kDialogWidth, 160);
constexpr QSize kOutlineTabSize(kDialogWidth, 440);
}

GoToLineDialog::GoToLineDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Go To"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    applyTitleBarTheme();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("goToTabs"));
    m_tabs->addTab(buildLineTab(), tr("Line"));
    m_tabs->addTab(buildOutlineTab(), tr("Outline"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &GoToLineDialog::onTabChanged);

    mainLayout->addWidget(m_tabs);

    applyTabSize(LineTab);
}

QWidget* GoToLineDialog::buildLineTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* inputLayout = new QHBoxLayout();
    auto* lineLabel = new QLabel(tr("Line number:"), page);
    m_lineEdit = new QLineEdit(page);
    m_lineEdit->setObjectName(QStringLiteral("goToLineInput"));
    m_lineEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // An even logical height lands on whole physical pixels at common Windows
    // scale factors (notably 150%), keeping the text and caret centered.
    m_lineEdit->setFixedHeight(24);
    // The platform style owns the painted text rect, so use QLineEdit's direct
    // margins (rather than stylesheet padding) for the one-pixel upward shift.
    m_lineEdit->setTextMargins(0, 0, 0, 0);
    m_lineEdit->setValidator(new QIntValidator(1, 9999999, this));
    inputLayout->addWidget(lineLabel);
    inputLayout->addWidget(m_lineEdit);

    m_rangeLabel = new QLabel(page);
    m_rangeLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* btnGo = new QPushButton(tr("Go"), page);
    btnGo->setDefault(true);
    auto* btnCancel = new QPushButton(tr("Cancel"), page);
    btnLayout->addWidget(btnGo);
    btnLayout->addWidget(btnCancel);

    layout->addLayout(inputLayout);
    layout->addWidget(m_rangeLabel);
    layout->addStretch();
    layout->addLayout(btnLayout);

    connect(btnGo, &QPushButton::clicked, this, &GoToLineDialog::onGoClicked);
    connect(btnCancel, &QPushButton::clicked, this, &GoToLineDialog::reject);
    connect(m_lineEdit, &QLineEdit::returnPressed, this, &GoToLineDialog::onGoClicked);

    return page;
}

QWidget* GoToLineDialog::buildOutlineTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_outlineSearch = new QLineEdit(page);
    m_outlineSearch->setObjectName(QStringLiteral("goToOutlineSearch"));
    m_outlineSearch->setPlaceholderText(tr("Search headings..."));
    m_outlineSearch->setClearButtonEnabled(true);
    m_outlineSearch->installEventFilter(this);
    connect(m_outlineSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        // Preselect the first real match, not the ancestor rows kept visible to
        // show where it sits, so Enter goes where the typing pointed.
        QTreeWidgetItem* match = OutlineModel::filter(m_outlineTree, text);
        if (match) {
            m_outlineTree->setCurrentItem(match);
            m_outlineTree->scrollToItem(match);
        } else if (text.isEmpty()) {
            selectHeadingAtCursor();
        }
    });

    m_outlineTree = new QTreeWidget(page);
    m_outlineTree->setObjectName(QStringLiteral("goToOutlineTree"));
    m_outlineTree->setHeaderHidden(true);
    m_outlineTree->setIndentation(16);
    m_outlineTree->setUniformRowHeights(true);
    m_outlineTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_outlineTree, &QTreeWidget::itemClicked, this, &GoToLineDialog::activateOutlineItem);
    connect(m_outlineTree, &QTreeWidget::itemActivated, this, &GoToLineDialog::activateOutlineItem);

    layout->addWidget(m_outlineSearch);
    layout->addWidget(m_outlineTree, 1);

    return page;
}

void GoToLineDialog::setEditor(MarkdownEditor* editor)
{
    m_editor = editor;
}

void GoToLineDialog::selectLineTab()
{
    m_tabs->setCurrentIndex(LineTab);
}

void GoToLineDialog::updateLineRange()
{
    if (!m_editor) return;
    int maxLines = m_editor->document()->blockCount();
    m_rangeLabel->setText(tr("Line 1\xE2\x80\x93%1").arg(maxLines));
}

void GoToLineDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
    updateLineRange();
    m_lineEdit->clear();
    m_outlineSearch->clear();
    onTabChanged(m_tabs->currentIndex());
}

void GoToLineDialog::onTabChanged(int index)
{
    applyTabSize(index);
    if (index == OutlineTab) {
        refreshOutline();
        m_outlineSearch->setFocus();
    } else {
        m_lineEdit->setFocus();
        m_lineEdit->selectAll();
    }
}

void GoToLineDialog::applyTabSize(int index)
{
    // Keep the dialog width stable while switching tabs. The Outline tab gets
    // a taller body whose list scrolls once the headings exceed it.
    setFixedSize(index == OutlineTab ? kOutlineTabSize : kLineTabSize);
}

void GoToLineDialog::refreshOutline()
{
    if (!m_editor) {
        m_outlineTree->clear();
        return;
    }

    OutlineModel::populate(m_outlineTree, m_editor->toPlainText(), tr("No headings found"));
    QTreeWidgetItem* match = OutlineModel::filter(m_outlineTree, m_outlineSearch->text());
    if (match) {
        m_outlineTree->setCurrentItem(match);
        m_outlineTree->scrollToItem(match);
        return;
    }
    selectHeadingAtCursor();
}

void GoToLineDialog::selectHeadingAtCursor()
{
    if (!m_editor) return;

    // Preselect the heading the caret currently sits in, so Enter without any
    // typing behaves like "go to the heading I am under".
    const int currentLine = m_editor->textCursor().blockNumber();
    QTreeWidgetItem* best = nullptr;
    int minDistance = std::numeric_limits<int>::max();
    for (QTreeWidgetItemIterator it(m_outlineTree, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
        if ((*it)->flags() == Qt::NoItemFlags) continue;
        const int line = (*it)->data(0, Qt::UserRole).toInt();
        if (line <= currentLine && currentLine - line < minDistance) {
            minDistance = currentLine - line;
            best = *it;
        }
    }
    if (!best) {
        QTreeWidgetItemIterator it(m_outlineTree, QTreeWidgetItemIterator::NotHidden);
        if (*it && (*it)->flags() != Qt::NoItemFlags)
            best = *it;
    }
    if (best) {
        m_outlineTree->setCurrentItem(best);
        m_outlineTree->scrollToItem(best);
    }
}

QTreeWidgetItem* GoToLineDialog::currentOutlineItem() const
{
    QTreeWidgetItem* item = m_outlineTree->currentItem();
    if (item && !item->isHidden() && item->flags() != Qt::NoItemFlags)
        return item;

    QTreeWidgetItemIterator it(m_outlineTree, QTreeWidgetItemIterator::NotHidden);
    if (*it && (*it)->flags() != Qt::NoItemFlags)
        return *it;
    return nullptr;
}

void GoToLineDialog::moveOutlineSelection(int delta)
{
    QList<QTreeWidgetItem*> visible;
    for (QTreeWidgetItemIterator it(m_outlineTree, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
        if ((*it)->flags() != Qt::NoItemFlags)
            visible.append(*it);
    }
    if (visible.isEmpty()) return;

    int index = visible.indexOf(m_outlineTree->currentItem());
    if (index < 0)
        index = delta > 0 ? -1 : visible.size();
    index = qBound(0, index + delta, visible.size() - 1);

    m_outlineTree->setCurrentItem(visible.at(index));
    m_outlineTree->scrollToItem(visible.at(index));
}

void GoToLineDialog::activateOutlineItem(QTreeWidgetItem* item)
{
    if (!m_editor || !item || item->flags() == Qt::NoItemFlags) return;

    const int line = item->data(0, Qt::UserRole).toInt();
    emit navigationRequested();
    accept();
    // goToLine() reveals, centers, flashes and focuses the editor; it runs after
    // accept() so the closing dialog cannot take focus back.
    m_editor->goToLine(line + 1);
}

bool GoToLineDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_outlineSearch && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Down:
            moveOutlineSelection(1);
            return true;
        case Qt::Key_Up:
            moveOutlineSelection(-1);
            return true;
        case Qt::Key_PageDown:
            moveOutlineSelection(10);
            return true;
        case Qt::Key_PageUp:
            moveOutlineSelection(-10);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateOutlineItem(currentOutlineItem());
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void GoToLineDialog::applyTitleBarTheme()
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

void GoToLineDialog::onGoClicked()
{
    if (!m_editor) return;
    QString text = m_lineEdit->text();
    if (text.isEmpty()) {
        reject();
        return;
    }
    bool ok;
    int line = text.toInt(&ok);
    if (ok && line > 0) {
        emit navigationRequested();
        m_editor->goToLine(line);
        accept();
    }
}
