#pragma once

#include <QDialog>

class QListWidget;
class QTextBrowser;
class QLabel;

// Lists FastMD's third-party open-source components (name, version, license,
// homepage) and lets the user view each component's full license text.
class LicensesDialog : public QDialog {
    Q_OBJECT
public:
    explicit LicensesDialog(QWidget* parent = nullptr);

private:
    struct Component {
        QString name;
        QString version;
        QString license;
        QString homepage;
        QString licenseResourcePath; // qrc path to the full license text
    };

    void showComponent(int index);

    QList<Component> m_components;
    QListWidget*  m_list = nullptr;
    QLabel*       m_metaLabel = nullptr;
    QTextBrowser* m_licenseText = nullptr;
};
