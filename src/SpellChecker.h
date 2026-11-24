#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "SpellCheckExclusions.h"

struct SpellLanguage {
    QString tag;         // BCP-47, e.g. "en-US"
    QString displayName; // e.g. "English (United States)"
};

// Thin wrapper around the Windows Spell Checking API (ISpellCheckerFactory /
// ISpellChecker). No dictionaries are bundled: the available languages are the
// ones the user has installed in Windows.
//
// Nothing here runs on the startup path. The COM factory is created on the
// first call that genuinely needs it (opening Preferences, opening the status
// bar popup, or enabling spell check) and cached from then on.
class SpellChecker : public QObject {
    Q_OBJECT
public:
    static SpellChecker& instance();

    // Enumerated once and cached. Empty when the platform has no spell
    // checking support or no language packs with spelling data.
    const QVector<SpellLanguage>& availableLanguages();
    bool isLanguageAvailable(const QString& tag);
    // The best available substitute for `preferred`: itself when installed,
    // otherwise a language-family match, otherwise a system-locale match,
    // otherwise the first available language. Empty when none are available.
    QString resolveLanguage(const QString& preferred);
    static QString displayNameFor(const QString& tag);

    bool setLanguage(const QString& tag);
    QString languageTag() const { return m_languageTag; }
    bool isReady() const { return m_checker != nullptr; }

    // The platform API does its own word segmentation, so callers never need a
    // tokenizer - only the exclusion mask that decides which text to pass in.
    QVector<SpellCheckExclusions::Span> check(const QString& text);
    QStringList suggestions(const QString& word);

    void addToDictionary(const QString& word);
    void ignoreWord(const QString& word);

signals:
    // Emitted when the dictionary gains a word, so open editors can drop stale
    // underlines for it.
    void dictionaryChanged();

private:
    explicit SpellChecker(QObject* parent = nullptr);
    ~SpellChecker() override;

    bool ensureFactory();
    void releaseChecker();

    void* m_factory = nullptr; // ISpellCheckerFactory*
    void* m_checker = nullptr; // ISpellChecker*
    QString m_languageTag;
    QVector<SpellLanguage> m_languages;
    bool m_languagesLoaded = false;
    bool m_factoryAttempted = false;
    bool m_comInitialized = false;
};
