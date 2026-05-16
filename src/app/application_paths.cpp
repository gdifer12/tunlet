#include "app/application_paths.hpp"

#include <QDir>

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

}  // namespace tunlet::app
