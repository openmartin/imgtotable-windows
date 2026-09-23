#pragma once

#include <QString>
#include <filesystem>

namespace PlatformUtils {
std::filesystem::path toPath(const QString& path);
QString modelPath();
}
