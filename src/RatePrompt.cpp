#include "RatePrompt.h"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <appmodel.h>
#include <shobjidl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Services.Store.h>
#endif

namespace {

constexpr auto kLaunchCountKey = "rating/launchCount";
constexpr auto kFirstUseDateKey = "rating/firstUseDate";
constexpr auto kRatedKey = "rating/rated";
constexpr auto kDontAskAgainKey = "rating/dontAskAgain";
constexpr auto kLastPromptShownKey = "rating/lastPromptShown";
constexpr auto kLastPromptLaunchCountKey = "rating/lastPromptLaunchCount";
constexpr auto kPostponementCountKey = "rating/postponementCount";
constexpr auto kStoreRatingUrl = "ms-windows-store://review/?ProductId=9N4DSD4WNZ9D";

constexpr int kMinimumLaunches = 10;
constexpr int kMinimumDaysSinceFirstUse = 3;
constexpr int kDaysAfterMaybeLater = 7;
constexpr int kLaunchesAfterMaybeLater = 10;
constexpr int kMaximumPostponements = 3;

bool reviewFlowInProgress = false;

// Temporary UI-review switch. Keep this off in production: normal prompts are
// Store-only, eligibility-gated, and scheduled after a successful save/export.
constexpr bool kShowAtEveryStoreStartupForUiReview = false;

bool isPackaged()
{
#ifdef Q_OS_WIN
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
#else
    return false;
#endif
}

bool isEnabledForThisBuild()
{
    // The review override also permits the local fastmd.exe to display the
    // dialog for visual QA.  With the override off, this remains strictly an
    // MSIX/Store-only feature.
    return isPackaged() || kShowAtEveryStoreStartupForUiReview;
}

bool canPrompt(const QSettings& settings)
{
    if (settings.value(QLatin1String(kRatedKey), false).toBool()
        || settings.value(QLatin1String(kDontAskAgainKey), false).toBool()) {
        return false;
    }

    if (settings.value(QLatin1String(kPostponementCountKey), 0).toInt()
        >= kMaximumPostponements) {
        return false;
    }

    const int launches = settings.value(QLatin1String(kLaunchCountKey), 0).toInt();
    const QDate firstUse = QDate::fromString(
        settings.value(QLatin1String(kFirstUseDateKey)).toString(), Qt::ISODate);
    if (launches < kMinimumLaunches || !firstUse.isValid()
        || firstUse.daysTo(QDate::currentDate()) < kMinimumDaysSinceFirstUse) {
        return false;
    }

    const QDateTime lastPrompt = QDateTime::fromString(
        settings.value(QLatin1String(kLastPromptShownKey)).toString(), Qt::ISODate);
    if (!lastPrompt.isValid())
        return true;

    const int launchesAtLastPrompt = settings.value(
        QLatin1String(kLastPromptLaunchCountKey), launches).toInt();
    // "Whichever comes later" means both cooldowns must have elapsed.
    return lastPrompt.daysTo(QDateTime::currentDateTimeUtc()) >= kDaysAfterMaybeLater
        && launches - launchesAtLastPrompt >= kLaunchesAfterMaybeLater;
}

void openRatingFallback()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kStoreRatingUrl)));
}

void recordPostponement()
{
    QSettings settings;
    const int count = settings.value(QLatin1String(kPostponementCountKey), 0).toInt();
    settings.setValue(QLatin1String(kPostponementCountKey), count + 1);
}

void requestNativeStoreRating(QWidget* parent)
{
    if (reviewFlowInProgress)
        return;
    reviewFlowInProgress = true;

    // The local executable is allowed to show the prompt only for UI review.
    // It has no package identity, so never hand it to StoreContext: some Store
    // runtime versions fail this call unrecoverably instead of returning an
    // HRESULT.  Its Rate button still reaches the same review destination.
    if (!isPackaged()) {
        reviewFlowInProgress = false;
        openRatingFallback();
        return;
    }

#ifdef Q_OS_WIN
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Services::Store;

    try {
        const StoreContext context = StoreContext::GetDefault();
        context.as<::IInitializeWithWindow>()->Initialize(reinterpret_cast<HWND>(parent->winId()));
        const auto operation = context.RequestRateAndReviewAppAsync();
        QPointer<QWidget> parentPtr(parent);
        operation.Completed([parentPtr](const auto& completed, AsyncStatus status) {
            QMetaObject::invokeMethod(qApp, [parentPtr, completed, status]() {
                bool requestFailed = status != AsyncStatus::Completed;
                bool reviewSucceeded = false;
                if (!requestFailed) {
                    try {
                        const auto result = completed.GetResults();
                        reviewSucceeded = result.Status() == StoreRateAndReviewStatus::Succeeded;
                    } catch (const winrt::hresult_error&) {
                        requestFailed = true;
                    }
                }

                if (reviewSucceeded) {
                    QSettings().setValue(QLatin1String(kRatedKey), true);
                } else if (requestFailed) {
                    // The native API was unavailable or failed before it could
                    // report a Store result.  Use the supported URI fallback.
                    openRatingFallback();
                }

                reviewFlowInProgress = false;
            });
        });
        return;
    } catch (const winrt::hresult_error&) {
        // Older Store/runtime combinations can lack the API.  The product's
        // review URI is the supported, non-blocking fallback.
    }
#else
    Q_UNUSED(parent);
#endif
    reviewFlowInProgress = false;
    openRatingFallback();
}

void showPrompt(QWidget* parent, bool forceForReview)
{
    static bool promptVisible = false;
    if (!parent || promptVisible || reviewFlowInProgress || !isEnabledForThisBuild())
        return;

    QSettings settings;
    if (!forceForReview && !canPrompt(settings))
        return;

    promptVisible = true;
    const int launches = settings.value(QLatin1String(kLaunchCountKey), 0).toInt();
    settings.setValue(QLatin1String(kLastPromptShownKey), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.setValue(QLatin1String(kLastPromptLaunchCountKey), launches);

    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Enjoying FastMD?"));
    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setModal(true);
    dialog->setObjectName(QStringLiteral("ratePrompt"));

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto* text = new QLabel(QObject::tr("If FastMD is helping your writing, would you rate it on Microsoft Store?"), dialog);
    text->setWordWrap(true);
    text->setFixedWidth(360);
    layout->addWidget(text);

    layout->addSpacing(12);
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    auto* rate = new QPushButton(QObject::tr("Rate FastMD"), dialog);
    auto* later = new QPushButton(QObject::tr("Maybe later"), dialog);
    auto* never = new QPushButton(QObject::tr("Don't ask again"), dialog);
    // Keep the buttons compact at high DPI while reserving enough width for
    // each label; the identical height gives the action row a clean baseline.
    rate->setFixedSize(110, 30);
    later->setFixedSize(98, 30);
    never->setFixedSize(132, 30);
    rate->setDefault(true);
    buttons->addWidget(rate);
    buttons->addWidget(later);
    buttons->addWidget(never);
    layout->addLayout(buttons);

    QObject::connect(dialog, &QDialog::finished, dialog, [dialog](int result) {
        if (result == QDialog::Rejected) {
            const QSettings settings;
            if (!settings.value(QLatin1String(kRatedKey), false).toBool()
                && !settings.value(QLatin1String(kDontAskAgainKey), false).toBool()) {
                recordPostponement();
            }
        }
        Q_UNUSED(dialog);
        promptVisible = false;
    });
    QObject::connect(rate, &QPushButton::clicked, dialog, [dialog, parent]() {
        dialog->accept();
        requestNativeStoreRating(parent);
    });
    QObject::connect(later, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(never, &QPushButton::clicked, dialog, [dialog]() {
        QSettings().setValue(QLatin1String(kDontAskAgainKey), true);
        dialog->reject();
    });
    dialog->open();
}

void schedule(QWidget* parent, bool forceForReview, int delayMs = 750)
{
    if (!isEnabledForThisBuild() || !parent)
        return;
    QPointer<QWidget> parentPtr(parent);
    QTimer::singleShot(delayMs, parent, [parentPtr, forceForReview]() {
        if (parentPtr)
            showPrompt(parentPtr, forceForReview);
    });
}

} // namespace

void RatePrompt::recordLaunch()
{
    if (!isEnabledForThisBuild())
        return;
    QSettings settings;
    settings.setValue(QLatin1String(kLaunchCountKey),
                      settings.value(QLatin1String(kLaunchCountKey), 0).toInt() + 1);
    if (!settings.contains(QLatin1String(kFirstUseDateKey)))
        settings.setValue(QLatin1String(kFirstUseDateKey), QDate::currentDate().toString(Qt::ISODate));
}

void RatePrompt::afterSuccessfulSave(QWidget* parent)
{
    schedule(parent, false);
}

void RatePrompt::afterSuccessfulExport(QWidget* parent)
{
    schedule(parent, false);
}

void RatePrompt::scheduleStartupReview(QWidget* parent)
{
    schedule(parent, kShowAtEveryStoreStartupForUiReview, 10000);
}
