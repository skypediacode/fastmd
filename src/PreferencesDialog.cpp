#include "PreferencesDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

const QStringList kAssociatedExtensions = {
    QStringLiteral(".txt"),
    QStringLiteral(".md"),
    QStringLiteral(".markdown"),
};

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

} // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(420, 300);
    setSizeGripEnabled(false);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    m_tabWidget = new QTabWidget(this);

    auto* generalTab = new QWidget(m_tabWidget);
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, tr("General"));

    auto* fileAssocTab = new QWidget(m_tabWidget);
    setupFileAssociationTab(fileAssocTab);
    m_tabWidget->addTab(fileAssocTab, tr("File Associations"));

    mainLayout->addWidget(m_tabWidget);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    QSettings s;
    m_restoreSessionCheckbox->setChecked(s.value(QStringLiteral("restoreSessionOnStartup"), false).toBool());
    updateFileAssociationUi();

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        QSettings s;
        s.setValue(QStringLiteral("restoreSessionOnStartup"), m_restoreSessionCheckbox->isChecked());
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

void PreferencesDialog::setupGeneralTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    auto* sectionTitle = new QLabel(tr("Startup Options"), tab);
    QFont titleFont = sectionTitle->font();
    titleFont.setBold(true);
    sectionTitle->setFont(titleFont);
    layout->addWidget(sectionTitle);

    m_restoreSessionCheckbox = new QCheckBox(tr("Restore previous session on startup"), tab);
    layout->addWidget(m_restoreSessionCheckbox);

    layout->addStretch();
}

void PreferencesDialog::setupFileAssociationTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    auto* sectionTitle = new QLabel(tr("File Associations"), tab);
    QFont titleFont = sectionTitle->font();
    titleFont.setBold(true);
    sectionTitle->setFont(titleFont);
    layout->addWidget(sectionTitle);

    auto* description = new QLabel(
        tr("Register FastMD with Windows for .txt, .md, and .markdown files."), tab);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_associateFilesButton = new QPushButton(
        tr("Associate supported file types with FastMD"), tab);
    connect(m_associateFilesButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        if (associateSupportedFileTypes(&errorMessage)) {
            if (m_fileAssociationStatusLabel) {
                m_fileAssociationStatusLabel->setText(tr(
                    "FastMD is now available in Open With for .txt, .md, and .markdown on this Windows account."));
            }
            return;
        }

        QMessageBox::warning(this, tr("File Associations"),
            tr("FastMD could not be registered for file associations.\n%1").arg(errorMessage));
    });
    layout->addWidget(m_associateFilesButton);

    m_fileAssociationStatusLabel = new QLabel(tab);
    m_fileAssociationStatusLabel->setWordWrap(true);
    layout->addWidget(m_fileAssociationStatusLabel);

    layout->addStretch();
}

void PreferencesDialog::updateFileAssociationUi()
{
#ifdef Q_OS_WIN
    if (m_associateFilesButton)
        m_associateFilesButton->setEnabled(true);
    if (m_fileAssociationStatusLabel) {
        m_fileAssociationStatusLabel->setText(tr(
            "Registers FastMD as an available app for .txt, .md, and .markdown on this Windows account."));
    }
#else
    if (m_associateFilesButton)
        m_associateFilesButton->setEnabled(false);
    if (m_fileAssociationStatusLabel) {
        m_fileAssociationStatusLabel->setText(tr(
            "File associations are not supported on this platform yet."));
    }
#endif
}

bool PreferencesDialog::associateSupportedFileTypes(QString* errorMessage)
{
#ifndef Q_OS_WIN
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

    for (const QString& ext : kAssociatedExtensions) {
        const QString openWithKey = QStringLiteral("Software\\Classes\\") + ext
            + QStringLiteral("\\OpenWithProgids");
        if (!setRegistryValue(HKEY_CURRENT_USER, openWithKey, progId, QString())) {
            if (errorMessage)
                *errorMessage = tr("Unable to register %1 with FastMD.").arg(ext);
            return false;
        }
    }

    return true;
#endif
}
