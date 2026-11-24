#pragma once

#include "DocumentModel.h"

#include <QString>
#include <QList>

struct RecoverySnapshot {
    QString key;
    QString path;
    QString content;
    QString lineEnding;
    // Absent (default UTF-8) in snapshots written before version 3.
    TextEncoding encoding = TextEncoding::Utf8;
    EditorMode mode = EditorMode::Markdown;
    QString displayName;

    // Language state (language-indicator-plan.md), absent (hasLanguageState
    // false) when read back from a version-1 snapshot -- callers derive
    // compatible state from path + mode in that case, same as session migration.
    bool    hasLanguageState = false;
    QString selectedDefinitionName;
    QString automaticallyDetectedName;
    LanguageSelectionSource languageSource = LanguageSelectionSource::Automatic;
    bool    hasLastNonMarkdownSelection = false;
    QString lastNonMarkdownDefinitionName;
    LanguageSelectionSource lastNonMarkdownSource = LanguageSelectionSource::Automatic;
};

class RecoveryManager {
public:
    static QString recoveryDirectory();
    static QString keyFor(const QString& path, const QString& recoveryId);
    static bool write(const RecoverySnapshot& snapshot);
    static QList<RecoverySnapshot> readAll();
    static void remove(const QString& key);
    static void clear();
};
