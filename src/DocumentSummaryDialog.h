#pragma once
#include <QDialog>
#include "DocumentSummary.h"

class QLabel;
class QVBoxLayout;

// Read-only snapshot view. Receives an immutable DocumentSummary rather than
// owning an editor or connecting to edit signals -- see MainWindow::showDocumentSummary().
class DocumentSummaryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DocumentSummaryDialog(QWidget* parent = nullptr);

    void setSummary(const DocumentSummary& summary);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyTitleBarTheme();
    void rebuild();

    QVBoxLayout* m_layout = nullptr;
    QWidget*     m_generalSection = nullptr;
    QWidget*     m_markdownSection = nullptr;
    DocumentSummary m_summary;
};
