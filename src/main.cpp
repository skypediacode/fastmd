#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("FastMD");
    app.setApplicationVersion("1.5.0");
    app.setOrganizationName("FastMD");

    QIcon appIcon(QStringLiteral(":/fastmd.ico"));
    app.setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

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
