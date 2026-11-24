#include "PreferencesDialog.h"
#include <QDesktopServices>
#include <QUrl>
#include "CompactComboBox.h"
#include "SearchableComboBox.h"
#include "FastMDFileTypes.h"
#include "FontPreferences.h"
#include "SyntaxDefinitionResolver.h"
#include "TerminalCloseAllPreferences.h"
#include "ThemedMessageBox.h"

#include <array>
#include <functional>
#include <initializer_list>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QRadioButton>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#endif
#include <algorithm>

namespace {

QStringList markdownTextAssociationExtensions()
{
    QStringList result;
#define FASTMD_APPEND_EXT(ext) result.append(QStringLiteral(".") + QStringLiteral(ext));
    FASTMD_WRITING_EXTENSIONS(FASTMD_APPEND_EXT)
#undef FASTMD_APPEND_EXT
    return result;
}

constexpr std::array<int, 13> kAutoSaveDelayOptions = {
    500, 1000, 2000, 3000, 5000, 10000, 30000, 60000,
    2 * 60 * 1000, 5 * 60 * 1000, 10 * 60 * 1000, 15 * 60 * 1000, 30 * 60 * 1000
};

int nearestAutoSaveDelay(int value)
{
    int nearest = kAutoSaveDelayOptions.front();
    int nearestDistance = qAbs(value - nearest);
    for (const int candidate : kAutoSaveDelayOptions) {
        const int distance = qAbs(value - candidate);
        if (distance < nearestDistance) {
            nearest = candidate;
            nearestDistance = distance;
        }
    }
    return nearest;
}

#ifdef Q_OS_WIN
bool setRegistryValue(HKEY root, const QString& subKey, const QString& valueName, const QString& value)
{
    HKEY key = nullptr;
    const std::wstring subKeyW = subKey.toStdWString();
    const LONG createResult = RegCreateKeyExW(
        root, subKeyW.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (createResult != ERROR_SUCCESS)
        return false;

    const std::wstring valueNameW = valueName.isEmpty() ? std::wstring() : valueName.toStdWString();
    const std::wstring valueW = value.toStdWString();
    const wchar_t* namePtr = valueName.isEmpty() ? nullptr : valueNameW.c_str();
    const BYTE* data = reinterpret_cast<const BYTE*>(valueW.c_str());
    const DWORD bytes = static_cast<DWORD>((valueW.size() + 1) * sizeof(wchar_t));
    const LONG setResult = RegSetValueExW(key, namePtr, 0, REG_SZ, data, bytes);
    RegCloseKey(key);
    return setResult == ERROR_SUCCESS;
}
#endif

// Installed families for the font pickers. Read only when Preferences opens;
// the startup warm-up worker has usually enumerated them already, so this is a
// walk over a cached list, not a font scan. Vertical "@" variants and private
// system families are not choices a user means to make.
QStringList installedFontFamilies()
{
    QStringList families;
    for (const QString& family : QFontDatabase::families()) {
        if (!family.startsWith(u'@') && !QFontDatabase::isPrivateFamily(family))
            families.append(family);
    }
    return families;
}

// Lists the families of one surface, its built-in font first. A saved family
// that is no longer installed stays listed (and selected) under a note rather
// than being silently replaced. The dropdown searches this list; Default stays
// listed whatever is typed.
QComboBox* newFontCombo(QWidget* parent, const QString& defaultLabel, const QStringList& families)
{
    auto* combo = new SearchableComboBox(parent);
    combo->setProperty("preferencesCombo", true);
    combo->setMinimumWidth(260);
    combo->setFixedHeight(32);
    combo->setMaxVisibleItems(12);
    combo->setFirstRowPinned(true);
    combo->setSearchPlaceholderText(QCoreApplication::translate("PreferencesDialog", "Search fonts"));
    combo->addItem(defaultLabel, QString());
    for (const QString& family : families)
        combo->addItem(family, family);
    return combo;
}

void selectFontFamily(QComboBox* combo, const QString& family)
{
    const QString name = FontPreferences::normalizedFamily(family);
    int index = combo->findData(name);
    if (index < 0) {
        combo->insertItem(1, QCoreApplication::translate("PreferencesDialog", "%1 (not installed)").arg(name), name);
        index = 1;
    }
    combo->setCurrentIndex(index);
}

// One Document Spacing preset list. The stored value rides in the item data, the
// label is the number with its unit, and the default is marked in it.
QComboBox* newSpacingCombo(QWidget* parent, int defaultValue, std::initializer_list<int> presets,
                           const std::function<QString(int)>& label)
{
    auto* combo = new CompactComboBox(parent);
    combo->setProperty("preferencesCombo", true);
    combo->setMinimumWidth(260);
    combo->setFixedHeight(32);
    combo->setMaxVisibleItems(8);
    for (const int preset : presets) {
        const QString text = label(preset);
        combo->addItem(preset == defaultValue
                           ? QCoreApplication::translate("PreferencesDialog", "%1 (Default)").arg(text)
                           : text,
                       preset);
    }
    return combo;
}

void selectSpacing(QComboBox* combo, int value, int fallback)
{
    int index = combo->findData(value);
    if (index < 0)
        index = combo->findData(fallback);
    combo->setCurrentIndex(index);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(540, 360);
    setSizeGripEnabled(false);

#ifdef Q_OS_WIN
    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL enabled = isDarkTheme ? TRUE : FALSE;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))) ||
        SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)))) {
        const COLORREF captionColor = isDarkTheme ? RGB(0x20, 0x21, 0x22) : RGB(0xf3, 0xf3, 0xf3);
        const COLORREF textColor = isDarkTheme ? RGB(0xbf, 0xbf, 0xbf) : RGB(0x20, 0x20, 0x20);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("preferencesTabs"));
    m_tabWidget->tabBar()->setExpanding(false);

    auto* generalTab = new QWidget(m_tabWidget);
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, tr("General"));

    auto* editorTab = new QWidget(m_tabWidget);
    setupEditorTab(editorTab);
    m_tabWidget->addTab(editorTab, tr("Editor"));

    auto* fontsTab = new QWidget(m_tabWidget);
    setupFontsTab(fontsTab);
    m_tabWidget->addTab(fontsTab, tr("Typography"));

    auto* fileAssocTab = new QWidget(m_tabWidget);
    setupFileAssociationTab(fileAssocTab);
    m_tabWidget->addTab(fileAssocTab, tr("File Associations"));

    mainLayout->addWidget(m_tabWidget);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    QSettings s;
    m_restoreSessionCheckbox->setChecked(s.value(QStringLiteral("restoreSessionOnStartup"), false).toBool());
    m_closeAppOnLastTabCloseCheckbox->setChecked(s.value(QStringLiteral("closeAppOnLastTabClose"), true).toBool());
    m_confirmCloseAllTerminalsCheckbox->setChecked(
        TerminalCloseAllPreferences::confirmationEnabled(s));
    m_autoSaveCheckbox->setChecked(s.value(QStringLiteral("autoSave/enabled"), false).toBool());
    setAutoSaveDelayMs(s.value(QStringLiteral("autoSave/delayMs"), 2000).toInt());
    m_crashRecoveryCheckbox->setChecked(s.value(QStringLiteral("autoSave/crashRecovery"), true).toBool());
    m_openPdfExternallyCheckbox->setChecked(s.value(QStringLiteral("files/openPdfExternally"), false).toBool());
    m_openHtmlExternallyCheckbox->setChecked(s.value(QStringLiteral("files/openHtmlExternally"), false).toBool());
    m_openImagesExternallyCheckbox->setChecked(s.value(QStringLiteral("files/openImagesExternally"), false).toBool());
    m_showExportSuccessDialogCheckbox->setChecked(
        s.value(QStringLiteral("export/showSuccessDialog"), true).toBool());
    setHomeFolder(s.value(QStringLiteral("workspace/homeFolder"),
                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString());
    updateFileAssociationUi();

    int defaultNewDocModeInt = s.value(QStringLiteral("defaultNewDocMode"), static_cast<int>(EditorMode::Markdown)).toInt();
    if (defaultNewDocModeInt == static_cast<int>(EditorMode::Markdown)) {
        m_defaultModeMarkdownRadio->setChecked(true);
    } else {
        m_defaultModePlainTextRadio->setChecked(true);
    }
    m_highlightCurrentLineCheckbox->setChecked(s.value(QStringLiteral("editor/highlightCurrentLine"), true).toBool());
    m_renderSingleLineBreaksCheckbox->setChecked(s.value(QStringLiteral("editor/renderSingleLineBreaks"), true).toBool());

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        const QString selectedHomeFolder = homeFolder();
        if (!QFileInfo(selectedHomeFolder).isDir()) {
            themedWarning(this, tr("Home Folder"),
                          tr("The Home Folder must be an existing folder."));
            return;
        }
        QSettings s;
        s.setValue(QStringLiteral("restoreSessionOnStartup"), m_restoreSessionCheckbox->isChecked());
        s.setValue(QStringLiteral("closeAppOnLastTabClose"), m_closeAppOnLastTabCloseCheckbox->isChecked());
        TerminalCloseAllPreferences::setConfirmationEnabled(
            s, m_confirmCloseAllTerminalsCheckbox->isChecked());
        s.setValue(QStringLiteral("autoSave/enabled"), m_autoSaveCheckbox->isChecked());
        s.setValue(QStringLiteral("autoSave/delayMs"), autoSaveDelayMs());
        s.setValue(QStringLiteral("autoSave/crashRecovery"), m_crashRecoveryCheckbox->isChecked());
        s.setValue(QStringLiteral("files/openPdfExternally"), m_openPdfExternallyCheckbox->isChecked());
        s.setValue(QStringLiteral("files/openHtmlExternally"), m_openHtmlExternallyCheckbox->isChecked());
        s.setValue(QStringLiteral("files/openImagesExternally"), m_openImagesExternallyCheckbox->isChecked());
        s.setValue(QStringLiteral("export/showSuccessDialog"), m_showExportSuccessDialogCheckbox->isChecked());
        s.setValue(QStringLiteral("workspace/homeFolder"), selectedHomeFolder);
        s.setValue(QStringLiteral("defaultNewDocMode"), m_defaultModeMarkdownRadio->isChecked() ?
            static_cast<int>(EditorMode::Markdown) : static_cast<int>(EditorMode::PlainText));
        s.setValue(QStringLiteral("editor/highlightCurrentLine"), m_highlightCurrentLineCheckbox->isChecked());
        s.setValue(QStringLiteral("editor/renderSingleLineBreaks"), m_renderSingleLineBreaksCheckbox->isChecked());
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool PreferencesDialog::restoreSessionOnStartup() const
{
    return m_restoreSessionCheckbox->isChecked();
}

void PreferencesDialog::setRestoreSessionOnStartup(bool enable)
{
    m_restoreSessionCheckbox->setChecked(enable);
}

void PreferencesDialog::revealTarget(PreferencesTargets::Id target)
{
    const QList<QWidget*> candidates = targetWidgets(target);
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const QWidget* page = m_tabWidget->widget(i);
        if (std::any_of(candidates.cbegin(), candidates.cend(),
                        [page](const QWidget* w) { return page->isAncestorOf(w); })) {
            m_tabWidget->setCurrentIndex(i);
            break;
        }
    }
    // QDialog chooses its own focus widget as it is shown, and only a shown
    // dialog can say which candidate is really visible, so this waits for
    // exec() to put it on screen. OtherFocusReason, unlike ShortcutFocusReason,
    // does not set WA_KeyboardFocusChange, so the style draws no focus frame
    // until the user presses Tab; Space and Tab still start from the target.
    QTimer::singleShot(0, this, [this, target]() {
        for (QWidget* widget : targetWidgets(target)) {
            if (widget->isVisible() && widget->isEnabled()) {
                widget->setFocus(Qt::OtherFocusReason);
                return;
            }
        }
    });
}

QList<QWidget*> PreferencesDialog::targetWidgets(PreferencesTargets::Id target) const
{
    using PreferencesTargets::Id;
    switch (target) {
    case Id::RestoreSession:           return { m_restoreSessionCheckbox };
    case Id::CloseAppOnLastTab:        return { m_closeAppOnLastTabCloseCheckbox };
    case Id::ConfirmCloseAllTerminals: return { m_confirmCloseAllTerminalsCheckbox };
    case Id::HomeFolder:               return { m_homeFolderEdit };
    case Id::DefaultSaveFolder:        return { m_defaultSaveFolderEdit };
    case Id::OpenPdfExternally:        return { m_openPdfExternallyCheckbox };
    case Id::OpenHtmlExternally:       return { m_openHtmlExternallyCheckbox };
    case Id::OpenImagesExternally:     return { m_openImagesExternallyCheckbox };
    case Id::ShowExportSuccessDialog:  return { m_showExportSuccessDialogCheckbox };
    case Id::HighlightCurrentLine:     return { m_highlightCurrentLineCheckbox };
    case Id::RenderSingleLineBreaks:   return { m_renderSingleLineBreaksCheckbox };
    // The checked radio button is the one Tab would land on.
    case Id::DefaultEditorMode:
        return { m_defaultModePlainTextRadio->isChecked() ? m_defaultModePlainTextRadio
                                                          : m_defaultModeMarkdownRadio };
    case Id::AutoSave:                 return { m_autoSaveCheckbox };
    case Id::AutoSaveDelay:            return { m_autoSaveDelayCombo, m_autoSaveCheckbox };
    case Id::CrashRecovery:            return { m_crashRecoveryCheckbox };
    // With no spelling languages installed the checkbox is disabled and the
    // way forward is installing one.
    case Id::SpellCheck:               return { m_spellCheckCheckbox, m_spellManageButton };
    case Id::EditorFont:               return { m_editorFontCombo };
    case Id::PreviewFont:              return { m_previewFontCombo };
    case Id::PrintFont:                return { m_printFontCombo };
    case Id::LineSpacing:              return { m_lineSpacingCombo };
    case Id::ParagraphSpacing:         return { m_paragraphSpacingCombo };
    case Id::ListSpacing:              return { m_listSpacingCombo };
    // Its buttons write to the registry. A focused push button becomes the
    // dialog's default, so Enter would press one instead of OK: the tab
    // header takes focus, and the page is reached through it.
    case Id::FileAssociations:         return { m_tabWidget->tabBar(), m_associateMarkdownTextButton };
    case Id::Count:                    break;
    }
    return {};
}

bool PreferencesDialog::closeAppOnLastTabClose() const
{
    return m_closeAppOnLastTabCloseCheckbox->isChecked();
}

void PreferencesDialog::setCloseAppOnLastTabClose(bool enable)
{
    m_closeAppOnLastTabCloseCheckbox->setChecked(enable);
}

QString PreferencesDialog::defaultSaveFolder() const
{
    return m_defaultSaveFolderEdit->text().trimmed();
}

void PreferencesDialog::setDefaultSaveFolder(const QString& path)
{
    m_defaultSaveFolderEdit->setText(path);
}

QString PreferencesDialog::homeFolder() const
{
    const QString path = m_homeFolderEdit->text().trimmed();
    return path.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : path;
}

void PreferencesDialog::setHomeFolder(const QString& path)
{
    m_homeFolderEdit->setText(path);
}

bool PreferencesDialog::openPdfExternally() const { return m_openPdfExternallyCheckbox->isChecked(); }
void PreferencesDialog::setOpenPdfExternally(bool enable) { m_openPdfExternallyCheckbox->setChecked(enable); }
bool PreferencesDialog::openHtmlExternally() const { return m_openHtmlExternallyCheckbox->isChecked(); }
void PreferencesDialog::setOpenHtmlExternally(bool enable) { m_openHtmlExternallyCheckbox->setChecked(enable); }
bool PreferencesDialog::openImagesExternally() const { return m_openImagesExternallyCheckbox->isChecked(); }
void PreferencesDialog::setOpenImagesExternally(bool enable) { m_openImagesExternallyCheckbox->setChecked(enable); }
bool PreferencesDialog::showExportSuccessDialog() const { return m_showExportSuccessDialogCheckbox->isChecked(); }
void PreferencesDialog::setShowExportSuccessDialog(bool enable) { m_showExportSuccessDialogCheckbox->setChecked(enable); }

EditorMode PreferencesDialog::defaultNewDocMode() const
{
    return m_defaultModeMarkdownRadio->isChecked() ? EditorMode::Markdown : EditorMode::PlainText;
}

void PreferencesDialog::setDefaultNewDocMode(EditorMode mode)
{
    if (mode == EditorMode::Markdown) {
        m_defaultModeMarkdownRadio->setChecked(true);
    } else {
        m_defaultModePlainTextRadio->setChecked(true);
    }
}

bool PreferencesDialog::highlightCurrentLineEnabled() const
{
    return m_highlightCurrentLineCheckbox->isChecked();
}

void PreferencesDialog::setHighlightCurrentLineEnabled(bool enable)
{
    m_highlightCurrentLineCheckbox->setChecked(enable);
}

bool PreferencesDialog::renderSingleLineBreaksEnabled() const
{
    return m_renderSingleLineBreaksCheckbox->isChecked();
}

void PreferencesDialog::setRenderSingleLineBreaksEnabled(bool enable)
{
    m_renderSingleLineBreaksCheckbox->setChecked(enable);
}

bool PreferencesDialog::autoSaveEnabled() const { return m_autoSaveCheckbox->isChecked(); }
void PreferencesDialog::setAutoSaveEnabled(bool enable) { m_autoSaveCheckbox->setChecked(enable); }
int PreferencesDialog::autoSaveDelayMs() const
{
    return m_autoSaveDelayCombo ? m_autoSaveDelayCombo->currentData().toInt() : 2000;
}

void PreferencesDialog::setAutoSaveDelayMs(int delayMs)
{
    if (!m_autoSaveDelayCombo)
        return;
    const int supportedDelay = nearestAutoSaveDelay(delayMs);
    const int index = m_autoSaveDelayCombo->findData(supportedDelay);
    if (index >= 0)
        m_autoSaveDelayCombo->setCurrentIndex(index);
}
bool PreferencesDialog::crashRecoveryEnabled() const { return m_crashRecoveryCheckbox->isChecked(); }
void PreferencesDialog::setCrashRecoveryEnabled(bool enable) { m_crashRecoveryCheckbox->setChecked(enable); }

void PreferencesDialog::setupGeneralTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(16);

    QFont titleFont;
    titleFont.setBold(true);

    // Startup section
    auto* startupGroup = new QVBoxLayout();
    startupGroup->setSpacing(6);
    auto* startupTitle = new QLabel(tr("Startup"), tab);
    startupTitle->setFont(titleFont);
    startupGroup->addWidget(startupTitle);
    m_restoreSessionCheckbox = new QCheckBox(tr("Restore previous session on startup"), tab);
    startupGroup->addWidget(m_restoreSessionCheckbox);
    layout->addLayout(startupGroup);

    // Tabs section
    auto* tabsGroup = new QVBoxLayout();
    tabsGroup->setSpacing(6);
    auto* tabsTitle = new QLabel(tr("Tabs"), tab);
    tabsTitle->setFont(titleFont);
    tabsGroup->addWidget(tabsTitle);
    m_closeAppOnLastTabCloseCheckbox = new QCheckBox(tr("Close the application when the last tab is closed"), tab);
    m_closeAppOnLastTabCloseCheckbox->setToolTip(tr("If disabled, closing the last tab will replace it with a new document instead of closing the application."));
    tabsGroup->addWidget(m_closeAppOnLastTabCloseCheckbox);
    layout->addLayout(tabsGroup);

    // Terminal section
    auto* terminalGroup = new QVBoxLayout();
    terminalGroup->setSpacing(6);
    auto* terminalTitle = new QLabel(tr("Terminal"), tab);
    terminalTitle->setFont(titleFont);
    terminalGroup->addWidget(terminalTitle);
    m_confirmCloseAllTerminalsCheckbox = new QCheckBox(
        tr("Confirm before closing all terminal sessions"), tab);
    terminalGroup->addWidget(m_confirmCloseAllTerminalsCheckbox);
    layout->addLayout(terminalGroup);

    // Files section
    auto* filesGroup = new QVBoxLayout();
    filesGroup->setSpacing(6);
    auto* filesTitle = new QLabel(tr("Files"), tab);
    filesTitle->setFont(titleFont);
    filesGroup->addWidget(filesTitle);

    auto* folderGroup = new QVBoxLayout();
    folderGroup->setSpacing(6);

    auto* homeFolderLabel = new QLabel(tr("Home folder:"), tab);
    folderGroup->addWidget(homeFolderLabel);

    auto* homeFolderRow = new QHBoxLayout();
    homeFolderRow->setSpacing(6);
    m_homeFolderEdit = new QLineEdit(tab);
    m_homeFolderEdit->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    homeFolderRow->addWidget(m_homeFolderEdit);

    auto* homeBrowseButton = new QPushButton(tr("Browse…"), tab);
    homeBrowseButton->setFixedWidth(80);
    connect(homeBrowseButton, &QPushButton::clicked, this, [this]() {
        const QString current = homeFolder();
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Select Home Folder"), current,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!chosen.isEmpty())
            m_homeFolderEdit->setText(QDir::toNativeSeparators(chosen));
    });
    homeFolderRow->addWidget(homeBrowseButton);
    folderGroup->addLayout(homeFolderRow);

    auto* folderLabel = new QLabel(tr("Default save folder:"), tab);
    folderGroup->addWidget(folderLabel);

    auto* folderRow = new QHBoxLayout();
    folderRow->setSpacing(6);
    m_defaultSaveFolderEdit = new QLineEdit(tab);
    m_defaultSaveFolderEdit->setPlaceholderText(tr("(not set — remembers last used location)"));
    folderRow->addWidget(m_defaultSaveFolderEdit);

    auto* browseButton = new QPushButton(tr("Browse…"), tab);
    browseButton->setFixedWidth(80);
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString current = m_defaultSaveFolderEdit->text().trimmed();
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Select Default Save Folder"),
            current.isEmpty() ? QDir::homePath() : current,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!chosen.isEmpty())
            m_defaultSaveFolderEdit->setText(QDir::toNativeSeparators(chosen));
    });
    folderRow->addWidget(browseButton);
    folderGroup->addLayout(folderRow);
    filesGroup->addLayout(folderGroup);

    m_openPdfExternallyCheckbox = new QCheckBox(tr("Open PDF files in the default system application"), tab);
    filesGroup->addWidget(m_openPdfExternallyCheckbox);
    m_openHtmlExternallyCheckbox = new QCheckBox(tr("Open HTML files in the default system application"), tab);
    filesGroup->addWidget(m_openHtmlExternallyCheckbox);
    m_openImagesExternallyCheckbox = new QCheckBox(tr("Open image files in the default system application"), tab);
    filesGroup->addWidget(m_openImagesExternallyCheckbox);
    m_showExportSuccessDialogCheckbox = new QCheckBox(
        tr("Show a dialog after exporting a file"), tab);
    m_showExportSuccessDialogCheckbox->setToolTip(
        tr("Lets you open the exported file or its folder when an export finishes."));
    filesGroup->addWidget(m_showExportSuccessDialogCheckbox);

    layout->addLayout(filesGroup);

    // Application Reset section
    auto* resetGroup = new QVBoxLayout();
    resetGroup->setSpacing(6);
    auto* resetTitle = new QLabel(tr("Application"), tab);
    resetTitle->setFont(titleFont);
    resetGroup->addWidget(resetTitle);
    
    auto* resetButton = new QPushButton(tr("Restore Default Application Settings..."), tab);
    resetButton->setFixedWidth(260);
    connect(resetButton, &QPushButton::clicked, this, &PreferencesDialog::onResetApplicationSettings);
    resetGroup->addWidget(resetButton);
    layout->addLayout(resetGroup);

    layout->addStretch();
}

void PreferencesDialog::onResetApplicationSettings()
{
    ThemedMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Restore Default Application Settings"));
    box.setText(tr("This will reset all FastMD settings to their default values.\n"
                   "Your document files will NOT be modified or deleted.\n\n"
                   "The following will be reset, including but not limited to:\n"
                   "- Preferences\n"
                   "- Theme\n"
                   "- Window layout and size\n"
                   "- Session settings\n"
                   "- Recent files\n"
                   "- Auto Save settings\n"
                   "- Default editor mode\n"
                   "- File-specific Word Wrap preferences\n"
                   "- Any other application preferences stored via QSettings"
                   ));
    auto* resetBtn = box.addButton(tr("Reset"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(QMessageBox::Cancel);
    bindDialogKey(&box, resetBtn, Qt::Key_R);
    bindDialogKey(&box, box.buttons().constLast(), Qt::Key_C);

    box.exec();
    if (box.clickedButton() == resetBtn) {
        QSettings s;
        s.clear();
        s.sync();

        ThemedMessageBox restartBox(this);
        restartBox.setIcon(QMessageBox::Information);
        restartBox.setWindowTitle(tr("Restart Required"));
        restartBox.setText(tr("FastMD must restart for all changes to take effect."));
        auto* restartNowBtn = restartBox.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
        auto* laterBtn = restartBox.addButton(tr("Later"), QMessageBox::RejectRole);
        restartBox.setDefaultButton(restartNowBtn);
        bindDialogKey(&restartBox, restartNowBtn, Qt::Key_R);
        bindDialogKey(&restartBox, laterBtn, Qt::Key_L);
        restartBox.exec();

        emit applicationReset(restartBox.clickedButton() == restartNowBtn);
        accept(); // Close the preferences dialog
    }
}

void PreferencesDialog::setupFileAssociationTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* sectionTitle = new QLabel(tr("File Associations"), tab);
    QFont titleFont = sectionTitle->font();
    titleFont.setBold(true);
    sectionTitle->setFont(titleFont);
    layout->addWidget(sectionTitle);

    auto* description = new QLabel(
        tr("Register FastMD with Windows as an available app in Open With. "
           "Windows keeps control of which app actually opens each file type by default."), tab);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* narrowDescription = new QLabel(
        tr(".md, .markdown, .mdown, .mkd, .txt, and .text"), tab);
    narrowDescription->setWordWrap(true);
    layout->addWidget(narrowDescription);

    m_associateMarkdownTextButton = new QPushButton(
        tr("Associate Markdown and Text Files with FastMD"), tab);
    connect(m_associateMarkdownTextButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        if (associateExtensions(markdownTextAssociationExtensions(), &errorMessage)) {
            if (m_fileAssociationStatusLabel) {
                m_fileAssociationStatusLabel->setText(tr(
                    "FastMD is now available in Open With for .md, .markdown, .mdown, .mkd, .txt, and .text on this Windows account."));
            }
            return;
        }

        themedWarning(this, tr("File Associations"),
            tr("FastMD could not be registered for file associations.\n%1").arg(errorMessage));
    });
    layout->addWidget(m_associateMarkdownTextButton);

    auto* broadDescription = new QLabel(
        tr("Registers every extension advertised by installed FastMD syntax definitions "
           "(C++, Python, JavaScript, JSON, and hundreds of others)."), tab);
    broadDescription->setWordWrap(true);
    layout->addWidget(broadDescription);

    m_associateAllSupportedButton = new QPushButton(
        tr("Associate All Supported Text Files with FastMD"), tab);
    connect(m_associateAllSupportedButton, &QPushButton::clicked, this, [this]() {
        QStringList extensions = markdownTextAssociationExtensions();
        for (const QString& ext : SyntaxDefinitionResolver::associableExtensions()) {
            if (!extensions.contains(ext, Qt::CaseInsensitive))
                extensions.append(ext);
        }
        std::sort(extensions.begin(), extensions.end());

        QString errorMessage;
        if (associateExtensions(extensions, &errorMessage)) {
            if (m_fileAssociationStatusLabel) {
                m_fileAssociationStatusLabel->setText(tr(
                    "FastMD is now available in Open With for %1 supported text file types on this Windows account.")
                        .arg(extensions.size()));
            }
            return;
        }

        themedWarning(this, tr("File Associations"),
            tr("FastMD could not be registered for file associations.\n%1").arg(errorMessage));
    });
    layout->addWidget(m_associateAllSupportedButton);

    m_fileAssociationStatusLabel = new QLabel(tab);
    m_fileAssociationStatusLabel->setWordWrap(true);
    layout->addWidget(m_fileAssociationStatusLabel);

    layout->addStretch();
}

void PreferencesDialog::setupEditorTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    QFont titleFont;
    titleFont.setBold(true);
    auto* title = new QLabel(tr("Editor"), tab);
    title->setFont(titleFont);
    layout->addWidget(title);

    m_highlightCurrentLineCheckbox = new QCheckBox(tr("Highlight current line"), tab);
    layout->addWidget(m_highlightCurrentLineCheckbox);

    m_renderSingleLineBreaksCheckbox = new QCheckBox(
        tr("Render single line breaks as visible line breaks"), tab);
    m_renderSingleLineBreaksCheckbox->setToolTip(tr(
        "Display a single newline inside a paragraph as a visible line break. "
        "Disable this for standard CommonMark/GitHub-style paragraph rendering."));
    layout->addWidget(m_renderSingleLineBreaksCheckbox);

    layout->addSpacing(6);
    auto* defaultModeGroup = new QVBoxLayout();
    defaultModeGroup->setSpacing(6);
    auto* defaultModeLabel = new QLabel(tr("Default editor mode:"), tab);
    defaultModeGroup->addWidget(defaultModeLabel);

    auto* radioLayout = new QHBoxLayout();
    radioLayout->setSpacing(24);
    m_defaultModeMarkdownRadio = new QRadioButton(tr("Markdown"), tab);
    m_defaultModePlainTextRadio = new QRadioButton(tr("Code/Text"), tab);
    radioLayout->addWidget(m_defaultModeMarkdownRadio);
    radioLayout->addWidget(m_defaultModePlainTextRadio);
    radioLayout->addStretch();

    defaultModeGroup->addLayout(radioLayout);
    layout->addLayout(defaultModeGroup);

    auto* autoSaveGroup = new QVBoxLayout();
    autoSaveGroup->setSpacing(6);
    auto* autoSaveTitle = new QLabel(tr("Auto Save"), tab);
    autoSaveTitle->setFont(titleFont);
    autoSaveGroup->addWidget(autoSaveTitle);
    m_autoSaveCheckbox = new QCheckBox(tr("Enable Auto Save"), tab);
    autoSaveGroup->addWidget(m_autoSaveCheckbox);
    auto* delayRow = new QHBoxLayout();
    delayRow->setAlignment(Qt::AlignVCenter);
    auto* delayLabel = new QLabel(tr("Auto Save delay:"), tab);
    delayLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    delayRow->addWidget(delayLabel, 0, Qt::AlignVCenter);
    m_autoSaveDelayCombo = new CompactComboBox(tab);
    m_autoSaveDelayCombo->setProperty("preferencesCombo", true);
    m_autoSaveDelayCombo->setMinimumWidth(220);
    m_autoSaveDelayCombo->setFixedHeight(32);
    m_autoSaveDelayCombo->addItem(tr("500 ms"), 500);
    m_autoSaveDelayCombo->addItem(tr("1 second"), 1000);
    m_autoSaveDelayCombo->addItem(tr("2 seconds"), 2000);
    m_autoSaveDelayCombo->addItem(tr("3 seconds"), 3000);
    m_autoSaveDelayCombo->addItem(tr("5 seconds"), 5000);
    m_autoSaveDelayCombo->addItem(tr("10 seconds"), 10000);
    m_autoSaveDelayCombo->addItem(tr("30 seconds"), 30000);
    m_autoSaveDelayCombo->addItem(tr("1 minute"), 60000);
    m_autoSaveDelayCombo->addItem(tr("2 minutes"), 2 * 60 * 1000);
    m_autoSaveDelayCombo->addItem(tr("5 minutes"), 5 * 60 * 1000);
    m_autoSaveDelayCombo->addItem(tr("10 minutes"), 10 * 60 * 1000);
    m_autoSaveDelayCombo->addItem(tr("15 minutes"), 15 * 60 * 1000);
    m_autoSaveDelayCombo->addItem(tr("30 minutes"), 30 * 60 * 1000);
    delayRow->addWidget(m_autoSaveDelayCombo, 0, Qt::AlignVCenter);
    delayRow->addStretch();
    autoSaveGroup->addLayout(delayRow);
    m_crashRecoveryCheckbox = new QCheckBox(tr("Enable Crash Recovery"), tab);
    m_crashRecoveryCheckbox->setToolTip(tr("Keep private recovery snapshots for unexpected shutdowns."));
    autoSaveGroup->addWidget(m_crashRecoveryCheckbox);
    connect(m_autoSaveCheckbox, &QCheckBox::toggled,
            m_autoSaveDelayCombo, &QWidget::setEnabled);
    m_autoSaveDelayCombo->setEnabled(m_autoSaveCheckbox->isChecked());
    layout->addLayout(autoSaveGroup);

    auto* spellGroup = new QVBoxLayout();
    spellGroup->setSpacing(6);
    auto* spellTitle = new QLabel(tr("Spell Check"), tab);
    spellTitle->setFont(titleFont);
    spellGroup->addWidget(spellTitle);

    m_spellCheckCheckbox = new QCheckBox(tr("Enable spell checking"), tab);
    m_spellCheckCheckbox->setToolTip(tr(
        "Underline misspelled prose in Markdown and plain text documents. "
        "Uses the spelling languages installed in Windows."));
    spellGroup->addWidget(m_spellCheckCheckbox);

    auto* spellRow = new QHBoxLayout();
    spellRow->setAlignment(Qt::AlignVCenter);
    m_spellLanguageLabel = new QLabel(tr("Language:"), tab);
    m_spellLanguageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    spellRow->addWidget(m_spellLanguageLabel, 0, Qt::AlignVCenter);
    m_spellLanguageCombo = new CompactComboBox(tab);
    m_spellLanguageCombo->setProperty("preferencesCombo", true);
    m_spellLanguageCombo->setMinimumWidth(220);
    m_spellLanguageCombo->setFixedHeight(32);
    spellRow->addWidget(m_spellLanguageCombo, 0, Qt::AlignVCenter);
    spellRow->addStretch();
    spellGroup->addLayout(spellRow);

    // Shown instead of the language row when Windows has no spelling data
    // installed at all, so the dialog never presents an empty dropdown.
    m_spellUnavailableLabel = new QLabel(
        tr("No spell-check languages are installed in Windows."), tab);
    m_spellUnavailableLabel->setObjectName(QStringLiteral("spellUnavailableLabel"));
    m_spellUnavailableLabel->setWordWrap(true);
    m_spellUnavailableLabel->setVisible(false);
    spellGroup->addWidget(m_spellUnavailableLabel);

    auto* manageRow = new QHBoxLayout();
    m_spellManageButton = new QPushButton(tr("Manage Windows language settings..."), tab);
    m_spellManageButton->setObjectName(QStringLiteral("spellManageButton"));
    connect(m_spellManageButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:regionlanguage")));
    });
    manageRow->addWidget(m_spellManageButton);
    manageRow->addStretch();
    spellGroup->addLayout(manageRow);

    connect(m_spellCheckCheckbox, &QCheckBox::toggled, this, [this]() { updateSpellCheckUi(); });
    layout->addLayout(spellGroup);

    updateSpellCheckUi();
    layout->addStretch();
}

void PreferencesDialog::setupFontsTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    QFont titleFont;
    titleFont.setBold(true);
    auto* title = new QLabel(tr("Fonts"), tab);
    title->setFont(titleFont);
    layout->addWidget(title);

    const QStringList families = installedFontFamilies();
    m_editorFontCombo = newFontCombo(
        tab, tr("Default (%1)").arg(FontPreferences::DefaultEditorFamily), families);
    m_previewFontCombo = newFontCombo(tab, tr("Default (Segoe UI)"), families);
    m_printFontCombo = newFontCombo(tab, tr("Default (Segoe UI)"), families);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    QList<QLabel*> rowLabels;
    QList<QComboBox*> rowCombos;
    const auto addRow = [&](int row, const QString& label, QComboBox* combo) {
        auto* text = new QLabel(label, tab);
        text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        text->setBuddy(combo);
        rowLabels.append(text);
        rowCombos.append(combo);
        grid->addWidget(text, row, 0, Qt::AlignVCenter);
        grid->addWidget(combo, row, 1, Qt::AlignVCenter);
    };
    addRow(0, tr("Editor font:"), m_editorFontCombo);
    addRow(1, tr("Preview font:"), m_previewFontCombo);
    addRow(2, tr("Print / PDF font:"), m_printFontCombo);
    grid->setColumnStretch(2, 1);
    layout->addLayout(grid);

    auto* note = new QLabel(tr(
        "Preview applies to Browser Preview and HTML export too. Code blocks, math "
        "and diagrams keep their own fonts. Vietnamese letters a font does not have "
        "are drawn from Segoe UI. Choose Default to restore the original font."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    layout->addSpacing(6);
    auto* spacingTitle = new QLabel(tr("Document Spacing"), tab);
    spacingTitle->setFont(titleFont);
    layout->addWidget(spacingTitle);

    namespace DS = DocumentSpacing;
    m_lineSpacingCombo = newSpacingCombo(tab, DS::DefaultLine,
        { DS::LinePresets[0], DS::LinePresets[1], DS::LinePresets[2], DS::LinePresets[3],
          DS::LinePresets[4], DS::LinePresets[5], DS::LinePresets[6] },
        [](int value) { return DS::hundredthsText(value); });
    m_paragraphSpacingCombo = newSpacingCombo(tab, DS::DefaultParagraph,
        { DS::ParagraphPresets[0], DS::ParagraphPresets[1], DS::ParagraphPresets[2],
          DS::ParagraphPresets[3], DS::ParagraphPresets[4], DS::ParagraphPresets[5],
          DS::ParagraphPresets[6] },
        [](int value) { return QCoreApplication::translate("PreferencesDialog", "%1 pt").arg(value); });
    m_listSpacingCombo = newSpacingCombo(tab, DS::DefaultList,
        { DS::ListPresets[0], DS::ListPresets[1], DS::ListPresets[2], DS::ListPresets[3],
          DS::ListPresets[4] },
        [](int value) {
            return QCoreApplication::translate("PreferencesDialog", "%1 em").arg(DS::hundredthsText(value));
        });

    auto* spacingGrid = new QGridLayout();
    spacingGrid->setHorizontalSpacing(12);
    spacingGrid->setVerticalSpacing(8);
    const auto addSpacingRow = [&](int row, const QString& label, QComboBox* combo) {
        auto* text = new QLabel(label, tab);
        text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        text->setBuddy(combo);
        rowLabels.append(text);
        rowCombos.append(combo);
        spacingGrid->addWidget(text, row, 0, Qt::AlignVCenter);
        spacingGrid->addWidget(combo, row, 1, Qt::AlignVCenter);
    };
    addSpacingRow(0, tr("Line spacing:"), m_lineSpacingCombo);
    addSpacingRow(1, tr("Paragraph spacing:"), m_paragraphSpacingCombo);
    addSpacingRow(2, tr("List spacing:"), m_listSpacingCombo);
    spacingGrid->setColumnStretch(2, 1);
    layout->addLayout(spacingGrid);

    int labelWidth = 0;
    for (const QLabel* l : std::as_const(rowLabels))
        labelWidth = qMax(labelWidth, l->sizeHint().width());
    for (QLabel* l : std::as_const(rowLabels))
        l->setFixedWidth(labelWidth);
    for (QComboBox* c : std::as_const(rowCombos)) {
        c->setMinimumWidth(0);
        c->setFixedWidth(300);
    }

    auto* spacingNote = new QLabel(tr(
        "Applies to Preview, Browser Preview, HTML export, Markdown Print and PDF. "
        "The Editor, Plain Text printing and Pandoc exports are unaffected."), tab);
    spacingNote->setWordWrap(true);
    layout->addWidget(spacingNote);

    layout->addStretch();
}

QString PreferencesDialog::editorFontFamily() const { return m_editorFontCombo->currentData().toString(); }
void PreferencesDialog::setEditorFontFamily(const QString& family) { selectFontFamily(m_editorFontCombo, family); }
QString PreferencesDialog::previewFontFamily() const { return m_previewFontCombo->currentData().toString(); }
void PreferencesDialog::setPreviewFontFamily(const QString& family) { selectFontFamily(m_previewFontCombo, family); }
QString PreferencesDialog::printFontFamily() const { return m_printFontCombo->currentData().toString(); }
void PreferencesDialog::setPrintFontFamily(const QString& family) { selectFontFamily(m_printFontCombo, family); }

DocumentSpacing::Settings PreferencesDialog::documentSpacing() const
{
    return DocumentSpacing::normalized({ m_lineSpacingCombo->currentData().toInt(),
                                         m_paragraphSpacingCombo->currentData().toInt(),
                                         m_listSpacingCombo->currentData().toInt() });
}

void PreferencesDialog::setDocumentSpacing(const DocumentSpacing::Settings& spacing)
{
    const DocumentSpacing::Settings normalized = DocumentSpacing::normalized(spacing);
    selectSpacing(m_lineSpacingCombo, normalized.line, DocumentSpacing::DefaultLine);
    selectSpacing(m_paragraphSpacingCombo, normalized.paragraph, DocumentSpacing::DefaultParagraph);
    selectSpacing(m_listSpacingCombo, normalized.list, DocumentSpacing::DefaultList);
}

void PreferencesDialog::updateSpellCheckUi()
{
    if (!m_spellCheckCheckbox || !m_spellLanguageCombo)
        return;
    const bool hasLanguages = m_spellLanguageCombo->count() > 0;
    m_spellCheckCheckbox->setEnabled(hasLanguages);
    m_spellLanguageLabel->setVisible(hasLanguages);
    m_spellLanguageCombo->setVisible(hasLanguages);
    m_spellUnavailableLabel->setVisible(!hasLanguages);
    m_spellLanguageCombo->setEnabled(hasLanguages && m_spellCheckCheckbox->isChecked());
}

void PreferencesDialog::setAvailableSpellLanguages(const QVector<QPair<QString, QString>>& languages)
{
    if (!m_spellLanguageCombo)
        return;
    const QString current = spellCheckLanguage();
    m_spellLanguageCombo->clear();
    for (const QPair<QString, QString>& language : languages)
        m_spellLanguageCombo->addItem(language.second, language.first);
    if (!current.isEmpty())
        setSpellCheckLanguage(current);
    updateSpellCheckUi();
}

bool PreferencesDialog::spellCheckEnabled() const
{
    return m_spellCheckCheckbox && m_spellCheckCheckbox->isEnabled()
        && m_spellCheckCheckbox->isChecked();
}

void PreferencesDialog::setSpellCheckEnabled(bool enable)
{
    if (m_spellCheckCheckbox)
        m_spellCheckCheckbox->setChecked(enable);
    updateSpellCheckUi();
}

QString PreferencesDialog::spellCheckLanguage() const
{
    if (!m_spellLanguageCombo || m_spellLanguageCombo->currentIndex() < 0)
        return {};
    return m_spellLanguageCombo->currentData().toString();
}

void PreferencesDialog::setSpellCheckLanguage(const QString& tag)
{
    if (!m_spellLanguageCombo || tag.isEmpty())
        return;
    const int index = m_spellLanguageCombo->findData(tag);
    if (index >= 0)
        m_spellLanguageCombo->setCurrentIndex(index);
}

void PreferencesDialog::updateFileAssociationUi()
{
#ifdef Q_OS_WIN
    if (m_associateMarkdownTextButton)
        m_associateMarkdownTextButton->setEnabled(true);
    if (m_associateAllSupportedButton)
        m_associateAllSupportedButton->setEnabled(true);
    if (m_fileAssociationStatusLabel) {
        m_fileAssociationStatusLabel->setText(tr(
            "Registers FastMD as an available app in Open With on this Windows account."));
    }
#else
    if (m_associateMarkdownTextButton)
        m_associateMarkdownTextButton->setEnabled(false);
    if (m_associateAllSupportedButton)
        m_associateAllSupportedButton->setEnabled(false);
    if (m_fileAssociationStatusLabel) {
        m_fileAssociationStatusLabel->setText(tr(
            "File associations are not supported on this platform yet."));
    }
#endif
}

bool PreferencesDialog::associateExtensions(const QStringList& extensions, QString* errorMessage)
{
#ifndef Q_OS_WIN
    Q_UNUSED(extensions);
    if (errorMessage)
        *errorMessage = tr("File associations are only supported on Windows.");
    return false;
#else
    const QString exePath = QCoreApplication::applicationFilePath();
    if (exePath.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("Unable to determine the FastMD executable path.");
        return false;
    }

    const QString command = QStringLiteral("\"") + QDir::toNativeSeparators(exePath)
        + QStringLiteral("\" \"%1\"");
    const QString progId = QStringLiteral("FastMD.Document");
    const QString progIdRoot = QStringLiteral("Software\\Classes\\") + progId;
    const QString capabilitiesRoot = QStringLiteral("Software\\FastMD\\Capabilities");

    if (!setRegistryValue(HKEY_CURRENT_USER, capabilitiesRoot,
            QStringLiteral("ApplicationName"), QStringLiteral("FastMD")) ||
        !setRegistryValue(HKEY_CURRENT_USER, capabilitiesRoot,
            QStringLiteral("ApplicationDescription"),
            tr("FastMD Markdown and text editor")) ||
        !setRegistryValue(HKEY_CURRENT_USER, capabilitiesRoot,
            QStringLiteral("ApplicationIcon"),
            QDir::toNativeSeparators(exePath) + QStringLiteral(",0")) ||
        !setRegistryValue(HKEY_CURRENT_USER, QStringLiteral("Software\\RegisteredApplications"),
            QStringLiteral("FastMD"), capabilitiesRoot)) {
        if (errorMessage)
            *errorMessage = tr("Unable to register FastMD with Windows.");
        return false;
    }

    for (const QString& ext : extensions) {
        if (!setRegistryValue(HKEY_CURRENT_USER, capabilitiesRoot + QStringLiteral("\\FileAssociations"),
                ext, progId)) {
            if (errorMessage)
                *errorMessage = tr("Unable to register %1 with FastMD.").arg(ext);
            return false;
        }
    }

    if (!setRegistryValue(HKEY_CURRENT_USER, progIdRoot,
            QString(), tr("FastMD Document"))) {
        if (errorMessage)
            *errorMessage = tr("Unable to create the FastMD file type entry.");
        return false;
    }
    if (!setRegistryValue(HKEY_CURRENT_USER, progIdRoot + QStringLiteral("\\DefaultIcon"),
            QString(), QDir::toNativeSeparators(exePath) + QStringLiteral(",0"))) {
        if (errorMessage)
            *errorMessage = tr("Unable to set the FastMD file icon.");
        return false;
    }
    if (!setRegistryValue(HKEY_CURRENT_USER, progIdRoot + QStringLiteral("\\shell\\open\\command"),
            QString(), command)) {
        if (errorMessage)
            *errorMessage = tr("Unable to set the FastMD open command.");
        return false;
    }

    for (const QString& ext : extensions) {
        const QString openWithKey = QStringLiteral("Software\\Classes\\") + ext
            + QStringLiteral("\\OpenWithProgids");
        if (!setRegistryValue(HKEY_CURRENT_USER, openWithKey, progId, QString())) {
            if (errorMessage)
                *errorMessage = tr("Unable to register %1 with FastMD.").arg(ext);
            return false;
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#endif
}
