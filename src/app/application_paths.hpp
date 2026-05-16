#pragma once

#include <QString>

namespace tunlet::app {

QString defaultConfigPath();
QString expandUserPath(const QString &path);

}  // namespace tunlet::app
