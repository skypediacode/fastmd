#pragma once

class QWidget;

// Store-only, persisted rating request flow.  All entry points are no-ops for
// unpackaged / non-Windows builds, so portable and developer builds never show
// Store UI or accumulate Store-rating state.
namespace RatePrompt {
    void recordLaunch();
    void afterSuccessfulSave(QWidget* parent);
    void afterSuccessfulExport(QWidget* parent);
    void scheduleStartupReview(QWidget* parent);
}
