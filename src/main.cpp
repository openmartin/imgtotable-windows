#include "ui/MainWindow.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ImgToTable"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("ImgToTable"));
    app.setQuitOnLastWindowClosed(false);
    MainWindow window;
    window.show();
    return app.exec();
}
