#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QTabWidget;

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    bool restoreSessionOnStartup() const;
    void setRestoreSessionOnStartup(bool enable);

private:
    void setupGeneralTab(QWidget* tab);
    void setupFileAssociationTab(QWidget* tab);
    void updateFileAssociationUi();
    bool associateSupportedFileTypes(QString* errorMessage = nullptr);

    QTabWidget*  m_tabWidget = nullptr;
    QCheckBox*   m_restoreSessionCheckbox = nullptr;
    QPushButton* m_associateFilesButton = nullptr;
    QLabel*      m_fileAssociationStatusLabel = nullptr;
};
