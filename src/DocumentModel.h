#pragma once
#include "TextEncoding.h"
#include "GitRepository.h"
#include "DocumentSaveCoordinator.h"

#include <QString>
#include <QUuid>
#include <optional>

enum class EditorMode {
    PlainText,
    Markdown,
    Pdf,
    Html,
    Image,
    Svg,
    Diff, // MUST stay appended last -- persisted as a raw int (session "mode" key)
};

inline bool isViewerMode(EditorMode m) {
    return m == EditorMode::Pdf || m == EditorMode::Html || m == EditorMode::Image
        || m == EditorMode::Svg || m == EditorMode::Diff;
}

enum class LanguageSelectionSource {
    Automatic,
    Manual,
};

enum class MarkdownPresentation {
    Preview,
    MindMap,
};

// Remembers the last non-Markdown language chosen (or auto-detected) for a
// tab, so toggling Markdown -> Code/Text -> Markdown -> Code/Text restores
// what the user had before crossing into Markdown, per the Behavior Matrix
// in language-indicator-plan.md. An engaged instance with an empty
// definitionName means Plain Text (not "no memory yet").
struct NonMarkdownLanguageSelection {
    QString definitionName;
    LanguageSelectionSource source = LanguageSelectionSource::Automatic;
};

class DocumentModel {
public:
    explicit DocumentModel(int untitledIndex);
    ~DocumentModel();

    static EditorMode modeForPath(const QString& path);
    static QString pathKeyFor(const QString& path);
    static DocumentModel* openDocumentForKey(const QString& pathKey);

    const QString& filePath() const { return m_filePath; }
    const QString& pathKey() const { return m_pathKey; }
    void setFilePath(const QString& path);

    bool isViewer() const { return isViewerMode(m_mode); }

    bool isDirty() const { return m_dirty; }
    void setDirty(bool d) { m_dirty = d; }

    // Set while an external (out-of-FastMD) modification conflicts with
    // unsaved editor changes; Auto Save is suspended for the document until
    // the user resolves it via the reload/keep-mine dialog.
    bool hasExternalConflict() const { return m_externalConflict; }
    void setExternalConflict(bool c) { m_externalConflict = c; }

    EditorMode editorMode() const { return m_mode; }
    // General editable code should not call this directly -- use
    // MainWindow's centralized language operation instead, which derives
    // Markdown/PlainText from the selected definition name. This setter
    // remains for Pdf/Html viewer classification and session-migration
    // compatibility paths.
    void setEditorMode(EditorMode mode) { m_mode = mode; }

    bool markdownPreviewVisible() const { return m_markdownPreviewVisible; }
    void setMarkdownPreviewVisible(bool visible) { m_markdownPreviewVisible = visible; }
    MarkdownPresentation markdownPresentation() const { return m_markdownPresentation; }
    void setMarkdownPresentation(MarkdownPresentation presentation) { m_markdownPresentation = presentation; }

    // Line ending and encoding are per-document byte-level formats: the editor
    // buffer is always '\n'-separated QString, so they take effect on save.
    const QString& lineEnding() const { return m_lineEnding; }
    void setLineEnding(const QString& lineEnding) { m_lineEnding = lineEnding; }

    TextEncoding encoding() const { return m_encoding; }
    void setEncoding(TextEncoding encoding) { m_encoding = encoding; }

    // Bundled, application-owned content (the built-in User Guide). It lives in
    // the Qt resource system, so there is no file on disk for Save to overwrite
    // and no path for the session to restore: such a tab is never editable,
    // never dirty, never saved, and never remembered.
    bool isBundled() const { return m_bundled; }
    void setBundled(bool bundled) { m_bundled = bundled; }

    // Distinguishes the User Guide from Release Notes from What's New: all
    // three are bundled read-only documents, but each must stay a singleton
    // tab of its own rather than de-duplicating against "any bundled tab".
    const QString& bundledKind() const { return m_bundledKind; }
    void setBundledKind(const QString& kind) { m_bundledKind = kind; }

    // Per-tab Reading Mode, independent of MainWindow's window-level override:
    // the User Guide opens in the reading layout without turning Reading Mode on
    // for every other Markdown tab in the window.
    bool readingModeOverride() const { return m_readingModeOverride; }
    void setReadingModeOverride(bool enabled) { m_readingModeOverride = enabled; }

    // A Git diff document: a read-only, side-by-side view of two versions of a
    // file. It deliberately has no file path -- it is not the file, and giving
    // it one would displace the real editing tab in the open-document registry.
    bool isDiff() const { return m_diffSpec.has_value(); }
    const GitDiffSpec* diffSpec() const { return m_diffSpec ? &*m_diffSpec : nullptr; }
    void setDiffSpec(const GitDiffSpec& spec) { m_diffSpec = spec; }

    bool isUntitled() const { return m_filePath.isEmpty(); }
    QString displayName() const;
    // Names a tab that has no file path (the User Guide) instead of
    // "Untitled N". Ignored once a real path is set.
    void setDisplayNameOverride(const QString& name) { m_displayNameOverride = name; }
    const QString& recoveryId() const { return m_recoveryId; }
    const DocumentSaveCoordinator::StatePtr& saveState() const { return m_saveState; }

    // ---- Editable-document language state ---------------------------------
    // selectedDefinitionName: empty means Plain Text; "FastMD Markdown" means
    // Markdown. This is the authoritative source for editable EditorMode --
    // setting it also derives and stores editorMode() for non-viewer modes.
    const QString& selectedDefinitionName() const { return m_selectedDefinitionName; }
    void setSelectedDefinitionName(const QString& name);

    const QString& automaticallyDetectedName() const { return m_automaticallyDetectedName; }
    void setAutomaticallyDetectedName(const QString& name) { m_automaticallyDetectedName = name; }

    LanguageSelectionSource languageSelectionSource() const { return m_languageSource; }
    void setLanguageSelectionSource(LanguageSelectionSource source) { m_languageSource = source; }

    bool hasLastNonMarkdownSelection() const { return m_lastNonMarkdownSelection.has_value(); }
    NonMarkdownLanguageSelection lastNonMarkdownSelection() const {
        return m_lastNonMarkdownSelection.value_or(NonMarkdownLanguageSelection{});
    }
    void setLastNonMarkdownSelection(const NonMarkdownLanguageSelection& selection) {
        m_lastNonMarkdownSelection = selection;
    }
    void clearLastNonMarkdownSelection() { m_lastNonMarkdownSelection.reset(); }

private:
    void updatePathRegistry(const QString& previousKey);

    QString m_filePath;
    QString m_pathKey;
    QString m_displayNameOverride;
    std::optional<GitDiffSpec> m_diffSpec;
    bool    m_bundled      = false;
    QString m_bundledKind;
    bool    m_readingModeOverride = false;
    bool    m_dirty        = false;
    bool    m_externalConflict = false;
    int     m_untitledIndex;
    EditorMode m_mode      = EditorMode::Markdown;
    bool    m_markdownPreviewVisible = true;
    MarkdownPresentation m_markdownPresentation = MarkdownPresentation::Preview;
    QString m_lineEnding   = QStringLiteral("\r\n");
    TextEncoding m_encoding = TextEncoding::Utf8;
    QString m_recoveryId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DocumentSaveCoordinator::StatePtr m_saveState = std::make_shared<DocumentSaveCoordinator::State>();

    QString m_selectedDefinitionName = QStringLiteral("FastMD Markdown");
    QString m_automaticallyDetectedName = QStringLiteral("FastMD Markdown");
    LanguageSelectionSource m_languageSource = LanguageSelectionSource::Automatic;
    std::optional<NonMarkdownLanguageSelection> m_lastNonMarkdownSelection;
};
