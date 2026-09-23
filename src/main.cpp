#include "core/InferenceEngine.h"
#include "platform/PlatformUtils.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ImgToTable"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    InferenceEngine engine;
    const auto path = PlatformUtils::modelPath();
    if (!engine.loadModel(PlatformUtils::toPath(path))) {
        qWarning().noquote() << "Model initialization failed:" << path
                             << QString::fromStdString(engine.lastError());
    } else {
        qInfo().noquote() << "Loaded CPU model:" << path;
    }
    MainWindow window(engine);
    window.show();
    return app.exec();
}
