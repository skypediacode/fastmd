#include "LanguageSelectorPopup.h"
#include "SyntaxDefinitionResolver.h"
#include "SyntaxHighlightingRepository.h"

#include <QVBoxLayout>
#include <QLineEdit>
#include <QTreeWidget>
#include <QHeaderView>
#include <QKeyEvent>
#include <QScreen>
#include <QApplication>
#include <QCollator>
#include <QFont>
#include <QStyleFactory>
#include <functional>
#include <algorithm>

namespace {
constexpr int RoleStableName = Qt::UserRole;
constexpr int RoleIsAutoDetect = Qt::UserRole + 1;
constexpr int RoleIsSelectable = Qt::UserRole + 2;
constexpr int RoleAlwaysVisible = Qt::UserRole + 3;
// The row's label exactly as built, without the current-language marker.
// Display text carries the marker, so filtering and re-marking both read this
// instead of trying to strip the marker back off the visible string.
constexpr int RoleLabel = Qt::UserRole + 4;
// Everything the search box matches against for this row, pre-folded into one
// lowercase haystack: display label, section, untranslated definition name,
// alternate names, and the file patterns the definition claims (so typing
// "cpp" or ".ipynb" finds the language even when its name shares nothing with
// the extension).
constexpr int RoleSearchText = Qt::UserRole + 5;

const QString& checkedMarker()
{
    static const QString marker = QStringLiteral("✓ ");
    return marker;
}

const QString& uncheckedMarker()
{
    static const QString marker = QStringLiteral("   ");
    return marker;
}

void setRowLabel(QTreeWidgetItem* item, const QString& label, const QStringList& extraSearchTerms = {})
{
    item->setData(0, RoleLabel, label);
    QStringList haystack = extraSearchTerms;
    haystack.prepend(label);
    item->setData(0, RoleSearchText, haystack.join(QLatin1Char(' ')).toLower());
    item->setText(0, uncheckedMarker() + label);
}
}

LanguageSelectorPopup::LanguageSelectorPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("languageSelectorPopup"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("languageSelectorSearch"));
    m_search->setPlaceholderText(tr("Search language..."));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("languageSelectorTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(8);
    m_tree->setMinimumSize(280, 320);
    // The native Windows 11 style paints its own accent-colored "current
    // item" indicator bar on tree/list rows that our QSS ::item rules can't
    // suppress (it isn't a styleable box-model subcontrol); Fusion has no
    // such decoration and still honors all of our ::item QSS below.
    if (QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        fusionStyle->setParent(m_tree);
        m_tree->setStyle(fusionStyle);
    }
    layout->addWidget(m_tree);

    connect(m_search, &QLineEdit::textChanged, this, &LanguageSelectorPopup::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, &LanguageSelectorPopup::activateItem);
    connect(m_tree, &QTreeWidget::itemClicked, this, &LanguageSelectorPopup::activateItem);

    m_search->installEventFilter(this);
    m_tree->installEventFilter(this);
}

void LanguageSelectorPopup::ensurePopulated()
{
    if (m_populated)
        return;
    m_populated = true;

    auto* autoItem = new QTreeWidgetItem(m_tree);
    autoItem->setData(0, RoleIsAutoDetect, true);
    autoItem->setData(0, RoleIsSelectable, true);
    autoItem->setData(0, RoleAlwaysVisible, true);
    setRowLabel(autoItem, tr("Auto Detect"));

    auto* markdownItem = new QTreeWidgetItem(m_tree);
    markdownItem->setData(0, RoleStableName, SyntaxDefinitionResolver::fastMdMarkdownDefinitionName());
    markdownItem->setData(0, RoleIsSelectable, true);
    markdownItem->setData(0, RoleAlwaysVisible, true);
    setRowLabel(markdownItem, tr("Markdown"),
                { QStringLiteral("md markdown mdown mkd") });

    auto* plainItem = new QTreeWidgetItem(m_tree);
    plainItem->setData(0, RoleStableName, QString());
    plainItem->setData(0, RoleIsSelectable, true);
    plainItem->setData(0, RoleAlwaysVisible, true);
    setRowLabel(plainItem, tr("Plain Text"), { QStringLiteral("txt text none") });

    const auto definitions = SyntaxDefinitionResolver::availableDefinitions();
    QHash<QString, QVector<KSyntaxHighlighting::Definition>> bySection;
    for (const auto& def : definitions)
        bySection[def.translatedSection()].append(def);

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    QStringList sections = bySection.keys();
    std::sort(sections.begin(), sections.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });

    QHash<QString, int> nameCounts;
    for (const auto& def : definitions)
        nameCounts[def.translatedName()]++;

    for (const QString& section : sections) {
        auto defs = bySection.value(section);
        std::sort(defs.begin(), defs.end(), [&collator](const KSyntaxHighlighting::Definition& a, const KSyntaxHighlighting::Definition& b) {
            return collator.compare(a.translatedName(), b.translatedName()) < 0;
        });

        const QString sectionLabel = section.isEmpty() ? tr("Other") : section;
        auto* sectionItem = new QTreeWidgetItem(m_tree, { sectionLabel });
        sectionItem->setFlags(sectionItem->flags() & ~Qt::ItemIsSelectable);
        QFont sectionFont = sectionItem->font(0);
        sectionFont.setBold(true);
        sectionItem->setFont(0, sectionFont);
        sectionItem->setData(0, RoleIsSelectable, false);
        sectionItem->setData(0, RoleLabel, sectionLabel);

        for (const auto& def : defs) {
            QString label = def.translatedName();
            if (nameCounts.value(label) > 1)
                label = tr("%1 (%2)").arg(label, section);
            auto* item = new QTreeWidgetItem(sectionItem);
            item->setData(0, RoleStableName, def.name());
            item->setData(0, RoleIsSelectable, true);
            item->setToolTip(0, def.name());

            QStringList searchTerms{ sectionLabel, def.name() };
            searchTerms += def.alternativeNames();
            for (const QString& pattern : def.extensions())
                searchTerms.append(QString(pattern).remove(QLatin1Char('*')));
            setRowLabel(item, label, searchTerms);
        }
    }
    m_tree->expandAll();
}

void LanguageSelectorPopup::popup(QWidget* anchor, const QString& currentDefinitionName, bool isAutomatic)
{
    ensurePopulated();
    m_search->clear();
    applyFilter(QString());
    setCurrentLanguage(currentDefinitionName, isAutomatic);

    adjustSize();
    QPoint pos = anchor->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - height());
    if (const QScreen* screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        if (pos.x() + width() > avail.right())
            pos.setX(avail.right() - width());
        if (pos.x() < avail.left())
            pos.setX(avail.left());
        if (pos.y() < avail.top())
            pos.setY(avail.top());
        if (pos.y() + height() > avail.bottom())
            pos.setY(avail.bottom() - height());
    }
    move(pos);
    show();
    m_search->setFocus();
}

void LanguageSelectorPopup::applyFilter(const QString& text)
{
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = m_tree->topLevelItem(i);
        if (top->childCount() == 0) {
            // Auto Detect / Markdown / Plain Text stay pinned and reachable no
            // matter what is typed -- they are commands, not search results.
            const bool alwaysVisible = top->data(0, RoleAlwaysVisible).toBool();
            top->setHidden(!(alwaysVisible || needle.isEmpty()
                             || top->data(0, RoleSearchText).toString().contains(needle)));
            continue;
        }
        const bool sectionMatches = !needle.isEmpty()
            && top->data(0, RoleLabel).toString().toLower().contains(needle);
        bool anyChildVisible = false;
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* child = top->child(c);
            const bool match = needle.isEmpty() || sectionMatches
                || child->data(0, RoleSearchText).toString().contains(needle);
            child->setHidden(!match);
            anyChildVisible = anyChildVisible || match;
        }
        top->setHidden(!anyChildVisible);
    }
}

void LanguageSelectorPopup::setCurrentLanguage(const QString& currentDefinitionName, bool isAutomatic)
{
    m_currentDefinitionName = currentDefinitionName;
    m_isAutomatic = isAutomatic;
    refreshCheckedState();
    updateAccessibleName();
}

void LanguageSelectorPopup::refreshCheckedState()
{
    const bool markdownSelected = SyntaxDefinitionResolver::isMarkdownDefinitionName(m_currentDefinitionName);
    std::function<void(QTreeWidgetItem*)> mark = [&](QTreeWidgetItem* item) {
        for (int i = 0; i < item->childCount(); ++i)
            mark(item->child(i));

        const bool isAutoRow = item->data(0, RoleIsAutoDetect).toBool();
        if (!item->data(0, RoleIsSelectable).toBool() && !isAutoRow)
            return;

        bool current = false;
        if (isAutoRow) {
            // The Auto Detect row reports the selection *source*, not the
            // language, so it can be marked alongside the language row.
            current = m_isAutomatic;
        } else {
            const QString name = item->data(0, RoleStableName).toString();
            current = markdownSelected ? SyntaxDefinitionResolver::isMarkdownDefinitionName(name)
                                       : (name == m_currentDefinitionName);
        }

        QFont f = item->font(0);
        f.setBold(current);
        item->setFont(0, f);

        const QString label = item->data(0, RoleLabel).toString();
        item->setText(0, (current ? checkedMarker() : uncheckedMarker()) + label);
        item->setData(0, Qt::AccessibleTextRole, current
            ? (isAutoRow ? tr("%1, current selection source").arg(label)
                         : tr("%1, current language").arg(label))
            : label);
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        mark(m_tree->topLevelItem(i));
}

void LanguageSelectorPopup::updateAccessibleName()
{
    setAccessibleName(tr("Language selector, current language %1, %2")
        .arg(SyntaxDefinitionResolver::displayNameForStoredName(m_currentDefinitionName),
             m_isAutomatic ? tr("automatic") : tr("manual")));
}

void LanguageSelectorPopup::activateItem(QTreeWidgetItem* item)
{
    if (!item || !item->data(0, RoleIsSelectable).toBool())
        return;

    if (item->data(0, RoleIsAutoDetect).toBool()) {
        emit autoDetectRequested();
    } else {
        emit definitionRequested(item->data(0, RoleStableName).toString());
    }
    close();
}

void LanguageSelectorPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool LanguageSelectorPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (watched == m_search) {
            if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
                m_tree->setFocus();
                if (!m_tree->currentItem()) {
                    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
                        if (!m_tree->topLevelItem(i)->isHidden()) {
                            m_tree->setCurrentItem(m_tree->topLevelItem(i));
                            break;
                        }
                    }
                }
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (QTreeWidgetItem* item = m_tree->currentItem())
                    activateItem(item);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        }
        if (watched == m_tree && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)) {
            if (QTreeWidgetItem* item = m_tree->currentItem())
                activateItem(item);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
