#pragma once

#include <QDialog>
#include <QPair>
#include <QVector>
#include "DocumentModel.h"
#include "DocumentSpacing.h"
#include "PreferencesTargets.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QRadioButton;

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    // Selects the tab holding `target` and, once the dialog is on screen,
    // focuses its control. A control that is disabled or hidden at that point
    // gives way to the next candidate, the setting that controls it. Nothing
    // is changed: OK and Cancel keep their usual meaning.
    void revealTarget(PreferencesTargets::Id target);
    // The controls `target` leads to, the preferred one first.
    QList<QWidget*> targetWidgets(PreferencesTargets::Id target) const;

    bool    restoreSessionOnStartup() const;
    void    setRestoreSessionOnStartup(bool enable);

    bool    closeAppOnLastTabClose() const;
    void    setCloseAppOnLastTabClose(bool enable);

    QString defaultSaveFolder() const;
    void    setDefaultSaveFolder(const QString& path);
    QString homeFolder() const;
    void    setHomeFolder(const QString& path);
    bool openPdfExternally() const;
    void setOpenPdfExternally(bool enable);
    bool openHtmlExternally() const;
    void setOpenHtmlExternally(bool enable);
    bool openImagesExternally() const;
    void setOpenImagesExternally(bool enable);
    bool showExportSuccessDialog() const;
    void setShowExportSuccessDialog(bool enable);

    EditorMode defaultNewDocMode() const;
    void    setDefaultNewDocMode(EditorMode mode);

    bool    highlightCurrentLineEnabled() const;
    void    setHighlightCurrentLineEnabled(bool enable);
    bool renderSingleLineBreaksEnabled() const;
    void setRenderSingleLineBreaksEnabled(bool enable);

    bool autoSaveEnabled() const;
    void setAutoSaveEnabled(bool enable);
    int autoSaveDelayMs() const;
    void setAutoSaveDelayMs(int delayMs);
    bool crashRecoveryEnabled() const;
    void setCrashRecoveryEnabled(bool enable);

    // The available list is supplied by MainWindow (which owns the lazily
    // resolved SpellChecker state) rather than read here, so opening this
    // dialog is the only thing that ever triggers the enumeration.
    void setAvailableSpellLanguages(const QVector<QPair<QString, QString>>& languages);
    bool spellCheckEnabled() const;
    void setSpellCheckEnabled(bool enable);
    QString spellCheckLanguage() const;
    void setSpellCheckLanguage(const QString& tag);

    // Font families; empty means FastMD's built-in font for that surface.
    QString editorFontFamily() const;
    void setEditorFontFamily(const QString& family);
    QString previewFontFamily() const;
    void setPreviewFontFamily(const QString& family);
    QString printFontFamily() const;
    void setPrintFontFamily(const QString& family);

    // Document Spacing: the three presets the dialog offers (see DocumentSpacing.h).
    DocumentSpacing::Settings documentSpacing() const;
    void setDocumentSpacing(const DocumentSpacing::Settings& spacing);

private:
    void setupGeneralTab(QWidget* tab);
    void setupFileAssociationTab(QWidget* tab);
    void setupEditorTab(QWidget* tab);
    void setupFontsTab(QWidget* tab);
    void updateSpellCheckUi();
    void updateFileAssociationUi();
    bool associateExtensions(const QStringList& extensions, QString* errorMessage = nullptr);

signals:
    void applicationReset(bool restartNow);

private slots:
    void onResetApplicationSettings();

private:
    QTabWidget*  m_tabWidget = nullptr;
    QCheckBox*   m_restoreSessionCheckbox = nullptr;
    QCheckBox*   m_closeAppOnLastTabCloseCheckbox = nullptr;
    QCheckBox*   m_confirmCloseAllTerminalsCheckbox = nullptr;
    QLineEdit*   m_defaultSaveFolderEdit = nullptr;
    QLineEdit*   m_homeFolderEdit = nullptr;
    QCheckBox*   m_openPdfExternallyCheckbox = nullptr;
    QCheckBox*   m_openHtmlExternallyCheckbox = nullptr;
    QCheckBox*   m_openImagesExternallyCheckbox = nullptr;
    QCheckBox*   m_showExportSuccessDialogCheckbox = nullptr;
    QRadioButton* m_defaultModeMarkdownRadio = nullptr;
    QRadioButton* m_defaultModePlainTextRadio = nullptr;
    QCheckBox*   m_highlightCurrentLineCheckbox = nullptr;
    QCheckBox*   m_renderSingleLineBreaksCheckbox = nullptr;
    QPushButton* m_associateMarkdownTextButton = nullptr;
    QPushButton* m_associateAllSupportedButton = nullptr;
    QLabel*      m_fileAssociationStatusLabel = nullptr;
    QCheckBox*   m_autoSaveCheckbox = nullptr;
    QComboBox*   m_autoSaveDelayCombo = nullptr;
    QCheckBox*   m_crashRecoveryCheckbox = nullptr;
    QCheckBox*   m_spellCheckCheckbox = nullptr;
    QComboBox*   m_spellLanguageCombo = nullptr;
    QLabel*      m_spellLanguageLabel = nullptr;
    QLabel*      m_spellUnavailableLabel = nullptr;
    QPushButton* m_spellManageButton = nullptr;
    QComboBox*   m_editorFontCombo = nullptr;
    QComboBox*   m_previewFontCombo = nullptr;
    QComboBox*   m_printFontCombo = nullptr;
    QComboBox*   m_lineSpacingCombo = nullptr;
    QComboBox*   m_paragraphSpacingCombo = nullptr;
    QComboBox*   m_listSpacingCombo = nullptr;
};
