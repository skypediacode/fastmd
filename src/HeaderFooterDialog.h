#pragma once

#include "ExportManager.h"
#include "HeaderFooter.h"

#include <QDialog>

class CompactComboBox;
class HeaderFooterPreview;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// File > Header & Footer...: the six slot templates, their two band switches
// and a live thumbnail of the configured page. Page size and margins are only
// read here; they stay owned by the native Page Setup dialog.
class HeaderFooterDialog : public QDialog
{
    Q_OBJECT
public:
    HeaderFooterDialog(const ExportManager::PdfExportOptions& page, const HeaderFooterContext& sample,
                       QWidget* parent = nullptr);

    HeaderFooterOptions options() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setOptions(const HeaderFooterOptions& options);
    void applyPreset(int index);
    void bandToggled(bool header, bool on);
    void insertToken(const QString& token);
    void refresh();
    void applyTitleBarTheme();

    QLineEdit* m_header[HeaderFooterOptions::SlotCount] = {};
    QLineEdit* m_footer[HeaderFooterOptions::SlotCount] = {};
    QCheckBox* m_headerCheck = nullptr;
    QCheckBox* m_footerCheck = nullptr;
    CompactComboBox* m_preset = nullptr;
    HeaderFooterPreview* m_preview = nullptr;
    QLabel* m_marginHint = nullptr;
    QList<QPushButton*> m_tokenButtons;
    QLineEdit* m_lastEdit = nullptr;
    QMarginsF m_marginsMm;
    bool m_updating = false;
};
