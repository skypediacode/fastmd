#include "StartupSettings.h"

#include <QSettings>

namespace {

constexpr const char* kStartupKeys[] = {
    "geometry", "recentFiles", "theme", "previewVisible", "wordWrapEnabled",
    "workspaceTreeVisible", "notepadMode", "mathToolbarVisible", "chemistryToolbarVisible",
    "restoreSessionOnStartup", "closeAppOnLastTabClose", "autoSave/enabled",
    "autoSave/delayMs", "autoSave/crashRecovery", "files/openPdfExternally",
    "files/openHtmlExternally", "files/openImagesExternally", "export/showSuccessDialog",
    "defaultSaveFolder", "defaultNewDocMode",
    "editorFontSize", "editor/fontFamily", "preview/fontFamily",
    "document/lineSpacing", "document/paragraphSpacing", "document/listSpacing", "gitDiffZoomDelta", "editor/highlightCurrentLine", "editor/renderSingleLineBreaks",
    "statusBarVisible", "spellCheck/enabled", "spellCheck/language",
    "find/matchCase", "find/wholeWord", "find/regex", "find/preserveCase",
    "lastSaveFolder", "lastOpenFolder", "lastExportFolder", "workspace/recentFolders",
    "workspace/expandedPaths", "workspace/showAll", "workspace/fileTypeColumnVisible",
    "workspace/dateModifiedColumnVisible", "workspace/sizeColumnVisible",
    "workspace/sortColumn", "workspace/sortOrder", "workspace/columnLayoutState",
    "workspace/outlineExpanded", "workspace/outlineSplitterState",
    "workspace/rootPath", "session/activeTab", "splitterState", "pdf/pageSizeId",
    "pdf/customWidthMm", "pdf/customHeightMm", "pdf/customPageName", "pdf/orientation",
    "pdf/marginLeftMm", "pdf/marginTopMm", "pdf/marginRightMm", "pdf/marginBottomMm",
    "pdf/fontSize", "pdf/fontFamily", "pdf/headerEnabled", "pdf/footerEnabled", "pdf/headerLeft",
    "pdf/headerCenter", "pdf/headerRight", "pdf/footerLeft", "pdf/footerCenter",
    "pdf/footerRight", "workspaceSplitterState", "terminal/visible", "app/welcomeShown", "app/lastRunVersion",
};

}

bool StartupSettings::preloads(const QString& key)
{
    for (const char* preloaded : kStartupKeys) {
        if (key == QLatin1StringView(preloaded))
            return true;
    }
    return false;
}

StartupSettings StartupSettings::load()
{
    StartupSettings snapshot;
    QSettings settings;
    for (const char* key : kStartupKeys) {
        const QString settingKey = QString::fromLatin1(key);
        snapshot.m_values.insert(settingKey, settings.value(settingKey));
    }

    const int count = settings.beginReadArray(QStringLiteral("session/tabs"));
    snapshot.m_sessionTabs.reserve(count);
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        QVariantMap tab;
        tab.insert(QStringLiteral("path"), settings.value(QStringLiteral("path")));
        tab.insert(QStringLiteral("cursorPos"), settings.value(QStringLiteral("cursorPos"), 0));
        tab.insert(QStringLiteral("editorScroll"), settings.value(QStringLiteral("editorScroll"), 0));
        tab.insert(QStringLiteral("previewScroll"), settings.value(QStringLiteral("previewScroll"), 0));
        tab.insert(QStringLiteral("mode"), settings.value(QStringLiteral("mode"), 0));
        tab.insert(QStringLiteral("previewVisible"), settings.value(QStringLiteral("previewVisible"), true));
        tab.insert(QStringLiteral("presentation"), settings.value(QStringLiteral("presentation"), 0));
        if (settings.contains(QStringLiteral("selectedDefinitionName"))) {
            tab.insert(QStringLiteral("selectedDefinitionName"), settings.value(QStringLiteral("selectedDefinitionName")));
            tab.insert(QStringLiteral("automaticallyDetectedName"), settings.value(QStringLiteral("automaticallyDetectedName")));
            tab.insert(QStringLiteral("languageSource"), settings.value(QStringLiteral("languageSource"), 0));
            if (settings.contains(QStringLiteral("lastNonMarkdownDefinitionName"))) {
                tab.insert(QStringLiteral("lastNonMarkdownDefinitionName"), settings.value(QStringLiteral("lastNonMarkdownDefinitionName")));
                tab.insert(QStringLiteral("lastNonMarkdownSource"), settings.value(QStringLiteral("lastNonMarkdownSource"), 0));
            }
        }
        const QString path = tab.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            const QString wordWrapKey = QStringLiteral("WordWrap/") + path;
            const QVariant wordWrap = settings.value(wordWrapKey);
            if (wordWrap.isValid())
                snapshot.m_values.insert(wordWrapKey, wordWrap);
        }
        snapshot.m_sessionTabs.append(std::move(tab));
    }
    settings.endArray();
    return snapshot;
}
