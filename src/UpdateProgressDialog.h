#pragma once
#include <QDialog>

class QLabel;
class QProgressBar;

// Small modal, non-closable progress dialog used to narrate Microsoft Store
// update phases (checking / installing) so the app never appears to just
// "disappear" mid-update. Two or three short lines of status text plus a
// progress indicator; no buttons, no close box.
//
// The user cannot dismiss this dialog (no close box, Escape/Alt+F4 blocked) --
// only dismiss() closes it. Always call dismiss() when the operation this
// dialog narrates finishes, on every code path (success, failure, exception),
// before showing any follow-up dialog.
class UpdateProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateProgressDialog(QWidget* parent, const QString& title,
                                   const QString& line1, const QString& line2 = QString(),
                                   const QString& line3 = QString(),
                                   bool determinate = false);

    void setProgress(double progress);

    // Closes and schedules deletion of the dialog. Safe to call more than
    // once (e.g. from both a completion handler and a QPointer-guarded
    // cleanup path); subsequent calls are no-ops once hidden.
    void dismiss();

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
    void showEvent(QShowEvent* event) override;

private:
    void applyTitleBarTheme();

    QProgressBar* m_progressBar = nullptr;
    bool m_dismissRequested = false;
};
