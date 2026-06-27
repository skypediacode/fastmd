#include <QApplication>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("FastMD");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("FastMD");

    QIcon appIcon(QStringLiteral(":/fastmd.ico"));
    app.setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}
