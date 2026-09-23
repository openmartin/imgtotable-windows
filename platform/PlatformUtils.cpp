#include "PlatformUtils.h"

#include <QCoreApplication>
#include <QDir>

namespace PlatformUtils {
std::filesystem::path toPath(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}

QString modelPath() {
    const QDir directory(QCoreApplication::applicationDirPath());
    return directory.filePath(QStringLiteral(IMGTOTABLE_MODEL_RELATIVE_PATH));
}
}
