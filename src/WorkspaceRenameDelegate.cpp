#include "WorkspaceRenameDelegate.h"
#include "WorkspaceFileOps.h"
#include "ThemedMessageBox.h"
#include "ThemePalette.h"

#include <QLineEdit>
#include <QIcon>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QColor>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPalette>
#include <QTreeView>
#include <QTimer>

namespace {

class RenameLineEdit final : public QLineEdit
{
public:
    RenameLineEdit(const QIcon& icon, QWidget* parent)
        : QLineEdit(parent)
        , m_icon(icon)
    {
        setTextMargins(20, 0, 0, 0);
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->matches(QKeySequence::Copy) ||
                keyEvent->matches(QKeySequence::Cut) ||
                keyEvent->matches(QKeySequence::Paste) ||
                keyEvent->matches(QKeySequence::SelectAll) ||
                keyEvent->matches(QKeySequence::Undo)) {
                event->accept();
                return true;
            }
        }
        return QLineEdit::event(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QLineEdit::paintEvent(event);
        if (m_icon.isNull())
            return;

        QPainter painter(this);
        const int size = qMin(16, height() - 4);
        m_icon.paint(&painter, QRect(2, (height() - size) / 2, size, size),
                     Qt::AlignCenter, QIcon::Normal, QIcon::On);
    }

private:
    QIcon m_icon;
};

}

WorkspaceRenameDelegate::WorkspaceRenameDelegate(std::function<QString(const QModelIndex&)> pathResolver,
                                                   QObject* parent)
    : QStyledItemDelegate(parent)
    , m_pathResolver(std::move(pathResolver))
{
}

QWidget* WorkspaceRenameDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                                const QModelIndex& index) const
{
    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    auto* lineEdit = new RenameLineEdit(icon, parent);
    lineEdit->setObjectName(QStringLiteral("workspaceRenameEditor"));
    const bool dark = option.palette.color(QPalette::Base).value() < 128;
    if (dark) {
        lineEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { padding: 0px 3px; margin: 0px; border: 1px solid #3994bc; border-radius: 4px; background: #121314; color: #ffffff; font: inherit; selection-background-color: #276782; selection-color: #ffffff; }"
        ));
    } else {
        lineEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { padding: 0px 3px; margin: 0px; border: 1px solid #0078d4; border-radius: 4px; background: #ffffff; color: #000000; font: inherit; selection-background-color: #0078d4; selection-color: #ffffff; }"
        ));
    }
    return lineEdit;
}

void WorkspaceRenameDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    // Adjust the rect for content clipping to reserve space for the trailing action (only in Column 0)
    QStyleOptionViewItem content = option;
    if (index.column() == 0 && m_trailingActionWidth > 0) {
        content.rect.setRight(content.rect.right() - m_trailingActionWidth);
    }

    // Disable the delegate's own background/indicator painting for selected/hovered states
    // so it doesn't overwrite the custom background we drew in drawRow or draw native indicator pills.
    content.state &= ~QStyle::State_Selected;
    content.state &= ~QStyle::State_MouseOver;

    // Explicitly set the text color for selected items so it's readable
    // on our custom background.
    const auto* tree = qobject_cast<const QTreeView*>(option.widget);
    const bool selected = (option.state & QStyle::State_Selected) ||
                          (tree && tree->selectionModel() && tree->selectionModel()->isRowSelected(index.row(), index.parent()));
    if (selected) {
        const bool dark = option.palette.color(QPalette::Base).value() < 128;
        content.palette.setColor(QPalette::Text, ThemePalette::color(ThemePalette::Text, dark));
    }

    painter->save();
    painter->setClipRect(content.rect);
    QStyledItemDelegate::paint(painter, content, index);
    painter->restore();
}

void WorkspaceRenameDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit || !m_pathResolver)
        return;

    const QString path = m_pathResolver(index);
    const QFileInfo info(path);
    lineEdit->setText(info.fileName());

    // Select only the base name (not the extension) for files, matching
    // Explorer's inline-rename selection behavior; select everything for
    // directories, which have no meaningful extension.
    const bool selectEverything = info.isDir() || info.suffix().isEmpty();
    const int baseNameLength = info.completeBaseName().length();
    if (selectEverything) {
        lineEdit->selectAll();
    } else {
        lineEdit->setSelection(0, baseNameLength);
    }

    // QAbstractItemView may focus the editor after setEditorData() returns.
    // Re-apply the range once the editor has entered the event loop so that
    // that focus pass cannot turn a file-name-only selection into select-all.
    QTimer::singleShot(0, lineEdit, [lineEdit, fileName = info.fileName(),
                                     selectEverything, baseNameLength] {
        if (lineEdit->text() != fileName)
            return;

        if (selectEverything)
            lineEdit->selectAll();
        else
            lineEdit->setSelection(0, baseNameLength);
    });
}

void WorkspaceRenameDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                            const QModelIndex& index) const
{
    Q_UNUSED(model);
    auto* lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit || !m_pathResolver)
        return;

    const QString oldPath = m_pathResolver(index);
    const QString newName = lineEdit->text().trimmed();
    if (oldPath.isEmpty())
        return;

    const QFileInfo oldInfo(oldPath);
    if (newName.isEmpty() || newName == oldInfo.fileName() || !WorkspaceFileOps::isValidFileName(newName))
        return;

    QString finalName = newName;
    const QString desiredPath = QDir(oldInfo.absolutePath()).filePath(newName);
    if (QFileInfo::exists(desiredPath)) {
        const QString suggestedPath = WorkspaceFileOps::uniqueDestinationPath(desiredPath);
        const QString suggestedName = QFileInfo(suggestedPath).fileName();
        const auto answer = themedQuestion(
            editor->window(), tr("Rename File"),
            tr("Do you want to rename \"%1\" to \"%2\"?").arg(oldInfo.fileName(), suggestedName),
            QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No), QMessageBox::Yes,
            tr("There is already a file with the same name in this location."));
        if (answer != QMessageBox::Yes)
            return;
        finalName = suggestedName;
    }

    const QString newPath = WorkspaceFileOps::renamePath(oldPath, finalName);
    if (!newPath.isEmpty())
        emit renamed(oldPath, newPath);
}

void WorkspaceRenameDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                                    const QModelIndex& index) const
{
    Q_UNUSED(index);
    QRect r = option.rect;
    if (option.widget && index.column() == 0) {
        r = option.widget->style()->subElementRect(
            QStyle::SE_ItemViewItemText, &option, option.widget);
    }
    if (index.column() == 0 && m_trailingActionWidth > 0)
        r.setRight(r.right() - m_trailingActionWidth);
    editor->setGeometry(r);
}
