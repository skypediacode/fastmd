#include "SpellChecker.h"

#include <QLocale>
#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>

namespace {

QString takeCoString(LPWSTR value)
{
    if (!value)
        return {};
    const QString result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

} // namespace
#endif

SpellChecker& SpellChecker::instance()
{
    static SpellChecker checker;
    return checker;
}

SpellChecker::SpellChecker(QObject* parent)
    : QObject(parent)
{
}

SpellChecker::~SpellChecker()
{
    releaseChecker();
#if defined(Q_OS_WIN)
    if (m_factory) {
        static_cast<ISpellCheckerFactory*>(m_factory)->Release();
        m_factory = nullptr;
    }
    // Balance S_OK and S_FALSE alike, but only after every cached interface
    // from this apartment has been released. RPC_E_CHANGED_MODE means COM was
    // already initialized differently and adds no reference to balance.
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
#endif
}

void SpellChecker::releaseChecker()
{
#if defined(Q_OS_WIN)
    if (m_checker) {
        static_cast<ISpellChecker*>(m_checker)->Release();
        m_checker = nullptr;
    }
#endif
    m_languageTag.clear();
}

bool SpellChecker::ensureFactory()
{
#if defined(Q_OS_WIN)
    if (m_factory)
        return true;
    if (m_factoryAttempted)
        return false;
    m_factoryAttempted = true;

    const HRESULT comHr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    m_comInitialized = SUCCEEDED(comHr);
    ISpellCheckerFactory* factory = nullptr;
    const HRESULT hr = CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
                                         CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory)
        return false;
    m_factory = factory;
    return true;
#else
    return false;
#endif
}

QString SpellChecker::displayNameFor(const QString& tag)
{
    const QLocale locale(tag);
    if (locale.language() == QLocale::C)
        return tag;
    const QString language = QLocale::languageToString(locale.language());
    const QString territory = QLocale::territoryToString(locale.territory());
    if (territory.isEmpty() || locale.territory() == QLocale::AnyTerritory)
        return language;
    return QStringLiteral("%1 (%2)").arg(language, territory);
}

const QVector<SpellLanguage>& SpellChecker::availableLanguages()
{
    if (m_languagesLoaded)
        return m_languages;
    m_languagesLoaded = true;

#if defined(Q_OS_WIN)
    if (!ensureFactory())
        return m_languages;

    IEnumString* tags = nullptr;
    if (FAILED(static_cast<ISpellCheckerFactory*>(m_factory)->get_SupportedLanguages(&tags)) || !tags)
        return m_languages;

    // get_SupportedLanguages() reports every BCP-47 tag the *installed spell
    // check providers* can serve, which is not the same list as the languages
    // shown in Windows' Language settings: one installed language pack
    // typically covers a family of related locales that share (or specialize)
    // its data. Those entries are real, working dictionaries - they are only
    // surprising, not broken - so nothing here filters by locale. What it does
    // do is re-ask the factory whether each enumerated tag is actually usable,
    // so a provider that advertises a tag it cannot serve never reaches the UI.
    LPOLESTR value = nullptr;
    while (tags->Next(1, &value, nullptr) == S_OK) {
        const QString tag = takeCoString(value);
        value = nullptr;
        if (tag.isEmpty())
            continue;
        BOOL supported = FALSE;
        if (FAILED(static_cast<ISpellCheckerFactory*>(m_factory)
                       ->IsSupported(reinterpret_cast<LPCWSTR>(tag.utf16()), &supported))
            || !supported)
            continue;
        m_languages.append({ tag, displayNameFor(tag) });
    }
    tags->Release();

    std::sort(m_languages.begin(), m_languages.end(),
              [](const SpellLanguage& a, const SpellLanguage& b) {
                  const int byName = a.displayName.compare(b.displayName, Qt::CaseInsensitive);
                  return byName != 0 ? byName < 0 : a.tag < b.tag;
              });
#endif
    return m_languages;
}

bool SpellChecker::isLanguageAvailable(const QString& tag)
{
    if (tag.isEmpty())
        return false;
    const auto& languages = availableLanguages();
    return std::any_of(languages.cbegin(), languages.cend(), [&tag](const SpellLanguage& language) {
        return language.tag.compare(tag, Qt::CaseInsensitive) == 0;
    });
}

QString SpellChecker::resolveLanguage(const QString& preferred)
{
    const auto& languages = availableLanguages();
    if (languages.isEmpty())
        return {};
    if (isLanguageAvailable(preferred)) {
        for (const SpellLanguage& language : languages) {
            if (language.tag.compare(preferred, Qt::CaseInsensitive) == 0)
                return language.tag;
        }
    }

    const auto familyMatch = [&languages](const QString& tag) -> QString {
        const QString family = tag.section(QLatin1Char('-'), 0, 0);
        if (family.isEmpty())
            return {};
        for (const SpellLanguage& language : languages) {
            if (language.tag.section(QLatin1Char('-'), 0, 0).compare(family, Qt::CaseInsensitive) == 0)
                return language.tag;
        }
        return {};
    };

    if (!preferred.isEmpty()) {
        const QString match = familyMatch(preferred);
        if (!match.isEmpty())
            return match;
    }

    const QString systemTag = QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));
    for (const SpellLanguage& language : languages) {
        if (language.tag.compare(systemTag, Qt::CaseInsensitive) == 0)
            return language.tag;
    }
    const QString systemFamily = familyMatch(systemTag);
    if (!systemFamily.isEmpty())
        return systemFamily;

    return languages.first().tag;
}

bool SpellChecker::setLanguage(const QString& tag)
{
    if (tag.isEmpty()) {
        releaseChecker();
        return false;
    }
    if (m_checker && m_languageTag.compare(tag, Qt::CaseInsensitive) == 0)
        return true;

#if defined(Q_OS_WIN)
    if (!ensureFactory())
        return false;

    ISpellChecker* checker = nullptr;
    const HRESULT hr = static_cast<ISpellCheckerFactory*>(m_factory)
                           ->CreateSpellChecker(reinterpret_cast<LPCWSTR>(tag.utf16()), &checker);
    if (FAILED(hr) || !checker)
        return false;

    releaseChecker();
    m_checker = checker;
    m_languageTag = tag;
    return true;
#else
    Q_UNUSED(tag);
    return false;
#endif
}

QVector<SpellCheckExclusions::Span> SpellChecker::check(const QString& text)
{
    QVector<SpellCheckExclusions::Span> errors;
#if defined(Q_OS_WIN)
    if (!m_checker || text.isEmpty())
        return errors;

    IEnumSpellingError* enumerator = nullptr;
    const HRESULT hr = static_cast<ISpellChecker*>(m_checker)
                           ->Check(reinterpret_cast<LPCWSTR>(text.utf16()), &enumerator);
    if (FAILED(hr) || !enumerator)
        return errors;

    ISpellingError* error = nullptr;
    while (enumerator->Next(&error) == S_OK && error) {
        ULONG start = 0;
        ULONG length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        error->get_StartIndex(&start);
        error->get_Length(&length);
        error->get_CorrectiveAction(&action);
        error->Release();
        error = nullptr;
        if (length == 0)
            continue;
        if (action == CORRECTIVE_ACTION_GET_SUGGESTIONS || action == CORRECTIVE_ACTION_REPLACE)
            errors.append({ static_cast<int>(start), static_cast<int>(length) });
    }
    enumerator->Release();
#else
    Q_UNUSED(text);
#endif
    return errors;
}

QStringList SpellChecker::suggestions(const QString& word)
{
    QStringList result;
#if defined(Q_OS_WIN)
    if (!m_checker || word.isEmpty())
        return result;

    IEnumString* enumerator = nullptr;
    const HRESULT hr = static_cast<ISpellChecker*>(m_checker)
                           ->Suggest(reinterpret_cast<LPCWSTR>(word.utf16()), &enumerator);
    if (FAILED(hr) || !enumerator)
        return result;

    LPOLESTR value = nullptr;
    while (enumerator->Next(1, &value, nullptr) == S_OK) {
        const QString suggestion = takeCoString(value);
        value = nullptr;
        if (!suggestion.isEmpty())
            result.append(suggestion);
    }
    enumerator->Release();
#else
    Q_UNUSED(word);
#endif
    return result;
}

void SpellChecker::addToDictionary(const QString& word)
{
#if defined(Q_OS_WIN)
    if (!m_checker || word.isEmpty())
        return;
    static_cast<ISpellChecker*>(m_checker)->Add(reinterpret_cast<LPCWSTR>(word.utf16()));
    emit dictionaryChanged();
#else
    Q_UNUSED(word);
#endif
}

void SpellChecker::ignoreWord(const QString& word)
{
#if defined(Q_OS_WIN)
    if (!m_checker || word.isEmpty())
        return;
    static_cast<ISpellChecker*>(m_checker)->Ignore(reinterpret_cast<LPCWSTR>(word.utf16()));
    emit dictionaryChanged();
#else
    Q_UNUSED(word);
#endif
}
