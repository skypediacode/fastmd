#include "MainWindow.h"

#include "AlignedMenu.h"
#include "DocumentModel.h"
#include "FixAllMisspellingsDialog.h"
#include "MarkdownEditor.h"
#include "SpellCheckExclusions.h"
#include "SpellChecker.h"
#include "TabWidget.h"
#include "ThemedMessageBox.h"

#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextBlock>
#include <QToolButton>
#include <QUrl>

// ---------------------------------------------------------------------------
// Spell check state.
//
// applySpellCheckState() is the only writer of m_spellCheckEnabled and
// m_spellCheckLanguage. Preferences, the Edit menu, the status-bar popup and
// the Command Palette all route through it, which is what keeps those four
// surfaces from drifting apart.

bool MainWindow::spellCheckEffectivelyOn() const
{
    return m_spellCheckEnabled && !m_spellCheckLanguage.isEmpty();
}

// Enumerating the installed languages spawns the platform spell-check factory,
// so it must never run at startup. Every surface that needs the list calls
// this first, and it does its work exactly once.
void MainWindow::ensureSpellCheckResolved()
{
    if (m_spellCheckResolved)
        return;
    m_spellCheckResolved = true;

    SpellChecker& checker = SpellChecker::instance();
    const QString resolved = checker.resolveLanguage(m_spellCheckLanguage);
    if (resolved.isEmpty()) {
        // Nothing installed: keep the feature safely inactive rather than
        // pretending a language is selected.
        m_spellCheckLanguage.clear();
        m_spellCheckEnabled = false;
        return;
    }
    m_spellCheckLanguage = resolved;
    if (m_spellCheckEnabled)
        checker.setLanguage(resolved);
}

void MainWindow::applySpellCheckState(bool enabled, const QString& languageTag)
{
    SpellChecker& checker = SpellChecker::instance();
    const QString previousCheckerLanguage = checker.languageTag();

    QString resolved = languageTag;
    if (enabled || !languageTag.isEmpty()) {
        m_spellCheckResolved = true;
        resolved = checker.resolveLanguage(languageTag);
    }

    if (enabled && resolved.isEmpty())
        enabled = false;

    m_spellCheckLanguage = resolved;
    m_spellCheckEnabled = enabled;

    // A failed CreateSpellChecker leaves the feature off, but the chosen
    // language is kept: discarding it would silently lose the user's setting
    // over a transient failure, and it is still the right thing to retry with.
    if (enabled && !checker.setLanguage(resolved))
        m_spellCheckEnabled = false;

    if (m_tabs)
        m_tabs->setSpellCheckActive(spellCheckEffectivelyOn());

    // setSpellCheckActive(true) is intentionally a no-op for an editor that is
    // already active. When the dictionary changed underneath that state, force
    // the visible editor to replace its old-language underlines now; hidden
    // tabs are refreshed on activation instead of all waking at once.
    if (spellCheckEffectivelyOn()
        && previousCheckerLanguage.compare(m_spellCheckLanguage, Qt::CaseInsensitive) != 0
        && m_tabs && m_tabs->currentEditor()) {
        m_tabs->currentEditor()->invalidateSpellCheck();
    }

    syncSpellCheckUi();
    writeSettings();
}

// Projects the shared state onto every surface. Called from
// applySpellCheckState() and from syncCurrentTabUi(), so a tab switch that
// changes Fix All's eligibility is reflected immediately.
void MainWindow::syncSpellCheckUi()
{
    const bool on = spellCheckEffectivelyOn();

    if (m_lblSpell) {
        m_lblSpell->setText(on ? tr("Spell: %1").arg(m_spellCheckLanguage) : tr("Spell: Off"));
        m_lblSpell->setToolTip(on
            ? tr("Spell check: %1 -- click to change")
                  .arg(SpellChecker::displayNameFor(m_spellCheckLanguage))
            : tr("Spell check is off -- click to enable"));
    }

    if (m_actSpellEnable) {
        QSignalBlocker blocker(m_actSpellEnable);
        m_actSpellEnable->setChecked(on);
    }

    // The Command Palette reads isEnabled() directly without ever opening the
    // menu, so Fix All's enabled state has to be maintained here rather than
    // on aboutToShow.
    if (m_actSpellFixAll) {
        TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
        const bool eligible = pg && pg->editor && pg->editor->spellCheckActive();
        m_actSpellFixAll->setEnabled(on && eligible);
    }

    if (m_spellLanguageGroup) {
        for (QAction* action : m_spellLanguageGroup->actions()) {
            QSignalBlocker blocker(action);
            action->setChecked(action->data().toString().compare(m_spellCheckLanguage,
                                                                 Qt::CaseInsensitive) == 0);
        }
    }
}

void MainWindow::rebuildSpellLanguageMenu()
{
    if (!m_spellLanguageMenu)
        return;
    ensureSpellCheckResolved();

    const QVector<SpellLanguage>& languages = SpellChecker::instance().availableLanguages();
    if (m_spellLanguageGroup && m_spellLanguageGroup->actions().size() == languages.size()) {
        syncSpellCheckUi();
        return;
    }

    m_spellLanguageMenu->clear();
    delete m_spellLanguageGroup;
    m_spellLanguageGroup = new QActionGroup(this);

    if (languages.isEmpty()) {
        QAction* empty = m_spellLanguageMenu->addAction(tr("No languages installed"));
        empty->setEnabled(false);
        return;
    }

    for (const SpellLanguage& language : languages) {
        QAction* action = m_spellLanguageMenu->addAction(language.displayName);
        action->setCheckable(true);
        action->setData(language.tag);
        m_spellLanguageGroup->addAction(action);
        const QString tag = language.tag;
        connect(action, &QAction::triggered, this, [this, tag] {
            applySpellCheckState(true, tag);
        });
    }
    syncSpellCheckUi();
}

// ---------------------------------------------------------------------------
void MainWindow::openSpellCheckMenu()
{
    if (consumeStatusBarPopupDismissal(m_lblSpell))
        return;
    if (!m_lblSpell)
        return;

    ensureSpellCheckResolved();

    if (!m_spellMenu) {
        m_spellMenu = new AlignedMenu(this);
        m_spellMenu->setShowAnimationStyle(AlignedMenu::ContextMenu);
    }

    // Rebuilt per open because the installed-language list is read from
    // Windows and can change while FastMD is running.
    m_spellMenu->clear();
    m_spellMenu->addAction(m_actSpellEnable);
    m_spellMenu->addSeparator();

    const QVector<SpellLanguage>& languages = SpellChecker::instance().availableLanguages();
    if (languages.isEmpty()) {
        QAction* empty = m_spellMenu->addAction(tr("No languages installed"));
        empty->setEnabled(false);
    } else {
        // No QActionGroup here: this menu is rebuilt on every open, so a group
        // parented to it would accumulate one dead object per open, and the
        // popup closes on any selection anyway.
        for (const SpellLanguage& language : languages) {
            QAction* action = m_spellMenu->addAction(language.displayName);
            action->setCheckable(true);
            action->setChecked(language.tag.compare(m_spellCheckLanguage, Qt::CaseInsensitive) == 0);
            const QString tag = language.tag;
            connect(action, &QAction::triggered, this, [this, tag] {
                applySpellCheckState(true, tag);
            });
        }
    }

    m_spellMenu->addSeparator();
    m_spellMenu->addAction(m_actSpellFixAll);
    m_spellMenu->addSeparator();
    m_spellMenu->addAction(m_actSpellSettings);

    syncSpellCheckUi();
    popupStatusBarMenu(m_spellMenu, m_lblSpell);
}

// ---------------------------------------------------------------------------
// Fix All Misspellings.

void MainWindow::fixAllMisspellings()
{
    if (!spellCheckEffectivelyOn())
        return;

    TabPage* pg = m_tabs ? m_tabs->currentPage() : nullptr;
    if (!pg || !pg->editor || !pg->editor->spellCheckActive())
        return;

    MarkdownEditor* editor = pg->editor;
    QTextDocument* document = editor->document();
    SpellChecker& checker = SpellChecker::instance();
    const bool markdown = pg->model->editorMode() == EditorMode::Markdown;

    // Explicitly user-triggered, so the whole document is fair game - but it is
    // chunked so the UI keeps painting, and it reuses the live exclusion mask
    // and engine rather than a second implementation.
    const int blockCount = document->blockCount();
    QProgressDialog* progress = nullptr;
    if (blockCount > 2000) {
        progress = new QProgressDialog(tr("Checking spelling..."), tr("Cancel"), 0, blockCount, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(300);
    }

    QVector<FixAllEntry> entries;
    QHash<QString, int> indexByWord;
    SpellCheckExclusions::LineState state;
    bool cancelled = false;

    QTextBlock block = document->firstBlock();
    for (int number = 0; block.isValid(); block = block.next(), ++number) {
        const QString text = block.text();

        if (markdown) {
            const int userState = block.userState();
            if (userState >= 0)
                state.inFence = (userState == 1);
        }

        const QVector<SpellCheckExclusions::Span> prose =
            markdown ? SpellCheckExclusions::proseSpans(text, state)
                     : SpellCheckExclusions::plainTextSpans(text);
        const int blockPosition = block.position();
        for (const SpellCheckExclusions::Span& span : prose) {
            const QString fragment = text.mid(span.start, span.length);
            for (const SpellCheckExclusions::Span& error : checker.check(fragment)) {
                const int start = blockPosition + span.start + error.start;
                const QString word = text.mid(span.start + error.start, error.length);
                int index = indexByWord.value(word, -1);
                if (index < 0) {
                    index = entries.size();
                    indexByWord.insert(word, index);
                    FixAllEntry entry;
                    entry.word = word;
                    entry.suggestions = checker.suggestions(word);
                    entries.append(entry);
                }
                // A QTextCursor is re-mapped by QTextDocument through any edit
                // that happens while the review dialog is open, which is what
                // makes stale positions impossible at apply time.
                QTextCursor occurrence(document);
                occurrence.setPosition(start);
                occurrence.setPosition(start + error.length, QTextCursor::KeepAnchor);
                entries[index].occurrences.append(occurrence);
            }
        }

        state = SpellCheckExclusions::advance(text, state, number);

        if (progress && (number % 300) == 0) {
            progress->setValue(number);
            QApplication::processEvents();
            if (progress->wasCanceled()) {
                cancelled = true;
                break;
            }
        }
    }

    if (progress) {
        progress->close();
        progress->deleteLater();
    }
    if (cancelled)
        return;

    if (entries.isEmpty()) {
        themedInformation(this, tr("Spell Check"), tr("No misspellings found."));
        return;
    }

    FixAllMisspellingsDialog dialog(entries, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QVector<FixAllEntry> results = dialog.entries();
    int replaced = 0;

    // One edit block for the whole operation, so a single Ctrl+Z reverts it.
    QTextCursor scratch(document);
    scratch.beginEditBlock();
    for (const FixAllEntry& entry : results) {
        if (!entry.include || entry.suggestions.isEmpty())
            continue;
        if (entry.chosen < 0 || entry.chosen >= entry.suggestions.size())
            continue;
        const QString replacement = entry.suggestions.at(entry.chosen);
        for (QTextCursor occurrence : entry.occurrences) {
            if (occurrence.selectedText() != entry.word)
                continue;
            occurrence.insertText(replacement);
            ++replaced;
        }
    }
    scratch.endEditBlock();

    editor->refreshSpellCheck();
    statusBar()->showMessage(replaced == 1 ? tr("Replaced 1 misspelling.")
                                           : tr("Replaced %1 misspellings.").arg(replaced), 4000);
}
