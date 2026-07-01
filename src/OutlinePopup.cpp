#include "OutlinePopup.h"
#include "MarkdownEditor.h"
#include "DocumentModel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QHeaderView>
#include <QEvent>
#include <QDateTime>

OutlinePopup::OutlinePopup(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(300, 400);

    // Simple styling
    setStyleSheet("OutlinePopup { background: #ffffff; border: 1px solid #a0a0a0; border-top: 1px solid #a0a0a0; border-left: 1px solid #a0a0a0; border-radius: 6px; }"
                  "QTreeWidget { border: none; font-family: 'Segoe UI'; font-size: 13px; }"
                  "QLineEdit { border: 1px solid #cccccc; border-radius: 4px; padding: 4px; margin: 4px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search headings..."));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &OutlinePopup::filterTree);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePopup::onItemClicked);

    layout->addWidget(m_search);
    layout->addWidget(m_tree);
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
    m_tree->clear();
    if (!m_editor) return;

    QString text = m_editor->toPlainText();
    QStringList lines = text.split('\n');

    QRegularExpression regex("^#{1,6}\\s+(.*)");
    QTreeWidgetItem* lastItems[7] = { nullptr };

    for (int i = 0; i < lines.size(); ++i) {
        QRegularExpressionMatch match = regex.match(lines[i]);
        if (match.hasMatch()) {
            int level = lines[i].indexOf(' '); // Number of # chars
            if (level < 1 || level > 6) continue;

            QString title = match.captured(1).trimmed();
            auto* item = new QTreeWidgetItem();
            item->setText(0, title);
            item->setData(0, Qt::UserRole, i); // Store line number

            // Find parent
            QTreeWidgetItem* parent = nullptr;
            for (int l = level - 1; l >= 1; --l) {
                if (lastItems[l]) {
                    parent = lastItems[l];
                    break;
                }
            }

            if (parent) {
                parent->addChild(item);
            } else {
                m_tree->addTopLevelItem(item);
            }

            lastItems[level] = item;
            // Clear deeper levels
            for (int l = level + 1; l <= 6; ++l) {
                lastItems[l] = nullptr;
            }
        }
    }

    if (m_tree->topLevelItemCount() == 0) {
        auto* emptyItem = new QTreeWidgetItem();
        emptyItem->setText(0, tr("No headings found"));
        emptyItem->setFlags(Qt::NoItemFlags);
        m_tree->addTopLevelItem(emptyItem);
    } else {
        m_tree->expandAll();
    }
    
    updateHighlight();
}

void OutlinePopup::filterTree(const QString& text) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->flags() & Qt::NoItemFlags) continue; // Skip empty item
        
        bool match = item->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match && !text.isEmpty());
        // For simplicity, we just filter top level or all levels loosely.
        // A full recursive filter is better, but this is a start.
        QList<QTreeWidgetItem*> children;
        // recursive collect
        std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* p) {
            for (int c = 0; c < p->childCount(); ++c) {
                children.append(p->child(c));
                collect(p->child(c));
            }
        };
        collect(item);
        
        for (auto* child : children) {
            bool childMatch = child->text(0).contains(text, Qt::CaseInsensitive);
            child->setHidden(!childMatch && !text.isEmpty());
            if (childMatch && !text.isEmpty()) {
                // ensure parent is visible
                auto* p = child->parent();
                while (p) {
                    p->setHidden(false);
                    p = p->parent();
                }
            }
        }
    }
}

void OutlinePopup::onItemClicked(QTreeWidgetItem* item) {
    if (!m_editor || (item->flags() & Qt::NoItemFlags)) return;
    
    int line = item->data(0, Qt::UserRole).toInt();
    QTextCursor cursor(m_editor->document()->findBlockByNumber(line));
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    
    // Close popup on click as requested
    hide();
}

void OutlinePopup::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    setProperty("lastHideTime", QDateTime::currentMSecsSinceEpoch());
    m_search->clear();
    emit popupHidden();
}

bool OutlinePopup::eventFilter(QObject* obj, QEvent* event) {
    return QWidget::eventFilter(obj, event);
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
