#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QEvent>
#include "MainWindow.h"
#include "StartupProfiler.h"

int main(int argc, char* argv[])
{
    StartupProfiler::init();
    FASTMD_MARK("main() entered");

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("FastMD");
    app.setApplicationVersion("1.5.0");
    app.setOrganizationName("FastMD");
    FASTMD_MARK("QApplication constructed");

    QIcon appIcon(QStringLiteral(":/fastmd.ico"));
    app.setWindowIcon(appIcon);
    FASTMD_MARK("window icon loaded");

    MainWindow window;
    window.setWindowIcon(appIcon);
    FASTMD_MARK("MainWindow constructed");

    // First-paint probe: the first paint event delivered to the window is the
    // closest in-process proxy for "the user sees something".
    if (StartupProfiler::enabled()) {
        struct FirstPaintProbe : QObject {
            using QObject::QObject;
            bool eventFilter(QObject* o, QEvent* e) override {
                if (!fired && e->type() == QEvent::Paint) {
                    fired = true;
                    FASTMD_MARK("first window paint");
                    o->removeEventFilter(this);
                    deleteLater();
                }
                return false;
            }
            bool fired = false;
        };
        window.installEventFilter(new FirstPaintProbe(&window));
        // First event-loop idle: queued behind everything posted during show().
        QTimer::singleShot(0, &app, [] { FASTMD_MARK("first event-loop idle"); });
    }

    window.show();
    FASTMD_MARK("window.show() returned");

    const QStringList args = app.arguments();
    QStringList filesToOpen;
    for (int i = 1; i < args.size(); ++i) {
        const QFileInfo fileInfo(args.at(i));
        if (fileInfo.exists() && fileInfo.isFile())
            filesToOpen.append(fileInfo.absoluteFilePath());
    }

    if (!filesToOpen.isEmpty()) {
        QTimer::singleShot(0, &window, [filesToOpen, &window]() {
            for (const QString& path : filesToOpen) {
                QMetaObject::invokeMethod(
                    &window, "openFilePath", Qt::DirectConnection, Q_ARG(QString, path));
            }
        });
    }

    return app.exec();
}
