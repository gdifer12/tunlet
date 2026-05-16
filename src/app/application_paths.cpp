#include "app/application_paths.hpp"

#include <QDir>
#include <QFileInfo>

namespace tunlet::app {

QString defaultConfigPath() {
    return QDir::homePath() + "/.config/tunlet/config.yaml";
}

QString expandUserPath(const QString &path) {
    if (path.startsWith("~/")) {
        return QDir::homePath() + path.mid(1);
    }

    return path;
}

QString resolveConfiguredPath(const QString &path, const QString &configRoute, const QString &fallbackBasePath) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QString expandedPath = expandUserPath(trimmedPath);
    if (QFileInfo(expandedPath).isAbsolute()) {
        return QDir::cleanPath(expandedPath);
    }

    QString basePath = configRoute.trimmed().isEmpty() ? fallbackBasePath.trimmed() : configRoute.trimmed();
    basePath = expandUserPath(basePath);
    if (basePath.isEmpty()) {
        return QDir::cleanPath(expandedPath);
    }

    return QDir(QDir::cleanPath(basePath)).filePath(expandedPath);
}

}  // namespace tunlet::app
