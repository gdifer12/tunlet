#pragma once

#include <QString>

namespace tunlet::app {

QString defaultConfigPath();
QString expandUserPath(const QString &path);
QString resolveConfiguredPath(const QString &path, const QString &configRoute, const QString &fallbackBasePath = {});

}  // namespace tunlet::app
